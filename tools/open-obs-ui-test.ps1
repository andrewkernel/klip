param([ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$')][string]$Name='obs-ui-verification',
  [string]$Executable)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root=Join-Path $repo "work/$Name"
if(Test-Path $root){throw 'Use a fresh test name to preserve settings evidence'}
New-Item -ItemType Directory $root | Out-Null
@('obs_replay_enabled=false','capture_preview_enabled=true','clip_duration_seconds=42',
  'rolling_buffer_seconds=60','hotkey_modifiers=16391','hotkey_save_virtual_key=133',
  'hotkey_record_virtual_key=134','hotkey_toggle_ui_virtual_key=135') | Set-Content "$root/settings.ini"
$previousRoot=$env:KLIP_DATA_ROOT
try {
  $env:KLIP_DATA_ROOT=$root
  $exe=if($Executable){(Resolve-Path -LiteralPath $Executable).Path}else{Join-Path $repo 'build-obs-Release/bin/64bit/Klip.exe'}
  # This is the interactive dashboard under inspection, not a background helper.
  $process=Start-Process $exe -WorkingDirectory (Split-Path $exe) -PassThru
  [pscustomobject]@{pid=$process.Id;data_root=$root;executable=$exe} | ConvertTo-Json
} finally {$env:KLIP_DATA_ROOT=$previousRoot}
