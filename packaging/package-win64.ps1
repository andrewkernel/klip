[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDirectory,
  [Parameter(Mandatory = $true)]
  [string]$FfmpegRoot,
  [string]$Version = "",
  [string]$OutputDirectory = "",
  [string]$VcpkgInstalledDirectory = "",
  [string]$InnoSetupCompiler = "",
  [switch]$SkipInstaller,
  [switch]$FetchFfmpegSource
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Version)) {
  $Version = & (Join-Path $PSScriptRoot "resolve-version.ps1")
}
if ($Version -notmatch '^\d+\.\d+\.\d+(?:[-.][0-9A-Za-z.-]+)?$') {
  throw "Version must be a filesystem-safe semantic version, for example 0.3.0 or 0.3.0-beta.1."
}

$buildRoot = (Resolve-Path $BuildDirectory).Path
$ffmpegPath = (Resolve-Path $FfmpegRoot).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repositoryRoot "artifacts"
}
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$stageRoot = Join-Path $outputRoot "Klip-$Version-win64"
$portableArchive = Join-Path $outputRoot "Klip-$Version-win64-portable.zip"

$executable = Join-Path $buildRoot "Klip.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
  throw "Klip.exe was not found in $buildRoot"
}
$requiredDllPatterns = @("avcodec-*.dll", "avformat-*.dll", "avutil-*.dll", "swresample-*.dll")
foreach ($pattern in $requiredDllPatterns) {
  if (-not (Get-ChildItem -LiteralPath $buildRoot -Filter $pattern -File)) {
    throw "Required FFmpeg runtime $pattern was not found in $buildRoot"
  }
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
if (Test-Path -LiteralPath $stageRoot) {
  Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $stageRoot | Out-Null
Copy-Item -LiteralPath $executable -Destination $stageRoot
Get-ChildItem -LiteralPath $buildRoot -Filter "*.dll" -File |
  Copy-Item -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $repositoryRoot "README.md") -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $repositoryRoot "EULA.txt") -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $repositoryRoot "PRIVACY.md") -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md") -Destination $stageRoot

$licensesRoot = Join-Path $stageRoot "licenses"
$ffmpegLicenses = Join-Path $licensesRoot "ffmpeg"
New-Item -ItemType Directory -Force -Path $ffmpegLicenses | Out-Null
$ffmpegNotices = @(Get-ChildItem -LiteralPath $ffmpegPath -File |
  Where-Object { $_.Name -match "^(LICENSE|COPYING|README)" })
$vcpkgFfmpegCopyright = Join-Path $ffmpegPath "share\ffmpeg\copyright"
if (Test-Path -LiteralPath $vcpkgFfmpegCopyright -PathType Leaf) {
  $ffmpegNotices += Get-Item -LiteralPath $vcpkgFfmpegCopyright
}
if ($ffmpegNotices.Count -eq 0) {
  throw "The FFmpeg SDK does not contain a root license/readme notice"
}
$ffmpegNotices | Copy-Item -Destination $ffmpegLicenses

if (-not [string]::IsNullOrWhiteSpace($VcpkgInstalledDirectory)) {
  $imguiCopyright = Join-Path $VcpkgInstalledDirectory "x64-windows-static\share\imgui\copyright"
  if (-not (Test-Path -LiteralPath $imguiCopyright -PathType Leaf)) {
    throw "Dear ImGui's vcpkg copyright file was not found at $imguiCopyright"
  }
  $imguiLicenses = Join-Path $licensesRoot "dear-imgui"
  New-Item -ItemType Directory -Force -Path $imguiLicenses | Out-Null
  Copy-Item -LiteralPath $imguiCopyright -Destination (Join-Path $imguiLicenses "LICENSE.txt")
}

$ffmpegExecutable = @(
  (Join-Path $ffmpegPath "bin\ffmpeg.exe"),
  (Join-Path $ffmpegPath "tools\ffmpeg\ffmpeg.exe")
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ($ffmpegExecutable) {
  $versionText = (& $ffmpegExecutable -hide_banner -version 2>&1 | Out-String)
  $buildText = (& $ffmpegExecutable -hide_banner -buildconf 2>&1 | Out-String)
  Set-Content -LiteralPath (Join-Path $ffmpegLicenses "BUILD-AND-VERSION.txt") -Encoding UTF8 `
    -Value ($versionText + [Environment]::NewLine + $buildText)
  if ($FetchFfmpegSource) {
    $match = [regex]::Match($versionText, "-g(?<hash>[0-9a-f]{7,40})(?:-|\s|$)")
    if (-not $match.Success) {
      throw "Could not identify the FFmpeg source revision from ffmpeg -version"
    }
    $revision = $match.Groups["hash"].Value
    $sourceArchive = Join-Path $outputRoot "ffmpeg-source-$revision.zip"
    Invoke-WebRequest -UseBasicParsing `
      -Uri "https://github.com/FFmpeg/FFmpeg/archive/$revision.zip" `
      -OutFile $sourceArchive
  }
}

if (Test-Path -LiteralPath $portableArchive) {
  Remove-Item -LiteralPath $portableArchive -Force
}
Compress-Archive -Path (Join-Path $stageRoot "*") -DestinationPath $portableArchive `
  -CompressionLevel Optimal

if (-not $SkipInstaller -and [string]::IsNullOrWhiteSpace($InnoSetupCompiler)) {
  $InnoSetupCompiler = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
  ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $SkipInstaller -and [string]::IsNullOrWhiteSpace($InnoSetupCompiler)) {
  throw "Inno Setup 6 was not found. Install it, pass -InnoSetupCompiler, or explicitly use -SkipInstaller."
}
if (-not $SkipInstaller) {
  $compiler = (Resolve-Path $InnoSetupCompiler).Path
  & $compiler "/DAppVersion=$Version" "/DBuildRoot=$stageRoot" `
    "/DOutputRoot=$outputRoot" (Join-Path $PSScriptRoot "ClippingSetup.iss")
  if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed with exit code $LASTEXITCODE"
  }
}

Write-Host "Portable package: $portableArchive"
Write-Host "Staged application: $stageRoot"
if ($SkipInstaller) {
  Write-Host "Installer creation was explicitly skipped."
} else {
  Write-Host "Installer: $(Join-Path $outputRoot "Klip-$Version-win64-setup.exe")"
}

$artifacts = @($portableArchive)
if (-not $SkipInstaller) {
  $artifacts += Join-Path $outputRoot "Klip-$Version-win64-setup.exe"
}
$checksums = foreach ($artifact in $artifacts) {
  if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
    throw "Expected release artifact was not created: $artifact"
  }
  $hash = Get-FileHash -LiteralPath $artifact -Algorithm SHA256
  "{0}  {1}" -f $hash.Hash.ToLowerInvariant(), (Split-Path -Leaf $artifact)
}
$checksumFile = Join-Path $outputRoot "SHA256SUMS.txt"
Set-Content -LiteralPath $checksumFile -Value $checksums -Encoding ascii
Write-Host "Checksums: $checksumFile"
