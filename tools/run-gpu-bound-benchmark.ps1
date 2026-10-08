param(
  [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$')][string]$Name='gpu-bound-benchmark',
  [ValidateRange(1,256)][int]$GpuWork=256,
  [ValidateRange(100,180)][int]$SourceSeconds=120,
  [ValidateSet('balanced','limited','performance')][string[]]$Conditions=@('balanced','limited','balanced'),
  [string]$Executable='build-obs-Release-gpu-bench/bin/64bit/Klip.exe',
  [switch]$SmoothWorkload,
  [switch]$PresentMon
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root=Join-Path $repo "work/$Name"
if(Test-Path $root){throw 'Use a fresh name; benchmark evidence is preserved'}
if(Get-Process Klip,obs64,cadence_scene -ErrorAction SilentlyContinue){throw 'Close other capture/fixture processes before benchmarking'}
$exe=(Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory $root | Out-Null
$llvm=(Get-ChildItem "$repo/work/validation-tools/llvm" -Directory|Select-Object -First 1).FullName
$sceneExe=Join-Path $root 'cadence_scene.exe'
& "$llvm/bin/clang++.exe" -std=c++20 -O2 "-I$repo/include" "$PSScriptRoot/cadence_scene.cpp" -o $sceneExe -ld3d11 -ldxgi -luser32 -lwinmm -ld3dcompiler
if($LASTEXITCODE){throw 'Diagnostic fixture build failed'}
Copy-Item "$llvm/bin/libc++.dll","$llvm/bin/libunwind.dll" $root
$nvidia=(Get-Command nvidia-smi -ErrorAction Stop).Source
$presentmonExe=if($PresentMon){& "$PSScriptRoot/prepare-presentmon.ps1"}else{$null}
@{executable=$exe;executable_sha256=(Get-FileHash $exe).Hash;fixture_sha256=(Get-FileHash $sceneExe).Hash;
  gpu_work=$GpuWork;conditions=$Conditions;fps=60;size='1920x1080';cqp=18;presentmon=$PresentMon.IsPresent;smooth_workload=$SmoothWorkload.IsPresent;source='uncapped D3D11 diagnostic shader';
  limitations='CPU-side Present completion/submit cadence, not ETW displayed FPS, latency or a Fortnite benchmark. Test-hook-enabled executable uses the normal 250ms background loop.'}|
  ConvertTo-Json -Depth 5|Set-Content "$root/protocol.json"
$trialIndex=0
foreach($condition in $Conditions){
  ++$trialIndex
  $trial=Join-Path $root ("{0:D2}-{1}" -f $trialIndex,$condition)
  New-Item -ItemType Directory $trial|Out-Null
  $quality=if($condition -eq 'performance'){'performance'}else{'balanced'}
  @('capture_mode=game_window','preferred_game_title="Klip cadence test scene"',
    'output_width=1920','output_height=1080','target_fps=60','clip_duration_seconds=15',
    'rolling_buffer_seconds=30','rolling_buffer_bytes=268435456','desktop_audio_enabled=true',
    'desktop_audio_gain=1','microphone_enabled=false','audio_bitrate=192000','capture_cursor=false',
    'obs_encoder_id="obs_nvenc_h264_tex"','obs_cq=18',"encoder_quality=$quality",'obs_separate_audio_tracks=false',
    "obs_limit_game_capture_fps=$(($condition -eq 'limited').ToString().ToLowerInvariant())",
    'static_overlay_enabled=false','live_overlay_enabled=false') | Set-Content "$trial/settings.ini"
  $previousRoot=$env:KLIP_DATA_ROOT
  $source=$null;$capture=$null;$gpuProcess=$null;$presentmonProcess=$null
  $phases=[Collections.Generic.List[object]]::new()
  try {
    $sourceArgs="$SourceSeconds --fps=0 --present-interval=0 --gpu-work=$GpuWork --av --frame-log=`"$trial/source-frames.csv`""
    if($SmoothWorkload){$sourceArgs+=' --smooth-workload'}
    $source=Start-Process $sceneExe -ArgumentList $sourceArgs -WindowStyle Hidden -RedirectStandardOutput "$trial/source.log" -RedirectStandardError "$trial/source-error.log" -PassThru
    if($PresentMon){
      $session="KlipPerf-$([Guid]::NewGuid().ToString('N'))"
      $pmArgs="--process_id $($source.Id) --output_file `"$trial/presentmon.csv`" --qpc_time --no_console_stats --no_track_input --terminate_on_proc_exit --timed $($SourceSeconds+10) --terminate_after_timed --session_name $session"
      $presentmonProcess=Start-Process $presentmonExe -ArgumentList $pmArgs -WindowStyle Hidden -RedirectStandardOutput "$trial/presentmon.log" -RedirectStandardError "$trial/presentmon-error.log" -PassThru
    }
    $gpuProcess=Start-Process $nvidia -ArgumentList '--query-gpu=timestamp,index,name,pstate,clocks.gr,clocks.mem,power.draw,temperature.gpu,utilization.gpu,utilization.encoder --format=csv,noheader,nounits --loop-ms=1000' -WindowStyle Hidden -RedirectStandardOutput "$trial/gpu.csv" -RedirectStandardError "$trial/gpu-errors.log" -PassThru
    Start-Sleep -Seconds 8
    if($source.HasExited){throw "Source exited: $($source.ExitCode)"}
    $preStart=[Diagnostics.Stopwatch]::GetTimestamp()
    Start-Sleep -Seconds 15
    $phases.Add(@{name='source-before';start_qpc=$preStart;end_qpc=[Diagnostics.Stopwatch]::GetTimestamp()})
    $env:KLIP_DATA_ROOT=$trial
    $capture=Start-Process $exe -WorkingDirectory (Split-Path $exe) -ArgumentList '--obs-background-benchmark' -WindowStyle Hidden -PassThru
    $readyDeadline=[DateTime]::UtcNow.AddSeconds(25)
    while([DateTime]::UtcNow -lt $readyDeadline){
      if($capture.HasExited){throw "Capture exited before ready: $($capture.ExitCode)"}
      if((Test-Path "$trial/klip.log") -and (Select-String -LiteralPath "$trial/klip.log" -Pattern 'BACKGROUND BENCHMARK READY' -Quiet)){break}
      Start-Sleep -Milliseconds 250
    }
    if(-not (Select-String -LiteralPath "$trial/klip.log" -Pattern 'BACKGROUND BENCHMARK READY' -Quiet)){throw 'Capture readiness deadline exceeded'}
    Start-Sleep -Seconds 8
    $captureStart=[Diagnostics.Stopwatch]::GetTimestamp()
    & "$PSScriptRoot/sample-capture-process.ps1" -CapturePid $capture.Id -SourcePid $source.Id -EvidenceRoot $trial -Phase replay -Seconds 15
    $phases.Add(@{name='source-with-capture';start_qpc=$captureStart;end_qpc=[Diagnostics.Stopwatch]::GetTimestamp()})
    if(-not $capture.WaitForExit(40000)){throw 'Capture exceeded normal-loop benchmark deadline'}
    if($capture.ExitCode -ne 0 -or -not (Select-String -LiteralPath "$trial/klip.log" -Pattern 'BACKGROUND BENCHMARK PASSED' -Quiet)){throw 'Capture/save failed'}
    Start-Sleep -Seconds 5
    if($source.HasExited){throw 'Source ended before post baseline'}
    $postStart=[Diagnostics.Stopwatch]::GetTimestamp()
    Start-Sleep -Seconds 15
    if($source.HasExited){throw 'Source ended during post baseline'}
    $phases.Add(@{name='source-after';start_qpc=$postStart;end_qpc=[Diagnostics.Stopwatch]::GetTimestamp()})
    if(-not $source.WaitForExit(45000)){throw 'Source exceeded its bounded lifetime'}
    if($source.ExitCode -ne 0){throw "Source failed: $($source.ExitCode)"}
    if($PresentMon){
      if(-not $presentmonProcess.WaitForExit(15000)){throw 'PresentMon did not exit after the source'}
      if($presentmonProcess.ExitCode -ne 0 -or -not(Test-Path "$trial/presentmon.csv")){throw 'PresentMon failed; do not claim ETW metrics'}
    }
    @{condition=$condition;valid=$true;source_pid=$source.Id;capture_pid=$capture.Id;phases=$phases.ToArray()}|
      ConvertTo-Json -Depth 5|Set-Content "$trial/phases.json"
    Write-Output "Completed bounded GPU trial $trialIndex/$($Conditions.Count): $condition"
  } finally {
    $env:KLIP_DATA_ROOT=$previousRoot
    # Terminate only helpers created here if a test fails; preserve partial media.
    foreach($owned in @($capture,$source,$gpuProcess,$presentmonProcess)){
      if($owned -and -not $owned.HasExited){Stop-Process -Id $owned.Id -ErrorAction SilentlyContinue; $owned.WaitForExit(5000)|Out-Null}
    }
  }
}
Write-Output "Preserved GPU-bound evidence: $root"
