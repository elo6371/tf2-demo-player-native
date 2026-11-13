# resource-discovery-capture.ps1 -- T9 (2026-10-16): produce the product runs of
# the resource-discovery gate. Bash does the judging; this file only makes the
# runs, because every reading it collects is a window title and a trace file and
# both belong to the product process, which has to be launched and closed here.
#
# WHY FIVE CASES
#   The lookup has three stores and two ways to fail. A gate that only ran the
#   happy path would pass under a product that fell back to anything at all, so
#   each store and each refusal is its own case:
#     installed-map   a BSP present in tf/maps                    -> store=maps
#     download-gully  a BSP present only in tf/download/maps      -> store=download-maps
#     download-process a second such map, so the first is not a one-off
#     missing-map     a name present in no store                  -> store=none
#     traversal-map   a name that is not a name                   -> refused
#   The title of each run carries the BSP verdict ("BSP: <n> tris" or
#   "BSP: invalid/<error>"), which is the scene consequence of the lookup: the
#   store the product read decides whether the world was parsed at all.
#
# WHY THE FIXTURES ARE BUILT HERE
#   The two negative demos are a real demo with one 260-byte header field
#   rewritten. Hand-made copies committed to the tree would drift from the real
#   header layout they depend on; a copy generated at run time from the live
#   demo cannot. The map name field is byte 536..795 of an HL2DEMO header.
#
# USAGE
#   powershell -File resource-discovery-capture.ps1 -OutDir <dir>
#   [-TfRoot <path>] [-Seconds 45] [-Only <case-name>]
# Exit 0 when every case produced a title, 1 otherwise.

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$OutDir,
  [string]$TfRoot = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf',
  [int]$Seconds = 45,
  [int]$AudioDevice = 6,
  [string]$Only
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = $PSScriptRoot
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$demoDir = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos'
$sourceDem = Join-Path $demoDir 'autorecord_2026-07-17_23-17-46.dem'
if (-not (Test-Path -LiteralPath $sourceDem)) { throw "missing source demo: $sourceDem" }
if (-not (Test-Path -LiteralPath $TfRoot)) { throw "missing tf root: $TfRoot" }

# ---- fixtures: one real demo, one header field rewritten -----------------
$MapFieldOffset = 536
$MapFieldLength = 260

function New-MapFixture {
  param([string]$Name, [string]$MapName)
  $path = Join-Path $OutDir ("fixtures\{0}.dem" -f $Name)
  New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
  if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
  Copy-Item -LiteralPath $sourceDem -Destination $path
  $bytes = [System.IO.File]::ReadAllBytes($path)
  $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($MapName)
  if ($nameBytes.Length -gt ($MapFieldLength - 1)) { throw "map name too long: $MapName" }
  for ($i = 0; $i -lt $MapFieldLength; $i++) { $bytes[$MapFieldOffset + $i] = 0 }
  [Array]::Copy($nameBytes, 0, $bytes, $MapFieldOffset, $nameBytes.Length)
  [System.IO.File]::WriteAllBytes($path, $bytes)
  return $path
}

$missingDem = New-MapFixture -Name 'missing-map' -MapName 'cp_t9_no_such_map'
$traversalDem = New-MapFixture -Name 'traversal-map' -MapName '..\..\t9_probe'

$cases = @(
  @{ Name = 'installed-map';     Demo = $sourceDem },
  @{ Name = 'download-gully';    Demo = (Join-Path $demoDir 'gullyscout.dem') },
  @{ Name = 'download-process';  Demo = (Join-Path $demoDir 'processdemo.dem') },
  @{ Name = 'missing-map';       Demo = $missingDem },
  @{ Name = 'traversal-map';     Demo = $traversalDem }
)

$bad = 0
foreach ($case in $cases) {
  if ($Only -and ($case.Name -ne $Only)) { continue }
  $dir = Join-Path $OutDir $case.Name
  $argv = @('--tf-root', $TfRoot)
  $out = & (Join-Path $repo 'title-capture.ps1') -Demo $case.Demo -OutDir $dir -Seconds $Seconds -AudioDevice $AudioDevice -ExtraArgs $argv 2>&1
  $rc = $LASTEXITCODE
  $line = ($out | Where-Object { $_ -match 'TITLE-CAPTURE=' } | Select-Object -Last 1)
  if ([string]::IsNullOrEmpty($line)) { $line = "TITLE-CAPTURE=FAIL (no output, rc=$rc)" }
  if ($rc -ne 0) { $bad++ }
  Write-Output ("{0,-18} {1}" -f $case.Name, $line)
}

Write-Output ("cases={0} failed={1}" -f ($cases | Where-Object { -not $Only -or $_.Name -eq $Only }).Count, $bad)
if ($bad -gt 0) { exit 1 }
exit 0
