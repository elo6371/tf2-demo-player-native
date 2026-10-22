# desktop-guard.ps1 -- stand in for whatever the operator has on screen fullscreen,
# and measure whether anything the capture path does takes the desktop away from it.
#
# WHAT IT MEASURES (each one is a reading, not an opinion):
#   * how many times the guard lost the foreground (Win32 GetForegroundWindow),
#     sampled every 20 ms for the whole time it is up;
#   * how many times the guard was deactivated (the Form.Deactivate event, the
#     frame-level counterpart of the same question);
#   * whether the display mode changed underneath it (bounds, refresh, bpp);
#   * the worst gap between two samples, which is what a stolen frame or a
#     blocked compositor would look like from inside a fullscreen application.
#
# WHAT IT IS NOT
#   It is a borderless topmost fullscreen window, not a Direct3D exclusive-mode
#   swap chain. A hidden window cannot change the display mode either way, but
#   "an exclusive fullscreen game keeps its flip queue" is NOT proven here and
#   must not be claimed from this file.
param(
  [int]$Seconds = 20,
  [string]$OutFile = "guard.txt"
)

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class GuardWin {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool EnumDisplaySettings(string dev, int mode, ref DEVMODE dm);
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct DEVMODE {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
    public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
    public int dmFields;
    public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
    public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
    public short dmLogPixels;
    public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
    public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2;
  }
  public static DEVMODE Mode() {
    DEVMODE d = new DEVMODE();
    d.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
    EnumDisplaySettings(null, -1, ref d);
    return d;
  }
}
"@

$script:deactivations = 0
$script:activations = 0
$script:stolen = 0
$script:worstGapMs = 0
$script:guardHwnd = $null
$script:myPid = $PID
$script:thieves = @()
$script:noForeground = 0
$script:sinceShownMs = 0
$script:shownAt = 0

$form = New-Object System.Windows.Forms.Form
$form.FormBorderStyle = "None"
$form.WindowState = "Maximized"
$form.TopMost = $true
$form.BackColor = [System.Drawing.Color]::Black
$form.ShowInTaskbar = $false
$form.Text = "desktop-guard"

$form.add_Deactivate({ $script:deactivations++ })
$form.add_Activated({ $script:activations++ })

$script:lastTick = [Environment]::TickCount

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 20
$timer.add_Tick({
  $now = [Environment]::TickCount
  $script:sinceShownMs = $now - $script:shownAt
  $gap = $now - $script:lastTick
  if ($gap -gt $script:worstGapMs) { $script:worstGapMs = $gap }
  $script:lastTick = $now
  $fg = [GuardWin]::GetForegroundWindow()
  # Compare PROCESS, not HWND. The form's Handle and the foreground window can be
  # two different handles of this same process (the form is owned, the desktop
  # hands back the owner), and counting that as a theft would make every run look
  # guilty. What matters is whether a DIFFERENT process took the desktop.
  $fgPid = 0
  if ($fg -ne [IntPtr]::Zero) { [void][GuardWin]::GetWindowThreadProcessId($fg, [ref]$fgPid) }
  # No foreground window at all is not a theft, it is the desktop between owners
  # (it happens while the guard itself is still being activated). It is counted
  # separately so it can never be mistaken for interference.
  if ($fgPid -eq 0) { $script:noForeground++; return }
  if ($script:sinceShownMs -lt 400) { return }
  if ($fgPid -ne $script:myPid) {
    $script:stolen++
    $name = "?"
    try { $name = (Get-Process -Id $fgPid -ErrorAction Stop).ProcessName } catch { $name = "pid-$fgPid" }
    if ($script:thieves -notcontains $name) { $script:thieves += $name }
  }
})

$modeBefore = [GuardWin]::Mode()

$form.add_Shown({
  $script:guardHwnd = $form.Handle
  [GuardWin]::SetForegroundWindow($form.Handle) | Out-Null
  $script:lastTick = [Environment]::TickCount
  $script:shownAt = $script:lastTick
  $script:sinceShownMs = 0
  $timer.Start()
})

# Flush the counters to disk every few seconds. A guard that only writes on exit
# loses everything if it is killed -- and the run it was watching is exactly the
# thing worth keeping when something goes wrong.
$flush = New-Object System.Windows.Forms.Timer
$flush.Interval = 5000
$flush.add_Tick({
  $partial = @(
    "guard.partial=1",
    "guard.deactivations=$script:deactivations",
    "guard.activations=$script:activations",
    "guard.foregroundStolenSamples=$script:stolen",
    "guard.stolenBy=$($script:thieves -join ',')",
    "guard.noForegroundSamples=$script:noForeground",
    "guard.worstGapMs=$script:worstGapMs"
  )
  $partial | Set-Content -Path $OutFile -Encoding ASCII
})
$flush.Start()

$killer = New-Object System.Windows.Forms.Timer
$killer.Interval = [Math]::Max(1000, $Seconds * 1000)
# The close itself deactivates the form, so the count that matters is the one
# taken here, while the guard is still up. A reading sampled after Close would
# report 1 on a perfectly quiet run and hide a real second one.
$killer.add_Tick({ $timer.Stop(); $flush.Stop(); $script:deactAtEnd = $script:deactivations; $form.Close() })
$killer.Start()

[void]$form.ShowDialog()

$modeAfter = [GuardWin]::Mode()
$modeChanged = 0
if ($modeBefore.dmPelsWidth -ne $modeAfter.dmPelsWidth) { $modeChanged++ }
if ($modeBefore.dmPelsHeight -ne $modeAfter.dmPelsHeight) { $modeChanged++ }
if ($modeBefore.dmDisplayFrequency -ne $modeAfter.dmDisplayFrequency) { $modeChanged++ }
if ($modeBefore.dmBitsPerPel -ne $modeAfter.dmBitsPerPel) { $modeChanged++ }

$lines = @(
  "guard.seconds=$Seconds",
  "guard.deactivations=$script:deactAtEnd",
  "guard.activations=$script:activations",
  "guard.foregroundStolenSamples=$script:stolen",
  "guard.worstGapMs=$script:worstGapMs",
  "guard.modeChangedFields=$modeChanged",
  "guard.stolenBy=$($script:thieves -join ',')",
  "guard.noForegroundSamples=$script:noForeground",
  "guard.modeBefore=$($modeBefore.dmPelsWidth)x$($modeBefore.dmPelsHeight)@$($modeBefore.dmDisplayFrequency)Hz/$($modeBefore.dmBitsPerPel)bpp",
  "guard.modeAfter=$($modeAfter.dmPelsWidth)x$($modeAfter.dmPelsHeight)@$($modeAfter.dmDisplayFrequency)Hz/$($modeAfter.dmBitsPerPel)bpp"
)
$lines | Set-Content -Path $OutFile -Encoding ASCII
$lines | ForEach-Object { Write-Output $_ }
