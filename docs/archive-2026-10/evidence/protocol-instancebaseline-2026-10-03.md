# Protocol checkpoint: instancebaseline class mapping (2026-10-03)

## Scope

This checkpoint changes only `native/include/demo_header.h` and `native/src/demo_header.cpp`.

## Fixes

- Decode string-table text with the Source history-copy form (`history index`, prefix length, remainder).
- Treat the `instancebaseline` entry text as the server class ID. The string-table entry index is only a table index and is not used to select a `ServerClass`.
- Pass the fixed user-data bit count directly. The four-bit `FixedUserDataSize::bits` field is already a bit count; multiplying it by eight was incorrect.
- Apply baseline and entity updates to a temporary `EntityState`, committing it only after the update stream decodes successfully. A failed delta therefore cannot leave a partially applied entity state.

## Verification

- CMake configure: passed with Windows SDK `10.0.26100.0`.
- Release build: passed (`native/build/Release/tf2_demo_native.exe`).
- Real Demo smoke test: `843e396401732f6352f49687b51cec6f_matcha-20260909-1303-cp_sunshine.dem`.
- After six seconds: process alive, `Responding=True`, `base=3 baseok=1 basefail=1 basezip=1`, `entityfail=241806`, `epstate=120903`, no crash.

The `baseok=1` result is consistent with the sample containing only two Enter updates and many Preserve updates; it proves the corrected class-text lookup is exercised, but does not prove complete entity delta semantics.

## Remaining protocol work

- Preserve updates still require a complete baseline/checkpoint model for every entity and both baseline slots.
- Complex SendProp encodings (SendProxy, XYZE, nested arrays/data tables, and all float flags) remain incomplete.
- A per-tick externally queryable snapshot is not yet exposed by this module.
