param(
  [string]$BuildDir = 'native/build-fast',
  [string]$TfRoot = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf',
  [string]$Map = 'cp_snakewater_final1'
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

cmake -S native -B $BuildDir -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build $BuildDir --target resource_reachability_probe world_material_probe --parallel 2
& (Join-Path $BuildDir 'resource_reachability_probe.exe') $TfRoot $Map
if ($LASTEXITCODE -ne 0) { throw "resource_reachability_probe failed: $LASTEXITCODE" }
& (Join-Path $BuildDir 'world_material_probe.exe') --self-test
if ($LASTEXITCODE -ne 0) { throw "world_material_probe failed: $LASTEXITCODE" }
Write-Output 'VERIFY-RENDER=PASS'
