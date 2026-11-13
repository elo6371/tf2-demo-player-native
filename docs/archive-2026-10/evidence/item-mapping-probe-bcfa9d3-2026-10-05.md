# Item mapping probe from native-mvp `bcfa9d3` (2026-10-05)

This evidence was produced in a clean worktree created from
`bcfa9d3ca1914dfa93e753a0eed31dba86f19a51` on branch
`item-mapping-evidence`. Only this evidence file was added; no protocol,
renderer, animation, or ViewModel code was changed.

Build:

```text
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release --target item_schema_probe material_chain_probe pcf_capability_probe --parallel 2
```

Real TF2 root: `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf`.

```text
entries=11592
paint_kits=0
proto_defs bytes=9830027 signature_bytes=128
header0=0 header1=111 header2=95
wire_ok=0 safe_prefix_bytes=105 boundary_remainder_bytes=6
container_segments=unproven semantic_status=unproven decoded=false mapping=false
item=1151 item_status=1 visuals=1
paint_kit=390 status=missing
```

The PCF and VPK/material probes also passed their negative and real-resource
checks:

```text
pcf loose_count=0 vpk_status=opened vpk_pcf_count=135 header_validated=false parsed=false rendered=false mapping=false
archiveOpen=true archiveCount=4 realCubemapVtfFlags=14 cubemapParseFailures=0
emptyVtfRejected=true corruptVtfRejected=true oversizedVtfRejected=true emptyBspRejected=true
```

The remaining blocker is authoritative PaintKit/VPD field semantics and a
trusted ItemDef/PaintKit-to-material override dataset. The readable VPD strings
and VPK filenames are retained as evidence only; no material mapping is inferred.
