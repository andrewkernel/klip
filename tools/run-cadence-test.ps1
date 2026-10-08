param(
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [ValidateSet(30,60,120)][int]$Fps=60,
  [ValidateSet(30,60,120)][int]$SourceFps=120,
  [switch]$NativeScene,
  [ValidateSet(0,30,60,120)][int]$NativeSourceFps=0,
  [ValidateRange(0,240)][double]$MinimumUniqueFps=0,
  [switch]$WindowCapture,
  [switch]$ResizeSource,
  [switch]$SourceSwitch,
  [switch]$SourceSwitchAv,
  [switch]$SourceSizedWindow,
  [switch]$Amd,
  [switch]$Intel,
  [switch]$SoftwareFallback,
  [switch]$FallbackDiagnostics,
  [switch]$FeatureLevel10,
  [switch]$EventQuerySync,
  [switch]$SoftwarePerformance,
  [switch]$RuntimeEncoderFailure,
  [switch]$BalancedQuality,
  [switch]$RequireAvMarkers,
  [switch]$VideoOnly,
  [switch]$HotkeyActions,
  [switch]$HotkeyInput,
  [switch]$LongAvSync,
  [switch]$Stress,
  [long]$ProcessorAffinityMask=0,
  [string]$Name='cadence-test',
  [string]$Executable=''
)
$ErrorActionPreference='Stop'
if($BalancedQuality -and -not ($RuntimeEncoderFailure -or $SoftwareFallback)){throw 'BalancedQuality is supported only with a software fallback acceptance run.'}
if($Amd -and $Intel){throw 'Choose only one vendor-specific hardware adapter.'}
if($Fps -eq 30 -and ($SourceSwitch -or $SourceSwitchAv -or $SourceSizedWindow -or $Intel -or $FeatureLevel10 -or $EventQuerySync -or $FallbackDiagnostics -or $RuntimeEncoderFailure -or $Stress -or $WindowCapture -or $ResizeSource)){throw 'The 30 FPS acceptance path supports display capture with the default, AMD AMF, or software fallback encoder.'}
if($SourceSwitchAv -and ($SourceSwitch -or $Fps -ne 60 -or $NativeScene -or $Intel -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $Stress -or $WindowCapture -or $ResizeSource -or $EventQuerySync -or -not $RequireAvMarkers)){throw 'The audiovisual source-switch test requires a 60 FPS FFplay source, hardware encoder, and -RequireAvMarkers.'}
if($SourceSizedWindow -and ($Fps -ne 60 -or -not $NativeScene -or $SourceSwitch -or $SourceSwitchAv -or $Intel -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $Stress -or $WindowCapture -or $ResizeSource -or $EventQuerySync -or $RequireAvMarkers)){throw 'The source-sized WGC test requires a 60 FPS native odd-sized window on the default hardware encoder.'}
if($SourceSwitch -and ($Fps -ne 60 -or -not $NativeScene -or $Intel -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $Stress -or $WindowCapture -or $ResizeSource -or $EventQuerySync -or $RequireAvMarkers)){throw 'The source-switch test requires a 60 FPS native-scene hardware-encoder run; AMD is supported as an optional cross-adapter test.'}
if($Intel -and ($Fps -ne 60 -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $Stress -or $WindowCapture -or $ResizeSource)){throw 'The Intel acceptance mode requires a 60 FPS display-capture run with the Intel adapter and Media Foundation hardware encoder.'}
if($EventQuerySync -and ($Fps -ne 60 -or $Amd -or $Intel -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $Stress -or $WindowCapture -or $ResizeSource)){throw 'The event-query synchronization test requires a 60 FPS display-capture run on the default adapter.'}
if($HotkeyActions -and ($Fps -ne 60 -or -not $NativeScene -or $Amd -or $Intel -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $EventQuerySync -or $Stress -or $WindowCapture -or $ResizeSource -or $RequireAvMarkers -or $VideoOnly)){throw 'Hotkey action acceptance requires a 60 FPS native scene on the default hardware adapter.'}
if($HotkeyInput -and ($Fps -ne 60 -or -not $NativeScene -or -not $WindowCapture -or $Amd -or $Intel -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $EventQuerySync -or $Stress -or $ResizeSource -or $RequireAvMarkers -or $VideoOnly -or $HotkeyActions)){throw 'Global hotkey input acceptance requires a 60 FPS native-window capture on the default hardware adapter.'}
if($LongAvSync -and ($Fps -ne 60 -or $SourceFps -ne 60 -or $NativeScene -or $WindowCapture -or $Amd -or $Intel -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $EventQuerySync -or $Stress -or $ResizeSource -or $RequireAvMarkers -eq $false -or $VideoOnly -or $SourceSwitch -or $SourceSwitchAv -or $SourceSizedWindow -or $HotkeyActions -or $HotkeyInput -or $RuntimeEncoderFailure)){throw 'Long A/V sync acceptance requires a 60 FPS audiovisual source and -RequireAvMarkers on the default hardware encoder.'}
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $Executable){$Executable=Join-Path $repo "build-validation-$Configuration/Klip.exe"}
$Executable=(Resolve-Path $Executable).Path
if($RuntimeEncoderFailure -or $BalancedQuality){
  $cachePath=Join-Path $repo "build-validation-$Configuration/CMakeCache.txt"
  if(-not (Test-Path $cachePath) -or (Get-Content -Raw $cachePath) -notmatch 'KLIP_ENABLE_TEST_HOOKS:BOOL=ON'){
    throw "This acceptance mode requires a build with test hooks. Run tools/build-validation.ps1 -Configuration $Configuration -EnableTestHooks `$true first."
  }
}
$bin=(Get-ChildItem "$repo/work/validation-tools/ffmpeg-8.1.2" -Directory | Select-Object -First 1).FullName+'/bin'
$llvm=(Get-ChildItem "$repo/work/validation-tools/llvm" -Directory | Select-Object -First 1).FullName
$previousPath=$env:PATH
$env:PATH=(Join-Path $llvm 'bin')+';'+$env:PATH
$scene=Join-Path $repo 'work/cadence_scene.exe'
& (Join-Path $llvm 'bin/clang++.exe') -std=c++20 -O2 -Wall -Wextra -Werror `
  "-I$repo/include" `
  (Join-Path $PSScriptRoot 'cadence_scene.cpp') -o $scene -ld3d11 -ldxgi -luser32 -lwinmm -ld3dcompiler
if($LASTEXITCODE -ne 0){throw 'Could not build the moving test scene.'}
$root=Join-Path $repo "work/$Name"
if(Test-Path $root){throw "Test directory already exists: $root. Choose a fresh Name."}
New-Item -ItemType Directory $root | Out-Null
$previousRoot=$env:KLIP_DATA_ROOT
$previousTrace=$env:KLIP_CAPTURE_TRACE
$env:KLIP_DATA_ROOT=$root
$env:KLIP_CAPTURE_TRACE='1'
$source=if($LongAvSync){Join-Path $repo "work/cadence-source-$SourceFps-long.mp4"}else{Join-Path $repo "work/cadence-source-$SourceFps.mp4"}
$player=$null
$capture=$null
try {
  if($LongAvSync){
    & "$PSScriptRoot/new-cadence-source.ps1" -Fps $SourceFps -Seconds 70 -OutputPath $source
    if($LASTEXITCODE -ne 0){throw 'Could not generate the long audiovisual marker source.'}
  }
  # The visible moving source is required for a real desktop capture test.
  if($NativeScene) {
    $sceneSeconds=if($Stress){90}else{30}
    $sceneArguments=if($ResizeSource){"$sceneSeconds --resize"}elseif($SourceSizedWindow){"$sceneSeconds --odd-size"}else{"$sceneSeconds"}
    if($NativeSourceFps -gt 0){$sceneArguments+=" --fps=$NativeSourceFps"}
    $player=Start-Process $scene -ArgumentList $sceneArguments -WindowStyle Hidden -RedirectStandardOutput "$root/source.log" -PassThru
  } else {
    $playerArguments='-autoexit -fs -loglevel error'
    if($SourceSwitchAv){$playerArguments+=' -window_title "Klip cadence switch source"'}
    $playerArguments+=' -i "'+$source+'"'
    $player=Start-Process "$bin/ffplay.exe" -ArgumentList $playerArguments -PassThru
  }
  Start-Sleep -Seconds 2
  if(($Amd -and $Stress) -or ($SoftwareFallback -and ($Amd -or $Stress -or $Fps -eq 120)) -or ($SoftwarePerformance -and ($Amd -or $Stress)) -or ($SoftwareFallback -and $SoftwarePerformance) -or ($RuntimeEncoderFailure -and ($Fps -ne 60 -or $Amd -or $Intel -or $SoftwareFallback -or $FallbackDiagnostics -or $Stress -or $WindowCapture -or $ResizeSource -or $EventQuerySync -or $SourceSwitch -or $SourceSwitchAv -or $SourceSizedWindow)) -or ($FallbackDiagnostics -and (-not $NativeScene -or $Fps -ne 60 -or $Amd -or $SoftwareFallback -or $SoftwarePerformance -or $Stress -or $WindowCapture -or $ResizeSource)) -or ($FeatureLevel10 -and ($Fps -ne 60 -or $NativeScene -or $Amd -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $Stress -or $WindowCapture -or $ResizeSource)) -or ($WindowCapture -and ($Amd -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $Stress -or $EventQuerySync)) -or ($ResizeSource -and (-not $NativeScene -or $WindowCapture -or $Amd -or $SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics -or $FeatureLevel10 -or $Stress -or $Fps -eq 120 -or $EventQuerySync))){throw 'Requested cadence test option combination is not supported.'}
  if($RequireAvMarkers -and ($NativeScene -or $WindowCapture -or $FallbackDiagnostics -or $Stress)){throw 'A/V marker analysis requires the FFplay audiovisual source.'}
  if($RequireAvMarkers -and $VideoOnly){throw 'VideoOnly cannot be combined with A/V marker analysis.'}
  $flag=if($LongAvSync){'--long-av-sync-smoke-test'}elseif($Fps -eq 30 -and $Amd){'--30fps-smoke-test --amd-hardware-smoke-test'}elseif($Fps -eq 30 -and $SoftwareFallback){'--30fps-smoke-test --software-fallback-smoke-test'}elseif($Fps -eq 30 -and $SoftwarePerformance){'--30fps-smoke-test --software-performance-smoke-test'}elseif($Fps -eq 30){'--30fps-smoke-test'}elseif($Fps -eq 120 -and $SoftwarePerformance){'--software-performance-120fps-smoke-test'}elseif($RuntimeEncoderFailure){'--runtime-encoder-fallback-smoke-test'}elseif($SourceSwitchAv -and $Amd){'--source-switch-av-smoke-test --amd-hardware-smoke-test'}elseif($SourceSwitchAv){'--source-switch-av-smoke-test'}elseif($SourceSizedWindow){'--source-sized-window-smoke-test'}elseif($SourceSwitch -and $Amd){'--source-switch-smoke-test --amd-hardware-smoke-test'}elseif($SourceSwitch){'--source-switch-smoke-test'}elseif($FeatureLevel10 -and $EventQuerySync){'--feature-level-10-smoke-test --event-query-sync-smoke-test'}elseif($Intel){'--intel-hardware-smoke-test'}elseif($EventQuerySync){'--event-query-sync-smoke-test'}elseif($FeatureLevel10){'--feature-level-10-smoke-test'}elseif($FallbackDiagnostics){'--encoder-fallback-smoke-test'}elseif($ResizeSource){'--window-resize-smoke-test'}elseif($WindowCapture -and $Fps -eq 120){'--window-120fps-smoke-test'}elseif($WindowCapture){'--window-capture-smoke-test'}elseif($Amd -and $Fps -eq 120){'--amd-120fps-smoke-test'}elseif($Amd){'--amd-hardware-smoke-test'}elseif($SoftwarePerformance){'--software-performance-smoke-test'}elseif($SoftwareFallback){'--software-fallback-smoke-test'}elseif($Stress){'--nvenc-stress-smoke-test'}elseif($Fps -eq 120){'--120fps-smoke-test'}else{'--capture-smoke-test'}
  if($VideoOnly){$flag+=' --video-only-smoke-test'}
  if($HotkeyInput){$flag+=' --hotkey-input-smoke-test'}
  if($HotkeyActions){$flag+=' --hotkey-actions-smoke-test'}
  if($RuntimeEncoderFailure -and $SoftwarePerformance){$flag+=' --software-performance-smoke-test'}
  if($BalancedQuality){$flag+=' --balanced-quality-smoke-test'}
  $capture=Start-Process $Executable -ArgumentList $flag -WindowStyle Hidden -RedirectStandardError "$root/ffmpeg.stderr.log" -PassThru
  if($ProcessorAffinityMask -ne 0){
    $capture.Refresh()
    $capture.ProcessorAffinity=[IntPtr]$ProcessorAffinityMask
    Write-Output "Capture process affinity mask=0x$('{0:X}' -f $ProcessorAffinityMask)"
  }
  $samples=[System.Collections.Generic.List[object]]::new()
  $started=Get-Date
  while(!$capture.WaitForExit(1000)) {
    $process=Get-Process -Id $capture.Id -ErrorAction SilentlyContinue
    if($process) {
      if($process.MainWindowTitle -match '^Klip (startup failed|fatal error)$'){
        $dialogTitle=$process.MainWindowTitle
        $capture.CloseMainWindow() | Out-Null
        if(-not $capture.WaitForExit(3000)){Stop-Process -Id $capture.Id -Force -ErrorAction SilentlyContinue}
        throw "Klip displayed '$dialogTitle' during capture acceptance; inspect $root/klip.log and rerun with Klip visible for the dialog details."
      }
      $samples.Add([pscustomobject]@{ElapsedSeconds=[math]::Round(((Get-Date)-$started).TotalSeconds,1);CpuSeconds=[math]::Round($process.CPU,2);WorkingSetMiB=[math]::Round($process.WorkingSet64/1MB,1);PrivateMiB=[math]::Round($process.PrivateMemorySize64/1MB,1);SourceAlive=($player -and !$player.HasExited)})
    }
    if(((Get-Date)-$started).TotalSeconds -gt 100){throw "Capture timeout PID=$($capture.Id); inspect before restarting."}
  }
  $capture.Refresh()
  $captureExitCode=$capture.ExitCode
  $acceptanceLog=Get-Content "$root/klip.log" -Raw
  if($acceptanceLog -notmatch 'Capture (display|window''s current monitor) adapter:|Could not map capture target (to a display adapter|monitor to an attached hardware display adapter)'){
    throw 'Capture acceptance did not record whether the WGC target display and D3D adapter are local or cross-adapter.'
  }
  if($HotkeyActions -and ($acceptanceLog -notmatch 'HOTKEY ACTION ACCEPTANCE recording-start dispatched' -or $acceptanceLog -notmatch 'HOTKEY ACTION ACCEPTANCE save-clip dispatched' -or $acceptanceLog -notmatch 'HOTKEY ACTION ACCEPTANCE recording-stop dispatched')){throw 'The app did not route all three registered hotkey action IDs through its real message handler.'}
  if($HotkeyInput -and ($acceptanceLog -notmatch 'HOTKEY INPUT ACCEPTANCE show/hide keys delivered' -or $acceptanceLog -notmatch 'HOTKEY INPUT ACCEPTANCE record-start key delivered' -or $acceptanceLog -notmatch 'HOTKEY INPUT ACCEPTANCE save-clip key delivered' -or $acceptanceLog -notmatch 'HOTKEY INPUT ACCEPTANCE record-stop key delivered')){throw 'Synthetic system key input did not trigger all registered global shortcuts.'}
  if(($SourceSwitch -or $SourceSwitchAv) -and ($acceptanceLog -notmatch 'SOURCE SWITCH ACCEPTANCE READY' -or $acceptanceLog -notmatch 'SOURCE SWITCH ACCEPTANCE switched' -or $acceptanceLog -notmatch 'SOURCE SWITCH ACCEPTANCE restored')){throw 'The capture acceptance run did not complete a source switch and restoration.'}
  if($null -eq $captureExitCode){
    if($acceptanceLog -match 'CAPTURE ACCEPTANCE PASSED'){
      Write-Warning 'Windows did not expose the child exit code; using the terminal acceptance log result.'
      $captureExitCode=0
    }elseif($acceptanceLog -match 'CAPTURE ACCEPTANCE FAILED|CAPTURE ACCEPTANCE TIMED OUT'){
      $captureExitCode=1
    }else{
      throw 'Capture process exited without an exit code or terminal acceptance result.'
    }
  }
  $samples | Export-Csv -NoTypeInformation -Path "$root/resource-samples.csv"
  Write-Output "Capture exit=$captureExitCode"
  Get-Content "$root/klip.log" -Tail 8
  $expectedEncoder=if($Intel){'h264_mf'}elseif($Amd){'h264_amf'}elseif($SoftwareFallback -or $SoftwarePerformance -or $FallbackDiagnostics){'h264_mf_software'}elseif($Stress -or $Fps -eq 120){'h264_nvenc'}else{$null}
  if($expectedEncoder -and $acceptanceLog -notmatch ("CAPTURE ACCEPTANCE PASSED encoder="+[regex]::Escape($expectedEncoder)+"\b")){throw "Capture did not pass with expected encoder $expectedEncoder."}
  if($EventQuerySync -and $acceptanceLog -notmatch 'GPU synchronization: forced D3D11 event-query fallback'){throw 'Capture did not use the forced D3D11 event-query synchronization path.'}
  if($FallbackDiagnostics -and ($acceptanceLog -notmatch 'Software compatibility fallback active: h264_mf_software' -or $acceptanceLog -notmatch 'h264_klip_test_missing' -or $acceptanceLog -notmatch 'not present in this FFmpeg build')){throw 'The missing preferred encoder did not produce the expected actionable fallback notice.'}
  if($RuntimeEncoderFailure){
    if($acceptanceLog -notmatch 'Video encoder failed at runtime; restarting once: h264_nvenc' -or $acceptanceLog -notmatch 'Video encoder failed again; trying fallback: h264_nvenc' -or $acceptanceLog -notmatch '(Encoder fallback active|Software compatibility fallback active): .* selected after h264_nvenc') {throw 'The forced NVENC runtime failures did not trigger a second attempt and backend fallback.'}
    if($acceptanceLog -match 'Software compatibility fallback active:' -and $acceptanceLog -notmatch 'Performance mode \(720p/60\)'){throw 'Software fallback did not provide the tested performance-mode recommendation.'}
    if($acceptanceLog -notmatch 'CAPTURE ACCEPTANCE PASSED encoder=(?!h264_nvenc)[A-Za-z0-9_]+' ){throw 'Capture did not complete after switching away from NVENC.'}
  }
  $mediaFiles=@(Get-ChildItem "$root/Clips","$root/Recordings" -Filter '*.mp4')
  foreach($file in $mediaFiles) {
    & python "$PSScriptRoot/analyze-cadence.py" $file.FullName --tools $bin > ($file.FullName+'.analysis.json')
    if($LASTEXITCODE -ne 0){throw "Analysis failed: $($file.FullName)"}
  }
  foreach($file in $mediaFiles) {
    $cadence=Get-Content -Raw ($file.FullName+'.analysis.json') | ConvertFrom-Json
    if($MinimumUniqueFps -gt 0 -and [double]$cadence.unique_transitions_per_second -lt $MinimumUniqueFps){throw "Decoded motion failed in $($file.Name): unique updates/s=$($cadence.unique_transitions_per_second), required=$MinimumUniqueFps."}
    if($VideoOnly){
      $audioStreams=@($cadence.streams | Where-Object codec_type -eq 'audio')
      if($audioStreams.Count -gt 0){
        $volumeOutput=& "$bin/ffmpeg.exe" -hide_banner -nostats -i $file.FullName -map 0:a:0 -af volumedetect -f null NUL 2>&1
        if($LASTEXITCODE -ne 0){throw "Could not decode the video-only audio track in $($file.Name)."}
        $volumeMatch=[regex]::Match(($volumeOutput -join "`n"),'max_volume:\s*(-?\d+(?:\.\d+)?)\s*dB')
        if(-not $volumeMatch.Success -or [double]::Parse($volumeMatch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture) -gt -80){throw "Video-only acceptance contained audible audio in $($file.Name)."}
      }
    }
    if($cadence.nonmonotonic_presentation_timestamps -ne 0){throw "Saved media has duplicate or non-monotonic presentation timestamps in $($file.Name): $($cadence.nonmonotonic_presentation_timestamps) invalid intervals."}
    if($Fps -eq 30 -or ($Fps -eq 120 -and $SoftwarePerformance)){
      $decodedFps=[double]$cadence.decoded_frames/[double]$cadence.presentation_span_seconds
      $minimumFps=if($Fps -eq 30){29.5}else{118}
      $maximumFps=if($Fps -eq 30){30.5}else{122}
      $maximumInterval=if($Fps -eq 30){50}else{25}
      if($decodedFps -lt $minimumFps -or $decodedFps -gt $maximumFps -or $cadence.nonmonotonic_presentation_timestamps -ne 0 -or $cadence.max_presentation_interval_ms -gt $maximumInterval){throw "$Fps FPS cadence acceptance failed in $($file.Name): decodedFPS=$decodedFps, maxIntervalMs=$($cadence.max_presentation_interval_ms), nonmonotonic=$($cadence.nonmonotonic_presentation_timestamps)."}
    }
    if($RuntimeEncoderFailure){
      if($cadence.max_presentation_interval_ms -gt 200.1){throw "Runtime encoder recovery cadence failed in $($file.Name): maxIntervalMs=$($cadence.max_presentation_interval_ms)."}
      if($NativeScene -and ([double]$cadence.unique_transitions_per_second -lt 56 -or [double]$cadence.repeat_percent -gt 8)){throw "Runtime encoder recovery motion failed in $($file.Name): uniqueTransitionsPerSecond=$($cadence.unique_transitions_per_second), repeats=$($cadence.repeat_percent)% ."}
    }
    if($RequireAvMarkers){
      $analysis=Get-Content -Raw ($file.FullName+'.analysis.json') | ConvertFrom-Json
      $syncValues=@($analysis.audio_minus_video_ms | ForEach-Object {[double]$_})
      $outliers=@($syncValues | Where-Object {[math]::Abs($_) -gt 40}).Count
      if($analysis.flash_count -lt 3 -or $analysis.beep_count -lt 3 -or $syncValues.Count -lt 3 -or $outliers -gt [math]::Floor($syncValues.Count*0.1)){
        throw "A/V marker coverage or sync exceeded tolerance in $($file.Name): flashes=$($analysis.flash_count), beeps=$($analysis.beep_count), paired=$($syncValues.Count), outliers=$outliers."
      }
      if($LongAvSync -and $file.Directory.Name -eq 'Recordings'){
        if($null -eq $analysis.av_offset_drift_ms_per_minute){throw "Long-run A/V sync drift could not be measured in $($file.Name)."}
        if([math]::Abs([double]$analysis.av_offset_drift_ms_per_minute) -gt 20){throw "Long-run A/V sync drift exceeded 20ms/min in $($file.Name): drift=$($analysis.av_offset_drift_ms_per_minute)ms/min."}
      }
    }
  }
  if($captureExitCode -ne 0){exit $captureExitCode}
  $expectedRecordingSeconds=if($Stress -or $LongAvSync){60}elseif($SourceSwitchAv){15}else{10}
  $recording=Get-ChildItem "$root/Recordings" -Filter '*.mp4' -File | Select-Object -First 1
  if(-not $recording){throw 'Capture acceptance did not produce a recording.'}
  $recordingAnalysis=Get-Content -Raw ($recording.FullName+'.analysis.json') | ConvertFrom-Json
  $video=$recordingAnalysis.streams | Where-Object codec_type -eq 'video' | Select-Object -First 1
  if(-not $video -or [double]$video.duration -lt ($expectedRecordingSeconds-0.25)){
    throw "Recording was shorter than requested: expected at least $($expectedRecordingSeconds-0.25)s, got $($video.duration)s."
  }
  $frameInfo=& "$bin/ffprobe.exe" -v error -select_streams v:0 -show_frames `
    -show_entries frame=key_frame -of json $recording.FullName | ConvertFrom-Json
  if($LASTEXITCODE -ne 0){throw 'Could not inspect recording keyframes.'}
  $keyframes=@($frameInfo.frames | Where-Object key_frame -eq 1).Count
  if($keyframes -lt 2){throw "Recording contains only $keyframes keyframe(s); expected periodic IDRs."}
  if($ResizeSource){
    $sourceLog=Get-Content "$root/source.log" -Raw
    $initial=[regex]::Match($sourceLog,'initial=(\d+)x(\d+)')
    $resizes=[regex]::Matches($sourceLog,'resize=(\d+)x(\d+)')
    if(-not $initial.Success -or $resizes.Count -lt 2){throw 'The test scene did not report both source-size changes.'}
    $captureLog=Get-Content "$root/klip.log" -Raw
    foreach($resize in $resizes){
      $resolution="Capture source resolution changed to $($resize.Groups[1].Value)x$($resize.Groups[2].Value)"
      if($captureLog -notmatch [regex]::Escape($resolution)){throw "Klip did not observe the test source resize to $($resize.Groups[1].Value)x$($resize.Groups[2].Value)."}
    }
    foreach($media in @($recording)+@(Get-ChildItem "$root/Clips" -Filter '*.mp4' -File | Select-Object -First 1)){
      $mediaAnalysis=Get-Content -Raw ($media.FullName+'.analysis.json') | ConvertFrom-Json
      $mediaVideo=$mediaAnalysis.streams | Where-Object codec_type -eq 'video' | Select-Object -First 1
      if([int]$mediaVideo.width -ne [int]$initial.Groups[1].Value -or [int]$mediaVideo.height -ne [int]$initial.Groups[2].Value){
        throw "Source-sized output changed dimensions during recording: expected $($initial.Groups[1].Value)x$($initial.Groups[2].Value), got $($mediaVideo.width)x$($mediaVideo.height)."
      }
    }
  }
  if($SourceSizedWindow){
    if((Get-Content "$root/source.log" -Raw) -notmatch 'initial=1281x721'){throw 'The WGC test scene was not created at the requested odd dimensions.'}
    foreach($media in @($recording)+@(Get-ChildItem "$root/Clips" -Filter '*.mp4' -File | Select-Object -First 1)){
      $mediaAnalysis=Get-Content -Raw ($media.FullName+'.analysis.json') | ConvertFrom-Json
      $video=$mediaAnalysis.streams | Where-Object codec_type -eq 'video' | Select-Object -First 1
      if($video.width -ne 1280 -or $video.height -ne 720){throw "Source-sized capture did not normalize odd 1281x721 to even NV12 1280x720 in $($media.Name)."}
    }
  }
  $partial=Get-ChildItem $root -Recurse -Filter '*.partial' -File
  if($partial){throw "Partial media outputs remain: $($partial.FullName -join ', ')"}
} finally {
  if($player -and !$player.HasExited){$player.CloseMainWindow() | Out-Null}
  $env:KLIP_DATA_ROOT=$previousRoot
  $env:KLIP_CAPTURE_TRACE=$previousTrace
  $env:PATH=$previousPath
}
