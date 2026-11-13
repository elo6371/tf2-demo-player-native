# Audio and effects (module 4) 2026-10-06

Local work only. Do not treat this file as a GitHub browse target.

## What changed

- Gameplay sounds are classified as weapon, footstep, Uber, or world, then scheduled at `playback tick + round(delaySeconds * tickRate)`. A negative delay clamps to tick 0. Voice, announcer, and music names are dropped before playback.
- Spatial gain and pan were already computed with the listener facing +X. The scheduler still uses that mix. Tests drive a sink callback, so the default audio device is not opened.
- `CTETFParticleEffect` joins the effect timeline with its particle name. The temp-entity delay byte is still not converted, because its time unit is not established.
- Binary DMX version 2 PCF files (the `<!-- dmx encoding binary 2 format pcf 1 -->` layout in TF2's misc VPK) yield particle-system names and operator function names. Titles such as `Movement Basic` count as the common operator `movement_basic`. Renderer names are stored and are not marked common. Other encodings return an error. A corrupt file does not throw.
- The viewed entity supplies health, team, class, clip, and `m_flChargeLevel` when those properties exist. Missing properties stay unknown. The window title shows the readout. Drawn HUD panels, theme import, and settings pages are still the product-UI module.

## Probe

```
native/build-mingw/audio_effects_probe.exe
```

Observed with llvm-mingw on 2026-10-06. Exit code 0. `TF_ROOT` was unset, so the real file came from `D:\Steam\steamapps\common\Team Fortress 2\tf\tf2_misc_dir.vpk`.

```
selfTest=true delay=true kinds=true voiceFiltered=true spatial=true
schedulerSink=true missingKept=true hud=true particle=true pcf=true pcfCorrupt=true
realPcf=parsed realOperators=514 realCommon=448 realFunction=Movement Basic device=sink
```

`audio_scheduler_probe.exe` with `TF_ROOT` pointed at that `tf` directory also exited 0, with `playbackCalls=0`. `temp_effect_probe.exe` exited 0.

## Not claimed

- No waveform was played on a device. The sink received one synthetic buffer in the new probe and one real stickybomb WAV in the older scheduler probe.
- `particles/bigboom.pcf` was parsed for operator names. Particles were not simulated or drawn.
- The charge value is the raw `m_flChargeLevel` on the viewed entity. It is not a medic uber percentage taken from the medigun.
- D3D was not executed. The title HUD is text. mingw's syntax check of `main.cpp` stops on `SetProcessDpiAwarenessContext`, which is the existing Win32 call, and this machine still has no Visual Studio.
- Animation and ViewModel were not merged. Long-playback acceptance was not run.
