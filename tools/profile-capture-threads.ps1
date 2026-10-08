param(
  [Parameter(Mandatory)][int]$CapturePid,
  [Parameter(Mandatory)][string]$EvidenceRoot,
  [ValidateRange(5,120)][int]$Seconds=20
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath $EvidenceRoot).Path
$output=Join-Path $root 'thread-profile.json'
if(Test-Path $output){throw 'Existing thread evidence is preserved'}
# Read-only thread metadata; no app UI control, injection or private-memory reads.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class KlipThreadMetadata {
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenThread(uint access, bool inherit, uint id);
  [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr thread, out IntPtr text);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
  public static string Name(uint id) {
    IntPtr handle=OpenThread(0x0800,false,id), text=IntPtr.Zero;
    if(handle==IntPtr.Zero) return "unavailable (OpenThread error " + Marshal.GetLastWin32Error() + ")";
    try { int hr=GetThreadDescription(handle,out text); return hr>=0 && text!=IntPtr.Zero ? Marshal.PtrToStringUni(text) : "unavailable (HRESULT " + hr + ")"; }
    finally { if(text!=IntPtr.Zero) LocalFree(text); CloseHandle(handle); }
  }
}
'@
$process=Get-Process -Id $CapturePid
$identity=$process.StartTime.ToUniversalTime().Ticks
$logical=(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$before=@{}
foreach($thread in $process.Threads){
  try {$before[$thread.Id]=@{cpu=$thread.TotalProcessorTime.TotalSeconds;
      start=$thread.StartTime.ToUniversalTime().Ticks;name=[KlipThreadMetadata]::Name($thread.Id)}}catch{}
}
$processCpuBefore=$process.TotalProcessorTime.TotalSeconds
$clock=[Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $Seconds
$process=Get-Process -Id $CapturePid
if($process.StartTime.ToUniversalTime().Ticks -ne $identity){throw 'Process identity changed'}
$elapsed=$clock.Elapsed.TotalSeconds
$processCpuAfter=$process.TotalProcessorTime.TotalSeconds
$rows=@()
foreach($thread in $process.Threads){
  try {
    $first=$before[$thread.Id]
    if($first -and $thread.StartTime.ToUniversalTime().Ticks -eq $first.start){
      $delta=$thread.TotalProcessorTime.TotalSeconds-$first.cpu
      $rows+=@{id=$thread.Id;name=$first.name;cpu_seconds=$delta;cpu_machine_percent=100*$delta/$elapsed/$logical}
    }
  }catch{}
}
@{pid=$CapturePid;executable=$process.Path;duration_seconds=$elapsed;
  cpu_machine_percent=100*($processCpuAfter-$processCpuBefore)/$elapsed/$logical;
  threads=@($rows|Sort-Object cpu_seconds -Descending);
  limitations='Read-only CPU-time deltas for surviving thread identities. Exited/new threads omitted; thread names may be unavailable. This is not a stack trace or GPU profiler.'} |
  ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $output -Encoding utf8
Get-Content -LiteralPath $output
