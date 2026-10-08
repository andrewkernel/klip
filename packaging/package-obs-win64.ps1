param(
  [Parameter(Mandatory)][string]$BuildDirectory,
  [Parameter(Mandatory)][string]$SourcesDirectory,
  [string]$OutputDirectory='',
  [string]$InnoSetupCompiler=''
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version=& "$PSScriptRoot/resolve-version.ps1"
$build=(Resolve-Path $BuildDirectory).Path
$sources=(Resolve-Path $SourcesDirectory).Path
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repo "artifacts/Klip-$version"}
if(Test-Path $OutputDirectory){throw 'Use a fresh output directory; existing release artifacts are preserved'}
$cache=Get-Content "$build/CMakeCache.txt" -Raw
if($cache -notmatch 'KLIP_USE_LIBOBS:BOOL=(ON|TRUE|1)' -or $cache -notmatch 'KLIP_ENABLE_TEST_HOOKS:BOOL=(OFF|FALSE|0)'){throw 'Release requires libobs with diagnostic hooks disabled'}
if(-not(Test-Path "$sources/dependency-sources.json")){throw 'Corresponding dependency sources are required'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
$stage=Join-Path $out "Klip-$version-win64"
New-Item -ItemType Directory $stage | Out-Null
foreach($name in @('bin','data','obs-plugins','licenses')){Copy-Item -LiteralPath "$build/$name" -Destination $stage -Recurse}
foreach($name in @('LICENSE','EULA.txt','PRIVACY.md','README.md','THIRD_PARTY_NOTICES.md')){Copy-Item -LiteralPath "$repo/$name" -Destination $stage}
Copy-Item "$build/obs-pin.json" $stage
$imgui=(Get-ChildItem "$repo/work/validation-tools/imgui-1.92.8" -Directory | Select-Object -First 1).FullName
$llvm=(Get-ChildItem "$repo/work/validation-tools/llvm" -Directory | Select-Object -First 1).FullName
Copy-Item "$imgui/LICENSE.txt" "$stage/licenses/ImGui-MIT.txt"
Copy-Item "$llvm/LICENSE.TXT" "$stage/licenses/LLVM-runtime.txt"
Copy-Item "$repo/LICENSE" "$stage/licenses/Klip-GPL.txt"
Copy-Item "$sources/dependency-sources.json" "$stage/licenses/dependency-sources.json"
foreach($sourceTree in @("$repo/work/release-source-checkouts","$repo/work/obs-source-32.1.2/deps","$llvm/x86_64-w64-mingw32/share/mingw32")){
  foreach($file in Get-ChildItem $sourceTree -Recurse -File | Where-Object Name -Match '^(LICENSE|COPYING|COPYRIGHT|LICENCE)'){
    $relative=[IO.Path]::GetRelativePath($sourceTree,$file.FullName)
    $target=Join-Path "$stage/licenses/upstream" $relative
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item $file.FullName $target -Force
  }
}
$sevenZip=(Get-Command 7z.exe -ErrorAction SilentlyContinue).Source
if(-not $sevenZip){$sevenZip='C:/Program Files/7-Zip/7z.exe'}
if(-not(Test-Path $sevenZip)){throw '7-Zip is required to collect archive license notices'}
foreach($archive in Get-ChildItem $sources -File | Where-Object Name -Match '^(v1\.|libtheora|lame-)'){
  $target=Join-Path "$stage/licenses/archive-notices" $archive.BaseName
  & $sevenZip x $archive.FullName "-o$target" '-r' '*LICENSE*' '*COPYING*' '*COPYRIGHT*' '-y' '-bso0'
  if($LASTEXITCODE -gt 1){throw "License extraction failed: $($archive.Name)"}
}
Copy-Item "$repo/packaging/PORTABLE-README.txt" "$stage/START-HERE.txt"
& "$llvm/bin/clang++.exe" -std=c++20 -O2 -municode -mwindows -static -nostdlib++ "$PSScriptRoot/launcher.cpp" "$build/CMakeFiles/klip_app.dir/klip.rc.res" -o "$stage/Klip.exe" -luser32
if($LASTEXITCODE){throw 'Root compatibility launcher compilation failed'}
$fileManifest=Get-ChildItem $stage -Recurse -File | ForEach-Object {@{path=[IO.Path]::GetRelativePath($stage,$_.FullName);sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}}
@{version=$version;engine='libobs 32.1.2';test_hooks=$false;files=$fileManifest} | ConvertTo-Json -Depth 4 | Set-Content "$stage/release-manifest.json"
$portable=Join-Path $out "Klip-$version-win64-portable.zip"
Compress-Archive -Path $stage -DestinationPath $portable -CompressionLevel Optimal
if(-not $InnoSetupCompiler){$InnoSetupCompiler="$env:LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe"}
if(-not(Test-Path $InnoSetupCompiler)){throw 'Inno Setup compiler required'}
& $InnoSetupCompiler "/DAppVersion=$version" "/DBuildRoot=$stage" "/DOutputRoot=$out" "$PSScriptRoot/ClippingSetup.iss"
if($LASTEXITCODE){throw 'Installer compilation failed'}
$sourceArchive=Join-Path $out "Klip-$version-corresponding-sources.zip"
& git -C $repo archive --format=zip "--output=$sources/Klip-$version-source.zip" HEAD
if($LASTEXITCODE){throw 'Klip corresponding source archive failed'}
Copy-Item "$repo/docs/release-2.0.md" "$sources/BUILDING.md"
Compress-Archive -Path "$sources/*" -DestinationPath $sourceArchive -CompressionLevel Optimal
$artifacts=@(Get-Item "$out/Klip-$version-win64-setup.exe",$portable,$sourceArchive)
$artifacts | ForEach-Object {'{0}  {1}' -f (Get-FileHash $_.FullName).Hash.ToLowerInvariant(),$_.Name} | Set-Content "$out/SHA256SUMS.txt" -Encoding ascii
Write-Output "Packaged unsigned public beta: $out"
