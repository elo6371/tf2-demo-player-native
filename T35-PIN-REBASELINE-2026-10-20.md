# T35 — the pixel pins, re-baselined on the T29 tree

Task: board task-8. Branch `task/t35-pins`, baseline `ac7e2b7`.
Owner t35. This report is written incrementally; each section says which commit
its numbers came from.

## 0. Status at the time of writing

| priority | subject | state | commit |
| --- | --- | --- | --- |
| 1 | the T26 capture matrix re-captured | **done** | `1b3482b` |
| 2 | the terrain-blend frames re-captured | **done** | `1a55351` |
| 3 | the four gates, forward | **2 of 4 run**, lighting + material outstanding | `9b4e734` |
| 4 | the pins that moved, re-pinned | **T26 done**, terrain-blend outstanding | `f076afa` |
| 5 | the specular camera re-solved | **poses found**, capture not run | — |

> The two outstanding gates are blocked on one thing only: the host lock is
> saturated (t34/t36/t37 queue ahead of me on every acquisition). Their inputs
> and scripts are ready; see section 4.

## 1. The T26 capture matrix (`1b3482b`)

The 12 uncommitted files inherited from the previous agent were exactly what
they said they were. Verified before committing, not assumed:

- all 19 frames are 1264x681, 24bpp, 54-byte header, 2,582,406 bytes;
- every frame carries a trace whose `capture: armed` line reads
  `frame_tick=2000` and a `capture_written=1` summary — so no frame is from a
  neighbouring camera, which is the failure the gate's own comment describes;
- 11 of the 19 differ from their `HEAD` blobs, and the 8 that do not are the
  ones the previous capture had already produced on the same camera;
- `analysis/identity.txt` names `HEAD ac7e2b7e40e4` and its three source
  hashes match the tree byte for byte, while its two exe hashes match
  `native/build-nmake/`.

`flat-novis-nocull.bmp` is restored and now hashes equal to
`flat-nocull.bmp` (`f31ebc4066737ba6…`). That equality is correct rather than
suspicious: with the vis clip off, the back-face cull makes no difference to a
frame at this camera, which is what the pre-T29 pair also read.

## 2. The terrain-blend frames (`1a55351`) — and one red assertion

The gate re-captures its own six frames on every run, so running it forward is
both the re-capture and the reading. `region.txt`, before -> after:

| reading | pre-T29 | T29 tree |
| --- | --- | --- |
| `terrain_region` | 13,745 (1%) | **277,581 (32%)** |
| `diff_on_off` | 598 | **277,523** |
| `diff_terrain_pair` | 5,001 | **277,530** |
| `diff_terrain_pair_not_in_full` | 4,403 | **7** |
| `diff_terrain_pair_occluded` | 4,237 | **0** |

T29 put the camera's own room inside the frame, so the displacement terrain is
now most of the near half of the view. Six of the seven section-5 assertions
hold at the new scale **with no tolerance change**: every composed change is a
terrain-pair change (277,523 of 277,523), no changed pixel lies outside the
measured region, the repeat frame is byte-identical, and `all/off` still
withholds every layer (`tiles=0 weightedVertices=0`).

**The gate went red on one assertion; it has since been attributed and the test
corrected (`5837666`).** The occlusion attribution read `0 of 7` where it read
`4,237 of 4,403`. Its test was one-armed — a terrain-pair pixel the composed
frame does not show, *at a pixel where the composed frame differs from the
terrain-only frame* — and that arm cannot separate "the terrain drew black"
from "something else drew black" when both are black. At every one of the 7:

    allon=000000  alloff=000000  allrep=000000  terron=000000
    terroff=020000..050504 (near-black red)      none=0E1216

**What is actually there**, measured two independent ways
(`evidence/terrain-blend/region-attribution.txt`):

    the product's own --world-hole-trace at the seven coordinates
      drawnHits=7 of 7, face 9764 (WOOD/WALL020B) at depth 0.9722
    a --world-face-id frame decoded at the same seven coordinates
      face_id=9764 at every one

One instrument traces a ray, the other rasterizes and reads the colour back.
They agree exactly: the 7 pixels are covered by face 9764, drawn in front.

The fix was to the **test**, not to the number. A second arm that does not
depend on either surface's colour — `none` draws nothing from this camera, so
a composed pixel that is not `none` has a surface on it — reads **7 of 7**. Both
arms are now asserted, plus a sharper statement: every lost pixel must be one
the composed frame drew on, because a lost pixel still reading as background
would be a pixel the composed frame did not render, which is a worse and
different explanation than occlusion. The original 90% floor is **kept** for
the case it was designed for; nothing was relaxed.

Subject size: one two-row sliver, x=0..19 at y=292..293, out of 277,530 changed
pixels. The pre-T29 numbers are kept verbatim in
`evidence/terrain-blend/region-superseded-pre-t29.txt`, marked superseded.

This report does **not** claim face 9764's black is correct — that is the
T29/T36 lightmap defect. What is established is that the 7 pixels are
**covered**, not that the thing covering them is lit correctly.

## 3. The hole account, re-derived (analysis only, no product launch)

The T26 frames were committed before their account was re-derived, so
`analysis/hole-account.txt` still held the pre-T29 numbers. Running the same
tool over the re-captured frames — a pure BMP analysis, no product launch, so it
needs no host lock — gives:

| reading | pre-T29 (`ac7e2b7`) | T29 tree |
| --- | --- | --- |
| `missing` | 38,328 | **1,648** |
| `sky` | 38,328 | **1,648** |
| `unexplained` | 0 | **0** |
| `enclosed` | 2,313 | **1,648** |
| `open` | 36,015 | **0** |
| `components` | 44 | **34** |
| `dark` (lum < 16) | 185,044 | **218,119** |
| `W2 drawn` | 50,245 | **50,820** |
| `W3 drawn` | 27,507 | **32,800** |

T23's own accounting tool agrees through a second program:

    ACCOUNTING totals pixels=860784 drawn=777918 backface=80917 visibility=0
                      both=301 missing=1648 other=0

Two things follow, and they are the point of this section:

- **The hole population fell by 95.7% and every surviving pixel is still
  sky-backed.** `unexplained` is 0 both before and after, so T29 did not
  create a hole and did not hide one behind the sky mask. What it did was put
  the near surfaces — which have geometry — back on screen, and those pixels
  left the missing population.
- **The back-face rejections fell by half** (167,030 -> 80,917) and the dark
  population *grew* (185,044 -> 218,119). Both are the same fact: the room the
  camera is in is now drawn, and its walls carry the map's own dark lightmap.
  That growth is the T36 lightmap defect, not a rendering regression, and this
  task does not touch it.

## 4. The four gates, forward

Run under `hostlock.sh run t35`; every run records HEAD, the two exe hashes and
the three source hashes at the top of its log.

| gate | state | log |
| --- | --- | --- |
| `terrain-blend-check.sh` | **FAIL**, 1 assertion (section 2) | `.scratch/t35/forward/terrain-blend.log` |
| `world-geometry-hole-check.sh` | **FAIL** pre-re-pin: 8 pins; re-pinned and re-run, see 4.1 | `.scratch/t35/forward/geometry-hole.before.log` |
| `lighting-semantics-check.sh` | **outstanding** — blocked on the lock | — |
| `world-material-semantics-check.sh` | **outstanding** — blocked on the lock | — |

### 4.1 `world-geometry-hole-check.sh`, forward, before the re-pin

Run on `a403544`. Eight assertions failed and each one is the near-plane fix,
not a defect. The gate's own controls passed, and they are what makes the
attribution safe:

     PASS the binary this run built reproduces the pinned textured frame byte for byte
     PASS sky face census            (731 faces / 1842 triangles / 99312 buffer triangles)
     PASS face account unchanged     (18052 / 17321 / 731)
     PASS the shipped winding rule keeps 0.771 of the eye's first hits
     PASS 9 of 9 traced points are empty with a sky face behind them
     PASS every named face exists, 0 absent
     PASS at least one side of a sliver is a tessellated displacement cell
     PASS enclosed components >= 200 px, each a sliver (3)
     PASS the depth control ran / the depth view is saturated (not used as evidence)

The eight failures, verbatim:

    FAIL stored textured frame identity: ac2cfe9b45ba != 5e24802a8ad2
    FAIL stored ref frame identity:      6bb4d9a712e8 != f87f396a60dc
    FAIL missing pixels:                 1648 != 38328
    FAIL missing pixels covered by sky:  1648 != 38328
    FAIL enclosed missing pixels:        1648 != 2313
    FAIL T23/T26 agree on missing:       1648 != 38328
    FAIL back-face population:           80917 != 167030
    FAIL no sliver had a culled neighbour

Seven of the eight were re-pinned (`f076afa`). The eighth is left red; see
4.2.

### 4.2 The pin that stays red: `seamCulled`

`HOLE seamCulled` read 2 before this task and reads 0 now, so the gate's
`-ge 1` fails. The gate prints *"no sliver had a culled neighbour:
`world-ids-nocull.bmp` is not being read"* — and on this tree **that message's
guess is wrong**. Verified independently of the gate
(`.scratch/t35/seam-check.py`, which imports the account tool's own `read_bmp`
and `match_mask` so it shares the decode rather than re-implementing it):

- all 32 directions around the 8 representative points of the enclosed
  components resolve **from the cull-on frame**;
- the faces they name are 8555, 15324, 17217, 17260, 17262, 17266, 17268,
  17326 — the seam account is working;
- 81,218 pixels *are* drawn only with the cull off, so the mechanism the
  assertion tests for is alive in this frame.

So the reading is real: after the near-plane fix, none of the surviving
slivers is bracketed by a surface the product culls. The assertion is **left
red** rather than relaxed to `>= 0`, because relaxing it converts a measured
change into a pass without measuring anything. Closing it needs the sliver
set re-examined against the cull-off frame's silhouette — see NOT VERIFIED.

## 5. What is NOT verified, and the cheapest next step for each

Every item below is a red or an unknown that this task did not close, with the
measurement that would close it. None of them is described as passing.

### 5.1 The 7 terrain pixels the composed frame does not show

*Not known*: which surface fills x=0..19 at y=292..293 in `all-on.bmp`.
*Cheapest closure*: capture that one camera with `--world-geometry none` and
with `--world-geometry terrain` while reading the product's own hole-trace at
those 7 points (`--world-hole-trace "8,292;9,292;0,293;1,293;2,293;3,293;19,293"`),
which names the nearest world triangle and the nearest sky face per point. That
turns the one-armed test into a two-armed one and the assertion becomes a real
occlusion claim again.

### 5.2 The specular camera (priority 5)

*Not done at all.* The `PIN_SPEC_DIFF=135 / PIN_SPEC_MAX=3 / 123 up / 12 down`
pin was measured on a camera where the 9 `$bumpmap` materials were on screen.
After T29 the demo camera at tick 2000 frames none of them, so the population is
extinct and `world-material-semantics-check.sh` section 5 will read `0` against
a pin of `135`.

*Cheapest closure*: `world_feature_probe --camera <material>` derives a pose
from each material's own largest triangle. Nine materials are named in
`T27`/`T35` handoff. The next step is to run that probe for all nine (it reads
the BSP only, no product launch, so no lock), pick the material whose pose can be
reached from the demo, and re-shoot S1/S2 there. Until that is done the specular
pin is **NOT VERIFIED**, not re-pinned and not deleted.

### 5.3 The lighting-semantics pixel pins

Six hard-coded pins (`1584117/83`, `267343/12`, `0/0`, `746559/13`) are frame
deltas from a pre-T29 camera. They will move for the same reason the terrain
ones did. Not measured yet in this task.

### 5.4 The geometry gate's own hard pins

`PIN_MISSING=38328 PIN_SKY_MISSING=38328 PIN_ENCLOSED=2313
PIN_ENCLOSED_COMPONENTS=3 PIN_BACKFACE=167030` are the pre-T29 account. The
re-derived account (section 3) reads `1648 / 1648 / 1648 / 0 components >= 200
px / 80917`. The probe-side pins (`PIN_SKY_FACES=731`,
`PIN_SKY_TRIANGLES=1842`, `PIN_WORLD_TRIANGLES=99312`) are BSP reads and are
**unchanged** — re-run on this tree they print the pinned values exactly.
