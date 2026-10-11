[CmdletBinding()]
param([string]$BuildDirectory='build-obs-Release-updater-trimmer',[ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$Name='updater-e2e-20261011')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build=(Resolve-Path $BuildDirectory).Path
$run=Join-Path $repo "work/$Name"
if(Test-Path $run){throw 'Use a fresh test name'}
New-Item -ItemType Directory $run | Out-Null
$stage=Join-Path $run 'stage'
New-Item -ItemType Directory "$stage/bin/64bit" | Out-Null
Copy-Item "$build/update-fixture/bin/64bit/Klip.exe" "$stage/bin/64bit/Klip.exe"
[IO.File]::WriteAllText("$stage/updated.marker",'Isolated fixture upgrade 9.0.0 (never published)')
$payload=Join-Path $run 'payload'
New-Item -ItemType Directory $payload | Out-Null
$compiler=Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe'
& $compiler '/Qp' "/DBuildRoot=$stage" "/DOutputRoot=$payload" "$repo/tests/update_fixture.iss"
if($LASTEXITCODE){throw 'Isolated installer compilation failed'}
& "$PSScriptRoot/sign-update.ps1" -Installer "$payload/Klip-9.0.0-win64-setup.exe" -Version 9.0.0 -Output "$payload/update.manifest"
if($LASTEXITCODE){throw 'Fixture signing failed'}
& "$PSScriptRoot/sign-update.ps1" -Installer "$repo/artifacts/klip-2.0.1-beta/Klip-2.0.1-win64-setup.exe" -Version 2.0.1 -Output "$payload/current.manifest"
if($LASTEXITCODE){throw 'Current-version fixture signing failed'}
$failedPayload=Join-Path $run 'failure-payload'
New-Item -ItemType Directory $failedPayload | Out-Null
& $compiler '/Qp' '/DFailInstall=1' "/DBuildRoot=$stage" "/DOutputRoot=$failedPayload" "$repo/tests/update_fixture.iss"
if($LASTEXITCODE){throw 'Failed-install fixture compilation failed'}
& "$PSScriptRoot/sign-update.ps1" -Installer "$failedPayload/Klip-9.0.0-win64-setup.exe" -Version 9.0.0 -Output "$failedPayload/update.manifest"
if($LASTEXITCODE){throw 'Failed-install fixture signing failed'}
$productionPath='HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/{8A44B102-4E2D-4701-B01E-0B43D5C6C5A8}_is1'
$before=Get-ItemProperty -LiteralPath $productionPath -ErrorAction SilentlyContinue | Select-Object InstallLocation,DisplayVersion
$registryBase='Software\KlipUpdaterTests\'+[Guid]::NewGuid().ToString('N')
$previousRegistry=$env:KLIP_UPDATER_TEST_REGISTRY
$results=@()
try {
 foreach($case in @('current','bad-signature','corrupt','truncated','offline','cancel','unwritable','tamper-after-download','installer-failure','success')) {
  $root=Join-Path $run $case
  New-Item -ItemType Directory "$root/bin/64bit" | Out-Null
  Copy-Item "$build/update-fixture/bin/64bit/Klip.exe","$build/update-fixture/bin/64bit/KlipUpdater.exe" "$root/bin/64bit/"
  [IO.File]::WriteAllText("$root/settings-sentinel.ini",'keep settings')
  [IO.File]::WriteAllText("$root/clip-sentinel.mkv",'keep clip')
  if($case -eq 'installer-failure'){[IO.File]::WriteAllText("$root/expected-failure.marker",'expected recovery')}
  $virtualKey="$registryBase\$case"
  $locationKey="$virtualKey\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8A44B102-4E2D-4701-B01E-0B43D5C6C5A8}_is1"
  $key=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($locationKey)
  try {$key.SetValue('InstallLocation',$root)} finally {$key.Dispose()}
  $env:KLIP_UPDATER_TEST_REGISTRY=$virtualKey
  $stdout="$root/parent.stdout.txt";$stderr="$root/parent.stderr.txt"
  $casePayload=if($case -eq 'installer-failure'){$failedPayload}else{$payload}
  $process=Start-Process -FilePath "$root/bin/64bit/Klip.exe" -ArgumentList ('"'+$casePayload+'" '+$case) -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  if(-not $process.WaitForExit(30000)){throw "$case parent timed out"}
  if($process.ExitCode){throw "$case failed: $($process.ExitCode) $(Get-Content $stderr -Raw)"}
  if($case -eq 'success') {
   $deadline=[DateTime]::UtcNow.AddSeconds(45)
   while(-not(Test-Path "$root/restarted.ok") -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
   if(-not(Test-Path "$root/restarted.ok")){throw 'Installer/restart did not complete'}
   if(-not(Test-Path "$root/updated.marker")){throw 'Update file was not installed'}
  }
  if($case -eq 'installer-failure'){
   $deadline=[DateTime]::UtcNow.AddSeconds(45)
   while(-not(Test-Path "$root/recovered.ok") -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
   if(-not(Test-Path "$root/recovered.ok") -or -not(Test-Path "$root/failure-notice.txt") -or (Test-Path "$root/updated.marker")){throw 'Failed installer did not recover the original app'}
  }
  if((Get-Content "$root/settings-sentinel.ini" -Raw) -ne 'keep settings' -or (Get-Content "$root/clip-sentinel.mkv" -Raw) -ne 'keep clip'){throw 'Test settings/media changed'}
  $results += [pscustomobject]@{case=$case;passed=$true;detail=(Get-Content $stdout -Raw).Trim()}
  Write-Output "$case passed"
 }
 $after=Get-ItemProperty -LiteralPath $productionPath -ErrorAction SilentlyContinue | Select-Object InstallLocation,DisplayVersion
 if(($before|ConvertTo-Json -Compress) -ne ($after|ConvertTo-Json -Compress)){throw 'Production installation metadata unexpectedly changed'}
 $results | ConvertTo-Json | Set-Content "$run/results.json"
 Write-Output "Updater test evidence: $run"
} finally {
 $env:KLIP_UPDATER_TEST_REGISTRY=$previousRegistry
 # Exact owned test namespace only. Never remove an application/uninstall root.
 if($registryBase -notmatch '^Software\\KlipUpdaterTests\\[a-f0-9]{32}$'){throw 'Invalid test-registry cleanup target'}
 [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($registryBase,$false)
}
