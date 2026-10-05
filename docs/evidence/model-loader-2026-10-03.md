# Model resource parser evidence — 2026-10-03

## Scope

`native/include/model_loader.h` and `native/src/model_loader.cpp` add a read-only, bounds-checked Source MDL companion probe. It does not change the renderer or claim that a model is rendered.

## Local samples inspected

The installed TF2 tree contains loose model files under `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf`, including:

- `tf\download\models\arti\player\demo.mdl`
- matching `demo.vvd`, `demo.dx90.vtx`, `demo.dx80.vtx`, and `demo.sw.vtx`
- multiple class samples in the same download tree, such as `arti\player\scout.mdl` and `arti\player\engineer.mdl`

The stock path `tf\models\player\scout.mdl` was not present as a loose file during this check; stock assets may therefore require VPK lookup, which this module intentionally does not implement.

## What is implemented

- MDL/VVD/VTX signature and version-prefix validation.
- Safe file-size cap and checked offset/count arithmetic.
- Companion path derivation for `.vvd`, `.dx90.vtx`, `.dx80.vtx`, and `.sw.vtx`.
- MDL metadata extraction: name, checksum, flags, declared length, bone/attachment/sequence/body-part/texture counts.
- Fixed-layout bone records: name, parent index, and local position.
- Attachment records: name, flags, parent bone, origin, and local basis vectors.
- Sequence labels plus activity/flags/blend count from the known Source sequence descriptor layout.
- Explicit diagnostics when a table is outside the file, a companion is missing, or the resource set is incomplete.

## Current limits

- No GPU mesh upload, vertex decoding, skinning, animation evaluation, material binding, or ViewModel rendering.
- No VPK-backed model lookup; the caller must supply an extracted loose MDL path.
- VTX topology and VVD vertex/fixup tables are only signature checked; they are not decoded.
- The parser reports a structurally complete resource set, not a render-ready mesh.

## Minimal verification

The module is now listed in the native CMake target and is compiled into the Release executable. It remains read-only and does not claim GPU mesh upload or skinning; runtime use is still pending a model rendering integration task.
