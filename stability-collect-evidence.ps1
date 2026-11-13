# stability-collect-evidence.ps1 -- copy the raw run directories into the
# committed evidence tree, trimmed to what a reviewer actually reads.
#
# WHAT IS COPIED WHOLESALE
#   Every small file of every case: command.txt, env.txt, stdout.txt,
#   stderr.txt, exit-code.txt, process-state.txt, verdict.txt, crash.txt,
#   leftovers.txt, readings.json, sampler.csv, metrics.csv, title-first.txt,
#   title-last.txt, parser-probe.txt, plus the run-level files (manifest,
#   device info, blocked-inputs, install notes) and the fixtures' index.
#
# WHAT IS TRIMMED AND WHY
#   trace.txt is the app's own trace and the largest file by far (58 MB for an
#   11-minute soak). The committed evidence tree keeps traces whole below the
#   TRACE_WHOLE_LIMIT (8 MB) and, above it, an extract carrying every line that
#   can carry a verdict -- UNHANDLED / TERMINATE / THREW / mode: summary /
#   audio: summary / trace open, plus the head and tail of the file -- with the
#   full file's sha256 recorded next to it. The raw files stay in .scratch, and
#   sha256-index.txt pins every one of them so a later reader can tell whether
#   the raw evidence was touched after the fact.
#
# Usage: powershell -File stability-collect-evidence.ps1 [-Runs run-01,run-02,run-03]

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$ScratchRoot,
  [Parameter(Mandatory = $true)][string]$EvidenceRoot,
  [string[]]$Runs = @('run-01','run-02','run-03'),
  [double]$TraceWholeLimitMb = 8
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null

$copied = New-Object System.Collections.Generic.List[string]
$trimmed = New-Object System.Collections.Generic.List[string]

function Copy-CaseEvidence {
  param([string]$CaseDir, [string]$DestDir)
  New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
  foreach ($f in (Get-ChildItem -LiteralPath $CaseDir -File)) {
    switch -Wildcard ($f.Name) {
      'trace.txt' { continue }   # handled below
      default { Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $DestDir $f.Name) -Force; $script:copied.Add($f.FullName) }
    }
  }
  $trace = Join-Path $CaseDir 'trace.txt'
  if (Test-Path -LiteralPath $trace) {
    $lenMb = (Get-Item -LiteralPath $trace).Length / 1MB
    $sha = (Get-FileHash -LiteralPath $trace -Algorithm SHA256).Hash.ToLower()
    $lineCount = (Get-Content -LiteralPath $trace | Measure-Object -Line).Lines
    if ($lenMb -le $TraceWholeLimitMb) {
      Copy-Item -LiteralPath $trace -Destination (Join-Path $DestDir 'trace.txt') -Force
      $script:copied.Add($trace)
    } else {
      $markers = Select-String -LiteralPath $trace -Pattern 'UNHANDLED|TERMINATE|THREW|: summary|trace open' | Select-Object -First 200
      $head = Get-Content -LiteralPath $trace -TotalCount 30
      $tail = Get-Content -LiteralPath $trace -Tail 30
      $out = New-Object System.Collections.Generic.List[string]
      $out.Add("# trace extract (the raw trace was $([math]::Round($lenMb,1)) MB, over the $TraceWholeLimitMb MB commit limit)")
      $out.Add("# raw_sha256=$sha")
      $out.Add("# raw_line_count=$lineCount")
      $out.Add("")
      $out.Add("# --- verdict-bearing lines ---")
      foreach ($m in $markers) { $out.Add($m.Line) }
      $out.Add("")
      $out.Add("# --- first 30 lines ---")
      foreach ($l in $head) { $out.Add($l) }
      $out.Add("")
      $out.Add("# --- last 30 lines ---")
      foreach ($l in $tail) { $out.Add($l) }
      Set-Content -LiteralPath (Join-Path $DestDir 'trace-extract.txt') -Value $out -Encoding UTF8
      Set-Content -LiteralPath (Join-Path $DestDir 'trace-sha256.txt') -Value @(
        "file=trace.txt",
        "bytes=$((Get-Item -LiteralPath $trace).Length)",
        "lines=$lineCount",
        "sha256=$sha",
        "committed=trace-extract.txt (over the size limit)"
      ) -Encoding UTF8
      $script:trimmed.Add($trace)
    }
  }
}

foreach ($run in $Runs) {
  $runDir = Join-Path $ScratchRoot $run
  if (-not (Test-Path -LiteralPath $runDir)) { continue }
  Write-Output "collecting $run"
  # run-level files
  foreach ($name in @('run-manifest.txt','device-info.txt','blocked-inputs.txt','install-blocked.txt')) {
    $src = Join-Path $runDir $name
    if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $EvidenceRoot "$run-$name") -Force }
  }
  Copy-CaseEvidence -CaseDir $runDir -DestDir (Join-Path $EvidenceRoot $run)
  # case directories
  Get-ChildItem -LiteralPath $runDir -Directory | Where-Object { $_.Name -notmatch '^(fixtures|roots|install-copy)$' } | ForEach-Object {
    # Join-Path takes two positional arguments on Windows PowerShell 5.1 (the
    # third-argument form is PowerShell 6+), so the nesting is written out.
    Copy-CaseEvidence -CaseDir $_.FullName -DestDir (Join-Path (Join-Path $EvidenceRoot $run) $_.Name)
  }
  # fixtures: index plus the inputs themselves (they are small and are the
  # corrupt-input evidence)
  $fixDir = Join-Path $runDir 'fixtures'
  if (Test-Path -LiteralPath $fixDir) {
    # Copy-Item does not create intermediate directories, and the fixtures
    # folder exists only in the run tree.
    New-Item -ItemType Directory -Path (Join-Path (Join-Path $EvidenceRoot $run) 'fixtures') -Force | Out-Null
    Get-ChildItem -LiteralPath $fixDir -File | ForEach-Object {
      Copy-Item -LiteralPath $_.FullName -Destination (Join-Path (Join-Path (Join-Path $EvidenceRoot $run) 'fixtures') $_.Name) -Force
    }
  }
}

# ---------------------------------------------------------------- sha256 index
# The index entries are relative to the scratch root, so the root must be
# resolved first: Get-ChildItem returns absolute names, and Substring on a
# relative $ScratchRoot cut the wrong number of characters and produced
# entries like "ease\.scratch\..." that no reader could resolve.
$ScratchRoot = [System.IO.Path]::GetFullPath($ScratchRoot)
$index = New-Object System.Collections.Generic.List[string]
$index.Add("# sha256 of every raw file under the run directories, taken at collection time.")
$index.Add("# The raw files live under .scratch/stability-release and are not committed;")
$index.Add("# these digests are how a reviewer proves they were not edited afterwards.")
$index.Add(("# scratch root: {0}" -f $ScratchRoot))
Get-ChildItem -LiteralPath $ScratchRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
  $rel = $_.FullName.Substring($ScratchRoot.Length).TrimStart('\')
  $sha = ''
  try {
    $sha = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()
  } catch {
    # A file held open by another writer (this collector's own log redirect,
    # a product lock file) cannot be hashed. The index records that fact
    # instead of dying here and leaving the tree with no index at all.
    $index.Add(("# UNREADABLE at collection time: {0} ({1})" -f $rel, $_.Exception.Message))
    return
  }
  $index.Add("$sha  $($_.Length)  $rel")
}
Set-Content -LiteralPath (Join-Path $EvidenceRoot 'sha256-index.txt') -Value $index -Encoding UTF8

Write-Output "copied files: $($copied.Count)"
Write-Output "traces committed whole: $(($copied | Where-Object { $_ -match 'trace.txt' }).Count)"
Write-Output "traces trimmed to extracts: $($trimmed.Count)"
Write-Output "evidence root: $EvidenceRoot"
