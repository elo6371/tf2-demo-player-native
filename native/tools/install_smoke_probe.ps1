param([string]$InstallDir = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$required = @('tf2_demo_native.exe', 'native_install_smoke_probe.exe')
foreach ($name in $required) {
  if (-not (Test-Path (Join-Path $InstallDir $name) -PathType Leaf)) { throw "missing_install_file=$name" }
}
$system32 = Join-Path $env:WINDIR 'System32'
foreach ($name in @('d3d11.dll', 'dxgi.dll', 'd3dcompiler_47.dll', 'winmm.dll')) {
  if (-not (Test-Path (Join-Path $system32 $name) -PathType Leaf)) { throw "missing_system_dependency=$name" }
}
$probe = Join-Path $InstallDir 'native_install_smoke_probe.exe'
$output = & $probe 2>&1
if ($LASTEXITCODE -ne 0) { throw "install_probe_failed exit=$LASTEXITCODE output=$output" }
Write-Output ($output -join "`n")
Write-Output 'install_layout=ok tf2_assets_packaged=0'
