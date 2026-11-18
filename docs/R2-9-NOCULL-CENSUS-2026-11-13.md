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

## Cull on/off at a named Bagel oak

A fixed camera was placed at `--debug-camera 3660 -1216 600 5 0`, looking at the
large `oak_l_01` static prop at `(4160, -1216, 432)` (decoded from the BSP
static-prop records). Three runs were captured at tick 0 on the Bagel RC11 demo:

| Run | rc | Diff vs default |
|---|---:|---|
| default world cull | 0 | — |
| `--no-world-cull` | 0 | `differing_pixels=689683`, bbox whole frame |
| `--world-geometry none` | 0 | `differing_pixels=860784`, bbox whole frame |

The static-prop pass still reports `prepared=268 uploaded=268 rejected=0` in
both, and `--world-geometry none` removes only the world. The oak is a static
prop, not a world brush face.

**This is a negative control, not a positive `$nocull` case.** The global
`--no-world-cull` switch changes 689,683 pixels across the whole frame because
it disables backface culling for every world face; that difference cannot be
attributed to the oak's `$nocull` material. The model path already has
per-material cull handling (the 2026-11-13 candidate review), and the oak's
material is a model material. No targeted world-brush face carrying `$nocull`
has been demonstrated on Bagel yet, so **no world-renderer patch is made**.

Evidence: `D:/_shared/EXEC-2026-11-13/F2/bagel-cull/` (`cull-on.trace`,
`cull-off.trace`, `world-none.trace`, three BMPs).


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
- **Not decided:** whether a Bagel **world-brush face** carries one of the five
  materials. The oak camera above is a static prop, and the global cull switch
  is not a per-material control, so it cannot decide this. Until a targeted
  face fixture exists, F2-3 stays read-only and no world-renderer change is made.
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
