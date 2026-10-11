[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Installer,
  [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
  [Parameter(Mandatory)][string]$Output,
  [string]$KeyName='Klip.ReleaseSigning.v1',
  [ValidateRange(1,21)][int]$ValidDays=14
)
$ErrorActionPreference='Stop'
if(Test-Path $Output){throw 'Use a new output path; signed manifests are not overwritten'}
$file=Get-Item -LiteralPath $Installer
if($file.Name -ne "Klip-$Version-win64-setup.exe"){throw 'Installer filename/version mismatch'}
if($file.VersionInfo.ProductVersion.Trim() -ne $Version){throw 'Installer embedded version does not match the manifest; refusing to sign'}
if($file.Length -lt 65536 -or $file.Length -gt 268435456){throw 'Installer size is outside the updater limits'}
$key=[Security.Cryptography.CngKey]::Open($KeyName)
$signer=[Security.Cryptography.ECDsaCng]::new($key)
try {
  $hash=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()
  $expires=[DateTimeOffset]::UtcNow.AddDays($ValidDays).ToUnixTimeSeconds()
  $payload="klip-update-v1`n$Version`nhttps://klip-ten-zeta.vercel.app/Klip-$Version-win64-setup.exe`n$hash`n$($file.Length)`n$expires`n"
  $signature=$signer.SignData([Text.Encoding]::UTF8.GetBytes($payload),[Security.Cryptography.HashAlgorithmName]::SHA256)
  [IO.File]::WriteAllText([IO.Path]::GetFullPath($Output),$payload+[Convert]::ToHexString($signature).ToLowerInvariant()+"`n",[Text.UTF8Encoding]::new($false))
  Write-Output "Signed update manifest: $Output (expires in $ValidDays days)"
} finally {$signer.Dispose();$key.Dispose()}
