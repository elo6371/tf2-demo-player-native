# send-window-keys.ps1 -- post WM_KEYDOWN/WM_KEYUP to a running product window.
#
# WHY THIS EXISTS
#   The product's M / [ / ] handlers live in the window procedure, not in the
#   key-bind table (they change the master volume and are deliberately not
#   camera actions). A camera-script `key` step therefore cannot reach them: it
#   calls dispatchHotkey and reports "unbound". This helper sends the same
#   messages Windows sends, so the acceptance run exercises the product path
#   rather than a parallel test API.
#
# The process is found by name because Git Bash's $! is a Cygwin pid, not the
# Windows pid EnumWindows needs. The host lock guarantees one product process;
# if several exist, the newest is chosen and its pid is printed.
param(
  [string]$ProcessName = "tf2_demo_native",
  [string]$Keys = "",
  [int]$TimeoutSeconds = 150,
  [int]$GapMs = 300
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
}
"@

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$proc = $null
while ((Get-Date) -lt $deadline) {
  $all = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
  if ($all.Count -gt 0) {
    $proc = $all | Sort-Object StartTime -Descending | Select-Object -First 1
    break
  }
  Start-Sleep -Milliseconds 200
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

$tokens = $Keys.Split(',') | Where-Object { $_.Trim() -ne "" }
if ($tokens.Count -eq 0) {
  Write-Output "KEY-SEND=FAIL reason=no-keys"
  exit 2
}
foreach ($token in $tokens) {
  $vk = 0
  if (-not [int]::TryParse($token.Trim(), [ref]$vk)) {
    Write-Output "KEY-SEND=FAIL reason=bad-vk token=$token"
    exit 2
  }
  if (-not [WindowKeys]::Send($hwnd, $vk)) {
    Write-Output "KEY-SEND=FAIL reason=post-failed vk=$vk pid=$($proc.Id)"
    exit 1
  }
  Write-Output "KEY-SENT vk=$vk pid=$($proc.Id) hwnd=$hwnd"
  Start-Sleep -Milliseconds $GapMs
}
Write-Output "KEY-SEND=PASS count=$($tokens.Count) pid=$($proc.Id)"
