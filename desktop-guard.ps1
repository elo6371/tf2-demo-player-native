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
#     blocked compositor would look like from inside a fullscreen application;
#   * how often a VISIBLE top-level window belonging to -Watch was on screen.
#     That last one is the sensitivity signal that does not depend on Windows'
#     foreground rules: a background process cannot take the foreground from the
#     guard (the window manager flashes its taskbar button instead), so "a shown
#     product window steals the foreground" is not an experiment this machine can
#     run. "A shown product window EXISTS and is visible" is, and its absence is
#     exactly what --hidden-window claims.
#
# WHAT IT IS NOT
#   It is a borderless topmost fullscreen window, not a Direct3D exclusive-mode
#   swap chain. A hidden window cannot change the display mode either way, but
#   "an exclusive fullscreen game keeps its flip queue" is NOT proven here and
#   must not be claimed from this file.
param(
  [int]$Seconds = 20,
  [string]$OutFile = "guard.txt",
  [string]$Watch = "",
  [int]$HandOffMs = 6000,
  [string]$Phase = ""
)

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class GuardWin {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);

  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  // SwitchToThisWindow is the same request a click on the taskbar button makes: it asks
  // the shell to switch to a window. It does not inject input into anybody else's window,
  // which is why it is here and a synthetic key press is not: the instrument must never
  // change the desktop it is measuring. (A synthesized ALT press would land on whatever
  // has focus and could open a menu in the operator's application -- the instrument
  // interfering with the very thing the test is named after.)
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool altTab);

  // AttachThreadInput is a pair, never a single call: an attach that is not undone leaves
  // this thread sharing its input queue with the foreground thread for the rest of the
  // process's life. The symptom is a guard that still runs but stops receiving some
  // messages, and a target window that starts receiving keystrokes -- i.e. the instrument
  // starts changing the thing it measures. Every path below detaches in a finally, and the
  // count of outstanding attaches is reported so the instrument can assert it is zero.
  public static int attachedOutstanding = 0;
  // Named AttachedCount, not AttachedOutstanding: PowerShell resolves members
  // case-insensitively, so a method whose name differs from the field only by case is
  // shadowed by the field and the call fails with "does not contain a method".
  public static int AttachedCount() { return attachedOutstanding; }

  static bool AttachTo(IntPtr fg) {
    uint pid;
    uint fgThread = GetWindowThreadProcessId(fg, out pid);
    uint me = GetCurrentThreadId();
    if (fgThread == 0 || fgThread == me) { return false; }
    if (AttachThreadInput(fgThread, me, true)) { attachedOutstanding++; return true; }
    return false;
  }
  static void DetachFrom(IntPtr fg) {
    uint pid;
    uint fgThread = GetWindowThreadProcessId(fg, out pid);
    uint me = GetCurrentThreadId();
    if (fgThread != 0 && fgThread != me) {
      AttachThreadInput(fgThread, me, false);
      if (attachedOutstanding > 0) { attachedOutstanding--; }
    }
  }

  public static bool IsForegroundPid(uint pid) {
    IntPtr fg = GetForegroundWindow();
    if (fg == IntPtr.Zero) { return false; }
    uint p;
    GetWindowThreadProcessId(fg, out p);
    return p == pid;
  }

  // SetForegroundWindow is refused unless the caller is already the foreground process,
  // and it reports success in a way that does not always match what happened. So: attach
  // to the foreground thread, try several documented ways in order of increasing
  // bluntness, and then ASK THE SYSTEM who owns the foreground instead of trusting the
  // return value. Only the system's answer counts.
  public static bool TryForeground(IntPtr h, uint pid, int attempt) {
    IntPtr fg = GetForegroundWindow();
    bool attached = AttachTo(fg);
    try {
      bool ok = SetForegroundWindow(h);
      if (!ok) { BringWindowToTop(h); ok = SetForegroundWindow(h); }
      if (!ok && attempt >= 3) { SwitchToThisWindow(h, false); }
      System.Threading.Thread.Sleep(30);
      return IsForegroundPid(pid);
    } finally {
      if (attached) { DetachFrom(fg); }
    }
  }
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool EnumDisplaySettings(string dev, int mode, ref DEVMODE dm);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
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
  // Visible top-level windows owned by a process whose name matches. The process
  // name comes from the pid of each window, so a renamed executable is followed
  // rather than assumed.
  public static List<IntPtr> VisibleOf(string processName) {
    var found = new List<IntPtr>();
    var pids = new Dictionary<uint, string>();
    foreach (var p in System.Diagnostics.Process.GetProcesses()) {
      try { pids[(uint)p.Id] = p.ProcessName; } catch { }
    }
    EnumWindows((h, l) => {
      if (!IsWindowVisible(h)) return true;
      uint pid; GetWindowThreadProcessId(h, out pid);
      string name;
      if (pids.TryGetValue(pid, out name) && string.Equals(name, processName, StringComparison.OrdinalIgnoreCase)) {
        found.Add(h);
      }
      return true;
    }, IntPtr.Zero);
    return found;
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
$script:watchVisibleSamples = 0
$script:watchWindowFound = 0
$script:handOffSent = 0
$script:handOffAttempts = 0
$script:watchTicks = 0
$script:lastStartupTryMs = -1000
$script:startupForegroundOk = 0
$script:startupForegroundMs = -1
$script:foregroundSamples = 0
$script:everForeground = 0
$script:firstForegroundMs = -1
$script:lastForegroundMs = -1
$script:handOffOk = 0
$script:handOffAt = -1
$script:watchHwnd = [IntPtr]::Zero
$script:watchPid = 0
$script:handOffSelfOk = 0
# Wall-clock bounds of the observation, so a caller can check that the guard was
# looking at the moment it claims to be talking about. A guard that stopped before
# the product had a window reports every counter as a clean zero, which is the same
# reading as a product that never interfered.
$script:startEpochMs = 0
$script:stopEpochMs = 0

$form = New-Object System.Windows.Forms.Form
$form.FormBorderStyle = "None"
$form.WindowState = "Maximized"
$form.TopMost = $true
$form.BackColor = [System.Drawing.Color]::FromArgb(12, 12, 14)
$form.ShowInTaskbar = $false
$form.Text = "desktop-guard"
$form.Cursor = [System.Windows.Forms.Cursors]::Default

# The operator complained about a black screen: a blank rectangle looks like a
# crash. A label that says what is happening costs nothing and answers "is this
# thing working" while it holds the screen.
$label = New-Object System.Windows.Forms.Label
$label.Dock = "Fill"
$label.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
$label.ForeColor = [System.Drawing.Color]::FromArgb(150, 150, 158)
$label.Font = New-Object System.Drawing.Font("Segoe UI", 26, [System.Drawing.FontStyle]::Regular)
$labelText = "non-interference test" + [Environment]::NewLine + [Environment]::NewLine +
  "$Seconds s" + $(if ($Phase) { " / $Phase" } else { "" }) + [Environment]::NewLine +
  $(if ($Watch) { "watching for a window of: $Watch" } else { "watching for foreground theft" }) +
  [Environment]::NewLine + [Environment]::NewLine +
  "this screen is held by an automated check and will close by itself"
$label.Text = $labelText
$form.Controls.Add($label)

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
  # Take the foreground at startup, and keep trying for the first seconds. A process
  # started from a shell is not the foreground process, so Windows refuses the plain
  # call: measured with the plain call, this guard ran 14 s with everForeground=0 --
  # a fullscreen window that nobody ever activated. That makes "the product never took
  # the foreground from me" vacuous, because there was nothing to take, and it makes
  # Deactivate unable to fire at all. The same attach-and-ask route the hand-off uses
  # is used here, so the guard actually becomes the thing it claims to stand in for.
  if ($script:everForeground -eq 0 -and $script:sinceShownMs -gt 100 -and
      $script:sinceShownMs -lt 9000 -and
      ($script:sinceShownMs - $script:lastStartupTryMs) -ge 300) {
    $script:lastStartupTryMs = $script:sinceShownMs
    if ([GuardWin]::TryForeground($form.Handle, $script:myPid, 9)) {
      $script:startupForegroundOk = 1
      $script:startupForegroundMs = $script:sinceShownMs
    }
  }
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
  # Was this guard EVER the foreground process? Without this reading, "deactivations=0"
  # has two indistinguishable explanations: the guard held the foreground and nobody
  # took it (good), or the guard never got it in the first place (in which case a
  # Deactivate event could not have fired and the counter proves nothing). Counted
  # before the 400 ms settling cut below, because the activation happens during it.
  if ($fgPid -eq $script:myPid) {
    $script:foregroundSamples++
    $script:everForeground = 1
    if ($script:firstForegroundMs -lt 0) { $script:firstForegroundMs = $script:sinceShownMs }
    $script:lastForegroundMs = $script:sinceShownMs
  }
  if ($script:sinceShownMs -lt 400) { return }
  if ($fgPid -ne $script:myPid) {
    $script:stolen++
    $name = "?"
    try { $name = (Get-Process -Id $fgPid -ErrorAction Stop).ProcessName } catch { $name = "pid-$fgPid" }
    if ($script:thieves -notcontains $name) { $script:thieves += $name }
  }
})

# A slower scan for the sensitivity signal: does a visible top-level window of
# -Watch exist right now? 200 ms is fine for "was it on screen during this phase"
# and cheap enough to leave the 20 ms foreground timer alone.
$watchTimer = New-Object System.Windows.Forms.Timer
$watchTimer.Interval = 200
$watchTimer.add_Tick({
  if (-not $Watch) { return }
  $found = [GuardWin]::VisibleOf($Watch)
  if ($found.Count -gt 0) {
    $script:watchVisibleSamples++
    if ($script:watchWindowFound -eq 0) {
      $script:watchWindowFound = 1
      $script:watchHwnd = $found[0]
      $wp = 0
      [void][GuardWin]::GetWindowThreadProcessId($script:watchHwnd, [ref]$wp)
      $script:watchPid = $wp
    }
  }
  # Hand the foreground to the watched window once, so the instrument proves it can
  # ATTRIBUTE a foreground change to that process by name. Windows does not let a
  # background process take the foreground from the guard, so a shown product
  # window cannot demonstrate this by itself; the guard, being the foreground
  # process, can give it away. Without this step a dead counter and a quiet product
  # produce the same reading.
  # The hand-off waits for a window to exist. A capture run opens its archives for
  # ~18 s before it shows anything, so a fixed delay from the start of the guard fires
  # long before there is a window to hand the foreground to -- which is one of the ways
  # a control phase can look like it ran and prove nothing.
  # Retried, not fired once. Two things make a single attempt unreliable: the window
  # only exists from ~19.5 s, and Windows refuses SetForegroundWindow from a process
  # that is not itself foreground -- on this machine the guard lost the foreground 1327
  # times in 42 s while waiting (chrome, msedge, SearchHost, explorer). So each retry
  # takes the foreground back first, then hands it over, a few times, and the attempt
  # count is reported so a refusal is visible instead of looking like a quiet product.
  if ($Watch -and $script:handOffOk -eq 0 -and $script:handOffAttempts -lt 8 -and
      $script:watchHwnd -ne [IntPtr]::Zero -and $script:sinceShownMs -ge $HandOffMs) {
    $script:watchTicks++
    if (($script:watchTicks % 5) -eq 0) {
      $script:handOffAttempts++
      if ($script:handOffAt -lt 0) { $script:handOffAt = $script:sinceShownMs; $script:handOffSent = 1 }
      # Take the foreground back first (the guard is the only process allowed to give it
      # away), then hand it over, and let the SYSTEM say whether it moved: SetForegroundWindow's
      # return value is not the same question as "who owns the foreground now".
      [void][GuardWin]::TryForeground($form.Handle, $script:myPid, 9)
      if ([GuardWin]::IsForegroundPid($script:myPid)) { $script:handOffSelfOk = 1 }
      if ([GuardWin]::TryForeground($script:watchHwnd, $script:watchPid, $script:handOffAttempts)) {
        $script:handOffOk = 1
      }
    }
  }
})
$watchTimer.Start()

$modeBefore = [GuardWin]::Mode()

$form.add_Shown({
  $script:guardHwnd = $form.Handle
  [GuardWin]::SetForegroundWindow($form.Handle) | Out-Null
  $script:lastTick = [Environment]::TickCount
  $script:shownAt = $script:lastTick
  $script:sinceShownMs = 0
  $script:startEpochMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
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
$killer.add_Tick({
  $timer.Stop()
  $flush.Stop()
  $watchTimer.Stop()
  $script:deactAtEnd = $script:deactivations
  $script:stopEpochMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
  $form.Close()
})
$killer.Start()

[void]$form.ShowDialog()

$modeAfter = [GuardWin]::Mode()
$modeChanged = 0
if ($modeBefore.dmPelsWidth -ne $modeAfter.dmPelsWidth) { $modeChanged++ }
if ($modeBefore.dmPelsHeight -ne $modeAfter.dmPelsHeight) { $modeChanged++ }
if ($modeBefore.dmDisplayFrequency -ne $modeAfter.dmDisplayFrequency) { $modeChanged++ }
if ($modeBefore.dmBitsPerPel -ne $modeAfter.dmBitsPerPel) { $modeChanged++ }

$attributed = 0
if ($Watch -and ($script:thieves -contains $Watch)) { $attributed = 1 }

$lines = @(
  "guard.seconds=$Seconds",
  "guard.deactivations=$script:deactAtEnd",
  "guard.activations=$script:activations",
  "guard.foregroundStolenSamples=$script:stolen",
  "guard.worstGapMs=$script:worstGapMs",
  "guard.modeChangedFields=$modeChanged",
  "guard.stolenBy=$($script:thieves -join ',')",
  "guard.noForegroundSamples=$script:noForeground",
  "guard.foregroundSamples=$script:foregroundSamples",
  "guard.everForeground=$script:everForeground",
  "guard.startupForegroundOk=$script:startupForegroundOk",
  "guard.startupForegroundMs=$script:startupForegroundMs",
  "guard.firstForegroundMs=$script:firstForegroundMs",
  "guard.lastForegroundMs=$script:lastForegroundMs",
  "guard.stolenByCount=$($script:thieves.Count)",
  "guard.startEpochMs=$script:startEpochMs",
  "guard.stopEpochMs=$script:stopEpochMs",
  "guard.coveredMs=$($script:stopEpochMs - $script:startEpochMs)",
  "guard.watch=$Watch",
  "guard.watchVisibleSamples=$script:watchVisibleSamples",
  "guard.watchWindowFound=$script:watchWindowFound",
  "guard.handOffSent=$script:handOffSent",
  "guard.handOffAttempts=$script:handOffAttempts",
  "guard.handOffSelfOk=$script:handOffSelfOk",
  "guard.attachedThreadsOutstanding=$([GuardWin]::AttachedCount())",
  "guard.handOffOk=$script:handOffOk",
  "guard.handOffAttributed=$attributed",
  "guard.modeBefore=$($modeBefore.dmPelsWidth)x$($modeBefore.dmPelsHeight)@$($modeBefore.dmDisplayFrequency)Hz/$($modeBefore.dmBitsPerPel)bpp",
  "guard.modeAfter=$($modeAfter.dmPelsWidth)x$($modeAfter.dmPelsHeight)@$($modeAfter.dmDisplayFrequency)Hz/$($modeAfter.dmBitsPerPel)bpp"
)
$lines | Set-Content -Path $OutFile -Encoding ASCII
$lines | ForEach-Object { Write-Output $_ }
