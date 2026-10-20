# title-capture.ps1 -- T9 (2026-10-16): read the product's window title once.
#
# WHY THIS EXISTS
#   The title is the product's own account of what it opened and what it
#   classified the demo as. Two T9 findings live in it -- the recording label in
#   the "Demo:" segment, and "BSP: invalid/..." when the map could not be found
#   -- and neither is on stdout or in the trace. Reading the title is therefore
#   the only way to assert them, and the stability gate's load_title already
#   parses the same string, so a run that captures the title here can be judged
#   by the exact parser the release gate uses.
#
# WHY IT BORROWS THE SCANNER
#   The window enumeration lives in stability-root-check.ps1 as a compiled C#
#   type. A second copy here could disagree with the gate about what the window
#   says -- which is the whole reading -- so the Add-Type block is lifted out of
#   that file at run time instead of being rewritten. If someone changes the
#   scanner, this picks the change up.
#
# USAGE
#   powershell -File title-capture.ps1 -Demo <path.dem> -OutDir <dir> [-Seconds 45]
# Writes title-last.txt, exit-code.txt and command.txt into -OutDir. Exit 0 when
# a title was captured, 1 when the window never reported one.

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Demo,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [int]$Seconds = 45,
  # The rule for this machine: any invocation that could open the device names
  # it, because the default device is not the one the operator is listening on.
  [int]$AudioDevice = 6,
  [string[]]$ExtraArgs = @(),
  # T32: the installed copy of the product. Empty means the developer build, so
  # every existing caller keeps its exact behaviour; a clean-install case names
  # the installed exe and the capture then measures that binary from that
  # directory (the working directory below is already the exe own directory).
  [string]$ExePath,
  # T32: a settings file a previous case wrote. Empty means a fresh file under
  # -OutDir, which is what every historical caller got.
  [string]$SettingsFile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = $PSScriptRoot
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$exe = if ([string]::IsNullOrEmpty($ExePath)) {
  (Resolve-Path (Join-Path $repo 'native\build-nmake\tf2_demo_native.exe')).Path
} else {
  (Resolve-Path -LiteralPath $ExePath).Path
}
if (-not (Test-Path -LiteralPath $exe)) { throw "exe not built: $exe" }
if (-not (Test-Path -LiteralPath $Demo)) { throw "missing input: $Demo" }

$WindowClass = 'TF2DemoNativeRenderer'

# ---- the scanner, lifted from the root gate ------------------------------
$rootGate = Join-Path $repo 'stability-root-check.ps1'
if (-not (Test-Path -LiteralPath $rootGate)) { throw "the root gate is absent: $rootGate" }
$gateText = Get-Content -LiteralPath $rootGate -Raw
$start = $gateText.IndexOf('Add-Type -TypeDefinition')
if ($start -lt 0) { throw 'the root gate no longer contains the window scanner' }
# The here-string opens with @' and closes with '@. Searching for the opening
# delimiter again would cut the block off at its own first line, which fails as
# an unterminated string rather than as the missing-scanner error it is.
$open = $gateText.IndexOf("@'", $start)
if ($open -lt 0) { throw 'the scanner block does not open' }
$close = $gateText.IndexOf("'@", $open + 2)
if ($close -lt 0) { throw 'the scanner block is not terminated' }
# Scan returns the first window of the class, and EnumWindows order is not
# "the process I just started". A machine with another player instance on it --
# which is what this host looks like while several tasks share one validation
# queue -- puts that instance's window first, and a title read off it is a
# reading about somebody else's run. Worse, closing it posts WM_CLOSE into a
# process that was never mine.
#
# The enumeration stays the gate's, and an owner-filtered entry point is added
# to the lifted text rather than as a second scanner: Scan(class) keeps its
# exact behaviour, ScanOwned(class, pid) skips windows owned by anyone else.
$scanner = $gateText.Substring($start, ($close + 2) - $start)
$anchorSig = 'public static string Scan(string className) {'
if ($scanner -notmatch [regex]::Escape($anchorSig)) { throw 'the scanner signature moved; the owner filter was not applied' }
$scanner = $scanner.Replace($anchorSig,
  'public static string Scan(string className) { return ScanOwned(className, 0); }' + "`r`n  " +
  'public static string ScanOwned(string className, uint ownerPid) {')
$anchorPid = 'GetWindowThreadProcessId(h, out o);'
if ($scanner -notmatch [regex]::Escape($anchorPid)) { throw 'the scanner no longer reads the window owner; the filter was not applied' }
$scanner = $scanner.Replace($anchorPid, $anchorPid + "`r`n      if (ownerPid != 0 && o != ownerPid) return true;")
Invoke-Expression $scanner

# ---- one launch ---------------------------------------------------------
# A case that must read what a previous case saved passes -SettingsFile: the
# restart reading is "the second launch reused the root the first one wrote",
# and deleting the file between them would turn it into another first launch.
if ([string]::IsNullOrEmpty($SettingsFile)) {
  $settings = Join-Path $OutDir 'settings.ini'
  if (Test-Path -LiteralPath $settings) { Remove-Item -LiteralPath $settings -Force }
} else {
  $settings = [System.IO.Path]::GetFullPath($SettingsFile)
  $settingsParent = Split-Path $settings -Parent
  if (-not (Test-Path -LiteralPath $settingsParent)) { New-Item -ItemType Directory -Path $settingsParent -Force | Out-Null }
}

$arguments = New-Object System.Collections.Generic.List[string]
foreach ($a in $ExtraArgs) { $arguments.Add($a) }
$arguments.Add('--demo'); $arguments.Add($Demo)
$arguments.Add('--hidden-window')
$arguments.Add('--audio-device'); $arguments.Add("$AudioDevice")
$arguments.Add('--audio-disabled')
# Paused at tick 0: the connected title is written once and stays put, so the
# capture cannot land mid-load on a title that was about to change.
$arguments.Add('--start-paused')
$arguments.Add('--settings-file'); $arguments.Add($settings)

# The trace is where the map lookup reports itself ("map: lookup map=... store=
# ... selected=... reason=..."), which is the other half of what this harness is
# for: the title says which map the BSP parser was handed, the trace says which
# store answered for it. A capture with only one of the two cannot distinguish
# "found under download/maps" from "fell back to the built-in default".
$env:TF2_NATIVE_TRACE = (Join-Path $OutDir 'trace.txt')

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = ($arguments | ForEach-Object { if ($_ -match '\s') { '"{0}"' -f ($_ -replace '"', '\"') } else { $_ } }) -join ' '
$psi.WorkingDirectory = Split-Path $exe -Parent
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true

Set-Content -LiteralPath (Join-Path $OutDir 'command.txt') -Value @("`"$exe`"", $psi.Arguments) -Encoding UTF8

$proc = [System.Diagnostics.Process]::Start($psi)
$outTask = $proc.StandardOutput.ReadToEndAsync()
$errTask = $proc.StandardError.ReadToEndAsync()

$title = ''
$ownedHwnd = [IntPtr]::Zero
$foreignSeen = 0
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
  if ($proc.HasExited) { break }
  $scan = [T4RootScan]::ScanOwned($WindowClass, [uint32]$proc.Id)
  if ($scan -ne '') {
    $parts = $scan -split "`t", 3
    $ownedHwnd = [IntPtr][long]$parts[1]
    if ($parts[2].Length -gt 0) { $title = $parts[2] }
    # The VPK bracket exists only in a connected title, so it is the one thing
    # that says the title has finished settling.
    if ($title -match '\[VPK entries:') { break }
  } else {
    # Something of this class exists but is not mine. Recorded, never touched:
    # another task's player is mid-run and posting to it would end that run.
    if ([T4RootScan]::Scan($WindowClass) -ne '') { $foreignSeen++ }
  }
  Start-Sleep -Milliseconds 400
}
if ((-not $proc.HasExited) -and ($ownedHwnd -ne [IntPtr]::Zero)) {
  [void][T4RootScan]::PostMessageW($ownedHwnd, [uint32]0x0010, [System.IntPtr]::Zero, [System.IntPtr]::Zero)
}
if (-not $proc.WaitForExit(20000)) { $proc.Kill() }
$proc.WaitForExit(5000) | Out-Null

Set-Content -LiteralPath (Join-Path $OutDir 'stdout.txt') -Value $outTask.Result -Encoding UTF8
Set-Content -LiteralPath (Join-Path $OutDir 'stderr.txt') -Value $errTask.Result -Encoding UTF8
Set-Content -LiteralPath (Join-Path $OutDir 'exit-code.txt') -Value "$($proc.ExitCode)" -Encoding UTF8
Set-Content -LiteralPath (Join-Path $OutDir 'title-last.txt') -Value $title -Encoding UTF8
# The ownership proof travels with the evidence: a title without it cannot be
# told apart from one read off another process's window, which is the failure
# this harness had on its first run.
Set-Content -LiteralPath (Join-Path $OutDir 'child-pid.txt') -Value "$($proc.Id)" -Encoding UTF8
Set-Content -LiteralPath (Join-Path $OutDir 'window-pid.txt') -Value "$(if ($ownedHwnd -ne [IntPtr]::Zero) { $proc.Id } else { 0 })" -Encoding UTF8
Set-Content -LiteralPath (Join-Path $OutDir 'foreign-windows-seen.txt') -Value "$foreignSeen" -Encoding UTF8

if ([string]::IsNullOrEmpty($title)) {
  if ($foreignSeen -gt 0) {
    Write-Output 'TITLE-CAPTURE=FAIL (no title from this process; a foreign window of this class was present and left untouched)'
  } else {
    Write-Output 'TITLE-CAPTURE=FAIL (no window title)'
  }
  exit 1
}
Write-Output ("TITLE-CAPTURE=PASS (len={0} brackets={1})" -f $title.Length, ($(if ($title -match '\[VPK entries:') { 'yes' } else { 'no' })))
exit 0
