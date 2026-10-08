param([string]$Destination='', [string]$LlvmBin='')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $Destination){$Destination=Join-Path $repo 'work/obs-sdk-32.1.2'}
if(-not $LlvmBin){$LlvmBin=Join-Path (Get-ChildItem "$repo/work/validation-tools/llvm" -Directory | Select-Object -First 1).FullName 'bin'}
$source=Join-Path $repo 'work/obs-source-32.1.2'
$download=Join-Path $repo 'work/obs-runtime-32.1.2'
$commit='fb4d98bf88fae5fc85cb11fc57f7c5e309282194'
$sha='8d97e4563bd8d22d03e63042aa7dccede1d555c9bd35ce8a9e5019b0d0201bf6'
if(-not (Test-Path $source)){
  & git clone --depth 1 --branch 32.1.2 https://github.com/obsproject/obs-studio.git $source
  if($LASTEXITCODE){throw 'Could not obtain pinned OBS source'}
}
if((& git -C $source rev-parse HEAD) -ne $commit){throw 'OBS source does not match the pinned commit'}
New-Item -ItemType Directory -Force $download | Out-Null
$archive=Join-Path $download 'official.zip'
if(-not (Test-Path $archive)){
  & curl.exe -L --fail --silent --show-error 'https://github.com/obsproject/obs-studio/releases/download/32.1.2/OBS-Studio-32.1.2-Windows-x64.zip' -o $archive
  if($LASTEXITCODE){throw 'Could not download official OBS runtime'}
}
if((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha){throw 'OBS runtime SHA-256 mismatch'}
$official=Join-Path $download 'official'
if(-not (Test-Path "$official/bin/64bit/obs.dll")){Expand-Archive $archive $official}
$modules=@('win-capture','win-wasapi','obs-nvenc','obs-qsv11','obs-ffmpeg','obs-x264','image-source','obs-filters')
if(Test-Path "$Destination/pin.json"){
  $existing=Get-Content "$Destination/pin.json" -Raw | ConvertFrom-Json
  $missing=@($modules | Where-Object {$_ -notin $existing.modules -or -not (Test-Path "$Destination/obs-plugins/64bit/$_.dll")})
  if($existing.schema_revision -eq 2 -and -not $missing.Count){Write-Output "Pinned SDK already prepared: $Destination"; exit 0}
}
New-Item -ItemType Directory -Force "$Destination/bin/64bit","$Destination/obs-plugins/64bit","$Destination/data/obs-plugins","$Destination/include","$Destination/lib","$Destination/licenses" | Out-Null
function Copy-DataTree([string]$From,[string]$To){
  # Copy verified inputs without recursively deleting any existing destination.
  # PDB files are omitted at the source instead of removed after copying.
  foreach($file in Get-ChildItem -LiteralPath $From -Recurse -File){
    if($file.Extension -eq '.pdb'){continue}
    $relative=[IO.Path]::GetRelativePath($From,$file.FullName)
    $target=Join-Path $To $relative
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
  }
}
$pending=[Collections.Generic.Queue[string]]::new()
foreach($name in $modules){
  $dll="$official/obs-plugins/64bit/$name.dll"
  Copy-Item $dll "$Destination/obs-plugins/64bit/" -Force
  $pending.Enqueue($dll)
  Copy-DataTree "$official/data/obs-plugins/$name" "$Destination/data/obs-plugins/$name"
}
foreach($name in @('obs.dll','libobs-d3d11.dll','libobs-winrt.dll','obs-nvenc-test.exe','obs-qsv-test.exe','obs-amf-test.exe','obs-ffmpeg-mux.exe')){
  Copy-Item "$official/bin/64bit/$name" "$Destination/bin/64bit/"
  $pending.Enqueue("$official/bin/64bit/$name")
}
# Resolve the official PE dependency closure; system/driver DLLs remain OS prerequisites.
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while($pending.Count){
  $binary=$pending.Dequeue()
  foreach($line in (& "$LlvmBin/llvm-readobj.exe" --coff-imports $binary)){
    if($line -match '^  Name: (.+)$'){
      $name=$Matches[1]
      if($seen.Add($name) -and (Test-Path "$official/bin/64bit/$name")){
        if($name -match '^(Qt|obs-frontend|obs-scripting|libcef)'){throw "Unexpected frontend dependency: $name"}
        Copy-Item "$official/bin/64bit/$name" "$Destination/bin/64bit/"
        $pending.Enqueue("$official/bin/64bit/$name")
      }
    }
  }
}
Copy-DataTree "$official/data/libobs" "$Destination/data/libobs"
Copy-DataTree "$source/libobs" "$Destination/include/libobs"
Copy-Item "$source/COPYING" "$Destination/licenses/OBS-GPL-2.0.txt"
# Generate a C import library for the existing LLVM/MinGW validation toolchain.
$exports=@(& "$LlvmBin/llvm-readobj.exe" --coff-exports "$Destination/bin/64bit/obs.dll" | ForEach-Object {if($_ -match '^  Name: (.+)$'){$Matches[1]}})
@('LIBRARY obs.dll','EXPORTS') + $exports | Set-Content "$Destination/lib/obs.def" -Encoding ascii
& "$LlvmBin/llvm-dlltool.exe" -m i386:x86-64 -d "$Destination/lib/obs.def" -l "$Destination/lib/libobs.dll.a"
if($LASTEXITCODE){throw 'Could not generate libobs import library'}
@{schema_revision=2;version='32.1.2';source_commit=$commit;runtime_sha256=$sha;modules=$modules} | ConvertTo-Json | Set-Content "$Destination/pin.json" -Encoding utf8
Write-Output "Pinned minimal OBS SDK prepared: $Destination"
