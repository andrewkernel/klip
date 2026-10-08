param(
  [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$GamePid,
  [Parameter(Mandatory)][ValidatePattern('^[^"\r\n]+$')][string]$WindowTitle,
  [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$')][string]$Name,
  [ValidateSet('balanced','performance')][string]$Quality='balanced',
  [ValidateSet(60,120)][int]$Fps=60,
  [switch]$LimitGameCapture,
  [string]$SceneDescription='not recorded',
  [string]$Executable='build-obs-Release-gpu-bench/bin/64bit/Klip.exe'
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$game=Get-Process -Id $GamePid -ErrorAction Stop
$identity=$game.StartTime.ToUniversalTime().Ticks
if($game.MainWindowTitle -ne $WindowTitle){throw 'The supplied title must match the live game window exactly'}
$matchingWindows=@(Get-Process -Name $game.ProcessName | Where-Object MainWindowTitle -eq $WindowTitle)
if($matchingWindows.Count -ne 1){throw 'The game title/executable is ambiguous; close duplicate instances before testing'}
if(Get-Process Klip,obs64 -ErrorAction SilentlyContinue){throw 'Close Klip and OBS before measuring the source-only baseline'}
$exe=(Resolve-Path -LiteralPath $Executable).Path
$pm=& "$PSScriptRoot/prepare-presentmon.ps1"
$root=Join-Path $repo "work/$Name"
if(Test-Path -LiteralPath $root){throw 'Choose a fresh name; existing evidence is preserved'}
New-Item -ItemType Directory $root | Out-Null
$gameLabel="$WindowTitle  [$([IO.Path]::GetFileName($game.Path))]"
@('capture_mode=game_window',"preferred_game_title=`"$gameLabel`"",
  'output_width=1920','output_height=1080',"target_fps=$Fps",'clip_duration_seconds=15',
  'rolling_buffer_seconds=30','rolling_buffer_bytes=268435456','desktop_audio_enabled=true',
  'microphone_enabled=false','desktop_audio_gain=1','audio_bitrate=192000','capture_cursor=false',
  'obs_encoder_id="obs_nvenc_h264_tex"','obs_cq=18',"encoder_quality=$Quality",
  "obs_limit_game_capture_fps=$($LimitGameCapture.IsPresent.ToString().ToLowerInvariant())",
  'static_overlay_enabled=false','live_overlay_enabled=false') | Set-Content "$root/settings.ini"
$phases=[Collections.Generic.List[object]]::new()
$capture=$null;$observer=$null;$previousRoot=$env:KLIP_DATA_ROOT
$valid=$false;$failure=$null
function AssertSameGame {
  $current=Get-Process -Id $GamePid -ErrorAction Stop
  if($current.StartTime.ToUniversalTime().Ticks -ne $identity){throw 'The game process identity changed'}
}
function WaitGame([int]$Seconds) {
  $deadline=[DateTime]::UtcNow.AddSeconds($Seconds)
  while([DateTime]::UtcNow -lt $deadline){AssertSameGame; Start-Sleep -Milliseconds 250}
}
try {
  $session="KlipGame-$([Guid]::NewGuid().ToString('N'))"
  $args="--process_id $GamePid --output_file `"$root/presentmon.csv`" --qpc_time --no_console_stats --no_track_input --terminate_on_proc_exit --timed 130 --terminate_after_timed --session_name $session"
  $observer=Start-Process $pm -ArgumentList $args -WindowStyle Hidden -RedirectStandardOutput "$root/presentmon.log" -RedirectStandardError "$root/presentmon-errors.log" -PassThru
  WaitGame 5
  $start=[Diagnostics.Stopwatch]::GetTimestamp(); WaitGame 15
  $phases.Add(@{name='source-before';start_qpc=$start;end_qpc=[Diagnostics.Stopwatch]::GetTimestamp()})
  $env:KLIP_DATA_ROOT=$root
  $capture=Start-Process $exe -WorkingDirectory (Split-Path $exe) -ArgumentList '--obs-background-benchmark' -WindowStyle Hidden -PassThru
  $deadline=[DateTime]::UtcNow.AddSeconds(25)
  $ready=$false
  while([DateTime]::UtcNow -lt $deadline){
    AssertSameGame
    if($capture.HasExited){throw "Capture exited before readiness: $($capture.ExitCode)"}
    if((Test-Path "$root/klip.log") -and (Select-String -LiteralPath "$root/klip.log" -Pattern 'BACKGROUND BENCHMARK READY' -Quiet)){$ready=$true;break}
    Start-Sleep -Milliseconds 250
  }
  if(-not $ready){throw 'Game capture did not become ready; no display-capture substitution will be made'}
  WaitGame 8
  $start=[Diagnostics.Stopwatch]::GetTimestamp()
  & "$PSScriptRoot/sample-capture-process.ps1" -CapturePid $capture.Id -SourcePid $GamePid -EvidenceRoot $root -Seconds 15
  $phases.Add(@{name='source-with-capture';start_qpc=$start;end_qpc=[Diagnostics.Stopwatch]::GetTimestamp()})
  $resources=Get-Content "$root/replay-resources.json" -Raw | ConvertFrom-Json
  if($resources.valid -ne $true){throw "Invalid resource interval: $($resources.invalid_reason)"}
  $deadline=[DateTime]::UtcNow.AddSeconds(40)
  while(-not $capture.HasExited -and [DateTime]::UtcNow -lt $deadline){WaitGame 1}
  if(-not $capture.HasExited -or $capture.ExitCode -ne 0){throw 'Capture benchmark did not finish successfully'}
  if(-not (Select-String -LiteralPath "$root/klip.log" -Pattern 'BACKGROUND BENCHMARK PASSED' -Quiet)){throw 'Replay save was not confirmed'}
  $media=@(Get-ChildItem "$root/Clips" -Filter '*.mkv' -ErrorAction Stop)
  if($media.Count -ne 1 -or $media[0].Length -le 0){throw 'Expected one nonempty saved replay'}
  WaitGame 5
  $start=[Diagnostics.Stopwatch]::GetTimestamp(); WaitGame 15
  $phases.Add(@{name='source-after';start_qpc=$start;end_qpc=[Diagnostics.Stopwatch]::GetTimestamp()})
  # Let this timed observer exit normally and flush its complete CSV.
  $deadline=[DateTime]::UtcNow.AddSeconds(60)
  while(-not $observer.HasExited -and [DateTime]::UtcNow -lt $deadline){WaitGame 1}
  if(-not $observer.HasExited -or $observer.ExitCode -ne 0){throw 'The timed ETW observer did not finish successfully'}
  if(-not(Test-Path "$root/presentmon.csv")){throw 'The ETW trace was not produced'}
  AssertSameGame
  $valid=$true
} catch {
  $failure=$_.Exception.Message
  throw
} finally {
  $env:KLIP_DATA_ROOT=$previousRoot
  foreach($owned in @($capture,$observer)){
    if($owned -and -not $owned.HasExited){Stop-Process -Id $owned.Id -ErrorAction SilentlyContinue; $owned.WaitForExit(5000)|Out-Null}
  }
  @{valid=$valid;failure=$failure;source_pid=$GamePid;source_start_ticks=$identity;
    source_executable=$game.Path;window_title=$WindowTitle;scene=$SceneDescription;
    condition=$Quality;fps=$Fps;copy_limiter=$LimitGameCapture.IsPresent;
    executable=$exe;executable_sha256=(Get-FileHash $exe).Hash;
    qpc_frequency=[Diagnostics.Stopwatch]::Frequency;phases=$phases.ToArray();
    limitations='Requires a repeatable scene. No game settings/input are changed. Captured desktop audio stays local. Different scenes, shader compilation, FPS caps and thermal drift can confound the comparison.'} |
    ConvertTo-Json -Depth 5 | Set-Content "$root/gameplay-protocol.json"
}
Write-Output "Evidence preserved: $root. Game process was left running."
