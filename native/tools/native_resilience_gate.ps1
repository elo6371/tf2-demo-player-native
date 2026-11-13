param(
  [string]$InstallDir = (Join-Path $PSScriptRoot '..\build\Release'),
  [Parameter(Mandatory = $true)][string]$Demo,
  [string]$MissingTfRoot = (Join-Path ([System.IO.Path]::GetTempPath()) 'tf2-demo-missing-root'),
  [int]$Seconds = 3
)

$ErrorActionPreference = 'Stop'
$probe = Join-Path $InstallDir 'demo_open_probe.exe'
if (-not (Test-Path $probe -PathType Leaf)) { throw "missing_probe=$probe" }
if (-not (Test-Path $Demo -PathType Leaf)) { throw "missing_demo=$Demo" }

function Invoke-Probe([string]$Path) {
  $output = (& $probe $Path 2>&1 | Out-String).Trim()
  if ($LASTEXITCODE -ne 0) { throw "probe_failed path=$Path exit=$LASTEXITCODE output=$output" }
  return $output
}

$normal = Invoke-Probe $Demo
if ($normal -notmatch 'header=1\s+header_error=\s+index=1') {
  throw "normal_demo_rejected output=$normal"
}

$truncated = Join-Path ([System.IO.Path]::GetTempPath()) ("tf2-demo-truncated-{0}.dem" -f [guid]::NewGuid())
try {
  $source = [System.IO.File]::OpenRead($Demo)
  $target = [System.IO.File]::Create($truncated)
  try {
    $buffer = New-Object byte[] 4096
    $count = $source.Read($buffer, 0, $buffer.Length)
    $target.Write($buffer, 0, $count)
  }
  finally { $target.Dispose(); $source.Dispose() }
  $bad = Invoke-Probe $truncated
  if ($bad -notmatch 'header=1\s+header_error=\s+index=0') {
    throw "truncated_demo_accepted output=$bad"
  }
}
finally { Remove-Item -LiteralPath $truncated -Force -ErrorAction SilentlyContinue }

$stable = Join-Path $PSScriptRoot 'native_stability_gate.ps1'
if (-not (Test-Path $stable -PathType Leaf)) { throw "missing_stability_gate=$stable" }
$missing = & powershell -NoProfile -ExecutionPolicy Bypass -File $stable `
  -InstallDir $InstallDir -TfRoot $MissingTfRoot -Seconds $Seconds 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "missing_root_gate_failed output=$missing" }
if ($missing -notmatch '"stability"\s*:\s*"pass"') { throw "missing_root_not_pass output=$missing" }

Write-Output ('{"resilience":"pass","normal_demo":1,"truncated_demo_rejected":1,"missing_tf_root":1,"stability_output":' +
  (($missing.Trim() | ConvertTo-Json -Compress)))
