param(
  [string]$InstallDir = (Join-Path $PSScriptRoot '..\build\Release'),
  [Parameter(Mandatory = $true)][string]$Demo
)

$ErrorActionPreference = 'Stop'
$probe = Join-Path $InstallDir 'demo_open_probe.exe'
if (-not (Test-Path $probe -PathType Leaf)) { throw "missing_probe=$probe" }
if ([string]::IsNullOrWhiteSpace($Demo)) { throw 'demo_list_empty' }
$demoPaths = @($Demo -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($demoPaths.Count -eq 0) { throw 'demo_list_empty' }

$results = @()
foreach ($path in $demoPaths) {
  if (-not (Test-Path $path -PathType Leaf)) { throw "missing_demo=$path" }
  $output = (& $probe '--scan' $path 2>&1 | Out-String).Trim()
  if ($LASTEXITCODE -ne 0) { throw "scan_failed path=$path exit=$LASTEXITCODE output=$output" }
  $required = @('header=1', 'index=1', 'scan=1', 'entity_failures=0',
    'entity_unknown_state_failures=0', 'entity_prop_index_failures=0',
    'entity_prop_value_failures=0', 'temp_failures=0')
  foreach ($token in $required) {
    if ($output -notmatch [regex]::Escape($token)) {
      throw "regression_failed path=$path missing=$token output=$output"
    }
  }
  $entityMatch = [regex]::Match($output, 'entities=(\d+)')
  $results += [pscustomobject]@{
    path = $path
    passed = $true
    entities = if ($entityMatch.Success) { [long]$entityMatch.Groups[1].Value } else { 0 }
  }
}

[pscustomobject]@{
  regression = 'pass'
  samples = $results.Count
  results = $results
} | ConvertTo-Json -Compress
