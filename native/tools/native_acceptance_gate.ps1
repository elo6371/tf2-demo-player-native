param(
  [string]$InstallDir = (Join-Path $PSScriptRoot '..\build\Release'),
  [string]$TfRoot = '',
  [string]$Demo = '',
  [int]$Seconds = 30,
  [int]$MinSamples = 3,
  [double]$MinFps = 0,
  [long]$MaxWorkingSetBytes = 0,
  [switch]$Play,
  [switch]$MissingTfRoot,
  [switch]$RequireWarp
)

$ErrorActionPreference = 'Stop'
if ($Seconds -lt 2) { throw 'seconds_must_be_at_least_2' }
if ($MinSamples -lt 1) { throw 'min_samples_must_be_positive' }
if ($MinFps -lt 0) { throw 'min_fps_must_be_nonnegative' }
if ($MaxWorkingSetBytes -lt 0) { throw 'max_working_set_must_be_nonnegative' }
if ($MissingTfRoot -and $TfRoot) { throw 'missing_tf_root_conflicts_with_tf_root' }
if ($Play -and -not $Demo) { throw 'play_requires_demo' }

$exe = Join-Path $InstallDir 'tf2_demo_native.exe'
$smoke = Join-Path $InstallDir 'native_install_smoke_probe.exe'
if (-not (Test-Path $exe -PathType Leaf)) { throw "missing_executable=$exe" }
if (-not (Test-Path $smoke -PathType Leaf)) { throw "missing_smoke_probe=$smoke" }
if ($Demo -and -not (Test-Path $Demo -PathType Leaf)) { throw "missing_demo=$Demo" }
if ($TfRoot -and -not (Test-Path $TfRoot -PathType Container)) { throw "missing_tf_root=$TfRoot" }

$smokeOutput = (& $smoke 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "warp_probe_failed exit=$LASTEXITCODE output=$smokeOutput" }
if ($RequireWarp -and $smokeOutput -notmatch 'warp_fallback=1') {
  throw "warp_fallback_missing output=$smokeOutput"
}

$effectiveTfRoot = $TfRoot
if ($MissingTfRoot) {
  $effectiveTfRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("tf2-native-missing-{0}" -f [guid]::NewGuid())
  if (Test-Path $effectiveTfRoot) { throw "generated_missing_root_exists=$effectiveTfRoot" }
}

$metrics = Join-Path ([System.IO.Path]::GetTempPath()) ("tf2-native-acceptance-{0}.csv" -f [guid]::NewGuid())
$arguments = @('--no-vsync', '--metrics-file', $metrics)
if (-not $Play) { $arguments += '--start-paused' }
if ($effectiveTfRoot) { $arguments += @('--tf-root', $effectiveTfRoot) }
if ($Demo) { $arguments += @('--demo', $Demo) }

# Windows PowerShell 5.1 joins an argument array with spaces and does not quote
# paths. TF2 lives under "Team Fortress 2", so an unquoted --demo never opens.
function Format-NativeArgument([string]$text) {
  if ($text -match '[\s"]') { return '"' + ($text.Replace('"', '\"')) + '"' }
  return $text
}
$quotedArguments = (@($arguments | ForEach-Object { Format-NativeArgument $_ })) -join ' '

$process = $null
$exitCode = $null
try {
  $process = Start-Process -FilePath $exe -ArgumentList $quotedArguments -WorkingDirectory $InstallDir -PassThru
  # Demo indexing and map upload run before the message loop. A real TF2 demo
  # on this machine needs more than the old 10 second idle budget.
  $idleMs = $(if ($Play) { 60000 } else { 10000 })
  if (-not $process.WaitForInputIdle($idleMs)) { throw 'process_input_idle_timeout' }
  Start-Sleep -Seconds $Seconds
  if ($process.HasExited) { throw "process_exited_early=$($process.ExitCode)" }
}
finally {
  if ($process -and -not $process.HasExited) {
    $process.CloseMainWindow() | Out-Null
    if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
  }
  if ($process) { $exitCode = $process.ExitCode }
}

if ($exitCode -ne 0) { throw "process_exit_code=$exitCode" }
if (-not (Test-Path $metrics -PathType Leaf)) { throw 'metrics_file_missing' }
$rows = @(Import-Csv $metrics)
try {
  if ($rows.Count -lt $MinSamples) { throw "metrics_samples_below_minimum count=$($rows.Count) minimum=$MinSamples" }
  $fps = @($rows | ForEach-Object { [double]$_.fps } | Where-Object {
    -not [double]::IsNaN($_) -and -not [double]::IsInfinity($_) -and $_ -ge 0
  })
  $workingSet = @($rows | ForEach-Object { [long]$_.working_set_bytes } | Where-Object { $_ -gt 0 })
  if ($fps.Count -ne $rows.Count) { throw 'metrics_fps_invalid' }
  if ($workingSet.Count -ne $rows.Count) { throw 'metrics_working_set_invalid' }
  $averageFps = ($fps | Measure-Object -Average).Average
  $peakWorkingSet = ($workingSet | Measure-Object -Maximum).Maximum
  if ($MinFps -gt 0 -and $averageFps -lt $MinFps) {
    throw ("fps_below_threshold average={0:N2} threshold={1:N2}" -f $averageFps, $MinFps)
  }
  if ($MaxWorkingSetBytes -gt 0 -and $peakWorkingSet -gt $MaxWorkingSetBytes) {
    throw ("working_set_above_threshold peak={0} threshold={1} average_fps={2:N2} samples={3}" -f $peakWorkingSet, $MaxWorkingSetBytes, $averageFps, $rows.Count)
  }
  [pscustomobject]@{
    acceptance = 'pass'
    samples = $rows.Count
    average_fps = [math]::Round($averageFps, 2)
    peak_working_set_bytes = $peakWorkingSet
    play_mode = [bool]$Play
    missing_tf_root = [bool]$MissingTfRoot
    warp_probe = ($smokeOutput -match 'warp_fallback=1')
  } | ConvertTo-Json -Compress
}
finally {
  Remove-Item -LiteralPath $metrics -Force -ErrorAction SilentlyContinue
}
