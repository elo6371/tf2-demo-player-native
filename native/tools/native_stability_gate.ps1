param(
  [string]$InstallDir = (Join-Path $PSScriptRoot '..\build\Release'),
  [string]$TfRoot = '',
  [string]$Demo = '',
  [int]$Seconds = 5,
  [double]$MinFps = 0,
  [int]$MinSamples = 1,
  [long]$MaxWorkingSetBytes = 0,
  [switch]$RequireCleanExit,
  [switch]$Play
)

$ErrorActionPreference = 'Stop'
if ($Seconds -lt 2) { throw 'seconds_must_be_at_least_2' }
if ($MinFps -lt 0) { throw 'min_fps_must_be_nonnegative' }
if ($MinSamples -lt 1) { throw 'min_samples_must_be_positive' }
if ($MaxWorkingSetBytes -lt 0) { throw 'max_working_set_must_be_nonnegative' }

$exe = Join-Path $InstallDir 'tf2_demo_native.exe'
if (-not (Test-Path $exe -PathType Leaf)) { throw "missing_executable=$exe" }

$metrics = Join-Path ([System.IO.Path]::GetTempPath()) ("tf2-native-stability-{0}.csv" -f [guid]::NewGuid())
$arguments = @('--no-vsync', '--metrics-file', $metrics)
if (-not $Play) { $arguments += '--start-paused' }
if ($TfRoot) { $arguments += @('--tf-root', $TfRoot) }
if ($Demo) { $arguments += @('--demo', $Demo) }

$process = $null
$cleanExit = $false
try {
  $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $InstallDir -PassThru
  if (-not $process.WaitForInputIdle(10000)) { throw 'process_input_idle_timeout' }
  Start-Sleep -Seconds $Seconds
  if ($process.HasExited) { throw "process_exited_early=$($process.ExitCode)" }
}
finally {
  if ($process -and -not $process.HasExited) {
    $process.CloseMainWindow() | Out-Null
    if (-not $process.WaitForExit(3000)) {
      $process.Kill()
      $process.WaitForExit()
    }
  }
  if ($process -and $process.HasExited) { $cleanExit = ($process.ExitCode -eq 0) }
}

if (-not (Test-Path $metrics -PathType Leaf)) { throw 'metrics_file_missing' }
$rows = Import-Csv $metrics
try {
  if ($rows.Count -lt 1) { throw 'metrics_sample_missing' }
  if ($rows.Count -lt $MinSamples) {
    throw "metrics_samples_below_threshold samples=$($rows.Count) threshold=$MinSamples"
  }
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
  $peakWorkingSet = ($workingSet | Measure-Object -Maximum).Maximum
  if ($MaxWorkingSetBytes -gt 0 -and $peakWorkingSet -gt $MaxWorkingSetBytes) {
    throw "working_set_above_threshold peak=$peakWorkingSet threshold=$MaxWorkingSetBytes"
  }
  if ($RequireCleanExit -and -not $cleanExit) { throw 'process_did_not_exit_cleanly' }
  [pscustomobject]@{
    stability = 'pass'
    samples = $rows.Count
    average_fps = [math]::Round($averageFps, 2)
    peak_working_set_bytes = $peakWorkingSet
    clean_exit = $cleanExit
    play_mode = [bool]$Play
  } | ConvertTo-Json -Compress
}
finally {
  Remove-Item -LiteralPath $metrics -Force -ErrorAction SilentlyContinue
}
