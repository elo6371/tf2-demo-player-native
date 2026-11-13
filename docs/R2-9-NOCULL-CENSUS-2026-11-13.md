# R2-9 `$nocull` census on the seven first-release maps

- Date: 2026-11-13
- Task: F2-3 / R2-9 (read-only census)
- Base: `t105-e5` at `f0bf300`; no product run, no build, no source edit.
- Scope: the BSP pakfile embedded in each map's own `.bsp`. This is the store
  that carries map-shipped VMTs. It does **not** scan the shared TF2 VPKs, so a
  zero here means "no declaration in the map's own pak", not "no declaration
  anywhere".

## Result

| Map | BSP pak VMTs | `$nocull` VMTs | Named declarations |
|---|---:|---:|---|
| `cp_process_f12` | 50 | 1 | `materials/toolspro/toolstrigger.vmt` (invisible tool material) |
| `cp_sunshine` | 68 | 0 | none |
| `cp_gullywash_f9` | 89 | 1 | `materials/toolspro/toolstrigger.vmt` (invisible tool material) |
| `cp_sultry_b8a` | 267 | 0 | none |
| `koth_product_final` | 32 | 1 | `materials/toolspro/toolstrigger.vmt` (invisible tool material) |
| `koth_bagel_rc11` | 132 | 5 | `models/props_foliage/tf_oak_tree01{,b,c}.vmt`, `crasher/tarp001.vmt`, `detail/detailsprites_bagel.vmt` |
| `pl_upward_f12` | 93 | 1 | `materials/toolspro/toolstrigger.vmt` (invisible tool material) |

The six non-Bagel declarations are all the same `tools/toolstrigger` material,
which is a tool texture and is not part of a visible first-release scene. Bagel
RC11 is the only map with visible candidates: the three oak-tree materials, the
crasher tarp and the Bagel detail-sprite sheet.

## What this does and does not decide

The current world-texture record and world draw batches do **not** carry a
per-material cull flag; the world brush pass uses the global cull state. That is
the code-path finding from the 2026-11-13 candidate-fix review. A declaration in
a VMT is not proof that the material reaches a world brush face: the Bagel trees
and tarp may be model/prop materials, and `detailsprites_bagel` may be a decal
sheet.

Therefore:

- **Decided:** no first-release map outside Bagel RC11 has a visible `$nocull`
  candidate in its own BSP pak. A per-map world-cull patch is not justified by
  this census alone.
- **Not decided:** whether Bagel's five materials are drawn by the world brush
  pass, and whether culling them changes a pixel. That needs a fixed camera that
  sees a named tree/tarp face plus `--world-cull` on/off frames. Until then,
  F2-3 stays read-only and no world-renderer change is made.
- **Not covered:** VMTs in the shared VPKs. If a future positive scene names a
  material that is not in the map pak, scan the VPKs before proposing a patch.

## Reproduce

```bash
python /d/_shared/EXEC-2026-11-13/F2/nocull-census.py
```

The scanner reads `materials/**/*.vmt` from each BSP's zip pak with Python's
`zipfile`, case-insensitive matches `nocull`, and prints every declaration with
its material path. Log:
`D:/_shared/EXEC-2026-11-13/F2/nocull-census.txt`.
