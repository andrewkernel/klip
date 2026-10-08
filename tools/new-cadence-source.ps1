param([int]$Fps=120, [int]$Seconds=30, [string]$OutputPath='')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ffmpeg=(Get-ChildItem "$repo/work/validation-tools/ffmpeg-8.1.2" -Directory | Select-Object -First 1).FullName+'/bin/ffmpeg.exe'
$filters=@('drawbox=x=0:y=0:w=iw:h=96:color=black:t=fill')
for($bit=0;$bit -lt 12;$bit++) {
  $divisor=[math]::Pow(2,$bit)
  $filters += "drawbox=x=$($bit*80):y=8:w=64:h=64:color=white:t=fill:enable='mod(floor(n/$divisor),2)'"
}
$filters += "drawbox=x=0:y=ih-96:w=iw:h=96:color=white:t=fill:enable='lt(mod(n,$Fps),$([int]($Fps*.05)))'"
$output=if([string]::IsNullOrWhiteSpace($OutputPath)){"$repo/work/cadence-source-$Fps.mp4"}else{$OutputPath}
& $ffmpeg -hide_banner -loglevel warning -y -f lavfi -i "testsrc2=size=1280x720:rate=$Fps" `
  -f lavfi -i 'aevalsrc=if(lt(mod(t\,1)\,0.05)\,0.2*sin(2*PI*1000*t)\,0):s=48000' `
  -vf ($filters -join ',') -t $Seconds -c:v h264_nvenc -preset p3 -b:v 16000000 -c:a aac $output
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Write-Output $output
