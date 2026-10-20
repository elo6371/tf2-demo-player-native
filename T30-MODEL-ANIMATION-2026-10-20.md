# T30 — Real model, animation and companion robustness

**Task:** `NEXT-TASKS-2026-10-20.md` T30, shared task board `task-2`, owner `t30-model`.

## 0. Verdict

`VIEWMODEL-ROBUSTNESS=PASS (203 assertions, 0 failures)` — `bash
viewmodel-robustness-check.sh`, rc=0. The three real model classes either render
(the single-range and multi-range bodies: product request ok, mesh ready, their
own VTF keys, one draw call, pixels on screen) or are refused by name (a missing
VVD/VTX companion), and two further classes are named readings: a malformed
LOD-0 descriptor is refused, an absent LOD-0 is a reported LOD fallback, and an
absent animation companion is a reported degradation.

MUTATION-VERDICT-PLACEHOLDER

The three real inputs T30 asked for are covered by one deterministic probe and
one gate:

| class | real input | reading | outcome |
|---|---|---|---|
| single-range | `c_rocketlauncher.mdl` (soldier) | 1 LOD-0 range, 20,748 vertices, slot 0 -> `c_rocketlauncher.vtf`, 4 bones, 10 attachments, 4 declared LODs | renders (probe + product body request + frame) |
| multi-range, different VTFs | `c_syringegun.mdl` (medic) | 2 LOD-0 ranges, slots 0/1 -> `c_syringegun.vtf` (24,279 verts, 4 bones) and `c_syringegun_glass.vtf` (864 verts), 3 attachments, 4 LODs | renders; forced-slot frames partition the body |
| damaged companion | a REAL model's own bytes materialised loose with one companion omitted | `.vvd` absent, `.dx90.vtx` absent, animation companion absent | named refusal for the resource companions (`viewmodel companions are missing`); the animation companion is a NAMED DEGRADATION, not a refusal (see §3.3) |

Two further classes the task named are covered as readings, not as opinions:

- **LOD-0 refusal / fallback.** A malformed LOD-0 descriptor is refused by name
  (`descriptor 0 (bodyPart 0 model 0 mesh 0 lod 0) was refused: descriptor is not
  a bounded triangle list`). A LOD-0 set that is *absent* is not a refusal: the
  loader selects the next declared LOD and the probe reports `targetLod=1`,
  `lodFallback=1`, one descriptor, 13,476 indices. That is a newly named fallback,
  and it is reported separately from the companion degradation.
- **Companion animation binding.** Every one of the nine shipped `c_*_arms.mdl`
  models adopts a separate `*_animations.mdl` companion through the model's own
  include list (measured: 10 of 354 renderable models do; the nine arms models are
  nine of them). `c_sniper_arms.mdl` binds 38 of its 43 sequences by name from
  `c_sniper_animations.mdl` (88 animations), with 58/58 bind matrices.

## 1. Repository state

| item | value |
|---|---|
| worktree | `D:/TF2_Native_Worktrees/t30-model-animation` |
| branch | `task/t30-model-animation` |
| base commit | `36303c2` |
| product commits | `909a253` (probe + gate + sweep script), `a08d4ea` (fixture-root markers, post-fixture LOD view, real input pins, gate nounset defect), `PLACEHOLDER` (evidence), `PLACEHOLDER` (this report) |
| push status | not pushed; no remote command was run |

## 2. The installed inventory, measured in this worktree

`model-inventory-sweep.sh` sweeps every model the install ships — 326 `c_` weapon
models + 37 `v_` viewmodels, 363 in total — and writes one TSV row per model with
the probe's whole summary line. It is a committed script so the evidence is
reproducible; the gate does not re-run it (a 363-model sweep inside every
forward/mutation run would cost minutes and buy nothing).

```
swept 363 models, 9 with a refusal
single-range=186  multi-range-distinct-textures=148
renderable (rc=0): 354   refused (rc=1): 9   selected-LOD fallback: 0
models with a separate animation source: 10 (the nine c_*_arms + one more)
```

The nine refusals are the nine `c_*_animations.mdl` companion files, which live in
the `c_models` directory and were swept as if they were models: they carry no VVD
or VTX of their own and the loader correctly refuses them. That is the sweep
naming a class of files rather than a defect, and it is recorded here so the
number is not mistaken for nine broken models.

The nine arms models, through the same probe (the range/slot/companion table T30
step 1 asks for):

| model | ranges | distinct VTFs | bones | attachments | animation source | bound sequences |
|---|---|---|---|---|---|---|
| c_scout_arms.mdl | 1 | 1 | 58 | 11 | c_scout_animations.mdl (103 anims) | 43/46 |
| c_soldier_arms.mdl | 3 | 3 | 67 | 15 | c_soldier_animations.mdl (109) | 53/54 |
| c_pyro_arms.mdl | 3 | 2 | 58 | 11 | c_pyro_animations.mdl (98) | 30/31 |
| c_demo_arms.mdl | 2 | 2 | 65 | 20 | c_demo_animations.mdl (85) | 38/39 |
| c_heavy_arms.mdl | 2 | **1** | 60 | 21 | c_heavy_animations.mdl (92) | 41/42 |
| c_engineer_arms.mdl | 4 | 3 | 68 | 10 | c_engineer_animations.mdl (110) | 53/54 |
| c_medic_arms.mdl | 2 | 2 | 54 | 12 | c_medic_animations.mdl (62) | 24/25 |
| c_sniper_arms.mdl | 2 | 2 | 58 | 8 | c_sniper_animations.mdl (88) | 38/43 |
| c_spy_arms.mdl | 1 | 1 | 61 | 14 | c_spy_animations.mdl (115) | 58/59 |

Two readings in that table are the reason the chosen inputs are what they are:
`c_heavy_arms` has two ranges on **one** texture (so "two ranges" does not imply
"two textures"), and the pyro/engineer arms have more ranges than distinct
textures. The multi-range input had to be selected by the *distinct VTF* reading,
not by the range count.

## 3. The three real inputs

### 3.1 Single-range: `models/weapons/c_models/c_rocketlauncher/c_rocketlauncher.mdl`

One LOD-0 descriptor, one range, one texture slot, four declared LODs, ten
attachments. Probe: `targetDesc=1 targetIdx=20748 meshRanges=1
distinctRangeTextures=1`, `vtfStatus=resolved`, `badWeights=0`, `maxBone=2 < 4`,
`animationInput=ok` (the model's own table). The product is given the same path as
its weapon-body override and reports one range, one bound slot, one VTF key, one
draw call, and a captured frame.

### 3.2 Multi-range with different per-range VTFs:
`models/weapons/c_models/c_syringegun/c_syringegun.mdl`

Two LOD-0 descriptors, two ranges on slots 0/1: the body (24,279 vertices,
`materials/.../c_syringegun.vtf`) and the glass (864 vertices,
`materials/.../c_syringegun_glass.vtf`). Four bones, three attachments, four LODs,
both VTFs resolved, zero bad weights. This is the non-sniper model with a different
texture per range that T30 asked for, and it is not the pinned scattergun.
The forced-slot frames partition its coverage (§6); the forced change is measured
in pixels, not inferred from the keys.

### 3.3 Damaged companion: a real model with one companion omitted

The shipped install has no model with an incomplete companion set (the
material/fixture work already established that for viewmodels, and the 363-model
sweep found no model whose MDL resolved without VVD/VTX). So the damaged input is
made by taking a REAL model's own bytes — `v_scattergun_scout.mdl` and
`c_sniper_arms.mdl` — materialising them loose through the probe's new
`--materialize` mode, and omitting one file. The bytes are the shipped model's;
only the set is damaged. This is the same route the product takes for a loose
model, so the refusal/fallback readings are the product's own sentences.

| fixture | omitted | probe | product |
|---|---|---|---|
| vm-ok | — | `found-loose`, `companions=1`, mesh built, 2 ranges | request `status=ok`, `meshReady=1`, resourceStatus=ok, drawn |
| vm-no-vvd | `.vvd` | `companions=0`, `vvd=0`, mesh refused with a named reason | request `status=failed`, resourceStatus `incomplete(...,vvd=0,...)`, reason `viewmodel companions are missing`, no range table, nothing drawn |
| vm-no-vtx | `.dx90.vtx` | `companions=0`, `vtx=0` | same named refusal, resourceStatus `incomplete(...,vtx=0,...)` |
| arms-ok | — | animation companion adopted, 58/58 bind matrices | (probe-level input for the animation read) |
| arms-no-anim | declared `c_sniper_animations.mdl` | `separateAnimSource=0`, `animSeqs=1`, `boundSeqs=1`, mesh still builds | named **degradation** to the base model's stub table |

The two outcome classes are deliberately kept apart:

- A missing VVD/VTX companion is a **named refusal** of the whole request.
- A missing animation companion is a **named degradation**: T6's route-1 rule
  treats a model whose own table carries any animation as self-animating, so the
  companion is an optional improvement. The readings change from
  `separateAnimSource=1 / animSeqs=88 / boundSeqs=38` to
  `separateAnimSource=0 / animSeqs=1 / boundSeqs=1` while the mesh still builds.
  The product would therefore still draw a stub pose in this state. That is
  recorded as a limitation in §10, not rewritten as a pass.

### 3.4 LOD-0: refusal vs fallback

| fixture | reading | class |
|---|---|---|
| control | `targetLod=0 lodFallback=0 lodCount=4 targetDesc=1` | normal selection |
| `descriptor-not-triangles` (LOD-0 descriptor's index count +1) | `meshBuilt=0`, refusal `descriptor 0 (... lod 0) was refused: descriptor is not a bounded triangle list; the mesh is not built from a partial range set` | named refusal, whole mesh |
| `lod0-absent` (all LOD-0 descriptors removed) | `targetLod=1 lodFallback=1 targetDesc=1 targetIdx=13476 meshBuilt=1` | named fallback to LOD 1 |

No shipped model lacks LOD 0 (363/363 select LOD 0), so the fallback class is
reachable only through the fixture; the gate states that rather than implying the
install contains one.

## 4. What changed

| file | change and why |
|---|---|
| `native/tools/model_robustness_probe.cpp` | new. One deterministic line per real model: selected LOD + fallback, range table with material slot, material name, resolved VTF and per-range bone coverage, companion presence from the same `ModelLoader::resolveAsset` the product uses, animation source/status from the same `AnimationBindingCache` the product uses, and the named refusals. Two fixture families: the four LOD-0 descriptor defects (shared vocabulary with `viewmodel_range_probe`) and `lod0-absent`; plus `--materialize`, which writes a real model's MDL/VVD/ every VTX variant + declared animation companion into a loose fixture root (with the `.gitkeep` markers `AssetRoot::valid()` requires) so a gate can build a damaged input from real bytes. |
| `native/CMakeLists.txt` | new `model_robustness_probe` target, linking the loader, the animation binding and the same material reader the draw pass uses. No renderer, no device. |
| `viewmodel-robustness-check.sh` | new focused gate, 6 sections: inventory, the three inputs + damaged companion + LOD classes, the product's own lines on the pinned recording, pixel masks/contact/partition, four mutations, evidence. |
| `model-inventory-sweep.sh` | new. Full 363-model sweep; writes `evidence/t30/inventory.tsv` and the class list. |
| `evidence/t30/` | inventory (full sweep + curated), gate summary, three frozen frames, one inspection PNG. |
| `T30-MODEL-ANIMATION-2026-10-20.md` | this report. |

**No product rendering code was touched.** The probe and the gate are additive.
The only behavioural change is the probe itself; the product, the loader, the
animation binding and the ViewModel pass are byte-identical to `36303c2`.

## 5. Commands and exit codes

All build/launch/capture/mutation sequences ran inside
`bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t30 ...`.

```
```
bash build-target.sh model_robustness_probe tf2_demo_native vpk_query
  rc=0     (fresh tree, 2m10s; warnings only from pre-existing files:
            main.cpp C4244 x5 + C4457; native_renderer.cpp C4267 x2 + C4456 x2.
            The new probe and its CMake target produced no warning.)
bash model-inventory-sweep.sh
  rc=0     363 models swept: single-range=186, multi-range-distinct-textures=148,
            9 refusals (the nine *_animations.mdl companions)
bash viewmodel-robustness-check.sh
  rc=1     first attempt: 185 assertions held, 14 failed -- all 14 were gate
            instrumentation (the "weaponBody" pattern matched the follow-up
            weaponBodyMaterial line; a contact threshold assumed the whole-arms
            mask is mostly on the weapon; the syringegun's glass range is
            alpha-blended so its forced-slot partition is not pixel-exact).
            Fixed in 9c8f120.
bash viewmodel-robustness-check.sh
  rc=0     VIEWMODEL-ROBUSTNESS=PASS (203 assertions, 0 failures)
           host_overlap=0, probe 85226039..., executable c8bd9337...
```
```

## 6. Forward gate results

Six sections, 203 assertions, none failed. The readings this report depends on
are in `evidence/t30/summary.txt`; the numbers are:

**Product contract on the pinned recording** (hidden window, paused at playback
tick 0, audio disabled, pose sequence 1 overridden over the entity's own 29 and
reported as `sequenceOverride=1`):

| reading | bind | applied |
|---|---|---|
| `drawCalls` | 1 | 1 |
| `ranges` / `boundSlots` | 2 / 2 | 2 / 2 |
| `skinning` / `vmAnimApplied` | 0 / 0 | 1 / 1 |
| `boneTableCoverage` | 0/0 | 58/58 |
| `sequenceInRange` / `bindPose` | 1 / 0 | 1 / 0 |
| `drawnModel` | `c_sniper_arms.mdl` | same (entity's own model) |

The camera line is **byte-identical** between bind and applied:
`eye=(-0.09,0.22,0.70) forward=(-0.354,0.891,0.283) placement=(-31.70,55.50,12.75)
fov=90.00 near=1.4940 far=185.2539 distance=65.7352 radius=29.8797
hand=(1,0,0,0,0,1,0,0,0,0,1,0) attachment=weapon_bone`, so the frame difference
below is a pose difference and not a framing difference. The applied frame repeats
byte for byte (`45dd7bea…`), and the CPU pose extent is
`vertices=9996 posedVertices=9996 nonFinite=0 maxDisplacement=6.126 centerShift=0.009
valid=1` — the pose moves geometry by 6.126 units of a 29.88-unit model and does
not move the framing anchor.

**The three real inputs, in the product's own words and pixels:**

| input | product readings | pixels |
|---|---|---|
| single-range body (`c_rocketlauncher`) | `ranges=1 texturedRanges=1 boundSlots=1`, key `…/c_rocketlauncher.vtf`, `weaponDrawCalls=1 weaponVisible=1`, `override=1`, 20,748 vertices | body covers 10,698 px |
| multi-range body (`c_syringegun`) | `ranges=2 texturedRanges=2 boundSlots=2`, keys `…/c_syringegun.vtf,…/c_syringegun_glass.vtf`, `weaponDrawCalls=1 weaponVisible=1`, `override=1`, 25,143 vertices | natural body 4,991 px; forced slot 0 changes 577 px, forced slot 1 changes 2,481 px, union 2,494, overlap 564 |
| damaged companion fixtures | complete: request `ok`, `meshReady=1`, resourceStatus=ok, drawn; VVD omitted / VTX omitted: request `failed`, resourceStatus `incomplete(…,vvd=0,…)` / `incomplete(…,vtx=0,…)`, reason `viewmodel companions are missing`, no range table, nothing drawn | complete frame `6262ce7f…` vs refusal frame `803ff052…` (refusals of VVD and VTX are byte-identical to each other, as they should be) |

**The ViewModel's own coverage and the pose**: coverage 25,601 px (bbox 268×284);
the pose changes 10,097 px of which 10,095 are inside the coverage and **2** are
outside (the gate's documented tolerance is 64; the tool line is kept verbatim in
the evidence, where its own `status=FAIL` is the zero-tolerance display). The
body contributes 4,991 px and the arms 20,998 px, both fully inside the composite.

**Contact** (`bmp-mask-contact.py`, arms mask vs weapon-body mask, radius 8):
bind `maskPixels=20998 near=1480 nearFraction=0.0705 contactDistance=0`; applied
`maskPixels=19646 near=1364 nearFraction=0.0694 contactDistance=0`. The pose keeps
98.4 % of the bind contact and the masks still touch (distance 0). The fraction is
small because the mask is the whole arms, not the hand; a detached hand would
drive `contactDistance` to `-1` (none within 8 px) and the retained share to 0,
which is the reading the gate bounds.

**The multi-range partition, stated honestly**: the syringegun's glass range is
alpha-blended over the body, so unlike T22's opaque two-range models the forced
slot frames are not pixel-disjoint: 577 + 2,481 = 3,058 changes against a 2,494-px
union, i.e. 564 px changed by both. The gate asserts both ranges change pixels,
that the changes stay on the body (outside 2 and 20, bounded by 64), that the
overlap computed two ways is the same number, and that the overlap is under a
third of the union. The keys list is what proves the two different VTFs are bound
per range; the pixels prove both ranges are actually drawn.

**Frame hashes** (2,582,406 bytes each, 1264×681): bind `49604d9d…`, applied
`45dd7bea…` (repeat identical), world-only `04e32a13…`, multi-range body
`52f560c4…`. Frozen frames: `evidence/t30/bind-pose.bmp`,
`applied-pose.bmp`, `weapon-body-multi-range.bmp`, plus
`inspect-applied.png`.

## 7. Mutation suite

Four defects, one at a time, each applied to exactly one source file, built, measured,
then restored by byte copy and proven by `cmp` + sha256 before the next one. A
mutation counts only when (a) the defect *reproduced* (a reading took the defect's
value) and (b) no unrelated assertion broke. After the last restoration the gate
rebuilds and re-draws the applied frame, which must reproduce the baseline hash,
and re-probes the damaged companion fixture, which must return its pinned reading.

| # | defect | witness the gate pins |
|---|---|---|
| m1 | the pass draws the FIRST range's geometry for every range (`range.firstVertex/vertexCount` from `mesh.ranges.front()`) | the multi-range body frame hash moves; control: the range count and the vertex buffer are unchanged |
| m2 | every range reads the material slot NEXT to the one it names (`source.materialSlot + 1`) | the body's key list moves away from the natural `c_syringegun.vtf,c_syringegun_glass.vtf`; the frame hash moves; control: the range count is unchanged |
| m3 | `companionSetComplete` stops requiring the VVD | the no-VVD fixture's `companions` reading becomes 1 (pinned 0), and the product's refusal stops being the named `viewmodel companions are missing` |
| m4 | the bone table is uploaded as its own transpose (T18's defect written back into `toColumnMajor`) | pixels outside the ViewModel coverage explode past the pinned `outside <= 64`; control: `vmAnimApplied=1` and the CPU pose extent still moves geometry (the extent reading cannot see this defect — T18's lesson) |

MUTATION-RESULTS-PLACEHOLDER

## 8. Defects this round found in its own harness

Recorded because each one produced a wrong or missing reading before it was fixed:

1. **A stray quote + `set -u` killed the forward run at section 2.** An edit
   appended one extra `"` to an assertion; the quote balanced against a later
   line, so `bash -n` passed, and at run time the swallowed lines made
   `BIND_TRACE`/`LAST_RC` unbound. The first forward attempt reached section 2 and
   stopped before any product capture. Fixed, and the next run's log is the
   evidence that the whole script executes.
2. **A materialised fixture root was not a valid AssetRoot.** `AssetRoot::valid()`
   requires a non-empty file under `materials/`, `models/` and `maps/`; the
   probe's materialisation created the two directories empty, so the animation
   binding answered `model-missing / no asset root` on a fixture whose files were
   all present. The probe now writes the same `.gitkeep` markers the committed
   fixture roots carry.
3. **`lodFallback` was computed before the fixture was applied.** On the
   `lod0-absent` run the probe reported `targetLod=1` and `lodFallback=0` in the
   same line — the one number the fallback claim rests on was read from the
   pre-fixture descriptors. The whole LOD view is now recomputed after the
   fixture.
4. **`GROUPS=…` is a bash special variable.** The inventory script assigned its
   output path to `GROUPS`, which bash silently ignores (it is the group-id
   array), so the class list was written to a stray file named `197121` in the
   worktree root instead of `evidence/t30/inventory-groups.txt`. Found by the
   missing evidence file, the stray file was removed and the variable renamed.
5. **A background waiter can die with its pwsh wrapper.** The first hostlock
   v1 wait lost the acquisition race for over an hour and, when the wrapper was
   killed, left a ticket; hostlock v2/v2.1 (Lead) replaced it with a FIFO queue.
   The lesson recorded here: one waiter at a time, and check the ticket exists
   before assuming a wait is live. No lock was ever bypassed.
6. **A convenience grep pattern matched the line after the one it named.** The
   body assertions read `gpu: viewmodel weaponBody`, which also matches the
   `weaponBodyMaterial` line printed immediately after the `weaponBody` line;
   `tail -1` therefore returned the material line, which has no `status`,
   `meshReady`, `path` or `vertices` field, and eight assertions read
   `<none>` on runs whose traces plainly contained the values. Fixed by naming
   the line (`weaponBody model=`). The wrong reading was a harness bug that
   looked exactly like a product refusal, which is why the fix is recorded.
7. **A contact threshold invented instead of measured.** The first gate required
   20 % of the arms mask within 8 px of the body. The mask is the whole arms
   (sleeves included), so the real value is 7 %; a detached hand would read
   `contactDistance=-1`, not a smaller percentage. The gate now pins
   `nearFraction >= 0.05` **and** "the pose keeps at least 80 % of the bind
   contact" (measured 98.4 %), plus `contactDistance` in [0,8].
8. **A disjointness tolerance was imported from a different model.** T22's forced
   slot frames partitioned *opaque* two-range models exactly. The syringegun's
   second range is glass and alpha-blended, so 564 px change under both forced
   slots. The gate bounds the overlap (computed two ways, same number; under a
   third of the union) instead of asserting zero, and the report says which
   model property that is.

## 9. Hashes

Fixed data inputs (all verified before the gate runs them again):

| item | value |
|---|---|
| TF2 resource root | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` |
| recording | `demos/autorecord_2026-07-02_13-26-46.dem`, 21,856,667 bytes, sha256 `c8a58501d222bdd9aec6db92d2782931a29a13d963a87364d603263e6dab091e` (pinned by `evidence/viewmodel/animation-inputs.txt`) |
| map | `maps/pl_odyssey.bsp`, 131,192,482 bytes, sha256 `3e41e13207ade34d16db74d1b49d0235ae05f80d7bb1bc2abe3ad7c3b2ee190c` |
| archive directory files | `tf2_misc_dir.vpk` `b1e36819d6ae5f5f7a97831265d97f16901d8d72a218fb7742cb55e6fd92eda3`, `tf2_textures_dir.vpk` `a65f0dbe4a91847a2b8d8be0ff32f1105faf11fa3eaceb86118f2e83ecd9db7b` |
| tick | playback 0, `--start-paused`, hidden window, audio disabled |
| pose sequence | 1 (the drawn arms model's only posable base sequence; the entity's own 29 is overridden and reported) |

Source commits on `task/t30-model-animation` (base `36303c2`): `909a253` (probe,
gate, sweep script), `a08d4ea` (fixture markers, post-fixture LOD, real input
pins, lock/nounset defects), `23a2e83` (dead-lock reclaim), `782e263` (one
effect-flag set for the partition), `9c8f120` (body line patterns, contact
bound, translucent partition), then the evidence commit and this report.

Source files this round adds or changes (sha256, at the evidence commit):

| file | sha256 |
|---|---|
| `native/tools/model_robustness_probe.cpp` | `0ff3b0c7e09a577bfce2586d4093814f38cb05586806e40f002ba828256aaced` |
| `native/CMakeLists.txt` | `206539203ebfe8a6db7662faeed9b8a373c6ad962b929c90ac1be8e6689b7863` |
| `viewmodel-robustness-check.sh` | `6a9583dc179fb386e7b5438a56d6aee6d580c3efaf448ec892fa7e7bb85aac41` |
| `model-inventory-sweep.sh` | `bf6d39ffd96cc5eea87b46cd7c78f854ec4218a9d4f752f13e6c4a5181b93aa1` |
| `.gitattributes` (evidence/t30 -text) | `e6c075b368d33f49b15a481aa05d64a99236083a691e0fcb24e9fc4890743d7b` |

The product sources are **unmodified from `36303c2`**: `git diff 36303c2 --
native/src native/include` is empty on this branch.

The binaries that drew the frozen frames (identified, not reproducible — MSVC
stamps a fresh link signature, measured in T18 §7):

| item | sha256 |
|---|---|
| `model_robustness_probe.exe` (forward run) | `85226039cbd0bef936a7f7e20d4ded6938cca5008bd0140bbd56bd0649b6ad0f` |
| `tf2_demo_native.exe` (forward run) | `c8bd9337813808cffd30c8a34c330a0dd55ed9bfa009f13733883bd0d2c5c9e8` |

Frozen frames and evidence artifacts:

| item | bytes | sha256 |
|---|---|---|
| `evidence/t30/bind-pose.bmp` | 2,582,406 | `49604d9da68b2040ca54f8c683f66750ea6004f58d7c103252ee09f383228016` |
| `evidence/t30/applied-pose.bmp` | 2,582,406 | `45dd7bea557c847e14968443857742609c105b56dd7cf13b133b9900120c0ce6` |
| `evidence/t30/weapon-body-multi-range.bmp` | 2,582,406 | `52f560c4ab2d29ef4c3dbb7cde7750e0265ed3b3bb9fe18515bcdc718e47a323` |
| `evidence/t30/inventory.tsv` | 214,405 | the full 363-model sweep (`model_robustness_probe` 85226039...) |
| `evidence/t30/inventory-groups.txt` | 39,979 | class lists derived from the TSV |
| `evidence/t30/inventory.txt` | 3,396 | the gate's curated inventory readings |
| `evidence/t30/summary.txt` | 5,382 | the forward run's whole evidence block |
| `evidence/t30/gate-forward.log` | 15,261 | the forward run's log (203 assertions) |

## 10. Not verified

- **The animation companion's absence is a degradation, not a refusal.** T6's
  route-1 rule keeps the base model's stub table when the declared companion is
  absent, so the product would still draw a stub pose; the probe names the state
  (`separateAnimSource` 1→0, `animSeqs` 88→1, `boundSeqs` 38→1) but no product
  refusal exists for it. The 363-model sweep found no shipped model whose own
  table is empty *and* declares a companion, which is the input that would make
  `AnimationInputStatus::AnimSourceMissing` reachable. Cheapest next step: pick
  such a model from another game/content set, or add that one shape as a
  committed synthetic fixture (the same way `v_fixture_nocompanion` was made) and
  pin the named status.
- **LOD fallback is fixture-only here.** All 363 shipped models select LOD 0
  (`lodFallback=1` count is 0), so the LOD-1 fallback was exercised through the
  probe's `lod0-absent` fixture on a real model, not through a shipped model that
  lacks LOD 0.
- **The sibling ViewModel gates were not re-run this round**: 
  `viewmodel-animation-check.sh` (~11 min), `viewmodel-companion-check.sh`
  (~8 min), `viewmodel-range-check.sh` (~4 min), `viewmodel-presentation-check.sh`
  (~8 min), `viewmodel-material-check.sh` and `weapon-material-check.sh`. This
  round's tree changes no product code (probe/gate/sweep only), so those gates
  are expected to hold — but "expected" is not "run", and this report does not
  rewrite their old results as this tree's. Cheapest next step: one host-lock
  window running `bash viewmodel-range-check.sh` (its four assertions are the
  ones this task's inventory extends); then `viewmodel-companion-check.sh`.
- **One paused frame, one sequence.** All captures are paused at tick 0; the
  applied pose uses the arms model's only posable base sequence (1, one frame).
  A playing sequence and a per-tick animation are out of scope, as in T18/T22.
- **Three models in pixels, 363 at descriptor level.** The sweep proves range /
  LOD / VTF / companion *readings* for every shipped model; only the three chosen
  inputs (plus the fixtures) were drawn and measured in pixels.
- **The world behind the ViewModel is still the T17/T26 broken scene.** The gate
  judges the ViewModel's coverage, containment, contact and partition; it says
  nothing about the map.
- **The weapon body is the override, not the natural body.** The natural body on
  this recording is `c_pro_rifle` with two ranges on one atlas; the multi-range
  pixel claim uses `--viewmodel-weapon-model` and reports `override=1`.
- **The body does not take a sequence transform** (T22's `body_static=1`); this
  round re-observed it only as "the body's frames equal each other under the
  pose", not as a new investigation.
- **The executable is not byte-reproducible** (MSVC link signature): the frame
  hashes are reproducible for a source tree, the binary hash identifies the build
  that drew them.

## 11. Final worktree and process state

- Branch `task/t30-model-animation`: base `36303c2`, then the commits listed in
  §1; `git status --short` clean after the evidence and report commits.
- No `tf2_demo_native.exe`, probe, `cl.exe`, `link.exe`, `nmake` or `cmake`
  process started by this task is left running; `hostlock.sh status` shows the
  lock free and owned by nobody when this report is committed.
- `.scratch/t30/` holds the traces, per-run frames and logs (untracked);
  `evidence/t30/` holds the frozen artifacts this report's claims depend on.
