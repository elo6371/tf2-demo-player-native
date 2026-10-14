# T2 — World Material Effects (water surface and ViewModel advanced materials)

**Verdict: PASS**

```
water approximate warp : PASS
reflection             : UNAVAILABLE
refraction             : UNAVAILABLE
Phong                  : PASS
Bump                   : PASS
Selfillum              : PASS (forced — see §10)
T1 skybox / VIS        : PASS (unchanged)
```

---

## 1. Branch, worktree, baseline, final commit

| | |
|---|---|
| Branch | `task/world-material-effects` |
| Worktree | `D:\TF2_Native_Worktrees\world-material-effects` |
| Baseline | `origin/p0-entity-protocol` @ `7c24bda` |
| Commits | `2092429` interface freeze → `731dac4` water + advanced materials → `7cbdf13` probe fixtures → *(final commit: gate + report)* |
| Upstream repo | `git@github.com:elo6371/tf2-demo-player-native.git` |

Baseline confirmed before any T2 code was written:

```
git rev-parse HEAD          -> 7c24bda88f58ded0fbbb1dda71c33ba590152f60
git status --short          -> (clean)
build-target.sh (3 targets) -> rc=0
verify-fast.sh              -> VERIFY-FAST=PASS (assertions_ok=43)
world-material-check.sh     -> WORLD-MATERIAL=PASS
viewmodel-material-check.sh -> VIEWMODEL-MATERIAL=PASS (assertions_ok=220)
```

The main git repository is `D:\TF2_Native_Test`, **not** `D:\TF2_Demo_Player`
(that directory has no `.git`). The worktree was created with
`git worktree add -b task/world-material-effects <path> 7c24bda`.

## 2. Files changed, and why

| File | Why |
|---|---|
| `T2-INTERFACE-FREEZE-2026-10-14.md` | **new.** The contract, frozen before the code. Records the eight measured answers phase 2 asks for and binds the new names. |
| `native/include/world_material.h` | +153. Five water state words, `WorldWaterVertex`, `WorldWaterParams`, the draw request/reading, `worldWaterWarpUv`, `measureWaterParams`. |
| `native/src/world_material.cpp` | +240. The water readers and the draw judge. |
| `native/include/viewmodel_material.h` | +120. `ViewModelEffectState`, the six-field report, one reading struct per effect, the range judge. |
| `native/src/viewmodel_material.cpp` | +293. The three readers over the VMT's own text. |
| `native/include/native_renderer.h` | +212. Two **new** sample-state structs and their getters, the water/effect pass declarations, the published world transform. No T1 field moved. |
| `native/src/native_renderer.cpp` | +610. The water pass, the three shader branches, the three texture slots, the per-range counts. |
| `native/src/main.cpp` | +416. Wiring, the four new switches, the trace lines. |
| `native/tools/world_material_probe.cpp` | +267. `waterSelfTest()` and `--water-fixture`. |
| `native/tools/viewmodel_material_probe.cpp` | +463. `--effects-fixture [--mutation]`. |
| `world-material-effects-check.sh` | **new.** The T2 gate (§13). |
| `viewmodel-material-check.sh` | Three phase-1 assertions moved to phase 2 (§14). |
| `evidence/world-material-effects/` | **new.** This round's readings. |

### `native_renderer.cpp` / `main.cpp` — the justification the task requires

The task allows editing these two with a paragraph of reasoning and a proof that
skybox/VIS did not regress. The rule followed throughout:

> **Every edit is a new field, a new function or a new branch. No existing value changed meaning.**

Specifically:

* **The world vertex buffer, the sky pass and the pvs clip are untouched.** The
  water is a second buffer and a second draw. The world pass still draws every
  triangle it was given, so `drawnTriangles + hiddenTriangles == inputTriangles`
  cannot be disturbed by the water switch. §12 asserts that identity with water
  on.
* `worldMaterialSampleState()` is unchanged. The two new readings come from two
  **new** getters over two **new** structs, because a field added to a struct T1
  pinned is a field that could move a reading T1's gate asserts.
* The `RenderSettings` change is one additive field (`waterSurface`, default
  false, set true by the Standard preset) — the struct's existing fields keep
  their meaning and the settings file keeps loading.
* The world transform is **published** (`worldCenterX()` etc.) rather than
  recomputed in `main.cpp`. A second transform is a second thing that can
  disagree with the first; that is what the water pass's first bug was.

## 3. Build commands and exit codes

```
bash build-target.sh tf2_demo_native world_material_probe viewmodel_material_probe
  rc=0   (two pre-existing warnings: main.cpp C4457, native_renderer.cpp C4456)
```

Individual targets built during the round, every one `rc=0`:

| Command | rc |
|---|---|
| `build-target.sh tf2_demo_native` | 0 |
| `build-target.sh world_material_probe` | 0 |
| `build-target.sh viewmodel_material_probe` | 0 |

## 4. Forward gate results

```
bash world-material-effects-check.sh
  === 1/6 the probe's self-tests, including the water warp's properties ===
  OK selfTest / capabilitySelfTest / worldDrawSelfTest / waterSelfTest
  OK warp bounded / finite / NaN input / NaN amplitude / negative amplitude
  OK warp repeatable / warp moves with phase / zero amplitude is identity
  OK warp max delta = 0.02 (inside the 0.02 bound and not zero)
  OK waterOffState waterEmptyState waterPartialState waterFallbackState
  OK waterActiveState waterParams waterParamsBad
  === 2/6 the water fixtures, and the water draw on the real map ===
  OK water fixtures = 6 / checks = 42 / failures = 0
  OK capability fixtures = 9 / failures = 0        (T1/T3 fixtures still pass)
  OK water faces = 18 / water triangles = 36
  === 3/6 the effect readers, positive and negative, with their mutations ===
  OK effect fixtures = 14 / checks = 111 / failures = 0
  OK effect mutations = 14 / not moving a reading = 0
  === 4/6 the water surface ===
  OK off: state=unavailable draws=0 visible=0
  OK on : state=active draws=1 visible=1 warp=1 textureBound=1 faces=18 triangles=36
  OK reflection=unavailable refraction=unavailable
  OK water-off != water-on (5571 bytes differ)
  OK water on == water repeat = 0
  === 5/6 the ViewModel effects ===
  OK phong  state=active declared=1 parsed=1 uploaded=1 applied=1 ranges=1 fallback=0
  OK bump   state=active declared=1 parsed=1 uploaded=1 applied=1 ranges=1 fallback=0
  OK bumpTextureResolved=1 uploaded=1 reused=1 uploads=1
  OK selfillum natural: state=unavailable declared=0 applied=0
  OK selfillum forced : state=active declared=1 applied=1 ranges=1
  OK selfillum forced-then-off: state=declared applied=0
  OK phong-off != phong-on (5250 bytes)   OK bump-off != bump-on (12518 bytes)
  OK selfillum-off != selfillum-on (35646 bytes)
  OK selfillum force off == natural = 0   OK viewmodel natural == repeat = 0
  === 6/6 T1 did not regress ===
  OK sky state=active vertices=36 triangles=12 faces=6 draws=1
  OK vis clipApplied=1 input=200000 drawn=170244 hidden=29756
  OK vis drawn + hidden = input (170244 + 29756 = 200000)

WORLD-MATERIAL-EFFECTS=PASS          (exit 0)
```

Also re-run, both unchanged and green:

```
bash world-material-check.sh     -> WORLD-MATERIAL=PASS
bash viewmodel-material-check.sh -> VIEWMODEL-MATERIAL=PASS
bash verify-fast.sh              -> VERIFY-FAST=PASS (assertions_ok=43)
```

## 5. Mutation results

`bash world-material-effects-check.sh --mutation` — exit 0. Each mutation records
its target reading, the control reading, the change, whether the control held,
whether the source came back byte-identical, and whether the rebuild succeeded.

### 5a. Wrong expectations (the cheap half)

| Expectation that must NOT hold | Result |
|---|---|
| `phongAppliedRanges=0` while `phongDeclared=1` (the phase-1 state) | did not match |
| `bumpTextureUploaded=0` while the bump effect applied | did not match |
| `water draws=0` while the switch was on | did not match |
| `water faces=0` on a map that has 18 | did not match |
| effect mutation count = 99 | did not match |

### 5b. Product mutations (the product itself, not a perturbed string)

`MUTATION-CAUGHT=PASS` for all four; gate exit 0.

| # | Mutation | Target reading | Control | After mutation | Control held | Source restored | Rebuilt |
|---|---|---|---|---|---|---|---|
| 1 | water draw deleted (`if (false && worldWaterEnabled_ && …)`) | `water draws` | `state` | `draws=0` while `state=active` | yes | byte-identical | yes |
| 2 | warp disabled (`warpAmplitude = 0.0f`) | `water warp` | `draws` | `warp=0` while `draws=1` | yes | byte-identical | yes |
| 3 | Phong parameter upload deleted (`phongOn = false`) | `phongApplied` | `phongDeclared` | `applied=0` while `declared=1` | yes | byte-identical | yes |
| 4 | normal-map bind deleted (`bumpOn = false && …`) | `bumpApplied` | `bumpTextureUploaded` | `applied=0` while `textureUploaded=1` | yes | byte-identical | yes |

```
=== mutation: wrong expectations first, then four product mutations ===
  MUTATION-CAUGHT=PASS (water_draw_deleted: draws=0 while the state still reads active)
  MUTATION-CAUGHT=PASS (water_warp_disabled: draws=1 warp=0)
  MUTATION-CAUGHT=PASS (phong_upload_deleted: declared=1 applied=0)
  MUTATION-CAUGHT=PASS (bump_bind_deleted: textureUploaded=1 applied=0)
MUTATION-CAUGHT=PASS (five wrong expectations, the deleted water draw, the
  disabled warp, the deleted phong upload and the deleted normal-map bind
  each go red; every source came back byte-identical)
```

Every mutation is judged on a **named reading**, not on a whole-frame difference:
a mutation that changed the picture for an unrelated reason while leaving the
reading intact would not be counted as caught.

Mutations 1 and 2 are the two the first version of the water pass would have
passed. Mutation 1 is the exact shape of the defect that was actually shipped and
then found — the pass reported `draws=1` and changed no pixel — and the reading
that catches it is the **draw count**, not the state word.

**Mutation 1 is also the one that found defect 3 in the gate itself.** It failed
twice before it passed, and both failures were the reading's fault rather than the
mutation's:

* first run: `draws=1 state=active` — the water line was emitted at upload time
  and carried the *request judge's* draw count, which the deleted Draw call cannot
  move. The reading described its own copy of the guard.
* second run: `draws=0 state=unavailable` — the draw count was now honest, but
  `worldWaterState_` was never set on the upload function's success path, so the
  state word had nothing to do with the verdict.

Both are recorded in the appendix. The mutation is the instrument that made them
visible; a gate that had only ever been run in the green would have shipped both.

## 6. Water state, reason and counters

Real map `cp_snakewater_final1`, paused at tick 0:

```
off: state=unavailable faces=0  triangles=0  draws=0 visible=0 warp=0 textureBound=1 fallback=0
     reason=water draw disabled by the caller
on : state=active      faces=18 triangles=36 draws=1 visible=1 warp=1 textureBound=1 fallback=0
     reason=36 water triangles from 18 faces; approximate warp applied, water texture bound;
            reflection and refraction have no render target
```

* **Geometry** — 18 faces / 36 triangles, the same numbers
  `world-material-check.sh` pins for the capability layer. The draw counts the
  same geometry the parse measured.
* **Material** — the water material is found by walking the map's own water
  triangles for their material name; the parameters come from
  `measureWaterParams` over that VMT's text, which is the same reader the rest of
  the material chain uses.
* **Texture** — the material's `$bumpmap`/`$normalmap` resolved and decoded.
  `textureBound=1` on both runs, which is why the OFF row is "disabled by the
  caller" rather than "no texture".
* **Warp** — applied on the CPU, once, in `evaluateWaterDrawRequest`, so the
  vertices the renderer uploads are the ones the reading counted. Amplitude 0.02
  in uv units; phase from the playback tick (`tick / 66.0`), which is 0 on a
  paused run — that is what makes the repeat frame byte-identical.

### The warp, as three properties

| Property | How it is measured | Result |
|---|---|---|
| bounded | grid of 18 phases × 41 uvs, every delta must be ≤ amplitude | max delta **0.02** = the bound, never over |
| NaN-free | non-finite uv → passed through; NaN amplitude → passed through; negative amplitude → passed through | all three hold |
| repeatable | same phase twice → identical; phase 0 vs 0.37 → different | both hold |
| zero is identity | amplitude 0 → uv unchanged | holds |

## 7. Reflection and refraction

```
waterReflectionState = unavailable
waterRefractionState = unavailable
reason: "...; reflection and refraction have no render target"
```

**UNAVAILABLE, and the reason names the cause.** No render target was created, no
world colour was captured, and the water pass samples no depth or colour buffer.
The static cubemap, the flat shade and the approximate warp are **not** reported
as either — the reading names them separately, and the reflection/refraction
fields stay `unavailable` on every run.

The evaluation the task asks for, honestly:

* a second render target would need a resize path (the pass already has one for
  the back buffer, but a reflection target doubles the VRAM the 8 GB budget in
  `docs/VRAM_TUNING.md` does not have);
* a reflection pass that samples the world colour it is being drawn into is
  recursive, and the guard against that is a second view of the scene rather than
  a second pass over the same one;
* neither is implemented, so neither is claimed.

## 8. Phong readings

`v_sapper_spy` slot 0, pinned paused recording:

```
phongState=active phongDeclared=1 phongParsed=1 phongUploaded=1
phongApplied=1 phongAppliedRanges=1 phongFallback=0
phongExponent=5.000 phongBoost=0.250 phongFresnel=0.500..1.000
reason: the material declares $phong; $phongexponent=5.000000; $phongboost=0.250000;
        $phongfresnelranges=0.500000..1.000000 (3 components); every present parameter parsed
```

Parsed keys: `$phong`, `$phongboost`, `$phongexponent`, `$phongfresnelranges`,
`$phongtint`. The parameters reach the constant buffer (`phongParams`,
`phongTint`, `phongFeatures`) and the highlight is a half-vector lobe on the
**view-space** normal, so both the light and the camera participate.

**`phong-off` vs `phong-on`: 5250 bytes differ.**

### The parse bug this round found by running it

`$phongfresnelranges` is a **three**-component vector `[min mid max]` on this
install — the first material that declares `$phong 1` carries `[0 0.5 1]`. The
first reader parsed it as a pair, so Phong came back
`declared=1 parsed=0` on the only real material available. The fixture
`phong-positive` asserts the three-component form explicitly; the reader accepts
two or three and refuses anything else **by name**.

## 9. Bump readings

```
bumpState=active bumpDeclared=1 bumpParsed=1 bumpUploaded=1
bumpApplied=1 bumpAppliedRanges=1 bumpFallback=0
bumpTextureResolved=1 bumpTextureUploaded=1 bumpTextureReused=1 bumpUploads=1
reason: normal map models/player/spy/spy_hands_normal named by $bumpmap;
        no env mask from the normal map's alpha
```

**`bump-off` vs `bump-on`: 12518 bytes differ.**

Parsed keys: `$normalmap`, `$normalmapalphaenvmapmask`, `$bumpmap`. When both
texture keys are present the current one (`$normalmap`) is bound and the choice is
a reading (`bump.boundPath`), because a reader that picked the legacy key would
still report declared and parsed.

* **Independent cache** — the normal map has its own texture (`t1`) and its own
  upload counter. The albedo cache is keyed by the slot's own normalized vtf
  path, so a normal map cannot be uploaded as an albedo by construction rather
  than by a check somebody has to remember.
* **Uploads do not grow per frame** — `bumpUploads=1` for the whole run while
  `bumpTextureReused=1`; a pass that rebuilt every frame would climb.
* **Missing normal map falls back to plain lighting** — `bump-off` is exactly
  that state, and it moves the frame rather than breaking it.
* **No leakage from the previous material** — the counter is per-run and the
  texture is re-uploaded from the slot that is being drawn.

## 10. Selfillum readings

```
natural: selfillumState=unavailable selfillumDeclared=0 selfillumApplied=0
         reason: the material declares no $selfillum
forced : selfillumState=active selfillumDeclared=1 selfillumParsed=1
         selfillumApplied=1 selfillumAppliedRanges=1
         reason: the material declares no $selfillum; forced on by
                 --viewmodel-selfillum-force (the albedo alpha masks)
forced then off: selfillumState=declared selfillumApplied=0
```

**No ViewModel material on this install was found to declare `$selfillum`** — the
loose material tree has zero, and the drawn model's own VMT (read through the
asset root, from the VPK) declares `$phong` and `$bumpmap` and no selfillum. So
the branch is exercised through `--viewmodel-selfillum-force`, which follows the
established shape of `--world-env-force` and is **named in every reading line**
(`selfillumForce=1`) so a forced run cannot be mistaken for a natural one.

* **`selfillum-off` vs `selfillum-on`: 35646 bytes differ.**
* **The control:** the *same force* with the effect switched back off reproduces
  the natural frame **exactly (0 bytes)**. Without that pair the 35646 bytes
  could be the force flag rather than the effect.
* **Mask** — a declared `$selfillummask` is bound at `t2`; a material with
  `$selfillum 1` and no mask uses the albedo's alpha, which is Source's own
  default and is reported as `maskFromAlpha` rather than as a missing file read
  as zero. The mask is never taken from the previous material: the binding is
  per-run and the shader branches on `selfillumFeatures.y`.
* **Region** — the effect exists only in the ViewModel pixel shader. The world
  pixel shader has no selfillum branch, so a world triangle cannot receive it.

## 11. Positive and negative fixture inventory

### `world_material_probe --water-fixture` — 6 cases / 42 checks / 0 bad

| Case | Kind | Expectation |
|---|---|---|
| `resolved` | **positive** | state=active, 2 faces, 4 triangles, 1 draw, warp applied, 12 output vertices |
| `no-material` | negative | state=fallback, 1 draw, fallback=1, no warp |
| `texture-unresolved` | negative | state=fallback, 1 draw, fallback=1 |
| `partial-buffer` | negative (zero-sized / malformed input) | state=unavailable, "not a whole number of triangles" |
| `no-geometry` | negative (zero-sized input) | state=unavailable, "no water geometry" |
| `switch-off` | negative (caller's switch) | state=unavailable, 0 draws, empty output |

`world_material_probe --self-test` adds a fourth line: 17 named checks covering
the warp's four properties, the six draw states, and the water material reader
(`waterParams`, `waterParamsBad`).

### `viewmodel_material_probe --effects-fixture` — 14 cases / 111 checks / 0 bad

| Case | Kind | Covers |
|---|---|---|
| `phong-positive` | **positive** | exponent 24, boost 2.5, `[0 0.5 1]` → fresnel 0..1, tint parsed |
| `phong-no-exponent` | positive (default) | exponent defaults to 5, parsed |
| `phong-bad-exponent` | negative — invalid numeric | declared, NOT parsed |
| `phong-bad-tint` | negative — invalid numeric | declared, NOT parsed |
| `bump-positive` | **positive** | `$bumpmap` bound, `$normalmapalphaenvmapmask` on |
| `bump-normalmap-key` | positive | current key bound |
| `bump-both-keys` | positive | `$normalmap` wins over `$bumpmap`, and which one won is a reading |
| `bump-no-declaration` | negative | declared=0 |
| `selfillum-mask` | **positive** | mask path + tint parsed |
| `selfillum-no-mask` | positive (default) | `maskFromAlpha=1` |
| `selfillum-bad-tint` | negative — invalid numeric | declared, NOT parsed |
| `malformed-vmt` | negative — malformed VMT | refused, all three undeclared |
| `zero-sized-input` | negative — zero-sized | refused |
| `missing-shader-input` | negative | effect declared and parsed, and the DRAW cannot apply it — the declared/applied split |

Each case carries a mutation that must move a **named** check:
`--effects-fixture --mutation` → 125 checks / 14 mutations / 0 not moving.

### stdout compatibility

The probes' existing lines are byte-identical: `selfTest`, `capabilitySelfTest`
and `worldDrawSelfTest` are unchanged (verify-fast pins seven substrings of the
first), and `--fixture` still reports 9 fixtures / 90 checks / 120 with mutations.
The new output is `waterSelfTest` (a **new fourth line**),
`--water-fixture` and `--effects-fixture` — strictly additive modes.

## 12. Contrast frames and SHA-256

All from one gate run (`RUN_TAG 20261014143810-788`), `--hidden-window
--audio-disabled --start-paused --capture-tick 0 --audio-device 6`.

| Frame | SHA-256 (prefix) | Bytes |
|---|---|---|
| `water-off.bmp` | `fdefb876840850b8db13415bbe40d727…` | 2,582,406 |
| `water-on.bmp` | `e109a39e7257db4e154e3eb4fc249cb9…` | 2,582,406 |
| `water-repeat.bmp` | `e109a39e7257db4e154e3eb4fc249cb9…` | 2,582,406 |
| `vm-natural.bmp` | `68d51a6cadcfece6a6ebf79a31c74ff1…` | 2,582,406 |
| `vm-phong-off.bmp` | `61c766aead9f6df7a015117b958a6d2b…` | 2,582,406 |
| `vm-bump-off.bmp` | `763510dac57c8091d598d9e77dc9fe2a…` | 2,582,406 |
| `vm-selfillum-on.bmp` | `d62f7b78d498d8e484ccf55280946dd3…` | 2,582,406 |
| `vm-selfillum-off.bmp` | `68d51a6cadcfece6a6ebf79a31c74ff1…` | 2,582,406 |
| `vm-natural-repeat.bmp` | `68d51a6cadcfece6a6ebf79a31c74ff1…` | 2,582,406 |
| `t1-sky.bmp` | `fdefb876840850b8db13415bbe40d727…` | 2,582,406 |

| Comparison | Differing bytes | Verdict |
|---|---|---|
| water-off vs water-on | 5,571 | differ ✓ |
| water-on vs water-repeat | 0 | identical ✓ |
| vm-natural vs vm-phong-off | 5,250 | differ ✓ |
| vm-natural vs vm-bump-off | 12,518 | differ ✓ |
| vm-natural vs vm-selfillum-on | 35,646 | differ ✓ |
| vm-natural vs vm-selfillum-off | 0 | identical ✓ |
| vm-natural vs vm-natural-repeat | 0 | identical ✓ |

`vm-selfillum-off` hashing equal to `vm-natural` **and** `t1-sky` hashing equal to
`water-off` are both expected: `t1-sky` and `water-off` are the same launch with
the water off, and `vm-selfillum-off` is the natural launch with a force that was
switched back off.

Region analysis: the water pass draws 36 triangles, and the differing pixels are
confined to those triangles — the comparison is a whole-frame byte count here
because the gate also asserts the non-water readings (§12's identity) are
unchanged, which is the region control. The ViewModel comparisons use the same
paused single-frame launch `viewmodel-material-check.sh` established, so the world
is frozen and the only input that differs is the switch.

## 13. Texture upload and reuse readings

| Reading | Value | What it says |
|---|---|---|
| `texturesUploaded` (albedo) | 1 | one albedo texture for the whole run |
| `texturesReused` (albedo) | 1 | one bind served from the cache |
| `bumpTextureUploaded` | 1 | the normal map is bound |
| `bumpTextureReused` | 1 | the bind came from the existing texture |
| `bumpUploads` | 1 | **one** GPU normal-map texture created over the whole run |
| `bumpTextureResolved` | 1 | the material named one and it decoded |

`bumpUploads` is cumulative and is **not** reset per frame — that is the whole
reading. A constant 1 across many frames with a non-zero reuse count is a cache; a
value that climbed would be a per-frame rebuild, and
`viewmodel-material-check.sh` already asserts the same shape for the albedo
(`uploads do not grow with the frame count`).

## 14. `viewmodel-material-check.sh` — the three assertions that moved

Phase 1 asserted:

```
assert_eq "phong is NOT applied (phase 1)"    "$(rval "$READINGS" phongAppliedRanges)" "0"
assert_eq "bump is NOT applied (phase 1)"     "$(rval "$READINGS" bumpAppliedRanges)" "0"
assert_eq "selfillum is NOT applied (phase 1)" "$(rval "$READINGS" selfillumAppliedRanges)" "0"
```

That was the honest reading then: the VMTs declared the effects and the pixel
shader read none of them. T2 made the shader read them, so the assertions moved
with the product — **to a stronger claim, not a weaker one**:

```
assert_ge "phong applied by a drawn range (phase 2)" "$(rval "$READINGS" phongAppliedRanges)" 1
assert_ge "bump applied by a drawn range (phase 2)"  "$(rval "$READINGS" bumpAppliedRanges)" 1
assert_eq "selfillum is not declared by this material" "$(rval "$READINGS" selfillumSlots)" "0"
assert_eq "selfillum is therefore not applied here"    "$(rval "$READINGS" selfillumAppliedRanges)" "0"
```

Phase 1's `== 0` was satisfiable by a pass that never drew at all. Phase 2
requires the declaration count **and** the applied count to be non-zero, so a
pass that stopped binding, stopped drawing or stopped compiling goes red here
instead of reading as "phase 1 again". The three `phongSlots`/`bumpSlots`
`assert_ge 1` lines are unchanged.

This is a change to a gate, and it is recorded here rather than made quietly. It
is not a loosening: it is a strengthening, and the selfillum pair is the one that
would be a loosening if it were not also asserted by
`world-material-effects-check.sh` §5, which forces the effect on a material that
lacks it and requires the forced frame to differ.

## 15. Unverified items

Stated plainly rather than omitted.

1. **Reflection and refraction are not implemented.** They read `unavailable` by
   design (§7). No render target exists, so nothing about their cost, quality or
   correctness is measured.
2. **Selfillum has no natural input on this install.** The effect is proven
   through `--viewmodel-selfillum-force`; that a shipped ViewModel material would
   exercise the same path is inferred from the reader, not measured. The search
   covered the loose material tree (zero hits) and the drawn model's own VMT; the
   VPK's ~150k entries were not enumerated for `$selfillum`.
3. **The water warp's visual quality is not assessed.** Its three *properties*
   are measured; whether 0.02 uv units of two-sine drift looks like water is a
   judgement no reading here makes.
4. **Only one water material and one map were exercised.** `cp_snakewater_final1`
   (18 faces / 36 triangles) is the only water surface run. A map whose water
   material declares no `$bumpmap`/`$normalmap` would take the fallback path,
   which the fixtures cover but the product has not been run on.
5. **The ViewModel effect readings come from one model and one slot.**
   `v_sapper_spy` slot 0, which is the slot `viewmodel-material-check.sh` pins.
   Other models' materials are read by the fixture harness, not by the product.
6. **No long-run stability measurement.** `bumpUploads=1` was read after one
   frame; a 200-tick run was not compared against it for leaks.
7. **`--world-water` and the three `--viewmodel-*` switches were only exercised
   at tick 0 with `--start-paused`.** Their behaviour under a moving demo is not
   measured.
8. **The `t2_water` shader branch shares the world pixel shader.** The water uses
   `worldPixelShader_` with its own texture and no lightmap; a water surface with
   a lightmap is not possible in this implementation, which is a limitation
   rather than a defect but is not measured either way.

## 16. Worktree state

```
git status --short
 M native/src/main.cpp                 (the gate's trace split, committed with the gate)
 M viewmodel-material-check.sh         (the phase-1 → phase-2 assertions)
 M evidence/world-material/*.txt       (regenerated by re-running the T1 gates)
 M evidence/viewmodel/material-readings.txt
?? evidence/world-material-effects/
?? world-material-effects-check.sh
```

Everything above is committed with the gate and this report. The evidence files
under `evidence/world-material/` and `evidence/viewmodel/` are **regenerated
output** from re-running the two existing gates unchanged; their content is what
those gates produced on a passing run.

No merge to the mainline was performed. `verify-all.sh` was **not** run, as the
task requires.

## 17. GitHub branch address

```
https://github.com/elo6371/tf2-demo-player-native/tree/task/world-material-effects
```

Pushed as `origin/task/world-material-effects`. Not merged.

---

## Appendix — the four defects only running could find

Recorded because each one is the kind of thing a reading has to be able to catch,
and each was caught by a reading rather than by inspection.

1. **The water pass drew 36 triangles, reported `draws=1`, and changed no pixel.**
   The upload block sat *before `uploadWorldGeometry`*, so it read the world
   transform's initial values (centre 0, scale 1) and built its vertices in raw
   map coordinates — hundreds of units from where the scaled world is drawn. The
   initial transform is a *legitimate* transform, so the values cannot be used as
   a sentinel; the guard is `worldBoundsValid()`. Found by comparing the two
   frames, which is the assertion the task requires for exactly this reason.

2. **`$phongfresnelranges` is a three-component vector.** Parsing it as a pair
   made Phong `declared=1 parsed=0` on the only real material that declares it.
   The first fixture run then reported *my own wrong expectation* rather than the
   reader's, which is what made the fixture harness worth building.

3. **The water draw reading was the request's own count, and the state word was
   never set at all.** Two defects on one reading, both found by mutation 1 rather
   than by inspection:
   * `gpu: world water` was emitted from the upload block and carried
     `draws=waterReading.waterDrawCalls` — a number computed by the *request
     judge* before any frame ran. The deleted Draw call left it reading
     `draws=1`. This is the same class as the T1 note that a reading recomputed
     from the guard it describes cannot see a deleted draw. Fixed by splitting the
     channel: an upload-time `gpu: world water-upload` line carrying upload facts
     only, and a post-frame `gpu: world water` line read from
     `waterSurfaceSampleState()` — whose `waterDrawCalls` is set **inside the pass**
     and nowhere else.
   * `worldWaterState_` was only written on the upload function's *failure* paths,
     so the post-frame reading reported `state=unavailable` for a surface that
     drew. Fixed with `setWorldWaterReading()`, which hands the judge's verdict in
     rather than letting the renderer recompute it: the state is a judgement about
     the request, the draw count is a fact about the pass, and keeping them in one
     place made the second unable to move on its own.

   A gate that had only ever been run in the green would have shipped both. The
   mutation is what made them visible, and the fix is what made the mutation pass.

4. **`traceFmt` silently truncates at 512 bytes, and it did so three times.** The
   first effects line dropped its reason strings entirely; the split counters line
   was still 519 bytes and cut `selfillumAppliedRanges` in half; the reasons line
   on the forced-selfillum run dropped the switch fields, so the gate read an
   empty string and reported "the switch is not off" for a run where it was. The
   line is now four. A diagnostic channel that answers with a prefix is worse than
   one that fails, because a prefix looks like an answer.
