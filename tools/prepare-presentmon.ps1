$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root=Join-Path $repo 'work/validation-tools/presentmon-2.4.1'
$exe=Join-Path $root 'PresentMon-2.4.1-x64.exe'
# Official release asset digest published by GameTechDev/PresentMon on GitHub.
$digest='d74183e7ae630f72cd3690be0373ecbfdc6cbb86578148aab8fa2a7166068f34'
New-Item -ItemType Directory -Force $root|Out-Null
if(-not (Test-Path $exe)){
  Invoke-WebRequest -Uri 'https://github.com/GameTechDev/PresentMon/releases/download/v2.4.1/PresentMon-2.4.1-x64.exe' -OutFile $exe
}
if((Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant() -ne $digest){throw 'PresentMon official asset digest mismatch; do not run it'}
Write-Output $exe
