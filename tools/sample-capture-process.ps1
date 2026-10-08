param(
  [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$CapturePid,
  [int]$SourcePid=0,
  [Parameter(Mandatory)][string]$EvidenceRoot,
  [ValidateRange(5,300)][int]$Seconds=20,
  [ValidateSet('replay','replay-recording','saving','idle')][string]$Phase='replay'
)
$ErrorActionPreference='Stop'
if($Phase -ne 'idle' -and $SourcePid -lt 1){throw 'A live source PID is required outside idle measurements'}
if($Phase -ne 'idle' -and $SourcePid -eq $CapturePid){throw 'Capture and source must be different processes'}
$root=(Resolve-Path -LiteralPath $EvidenceRoot).Path
$output=Join-Path $root "$Phase-resources.json"
if(Test-Path $output){throw 'Existing resource samples are preserved; choose a fresh run/phase'}
$cpus=(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$processIds=@($CapturePid)
if($Phase -ne 'idle'){$processIds+= $SourcePid}
$clock=[Diagnostics.Stopwatch]::StartNew()
$samples=[Collections.Generic.List[object]]::new()
$previous=@{}
$identities=@{}
foreach($processId in $processIds){
  $process=Get-Process -Id $processId -ErrorAction SilentlyContinue
  if(-not $process -and ($processId -eq $CapturePid -or $Phase -ne 'idle')){
    throw "Required process $processId is not running"
  }
  if($process){$identities[$processId]=$process.StartTime.ToUniversalTime().Ticks}
}
$valid=$true
$invalidReason=$null
while($clock.Elapsed.TotalSeconds -lt $Seconds){
  if($Phase -ne 'idle' -and -not (Get-Process -Id $SourcePid -ErrorAction SilentlyContinue)){
    $valid=$false; $invalidReason='Source process ended during measurement'; break
  }
  $gpu=$null
  $gpuError=$null
  try {
    $filter="Name LIKE 'pid_${CapturePid}_%'"
    if($Phase -ne 'idle'){$filter+=" OR Name LIKE 'pid_${SourcePid}_%'"}
    $gpu=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -Filter $filter)
  } catch {$gpuError=$_.Exception.Message}
  $at=$clock.Elapsed.TotalSeconds
  $rows=@()
  foreach($processId in $processIds){
    $process=Get-Process -Id $processId -ErrorAction SilentlyContinue
    if(-not $process){
      if($processId -eq $CapturePid -or $Phase -ne 'idle'){
        $valid=$false; $invalidReason="Required process $processId ended during measurement"
      }
      continue
    }
    if(-not $identities.ContainsKey($processId) -or $process.StartTime.ToUniversalTime().Ticks -ne $identities[$processId]){
      $valid=$false; $invalidReason="Process identity changed for $processId"; continue
    }
    $cpu=$process.TotalProcessorTime.TotalSeconds
    $usage=$null
    if($previous.ContainsKey($processId)){
      $last=$previous[$processId]
      $usage=100*($cpu-$last.cpu)/($at-$last.time)/$cpus
    }
    $previous[$processId]=@{cpu=$cpu;time=$at}
    $engines=@($gpu | Where-Object {$_.Name.StartsWith("pid_${processId}_")})
    $threeD=@($engines | Where-Object {$_.Name -match 'engtype_(3D|Compute)'} | Select-Object -ExpandProperty UtilizationPercentage)
    $encode=@($engines | Where-Object {$_.Name -match 'engtype_VideoEncode'} | Select-Object -ExpandProperty UtilizationPercentage)
    $rows+=@{pid=$processId;cpu_machine_percent=$usage;private_bytes=$process.PrivateMemorySize64;
      working_set_bytes=$process.WorkingSet64;
      gpu_3d_or_compute_max_percent=$(if($threeD.Count){($threeD|Measure-Object -Maximum).Maximum}else{$null});
      gpu_video_encode_max_percent=$(if($encode.Count){($encode|Measure-Object -Maximum).Maximum}else{$null});
      raw_gpu_engines=@($engines|Select-Object Name,UtilizationPercentage)}
  }
  $samples.Add(@{elapsed_seconds=$at;utc=[DateTime]::UtcNow.ToString('o');processes=$rows;gpu_error=$gpuError})
  if(-not $valid){break}
  if(-not ($rows | Where-Object pid -eq $CapturePid)){break}
  Start-Sleep -Milliseconds 1000
}
@{phase=$Phase;valid=$valid;invalid_reason=$invalidReason;logical_processors=$cpus;duration_seconds=$clock.Elapsed.TotalSeconds;samples=$samples.ToArray();
  limitations='Parent-process CPU/RAM and Windows formatted per-process GPU counters; helper-process peaks are not included. GPU max is the busiest matching engine, not a sum or total system GPU load.'} |
  ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $output -Encoding utf8
Write-Output "Saved measured samples: $output"
