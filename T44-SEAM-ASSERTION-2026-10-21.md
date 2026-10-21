# T44 — the T26 seam assertion, corrected with a positive cull control

Date: 2026-10-21. Branch `task/t44-seam-assertion`, base `6888e6debfe7`.
Worktree `D:\TF2_Native_Worktrees\t44-seam-assertion`. Owner `t44`.
Gate as committed: `world-geometry-hole-check.sh` sha256
`0e7da591b5ea66e1e74c3ba24a128116a0b9ffe94ffb0a5c1129f7ee0cb0d462`.
Evidence commit: `0cc2cda`; this report is the next commit on the same branch.
Nothing was merged or pushed.

## 1. What was wrong

Section 8 asserted `SEAM_CULLED >= 1` — "at least one sliver had a culled
neighbour", counted as seam directions that resolve only in the cull-off
frame. On `6888e6d` it is red:

```
  seam directions that only the cull-off frame could name: 0
  FAIL no sliver had a culled neighbour: world-ids-nocull.bmp is not being read
```

The red is preserved verbatim in
`evidence/t26-world-holes/analysis/forward-before-t44.log` (run 16:04:44–16:20:35,
15 captures at tick 2000, exactly one red). It is the correct reading of this
tree, not a broken instrument: after the T29 near-plane fix no surviving sliver
is bracketed by a surface the product culls, and the T41 derived rule says why —
all eight named seam faces are derived-FRONT at the capture camera, so a correct
cull cannot remove them. The old pin encoded a pre-T29 artefact.

The pin is therefore **replaced, not relaxed**: it is superseded in the gate
header (lines 99–120) by four measurements that can fail independently, and one
mutation (M6) that proves the replacement is about geometry rather than a
disabled cull. No threshold was widened and no assertion was deleted.

## 2. The "32/32" documentation error

`NEXT-TASKS-2026-10-21.md` line 190 says "32/32 directions resolve". The gate's
own T40 log contradicts it: `evidence/t40-pixel-pins/t26-hole.log` lines 51–55
carry **6** `HOLE seam` lines (6 points × 4 directions = 24 probes) and **13**
face-info requests, and `evidence/t40-pixel-pins/T40-REPORT.md` (lines 99–107)
repeats the 6-face reading. The 32 does not exist in any artifact. T44:

- asserts the measured total and prints it (24/24), pinning `PIN_SEAM_PROBES=24`;
- keeps the 32/32 claim as superseded documentation (gate header, this report);
- does **not** invent probes to reach 32, and does not edit the shared tracker.

## 3. The new section 8

1. **Pose at the frame, not at the run.** The eye comes from the trace's last
   `scene: camera` line before `capture: armed ... frame_tick=2000`, recorded by
   `seam-cull.py eye`; the once-per-run `gpu: world vis eyeMap` line (the first
   drawn frame, and T41's original mistake) is printed as
   `EYE superseded-eyeMap absent`. The gate asserts `src=demo`, `tick=2000` and
   the eye value.
2. **Seam census.** 6 representative points, 4 directions each = 24 probes;
   every measured probe must resolve to a face id; the measured total is printed
   with the verdict. The eight named faces are pinned.
3. **Derived side.** For each named face, the T41 rule at that eye (D3D11
   `CULL_BACK`, `FrontCounterClockwise=FALSE`, front iff
   `dot(emittedNormal, eye-p) > 0`; brush fan emits the plane normal, displacement
   flipped onto outward): assert 0 derived-back and 8/8 parsed.
4. **Positive control.** At the same pose, compare `world-ids.bmp` against
   `world-ids-nocull.bmp`: the culled frame must be a subset (onlyInCull=0), the
   cull must remove ≥1 pixel from ≥1 named non-seam face, and the largest such
   face's id, kind, BSP side bit, derived side and pixel count are pinned; that
   face must not be one of the eight seam faces.
5. **Mutation M6.** Force `renderer.setWorldBackfaceCull(false)` in
   `native/src/main.cpp`, rebuild, capture the face-id frame with the demo's own
   cull request. The control must find 0 removed pixels (red), the frame must stay
   a subset of the unculled one, and the 8/8 derived-front classification must
   **not** move — proving the seam answer is a BSP+eye reading, not a cull
   artefact. Sources are then restored and the restored build must reproduce the
   pinned textured frame byte for byte.

## 4. Readings (measured, all PASS)

Pose (`EA` lines, evidence/t26-world-holes/analysis/seam-eye.txt):

```
EYE eye=-2294.8,348.8,383.4 src=demo tick=2000 armed=42068.5 capture: armed requested=2000 frame_tick=2000 frames=1431
EYE camera-line 42066.6 scene: camera mode=first-person target=1 follows=1914 holds=0 freeze=0 eye=-2294.8,348.8,383.4 pitch=-8.43 yaw=64.27 src=demo
EYE superseded-eyeMap absent
```

(The two camera lines before it are the previous demo tick, `-2299.3,341.0,383.4`,
and are not used.)

Seam census: 6 points / 24 probes / 24 resolved / 0 unresolved / 0 cull-off-only
directions; faces `8555,15324,17217,17260,17262,17266,17268,17326`.

Derived side at that eye (evidence/t26-world-holes/analysis/seam-derived.txt):

| face | kind | side | planeSD | emittedDot | derived |
|---|---|---|---|---|---|
| 8555 | brush | 0 | 87.964 | 87.964 | front |
| 15324 | brush | 1 | 758.800 | 758.800 | front |
| 17217 | disp | 0 | 59.114 | 59.114 | front |
| 17260 | disp | 1 | -2462.146 | 2462.146 | front |
| 17262 | disp | 1 | -2462.146 | 2462.146 | front |
| 17266 | disp | 1 | -2462.146 | 2462.146 | front |
| 17268 | disp | 1 | -2462.146 | 2462.146 | front |
| 17326 | disp | 0 | 383.400 | 383.400 | front |

`CLASS-SUMMARY faces=8 parsed=8 derivedFront=8 derivedBack=0`.

Positive cull control (evidence/t26-world-holes/analysis/cull-control.txt):

```
CULL state=ok frames=1264x681 kept=777918 removed=81218 onlyInCull=0 distinctRemoved=19
CULL-SUMMARY removed=81218 onlyInCull=0 distinct=19 nonSeam=18 seamFacesRemoved=1 topNonSeam=17605 topNonSeamPixels=33167 topNonSeamKind=disp topNonSeamSide=0 topNonSeamDerived=back
```

Top non-seam removals: 17605 33,167 px; 17672 25,675; 17674 15,739; 17330 1,765;
17327 1,676; 17259 1,132; 17258 843; 17256 337; 17328 304; 17331 249; 17304 190;
17299 73; 17320 28; 16957 22; 2574 6. Control face 17605 is a displacement face,
BSP side 0, planeSD -0.600 → derived-back, i.e. exactly the kind of face the
product cull is supposed to remove; it is not one of the 8 seam faces.

Honest edge case, printed and not hidden: one pixel of seam face 17266 is
removed by the cull at a silhouette edge (derived-front, side 1). It is a
raster/face-id attribution edge, not a culled front surface; the account reports
`CULL seam-removed face=17266 pixels=1 ... derived=front seam=1`.

Face info: 13/13 requested resolve, 0 absent, 6 displacement + 5 sky.

## 5. Gates run (under hostlock owner `t44`)

| command | run | rc | log |
|---|---|---|---|
| `bash world-geometry-hole-check.sh` (before the change) | 16:04:44–16:20:35 | 1 | `analysis/forward-before-t44.log` |
| `bash world-geometry-hole-check.sh` (after) | 16:36:08–16:39:58 (3m50s) | 0 | `analysis/forward-t44.log` |
| `bash world-geometry-hole-check.sh --mutation` | 16:47:43–16:57:19 (9m34s) | 0 | `analysis/mutation-t44.log` |

The mutation run re-ran the full forward gate first (all PASS) and then M1–M6;
the capture matrix resumed from 18 committed frames (0 capture files changed in
git). Forward log ends:
`PASS: every empty pixel ... the one winding repair the oracle can see is a measured regression.`
Mutation log ends:
`PASS: all six mutations turned the gate red and every source came back byte-identical.`

## 6. Mutations M1–M6

| id | mutation | observation that makes the gate red |
|---|---|---|
| M1 | parser keeps the sky count, drops the polygons | product refuses to draw: exit 14, `reload-stage stage=models lastError=0x80070057`; no frame |
| M2 | sky pass uploads the world triangles instead | `sky=0 unexplained=1648 enclosedUnexplained=1648` |
| M3 | face-id view returns a constant | `HOLE-POINT-SUMMARY points=9 failures=9` |
| M4 | sky mask gets its own transform | `HOLE-POINT-SUMMARY points=9 failures=2` |
| M5 | account reads empty off the product's picture | `missing=82866 sky=82866 enclosed=6499` |
| M6 | world back-face cull never runs | cull control `removed=0 onlyInCull=0` (red) while seam classification stays `derivedFront=8 derivedBack=0` |

After **each** mutation: `RESTORE=OK (four files byte-identical)` (cmp against
pre-mutation copies). Whole-suite sha256 before == after:

```
6be8b2430b1e61be91d2391023104c47760c774974ecbcaa1214f8de96ef69aa *native/src/bsp_map.cpp
f8badad54adce891111fdbe4f99562f1321227a83bbbcb58ec3b8f360ff27bbd *native/src/main.cpp
85b00d685bbd4d63270850293bc6a173ccf738ac15f11104945457ca185dcd34 *native/src/native_renderer.cpp
715fe65a7677b8ce4dc685ce5b674d79454277694365936e01b6c8adc7c97dbe *world-geometry-hole-account.py
```

`git status --porcelain native/` after the suite: 0 lines. The restored build
(not the mutant) reproduces the pinned `textured.bmp` byte for byte.

## 7. Evidence added

`evidence/t26-world-holes/analysis/`:
`forward-before-t44.log` (pre-change red), `forward-t44.log`,
`mutation-t44.log`, `seam-eye.txt`, `seam-derived.txt`, `cull-control.txt`,
and refreshed `identity.txt` / `point-trace.txt`. The 18 capture frames are
byte-identical to the commit (no capture file changed).

## 8. NOT VERIFIED

1. **Per-pixel derived side.** The gate asserts the face-level rule. T41 recorded
   ~51,890 px on derived-back faces inside the cull-on frame; T44 does not
   re-derive per pixel, and the 1 px of seam face 17266 removed by the cull is a
   silhouette-edge attribution, recorded above, not explained per pixel.
2. **Eye precision.** The trace prints the eye to 0.1 map units; the smallest
   margin among the eight faces is |dot| = 59.114, so the classification is far
   from a boundary — but a camera change moves the pin by design.
3. **Stale header numbers, pre-existing.** The gate header's items 2/3 still
   quote the pre-T29 baseline (38,328 missing px, 2,313 enclosed px) while the
   pins and assertions use 1,648. These lines predate T44; they were left
   byte-identical so the committed gate is exactly the artifact that was run.
   Flagged for the T26 owner.
4. **identity.txt churn.** It records exe sha256 values that T39 found not
   reproducible across rebuilds (source hashes are stable), so every gate run
   rewrites `identity.txt` and the trace timestamps in `point-trace.txt`. T49's
   chain will therefore dirty those two files; that is expected, not drift.
5. **Cold cost.** Section 8 now requires `captures/traces/world-ids.trace`. On a
   fresh checkout the capture matrix regenerates 18 frames (~13 min) before the
   trace exists; a missing trace now fails loudly by design. All runs under lock.

## 9. Merge risk

Touched: `world-geometry-hole-check.sh` (the only shared executable) and
`evidence/t26-world-holes/analysis/*` (logs + refreshed identity/point-trace).
No product source, no other gate, no pins outside T26, no capture frames.

- **T49/T50 chain:** must run the chain from the integration tree; the T26 step
  will now assert the new pins (81,218 removed / 33,167 px on 17605) and require
  the trace. If the integration tree's camera or world cull moved, the pins move
  legitimately — re-pin from a measured run, do not widen.
- **Evidence churn:** `identity.txt` line 1 is the HEAD hash, so the merge
  commit/chain rerun rewrites it (and point-trace timestamps). Expected.
- **Conflicts:** unlikely unless another task edits the same T26 analysis
  directory; the gate is not shared with T45–T48.
- **Merge order:** independent of the other wave branches; land before the final
  chain so the chain validates this gate. Nothing here is merged or pushed.
