# stability-root-check.ps1 -- prove the --tf-root contract, three ways.
#
# WHY THIS FILE EXISTS
#   The product used to append the Steam-library candidates to the explicit
#   --tf-root, so an operator pointing at a path that had been moved or deleted
#   silently ended up connected to a DIFFERENT installation, with a window title
#   that read as if nothing was wrong. That is the exact defect this script is
#   written to catch: the reading is the window title, which is the product's
#   own report of which root it connected, not the argument the harness passed.
#
# THE CONTRACT, from the task
#   --tf-root <valid-path>     uses ONLY that path
#   --tf-root <missing-path>   does NOT auto-connect Steam
#   no --tf-root               may auto-search the Steam/persistent roots
#
#   The third row is the reason the first two are not "the product ignores
#   --tf-root": with no flag at all the search must still work, so a failure of
#   the first two rows cannot be dismissed as the feature being broken
#   altogether.
#
# WHY IT READS THE TITLE
#   The title carries the connected root and the VPK entry count, which is the
#   product's own account of what it opened. An exit code cannot express any of
#   this: the product exits 0 in all three rows.
#
# USAGE
#   powershell -File stability-root-check.ps1 [-OutDir <dir>] [-Seconds 8]
# Exit: 0 = every row held; 1 = at least one row failed. The per-row readings
# are written to root-readings.txt next to this script's caller.

[CmdletBinding()]
param(
  [string]$OutDir,
  # 45 s, not less: the trace shows the window is created ~25 s in, after the
  # asset load and the first gpu reload. A shorter window finds nothing and
  # every row would then read as "no root connected" for the wrong reason.
  [int]$Seconds = 45,
  [int]$AudioDevice = 6
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = $PSScriptRoot
if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = Join-Path $repo ('.scratch\root-check') }
# Absolute, and created before the first row: the working directory of the child
# is the exe's own directory, so a relative -OutDir would resolve there and the
# trace and settings paths would point at a directory that does not exist.
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$exe = (Resolve-Path (Join-Path $repo 'native\build-nmake\tf2_demo_native.exe')).Path
if (-not (Test-Path -LiteralPath $exe)) { throw "exe not built: $exe" }

# The real install. Its VPK entry count is what proves a row really opened it
# rather than printing a path.
$steamRoot = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf'
if (-not (Test-Path -LiteralPath $steamRoot)) { throw "the reference install is absent: $steamRoot" }

# A path that does not exist. Under the run directory so a retry stays
# hermetic and nothing outside the tree is touched.
$missingRoot = Join-Path $OutDir 'roots\missing-root'
if (Test-Path -LiteralPath $missingRoot) { Remove-Item -LiteralPath $missingRoot -Recurse -Force }

$demo = 'D:\TF2_Demo_Player\testdata\demos\4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem'
if (-not (Test-Path -LiteralPath $demo)) { throw "missing input: $demo" }

# ------------------------------------------------------------------ win32
# The window is found through a compiled C# scanner, not a PowerShell
# scriptblock marshaled as a native callback. A scriptblock callback does not
# chain to the scope it was created in: the first revision read a function
# parameter inside the callback and every window was silently skipped; the
# second revision moved everything to script scope and STILL saw nothing,
# because any exception inside the marshaled callback is swallowed and its
# null return stops the enumeration at the first window. Enumeration living
# in C# has no scope to lose. One Scan call returns "pid<TAB>hwnd<TAB>title"
# for the first top-level window of the class, or an empty string.
$WindowClass = 'TF2DemoNativeRenderer'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class T4RootScan {
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")]
  public static extern bool EnumWindows(EnumWindowsProc proc, IntPtr l);
  [DllImport("user32.dll")]
  public static extern uint GetWindowThreadProcessId(IntPtr h, out uint processId);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetClassNameW(IntPtr h, StringBuilder name, int max);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetWindowTextW(IntPtr h, StringBuilder text, int max);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public static string Scan(string className) {
    IntPtr found = IntPtr.Zero;
    uint owner = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      StringBuilder cb = new StringBuilder(256);
      GetClassNameW(h, cb, 256);
      if (cb.ToString() != className) return true;
      uint o = 0;
      GetWindowThreadProcessId(h, out o);
      found = h;
      owner = o;
      return false;
    }, IntPtr.Zero);
    if (found == IntPtr.Zero) return "";
    StringBuilder tb = new StringBuilder(16384);
    GetWindowTextW(found, tb, 16384);
    return owner.ToString() + "\t" + found.ToString() + "\t" + tb.ToString();
  }
}
'@

# ------------------------------------------------------------------ one row
function Invoke-Row {
  param(
    [string]$Name,
    [string[]]$ProductArgs
  )
  $dir = Join-Path $OutDir $Name
  New-Item -ItemType Directory -Path $dir -Force | Out-Null
  $env:TF2_NATIVE_TRACE = (Join-Path $dir 'trace.txt')
  # A private settings file per row: the persistent tf_root is one of the
  # fallbacks under test, so a row must not read another row's choice.
  $settings = Join-Path $dir 'settings.ini'
  if (Test-Path -LiteralPath $settings) { Remove-Item -LiteralPath $settings -Force }

  $args = New-Object System.Collections.Generic.List[string]
  foreach ($a in $ProductArgs) { $args.Add($a) }
  $args.Add('--demo'); $args.Add($demo)
  $args.Add('--hidden-window')
  # The audio device is pinned explicitly. The rule for this machine is that
  # any invocation which could open the device names it, because the default
  # device is not the one the operator is listening on.
  $args.Add('--audio-device'); $args.Add("$AudioDevice")
  $args.Add('--audio-disabled')
  # Paused at tick 0: the row is about which root was opened, not about the
  # demo playing, and a paused start makes the window's first title the final
  # one instead of something read mid-load.
  $args.Add('--start-paused')
  $args.Add('--settings-file'); $args.Add($settings)

  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe
  $psi.Arguments = ($args | ForEach-Object { if ($_ -match '\s') { '"{0}"' -f ($_ -replace '"', '\"') } else { $_ } }) -join ' '
  $psi.WorkingDirectory = Split-Path $exe -Parent
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true

  Set-Content -LiteralPath (Join-Path $dir 'command.txt') -Value @("`"$exe`"", $psi.Arguments) -Encoding UTF8

  $proc = [System.Diagnostics.Process]::Start($psi)
  $outTask = $proc.StandardOutput.ReadToEndAsync()
  $errTask = $proc.StandardError.ReadToEndAsync()

  $title = ''
  $windowFound = $false
  $windowPid = 0
  $deadline = (Get-Date).AddSeconds($Seconds)
  # Poll the full window, not "first non-empty title": the title that exists
  # at window creation is the load-time one ("TF2 Demo Player - Native
  # D3D11"); the connected-root report lands later, after the assets and the
  # VPK open. The early break fires only on the VPK bracket, which a row that
  # failed to connect can never produce -- so the missing-root row's negative
  # is claimed at the deadline, not washed out by an early exit.
  while ((Get-Date) -lt $deadline) {
    if ($proc.HasExited) { break }
    $scan = [T4RootScan]::Scan($WindowClass)
    if ($scan -ne '') {
      $parts = $scan -split "`t", 3
      $windowPid = [int]$parts[0]
      $windowFound = $true
      if ($parts[2].Length -gt 0) { $title = $parts[2] }
      if ($title -match '\[VPK entries:') { break }
    }
    Start-Sleep -Milliseconds 400
  }
  # Close by posting WM_CLOSE: a click on the X, not a kill, so an orderly
  # teardown is what gets measured.
  if (-not $proc.HasExited) {
    $scan = [T4RootScan]::Scan($WindowClass)
    if ($scan -ne '') {
      $parts = $scan -split "`t", 3
      [void][T4RootScan]::PostMessageW([IntPtr][long]$parts[1], [uint32]0x0010, [System.IntPtr]::Zero, [System.IntPtr]::Zero)
    }
  }
  if (-not $proc.WaitForExit(20000)) { $proc.Kill() }
  $proc.WaitForExit(5000) | Out-Null
  $stdout = $outTask.Result
  $stderr = $errTask.Result
  Set-Content -LiteralPath (Join-Path $dir 'stdout.txt') -Value $stdout -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'stderr.txt') -Value $stderr -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'exit-code.txt') -Value "$($proc.ExitCode)" -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'title-last.txt') -Value $title -Encoding UTF8

  return [pscustomobject]@{
    Name         = $Name
    ExitCode     = $proc.ExitCode
    Title        = $title
    WindowFound  = $windowFound
    WindowPid    = $windowPid
    ChildPid     = $proc.Id
    Dir          = $dir
  }
}

# ------------------------------------------------------------------ the rows
$rows = New-Object System.Collections.Generic.List[object]
$rows.Add((Invoke-Row -Name 'explicit-valid-root'   -ProductArgs @('--tf-root', $steamRoot)))
$rows.Add((Invoke-Row -Name 'explicit-missing-root' -ProductArgs @('--tf-root', $missingRoot)))
$rows.Add((Invoke-Row -Name 'no-root-auto-search'   -ProductArgs @()))

# ------------------------------------------------------------------ judging
# The connected root is the drive path that sits between the product's status
# text and the VPK bracket: "TF2 Demo Player - <status>: D:\...\tf [VPK
# entries: 104137, ...]". The status word itself is localized (this install
# reports it in Chinese), so the parse must not anchor on a literal status
# word -- and the task forbids non-ASCII text in code anyway. The anchor is
# ASCII-only structure instead: a drive path followed by " [VPK entries:".
# That bracket exists only in a connected title, so a row that found no root
# yields the empty reading the missing-root row must produce.
function Get-ConnectedRoot {
  param([string]$Title)
  if ($Title -match '([A-Za-z]:\\[^[]+) \[VPK entries:') { return $Matches[1].Trim() }
  return ''
}
function Get-VpkEntries {
  param([string]$Title)
  if ($Title -match '\[VPK entries:\s*(\d+)') { return [int]$Matches[1] }
  return -1
}

$lines = New-Object System.Collections.Generic.List[string]
$fail = 0
$checks = New-Object System.Collections.Generic.List[string]
function Add-Check {
  param([string]$Label, [bool]$Ok, [string]$Detail)
  if ($Ok) { $checks.Add(("PASS {0} ({1})" -f $Label, $Detail)) }
  else { $checks.Add(("FAIL {0} ({1})" -f $Label, $Detail)); $script:fail = 1 }
}
foreach ($r in $rows) {
  $root = Get-ConnectedRoot -Title $r.Title
  $vpk = Get-VpkEntries -Title $r.Title
  $lines.Add(("row={0} exit={1} window_found={2} window_pid={3} child_pid={4} connected_root={5} vpk_entries={6}" -f `
    $r.Name, $r.ExitCode, $r.WindowFound, $r.WindowPid, $r.ChildPid, $root, $vpk))
  Write-Output $lines[-1]
}

# Every row must have produced a window with a title. Without this row the
# "missing root did not connect Steam" check below would pass on a run where
# the window was never found and the title was simply empty -- a false pass
# dressed as the very reading that is meant to catch the defect.
foreach ($r in $rows) {
  Add-Check ("{0} produced a window with a title" -f $r.Name) ($r.WindowFound -and $r.Title -ne '') ("window_found=$($r.WindowFound) title_len=$($r.Title.Length)")
}

# The window the callback matched must belong to this row's own child: if it
# belonged to any other process the title would be a reading about somebody
# else's run. The task forbids concurrent product runs, so a mismatch means
# the harness itself leaked a product process from an earlier row.
foreach ($r in $rows) {
  Add-Check ("{0} window belongs to its own child process" -f $r.Name) ($r.WindowPid -eq $r.ChildPid) ("window_pid=$($r.WindowPid) child_pid=$($r.ChildPid)")
}

$valid = $rows | Where-Object { $_.Name -eq 'explicit-valid-root' }
$missing = $rows | Where-Object { $_.Name -eq 'explicit-missing-root' }
$auto = $rows | Where-Object { $_.Name -eq 'no-root-auto-search' }

# Row 1: the explicit valid root is the ONLY candidate, so it must be the one
# connected, and the entry count must prove the install was really opened.
$c = Get-ConnectedRoot -Title $valid.Title
$v = Get-VpkEntries -Title $valid.Title
Add-Check 'explicit valid root is the connected root' ($c -eq $steamRoot) ("connected=$c")
Add-Check 'explicit valid root really opened the install' ($v -gt 100000) ("vpk_entries=$v")

# Row 2: the missing root must not fall back to Steam. The connected root is
# empty when nothing valid was found, and it must not be the Steam root either
# way -- that is the whole defect.
$c = Get-ConnectedRoot -Title $missing.Title
Add-Check 'missing root did not connect Steam' ($c -ne $steamRoot -and $c -eq '') ("connected='$c'")
Add-Check 'missing root did not connect the reference install either' ($c -ne $steamRoot) ("connected='$c'")

# Row 3: with no flag the auto-search must still work, or rows 1 and 2 could be
# passed by a product that ignores --tf-root entirely.
$c = Get-ConnectedRoot -Title $auto.Title
$v = Get-VpkEntries -Title $auto.Title
Add-Check 'no flag still auto-searches and connects a root' ($c -ne '') ("connected=$c")
Add-Check 'auto-search connected the install it found' ($v -gt 100000) ("vpk_entries=$v")

foreach ($l in $checks) { Write-Output $l }
$lines.Add('')
$lines.AddRange($checks)
$lines.Add(("verdict={0}" -f $(if ($script:fail -eq 0) { 'ROOT-CONTRACT=PASS' } else { 'ROOT-CONTRACT=FAIL' })))
Set-Content -LiteralPath (Join-Path $OutDir 'root-readings.txt') -Value $lines -Encoding UTF8

if ($script:fail -eq 0) { Write-Output 'ROOT-CONTRACT=PASS'; exit 0 }
Write-Output 'ROOT-CONTRACT=FAIL'
exit 1
