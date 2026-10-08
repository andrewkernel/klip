param(
  [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$')][string]$Name='klip-parity-profile',
  [ValidateSet(60,120)][int]$Fps=60,
  [ValidateSet(60,120,240)][int]$SourceFps=60,
  [ValidateRange(120,1800)][int]$SourceSeconds=900,
  [string]$Executable,
  [switch]$LimitGameCapture,
  [switch]$Launch
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root=Join-Path $repo "work/$Name"
if(Test-Path $root){throw 'Use a fresh name; existing profiles and evidence are preserved'}
if($Launch -and (Get-Process Klip,obs64,cadence_scene -ErrorAction SilentlyContinue)){
  throw 'Close other capture/fixture processes before a parity run'
}
New-Item -ItemType Directory $root | Out-Null
$settings=@('capture_mode=game_window','preferred_game_title="Klip cadence test scene"',
  'output_width=1920','output_height=1080',"target_fps=$Fps",'clip_duration_seconds=15',
  'rolling_buffer_seconds=30','rolling_buffer_bytes=268435456','desktop_audio_enabled=true',
  'desktop_audio_gain=1','microphone_enabled=false','audio_bitrate=192000','capture_cursor=false',
  'obs_encoder_id="obs_nvenc_h264_tex"','obs_cq=18','encoder_quality=balanced',
  'obs_separate_audio_tracks=false','capture_preview_enabled=false',
  "obs_limit_game_capture_fps=$($LimitGameCapture.IsPresent.ToString().ToLowerInvariant())",
  'static_overlay_enabled=false','live_overlay_enabled=false',
  'obs_filename_format="klip_%CCYY-%MM-%DD_%hh-%mm-%ss"',
  'hotkey_modifiers=16391','hotkey_save_virtual_key=123',
  'hotkey_record_virtual_key=121','hotkey_toggle_ui_virtual_key=122')
$settings | Set-Content "$root/settings.ini" -Encoding utf8
$exe=if($Executable){(Resolve-Path -LiteralPath $Executable).Path}else{Join-Path $repo 'build-obs-Release/bin/64bit/Klip.exe'}
@{settings=@{fps=$Fps;size='1920x1080';color='NV12/709/limited';
  audio='48k stereo/AAC192/desktop only';replay_seconds=15;replay_memory_mib=256;
  capture_cursor=$false;preview=$false;encoder=@{rate_control='CQP';cqp=18;preset='p5';
  tune='hq';multipass='qres';profile='high';bf=2;adaptive_quantization=$true;lookahead=$false;keyint_sec=2}};
  executable=$exe;executable_sha256=(Get-FileHash $exe).Hash;source_fps=$SourceFps;
  game_capture_limited=$LimitGameCapture.IsPresent;
  limitations='Controlled D3D11 source, not a Fortnite gameplay benchmark';root=$root} |
  ConvertTo-Json -Depth 8 | Set-Content "$root/protocol.json"
if(-not $Launch){Write-Output "Prepared isolated Klip comparison at $root";return}
$llvm=(Get-ChildItem "$repo/work/validation-tools/llvm" -Directory | Select-Object -First 1).FullName
$sceneExe=Join-Path $root 'cadence_scene.exe'
& "$llvm/bin/clang++.exe" -std=c++20 -O2 "-I$repo/include" "$PSScriptRoot/cadence_scene.cpp" -o $sceneExe -ld3d11 -ldxgi -luser32 -lwinmm -ld3dcompiler
if($LASTEXITCODE){throw 'Fixture build failed'}
Copy-Item "$llvm/bin/libc++.dll","$llvm/bin/libunwind.dll" $root
$sourceArgs="`"$SourceSeconds`" --fps=$SourceFps --av --present-interval=0 --frame-log=`"$root/source-frames.csv`" --reference-first=`"$root/source-first.ppm`""
$source=Start-Process $sceneExe -ArgumentList $sourceArgs -WindowStyle Hidden -RedirectStandardOutput "$root/source.log" -PassThru
$previousRoot=$env:KLIP_DATA_ROOT
try {
  $env:KLIP_DATA_ROOT=$root
  $capture=Start-Process $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
} finally {$env:KLIP_DATA_ROOT=$previousRoot}
@{root=$root;klip_pid=$capture.Id;source_pid=$source.Id;executable=$exe} |
  ConvertTo-Json | Set-Content "$root/processes.json"
Get-Content "$root/processes.json"
