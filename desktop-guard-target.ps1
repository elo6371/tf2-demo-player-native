# desktop-guard-target.ps1 -- the stand-in window used to prove that the guard's
# foreground counter can fire for a window it did not create.
#
# WHY THIS FILE EXISTS
#   A guard that records "nobody stole the foreground" proves nothing until
#   something has been made to steal it once. The product cannot play that role:
#   whether its window activates is a product decision, and a capture path is
#   supposed to create its window without activating it. So this script shows a
#   plain window from its own process, asks for the foreground, and writes down
#   whether Windows accepted the request. The guard, running alongside, must record
#   the theft BY NAME. If it does not, its "quiet" reading is a broken counter.
#
# WRITES  $env:T49_OUT  one line:
#   <out>|setForegroundWindow=<True|False>|foregroundPid=<pid>|myPid=<pid>
param(
  [string]$Out = ""
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class StealProbe {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
"@
if (-not $Out) { $Out = $env:T49_OUT }
$f = New-Object System.Windows.Forms.Form
$f.Text = "guard instrument target"
$f.Width = 460; $f.Height = 200
$f.StartPosition = "CenterScreen"
$l = New-Object System.Windows.Forms.Label
$l.Text = "asking for the foreground"
$l.AutoSize = $true
$l.Location = New-Object System.Drawing.Point(30, 60)
$f.Controls.Add($l)
$t = New-Object System.Windows.Forms.Timer
$t.Interval = 1500
$t.add_Tick({
  $t.Stop()
  $ok = [StealProbe]::SetForegroundWindow($f.Handle)
  $fg = [StealProbe]::GetForegroundWindow()
  $fgPid = 0
  [void][StealProbe]::GetWindowThreadProcessId($fg, [ref]$fgPid)
  "$Out|setForegroundWindow=$ok|foregroundPid=$fgPid|myPid=$PID" | Set-Content -Path $Out -Encoding ASCII
})
$t.Start()
[void]$f.ShowDialog()
