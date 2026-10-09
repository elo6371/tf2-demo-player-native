param(
  [string]$BuildDir = 'native/build-fast'
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

cmake -S native -B $BuildDir -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build $BuildDir --target tf2_demo_native --parallel 2
cmake --build $BuildDir --target presentation_probe entity_model_probe resource_reachability_probe --parallel 2

& (Join-Path $BuildDir 'presentation_probe.exe') --self-test
if ($LASTEXITCODE -ne 0) { throw "presentation_probe failed: $LASTEXITCODE" }
& (Join-Path $BuildDir 'entity_model_probe.exe') --self-test
if ($LASTEXITCODE -ne 0) { throw "entity_model_probe failed: $LASTEXITCODE" }
Write-Output 'VERIFY-FAST=PASS'
