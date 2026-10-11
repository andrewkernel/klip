[CmdletBinding()]
param([string]$KeyName='Klip.ReleaseSigning.v1')
$ErrorActionPreference='Stop'
# CNG stores the non-exportable private key under the current Windows account.
# This script never writes private key bytes, credentials, or secrets to the repo.
if([Security.Cryptography.CngKey]::Exists($KeyName)) {
  $key=[Security.Cryptography.CngKey]::Open($KeyName)
} else {
  $parameters=[Security.Cryptography.CngKeyCreationParameters]::new()
  $parameters.ExportPolicy=[Security.Cryptography.CngExportPolicies]::None
  $key=[Security.Cryptography.CngKey]::Create([Security.Cryptography.CngAlgorithm]::ECDsaP256,$KeyName,$parameters)
}
try {
  $public=$key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
  Write-Output ([Convert]::ToHexString($public).ToLowerInvariant())
} finally {$key.Dispose()}
