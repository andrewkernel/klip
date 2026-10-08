param([string]$Destination='')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $Destination){$Destination=Join-Path $repo 'work/release-sources-2.0.0'}
New-Item -ItemType Directory -Force $Destination | Out-Null
$Destination=(Resolve-Path $Destination).Path
$recipes=Join-Path $repo 'work/obs-deps-2025-08-23'
if(-not(Test-Path $recipes)){
  git clone --depth 1 --branch 2025-08-23 https://github.com/obsproject/obs-deps.git $recipes
  if($LASTEXITCODE){throw 'OBS dependency recipes unavailable'}
}
if((& git -C $recipes rev-parse HEAD) -ne '21e25b2b508598ce8239de9ecd68400e45559399'){throw 'OBS dependency recipe revision mismatch'}
$required=@('FFmpeg','x264','opus','libogg','libvorbis','libvpx','svt-av1','aom',
  'libtheora','lame','mbedtls','srt','librist','nv-codec-headers','amf','zlib',
  'curl','jansson','rnnoise','speexdsp','freetype','detours','vpl','wil','zstd')
$manifest=[Collections.Generic.List[object]]::new()
foreach($script in Get-ChildItem "$recipes/deps.ffmpeg","$recipes/deps.windows" -Filter '*.ps1'){
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile($script.FullName,[ref]$tokens,[ref]$errors)
  $defaults=@{}
  foreach($param in $ast.ParamBlock.Parameters){$defaults[$param.Name.VariablePath.UserPath]=$param.DefaultValue}
  if(-not $defaults.Name){continue}
  $name=$defaults.Name.SafeGetValue()
  if($name -notin $required -or $manifest.name -contains $name){continue}
  $uri=$defaults.Uri.SafeGetValue()
  $hash=if($defaults.Hash){$defaults.Hash.Extent.Text.Trim("'`"")}else{$defaults.Hashes.SafeGetValue().x64}
  $archive=Join-Path $Destination "$name-source.zip"
  if($uri -match '\.git$'){
    if($hash -notmatch '^[0-9a-f]{40}$'){throw "Unpinned Git source: $name"}
    $source=Join-Path $repo "work/release-source-checkouts/$name"
    if(-not(Test-Path "$source/.git")){
      New-Item -ItemType Directory -Force $source | Out-Null
      git -C $source init --quiet
      git -C $source remote add origin $uri
      git -C $source fetch --quiet --depth 1 origin $hash
      if($LASTEXITCODE){throw "Source fetch failed: $name"}
      git -C $source checkout --quiet --detach FETCH_HEAD
    }
    if((& git -C $source rev-parse HEAD) -ne $hash){throw "Source revision mismatch: $name"}
    git -C $source archive --format=zip "--output=$archive" HEAD
    if($LASTEXITCODE){throw "Source archive failed: $name"}
  }else{
    $hashPath=$hash.Replace('${PSScriptRoot}',$script.DirectoryName)
    $expected=(Import-Clixml -LiteralPath $hashPath).Hash.ToLowerInvariant()
    $archive=Join-Path $Destination ([IO.Path]::GetFileName(([uri]$uri).AbsolutePath))
    if(-not(Test-Path $archive)){
      curl.exe -fL --retry 2 --silent --show-error --max-time 180 $uri -o $archive
      if($LASTEXITCODE){throw "Source download failed: $name"}
    }
    if((Get-FileHash $archive).Hash.ToLowerInvariant() -ne $expected){throw "Source checksum mismatch: $name"}
  }
  $manifest.Add(@{name=$name;upstream=$uri;revision=$hash;archive=(Split-Path -Leaf $archive);sha256=(Get-FileHash $archive).Hash.ToLowerInvariant();recipe=$script.Name})
  Write-Output "Verified source: $name"
}
foreach($name in $required){if($manifest.name -notcontains $name){throw "Missing corresponding source: $name"}}
git -C $recipes archive --format=zip "--output=$Destination/obs-deps-recipes.zip" HEAD
git -C "$repo/work/obs-source-32.1.2" archive --format=zip "--output=$Destination/OBS-Studio-32.1.2-source.zip" HEAD
if($LASTEXITCODE){throw 'OBS source archive failed'}
Copy-Item "$repo/work/validation-tools/imgui-1.92.8.zip" "$Destination/imgui-1.92.8-source.zip"
$manifest.ToArray() | ConvertTo-Json -Depth 4 | Set-Content "$Destination/dependency-sources.json"
Write-Output "Corresponding upstream sources prepared: $Destination"
