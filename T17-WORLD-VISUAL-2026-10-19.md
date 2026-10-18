# T17 — World Visual Closure: what the reference frame is made of

Task `T17` (`NEXT-TASKS-2026-10-19.md`), owner rendering specialist. This round
turns the fixed reference frame from "a flat fragment with holes" into a picture
whose every population is named, fixes the three causes that were measured, and
leaves the rest as readings rather than adjectives.

**Status: code and diagnostics complete, formal gate run queued.** Per the
schedule set at 2026-10-19 00:44 (T16/T12 close-out integrates first; every
build, capture and mutation on this machine runs one at a time), the three
required gates have not been run against this tree yet. §8 lists exactly what is
waiting and what it is expected to move.

## 1. Identity

| | |
| --- | --- |
| task | T17 (World Visual Closure) |
| branch | `task/t17-world-visual` |
| worktree | `D:/TF2_Native_Worktrees/t17-world-visual` |
| base commit | `e67630b` (merge of `task/t15-vpk-build` into `p0-entity-protocol`) |
| source commit | `73ee6b9` |
| pushed | no (queued behind T16 integration) |
| product scope | `bsp_map.{h,cpp}`, `world_material.cpp`, `native_renderer.{h,cpp}`, `main.cpp`, `settings_store.cpp`, `world_geometry_probe.cpp` |
| evidence | `evidence/t17-world-visual/` (5 frames, 5 trace excerpts, probe output, 2 classification maps) |

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
| `ref2.bmp` / `ref2.png` | the reference frame before the sky fix: materials and light fixed, 20.2% clear colour |
| `sky.bmp` / `sky.png` | the same tick with `--world-sky`: hole 0.0004 |
| `ref2-class.png`, `sky-class.png` | the three populations painted magenta/red/green |
| `flat2.bmp`, `terrain2.bmp`, `none2.bmp` | flat geometry, terrain only, nothing |
| `trace-*.txt` | the `world material-fetch` / `blend-fetch` / `blend enabled` / `lightmap` / `sky-upload` / `vis` / `cull` lines of each frame |
| `probe.txt` | the geometry probe on `pl_odyssey` with the two tone histograms and the HDR comparison |

## 8. Queued: what runs when the machine is free

Nothing below has been executed against `73ee6b9`.

1. `bash build-target.sh tf2_demo_native world_geometry_probe` — expect `rc=0`.
2. A default-path capture at tick 2000 and a `--world-sky` capture at the same
   tick. **Expected**: byte-identical frames (`sky.bmp` is therefore the
   default frame), because `skyEnabled` reads `persistent.render.skybox`, which
   `loadSettings` now sets from the file's preset. If they differ, the loader
   fix is incomplete and the frame is not reproducible.
3. Two captures of the same settings — the determinism acceptance. Expected
   byte-identical (sha256).
4. `bash visual-baseline-check.sh` — **expected to fail on pins, not on
   behaviour**, and each moving pin has a measured reason:

   | pin | old | expected | why |
   | --- | --- | --- | --- |
   | `material fetch tiled` | 57 | 64 | pakfile textures now resolve |
   | `material fetch fromPakfile` | 23 | 59 | 36 more materials found in the pakfile |
   | `material fetch vtfTooLarge` | 43 | 54 | materials that were missing now reach their vtfs |
   | region W1..W4 `meanLum`/`stdLum` | T11 values | higher | the lightmap is now composed in display space |
   | region `nearBackground` | ~0 | unchanged | geometry coverage did not move |

   `distinct=108`, `capped=21`, `vmtNoBase=20`, the whole vis block, the cull
   line and the geometry block must **not** move; if any of them does, the fix
   is wrong and the pin is right.
5. `bash visual-baseline-check.sh --mutation` — M1/M2/M3 unchanged, plus the
   two T17 mutations to add: **M4** reverts the pakfile normalization
   (`material-fetch vmtMissing` must climb back off 3) and **M5** removes the
   `1/2.2` encode (the region luminances must fall out of their new pins).
6. `bash world-material-check.sh` and `--mutation` — the probe's readings moved
   only additively (`lightmap-face-tone`, `lightmap-hdr-tone`,
   `lightmap-tone darkMaterials`); its pinned sky readings are from
   `world_material_probe`, which this round did not touch.
7. `bash frame-capture-check.sh` — no product flag used by it changed.

Explicitly unverified in this round: the four gates above, the T16/T12
terrain-only close-out (`--world-geometry terrain` works on this base —
`terrainOnly=1`, `vertices=85596` — so the earlier `rc=14` does not reproduce
here), and any other gate whose frame pins include the world region
(`t2-visual-matrix.sh`, `world-render-reliability-check.sh`,
`world-material-effects-check.sh`), which T20 owns after the merge.
