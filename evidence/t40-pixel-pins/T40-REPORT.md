# T40 — final report: pixel pins re-pinned on the current tree, both semantic gates green, three visual gates re-run

Task: T40 (shared task task-2, owner t40). Branch: `task/t40-pixel-pins`
(worktree `D:\TF2_Native_Worktrees\t40-pixel-pins`), based on `34e1714`
(`p0-entity-protocol`). Integration repo untouched by this branch.

State at close: **tree clean**, no `native/` change on this branch, no source
mutation left behind, no product/build/capture command after 12:44:31.

## Commits (oldest first)

| commit | what |
|---|---|
| fa0a509 | forward readings of both gates on 34e1714 BEFORE any re-pin (87 OK / 9 FAIL lighting; 149 OK / 14 FAIL material) |
| c0393b8 | the re-pin itself: every moved pin carries its old value as a superseded comment naming the pre-fix frame, new tick-1200 captures C12/S1_12/S2_12, NOT VERIFIED printing in section 5b |
| 369a401 | forward PASS evidence (both gates rc=0) |
| bf0ac0b | lighting mutation form m1-m4 red, sources restored |
| d667452 | material mutation form m1-m5 red, sources restored |
| 3ee7993 | T25 PASS, T26 one measured red (left red on purpose) |
| 42103db | T12 PASS after building the missing terrain_blend_probe |
| (this commit) | this report |

## The re-pin (only current-tree values)

Every pin below was re-measured on `34e1714`; old values remain in the scripts
as comments with the pre-fix frame they came from
(`A=f87f396a60dc72082f232bcd71547066c02c433e35ccda1576c3b21333ef9608`).
Gate pins are differing **bytes**; the population splits are **pixels**
(classify2 + T26 face-id mask). Both units are labelled.

| pin | pre-T29 | now | population split (px) |
|---|---|---|---|
| lighting dynamic | 1584117/83 | **1776259/77** | old 56015: 56011 occluded, 2 same-face uv, 2 one-byte tie; new 114390: 114389 replaced, 1 same-face |
| lighting specular | 267343/12 | **390361/15** | old 15281: 15241 occluded, 21 same-face, 19 one-byte; new 61499: 61470 replaced, 14 sky, 15 same-face |
| bump under dynamic | 746559/13 | **910776/13** | same shape as dynamic, 56069 occluded / 114639 replaced |
| material bump under dynamic | 624/21 | **79/17** | old 220 occluded, 28 survivors common; new 28 common — shrank by occlusion |
| detail | 82213/60 | **271060/60** | old 16514: 15166 occluded, 28 same-face, **1320 BROKEN**; new 79459: 77940 replaced, 27 sky, **1492 BROKEN**; 96.9% attributed |
| $bumpmap2 (tick 1200) | 15953 (8790/7163) | **7343 (4107/3236)** | old 3858 all on baseline-moved pixels; both directions still present |
| specular at tick 2000 | 135 (123/12) | **0/0, NOT VERIFIED** | population extinct at this camera (S1==S2) |
| NEW tick-1200 bump pair C12→R1 | — | **990/19, 458 up / 532 down** | two-sided, exact pins + assert_ne positive controls |
| NEW tick-1200 specular pair S1_12→S2_12 | — | **344/5, 337 up / 7 down** | two-sided consumer proof, different camera |

The 19 old + 15 new one-byte specular ties and the 2 dynamic ties are ±1 byte
same-face colour ties: at each pixel one frame is byte-identical to its
baseline and the face id does not change. No pin depends on them.

## Forward gates (rc=0 on c0393b8; evidence in 369a401)

```
bash lighting-semantics-check.sh          # 96 OK / 0 FAIL
PASS: six controls measured; three pass switches compared one at a time;
four keys refused by name.
bash world-material-semantics-check.sh    # 183 OK / 0 FAIL
PASS: three material features pinned declaration->lookup->shader input->
pixel delta, the normal read by both consumers at tick 1200 (the tick-2000
specular view is NOT VERIFIED, see section 5b); the null frame is 0 px;
three refusals still named.
```

Deltas: lighting 0 / 1776259(77) / 390361(15) / 0 / 910776(13). Material 0 / 0 /
1776259(77) / 79(17) / 0 / 271060(60) / 7343(26) / 0 / 0 / 0 / 990(19) / 344(5).
Sign lines confirm the direction claims did not move (dynamic brighter=0;
specular darker=0) and the new tick-1200 pairs are two-sided.

Logs: `evidence/t40-pixel-pins/reforward-{lighting,material}.log`,
`reforward-pass.txt`, plus the gates' own `evidence/{lighting,material}-semantics/`.

## Mutation forms (both rc=0, same commit series)

`bash lighting-semantics-check.sh --mutation` -> all four red
  m1 $bumpmap2 state=applied (was refused) + self-test no longer passes
  m2 fixture no longer reads %detailtype
  m3 specular=1 in the dynamic-light-only run
  m4 bump=1 with bumpTexture=- (was 0/-)
  RESTORE=OK ×4 (git hash-object equality)
`bash world-material-semantics-check.sh --mutation` -> all five red
  m1 differing=0 (was 79) while the row still reads applied
  m2 11746 (was 7343) while the row still reads applied
  m3 255344 (was 271060) while the histogram still reads 0x15
  m4 417045 (was 79) while feature-apply still reports materials=45
  m5 rect '512x256' (was 256x256)
  RESTORE=OK ×5
Each run re-ran the forward suite first (96/0 and 183/0), so the reds are on the
same tree and the same pins. Logs: `mutation-{lighting,material}.log`.

## T25 / T26 / T12 (branch 3ee7993, 42103db)

* **T25 visual-baseline**: rc=0, 70 OK / 0 FAIL.
  "PASS: the visual baseline held (probe + trace + four world regions)."
  All four region pins held, so the near clip did not move the object
  acceptance.
* **T26 world-geometry hole**: rc=1, exactly one red, **left red on purpose**.
  `FAIL no sliver had a culled neighbour: world-ids-nocull.bmp is not being read`
  (seamCulled=0; "seam directions that only the cull-off frame could name: 0";
  seam faces 8555,15324,17217,17260,17262,17266,17268,17326). Every other
  assertion passed and the regenerated capture matrix is byte-identical to the
  committed one (only analysis run-tag lines differ).
  Named measured blocker, **owner T41** for the measurement (T43 to record
  NOT PASS): T41 measured that 0 of the 8 named seam faces are derived-back at
  this pose (all 8 derived-front; the cull removes 8 other displacement/side-0
  faces and none of the seam faces), so `seamCulled=0` is the correct reading on
  this tree and the old `-ge 1` pin encoded a pre-T29 artefact. The pin was NOT
  relaxed or deleted.
  Cheapest next step: replace the `-ge 1` pin with the measured assertion
  "0 of the 8 named seam faces are derived-back at this pose" plus a positive
  control that the cull removes ≥1 non-seam face; that is a T26 ownership change
  (T41/T43), not a T40 pin edit.
* **T12 terrain-blend**: first attempt FATAL `missing terrain_blend_probe.exe`
  (build gap, logged, not a result). Then build + run: rc=0, 94 OK / 0 FAIL,
  `TERRAIN-BLEND=PASS`; changes 277523/277523 inside the terrain region, 7
  terrain-pair pixels hidden behind a drawn surface; the T35 two-arm fix passes.

## NOT VERIFIED carried into T43 (printed by the gates, not absorbed by pins)

1. **Specular at the tick-2000 reference camera**: 0/0, population occluded by
   the near geometry T29 revealed. The tick-1200 pair is a consumer proof on a
   different camera, NOT a replacement. Owner T43. Cheapest next step: one
   wide-area diagnostic pose (T35 prepared nine material-framed poses) and pin
   the pair there.
2. **Detail residual 2,812 px** (1,320 old + 1,492 new, 0.33% of the frame) on 27
   non-displaced faces, top face 7536 = WOOD/WALL007A (1,315 px); the baseline
   frame is byte-identical there and the depth control is unusable
   (`HOLE depthControl … saturated=1`), so the mechanism is not established and
   the 271,060 pin does not absorb it. Owner T43. Cheapest next step: restore the
   pre-fix near plane with the T29 gate's own mutation, capture F at tick 2000,
   diff against the pre-fix F.
3. **Material bump direction at tick 2000** is one-sided (79 brighter / 0 darker)
   because the pre-fix population was mostly occluded; the two-sided claim is
   pinned at tick 1200 (990; 458/532) with assert_ne controls.
4. **T26 seamCulled red** (above), owner T41/T43.

## Merge risk / notes

* No `native/` file is touched by this branch; the merge is gate scripts +
  evidence only. The only conflict surface is the two gate scripts, which are
  also edited by T26/T35 branches — resolve by taking T40's pins only if the
  tree they were measured on (`34e1714` + T29 near clip) is the merged tree.
* The binaries on disk were rebuilt by the later holds from the same sources;
  the forward frames were reproduced identically across builds (C12/S1_12/S2_12
  hashes match the pre-run diagnostics and A==A2 in every run), so the pins are
  not build-specific.
* Pre-fix dumps used for classification live in
  `evidence/t40-pixel-pins/forward-readings.txt` (section 3) and
  `detail-residual-face-info.txt`; scratch classification scripts are untracked
  by design.
