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
#
# USAGE
#   powershell -File install-readiness-capture.ps1 -InstallDir <dir> -FixtureDir <dir>
#     -OutDir <dir> [-Cases all|lifecycle|stores] [-Only <case>] [-Seconds 30]
# Exit 0 when every requested case produced a title and an exit code.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$InstallDir,
  [Parameter(Mandatory = $true)][string]$FixtureDir,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [string]$TfRoot = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf',
  [string]$RealDemo = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\autorecord_2026-07-02_13-26-46.dem',
  [ValidateSet('all', 'lifecycle', 'stores')][string]$Cases = 'all',
  [string]$Only,
  [int]$Seconds = 30,
  [int]$AudioDevice = 6
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
  $output = & $titleCapture @arguments 2>&1
  $rc = $LASTEXITCODE
  Write-Output ("CASE {0} rc={1} {2}" -f $Name, $rc, ($output | Where-Object { $_ -match 'TITLE-CAPTURE=' } | Select-Object -Last 1))
  # stdout of a function IS its return value: returning an object here would
  # merge it into the caller variable and the strict-mode verdict would then fail
  # an array instead of reading the case rc. The log and the failure count are
  # script-scope state instead.
  $script:caseLog.Add(("{0} rc={1}" -f $Name, $rc))
  if ($rc -ne 0) { $script:badCases++ }
}

$script:caseLog = New-Object System.Collections.Generic.List[string]
$script:badCases = 0

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
    $relocatedRoot = Join-Path $OutDir 'relocated\install'
    if (Test-Path -LiteralPath (Join-Path $OutDir 'relocated')) {
      Remove-Item -LiteralPath (Join-Path $OutDir 'relocated') -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Split-Path $relocatedRoot -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $InstallDir -Destination $relocatedRoot -Recurse -Force
    $relocatedExe = Join-Path $relocatedRoot 'tf2_demo_native.exe'
    $caseDir = Join-Path $OutDir 'relocated'
    New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
    $output = & $titleCapture -Demo $RealDemo -OutDir $caseDir -Seconds $Seconds -AudioDevice $AudioDevice -ExtraArgs @('--tf-root', $TfRoot) -ExePath $relocatedExe 2>&1
    $rc = $LASTEXITCODE
    Write-Output ("CASE relocated rc={0} {1}" -f $rc, ($output | Where-Object { $_ -match 'TITLE-CAPTURE=' } | Select-Object -Last 1))
    $script:caseLog.Add(("{0} rc={1}" -f 'relocated', $rc))
    if ($rc -ne 0) { $script:badCases++ }
  }
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

Write-Output ("cases={0} failed={1}" -f $script:caseLog.Count, $script:badCases)
$script:caseLog | Set-Content -LiteralPath (Join-Path $OutDir 'cases.txt') -Encoding ASCII
if ($script:badCases -gt 0) { exit 1 }
exit 0
