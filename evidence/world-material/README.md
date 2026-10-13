# evidence/world-material — what each file is

Written 2026-10-13 with the T3 world material and lighting task. Most files here
are the captured stdout of one command and nothing is hand-edited; the exceptions
(the analysis note and the PNG conversions, added late in the round) say so in
their own table rows. The gate that writes the captured files is
`world-material-check.sh` (8 sections, forward and --mutation).

| File | Produced by | What it is |
| --- | --- | --- |
| `self-test.txt` | `world_material_probe --self-test` | Two lines. The first is the pre-existing self-test, byte-identical because `verify-fast.sh` pins seven substrings of it. The second is the capability layer's pure-function test. |
| `fixture-straight.txt` | `world_material_probe --fixture` | Nine synthetic BSP fixtures read back through `BspParser::parse`. 90 assertions, 0 bad. |
| `fixture-mutation.txt` | `world_material_probe --fixture --mutation` | The same fixtures with one deliberate break each. 120 assertions, 7 mutations, 0 bad. |
| `real-cp_snakewater_final1.txt` | `world_material_probe --bsp ... --tf-root ...` | The six capabilities measured on the installed map: every `{"reading": ...}` line the gate pins. |
| `negative-bagel.txt` | the same with `koth_bagel_rc13` | The negative control: a map that is not installed reads every capability unavailable, each with its own reason. |
| `render-lightmap.txt` | the product's `TF2_NATIVE_TRACE` | The GPU side: lightmap status and vertex pair, the displacement and water counts taken while the vertex buffer was filled, and the VIS line with the observer's cluster. |
| `render-cubemap.txt` | the product's trace, two runs | One line with the material's own answer (`mode=0 declared=0`) and one with `--world-env-force` (`mode=2 declared=1`), same map, same preset. |
| `render-frame-shape.txt` | hand-written analysis note (2026-10-13) | The frame-shape record behind the frame-capture gate's criterion change: the six captures of two rounds, their pairwise byte distances, the paused frame's attribution, and the tool bug this record sits next to. Reproducible from the frames with the scratch loaders it names. |
| `render-frame-fallback-decal.png` | PNG conversion of `evidence/frame-capture/demo.bmp` | The fallback picture this build draws when a demo's map is not installed (139121 colours). The negative pole of the comparison. |
| `render-frame-installed-map-pre-t3.png` | PNG conversion of the 2026-10-12 round's `demo-installed-map.bmp` | The snakewater world before per-face lightmaps: flat shades, 563 colours. |
| `render-frame-installed-map-t3.png` | PNG conversion of `evidence/frame-capture/demo-installed-map.bmp` | The same scene after: lightmapped, 15909 colours. |
| `before/` | an earlier run of the same gate | The readings **before** this task's fixes: `cubemap.state=fallback`, `samplesComplete=0`, no sky textures. Kept because the change is the finding, and a before/after pair is what makes it one. |

## Reading the numbers

- `cubemap.samplesComplete=33` of 33, `facesDecoded=198`, `fromPakfile=33`: every
  map cubemap decoded all six faces, all from the BSP's own pakfile.
- `cubemap.firstFacesInFile=7` against `firstDeclaredFaces=6`: the file holds seven
  face slots where the header's own rule declares six. Both are pinned.
- `cubemap.firstFaceMeans=62,45,56,58,51,54` and the checksum line: the pixels the
  decoder returned. Counts cannot catch a decoder that walks the image section at
  the wrong stride; these can, and were measured twice to prove it.
- `sky.smallestSize=1x1`: `sky_well_01dn.vtf` is a stub in this install while its
  five siblings are 512x256 and 512x512.
- `hiddenTriangles=26936` with `clipApplied=0`: how much of the uploaded geometry
  the PVS hides from the observer's own cluster, and the fact that nothing clips it.

## One claim with a narrower scope than it reads

`native/src/main.cpp` carries a comment for `--world-env-force` saying that no
installed map's first material declares `$envmap`, "measured across all 68 maps
in this install". The scan behind that number classified **68 of the 254 files in
`tf/maps/`**: the other 186 were skipped by the scan's own guard when a map's
first texinfo entry did not resolve to a material name, so 68 is what was read,
not what is installed. None of the 68 has an env-declaring first material, and one
(`mvm_mannworks`, `DEV/REFLECTIVITY_40`) names a material whose VMT is not
installed at all -- but the claim is about 68 maps, and the comment should be read
that way until a scan that classifies all 254 replaces it.
