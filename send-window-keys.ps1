# send-window-keys.ps1 -- post WM_KEYDOWN/WM_KEYUP to a running product window.
#
# WHY THIS EXISTS
#   The product's M / [ / ] handlers live in the window procedure, not in the
#   key-bind table. Bare M is the map-overview toggle (hud.overview); the
#   master-volume mute moved to Ctrl+M on 2026-11-17. A camera-script `key`
#   step cannot reach either: it calls dispatchHotkey and reports "unbound".
#   This helper sends the same messages Windows sends, so the acceptance run
#   exercises the product path rather than a parallel test API.
#
# -Modifiers synthesises held modifier keys (e.g. 17 = VK_CONTROL). PostMessage
#   does NOT update the target thread's key state, so the product reads
#   GetKeyState(VK_CONTROL) == 0 for a posted Ctrl+M and the chord arrives as a
#   bare M. AttachThreadInput + SetKeyboardState publishes the modifier in the
#   shared input state before the messages are posted; -HoldUntilExit keeps it
#   down until the product exits, because the product queues keys while its
#   load phase blocks the message pump and dispatches them later. The previous
#   state is restored and the threads detached afterwards. The gate proves the
#   chord actually arrived by asserting the product trace token
#   (source=key-ctrl-m), not by trusting this helper.
#
# The process is found by name because Git Bash's $! is a Cygwin pid, not the
# Windows pid EnumWindows needs. -CommandLineMatch pins the search to one run:
# the caller passes a string that only its own command line carries (the gate
# uses the run's unique --capture-frame path). Without it the newest same-name
# process is chosen, which is only safe while the host lock excludes other
# products - after an early crash it once posted to a foreign process.
param(
  [string]$ProcessName = "tf2_demo_native",
  [string]$CommandLineMatch = "",
  [string]$Keys = "",
  [string]$Modifiers = "",
  [int]$TimeoutSeconds = 150,
  [int]$GapMs = 300,
  [int]$WindowSettleMs = 2500,
  [int]$ModifierHoldMs = 0,
  [switch]$HoldUntilExit
)

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class WindowKeys {
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
  [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextLength(IntPtr hWnd);
  [DllImport("user32.dll", SetLastError = true)] private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr w, IntPtr l);
  [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
  [DllImport("user32.dll", SetLastError = true)] private static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
  [DllImport("user32.dll")] private static extern bool GetKeyboardState(byte[] lpKeyState);
  [DllImport("user32.dll")] private static extern bool SetKeyboardState(byte[] lpKeyState);

  private static uint selfTid;
  private static uint targetTid;
  private static bool attached;
  private static bool stateHeld;
  private static byte[] savedState;

  public static IntPtr FindMainWindow(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p;
      GetWindowThreadProcessId(h, out p);
      if (p == pid && GetWindowTextLength(h) > 0) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    if (found == IntPtr.Zero) {
      EnumWindows((h, l) => {
        uint p;
        GetWindowThreadProcessId(h, out p);
        if (p == pid) { found = h; return false; }
        return true;
      }, IntPtr.Zero);
    }
    return found;
  }

  public static bool Send(IntPtr hWnd, int vk) {
    bool down = PostMessage(hWnd, 0x0100, (IntPtr)vk, (IntPtr)1);
    System.Threading.Thread.Sleep(50);
    bool up = PostMessage(hWnd, 0x0101, (IntPtr)vk, (IntPtr)1);
    return down && up;
  }

  // Publish held modifier keys in the shared keyboard state. Returns false if
  // the attach or the state copy failed (the caller reports it; it does not
  // silently fall back to a bare key, because that would change the chord).
  public static bool AttachModifiers(IntPtr hWnd, int[] mods) {
    if (mods == null || mods.Length == 0) { return true; }
    uint pid;
    targetTid = GetWindowThreadProcessId(hWnd, out pid);
    selfTid = GetCurrentThreadId();
    if (targetTid == 0 || targetTid == selfTid) { return false; }
    attached = AttachThreadInput(selfTid, targetTid, true);
    if (!attached) { return false; }
    savedState = new byte[256];
    if (!GetKeyboardState(savedState)) { ReleaseModifiers(); return false; }
    byte[] working = (byte[])savedState.Clone();
    foreach (int m in mods) {
      if (m > 0 && m < 256) { working[m] = 0x80; }
    }
    if (!SetKeyboardState(working)) { ReleaseModifiers(); return false; }
    stateHeld = true;
    return true;
  }

  // Diagnostic: is the modifier still set in the shared keyboard state? The
  // gate prints this at post time and at release time, so a state that the
  // product resets mid-load is visible in the helper log rather than guessed.
  public static bool ModifierVisible(int vk) {
    byte[] st = new byte[256];
    if (!GetKeyboardState(st)) { return false; }
    return (st[vk] & 0x80) != 0;
  }

  public static void ReleaseModifiers() {
    if (stateHeld && savedState != null) { SetKeyboardState(savedState); }
    stateHeld = false;
    savedState = null;
    if (attached) { AttachThreadInput(selfTid, targetTid, false); }
    attached = false;
  }
}
"@

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$proc = $null
if ($CommandLineMatch -ne "") {
  # Target the exact product run. Several worktrees share this machine, so a
  # name-only search can attach to somebody else's tf2_demo_native when our own
  # process died during startup (measured 2026-11-17: the run crashed at 0.7 s,
  # the helper then attached to a foreign product and the gate blamed the keys).
  # Every key run passes a unique --capture-frame path; only the process this
  # gate launched carries it. A run that died before the helper started matches
  # nothing and fails here instead of posting keys somewhere else.
  $matchDeadline = (Get-Date).AddSeconds(20)
  while ((Get-Date) -lt $matchDeadline) {
    $cands = @(Get-CimInstance Win32_Process -Filter "Name='$ProcessName.exe'" -ErrorAction SilentlyContinue |
      Where-Object { $_.CommandLine -and $_.CommandLine.Contains($CommandLineMatch) })
    foreach ($cand in $cands) {
      $candProc = Get-Process -Id $cand.ProcessId -ErrorAction SilentlyContinue
      if ($null -ne $candProc) { $proc = $candProc; break }
    }
    if ($null -ne $proc) { break }
    Start-Sleep -Milliseconds 200
  }
  if ($null -eq $proc) {
    Write-Output "KEY-SEND=FAIL reason=process-not-found match=$CommandLineMatch"
    exit 2
  }
} else {
  while ((Get-Date) -lt $deadline) {
    $all = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
    if ($all.Count -gt 0) {
      $proc = $all | Sort-Object StartTime -Descending | Select-Object -First 1
      break
    }
    Start-Sleep -Milliseconds 200
  }
}
if ($null -eq $proc) {
  Write-Output "KEY-SEND=FAIL reason=process-not-found name=$ProcessName"
  exit 2
}

$hwnd = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
  if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) {
    Write-Output "KEY-SEND=FAIL reason=process-exited pid=$($proc.Id)"
    exit 2
  }
  $hwnd = [WindowKeys]::FindMainWindow([uint32]$proc.Id)
  if ($hwnd -ne [IntPtr]::Zero) { break }
  Start-Sleep -Milliseconds 200
}
if ($hwnd -eq [IntPtr]::Zero) {
  Write-Output "KEY-SEND=FAIL reason=no-window pid=$($proc.Id)"
  exit 1
}

# The window appears at the end of the load phase, but the pump and the UI volume
# model finish starting just after it. Posting at hwnd discovery races that
# startup: the queued edge can be dispatched before the volume model exists and
# then has no effect (observed with ] while muted). Settle first, then post into
# a running pump. The product still runs for the whole demo after this point.
if ($WindowSettleMs -gt 0) {
  Start-Sleep -Milliseconds $WindowSettleMs
}

$tokens = $Keys.Split(",") | Where-Object { $_.Trim() -ne "" }
if ($tokens.Count -eq 0) {
  Write-Output "KEY-SEND=FAIL reason=no-keys"
  exit 2
}
$mods = @()
foreach ($token in $Modifiers.Split(",")) {
  if ($token.Trim() -eq "") { continue }
  $mv = 0
  if (-not [int]::TryParse($token.Trim(), [ref]$mv)) {
    Write-Output "KEY-SEND=FAIL reason=bad-modifier token=$token"
    exit 2
  }
  $mods += $mv
}
$modCount = $mods.Count

$modsHeld = $true
if ($modCount -gt 0) {
  $modsHeld = [WindowKeys]::AttachModifiers($hwnd, [int[]]$mods)
  if ($modsHeld) {
    Write-Output "KEY-MODS=attach vks=$Modifiers mods=$modCount ok=1 pid=$($proc.Id)"
  } else {
    Write-Output "KEY-MODS=attach vks=$Modifiers mods=$modCount ok=0 pid=$($proc.Id)"
  }
}

$failed = $false
foreach ($token in $tokens) {
  $vk = 0
  if (-not [int]::TryParse($token.Trim(), [ref]$vk)) {
    Write-Output "KEY-SEND=FAIL reason=bad-vk token=$token"
    $failed = $true
    break
  }
  if (-not [WindowKeys]::Send($hwnd, $vk)) {
    Write-Output "KEY-SEND=FAIL reason=post-failed vk=$vk mods=$modCount pid=$($proc.Id)"
    $failed = $true
    break
  }
  Write-Output "KEY-SENT vk=$vk mods=$modCount pid=$($proc.Id) hwnd=$hwnd"
  Start-Sleep -Milliseconds $GapMs
}

if ($modCount -gt 0 -and $modsHeld) {
  $vis = [WindowKeys]::ModifierVisible([int]$mods[0])
  Write-Output "KEY-MODS=state stage=post vk=$($mods[0]) down=$(if ($vis) { 1 } else { 0 }) pid=$($proc.Id)"
}

# Hold the synthetic modifiers down through dispatch. Keys posted during the
# load phase wait in the queue, so releasing before the pump starts would make
# every chord arrive as a bare key.
if (-not $failed -and $modCount -gt 0) {
  if ($HoldUntilExit) {
    while (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue) {
      Start-Sleep -Milliseconds 200
    }
  } elseif ($ModifierHoldMs -gt 0) {
    Start-Sleep -Milliseconds $ModifierHoldMs
  }
}
if ($modCount -gt 0 -and $modsHeld) {
  $vis = [WindowKeys]::ModifierVisible([int]$mods[0])
  Write-Output "KEY-MODS=state stage=release vk=$($mods[0]) down=$(if ($vis) { 1 } else { 0 }) pid=$($proc.Id)"
}
if ($modCount -gt 0) {
  [WindowKeys]::ReleaseModifiers()
  Write-Output "KEY-MODS=release mods=$modCount held=$modsHeld pid=$($proc.Id)"
}

if ($failed) { exit 1 }
Write-Output "KEY-SEND=PASS count=$($tokens.Count) mods=$modCount pid=$($proc.Id)"