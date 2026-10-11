$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archive=Join-Path $repo 'work/obs-deps-headers-2025-08-23.zip'
$sha='8de229cff6f1981508c0eb646b35e644633a5855787b9f5d3b90ae2aeb87ffc1'
if(-not(Test-Path $archive)) {
  & curl.exe -L --fail --silent --show-error 'https://github.com/obsproject/obs-deps/releases/download/2025-08-23/windows-deps-2025-08-23-x64.zip' -o $archive
  if($LASTEXITCODE){throw 'Could not obtain the pinned FFmpeg headers'}
}
if((Get-FileHash $archive).Hash.ToLowerInvariant() -ne $sha){throw 'OBS dependency archive hash mismatch'}
$destination=Join-Path $repo 'work/obs-clip-sdk'
if(-not(Test-Path "$destination/include/libavformat/avformat.h")) {
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip=[IO.Compression.ZipFile]::OpenRead($archive)
  try {
    foreach($entry in $zip.Entries) {
      if($entry.FullName -match '^include/(libavcodec|libavformat|libavutil|libswscale)/.+\.h$') {
        $target=Join-Path $destination $entry.FullName
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$true)
      }
    }
  } finally {$zip.Dispose()}
}
$llvm=(Get-ChildItem "$repo/work/validation-tools/llvm" -Directory | Select-Object -First 1).FullName
New-Item -ItemType Directory -Force "$destination/lib" | Out-Null
foreach($dll in @('avformat-61','avcodec-61','avutil-59','swscale-8')) {
  $binary="$repo/work/obs-sdk-32.1.2/bin/64bit/$dll.dll"
  if(-not(Test-Path $binary)){throw "Missing pinned runtime $dll"}
  if(Test-Path "$destination/lib/lib$dll.dll.a"){continue}
  $exports=@(& "$llvm/bin/llvm-readobj.exe" --coff-exports $binary | ForEach-Object {if($_ -match '^  Name: (.+)$'){$Matches[1]}})
  @("LIBRARY $dll.dll",'EXPORTS') + $exports | Set-Content "$destination/lib/$dll.def" -Encoding ascii
  & "$llvm/bin/llvm-dlltool.exe" -m i386:x86-64 -d "$destination/lib/$dll.def" -l "$destination/lib/lib$dll.dll.a"
  if($LASTEXITCODE){throw "Import library failed: $dll"}
}
Write-Output "Pinned trim SDK prepared: $destination"
