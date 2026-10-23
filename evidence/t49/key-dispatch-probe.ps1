<#
key-dispatch-probe.ps1 -- post two keys to the product's own window.

WHY POSTED MESSAGES AND NOT SYNTHESIZED INPUT
  The claim under test is about the product's dispatch table: H must reach the HUD and
  F9 must still reach T50's camera bindings after the two features were merged. That is
  a property of the program's own message handler, so the message goes to that window
  and nowhere else. Synthetic input (SendInput/keybd_event) would go to whatever owns
  the desktop at that instant, including the operator's editor -- and a harness that
  injects keystrokes into a live desktop to prove something about a hidden window is
  changing the thing it measures.

  PostMessage(WmKeyDown) to a specific HWND needs no focus, so this works while the
  window is hidden and while something else owns the foreground.

OUTPUT (key=value, one per line, ASCII)
  keyprobe.found / hwnd / pid / hSent / f9Sent / firstFoundMs / spawnMs
#>
param(
  [Parameter(Mandatory = $true)][string]$ProcessName,
  [Parameter(Mandatory = $true)][string]$Out,
  [int]$WaitMs = 240000,
  [int]$AfterKeyMs = 1500
)

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public class KeyProbe {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
  public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);

  public const uint WmKeyDown = 0x0100;
  public const uint WmKeyUp = 0x0101;

  // Top-level windows of one process. No IsWindowVisible filter: the forward phase runs
  // with --hidden-window, and the window this has to reach is exactly the invisible one.
  public static IntPtr FindWindow(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint owner; GetWindowThreadProcessId(h, out owner);
      if (owner == pid) {
        var cls = new StringBuilder(64);
        GetClassName(h, cls, 64);
        string name = cls.ToString();
        // Skip the tool-window/IME classes that also belong to the process; the product's
        // own window is the plain one its WndProc belongs to.
        if (name != "IME" && name != "MSCTFIME UI") { found = h; return false; }
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }

  public static bool Key(IntPtr h, uint vk) {
    bool down = PostMessage(h, WmKeyDown, (IntPtr)vk, (IntPtr)1);
    PostMessage(h, WmKeyUp, (IntPtr)vk, unchecked((IntPtr)(int)0xC0000001));
    return down;
  }
}
"@

$spawn = [Environment]::TickCount
$proc = $null
$deadline = $spawn + $WaitMs
while ([Environment]::TickCount -lt $deadline) {
  $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($proc) { break }
  Start-Sleep -Milliseconds 100
}

$lines = New-Object System.Collections.Generic.List[string]
if (-not $proc) {
  $lines.Add("keyprobe.found=0")
  $lines.Add("keyprobe.reason=no-process")
  $lines.Add("keyprobe.spawnMs=$([Environment]::TickCount - $spawn)")
  $lines | Set-Content -Path $Out -Encoding ASCII
  exit 1
}
$lines.Add("keyprobe.pid=$($proc.Id)")

$hwnd = [IntPtr]::Zero
$foundAt = -1
while ([Environment]::TickCount -lt $deadline) {
  $hwnd = [KeyProbe]::FindWindow([uint32]$proc.Id)
  if ($hwnd -ne [IntPtr]::Zero) { $foundAt = [Environment]::TickCount - $spawn; break }
  if ($proc.HasExited) { break }
  Start-Sleep -Milliseconds 100
}

if ($hwnd -eq [IntPtr]::Zero) {
  $lines.Add("keyprobe.found=0")
  $lines.Add("keyprobe.reason=no-window")
  $lines.Add("keyprobe.spawnMs=$([Environment]::TickCount - $spawn)")
  $lines | Set-Content -Path $Out -Encoding ASCII
  exit 1
}

$lines.Add("keyprobe.found=1")
$lines.Add("keyprobe.hwnd=$($hwnd.ToInt64())")
$lines.Add("keyprobe.firstFoundMs=$foundAt")

# Let the renderer and the key table finish coming up: the window is created before the
# first frame, and a key that arrives before the table is loaded would be dispatched by
# nothing and prove nothing either way.
Start-Sleep -Milliseconds 1200
$lines.Add("keyprobe.hSent=$(if ([KeyProbe]::Key($hwnd, 0x48)) { 1 } else { 0 })")   # 'H'
Start-Sleep -Milliseconds $AfterKeyMs
$lines.Add("keyprobe.f9Sent=$(if ([KeyProbe]::Key($hwnd, 0x78)) { 1 } else { 0 })")  # VK_F9
Start-Sleep -Milliseconds $AfterKeyMs
$lines.Add("keyprobe.spawnMs=$([Environment]::TickCount - $spawn)")
$lines | Set-Content -Path $Out -Encoding ASCII
