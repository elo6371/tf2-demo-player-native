# install-readiness-capture.ps1 -- T32 (2026-10-20): produce the product runs of
# install-readiness-check.sh from the CLEAN INSTALLED BINARY, not the developer
# build.
#
# WHY THIS IS SPLIT FROM THE GATE, AND WHY IT PASSES -ExePath
#   Every reading here belongs to a product process: the window title (what the
#   product says it opened) and the trace file (which store answered the map
#   lookup, whether a present VPK was refused). Bash judges; this file only
#   makes the runs. title-capture.ps1 already owns the record -> close -> exit
#   sequence and the owned-window rule, so this script drives it instead of
#   growing a second copy of that loop. -ExePath is what makes the same harness
#   point at <install dir>\tf2_demo_native.exe; the working directory the
#   capture uses is the exe's own directory, so "runs from the install
#   directory" is a property of the run, not of the harness.
#
# THE TWO GROUPS
#   lifecycle  runs against the real Steam install (pinned demo, real tf root):
#              first-launch, restart (same settings file), explicit --tf-root,
#              explicit missing --tf-root, relocated install. These are the
#              first launch / close / restart / explicit root / migration steps.
#   stores     runs against the synthetic roots install-readiness-fixtures.py
#              builds (one fixture demo whose header names the fixture map):
#              loose-before-VPK precedence, VPK-only, download/maps, missing
#              map, and three broken archives (empty, truncated, out-of-range
#              entry).
#   cycle      T47: only first-launch, restart (same settings) and explicit-root
#              from a fresh -OutDir. install-readiness-check.sh runs this a
#              second time from a separate staging directory and requires the
#              same root-selection decisions as the first cycle.
#
# T47 (2026-10-21): THE COLD-CACHE TITLE BOUND AND THE SEPARATE TIMEOUT CLASS
#   T43's cold run closed first-launch/restart/explicit-root at the old 30 s
#   bound with the window still on its load-time title (len=30, brackets=no)
#   while the trace of the same runs already reported
#   "resources: ... tf_root=... valid=1". A last-read title cannot tell "the
#   product connected but the title settled later" from "the product never
#   connected", so every case is now classified from the trace and the title
#   TOGETHER and the bound is a documented gate constant (install-readiness-
#   check.sh passes it; see TITLE_BOUND_SECONDS there):
#     OK                    expected/unexpected root decision matched the title
#     TITLE-CAPTURE-TIMEOUT trace says valid=1 but the title never reached the
#                           connected "[VPK entries:" form within the bound
#     ROOT-DISAGREEMENT     trace and title disagree about connectivity
#     TRACE-ROOT-MISSING    no "resources:" line: the product did not get far
#                           enough to make a root decision
#     UNEXPECTED-CONNECTED-TITLE  a missing/empty root produced a connected title
#   TITLE-CAPTURE-TIMEOUT is printed on its own line, counted separately, and
#   makes this script exit non-zero: it is never accepted as a connected title.
#   A classified timeout also ABORTS the remaining cases (CAPTURE-ABORT) instead
#   of letting each one pay the bound again, and -BudgetSeconds is an aggregate
#   ceiling on capture wall time checked before each case (CAPTURE-BUDGET-
#   EXHAUSTED): worst case for one forward run is one bound plus the ceiling,
#   which keeps the gate inside the 30-minute host-lock hold it runs under.
#   The classification lives here rather than in title-capture.ps1 so the shared
#   T9 capture keeps its exact behaviour for every other caller.
#
# USAGE
#   powershell -File install-readiness-capture.ps1 -InstallDir <dir> -FixtureDir <dir>
#     -OutDir <dir> [-Cases all|lifecycle|stores|cycle] [-Only <case>] [-Seconds 120]
#     [-BudgetSeconds 600]
# Exit 0 when every requested case produced a title, an exit code and an OK
# readiness class.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$InstallDir,
  [Parameter(Mandatory = $true)][string]$FixtureDir,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [string]$TfRoot = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf',
  [string]$RealDemo = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\autorecord_2026-07-02_13-26-46.dem',
  [ValidateSet('all', 'lifecycle', 'stores', 'cycle')][string]$Cases = 'all',
  [string]$Only,
  # T47: the documented poll bound in seconds, handed to title-capture.ps1 as
  # -Seconds. It is a CAP, not a fixed wait: title-capture stops polling as soon
  # as the connected "[VPK entries:" title appears. T43's cold-cache failure was
  # measured at the old value of 30 s; 120 s is 5.0x the slowest warm settle
  # measured on this host (24.0 s, T47-COLD-READINESS-2026-10-21.md). The cold
  # path cannot be produced on demand without clearing system-wide caches (out of
  # bounds), so the number is anchored to the measured warm reading, to the old
  # bound the cold run exceeded, and to the forced-timeout control the gate runs.
  # A run that exceeds the bound is classified TITLE-CAPTURE-TIMEOUT below, and
  # the remaining cases are aborted rather than each paying the bound.
  [int]$Seconds = 120,
  # T47: aggregate ceiling on title-capture wall time for this invocation, so a
  # pathological host cannot hold the shared lock for bound x caseload. The gate
  # passes a value sized from its measured warm forward capture. 0 = no ceiling.
  # Reached -> CAPTURE-BUDGET-EXHAUSTED and a non-zero exit; remaining cases are
  # named as skipped, never silently dropped.
  [int]$BudgetSeconds = 600,
  [int]$AudioDevice = 6
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# T47: wall clock of this invocation, the denominator of the aggregate budget.
$script:captureStart = Get-Date

$repo = $PSScriptRoot
$titleCapture = Join-Path $repo 'title-capture.ps1'
if (-not (Test-Path -LiteralPath $titleCapture)) { throw "title-capture.ps1 is absent: $titleCapture" }

$InstallDir = [System.IO.Path]::GetFullPath($InstallDir)
$FixtureDir = [System.IO.Path]::GetFullPath($FixtureDir)
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$installedExe = Join-Path $InstallDir 'tf2_demo_native.exe'
if (-not (Test-Path -LiteralPath $installedExe)) { throw "the installed product is absent: $installedExe" }
if (-not (Test-Path -LiteralPath $TfRoot)) { throw "the reference tf root is absent: $TfRoot" }
if (-not (Test-Path -LiteralPath $RealDemo)) { throw "the pinned demo is absent: $RealDemo" }
$fixtureDemo = Join-Path $FixtureDir ('demo\t32_marker.dem')
if (-not (Test-Path -LiteralPath $fixtureDemo)) { throw "the fixture demo is absent: $fixtureDemo" }
$missingRoot = Join-Path $FixtureDir 'roots\__no_such_root__'

function Test-Selected {
  param([string]$Name)
  if ([string]::IsNullOrEmpty($Only)) { return $true }
  return $Name -eq $Only
}

# T47: end this invocation with the same summary shape every run has produced,
# so install-readiness-check.sh reads one cases=/failed=/titleTimeouts= line
# whether the run completed or was cut short by the budget or a timeout.
function Complete-Capture {
  param([int]$Code)
  Write-Output ("cases={0} failed={1} titleTimeouts={2}" -f $script:caseLog.Count, $script:badCases, $script:titleTimeouts)
  $script:caseLog | Set-Content -LiteralPath (Join-Path $OutDir 'cases.txt') -Encoding ASCII
  exit $Code
}

# Abort before starting another case when the aggregate wait already exceeds the
# ceiling. The check runs before a case, so the overshoot is bounded by one case.
function Test-CaptureBudget {
  if ($BudgetSeconds -le 0) { return }
  $elapsed = ((Get-Date) - $script:captureStart).TotalSeconds
  if ($elapsed -le $BudgetSeconds) { return }
  Write-Output ('CAPTURE-BUDGET-EXHAUSTED elapsed_s={0:F1} budget_s={1} cases={2}' -f $elapsed, $BudgetSeconds, $script:caseLog.Count)
  Complete-Capture 1
}

# T47: whether the case is expected to reach the connected title. A missing path
# and an empty path are the only two lifecycle roots that must NOT connect; every
# other run reads a structurally valid root and must produce the VPK bracket.
# The map/store readings below do not depend on the bracket, but the root
# decision does, so the class is computed for every case.
$script:expectedConnected = @{
  'first-launch'         = $true
  'restart'              = $true
  'explicit-root'        = $true
  'relocated'            = $true
  'missing-root'         = $false
  'empty-root'           = $false
  'store-precedence'     = $true
  'store-vpk-only'       = $true
  'store-download-maps'  = $true
  'store-missing-map'    = $true
  'store-vpk-corrupt'    = $true
  'store-vpk-empty'      = $true
  'store-vpk-truncated'  = $true
}

# One reading file per case: what the trace says the root decision was and what
# the title says it displayed, plus the elapsed wall time. The two are separate
# signals on purpose (T47): a run may connect (trace valid=1) and still fail to
# show the connected title inside the bound, and that is a named result.
function Measure-CaseReadiness {
  param(
    [string]$Name,
    [string]$CaseDir,
    [double]$ElapsedSeconds
  )
  $tracePath = Join-Path $CaseDir 'trace.txt'
  $titlePath = Join-Path $CaseDir 'title-last.txt'
  $title = ''
  if (Test-Path -LiteralPath $titlePath) { $title = [string](Get-Content -LiteralPath $titlePath -Raw) }
  $title = $title.Trim()
  $titleBracket = $title -match '\[VPK entries:'
  $titleLoadTime = $title -eq 'TF2 Demo Player - Native D3D11'
  $traceValid = ''
  $traceRequested = ''
  $traceRoot = ''
  $traceResourcesMs = ''
  $traceLastMs = ''
  if (Test-Path -LiteralPath $tracePath) {
    $text = [string](Get-Content -LiteralPath $tracePath -Raw)
    $resourceMatch = [regex]::Match($text, '(?m)^([0-9.]+) resources: requested=(.*) tf_root=(.*) valid=([0-9]+)')
    if ($resourceMatch.Success) {
      $traceResourcesMs = $resourceMatch.Groups[1].Value
      $traceRequested = $resourceMatch.Groups[2].Value
      $traceRoot = $resourceMatch.Groups[3].Value
      $traceValid = $resourceMatch.Groups[4].Value
    }
    $stamps = [regex]::Matches($text, '(?m)^([0-9.]+) ')
    if ($stamps.Count -gt 0) { $traceLastMs = $stamps[$stamps.Count - 1].Groups[1].Value }
  }
  $expected = $true
  if ($script:expectedConnected.ContainsKey($Name)) { $expected = $script:expectedConnected[$Name] }
  $class = 'OK'
  if ($expected) {
    if ($traceValid -eq '') { $class = 'TRACE-ROOT-MISSING' }
    elseif ($traceValid -eq '1') { if (-not $titleBracket) { $class = 'TITLE-CAPTURE-TIMEOUT' } }
    elseif ($traceValid -eq '0') { $class = 'ROOT-DISAGREEMENT' }
    else { $class = 'TRACE-ROOT-UNPARSED' }
  } else {
    if ($titleBracket) { $class = 'UNEXPECTED-CONNECTED-TITLE' }
    elseif ($traceValid -eq '') { $class = 'TRACE-ROOT-MISSING' }
    elseif ($traceValid -eq '1') { $class = 'ROOT-DISAGREEMENT' }
  }
  $bracketFlag = '0'
  if ($titleBracket) { $bracketFlag = '1' }
  $loadFlag = '0'
  if ($titleLoadTime) { $loadFlag = '1' }
  $lines = New-Object System.Collections.Generic.List[string]
  $lines.Add("case=$Name")
  $lines.Add("class=$class")
  $lines.Add(('elapsed_s={0:F3}' -f $ElapsedSeconds))
  $lines.Add("trace_valid=$traceValid")
  $lines.Add("trace_requested=$traceRequested")
  $lines.Add("trace_root=$traceRoot")
  $lines.Add("trace_resources_ms=$traceResourcesMs")
  $lines.Add("trace_last_ms=$traceLastMs")
  $lines.Add("title_bracket=$bracketFlag")
  $lines.Add("title_load_time=$loadFlag")
  $lines.Add("title_len=$($title.Length)")
  Set-Content -LiteralPath (Join-Path $CaseDir 'readiness.txt') -Value $lines -Encoding ASCII
  # The READINESS lines are RETURNED with the object instead of being written
  # here: a function's stdout is its return value, so a Write-Output in this
  # function made the caller capture an array of strings and the object, and the
  # strict-mode $readiness.Class read then failed on the array's first element
  # (measured in the first T47 gate run, 2026-10-21).
  $readinessLine = ('READINESS case={0} elapsed_s={1:F1} trace_valid=<missing> title_bracket={2} title_load_time={3} class={4}' -f $Name, $ElapsedSeconds, $bracketFlag, $loadFlag, $class)
  $timeoutLine = ''
  if ($traceValid -ne '') {
    $readinessLine = ('READINESS case={0} elapsed_s={1:F1} trace_valid={2} trace_root={3} title_bracket={4} title_load_time={5} class={6}' -f $Name, $ElapsedSeconds, $traceValid, $traceRoot, $bracketFlag, $loadFlag, $class)
  }
  if ($class -eq 'TITLE-CAPTURE-TIMEOUT') {
    $timeoutLine = ('TITLE-CAPTURE-TIMEOUT case={0} elapsed_s={1:F1} trace_valid=1 trace_root={2} title_bracket=no title=load-time' -f $Name, $ElapsedSeconds, $traceRoot)
  }
  return [pscustomobject]@{
    Class         = $class
    TraceValid    = $traceValid
    TraceRoot     = $traceRoot
    TitleBracket  = $titleBracket
    TitleLoadTime = $titleLoadTime
    ReadinessLine = $readinessLine
    TimeoutLine   = $timeoutLine
  }
}

# One case = one title-capture run. -SettingsFile is passed only by the restart
# case, so the other cases keep title-capture's fresh-settings default.
function Invoke-Case {
  param(
    [string]$Name,
    [string]$Demo,
    [string[]]$ExtraArgs,
    [string]$SettingsFile = ''
  )
  if (-not (Test-Selected $Name)) { return $null }
  Test-CaptureBudget
  $caseDir = Join-Path $OutDir $Name
  New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
  $arguments = @{
    Demo = $Demo
    OutDir = $caseDir
    Seconds = $Seconds
    AudioDevice = $AudioDevice
    ExtraArgs = $ExtraArgs
    ExePath = $installedExe
  }
  if (-not [string]::IsNullOrEmpty($SettingsFile)) { $arguments['SettingsFile'] = $SettingsFile }
  $started = Get-Date
  $output = & $titleCapture @arguments 2>&1
  $rc = $LASTEXITCODE
  $elapsed = ((Get-Date) - $started).TotalSeconds
  Write-Output ("CASE {0} rc={1} {2}" -f $Name, $rc, ($output | Where-Object { $_ -match 'TITLE-CAPTURE=' } | Select-Object -Last 1))
  # stdout of a function IS its return value: returning an object here would
  # merge it into the caller variable and the strict-mode verdict would then fail
  # an array instead of reading the case rc. The log and the failure count are
  # script-scope state instead.
  $readiness = Measure-CaseReadiness -Name $Name -CaseDir $caseDir -ElapsedSeconds $elapsed
  Write-Output $readiness.ReadinessLine
  if ($readiness.TimeoutLine -ne '') { Write-Output $readiness.TimeoutLine }
  $script:caseLog.Add(("{0} rc={1} elapsed={2:F1}s class={3}" -f $Name, $rc, $elapsed, $readiness.Class))
  $caseBad = ($rc -ne 0) -or ($readiness.Class -ne 'OK')
  if ($caseBad) { $script:badCases++ }
  if ($readiness.Class -eq 'TITLE-CAPTURE-TIMEOUT') {
    $script:titleTimeouts++
    # T47: one classified timeout already makes the run red; spending the bound on
    # every remaining case would hold the shared lock for nothing. Abort, named.
    Write-Output ("CAPTURE-ABORT reason=title-capture-timeout case={0}" -f $Name)
    Complete-Capture 1
  }
}

$script:caseLog = New-Object System.Collections.Generic.List[string]
$script:badCases = 0
$script:titleTimeouts = 0

if ($Cases -eq 'all' -or $Cases -eq 'lifecycle') {
  # 1. First launch from the install directory with no --tf-root: a fresh user
  #    with TF2 installed must find it. The settings file this writes is the
  #    input of the restart case below.
  Invoke-Case -Name 'first-launch' -Demo $RealDemo -ExtraArgs @()
  $firstSettings = Join-Path $OutDir 'first-launch\settings.ini'

  # 2. Restart: the SAME settings file, no --tf-root. The product must reuse the
  #    root it saved on the first launch; deleting the file would turn this into
  #    a second first launch.
  if ((Test-Selected 'restart') -and (Test-Path -LiteralPath $firstSettings)) {
    Invoke-Case -Name 'restart' -Demo $RealDemo -ExtraArgs @() -SettingsFile $firstSettings
  } elseif (Test-Selected 'restart') {
    Write-Output 'CASE restart SKIPPED (first-launch wrote no settings file)'
  }

  Invoke-Case -Name 'explicit-root' -Demo $RealDemo -ExtraArgs @('--tf-root', $TfRoot)

  Invoke-Case -Name 'missing-root' -Demo $RealDemo -ExtraArgs @('--tf-root', $missingRoot)

  # 2b. A path that EXISTS but holds no resources is the half-installed case:
  #     it must be refused like a path that does not exist, not connected with
  #     an empty store count (the pre-T9 behaviour T4 recorded).
  $emptyRoot = Join-Path $FixtureDir 'roots\empty'
  Invoke-Case -Name 'empty-root' -Demo $RealDemo -ExtraArgs @('--tf-root', $emptyRoot)

  # 3. Migration: run the same package from a different parent directory. No
  #    installer wrote registry entries or absolute paths, so the copy has to
  #    behave identically; if it does not, the package is not relocatable.
  if (Test-Selected 'relocated') {
    Test-CaptureBudget
    $relocatedRoot = Join-Path $OutDir 'relocated\install'
    if (Test-Path -LiteralPath (Join-Path $OutDir 'relocated')) {
      Remove-Item -LiteralPath (Join-Path $OutDir 'relocated') -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Split-Path $relocatedRoot -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $InstallDir -Destination $relocatedRoot -Recurse -Force
    $relocatedExe = Join-Path $relocatedRoot 'tf2_demo_native.exe'
    $caseDir = Join-Path $OutDir 'relocated'
    New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
    $started = Get-Date
    $output = & $titleCapture -Demo $RealDemo -OutDir $caseDir -Seconds $Seconds -AudioDevice $AudioDevice -ExtraArgs @('--tf-root', $TfRoot) -ExePath $relocatedExe 2>&1
    $rc = $LASTEXITCODE
    $elapsed = ((Get-Date) - $started).TotalSeconds
    Write-Output ("CASE relocated rc={0} {1}" -f $rc, ($output | Where-Object { $_ -match 'TITLE-CAPTURE=' } | Select-Object -Last 1))
    $readiness = Measure-CaseReadiness -Name 'relocated' -CaseDir $caseDir -ElapsedSeconds $elapsed
    Write-Output $readiness.ReadinessLine
    if ($readiness.TimeoutLine -ne '') { Write-Output $readiness.TimeoutLine }
    $script:caseLog.Add(("{0} rc={1} elapsed={2:F1}s class={3}" -f 'relocated', $rc, $elapsed, $readiness.Class))
    $caseBad = ($rc -ne 0) -or ($readiness.Class -ne 'OK')
    if ($caseBad) { $script:badCases++ }
    if ($readiness.Class -eq 'TITLE-CAPTURE-TIMEOUT') {
      $script:titleTimeouts++
      Write-Output ("CAPTURE-ABORT reason=title-capture-timeout case=relocated")
      Complete-Capture 1
    }
  }
}

# T47: the determinism cycle. The gate runs this against a SECOND staging
# directory (its own install prefix) and its own -OutDir, then compares the
# root-selection decisions against the first cycle. Only the three cases the
# T47 brief names are run: first-launch, restart (same settings file) and
# explicit-root.
if ($Cases -eq 'cycle') {
  Invoke-Case -Name 'first-launch' -Demo $RealDemo -ExtraArgs @()
  $firstSettings = Join-Path $OutDir 'first-launch\settings.ini'
  if (Test-Path -LiteralPath $firstSettings) {
    Invoke-Case -Name 'restart' -Demo $RealDemo -ExtraArgs @() -SettingsFile $firstSettings
  } else {
    Write-Output 'CASE restart SKIPPED (first-launch wrote no settings file)'
  }
  Invoke-Case -Name 'explicit-root' -Demo $RealDemo -ExtraArgs @('--tf-root', $TfRoot)
}

if ($Cases -eq 'all' -or $Cases -eq 'stores') {
  $storeCases = @(
    @{ Name = 'store-precedence';    Root = 'precedence' },
    @{ Name = 'store-vpk-only';      Root = 'vpk-only' },
    @{ Name = 'store-download-maps'; Root = 'download-maps' },
    @{ Name = 'store-missing-map';   Root = 'missing-map' },
    @{ Name = 'store-vpk-corrupt';   Root = 'vpk-corrupt' },
    @{ Name = 'store-vpk-empty';     Root = 'vpk-empty' },
    @{ Name = 'store-vpk-truncated'; Root = 'vpk-truncated' }
  )
  foreach ($case in $storeCases) {
    if (-not (Test-Selected $case.Name)) { continue }
    $root = Join-Path $FixtureDir ('roots\' + $case.Root)
    Invoke-Case -Name $case.Name -Demo $fixtureDemo -ExtraArgs @('--tf-root', $root)
  }
}

if ($script:badCases -gt 0 -or $script:titleTimeouts -gt 0) { Complete-Capture 1 }
Complete-Capture 0
