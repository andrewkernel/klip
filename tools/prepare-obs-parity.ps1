param(
  [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$')][string]$Name='obs-parity-profile',
  [ValidateSet(60,120)][int]$Fps=60,
  [ValidateRange(120,1800)][int]$SourceSeconds=900,
  [switch]$Launch
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root=Join-Path $repo "work/$Name"
if(Test-Path $root){throw 'Use a fresh parity name; existing profiles and evidence are preserved'}
& "$PSScriptRoot/prepare-obs.ps1"
if($LASTEXITCODE){throw 'Pinned OBS preparation failed'}
$archive=Join-Path $repo 'work/obs-runtime-32.1.2/official.zip'
$sha='8d97e4563bd8d22d03e63042aa7dccede1d555c9bd35ce8a9e5019b0d0201bf6'
if((Get-FileHash $archive).Hash.ToLowerInvariant() -ne $sha){throw 'OBS archive hash mismatch'}
$studio=Join-Path $root 'studio'
Expand-Archive -LiteralPath $archive -DestinationPath $studio
$config=Join-Path $studio 'config/obs-studio'
$profile=Join-Path $config 'basic/profiles/KlipParity'
$scenes=Join-Path $config 'basic/scenes'
$media=Join-Path $root 'Media'
New-Item -ItemType Directory -Force $profile,$scenes,$media | Out-Null
$version=(32 -shl 24) -bor (1 -shl 16) -bor (2 -shl 8)
@('[General]',"LastVersion=$version",'Pre31Migrated=true') | Set-Content "$config/global.ini" -Encoding utf8
@('[General]','FirstRun=true','HotkeyFocusType=NeverDisableHotkeys',
  '[Basic]','Profile=KlipParity','ProfileDir=KlipParity','SceneCollection=KlipParity','SceneCollectionFile=KlipParity',
  '[BasicWindow]','PreviewEnabled=false','SysTrayEnabled=true','SysTrayWhenStarted=false',
  '[Video]','AdapterIdx=0') | Set-Content "$config/user.ini" -Encoding utf8
$hotkey={param($Key) @{bindings=@(@{key="OBS_KEY_$Key";control=$true;alt=$true;shift=$true})} | ConvertTo-Json -Compress -Depth 5}
$replayHotkey=@{'ReplayBuffer.Save'=@(@{key='OBS_KEY_F12';control=$true;alt=$true;shift=$true})} | ConvertTo-Json -Compress -Depth 5
@('[General]','Name=KlipParity','[Output]','Mode=Advanced','FilenameFormatting=obs_%CCYY-%MM-%DD_%hh-%mm-%ss',
  'OverwriteIfExists=false','[AdvOut]','RecType=Standard',"RecFilePath=$($media.Replace('\','/'))",'RecFormat2=mkv',
  'RecEncoder=obs_nvenc_h264_tex','RecAudioEncoder=ffmpeg_aac','RecTracks=1','Track1Bitrate=192',
  'RecUseRescale=false','RecRescaleFilter=0','RecRB=true','RecRBTime=15','RecRBSize=256','RecFileNameWithoutSpace=true',
  'Encoder=obs_x264','AudioEncoder=ffmpeg_aac','[SimpleOutput]','RecRBPrefix=Replay','RecRBSuffix=',
  '[Video]','BaseCX=1920','BaseCY=1080','OutputCX=1920','OutputCY=1080','FPSType=0',"FPSCommon=$Fps",
  'ScaleType=bicubic','ColorFormat=NV12','ColorSpace=709','ColorRange=Partial',
  '[Audio]','SampleRate=48000','ChannelSetup=Stereo','[Hotkeys]',
  ('OBSBasic.StartRecording='+(& $hotkey F10)),('OBSBasic.StopRecording='+(& $hotkey F11)),
  "ReplayBuffer=$replayHotkey") | Set-Content "$profile/basic.ini" -Encoding utf8
$encoder=@{rate_control='CQP';cqp=18;preset='p5';tune='hq';multipass='qres';profile='high';bf=2;
  adaptive_quantization=$true;lookahead=$false;keyint_sec=2}
$encoder | ConvertTo-Json | Set-Content "$profile/recordEncoder.json" -Encoding utf8
$capture=@{name='Parity capture';id='game_capture';versioned_id='game_capture';mixers=0;enabled=$true;
  settings=@{capture_mode='window';window='Klip cadence test scene:KlipCadenceScene:cadence_scene.exe';
    capture_cursor=$false;limit_framerate=$false;capture_audio=$false;anti_cheat_hook=$true}}
$scene=@{name='KlipParity';id='scene';versioned_id='scene';settings=@{items=@(@{name='Parity capture';id=1;
  visible=$true;locked=$true;pos=@{x=0;y=0};scale=@{x=1;y=1};rot=0;align=5;
  bounds_type=1;bounds_align=0;bounds=@{x=1920;y=1080}})}}
$desktop=@{name='Desktop Audio';id='wasapi_output_capture';versioned_id='wasapi_output_capture';
  volume=1.0;mixers=1;enabled=$true;settings=@{device_id='default'}}
@{name='KlipParity';current_scene='KlipParity';current_program_scene='KlipParity';
  scene_order=@(@{name='KlipParity'});sources=@($capture,$scene);DesktopAudioDevice1=$desktop;
  current_transition='Cut';transition_duration=0;transitions=@(@{name='Cut';id='cut_transition';settings=@{}})} |
  ConvertTo-Json -Depth 12 | Set-Content "$scenes/KlipParity.json" -Encoding utf8
# The marker and --portable both keep this run out of the user's normal OBS profile.
'' | Set-Content "$studio/portable_mode.txt"
@{obs_version='32.1.2';obs_archive_sha256=$sha;source_commit='fb4d98bf88fae5fc85cb11fc57f7c5e309282194';
  settings=@{fps=$Fps;size='1920x1080';color='NV12/709/limited';audio='48k stereo/AAC192/desktop only';
    replay_seconds=15;replay_memory_mib=256;encoder=$encoder;capture_cursor=$false;preview=$false};
  limitations='Controlled D3D11 source, not a Fortnite gameplay benchmark';root=$root} |
  ConvertTo-Json -Depth 8 | Set-Content "$root/protocol.json"
if($Launch){
  if(Get-Process Klip,obs64,cadence_scene -ErrorAction SilentlyContinue){throw 'Close other capture/fixture processes before a parity run'}
  $llvm=(Get-ChildItem "$repo/work/validation-tools/llvm" -Directory | Select-Object -First 1).FullName
  $sceneExe=Join-Path $root 'cadence_scene.exe'
  & "$llvm/bin/clang++.exe" -std=c++20 -O2 "-I$repo/include" "$PSScriptRoot/cadence_scene.cpp" -o $sceneExe -ld3d11 -ldxgi -luser32 -lwinmm -ld3dcompiler
  if($LASTEXITCODE){throw 'Fixture build failed'}
  Copy-Item "$llvm/bin/libc++.dll","$llvm/bin/libunwind.dll" $root
  $sourceArgs="`"$SourceSeconds`" --fps=$Fps --av --present-interval=0 --frame-log=`"$root/source-frames.csv`" --reference-first=`"$root/source-first.ppm`""
  $source=Start-Process $sceneExe -ArgumentList $sourceArgs -WindowStyle Hidden -RedirectStandardOutput "$root/source.log" -PassThru
  $exe=Join-Path $studio 'bin/64bit/obs64.exe'
  $process=Start-Process $exe -WorkingDirectory (Split-Path $exe) -ArgumentList '--portable --only-bundled-plugins --disable-updater --profile KlipParity --collection KlipParity --startreplaybuffer' -PassThru
  @{root=$root;obs_pid=$process.Id;source_pid=$source.Id;executable=$exe} | ConvertTo-Json | Set-Content "$root/processes.json"
  Get-Content "$root/processes.json"
} else {Write-Output "Prepared isolated OBS comparison at $root"}
