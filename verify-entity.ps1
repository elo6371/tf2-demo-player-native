param(
  [string]$BuildDir = 'native/build-fast',
  [string]$TfRoot = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf',
  [string]$Demo = 'D:\TF2_Demo_Player\testdata\demos\4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem'
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

cmake -S native -B $BuildDir -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build $BuildDir --target entity_model_probe --parallel 2
& (Join-Path $BuildDir 'entity_model_probe.exe') --self-test --tf-root $TfRoot --demo $Demo
if ($LASTEXITCODE -ne 0) { throw "entity_model_probe real demo failed: $LASTEXITCODE" }
Write-Output 'VERIFY-ENTITY=PASS'
