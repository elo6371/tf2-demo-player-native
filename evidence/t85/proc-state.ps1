$p = Get-Process tf2_demo_native -ErrorAction SilentlyContinue
if ($p) {
  "Responding=$($p.Responding) Handles=$($p.HandleCount) Threads=$($p.Threads.Count)"
  $p.Threads | Group-Object WaitReason | Sort-Object Count -Descending | Select-Object Count,Name | Format-Table -AutoSize | Out-String
} else { "not running" }
Get-Process WerFault,ApplicationFrameHost -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,StartTime | Format-Table -AutoSize | Out-String