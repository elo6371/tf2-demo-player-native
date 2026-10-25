# T59 — world materials: patch wrappers are followed, the rest are refused by name

Branch `task/t59-materials` at `c06e82c` (T58 integrated). Worktree
`D:/TF2_Native_Worktrees/t59-materials`. Every number below was read from a
product trace or a gate log on this tree; nothing is copied from another wave.

## 1. What the task asked, and what was already known

T56's static audit had already named this defect and called it a limitation, not
missing assets:

- `evidence/t56-map-audit/inventory.md:63` — "No-$basetexture materials are mostly
  patch shaders inheriting the base via include ... a semantics limitation, not
  missing assets".
- `evidence/t56-map-audit/inventory.md:82` — "Product does not follow include ...
  its vmtNoBase counter includes patch materials that do resolve".

So T59's job was to close a known, documented gap end to end — declaration, parse,
upload/tile, pixels — and to leave every refusal that is *correct* still named.

## 2. The defect at the text level

Reading the map's own pak (lump 40 of `pl_odyssey.bsp`) with the ledger helpers,
each refused material is a wrapper:

    "patch"
    {
        "include" "materials/METAL/METALCHROME002.vmt"
        "replace" { "$envmap" "maps/pl_odyssey/c-2960_3352_-1088" }
    }

The include resolves: `materials/METAL/METALCHROME002.vmt` sits in
`vpk:tf2_misc_dir.vpk` (196 B) and declares `"LightMappedGeneric"
{ "$basetexture" "Metal/metalchrome002" ... }`; the VTF
`materials/Metal/metalchrome002.vtf` is 87,648 B in `vpk:tf2_textures_dir.vpk`.

Three shapes occur, and they need different answers:

| wrapper | include says | answer |
| --- | --- | --- |
| METALCHROME002 (8 rows), FLOOR_TILE_004a/b (6), CONCRETE (extras) | plain `$basetexture` | follow the include, draw it |
| GLASSWINDOW001A (4 rows) | `$basetexture` **plus** `$basetexturetransform "center .5 .5 scale 4 4 rotate 45 translate 0 0"` | refuse **by name** (`basetexture-transform`), never draw it axis-aligned |
| WATER_WELL (2 rows) | no `$basetexture` at all (`Water` shader) | keep refusing (`no-basetexture`) |

## 3. The mechanism in the product

- `native/src/vmt_material.cpp:89` `VmtParser::parse` read only the root object;
  `material.baseTexture = scalar(root, "$basetexture")` at `:100`. The one
  existing special case was the eyes_dx8 fallback (`:106-111`). No include
  handling existed anywhere.
- A refused material is absent from `tileByMaterial`, so the world vertex build
  (`native/src/native_renderer.cpp:2279-2294`) gives it vertex alpha 0 and hands it
  `untiledBatch`; the untiled branch (`:2420-2431`) paints it a per-material grey
  (`0.78 + hash%23/100`) and the pixel shader's
  `lerp(colour.rgb, sampled*colour.rgb, colour.a)` draws a flat shade.
- Consequence: the map's own floor/metal/glass drew as flat grey slabs, and the
  refusal counter (`vmtNoBase`) counted them as unreadable.

## 4. The fix (four product files, two gate files)

`native/include/vmt_material.h` 3,637 -> 5,593 B / 113 lines:
`struct VmtPatchReading { bool declared; std::string includePath, includeResource;
bool resolved; std::vector<std::string> replaced, objectBlocks; }`, fields
`baseTextureTransform` / `baseTextureTransformText`, and
`using IncludeReader = std::function<std::vector<std::uint8_t>(const std::string&)>`
with a 3-argument `parse(text, material, reader)`. The 2-argument form keeps its
old meaning: a patch is *refused by name*, never read as its own empty root.

`native/src/vmt_material.cpp` 9,077 -> 15,689 B / 327 lines: the old extraction
body moved verbatim into `extractFields()`; new `VmtNode` / `loadNode()` /
`resolveNode()` (recursive, depth limit 4, overlays only the scalar keys of
`replace`, records object blocks) and `textureTransformIsIdentity()`. The resolved
node carries the **include's** shader, so `patch` never becomes a material.shader.
Named failure texts: "patch include chain is deeper than 4", "patch declares no
include", "patch include cannot be read: no store was given", "patch include is
missing: <resource>", "patch include did not parse: <resource>: <error>".

`native/src/main.cpp` 647,025 -> 650,359 B / 10,886 lines: counters
`fetchPatchDeclared/Resolved/Unparsable` and `fetchTransformRefused`; a
`readInclude` lambda over the same `assetChain` the VMT/VTF lookups use; new chain
reasons `patch-unresolved` and `basetexture-transform` with one trace line each
naming material, include and cause; the `gpu: world material-fetch` line gains
` patchDeclared=%zu patchResolved=%zu patchUnparsable=%zu transformRefused=%zu`;
new `--world-refused-mask` flag.

`native/include/native_renderer.h` + `native/src/native_renderer.cpp`: 
`setWorldRefusedMask()` paints the untiled branch magenta (alpha stays 0, so the
pixel shader still takes the flat branch) — this turns "refused" from a
triangle-weighted count into a countable number of pixels.

Gate files: `world-material-face-check.sh` 24,680 -> 29,649 B / 457 -> 534 lines
(new section 2b, mutant M5, verdict now counts its own reds) and
`material-coverage-check.sh` 700 -> 831 lines (re-pin, new section 8/8, mutant m7).

## 5. Evidence chain: declaration -> parse -> tile row -> pixels

**pl_odyssey** (demo `autorecord_2026-07-02_13-26-46.dem`, tick 2000, 1264x681):

    before  distinct=108 tiled=85  vmtNoBase=20 ... fromPakfile=85 downscaled=70
    after   distinct=108 tiled=99  vmtNoBase=2  ... fromPakfile=85 downscaled=76
            patchDeclared=19 patchResolved=19 patchUnparsable=0 transformRefused=4

99 + 3 (vmt-missing) + 2 (no-basetexture) + 4 (transform) = 108. The 14 materials
that only exist through an include are now tile rows with real sizes (e.g.
`metalchrome002_-2960_3352_-1088 tris=56 batch=1 tile=2 size=256x256 reason=tiled`,
`floor_tile_004a_-2352_3488_-896 tris=37 tile=6 size=512x512`).

**cp_snakewater_final1** (demo `bb841c6d...cp_snakewater_final1.dem`, tick 2000):

    after   distinct=148 tiled=123 vmtMissing=4 vmtNoBase=4 vtfMissing=1
            vtfTooLarge=112 fromPakfile=32 downscaled=112 blendFirst=3
            patchDeclared=30 patchResolved=30 patchUnparsable=0 transformRefused=16

123 + 4 + 4 + 1 + 16 = 148. Exactly **11 new** tiled rows against the committed
`evidence/material-coverage/tile-table-cp_snakewater_final1.txt` (112 names) and
**zero lost** — all 11 are the map's own wrappers
(`concrete/concretefloor006_-1479_2397_-64`, `metal/metalchrome001_-3223_2571_-16`,
`tile/floor_tile_004a_5934_-1624_-24`, `tile/floor_tile_005a_-2444_2301_152`, ...),
all batch 1, 203 triangles, 167 visible.

**Pixels.** Pose `5934 -1624 96 89 0` looks straight down at the include-only
wrapper `maps/cp_snakewater_final1/tile/floor_tile_004a_5934_-1624_-24`: the
product's own `--world-face-trace` resolves all 28 sampled points to it, so an
albedo capture of that frame is a direct reading of that one material.

| build | albedo distinct colours | modal colour | modal share |
| --- | --- | --- | --- |
| p0 `ea97815` (no include support) | **1** | (245,245,245) | 1.0000 |
| T59 | 169 (one-off run) / 236 (gate run) | (62,53,49) | 0.2350 / 0.2751 |

Textured view at the same pose: all 860,784 pixels differ between the two builds
(`60de0a90...` vs `c9a8b031...`, both SHA-256 of the BMP).

The `--world-refused-mask` arm localises refused materials: at 1264x681 the five
poses tried around the transform-refused glass paint 101,407 / 54,403 / 108,778 /
50,564 / 333 magenta pixels, and the g1 frame's own face trace agrees (3 of 28
points untiled with `reason=basetexture-transform`, 10.7% of points, 11.8% of
pixels). At the acceptance camera the same flag paints 0 px: no refused material
is in that frustum, which is why the *pixel* proof for this fix is built on the
snakewater pose above rather than on the acceptance frame.

## 6. What is still refused, and why that is correct

**pl_odyssey**, by pvs-visible triangles:

| reason | materials | visible triangles | named cause |
| --- | --- | --- | --- |
| `no-basetexture` | 2 | 300 | `water_well_-1120_-8880_312`, `water_well_beneath.vmt` — the `Water` shader has no base texture |
| `basetexture-transform` | 4 | 160 | `glasswindow001a` wrappers asking `scale 4 4 rotate 45` |
| `vmt-missing` | 3 | 144 | `METAL/METALRAIL013C` (74 tris), `METALGRATE013A` (66), `PROPS/HAZARDSTRIP001A` (4) — genuinely absent from the install |

Implementing `$basetexturetransform` was **not** done and must not be faked: a
rotated/scaled glass texture drawn axis-aligned is a wrong picture. The refusal is
named, counted and traced instead, so the next wave has a target.

## 7. Gates

### material-coverage-check.sh (cp_snakewater_final1) — re-pinned, not widened

Re-pin 2026-10-25, all with attribution:

| reading | old | new | why |
| --- | --- | --- | --- |
| materials tiled (product) | 112 | 123 | T59: 11 include-only wrappers resolved |
| tile table rows | 112 | 123 | same |
| tiles in batch 1 | 48 | 59 | same |
| triangles drawn | 189,128 | 193,508 | same |
| control refused / budget rows | 48 | 59 | same |
| `WOOD/WOOD_WALL007A` visible triangles | >= 100 | >= 67 | **already false on `ea97815`** (measured 67) — not T59 |
| `$ssbump` reason matcher | `*SSBump*` | `*ssbump*\|*SSBump*` | **already false on `ea97815`**: the probe prints lowercase |
| control-frame pixel delta | expected >= 5000 | expected == 0 | **already false on `ea97815`**: the control frame equals the full frame byte for byte (59 refused materials, 0 of them owning a pixel at that pose). The old assertion was measuring a dead premise; it is replaced by a reading (0) plus a live diff (blend-off moves 104,024 px, worst channel delta 34) so the diff code cannot be vacuous. |

The probe-side pin `plan.tiled = 112` is deliberately **left at 112**: the probe's
fetch model does not follow patch includes; only the product's fetch reports 123.
A comment in the gate says the two numbers are asserted separately on purpose.

New section 8/8 (the include pose) asserts: rc 0, 28 face-trace points, all 28
resolving to the include-only material, >= 100 distinct albedo colours, modal share
<= 500,000 ppm. The bounds sit between the two measured readings (1 colour vs
169/236), not around either one.

Seven mutants (m1 budget, m2 half-texel, m3 weapon range, m4 `$bumpmap2`, m5
triangle index, m6 regroup, m7 refused patch wrapper) — m7 is the one that says the
new section is not decoration: refusing the wrapper leaves every pre-T59 count
green and only the picture goes flat. See section 9 for the run, including the m6
anchor that had to be moved because its subject had left the frame.

### world-material-face-check.sh (pl_odyssey) — new section 2b

Asserts `patchDeclared > 0`, `patchResolved == patchDeclared`, `patchUnparsable ==
0`, `transformRefused == 4`, one transform-refused trace line per refusal, each
one printing `rotate 45`, 14 include-only tile rows, and that the two
`water_well` refusals are still `no-basetexture`. Mutant M5 re-breaks the include
resolution. The verdict sentence now **counts** its own reds (it said "four" while
five ran) and fails if fewer than five went red.

## 8. Not done / limits of this wave

- `$basetexturetransform` is refused, not implemented (see §6).
- The `material_coverage_probe` still models the fetch without includes; it is the
  authority for planner arithmetic only, the product trace is the authority for
  the fetch. Documented in the gate.
- The include fix is measured on two maps and their own paks; koth_bagel_rc13 has no
  BSP on this install (T56) so it cannot be a third.
- The odyssey acceptance frame contains no refused material, so the pixel proof of
  this fix lives on the snakewater pose (§5); the odyssey frame is unchanged by
  design and that is asserted by the face gate, not by a SHA pin.
- Frame SHAs pinned by other gates (`world-geometry-hole-check.sh`,
  `game-event-check.sh`, `weapon-body-check.sh`, `weapon-material-check.sh`) are
  re-checked on the integrated tree, not here; if the world pixels moved they must
  be re-pinned with attribution, never to make a chain green.

## 9. Mutation suite: seven defects, seven named reds

Run on this tree in one lock block, owner `t59-cov2` (first attempt `t59-covmut`,
19:40:28-19:54:15 aborted at m6; re-anchored and re-run 20:09-20:22). The forward
section re-ran first and stayed at **87 OK / 0 FAIL**. Mutation verdict:

`~
MUT-RC=0   MUTATION-CAUGHT=PASS x7   MUTATION-CAUGHT=FAIL x0
`~

| # | mutation | the reading that went red |
| --- | --- | --- |
| m1 | the atlas budget cut to one batch (the old 64-tile ceiling, rebuilt) | `tiled=64 refused=59` while the forward run read `123/0` |
| m2 | `worldTileCoord` loses its half-texel (every tile samples its edge) | probe self-test `rc=1`, `uv.origin=0.001946` against `0.002918` |
| m3 | a weapon range binds the world atlas at t0 | `weaponDrawsWithWorldAtlas=2` while the forward run read `0` |
| m4 | `$bumpmap2` is ignored | `declared=0` after the mutation, `1` before |
| m5 | the index buffer is built from triangle numbers | `tiled=123 batches=2` unchanged, the frame moved |
| m6 | the clip pushes every surviving index into batch 0 | include-pose frame moved: `pixels=860784 distinct=313 modal=0,0,0 modal_share_ppm=677541` (baseline `distinct=236 modal=62,53,49 share=0.2751`) |
| m7 | a `patch` wrapper is refused instead of followed | `distinct=1`, one colour covers `1000000 ppm` of the frame |

Every mutant restored its source byte-identically (`RESTORE=OK`) and the restored
build reproduced the baseline frame; the suite's own final sentence names all seven.

### The m6 anchor that had to move, and why that is not a loosened gate

The first run failed at m6 with "the frame is unchanged, so the regroup is not what
draws it". Measured cause: m6 compared the acceptance frame (tick 0, no camera
override), and **at that pose batch 1 owns zero pixels** -- the gate's own control
already proved it, since refusing all 59 batch-1 materials leaves that frame
byte-identical, and it did so on `ea97815` before T59 as well (the same drift that
took `WOOD/WOOD_WALL007A` from ">= 100" to 67 visible triangles). Moving batch-1
indices into batch 0 therefore could not move a pixel: the mutant was vacuous, and
a vacuous mutant is indistinguishable from a pass in the log.

m6 now runs at section 8's include pose (5934 -1624 96 89 0), where all 28
face-trace points resolve to a single include-only batch-1 material that fills the
screen, and it compares against that section's own frame. Pre-checked by hand
before the gate was touched (patch -> build -> capture -> `M6-PRE=CHANGED`,
`distinct 236 -> 313`, `modal (62,53,49) 0.2751 -> (0,0,0) 0.6775`, plan still
`tiled=123 batches=2`; restore -> source hash equal to the backup, rebuild clean).
No threshold was widened: the mutant still has to move the frame, and it now does so
at a pose where the mechanism has a subject.

## 10. Review response (T59b): one blocker and three majors

An independent adversarial review of `788f95c` was run on the merged tree
(`D:/TF2_Native_Worktrees/t59-review/REVIEW.md`, 297 lines: 1 blocker, 3 major,
8 minor, 7 nit). Every finding below was re-checked here before it was acted on,
and two of this report's own sentences are retracted.

### 10.1 BLOCKER -- the map material was never marked valid (fixed)

`788f95c` moved the body of `VmtParser::parse` into `extractFields`. The moved
body kept its closing

```cpp
  return true;
```

and lost the line above it, `material.valid = true;` (it is
`788f95c^:native/src/vmt_material.cpp:172`). `VmtMaterial::valid` therefore stayed at its
default `false` for every material the product parsed. The field has exactly one
reader, `native/src/main.cpp`'s worldspawn-material branch:

```cpp
if (!mapVmt.empty() && mapMaterial.valid) { ... resolve base texture ... }
```

With `valid` false the base texture was never resolved, `mapWidth/mapHeight` stayed
0, and the single-material upload path drew the flat fallback while **every
counter and every gate stayed green** -- all gates take the atlas overload, which
never reads this field.

Measured, odyssey tick 2000, acceptance camera, window title:

| tree | title reading |
| --- | --- |
| `047bcfb` (before) | `mapmat: WOOD/WALL007A, deps: base2=none bump=none, maptex: fallback]` |
| `480cd20` (after) | `mapmat: WOOD/WALL007A, deps: base2=none bump=none, maptex: 1024x1024]` |

The fix restores the assignment at the end of `extractFields` with a comment naming
the commit that dropped it and the single line that reads it, and the reading is
now a gate assertion (`OK the map material resolves a base texture (1024x1024)`). The
inverse mutation M6 sets `material.valid = false` and requires the title to fall back,
so the assertion is proven load-bearing rather than merely true.

### 10.2 MAJOR -- the wrapper assertion was a tautology (fixed)

`assert_eq "patch wrappers resolved through their include" $PATCHRES $PATCHDECL`
compared a counter with the value it had just been incremented to. `patchResolved`
is incremented when the include is read, before the transform check and before
tiling, so a wrapper that resolved and was then refused had no recorded outcome
at all -- three of the odyssey wrappers were in exactly that state and nothing
could see it.

The product now prints one ledger line per declared wrapper:

```
gpu: world patch material=<name> outcome=<tiled|basetexture-transform|
  patch-unresolved|no-basetexture|not-reached> tiled=<0|1> visible=<n>
```

and the gate asserts the closure instead: one line per declared wrapper, each
outcome one of the four named ones, and

tiled + transform + unresolved + no-basetexture + other == declared.

Measured on this tree, odyssey (19 declared wrappers):

| outcome | lines |
| --- | --- |
| `tiled` | 14 |
| `basetexture-transform` | 4 |
| `no-basetexture` (the water wrapper, whose include is the Water shader) | 1 |
| `patch-unresolved` | 0 |
| `not-reached` | 0 |

### 10.3 MAJOR -- `--world-refused-mask` covered only one of the two paths (fixed)

The mask was applied in the atlas vertex setup only. The single-material vertex
setup (used by the sky pass and by the fallback) has the same shade expression
without it, so a fallback run painted 0 px whether or not the flag worked. The
mask is now applied on both paths, and this report's earlier sentence -- "0 px
at the acceptance pose" -- is **retracted**: it could not distinguish "nothing to
paint" from "the flag is not wired on this path". What the mask readings in §5
do establish is that the flag paints refused geometry when refused geometry is
on screen (101,407 / 54,403 / 108,778 / 50,564 / 333 px at the five survey poses).

### 10.4 MAJOR -- the wrapper arithmetic in §5 did not close (retracted, corrected)

§5 said odyssey ships 20 wrappers and split them 14 + 4 + 2. The correct split is
**14 + 4 + 1 = 19**: `water/water_well_beneath.vmt` is an ordinary `vpk-volume` material,
not a wrapper; the second water refusal is the pak wrapper
`water/water_well_-1120_-8880_312`, whose include resolves to the Water shader,
which has no `$basetexture` by design. The §2 sentence that counted WATER_WELL twice is
corrected by this paragraph; the fetch partition itself was right
(`99 + 3 + 2 + 0 + 4 = 108`).

### 10.5 Minors: acted on, or deliberately not

- **Object-valued `$basetexturetransform` drawn axis-aligned -- fixed.** Both
  `$basetexturetransform` and `$basetexturetransform2` now go through one refusal helper, and an
  object-valued key is refused as an object block instead of being ignored.
- **One event counted in two counters** (`fetchVmtNoBase` and
  `fetchPatchUnparsable`) -- **kept, with the reason**: the partition in the
  material-fetch line has seven reason classes and `vmtNoBase` covers both textual
  failures; `patchUnparsable` is a sub-annotation. No material on either map reaches
  that branch (odyssey `patchUnparsable=0`, snakewater `patchUnparsable=0`), so the
  ambiguity is not load-bearing here.
- **The gate identified the 14 include-only rows by name substring** -- the closure
  now proves the 14 from the ledger sum, and the coverage gate's include pose
  asserts how many distinct materials its 28 points name (one), so the number is
  no longer carried by a substring search alone.
- **The 169-vs-236 distinct-colour spread at the include pose is not attributed**
  (the one-off capture and the gate capture differ in exactly one respect that was
  not recorded: the gate's capture renders the viewmodel companion). Both readings
  are two orders of magnitude away from the refused state (1 colour, modal share
  1.0000), which is what the bound is for; the bound is one-sided on purpose and
  no attempt is made to pin the exact colour count.

### 10.6 What the review confirmed as sound

Failed patches cannot fall back to their wrapper; the recursion is bounded
(`kMaxPatchDepth = 4`, checked before the read); scalar transform identity
detection is correct and caught `rotate 45` on all four odyssey glass wrappers; the
mask cannot change pixels when disabled; the M6 pixel mutant's re-anchor is
legitimate; `verify-all.sh` pins `COV_OK=87`; encoding is LF/no BOM/no stray CR;
and both fetch partitions close (odyssey `99+3+2+0+4=108`, snakewater
`123+4+4+1+16=148`).
