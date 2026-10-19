# T17 — World Visual Closure: what the reference frame is made of

Task `T17` (`NEXT-TASKS-2026-10-19.md`), owner rendering specialist. This round
turns the fixed reference frame from "a flat fragment with holes" into a picture
whose every population is named, fixes the three causes that were measured, and
leaves the rest as readings rather than adjectives.

**Status: verified.** Every gate in the focused list passed on the merged tree
(`efbb6da`, T18 integrated): `visual-baseline-check.sh` forward **PASS** (68
assertions), `--mutation` **PASS** (M1–M5 all red, every source restored
byte-identical), `world-material-check.sh` **PASS**, `--mutation` **PASS**,
`frame-capture-check.sh` **PASS**. §8 has the numbers.

## 1. Identity

| | |
| --- | --- |
| task | T17 (World Visual Closure) |
| branch | `task/t17-world-visual` |
| worktree | `D:/TF2_Native_Worktrees/t17-world-visual` |
| base commit | `e67630b` (merge of `task/t15-vpk-build` into `p0-entity-protocol`) |
| verified tree | `efbb6da` = `task/t17-world-visual` + `p0-entity-protocol` `ca10fad` (T18 merged) |
| source commits | `73ee6b9` (the three fixes), `4c9cf7f` (sky narrowed to the preset), `efbb6da` (the T18 merge; the gate and this report land in the commit after it) |
| product scope | `bsp_map.{h,cpp}`, `world_material.cpp`, `native_renderer.{h,cpp}`, `main.cpp`, `settings_store.cpp`, `world_geometry_probe.cpp`, `visual-baseline-check.sh` |
| evidence | `evidence/t17-world-visual/` (10 frames/maps, probe output, 5 gate logs) |

The T18 merge touched only viewmodel code and produced **no conflict** with this
task's hunks; the reference frame's hash is identical before and after it
(`d86785ef…`), so T18's bone-pose fix does not move the `--viewmodel-render
companion` picture.

## 2. What changed, and why

| file | change | measured reason |
| --- | --- | --- |
| `native/src/world_material.cpp` | `bspPakfileEntry` compares a normalized name (backslashes → slashes, no leading slash, lowercase) instead of `entryName == name` | pl_odyssey's pakfile stores `materials/nature/blendclifftograss_odyssey.vmt`; textdata names it `NATURE/BLENDCLIFFTOGRASS_ODYSSEY`. The VPK path already normalized, the pakfile path did not, so 29 of 108 world materials read `vmtMissing` — including all nine `WorldVertexTransition` terrain materials. `vmtMissing 29→3`, `vtfMissing 2→0`, `fromPakfile 23→59`, `blend-fetch declared 0→9`, `tiles 0→9`, `weightedVertices 0→85536`, `distinctWeights 0→34` |
| `native/src/native_renderer.cpp` | the pixel shader encodes the lightmap sample: `pow(sample, 1/2.2)` | the albedo textures are sRGB bytes, the RGBExp32 lightmap is linear radiance, and the target is UNORM. Using the sample as a multiplier is the "everything is dark" failure. `--world-lightmap off` on the same tick puts the same pixels at 192–255 that the lit frame put below 32 |
| `native/src/settings_store.cpp` | `skybox` follows the file's `preset`; the other Standard flags are left where the reader put them | a settings file that says `preset=1` while every flag holds Performance's value is a contradiction the frame inherits. The trace printed both halves at once: `cubemap ... preset=standard` and `sky-upload state=unavailable reason=skybox draw disabled by the caller`. 20.2% of the reference frame was clear colour because the sky pass never ran; with the sky drawn it is 0.04%. Only the sky is taken from the preset — see §6 for why the shading flags are not |
| `native/src/main.cpp` | `--world-lightmap on\|off`; `lightmapEnabled=` appended to the `world lightmap` trace line | there was no way to draw the world with the lightmap term removed, so "this surface is dark because its lightmap is dark" was not a measurable statement |
| `native/include/native_renderer.h` | `setWorldLightmapEnabled` / `worldLightmapEnabled` | the switch above; the product path never calls it |
| `native/include/bsp_map.h`, `native/src/bsp_map.cpp` | per-face tone histogram of what the packer wrote, the same histogram read out of lump 53 (HDR), `hasHdrLightmap` populated | the per-triangle histogram is taken through the uvs and therefore cannot separate "dark data" from "uv reading someone else's rectangle". The per-face histogram cannot be wrong about the data |
| `native/tools/world_geometry_probe.cpp` | `--atlas-width`, `--dump-lightmap`, `lightmap-face-tone`, `lightmap-hdr-tone`, `lightmap-tone darkMaterials` | the atlas is 4096x2251 (not square), so a uv measurement has to be taken at the width the product actually packs with; the dump is the page as a picture |

No global winding flip, no cull exception, no threshold moved. `visual-baseline-check.sh`
is untouched in this commit: its pins refer to the pre-fix tree and are part of §8.

## 3. Exact inputs

Identical to T11's reference input:

- recording `tf/demos/autorecord_2026-07-02_13-26-46.dem`, map `maps/pl_odyssey.bsp`
- capture tick **2000** (the first draw with `tick >= 2000`; a run that lands on
  2001 is retried, which is why every capture below reports `tick=2000`)
- 1264x681, `--viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion`
- POV = the demo's first-person observer camera, `eyeMap=(-2423.6,5566.0,-1280.0)`, cluster 1032
- tf root `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`
- probe: `--tf-root <tf> --map maps/pl_odyssey.bsp --atlas-width 4096`

## 4. The reference frame, decomposed

A pixel is in exactly one of three populations, and each is measured, not
eyeballed: **hole** = exactly the clear colour `(14,18,22)` (nothing was drawn),
**black** = drawn but every channel < 32 (the light took it to zero), **lit** =
everything else. `evidence/t17-world-visual/ref2-class.png` and `sky-class.png`
paint them magenta / red / green.

| population | default path (no `--world-sky`) | `--world-sky` |
| --- | --- | --- |
| hole (no geometry) | 0.2017 | **0.0004** |
| drawn, black | 0.2269 | 0.2269 |
| lit | 0.5715 | 0.7727 |

The hole population is the sky, all of it: the sky box fills 20.13% of the frame
and leaves 0.04% (the weapon's own silhouette edge). `--world-geometry none`
confirms the clear colour is what "nothing drawn" looks like
(`evidence/t17-world-visual/none2.bmp`), and `flat2.bmp` (unlit, single colour)
shows the same geometry coverage as the textured frame — so the holes were never
missing geometry, they were a missing pass.

## 5. The five categories, separated

T17 asks for geometry absence, visibility rejection, missing material, dark
lighting and camera placement to be told apart. They now are:

1. **Geometry absence — no.** `--world-geometry none` vs `flat`: the world pass
   covers 79.8% of the frame; the remaining 20.2% is the sky (§4), and with the
   sky pass on it is 0.04%. Displacement terrain is present and counted:
   `terrainOnly=1` draws `vertices=85596` of the world's 297936
   (`evidence/t17-world-visual/terrain2.bmp`).
2. **Visibility rejection — measured, and it is not hiding anything visible.**
   `--no-world-vis-clip` against the default frame differs in **3949 of 2582352
   bytes (0.1529%, max_delta 181)**: the clip removes triangles that are behind
   other triangles. The PVS row itself is T11's corrected one
   (`cameraCluster=1032 visibleFromCamera=1692 hiddenTriangles=22968 drawnTriangles=76344`).
3. **Missing material — fixed.** §2 row 1. Terrain now resolves both blend
   layers, 9 tiles, 85536 weighted vertices, 34 distinct weights, 3 materials
   still missing (a sky/water-only `.vmt`, a `$basetexture`-less patch, and one
   out-of-install reference) against 29 before.
4. **Dark lighting — real, and it is baked into the shipped map.** After the
   sRGB fix the dark population is unchanged at 22.69% *of the frame* and
   27.03% of triangles (`darkTriangles=26844`). It is not a uv error and not the
   page we chose:
   - `unwrittenTexels=0` of 296490 sampled corners, `luxelFits 17123/17126`
     — the uvs address written texels inside their own face's rectangle;
   - per-face tone (what the packer wrote) vs per-triangle tone (what the uvs
     read): dark bucket 5896/17126 = **34.4%** vs 31123/98830 = **31.5%** — the
     same population, so the sampling is not reading somebody else's rectangle;
   - lump 53 (HDR) says the same thing as lump 8 (LDR): `darkFaces 5303` vs
     5018, `zeroFaces 940` vs 898, intensity 0.891 vs 0.898, same 11130803
     samples. The tonemapped page is not what makes the frame dark.
   The worst materials by dark triangle count are `WOOD/WALL007A` (8092 of
   15339), `SWAMP/PROPS/FISHINGNET002` (2301 of 4984) and `WOOD/WOOD_WALL006B`
   (2069 of 2801).
5. **Camera placement — the acceptance camera, unchanged.** The frame is the
   demo's own observer eye, `eyeMap=(-2423.6,5566.0,-1280.0)`, the same point
   three instruments agree on (renderer inverse, `clusterAt`, probe).

## 6. What is still wrong, and what it would take

- **The 22.7% black population is map data.** It is drawn geometry with a bright
  albedo (`--world-lightmap off` reads 192–255 there) and a lightmap slice that
  is black in both lumps. Faithfully rendering it is the current behaviour; the
  only way to change it is to invent light, which T17 forbids. It is pinned as a
  reading, not fixed.
- **Back-face culling still removes a lot.** `--no-world-cull` against the
  default frame differs in **1109578 of 2582352 bytes (42.97%, max_delta 241)**.
  That is T11's measured residual — `misfaced=15126` of 99312 triangles, all
  brush/side-1 — and T11's decision table (`analysis/rule-decision.txt`, mask
  07) chose it over the alternatives with two first-hit oracles. A global flip
  is explicitly out of scope for T17, so this is reported, not touched.
- **The reference frame depends on `settings.ini`.** With the loader fixed, the
  sky follows `preset`, which the file supplies. A machine whose file says
  `preset=0` will draw no sky and the pinned region numbers will not match.
  §8 pins the sky state so that dependency is visible instead of silent.
- **Half a preset is still half a preset — for the shading.** `preset=standard`
  now decides the sky, and it still does not decide `dynamicLighting`,
  `specular`, `bumpMapping`, `shadows`, `waterReflection`, `textureMipBias`,
  `anisotropicLevel` or `modelLod`: those stay at Performance's values, so a
  standard-quality run currently draws with `lightDirectionAndMode.w = 0` (no
  directional light term) and `cameraPositionAndSpecular.w = 0`. This is
  deliberate, not an oversight: turning them on changes the shading of every
  world draw, and this round measured no frame under that shading, so claiming
  it here would be the kind of unmeasured change the task forbids. It is the
  next round's measurement, and it is cheap: one capture with the flags on,
  compared against `evidence/t17-world-visual/sky.bmp` on the same tick.

## 7. Evidence

`evidence/t17-world-visual/`:

| file | what it shows |
| --- | --- |
| `default-2000.bmp` / `.png` | the reference frame on the verified tree: material, light and sky fixed — hole 0.04%, black 22.69%, lit 77.27% |
| `repeat-2000.bmp` | the same settings captured again: **byte-identical**, `d86785efa19b24e2015c…` |
| `default-2000-class.png` | the three populations painted magenta / red / green |
| `ref2.bmp`, `sky.bmp` | the same tick before and after the sky pass (hole 0.2017 → 0.0004) |
| `flat-2000.bmp`, `terrain2.png`, `none-2000.bmp` | flat geometry, terrain only, nothing (the clear colour measured, not assumed) |
| `trace-flat2.txt`, `trace-none2.txt`, `trace-terrain2.txt`, `trace-sky.txt`, `trace-default-2000.txt` | the `material-fetch` / `blend-fetch` / `blend enabled` / `lightmap` / `sky-upload` / `vis` / `cull` lines of each frame |
| `probe.txt` | the geometry probe on `pl_odyssey`: both tone histograms, the HDR comparison, the winding census |
| `gate-visual-baseline-forward.txt`, `gate-visual-baseline-mutation.txt`, `gate-world-material-forward.txt`, `gate-world-material-mutation.txt`, `gate-frame-capture.txt` | verbatim gate output |

## 8. Verification

Everything below ran on the merged tree, one process at a time, after the T18
merge. Raw output is in `evidence/t17-world-visual/gate-*.txt`.

```
bash build-target.sh tf2_demo_native world_geometry_probe   # rc=0
bash visual-baseline-check.sh                               # rc=0  PASS (68 assertions)
bash visual-baseline-check.sh --mutation                    # rc=0  PASS (M1..M5 red)
bash world-material-check.sh                                # rc=0  PASS
bash world-material-check.sh --mutation                     # rc=0  PASS
bash frame-capture-check.sh                                 # rc=0  PASS
```

**Determinism and the sky A/B.** Three captures of the same tick with two
different flag sets:

| capture | flags | sha256 (first 20) |
| --- | --- | --- |
| `default-2000.bmp` | (none) | `d86785efa19b24e2015c` |
| `repeat-2000.bmp` | (none) | `d86785efa19b24e2015c` |
| `s2` (not archived) | `--world-sky` | `d86785efa19b24e2015c` |

Byte-identical three ways, so the determinism acceptance holds and the default
path draws exactly the frame `--world-sky` draws. The hash equals the one the
pre-merge `sky.bmp` had, which is also the evidence that T18's viewmodel fix
does not move this picture.

**What the gate pins now, and why** (all moved for a measured reason; nothing
was widened to fit):

| pin | was | now | why |
| --- | --- | --- | --- |
| `material-fetch tiled` | 57 | 64 | pakfile textures resolve |
| `material-fetch fromPakfile` | 23 | 59 | 36 more materials found in the pakfile |
| `material-fetch vtfTooLarge` | 43 | 54 | materials that were missing now reach their vtfs |
| `material-fetch vmtMissing` / `vtfMissing` | — | 3 / 0 | new pins: the two numbers the fix moved most |
| region W1 `meanLum` | 46..77 | 86.3 in 73..99 | display-space light + sky |
| region W2 `meanLum` / `grad` | 16..29 / 0.6..1.1 | 72.8 / 1.59 | same |
| region W3 `meanLum` / `std` | 8..14 / 6..11 | 92.6 / 70.3 | the region is now sky behind geometry, not a hole |
| region W4 `meanLum` / `grad` | 15..25 / 6.2..10.0 | 42.8 / 4.37 | flat sky now sits between the ceiling slabs |

`distinct=108`, `capped=21`, `vmtNoBase=20`, `vtfMissing=0`, the whole vis
block, the cull line and the geometry block did **not** move.

**New assertions** (section 2b of the gate): blend fetch `declared=9
resolved=9 refused=0`, blend `tiles=9 weightedVertices=85536
distinctWeights=34`, sky pass `state=active faces=6 vertices=36`, lightmap
`status=active vertices=297936 lightmapped=296490 lightmapEnabled=1`, and five
probe pins on the page itself (`atlas=4096x2251`, `unwrittenTexels=0`,
`darkTriangles=26844`, face tone `darkFaces=5018 zeroFaces=898`, HDR tone
`darkFaces=5303 zeroFaces=940`).

**Mutations**, all five red with the source byte-identical afterwards:

| mutation | reading that went red |
| --- | --- |
| M1 pvs stride-8 read | probe `visClusters` 1692 → 478 |
| M2 brush oriented too (mask 05) | flip census 8514 → 23640, eye oracle 458/594 → 204/594 |
| M3 only brush oriented (mask 02) | top-down oracle 3809/5381 → 5297/5381, misfaced 15126 → 8514 |
| M4 pakfile compared byte for byte | `material-fetch vmtMissing` 3 → 29 |
| M5 no `1/2.2` on the lightmap | W1 `meanLum` 86.3 → 57.3, out of [73,99] |

**One gate change that is not a pin.** `capture()` in
`visual-baseline-check.sh` retried nothing, while `--capture-tick 2000` lands on
2001 or 2003 about half the time on this recording (8 of 13 attempts this
round). A pin measured at tick 2000 cannot be compared with a frame from 2001,
so the helper now retries up to six times for `tick=2000`. Without it the gate
was green or red depending on which side of a tick boundary a run landed.

**Unverified in this round:** the T16/T12 close-out (`task/t12-blend-close`,
`d64b6cc`) is still not integrated, and any gate whose frame pins include the
world region —
`t2-visual-matrix.sh`, `world-render-reliability-check.sh`,
`world-material-effects-check.sh` — was not re-run; T20 owns those after all
branches merge. The shading half of the preset contradiction (§6) is unchanged
by design and has no measurement yet.
