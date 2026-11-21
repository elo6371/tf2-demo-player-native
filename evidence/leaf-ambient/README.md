# evidence/leaf-ambient — what each file is

Written 2026-11-21 with task-37 Half A (the BSP leaf ambient cube reader).
Every file here except the three scripts is the captured stdout of one command,
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
| `tools/page_diff.py` | hand-written, 2026-11-21 | The LDR-vs-HDR comparison behind `docs/LEAF-AMBIENT-2026-11-21.md` §4: per leaf, decodes page 52/56 and page 51/55 and compares luminance. Prints identical / differing / count-mismatch and the max absolute difference. |
| `tools/layout_test.py` | hand-written, 2026-11-21 | The sample-position layout test: for every leaf it takes the byte at `+24/+25/+26` as an 0.8 fraction of the leaf's `mins/maxs` box and checks it falls inside, which is what rules out the competing layouts (position first, or a 3-float position). |

## Reading the numbers

- `{"summary":"corpus", ..., "outOfRange":0`}: no index record, on any of the 261 maps,
  points past the end of its own sample array. The per-leaf pair layout holds on the whole corpus.
- `"unreferenced":197554` on 9 maps is the one deviation: those maps ship more samples
  than their leaves claim. The reader counts them instead of dropping them silently, because a
  silent drop and a clean parse look identical in every other number here.
- `"leavesWithout":695094` of `"leaves":1629874` (42.7%) carry no sample, and one map
  carries neither lump. That is why the API returns `bool` and not a zero cube: a zero cube
  means "this model is in the dark", which is a different claim.
- `directionStats` is a distribution, not a mean: `brightest[4]` of 3,279,485 beats the other
  five directions on 7868947 cubes. The SDK names no directions, so this is the evidence that
  index 4 is up and index 5 is down — an inference, and the doc says so.
- In the demo file the 20 misses are all `CWorld` / `CMannVsMachineStats` / `CVoteController` sitting at
  `[0,0,0]`, in leaf 1843 whose single sample is black. The misses are the world's index
  entities, not a failed leaf walk — a distinction that only shows up because the file prints
  the origin and the leaf alongside the hit flag.
