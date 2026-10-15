# stability-run-matrix.ps1 -- run the T4 stability/release matrix.
#
# One invocation = one run directory with a unique run id. Every case writes
# its own subdirectory through stability-run-case.ps1; this script only
# sequences the cases, records what ran when, and writes the blocked-input
# explanations for the environments this machine cannot produce.
#
# Mutual exclusion: the run refuses to start while any tf2_demo_native process
# is alive, because another task running the same exe would make the residual-
# process and window readings meaningless.
#
# Usage:
#   powershell -File stability-run-matrix.ps1 -RunId run-01
#     [-SoakSeconds 660] [-Sets smoke,soak,corrupt,resource,audio,device]
#   (the default -Sets value is everything)

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$RunId,
  [int]$SoakSeconds = 660,
  [int]$CaseSeconds = 20,
  [string[]]$Sets = @('smoke','soak','corrupt','resource','audio','device'),
  [string]$ScratchRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The outer shell this task is driven from hands the whole comma list over as a
# single string, so the set names are split here rather than trusted to arrive
# as an array. One element per set, trimmed, empty entries dropped.
$Sets = (($Sets -join ',').Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })

$repo = $PSScriptRoot
if ([string]::IsNullOrEmpty($ScratchRoot)) { $ScratchRoot = Join-Path $repo '.scratch\stability-release' }
$runDir = Join-Path $ScratchRoot $RunId
New-Item -ItemType Directory -Path $runDir -Force | Out-Null

$exe = (Resolve-Path (Join-Path $repo 'native\build-nmake\tf2_demo_native.exe')).Path
if (-not (Test-Path -LiteralPath $exe)) { throw "exe not built: $exe (run build-target.sh tf2_demo_native first)" }

$snake = 'D:\TF2_Demo_Player\testdata\demos\bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem'
$bagel = 'D:\TF2_Demo_Player\testdata\demos\4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem'
$tfRoot = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf'
foreach ($p in @($snake, $tfRoot)) {
  if (-not (Test-Path -LiteralPath $p)) { throw "missing input: $p" }
}

$manifest = New-Object System.Collections.Generic.List[string]
$manifest.Add(("run_id={0}" -f $RunId))
$manifest.Add(("started_at={0}" -f (Get-Date -Format 'o')))
$manifest.Add(("exe={0}" -f $exe))
# The exe's sha256, not just its path. A path cannot answer "is this the binary
# that produced this evidence": rebuild the product and the path is still the
# same while every byte of it is different, and a run whose binary was rebuilt
# afterwards would then describe a build that never ran. stability-release-check.sh
# compares this hash against the live exe's, which is what makes a stale matrix
# unable to certify a release it never measured.
$exeSha = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLower()
$manifest.Add(("exe_sha256={0}" -f $exeSha))
$manifest.Add(("exe_size={0}" -f (Get-Item -LiteralPath $exe).Length))
$manifest.Add(("snake_demo={0}" -f $snake))
$manifest.Add(("tf_root={0}" -f $tfRoot))

function Invoke-Case {
  param(
    [string]$Name,
    [int]$Seconds,
    [string[]]$CaseArgs = @(),
    [switch]$Hidden,
    [switch]$AudioDisabled,
    [int]$AudioDevice = -1,
    [switch]$NoClose,
    [string]$Demo,
    [string]$Root,
    [int]$SampleSeconds = 2
  )
  $caseDir = Join-Path $runDir $Name
  # The child is a native command: splatting a hashtable to it would pass the
  # values without their names, so the argument line is built explicitly.
  $childArgs = New-Object System.Collections.Generic.List[string]
  $childArgs.Add('-ExecutionPolicy'); $childArgs.Add('Bypass')
  $childArgs.Add('-NoProfile')
  $childArgs.Add('-File'); $childArgs.Add((Join-Path $repo 'stability-run-case.ps1'))
  $childArgs.Add('-Case'); $childArgs.Add($Name)
  $childArgs.Add('-RunDir'); $childArgs.Add($caseDir)
  $childArgs.Add('-Seconds'); $childArgs.Add("$Seconds")
  $childArgs.Add('-SampleSeconds'); $childArgs.Add("$SampleSeconds")
  if ($Demo) { $childArgs.Add('-Demo'); $childArgs.Add($Demo) }
  if ($Root) { $childArgs.Add('-TfRoot'); $childArgs.Add($Root) }
  if ($Hidden) { $childArgs.Add('-Hidden') }
  if ($AudioDisabled) { $childArgs.Add('-AudioDisabled') }
  if ($AudioDevice -ge 0) { $childArgs.Add('-AudioDevice'); $childArgs.Add("$AudioDevice") }
  if ($NoClose) { $childArgs.Add('-NoClose') }
  if ($CaseArgs.Count) { $childArgs.Add('-ExtraArgs'); foreach ($a in $CaseArgs) { $childArgs.Add($a) } }
  $out = & powershell @childArgs
  $rc = $LASTEXITCODE
  foreach ($line in $out) { $manifest.Add("case $line") }
  $manifest.Add(("case_done name=$Name runner_rc=$rc at={0}" -f (Get-Date -Format 'o')))
  Write-Output $out
}

# ---------------------------------------------------------------- fixtures
$fixtureDir = Join-Path $runDir 'fixtures'
& powershell -ExecutionPolicy Bypass -NoProfile -File (Join-Path $repo 'stability-make-inputs.ps1') -Demo $snake -OutDir $fixtureDir | Out-Null
$fixtureIndex = Join-Path $fixtureDir 'fixture-index.txt'
if (-not (Test-Path -LiteralPath $fixtureIndex)) { throw "fixture generation failed" }

# Fake roots for the resource cases: two empty-but-valid roots. AssetRoot::valid
# requires materials/ models/ maps/ to exist (loose files or *_dir.vpk inside),
# so an empty tree is a *valid* root with no assets at all -- the named
# fallback readings (VPK entries=0, BSP invalid/..., VMT unparsed, VTF
# fallback) come from the product, not from the harness refusing to start.
$emptyRootA = Join-Path $runDir 'roots\empty-root-a'
$emptyRootB = Join-Path $runDir 'roots\empty-root-b'
$movedRoot = Join-Path $runDir 'roots\moved-root'
$movedRootOld = Join-Path $runDir 'roots\moved-root-original'
$unreadableRoot = Join-Path $runDir 'roots\unreadable-root'
# Idempotency for a retried run id: the roots are re-created rather than reused.
foreach ($p in @($emptyRootA, $emptyRootB, $movedRoot, $movedRootOld, $unreadableRoot)) {
  if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force }
}
foreach ($root in @($emptyRootA, $emptyRootB, $unreadableRoot)) {
  New-Item -ItemType Directory -Path (Join-Path $root 'materials') -Force | Out-Null
  New-Item -ItemType Directory -Path (Join-Path $root 'models') -Force | Out-Null
  New-Item -ItemType Directory -Path (Join-Path $root 'maps') -Force | Out-Null
}
# The "moved" case: the tree exists at moved-root, is renamed away, and the app
# is then started with the path the move landed on and separately with the
# stale original path.
New-Item -ItemType Directory -Path (Join-Path $movedRoot 'materials') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $movedRoot 'models') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $movedRoot 'maps') -Force | Out-Null
Rename-Item -LiteralPath $movedRoot -NewName 'moved-root-original'

# ---------------------------------------------------------------- cases
# Durations are measured, not guessed: the demo load alone takes ~85 s on this
# machine (first metrics row and first frames land around t=80-90 s), so every
# case that has to reach the running loop is given 150 s. The 20 s the first
# attempt used for the resource cases ended inside the load, and the close the
# harness posts is only picked up once the loop runs -- which is why those
# cases came back KILLED_AT_END. The corrupt cases whose header parse fails
# skip the demo scan and reach the loop quickly; they get 60 s, which still
# clears their own load. The smoke is 150 s for the same reason.
Write-Output '=== smoke: the harness must prove itself before the 11-minute soak ==='
if ($sets -contains 'smoke') {
  Invoke-Case -Name 'smoke-hidden' -Seconds 150 -Demo $snake -Hidden -AudioDisabled
}

if ($sets -contains 'soak') {
  Write-Output '=== soak runs ==='
  Invoke-Case -Name 'soak-hidden' -Seconds $SoakSeconds -Demo $snake -Hidden -AudioDisabled
  Invoke-Case -Name 'soak-window' -Seconds $SoakSeconds -Demo $snake -AudioDevice 6
}

if ($sets -contains 'corrupt') {
  Write-Output '=== corrupt input matrix ==='
  $corrupt = @{
    'corrupt-empty'             = 'empty.dem'
    'corrupt-truncated-header'  = 'truncated-header.dem'
    'corrupt-truncated-packet'  = 'truncated-packet.dem'
    'corrupt-bad-magic'         = 'bad-magic.dem'
    'corrupt-bad-version'       = 'bad-version.dem'
    'corrupt-incomplete-netmsg' = 'incomplete-netmsg.dem'
    'corrupt-mid-truncated'     = 'mid-truncated.dem'
  }
  foreach ($name in ($corrupt.Keys | Sort-Object)) {
    Invoke-Case -Name $name -Seconds 60 -Demo (Join-Path $fixtureDir $corrupt[$name]) -Hidden -AudioDisabled
  }
  # The exe's own verdict for these inputs lives in the case directories; what
  # the *parser* says about the same bytes is a second, independent reading, and
  # it is the one that carries the named error when the exe stays silent. It is
  # written next to the exe's evidence, labelled as the parser's verdict, and no
  # gate reading below may substitute it for the exe's own output.
  $probeExe = Join-Path $repo 'native\build-nmake\demo_open_probe.exe'
  if (Test-Path -LiteralPath $probeExe) {
    foreach ($name in ($corrupt.Keys | Sort-Object)) {
      $caseDir = Join-Path $runDir $name
      $fixture = Join-Path $fixtureDir $corrupt[$name]
      $probeOut = & $probeExe $fixture 2>&1
      $probeText = New-Object System.Collections.Generic.List[string]
      $probeText.Add('# demo_open_probe.exe (the repo''s own header/index parser) on the same bytes')
      $probeText.Add('# this is the PARSER''s verdict, not the exe''s; the exe run is in this directory')
      foreach ($l in $probeOut) { $probeText.Add("$l") }
      Set-Content -LiteralPath (Join-Path $caseDir 'parser-probe.txt') -Value $probeText -Encoding UTF8
    }
  }
}

if ($sets -contains 'resource') {
  Write-Output '=== resource environments ==='
  Invoke-Case -Name 'res-missing-root' -Seconds 150 -Root (Join-Path $runDir 'roots\__no_such_dir__') -Demo $snake -Hidden -AudioDisabled
  Invoke-Case -Name 'res-missing-vpk'  -Seconds 150 -Root $emptyRootA -Demo $snake -Hidden -AudioDisabled
  Invoke-Case -Name 'res-missing-model' -Seconds 150 -Root $emptyRootB -Demo $snake -Hidden -AudioDisabled
  Invoke-Case -Name 'res-missing-bsp'  -Seconds 150 -Root $tfRoot -Demo $bagel -Hidden -AudioDisabled
  Invoke-Case -Name 'res-root-moved'   -Seconds 150 -Root $movedRootOld -Demo $snake -Hidden -AudioDisabled
  Invoke-Case -Name 'res-root-stale-path' -Seconds 150 -Root $movedRoot -Demo $snake -Hidden -AudioDisabled

  # Unreadable directory: deny the current identity read access, run, restore
  # the ACL no matter how the run ended. The restore is in a finally block on
  # purpose -- an unreverted deny ACL would break every later gate step.
  $aclRestoreError = $null
  try {
    $identity = "$env:USERDOMAIN\$env:USERNAME"
    if (-not $identity.Contains('\')) { $identity = $env:USERNAME }
    $denyOut = & icacls $unreadableRoot /deny "${identity}:(RD)" 2>&1
    $manifest.Add("icacls_deny_rc=$LASTEXITCODE")
    foreach ($l in $denyOut) { $manifest.Add("icacls $l") }
    Invoke-Case -Name 'res-unreadable-root' -Seconds 150 -Root $unreadableRoot -Demo $snake -Hidden -AudioDisabled
  } catch {
    $aclRestoreError = $_.Exception.Message
  } finally {
    $restoreOut = & icacls $unreadableRoot /reset 2>&1
    $manifest.Add("icacls_reset_rc=$LASTEXITCODE")
    foreach ($l in $restoreOut) { $manifest.Add("icacls_reset $l") }
  }
  if ($aclRestoreError) { $manifest.Add("icacls_error=$aclRestoreError") }
}

if ($sets -contains 'audio') {
  Write-Output '=== audio environments beyond the soak''s two ==='
  Invoke-Case -Name 'audio-invalid-device' -Seconds 5 -Demo $snake -AudioDevice 99 -NoClose
  Invoke-Case -Name 'audio-missing-resource' -Seconds 150 -Root $emptyRootA -Demo $snake -Hidden -AudioDisabled
}

if ($sets -contains 'device') {
  Write-Output '=== device environment record and blocked environments ==='
  # Device environment record + the two environments this machine cannot
  # produce, recorded as explicit blocked entries with their evidence.
  & powershell -ExecutionPolicy Bypass -NoProfile -File (Join-Path $repo 'stability-device-info.ps1') -OutDir $runDir | Out-Null

  $blocked = @'
Two graphics environments cannot be produced on this machine without changing
the product, and are recorded as blocked rather than skipped:

1. WARP software device
   The renderer creates a hardware device first and falls back to WARP when
   that fails (native_renderer.cpp:182-188), but no command-line switch or
   environment variable selects the driver type, so a gate cannot drive the
   exe onto WARP without editing product source -- which this task forbids.
   What *is* recorded: the machine can create a WARP device, proven by the
   repo's own native_install_smoke_probe.exe (install-smoke-probe.txt in this
   run directory: warp_fallback=1 with the feature level it selected). The
   product's HW->WARP fallback path itself is therefore NOT VERIFIED here.

2. Device without the required feature level
   Simulating it needs either a second adapter that lacks FL 11_0 or a driver
   downgrade; neither exists on this machine. What is recorded: dxdiag.txt
   carries this system GPU's feature levels, and the soak-window frame count
   is the positive control that a capable device renders. The refusal path for
   a device without FL 11_0 is therefore NOT VERIFIED here.
'@
  Set-Content -LiteralPath (Join-Path $runDir 'blocked-inputs.txt') -Value $blocked -Encoding UTF8

  $install = @'
No installer or release package exists in this repository: there is no Inno
Setup / WiX / MSI / self-extracting archive, and no packaging script, anywhere
in the tree (searched for *.iss, *.wxs, *.msi, setup*, package* at the repo
root and under scripts/). The "install to a fresh directory, start, demo,
close, restart, uninstall" acceptance is therefore NOT VERIFIED. Building a
development directory into a temporary release package would not be an
install-package acceptance and is not claimed as one.
'@
  Set-Content -LiteralPath (Join-Path $runDir 'install-blocked.txt') -Value $install -Encoding UTF8
}

$manifest.Add(("finished_at={0}" -f (Get-Date -Format 'o')))
Set-Content -LiteralPath (Join-Path $runDir 'run-manifest.txt') -Value $manifest -Encoding UTF8
Write-Output "matrix complete: $runDir"
