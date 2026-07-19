[CmdletBinding()]
param(
  [string]$ReleaseTag = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cmakeText = Get-Content -LiteralPath (Join-Path $repositoryRoot "CMakeLists.txt") -Raw
$cmakeMatch = [regex]::Match(
  $cmakeText,
  'project\(Klip\s+VERSION\s+(?<version>\d+\.\d+\.\d+)\s+LANGUAGES')
if (-not $cmakeMatch.Success) {
  throw "Could not resolve Klip's semantic version from CMakeLists.txt."
}
$version = $cmakeMatch.Groups["version"].Value

$manifest = Get-Content -LiteralPath (Join-Path $repositoryRoot "vcpkg.json") -Raw |
  ConvertFrom-Json
$manifestVersion = $manifest.'version-semver'
if ($manifestVersion -ne $version) {
  throw "Version mismatch: CMakeLists.txt is $version but vcpkg.json is $manifestVersion."
}

if (-not [string]::IsNullOrWhiteSpace($ReleaseTag)) {
  $tagMatch = [regex]::Match($ReleaseTag, '^v(?<version>\d+\.\d+\.\d+)$')
  if (-not $tagMatch.Success) {
    throw "Release tag '$ReleaseTag' must use the exact form vMAJOR.MINOR.PATCH."
  }
  $tagVersion = $tagMatch.Groups["version"].Value
  if ($tagVersion -ne $version) {
    throw "Release tag $ReleaseTag does not match the project version $version."
  }
}

Write-Output $version
