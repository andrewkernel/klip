param(
  [Parameter(Mandatory)][string]$PackageDirectory,
  [Parameter(Mandatory)][string]$BuildDirectory,
  [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$Name
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version=& "$repo/packaging/resolve-version.ps1"
$package=(Resolve-Path $PackageDirectory).Path
$build=(Resolve-Path $BuildDirectory).Path
$run=Join-Path $repo "work/$Name"
if(Test-Path $run){throw 'Use a fresh verification directory'}
New-Item -ItemType Directory $run | Out-Null
Expand-Archive -LiteralPath "$package/Klip-$version-win64-portable.zip" -DestinationPath $run
$root=Join-Path $run "Klip-$version-win64"
$manifest=Get-Content "$root/release-manifest.json" -Raw | ConvertFrom-Json
if($manifest.version -ne $version -or $manifest.test_hooks){throw 'Invalid package version or enabled diagnostic hooks'}
foreach($file in $manifest.files){
  if($file.path -match '(^|[\\/])klip_.*tests\.exe$|update-fixture'){throw 'Test fixture shipped'}
  if((Get-FileHash -LiteralPath (Join-Path $root $file.path)).Hash.ToLowerInvariant() -ne $file.sha256){throw "Package file mismatch: $($file.path)"}
}
foreach($exe in @('Klip.exe','KlipUpdater.exe')){
  if((Get-FileHash "$root/bin/64bit/$exe").Hash -ne (Get-FileHash "$build/bin/64bit/$exe").Hash){throw "Capture-tested binary mismatch: $exe"}
}
if((Get-Item "$root/bin/64bit/Klip.exe").VersionInfo.ProductVersion -ne $version){throw 'Executable version mismatch'}
$previousRoot=$env:KLIP_DATA_ROOT
try {
  $env:KLIP_DATA_ROOT=Join-Path $run 'isolated-data'
  $probe=Start-Process "$root/Klip.exe" -ArgumentList '--package-smoke-test' -WorkingDirectory $root -WindowStyle Hidden -PassThru
  if(-not $probe.WaitForExit(15000) -or $probe.ExitCode){throw 'Extracted package loader/settings probe failed'}
} finally {$env:KLIP_DATA_ROOT=$previousRoot}
Write-Output "Verified $($manifest.files.Count) packaged files, exact final app/helper and isolated startup: $root"
