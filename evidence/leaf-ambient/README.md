# evidence/leaf-ambient — what each file is

Written 2026-11-21 with task-37 Half A (the BSP leaf ambient cube reader).
Every file here except the four scripts is the captured stdout of one command,
byte for byte, nothing hand-edited; the probe writes no file of its own, the
shell redirect does. The numbers in `docs/LEAF-AMBIENT-2026-11-21.md` are read off these files.

| File | Produced by | What it is |
| --- | --- | --- |
| `self-test.txt` | `leaf_ambient_probe --self-test` | 28 `{"check":...}` lines and a summary, on a synthetic `BspFixture` built in memory: layout, leaf lookup agreeing with `clusterAt`, the two overloads picking different samples on purpose, empty leaves leaving the caller's value alone, the four page combinations, and three malformed-lump refusals. `failures=0`. |
| `real-cp_process_f12.txt` | `leaf_ambient_probe --map ... --leaves 0` | One `reading` line for the installed map, the `directionStats` line, then one line per leaf that carries a sample (3537 of 5654) with the cube the leaf-centre overload returns. The per-leaf census the doc's magnitudes come from. |
| `real-cp_badlands.txt` | the same command, `cp_badlands.bsp` | The second map: 3286 of 5516 leaves carry a sample. Its smallest cubes (leaf 5515, 1.8e-05) are the low end of the observed range. |
| `real-cp_cloak-no-lumps.txt` | the same command, `cp_cloak.bsp` | The negative control: the one map in this corpus that ships neither ambient lump. Three lines, `mode=unavailable`, every count 0, and the accessors must refuse. |
| `corpus-261-maps.txt` | `leaf_ambient_probe --corpus <tf/maps>` | All 261 installed maps: one `reading` line each, one aggregated `directionStats` line, then the corpus summary. This is the file the `261 / 260 / 0 / 1` page census and the 7.87M-cube direction histogram come from. |
| `demo-cp_process_f12.txt` | `leaf_ambient_probe --demo <demo.dem> --ticks 48850,49500,51000,54000 --entities 24` | A real STV demo: per tick the resolved server tick, then one line per entity with its world origin, the leaf and cluster it lands in, and the cube. Ends with the origin census and the hit/missed counters. |
| `tools/leaf_ambient_survey.py` | hand-written, 2026-11-21 | Independent cross-check of the C++ reader: unpacks the lumps itself (LZMA-aware), walks the per-leaf `(count, first)` pairs, and prints the per-map census as JSON. Its numbers match the probe's on all 261 maps, which is why the layout claim is not one implementation agreeing with itself. |
| `page-diff.txt` | `tools/page_diff.py <map.bsp>`, once per map | The LDR-vs-HDR comparison behind `docs/LEAF-AMBIENT-2026-11-21.md` §4: per leaf, decodes page 52/56 and page 51/55 and compares luminance. One line per map — run it once per map, because `maxAbsLumDiff` is a maximum over the whole run and a later row would inherit an earlier map's worst leaf. |
| `page-magnitude.txt` | `tools/page_magnitude.py <map.bsp>`, once per map | The magnitude table behind §0.5: the largest decoded value on each of the four pages (lightmap LDR 8 / HDR 53, ambient LDR 56 / HDR 55), plus the top six exponents. This is the file that says the ambient page tops out three orders of magnitude below the lightmap page, i.e. the values are linear and small. |
| `samples-per-leaf.txt` | `tools/leaf_ambient_survey.py <map.bsp>`, once per map | The per-map sample census behind §2 and the §4 leaf columns: leaf count, leaves with a sample, out-of-range count, and the full samples-per-leaf histogram. It is the second reader that has to agree with the probe before a layout claim is worth anything. |
| `tools/page_diff.py` | hand-written, 2026-11-21 | The comparison itself: per leaf, decodes page 52/56 against 51/55 and compares Rec.709 luminance. Prints identical / differing / count-mismatch, the max absolute difference and the mean relative difference. |
| `tools/page_magnitude.py` | hand-written, 2026-11-21 | The magnitude measurement: asks each of the four pages for its largest decoded value and its exponent histogram. Kept separate from `page_diff.py` because the lightmap page is 4 bytes per luxel and the ambient page 28 bytes per cube — one stride cannot measure both. |
| `tools/layout_test.py` | hand-written, 2026-11-21 | The sample-position layout test: for every leaf it takes the byte at `+24/+25/+26` as an 8-bit 0..255 fraction of the leaf's `mins/maxs` box and checks it falls inside, which is what rules out the competing layouts (position first, or a 3-float position). |

## Reading the numbers

- `{"summary":"corpus", ..., "outOfRange":0`}: no index record, on any of the 261 maps,
  points past the end of its own sample array. The per-leaf pair layout holds on the whole corpus.
- `"unreferenced":197554` on 9 maps is the one deviation: those maps ship more samples
  than their leaves claim. The reader counts them instead of dropping them silently, because a
  silent drop and a clean parse look identical in every other number here.
- `"leavesWithout":695094` of `1629178` (42.7%) carry no sample, and one map
  carries neither lump (`cp_cloak`; its 696 leaves are in the corpus total 1629874 but not in
  the 695094 / 934084 split, because a map without the lump contributes no count either way).
  That is why the API returns `bool` and not a zero cube: a zero cube
  means "this model is in the dark", which is a different claim.
- `directionStats` is a distribution, not a mean: `brightest[4]` of 3,279,485 beats the other
  five directions on 7868947 cubes. The SDK names no directions, so this is the evidence that
  index 4 is up and index 5 is down — an inference, and the doc says so.
- In the demo file the 20 misses are all `CWorld` / `CMannVsMachineStats` / `CVoteController` sitting at
  `[0,0,0]`, in leaf 1843 whose single sample is black. The misses are the world's index
  entities, not a failed leaf walk — a distinction that only shows up because the file prints
  the origin and the leaf alongside the hit flag.
