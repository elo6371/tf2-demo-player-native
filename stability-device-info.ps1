# stability-device-info.ps1 -- record the machine's graphics and audio
# environment once per run directory, so the device matrix is read against a
# pinned environment rather than against memory.
#
# Writes device-info.txt (and dxdiag.txt when dxdiag is available) into the
# given directory:
#   * every video controller (name, driver, status, video mode)
#   * the session type the runs happen in (console vs remote): a remote session
#     is not the physical GPU, and a device-matrix PASS read over RDP would be
#     a PASS on the remote display adapter instead
#   * dxdiag's reported feature levels for the system GPU
#   * the machine's WARP capability, read through the repo's own
#     native_install_smoke_probe.exe (which creates a WARP device and reports
#     the feature level), plus a run from a copied install directory whose TF
#     root is absent (the install-smoke contract)
#
# Usage: powershell -File stability-device-info.ps1 -OutDir <dir>

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$OutDir,
  [string]$InstallSmokeProbe
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

if ([string]::IsNullOrEmpty($InstallSmokeProbe)) {
  $InstallSmokeProbe = (Resolve-Path (Join-Path $PSScriptRoot 'native\build-nmake\native_install_smoke_probe.exe')).Path
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add(('recorded_at={0}' -f (Get-Date -Format 'o')))
$lines.Add(('machine={0}' -f $env:COMPUTERNAME))

$lines.Add('')
$lines.Add('=== video controllers ===')
Get-CimInstance Win32_VideoController | ForEach-Object {
  $lines.Add(("controller name={0} driver_version={1} status={2} video_processor={3} adapter_ram_mb={4} config_manager_error={5}" -f
    $_.Name, $_.DriverVersion, $_.Status, $_.VideoProcessor,
    $(if ($_.AdapterRAM) { [math]::Round($_.AdapterRAM / 1MB, 0) } else { 'unknown' }),
    $_.ConfigManagerErrorCode))
  $lines.Add(("  current_mode={0} min_refresh={1} max_refresh={2}" -f $_.CurrentHorizontalResolution,
    $_.MinRefreshRate, $_.MaxRefreshRate))
}

$lines.Add('')
$lines.Add('=== session ===')
$sessionName = (Get-Process -Id $PID).SessionId
# SESSIONNAME is the session's own name: "Console" for the physical desktop,
# "RDP-Tcp#N" for a remote session. A device-matrix reading taken over RDP is
# a reading of the remote display adapter, not of the machine's GPU, so this is
# recorded rather than assumed.
$lines.Add(("session_id={0}" -f $sessionName))
$lines.Add(("session_name={0}" -f $env:SESSIONNAME))
$lines.Add(("session_protocol_note=Console means the physical desktop; RDP-Tcp means a remote session"))

$lines.Add('')
$lines.Add('=== cpu / memory ===')
Get-CimInstance Win32_Processor | ForEach-Object {
  $lines.Add(("cpu name={0} cores={1} threads={2} max_mhz={3}" -f $_.Name, $_.NumberOfCores, $_.NumberOfLogicalProcessors, $_.MaxClockSpeed))
}
$os = Get-CimInstance Win32_OperatingSystem
$lines.Add(("total_memory_mb={0} free_memory_mb={1}" -f
  [math]::Round($os.TotalVisibleMemorySize / 1024, 0), [math]::Round($os.FreePhysicalMemory / 1024, 0)))
$lines.Add(("os={0} version={1} build={2}" -f $os.Caption, $os.Version, $os.BuildNumber))

$lines.Add('')
$lines.Add('=== audio output devices (waveOut enumeration) ===')
$winmm = Join-Path $PSScriptRoot 'native\build-nmake\winmm_sink_probe.exe'
if (Test-Path -LiteralPath $winmm) {
  $audioOut = & $winmm --list-devices 2>&1
  foreach ($line in $audioOut) { $lines.Add("  $line") }
} else {
  $lines.Add('  winmm_sink_probe not built; audio device list not captured')
}

Set-Content -LiteralPath (Join-Path $OutDir 'device-info.txt') -Value $lines -Encoding UTF8

# dxdiag: feature levels of the real GPU. -xhtml is the only silent mode; the
# text dump is parsed for the Display Devices section.
$dxdiagOut = Join-Path $OutDir 'dxdiag.txt'
$dxdiagOk = $false
try {
  $p = Start-Process -FilePath 'dxdiag' -ArgumentList '/t', $dxdiagOut -Wait -PassThru -WindowStyle Hidden -ErrorAction Stop
  $dxdiagOk = ($p.ExitCode -eq 0) -and (Test-Path -LiteralPath $dxdiagOut)
} catch {
  $dxdiagOk = $false
}
if (-not $dxdiagOk) {
  Set-Content -LiteralPath $dxdiagOut -Value 'dxdiag unavailable on this machine' -Encoding UTF8
}

# The repo's own install-smoke probe: WARP device creation + missing TF root
# prompt, run from a copied install directory so it answers what an installed
# copy would answer.
$probeLines = New-Object System.Collections.Generic.List[string]
if (Test-Path -LiteralPath $InstallSmokeProbe) {
  $probeDir = Join-Path $OutDir 'install-copy'
  New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
  Copy-Item -LiteralPath $InstallSmokeProbe -Destination (Join-Path $probeDir 'native_install_smoke_probe.exe') -Force
  $probeOut = & (Join-Path $probeDir 'native_install_smoke_probe.exe') 2>&1
  $probeLines.Add(("exit_code={0}" -f $LASTEXITCODE))
  foreach ($line in $probeOut) { $probeLines.Add("$line") }
  $probeLines.Add(("working_directory={0}" -f $probeDir))
} else {
  $probeLines.Add("install_smoke_probe missing: $InstallSmokeProbe")
}
Set-Content -LiteralPath (Join-Path $OutDir 'install-smoke-probe.txt') -Value $probeLines -Encoding UTF8

Write-Output "device-info written to $OutDir"
