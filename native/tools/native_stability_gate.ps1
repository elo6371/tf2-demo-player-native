param(
  [string]$InstallDir = (Join-Path $PSScriptRoot '..\build\Release'),
  [string]$TfRoot = '',
  [string]$Demo = '',
  [int]$Seconds = 5,
  [double]$MinFps = 0,
  [switch]$Play
)

$ErrorActionPreference = 'Stop'
if ($Seconds -lt 2) { throw 'seconds_must_be_at_least_2' }
if ($MinFps -lt 0) { throw 'min_fps_must_be_nonnegative' }

$exe = Join-Path $InstallDir 'tf2_demo_native.exe'
if (-not (Test-Path $exe -PathType Leaf)) { throw "missing_executable=$exe" }

$metrics = Join-Path ([System.IO.Path]::GetTempPath()) ("tf2-native-stability-{0}.csv" -f [guid]::NewGuid())
$arguments = @('--no-vsync', '--metrics-file', $metrics)
if (-not $Play) { $arguments += '--start-paused' }
if ($TfRoot) { $arguments += @('--tf-root', $TfRoot) }
if ($Demo) { $arguments += @('--demo', $Demo) }

$process = $null
try {
  $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $InstallDir -PassThru
  if (-not $process.WaitForInputIdle(10000)) { throw 'process_input_idle_timeout' }
  Start-Sleep -Seconds $Seconds
  if ($process.HasExited) { throw "process_exited_early=$($process.ExitCode)" }
}
finally {
  if ($process -and -not $process.HasExited) {
    $process.CloseMainWindow() | Out-Null
    if (-not $process.WaitForExit(3000)) { $process.Kill(); $process.WaitForExit() }
  }
}

if (-not (Test-Path $metrics -PathType Leaf)) { throw 'metrics_file_missing' }
$rows = Import-Csv $metrics
try {
  if ($rows.Count -lt 1) { throw 'metrics_sample_missing' }
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
  [pscustomobject]@{
    stability = 'pass'
    samples = $rows.Count
    average_fps = [math]::Round($averageFps, 2)
    peak_working_set_bytes = $peakWorkingSet
    play_mode = [bool]$Play
  } | ConvertTo-Json -Compress
}
finally {
  Remove-Item -LiteralPath $metrics -Force -ErrorAction SilentlyContinue
}
