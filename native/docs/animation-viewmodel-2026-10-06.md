# Animation and ViewModel (module 5) 2026-10-06

Local work only. Do not treat this file as a GitHub browse target.

## What changed

- `animation_decoder` reads StudioMDL version 44–49 animation tables without calling the bind-pose loader. Sequence descriptors bind to an animdesc through the short array at `animindexindex`. Looping uses sequence flag `0x0001`: the sample frame is `fmod(tick * fps / tickRate, frameCount)`. A non-looping sequence clamps to the last frame. The same tick sampled twice uses the same frame and the same bytes.
- RAWROT is Quaternion48 and RAWROT2 is Quaternion64, using the Source bit layout (21-bit biased components for Quaternion64, 16/16/15 plus a sign bit for Quaternion48). ANIMROT is three RLE channels times the bone rotation scale, then the Source `RadianEuler` conversion (x roll, y pitch, z yaw). ANIMPOS and RAWPOS (float16 Vector48) fill translation. A bone missing from the chain keeps its bind position and quaternion. Parent-before-child composition is `child model = parent model * local`, with quaternions stored xyzw.
- Malformed bone tables, animdesc indexes, negative bone-link offsets, and bone indexes past the skeleton return a reason and do not throw. External anim blocks and section blocks stored outside the MDL return a reason. TF2 class motion for this probe is in `models/player/scout_animations.mdl` and `models/player/soldier_animations.mdl`. `models/player/scout.mdl` itself has two local anims and names those include files.
- `viewmodel` resolves a `v_*.mdl` through the existing asset lookup, requires the vvd and a vtx companion, and returns a renderer-neutral request: path, sequence labels, attachment name and origin, hand matrix, and FOV. FOV defaults to 80 and clamps to 40–120 with the same comparisons as `clampViewModelFov`. NaN stays NaN. The right-hand matrix is identity. The left-hand matrix mirrors X. Attachment names are read from studio header offsets 240/244. The bind-pose loader's attachment offsets are unchanged.
- Nothing was added to the D3D draw path. Skinned meshes stay on the bind pose. The world pass still skips viewmodels.

## Probe

```
native/build-mingw/animation_viewmodel_probe.exe
```

Observed with llvm-mingw on 2026-10-06. Exit code 0. `TF_ROOT` was unset, so the real files came from `D:\Steam\steamapps\common\Team Fortress 2\tf\tf2_misc_dir.vpk`.

```
selfTest=true parent=true loop=true compressed=true malformed=true
fov=true missingCompanions=true bindPose=true scout=true soldier=true
motion=true section=true realLoop=true deterministic=true viewmodel=true
motionSequence=AttackStand_PRIMARY bones=76 anims=1012
viewSequences=8 viewSequence=idle viewAttachments=1 viewAttachment=weapon_bone
confidence=bit-layout; Euler order and position scale are not visually verified
```

Scout and Soldier animation MDLs parsed inside their bone counts. `AttackStand_PRIMARY` moved a bone between two ticks. A sectioned Scout sequence sampled past its first section. A real looping sequence wrapped back to the same positions as tick 0. Repeating that later tick matched bitwise. After decoding, `ModelLoader::inspectVpk` on `models/player/scout.mdl` still reported the same bone-0 position, the same `poseToBone`, and the same `sequenceDecodeReason` containing `not decoded`. `models/weapons/v_models/v_bat_scout.mdl` resolved from the VPK with companions, FOV 180 stored as 120, the left-hand matrix, sequence label `idle`, and attachment `weapon_bone`.

## Not claimed

- Euler angle order and position scale were not checked against an engine screenshot or a rendered frame. The formulas follow the Source SDK bit layout only.
- Bone matrices were not uploaded. The renderer still draws the bind pose. Viewmodels are not drawn.
- D3D was not executed. This machine has no Visual Studio. mingw still stops on `SetProcessDpiAwarenessContext` in `main.cpp`.
- `models/player/scout.mdl` does not contain the run cycles. Those cycles are in the included animation MDL, which the probe opens by the include name. Included models are not stitched into one virtual model.
- Particle simulation, HUD drawing, and long-playback acceptance were not changed or run.
