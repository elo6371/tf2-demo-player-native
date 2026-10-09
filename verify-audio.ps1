param(
  [string]$BuildDir = 'native/build-fast'
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

cmake -S native -B $BuildDir -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build $BuildDir --target audio_effects_probe audio_scheduler_probe winmm_sink_probe --parallel 2
& (Join-Path $BuildDir 'audio_effects_probe.exe')
if ($LASTEXITCODE -ne 0) { throw "audio_effects_probe failed: $LASTEXITCODE" }
& (Join-Path $BuildDir 'audio_scheduler_probe.exe')
if ($LASTEXITCODE -ne 0) { throw "audio_scheduler_probe failed: $LASTEXITCODE" }
& (Join-Path $BuildDir 'winmm_sink_probe.exe')
if ($LASTEXITCODE -ne 0) { throw "winmm_sink_probe failed: $LASTEXITCODE" }
Write-Output 'VERIFY-AUDIO=PASS (probe/sink only; no default playback device requested)'
