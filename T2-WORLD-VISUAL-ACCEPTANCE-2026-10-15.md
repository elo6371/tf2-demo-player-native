# T2 — World Material and Visual Acceptance (2026-10-15)

## Verdict

```
world-material-check.sh            : PASS (8/8 sections, 117 OK / 0 FAIL)
world-material-check.sh --mutation : MUTATION-CAUGHT=PASS (2/2 product mutations, sources byte-identical)
world-material-effects-check.sh    : WORLD-MATERIAL-EFFECTS=PASS (101 OK / 0 FAIL)
verify-render.sh                   : VERIFY-RENDER=PASS (9/9 gates, mutation=0)
t2-visual-matrix.sh                : T2-VISUAL-MATRIX=PASS (42 assertions)
```

The task's acceptance question, answered in one table (details in section 6):

| dimension | switch | geometry | pixels | kind |
|---|---|---|---|---|
| skybox | `--world-sky` / `--no-world-sky` | vertices 36 -> 0, draws 1 -> 0 | 2,005,722 bytes (77.67% of the frame) | geometry AND pixels |
| pvs clip | `--world-vis-clip` / `--no-world-vis-clip` | drawn 170,244 -> 200,000 (`drawn+hidden=input` on) | 347,102 bytes (13.44%) | geometry AND pixels |
| water | `--world-water on` / `off` | draws 1 -> 0 | 5,571 bytes (0.2157%); repeat run byte-identical | pixels |
| cubemap | `--world-env-force` (diagnostic) | - | gate section 7 asserts a moved frame (>=1000 bytes) | pixels |
| lightmap | no off switch exists | - | frame is the lit frame; no A/B possible | counters + identity only (stated, not dressed up) |

Which real input was absent / which claims remain unverified: section 9. The
three findings a naive reading would have gotten wrong: the clip pair is NOT
pixel-neutral (13.44% change, cause not distinguished); the POV recording's
live-tick frames are NOT run-reproducible (camera state; 43.6% between two
identical-switch runs at tick 3500); and the lightmap has no switch, so "each
switch changes pixels" cannot be claimed for it.

---

## 1. Task, branch, worktree

| | |
|---|---|
| Task | T2 — World Material and Visual Acceptance (`NEXT-TASKS-2026-10-15.md` section 5) |
| Branch | `task/world-material-visual` |
| Worktree | `D:\TF2_Native_Worktrees\world-material-visual` |
| Upstream repo | `D:\TF2_Native_Test` (`git@github.com:elo6371/tf2-demo-player-native.git`) |

The integration worktree and `D:\TF2_Demo_Player` were not edited. No existing
gate, no `native/` source, and no `verify-all.sh` step was modified.

## 2. Base commit and final commit

```
base     : 917b0122376419ae0a9de5be35d932a9fa487be3  (p0-entity-protocol)
           "docs: record final chain pin status and task plan"
commit A : f18da1134ea596d8606d03f42110ee67489c76e5  "test(t2): the world visual frame matrix, and the tools it needs"
final    : the commit carrying this report (its parent is f18da113); `git log -2` on the branch
```

## 3. Files changed, and why

| File | Why |
|---|---|
| `t2-visual-matrix.sh` | **new.** The round's frame-level pairs: five paused launches at tick 0 (on / sky-off / vis-off / water-off / repeat), the pass readings from their traces, byte-level frame comparisons with named bounds, and the committed frame record. No other gate compared the sky switch's or the clip switch's frames. |
| `bmp-to-png.py` | **new tool.** Converts the product's 24bpp BMP frames to PNG with the standard library only (PIL is not installed here); used by the matrix script for the committed frame record. |
| `bmp-diff-bbox.py` | **new tool.** Where two frames differ: differing pixel count plus the top-left-origin bounding box. Used by the matrix script to locate the water strip and by the report's region claims. |
| `evidence/t2-visual-20261015/` | **new.** This round's readings, frames, and logs (sections 5-8). |
| `evidence/t2-visual-20261015/tools/{bmp-diff-map.py,crop-stack.py}` | **new.** Analysis aids used for the report's region figures (difference heat map; same-region stacked crop). |
| `T2-WORLD-VISUAL-ACCEPTANCE-2026-10-15.md` | **new.** This report. |

No `native/` file changed. No existing script changed.

## 4. Real inputs and exact commands

Inputs (hashes recorded in `evidence/t2-visual-20261015/inputs.txt`):

| Input | Value |
|---|---|
| TF root | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` |
| Demo (primary) | `bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`, 51,834,617 bytes, sha256 `308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f` |
| Map | `cp_snakewater_final1.bsp`, 59,590,420 bytes, sha256 `f09d406d2bbe397c8f9df9ca17f11b36b53d40fcf57e3f1532ec9af811b2a1eb` |
| Demo (supplementary POV) | `autorecord_2026-05-16_17-16-17.dem`, 20,861,793 bytes, sha256 `5f9344d5206e4aa73db003220c2c3d4ffa556cf764a25e3bbc2e11d4589d39c5` |
| Pinned tick | 0, `--start-paused` (the deterministic point; first frame == captured frame) |

Commands, in order:

```text
bash build-target.sh tf2_demo_native world_material_probe viewmodel_material_probe
bash t2-visual-matrix.sh
WORLD_MATERIAL_SCRATCH=.scratch/t2-visual/wm-rec \
  WORLD_MATERIAL_OUT=evidence/t2-visual-20261015/world-material \
  bash world-material-check.sh
WORLD_MATERIAL_SCRATCH=.scratch/t2-visual/wm-mut \
  WORLD_MATERIAL_OUT=evidence/t2-visual-20261015/world-material-mutation \
  bash world-material-check.sh --mutation
WORLD_MATERIAL_EFFECTS_SCRATCH=.scratch/t2-visual/wme \
  WORLD_MATERIAL_EFFECTS_OUT=evidence/t2-visual-20261015/world-material-effects \
  bash world-material-effects-check.sh
VERIFY_RENDER_OUT=evidence/t2-visual-20261015/verify-render bash verify-render.sh
```

Every product launch in this round carries `--hidden-window --audio-disabled`
(no window, no audio device). Mutations ran with no other build/mutation active
(checked before and after; no residual processes).

## 5. Build and gate exit codes

| Step | Command | rc | Last line |
|---|---|---|---|
| build | `bash build-target.sh tf2_demo_native world_material_probe viewmodel_material_probe` | 0 | `rc=0` (warnings: `main.cpp C4457`, `native_renderer.cpp C4267 x2 / C4456`, all pre-existing) |
| matrix | `bash t2-visual-matrix.sh` | 0 | `T2-VISUAL-MATRIX=PASS` (assertions_ok=42) |
| world-material forward | `bash world-material-check.sh` | 0 | `WORLD-MATERIAL=PASS` |
| world-material mutation | `bash world-material-check.sh --mutation` | 0 | `MUTATION-CAUGHT=PASS` (2 of 2 product mutations + 6 wrong expectations, sources byte-identical) |
| effects forward | `bash world-material-effects-check.sh` | 0 | `WORLD-MATERIAL-EFFECTS=PASS` (101 OK / 0 FAIL) |
| render tier | `bash verify-render.sh` | 0 | `VERIFY-RENDER=PASS (9/9 gates, mutation=0)` |

Full console logs: `evidence/t2-visual-20261015/logs/`.

## 6. Named forward readings and negative/refusal readings

### 6a. The round's frame matrix (`t2-visual-matrix.sh`, tick 0, `--start-paused`)

Five launches, one demo, one map; readings read from the traces of the launch
that produced each frame.

| Reading | on | sky-off | vis-off | water-off |
|---|---|---|---|---|
| sky state / vertices / triangles / faces / draws | active / 36 / 12 / 6 / 1 | unavailable / 0 / 0 / 0 / 0 | (same as on) | (same as on) |
| sky reason | `six sides, 36 vertices` | `skybox draw disabled by the caller` | - | - |
| vis cameraCluster / clipApplied / state | 2287 / 1 / active | - | -1 / 0 / disabled | - |
| vis input / drawn / hidden | 200000 / 170244 / 29756 | - | 0 / 200000 / 0 | - |
| water state / faces / triangles / draws | active / 18 / 36 / 1 | - | - | unavailable / 0 / 0 / 0 |
| water reason | `36 water triangles from 18 faces; approximate warp applied, water texture bound; reflection and refraction have no render target` | - | - | `water draw disabled by the caller` |

Frame comparisons (`bmp-pixel-diff.py`, 1264x681x24):

| Pair | Differing bytes | Percent | Max delta | Region (top-left origin) |
|---|---|---|---|---|
| on vs sky-off | 2,005,722 | 77.6704% | 149 | whole frame (x=0..1263, y=0..680) |
| on vs vis-off | 347,102 | 13.4413% | 250 | x=0..1218, y=123..539 |
| on vs water-off | 5,571 | 0.2157% | 79 | x=646..846, y=396..405 (the water surface, 201x10) |
| on vs repeat (the determinism control) | **0** | 0% | 0 | - |

Frame hashes (sha256 of the committed BMPs, `frames/sha256.txt`):

```
on        e109a39e7257db4e154e3eb4fc249cb9174db3539761aa120f863a9a111e8e1e
sky-off   abf99b406db5e85ba6e94f455f0fbcfd68a2488063151e8f627d8d110c5250e5
vis-off   700537cf4898592c954249c1f1079ee3323413bca6a8cf16568c2f8d346cad09
water-off fdefb876840850b8db13415bbe40d727fea1348f6e0ddabb65a9cb7c71df8781
repeat    e109a39e7257db4e154e3eb4fc249cb9174db3539761aa120f863a9a111e8e1e
```

The `on` and `water-off` hashes are byte-identical to the T2-effects round's
`water-on` / `t1-sky` frames -- the current line renders the same tick-0 frame
as the merged round that produced them.

### 6b. The existing world-material gate (forward)

All eight sections green; the numbers this round reads:

- self-tests: 21 flags true, including `skyPartialFallback` (missing sky face
  -> named "4 of 6" fallback), `visClipWithoutPvs` / `visClipNeedsCamera`
  (unavailable PVS -> named refusal), `atlasFitsAt2048` / `atlasGrowsWhenFull`.
- fixtures: 9 fixtures / 90 checks / 0 failures; with mutations: 120 checks /
  7 mutations / 0 failures.
- real map (`cp_snakewater_final1`), the six capabilities pinned:
  - lightmap `active`: 11527 faces with lightmaps, 11527 packed, **0 dropped**,
    199922 triangles with light uv, atlas **4096x1156**, `atlasGrew=true`,
    reason `all 11527 faces fit the 4096-wide atlas`, uv all in range.
  - cubemap `active`: 33 samples, 33 complete, 198 faces decoded, all 33 from
    the BSP pakfile; first path `materials/maps/cp_snakewater_final1/c-5135_2022_-15.vtf`,
    per-face means and checksums pinned.
  - sky `active`: `sky_well_01`, 6 vmts, 6 textures resolved, 6 decoded,
    largest face 512x256, smallest 1x1, 3145732 bytes decoded. The six face
    paths the reader resolves are
    `materials/skybox/sky_well_01{rt,lf,bk,ft,up,dn}.vmt` in that face order
    (the order `skyMaterialPath` and the probe self-test pin); the `dn` face's
    texture is the 1x1 stub in this install, which is why the drawn box
    resamples to a 512 cube (`side=512 resampled=1` in the trace).
  - displacement: 1316 faces / 165376 triangles, all corners matched.
  - water: 18 faces / 36 triangles tagged.
  - vis: mode `cluster-pvs`, 2940 clusters, `visibleFromCluster0=384`.
- negative control (no BSP): every capability `unavailable` with its own
  reason (`the bsp declares no lightmap faces`, ...).
- the product's own trace (the render side == the parse side):
  - lightmap `status=active atlas=1 vertices=600000 lightmapped=599766`
    (== 3 x 199922, and strictly below vertices: 737 triangles carry no
    lightmap face).
  - `displacedTriangles=165376` and `waterTriangles=36` == the parse side.
  - vis `cameraCluster=2287 hiddenTriangles=29756 inputTriangles=200000
    drawnTriangles=170244 clipApplied=1`; `drawn + hidden = input`.
  - sky `vertices=36 triangles=12 faces=6 draws=1 side=512`.
- cubemap end to end on `2koth_abbey` (pakfile-embedded, deflate storage):
  plain run `mode=0 declared=0` (the material's own answer), forced run
  `mode=2 declared=1`; the forced mode moved the frame by **45,468 bytes**.
- switches off: sky `state=unavailable vertices=0 draws=0` with the
  caller-named reason; clip `state=disabled clipApplied=0 drawn=200000`.

### 6c. The effects gate (water and ViewModel effects), forward

`WORLD-MATERIAL-EFFECTS=PASS` (rc=0). The readings this round's acceptance
reads:

- water warp self-tests: bounded (max delta 0.02 = the bound), NaN-free,
  repeatable, "phase moves it", zero amplitude is identity; the six draw states
  (`waterOffState`...`waterParamsBad`) all hold.
- water fixtures 6 / 42 checks / 0 failures; capability fixtures 9 / 0
  failures; water on the real map 18 faces / 36 triangles.
- effect fixtures 14 / 111 checks / 0 failures; 14 effect mutations each move a
  reading; 125 checks with mutations / 0 failures.
- water surface (tick 0, paused): off `state=unavailable draws=0 visible=0`;
  on `state=active draws=1 visible=1 warp=1 textureBound=1 faces=18
  triangles=36`; `reflection=unavailable refraction=unavailable`.
  **water off vs on: 5,571 bytes differ; on vs repeat: 0.**
- ViewModel effects: phong `active declared=1 parsed=1 uploaded=1 applied=1
  ranges=1 fallback=0`; bump the same plus `textureResolved=1 uploaded=1
  reused=1 uploads=1`; selfillum natural `unavailable` (no material on this
  install declares it), forced `active declared=1 applied=1` with the force
  named, forced-then-off `declared applied=0`.
  **phong 5,250; bump 12,518; selfillum 35,646; selfillum-forced-off vs
  natural: 0; natural vs repeat: 0.**
- T1 regression inside the same gate: sky 36/12/6 draws=1; vis
  `clipApplied=1 input=200000 drawn=170244 hidden=29756`, `drawn+hidden=input`.

Every number in 6c reproduces the merged effects round's report exactly (its
section 12 table), on this line, from this worktree's build.

### 6d. The render tier

`VERIFY-RENDER=PASS (9/9 gates, mutation=0)`, rc=0. The nine: the seven gates
(resource reachability, frame capture, entity material, weapon animation,
viewmodel contract, viewmodel animation, viewmodel companion) -- each with its
own marker line -- plus the material chain's 12/12 synthetic keys (including
`cubemap mode is the honest 2d approximation` and `lightmap mode is documented
as an average`) and the evidence-tree immutability assertion (`nothing under
evidence/ moved`; the tier's output stayed in
`evidence/t2-visual-20261015/verify-render/`). The tier's own build step
rebuilt the nine targets it drives: `build_rc=0`. `mutation=0` is the tier's
forward-only default, not a failed mutation.


## 7. Mutation results and source restoration proof

`bash world-material-check.sh --mutation` — rc=0. Output, verbatim from
`logs/chain-2-mutation-effects-verify-render.log`:

```text
=== mutation: wrong expectations first, then two product mutations ===
  MUTATION-CAUGHT=PASS (sky_draw_deleted: draws=0 while the state still reads active)
  MUTATION-CAUGHT=PASS (vis_clip_deleted: clipApplied=0, drawn=200000 = the whole buffer)
MUTATION-CAUGHT=PASS (six wrong expectations, the deleted sky draw and the
  deleted pvs clip each go red; both sources came back byte-identical)
```

| Mutation | Source edit | Target reading | Control it must not move | After mutation | Source restored | Rebuild |
|---|---|---|---|---|---|---|
| `sky_draw_deleted` | `native_renderer.cpp`: the sky guard ANDed with `false` | `draws` -> 0 | `state` stays `active` | `draws=0` while `state=active` | byte-identical (`cmp`, asserted by the gate) | rc=0 |
| `vis_clip_deleted` | `main.cpp`: `applyWorldVisClip` enabled forced `false` | `clipApplied` -> 0, `drawn` -> whole buffer | the forward run's `clipApplied=1` | `clipApplied=0 drawn=200000` | byte-identical (`cmp`, asserted) | rc=0 |

The six "wrong expectation" probes (pre-fix values that must NOT match) all did
not match. The mutation's rebuilds and restore-rebuilds are logged in the
mutation scratch dir; the four build logs are copied to
`world-material-mutation/`.

One specificity limitation is carried unchanged and not papered over: the
second mutation forces the clip OFF, so its reading is byte-identical to the
legitimate `--no-world-vis-clip` run — the gate catches it only because the
forward section reads `clipApplied=1`. A third, *specific* clip mutation exists
as an unmerged commit (`f266188`); see section 9 item 11.

The effects gate was re-run forward (section 6c); its own mutation cycle was
not re-run this round — it is outside the task's command list and costs four
more rebuilds. The world-material mutation above is this round's mutation and
restoration proof.

## 8. Evidence paths and hashes

Directory: `evidence/t2-visual-20261015/` (324 files; `SHA256SUMS` at its root
covers every one, `README.md` indexes what each file is).

| What | Path | Hash / value |
|---|---|---|
| the matrix's committed frames | `frames/{on,sky-off,vis-off,water-off,repeat}.{bmp,png}` | `frames/sha256.txt`: on `e109a39e7257db4e...`, sky-off `abf99b406db5e85b...`, vis-off `700537cf4898592c...`, water-off `fdefb876840850b8...`, repeat `e109a39e...` (== on) |
| the gate's frames | `frames/{world-lightmapped,cubemap-plain,cubemap-forced}.bmp` | `frames/sha256-gate.txt`: `fdefb876...` / `2d7781bf...` / `8853e5f1...` |
| the six asserted trace lines | `readings/switches-tick0.txt` | (the lines quoted in 6a) |
| gate outputs | `world-material/`, `world-material-mutation/`, `world-material-effects/` | the same content those gates write to their own committed dirs, produced this round without rewriting the old dirs |
| the render tier | `verify-render/` | per-gate logs + the `9/9` verdict |
| the POV record | `pov/pov-water-live.txt` + 4 PNGs | POV demo sha256 `5f9344d5206e4aa7...` |
| full logs | `logs/` | chain-1 (matrix + wm forward), chain-2 (mutation + effects + verify-render), build |
| inputs | `inputs.txt`, `exe-identity.txt` | demo `308cc1453672b26d...`, map `f09d406d2bbe397c...`, product `771b17174e6c089d...` |

## 9. Unverified items and external blockers

Stated, not omitted.

1. **The lightmap has no runtime off switch.** No lightmap A/B pixel pair is
   possible with the product as shipped; its evidence is the coverage and
   identity readings (§6b) plus the frame. Lightmap *fidelity* is not claimed:
   the render tier's material-chain key reads `lightmap mode is documented as
   an average`, and no gamma/intensity equivalence to Source is asserted.
2. **The clip pair's pixel difference is measured; its cause is not
   distinguished.** The 13.44% change is consistent with the approximate
   face-to-cluster mapping and with coincident brush surfaces resolving
   differently once the extra triangles are drawn; this round does not tell
   them apart and does not claim which frame is "right". Source-equivalence of
   the visible set remains unverified (T1 report section 8 item 5 stands).
3. **Water reflection and refraction are unavailable by design** (no render
   target); the warp's visual quality is not assessed (its properties are).
4. **Displacement** remains a named fallback where geometry is truncated or
   unavailable (T1/T3 limitation, carried unchanged).
5. **One water material and one map** were exercised (cp_snakewater_final1, 18
   faces). A water material with no bump/normal map takes the fallback path,
   which the fixtures cover but the product has not been run on.
6. **4096+ atlas behavior and frequent map/cluster changes** need inputs this
   machine does not have (the task's own explicit limitation; not covered).
7. **Skybox texture vs flat-color sky** is measured as the 77.67% frame
   difference plus 6/6 decoded faces; the visual quality of the resampled 1x1
   face and the far-plane/depth behavior are not assessed.
8. **Live-tick frames of a POV recording are not run-reproducible.** At tick
   3500 of the POV demo two runs with identical switches differ by 43.6% of
   the frame (1126909 bytes) -- the smoothed camera state -- while the water
   switch's own contribution there is unmeasurable (the on/off comparison
   differs from the control by 28 bytes). The control is what caught this; a
   live-tick pixel comparison without one is not evidence. The committed
   matrix avoids the issue by using tick 0 paused; the matcha demo's static
   camera happened to be stable at ticks 3000/6000, which is an input property,
   not a guarantee.
9. **No human viewing of any frame** (machine comparisons only), and no real
   GPU FPS/soak measurement (external blocker: this sandbox).
10. **The effects round's own unverified items carry over unchanged**: the
    selfillum effect has no natural input on this install (it is proven only
    through the forced diagnostic switch), and the ViewModel effect readings
    come from one model and one slot.
11. **A later, unmerged gate improvement exists elsewhere.** Commit `f266188`
    on `task/world-render-followup` adds a third, *specific* clip mutation (the
    camera cluster is discarded, so the reading differs from the legitimate
    off switch). This round does not include it; the world-material gate's
    second mutation remains the non-specific one T1 documented. The integrator
    may want to bring `f266188`'s gate change in separately.


## 10. Clean-worktree and runnable status

- After the report commit, `git status --short` on the branch shows nothing
  outside gitignored `.scratch/`: the delivery is commits A+B and nothing else,
  and no tracked file outside the delivery changed.
- No `tf2_demo_native`, `nmake`, `cl`, or gate process was left by this round
  (checked before and after the mutation run).
- The branch is runnable from the committed tree: build rc=0 at the delivered
  state (`logs/build.log`), and every gate above re-runs from the committed
  scripts with the section-4 commands.
