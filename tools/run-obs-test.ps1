param(
  [ValidateSet('game','display')][string]$Mode='game',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [ValidatePattern('^[a-zA-Z0-9_-]{0,50}$')][string]$BuildSuffix='',
  [string]$Executable='',
  [ValidateSet(60,120)][int]$Fps=60,
  [ValidateSet(60,120,240)][int]$SourceFps=60,
  [ValidateSet('balanced','performance','quality')][string]$EncoderQuality='balanced',
  [switch]$LimitGameCapture,
  [ValidateRange(0,2)][int]$DisplayMethod=0,
  [switch]$GlobalHotkeys,
  [switch]$AvMarkers,
  [switch]$SeparateAudioTracks,
  [switch]$NativeResolution,
  [switch]$StartSourceLate,
  [switch]$SaveOnShutdown,
  [switch]$DestinationLoss,
  [ValidateSet('none','static','live')][string]$Overlay='none',
  [ValidateRange(0,1)][double]$OverlayOpacity=0.5,
  [switch]$OverlaySourceLate,
  [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$')][string]$Name='obs-capture-test'
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root=Join-Path $repo "work/$Name"
if(Test-Path $root){throw "Use a fresh test name to preserve evidence: $root"}
New-Item -ItemType Directory $root | Out-Null
$bin=Join-Path (Get-ChildItem "$repo/work/validation-tools/ffmpeg-8.1.2" -Directory | Select-Object -First 1).FullName 'bin'
$overlayImage=Join-Path $root 'overlay-white.png'
if($Overlay -eq 'static'){
  # Deterministic test asset, not an application artwork or a captured user image.
  & "$bin/ffmpeg.exe" -v error -f lavfi -i 'color=c=white:s=64x64' -frames:v 1 $overlayImage
  if($LASTEXITCODE){throw 'Could not create the overlay test fixture'}
}
$modeSetting=if($Mode -eq 'game'){'game_window'}else{'display'}
@("capture_mode=$modeSetting",'preferred_game_title="Klip cadence test scene"',
  $(if($NativeResolution){'output_width=0'}else{'output_width=1920'}),$(if($NativeResolution){'output_height=0'}else{'output_height=1080'}),"target_fps=$Fps",'clip_duration_seconds=15',
  'rolling_buffer_seconds=30','rolling_buffer_bytes=268435456','desktop_audio_enabled=true',
  'microphone_enabled=false','obs_encoder_id="obs_nvenc_h264_tex"','obs_cq=18',
  "encoder_quality=$EncoderQuality","obs_display_method=$DisplayMethod",
  "obs_limit_game_capture_fps=$($LimitGameCapture.IsPresent.ToString().ToLowerInvariant())",
  "obs_separate_audio_tracks=$($SeparateAudioTracks.IsPresent.ToString().ToLowerInvariant())",
  $(if($SaveOnShutdown){'obs_filename_format="shutdown_collision"'}else{'obs_filename_format="clip_%CCYY-%MM-%DD_%hh-%mm-%ss"'}),
  "static_overlay_enabled=$(($Overlay -eq 'static').ToString().ToLowerInvariant())",
  "live_overlay_enabled=$(($Overlay -eq 'live').ToString().ToLowerInvariant())",
  ('static_overlay_path="'+$overlayImage.Replace('\','/')+'"'),
  'live_overlay_window_title="Klip overlay test scene"',
  'static_overlay_x=0.72','static_overlay_y=0.14','static_overlay_width=0.20','static_overlay_height=0.06',
  "static_overlay_opacity=$($OverlayOpacity.ToString([Globalization.CultureInfo]::InvariantCulture))",
  'hotkey_modifiers=16391','hotkey_save_virtual_key=133','hotkey_record_virtual_key=134',
  'hotkey_toggle_ui_virtual_key=135') | Set-Content "$root/settings.ini"
$previousRoot=$env:KLIP_DATA_ROOT
$previousPath=$env:PATH
try {
  $llvm=(Get-ChildItem "$repo/work/validation-tools/llvm" -Directory | Select-Object -First 1).FullName
  $env:PATH="$llvm/bin;$env:PATH"
  $env:KLIP_DATA_ROOT=$root
  $scene=Join-Path $repo 'work/cadence_scene.exe'
  & "$llvm/bin/clang++.exe" -std=c++20 -O2 "-I$repo/include" "$PSScriptRoot/cadence_scene.cpp" -o $scene -ld3d11 -ldxgi -luser32 -lwinmm -ld3dcompiler
  if($LASTEXITCODE){throw 'Native scene build failed'}
  if($Overlay -eq 'live' -and -not $OverlaySourceLate){
    $overlaySource=Start-Process $scene -ArgumentList '40 --fps=60 --overlay-window' -WindowStyle Hidden -RedirectStandardOutput "$root/overlay-source.log" -PassThru
    Start-Sleep -Milliseconds 500
  }
  $sourceArgs="40 --fps=$SourceFps" + $(if($AvMarkers){' --av'}else{''})
  if(-not $StartSourceLate){
    $source=Start-Process $scene -ArgumentList $sourceArgs -WindowStyle Hidden -RedirectStandardOutput "$root/source.log" -PassThru
    Start-Sleep -Milliseconds 500
  }
  $buildName="build-obs-$Configuration" + $(if($BuildSuffix){"-$BuildSuffix"}else{''})
  $exe=if($Executable){(Resolve-Path -LiteralPath $Executable).Path}else{Join-Path $repo "$buildName/bin/64bit/Klip.exe"}
  $arguments=if($GlobalHotkeys){'--configured-capture-smoke-test --hotkey-input-smoke-test'}else{'--configured-capture-smoke-test'}
  if($SaveOnShutdown){$arguments='--configured-capture-smoke-test --obs-save-shutdown-smoke-test'}
  if($DestinationLoss){$arguments='--configured-capture-smoke-test --obs-save-failure-smoke-test'}
  $capture=Start-Process $exe -WorkingDirectory (Split-Path $exe) -ArgumentList $arguments -WindowStyle Hidden -PassThru
  if($Overlay -eq 'live' -and $OverlaySourceLate){
    Start-Sleep -Seconds 2
    $overlaySource=Start-Process $scene -ArgumentList '40 --fps=60 --overlay-window' -WindowStyle Hidden -RedirectStandardOutput "$root/overlay-source.log" -PassThru
  }
  if($StartSourceLate){
    Start-Sleep -Seconds 3
    $source=Start-Process $scene -ArgumentList $sourceArgs -WindowStyle Hidden -RedirectStandardOutput "$root/source.log" -PassThru
  }
  if(-not $capture.WaitForExit(45000)){throw "Acceptance exceeded its timeout (PID $($capture.Id))"}
  Get-Content "$root/klip.log" -Tail 10
  if($capture.ExitCode){throw "OBS acceptance failed: exit=$($capture.ExitCode)"}
  $files=@(Get-ChildItem "$root/Clips","$root/Recordings" -Filter '*.mkv')
  if($files.Count -ne 4){throw 'Expected three consecutive replay clips and a concurrent recording'}
  foreach($file in $files){
    $analysisArgs=@($file.FullName,'--tools',$bin)
    if($Overlay -ne 'none'){$analysisArgs+=@('--overlay-mode',$Overlay,'--overlay-opacity',$OverlayOpacity.ToString([Globalization.CultureInfo]::InvariantCulture))}
    & python "$PSScriptRoot/analyze-cadence.py" @analysisArgs > ($file.FullName+'.analysis.json')
    if($LASTEXITCODE){throw 'Saved media decode failed'}
    $a=Get-Content ($file.FullName+'.analysis.json') -Raw | ConvertFrom-Json
    $minimum=$Fps*0.97
    if($a.unique_transitions_per_second -lt $minimum -or $a.nonmonotonic_presentation_timestamps -ne 0){throw "Decoded motion failed: $($file.Name), unique=$($a.unique_transitions_per_second)/s"}
    $expectedTracks=if($SeparateAudioTracks){3}else{1}
    if(@($a.streams | Where-Object codec_type -eq audio).Count -ne $expectedTracks){throw 'Missing configured AAC audio tracks'}
    if($AvMarkers){
      $offsets=@($a.audio_minus_video_ms)
      if($offsets.Count -lt 3 -or @($offsets|Where-Object {[math]::Abs($_) -gt 40}).Count -gt 0){throw "A/V marker sync exceeded 40 ms: $($file.Name), offsets=$($offsets -join ',')"}
    }
    "DECODED $($file.Name): $($a.unique_transitions_per_second) distinct/s, repeats=$($a.repeat_percent)%"
  }
} finally {
  if($capture -and -not $capture.HasExited){
    # Stop only the exact acceptance process created by this harness on timeout.
    Stop-Process -Id $capture.Id -Force -ErrorAction SilentlyContinue
  }
  if($source -and -not $source.HasExited){$source.CloseMainWindow() | Out-Null}
  if($overlaySource -and -not $overlaySource.HasExited){$overlaySource.CloseMainWindow() | Out-Null}
  $env:KLIP_DATA_ROOT=$previousRoot
  $env:PATH=$previousPath
}
