# T11 Follow-up — World Geometry: the winding residual, measured

Task `T11` (`RELEASE-BLOCKERS-NEXT-WAVE-2026-10-17.md`), continuation round.
The first round (commits `3289111`, `622a8c9`, `a6e0b74`) fixed the pvs row table
and the per-class winding and delivered the two fixed views. This round attacks
the one geometry limitation that round left open: **"top-down right side still
culls"**, and the question underneath it — is the winding rule right?

## 1. Identity

| | |
| --- | --- |
| task | T11, follow-up round |
| branch | `task/t11-world-geometry` |
| worktree | `D:/TF2_Native_Worktrees/t11-world-geometry` |
| base commit | `a6e0b74` (test: refresh the gate-run evidence from the T11 build) |
| final commit | `HEAD` of the branch after this round (see §8) |
| product scope | `native/include/bsp_map.h`, `native/src/bsp_map.cpp`, `native/tools/world_geometry_probe.cpp`, `visual-baseline-check.sh` |

No file outside the T11 scope was touched. No `evidence/` directory belonging to
another task was written; `evidence/visual-baseline/` is written by its own gate,
which this task owns.

## 2. What changed, and why

| file | change | reason |
| --- | --- | --- |
| `native/include/bsp_map.h` | `BspTriangle::planeSide`, `BspTriangle::planeIndex`; `BspMap::windingSideOneFaces`, `windingMisfacedTriangles`, `windingMisfacedDisplaced` | a diagnostic cannot ask "was this triangle seen from the side its own data says" unless the triangle carries the face's plane and side bit |
| `native/src/bsp_map.cpp` | the face's side-aware outward normal is computed once per face; every emitted triangle is counted against it after the orientation rule ran | turns the winding question into a number instead of an argument |
| `native/tools/world_geometry_probe.cpp` | prints `sideAware misfaced=…`, a per-class × per-side outward/inward census, and a BSP-tree census; centroids are summed in a canonical vertex order | the tree census samples points near leaf boundaries, so the sample point must not move when the emission rule swaps two vertices |
| `visual-baseline-check.sh` | pins the four new readings; adds mutation **M3** | an open-defect reading that nothing pins is a reading that drifts |

The orientation rule itself is **unchanged**. §5b of `DIAGNOSIS.md` and §4 below
say why, with the measurement.

## 3. Exact inputs and commands

Reference input, identical to the first round:

- recording `tf/demos/autorecord_2026-07-02_13-26-46.dem`, map `maps/pl_odyssey.bsp`
- capture tick 2000, 1264x681, `--viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion`
- POV = the demo's first-person observer camera; debug camera = `(-2424, 5566, -80, pitch 90, yaw 0)`
- tf root `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`

```
bash build-target.sh tf2_demo_native world_geometry_probe          # rc=0
native/build-nmake/world_geometry_probe.exe --tf-root <tf> --map maps/pl_odyssey.bsp \
    --eye "-2423.6,5566.0,-1280.0"                                 # rc=0
bash visual-baseline-check.sh                                      # rc=0, PASS
bash visual-baseline-check.sh --mutation                           # rc=0, PASS (M1,M2,M3 red)
```

Executables after the gate's final restore build:

```
ff970c531213d24b194f32c906fe528d739c9a726ac3d52fcfd61552de552f2d  tf2_demo_native.exe
061ff2d28e328a191183bdc1243f7c10b0673ae695b7ea3d2ebb007051f6461b  world_geometry_probe.exe
```

## 4. Positive readings

Probe, `pl_odyssey`, eye `(-2423.6, 5566.0, -1280.0)` (cluster 1032):

```
triangles=99312 faces=18052 displaced=46912 water=300 lightmapped=37000 degenerate=4790
winding agree=5416 of 16136 faces (33.6%)
winding flippedTriangles=47252 of 99312
winding sideAware misfaced=53724 displacedMisfaced=38614 sideOneFaces=5388 of 16136
side-census brush        planeSide=0 outward=10931 inward=5361
side-census brush        planeSide=1 outward=4670  inward=3851
side-census displacement planeSide=0 outward=23364 inward=15164
side-census displacement planeSide=1 outward=4256  inward=4128
tree-side outwardOpen=9589 inwardOpen=4002 ambiguous=58134
eye cluster=1032 visClusters=1692
```

The new number is `misfaced=53724`: under the shipped rule, 54% of the emitted
triangles point away from the side their own plane's side bit calls the front.
That is the residual the cull mode removes, and it is larger than the first
round's `flippedTriangles` reading suggested.

Frames (coverage = fraction of the 1264x681 frame within 12 levels of the flat
colour, `--world-flat`):

| capture | world | no-cull maximum |
| --- | --- | --- |
| POV flat | 0.8953 | 0.9228 |
| POV depth | n/a (depth grey) | -- |
| top-down flat | 0.4683 | 0.6742 |
| top-down flat, no cull, no vis | 0.6773 | -- |

Determinism: two identical captures of the POV flat frame both hash
`9f3ec27f7dc18f02ac6e4a7261f4752bfd04ac5eec7e64316870566e37c53f86`; the
textured POV hashes `2486b77a…` — byte-identical to the pre-follow-up build,
i.e. the added instrumentation changed no pixel.

## 5. The measurement that decided the round

Eleven candidate orientation rules were captured on both fixed views with the
flat diagnostic (table and method in
`evidence/t11-world-geometry/analysis/rule-matrix.txt`). The no-cull frame of
each view is the maximum a rule can reach (POV 0.9228, top 0.6742).

| rule | POV | top-down |
| --- | --- | --- |
| shipped: brush +plane normal, displacement -plane normal | 0.8953 | 0.4683 |
| all triangles n·F >= 0 | 0.5492 | 0.6181 |
| all triangles n·F <= 0 | 0.8755 | 0.3709 |
| brush n·F >= 0, displacement n·F <= 0 | 0.7052 | 0.4490 |
| brush n·F <= 0, displacement n·F >= 0 | 0.7724 | 0.5551 |
| stored order, no reversal | 0.7414 | 0.6668 |
| global reversal | 0.8974 | 0.3705 |
| brush +plane normal, displacement left stored | 0.7286 | 0.5991 |

(`F` = the face's own side-aware outward normal.)

- **No plane-derived rule draws both views.** Each candidate buys one view with
  the other; the best POV rule leaves the top-down view at 55% of its maximum.
- **The stored order is coherent in the top-down view** (0.6668 of 0.6742 —
  culling removes 0.7%) and the shipped rule is coherent in the POV
  (0.8953 of 0.9228 — 97%).
- **A uniform sign is not a small correction.** Both uniform rules shatter the
  frame into screen-sized wedges (`analysis/m2-pov.png`, `analysis/m6-top.png`) —
  the exact defect this task opened with. Replacing the shipped rule with a
  "philosophically consistent" one would have re-created the release blocker in
  the primary view.

So the orientation rule is unchanged and the disagreement is a pinned reading
instead. The "top-down right side culls" limitation is restated accordingly: it
is not a hole in the map, it is the same surfaces the shipped rule removes, and
it cannot be closed without a per-face ground truth that does not come from the
plane lump.

## 6. Refusal readings (named, not silent)

- `tree-side ambiguous=58134` — the BSP-tree census samples 2 units either way
  along the outward direction and asks which side is open space. It is decisive
  for 13591 of 71725 PVS-kept triangles (9589 outward-open, 4002 inward-open) and
  **refuses to answer** for the rest, because displacement surfaces are not BSP
  splits. Named here so nobody mistakes it for a working oracle.
- No rule is claimed correct. The matrix is a set of measurements, not a verdict.
- Nothing in this round claims the terrain/material appearance (F2/T12) or the
  sky (`state=unavailable`); both stay open with their own tasks.

## 7. Mutation result

`bash visual-baseline-check.sh --mutation` → PASS. Forward pins held; all three
mutations went red; all three sources came back byte-identical.

| mutation | red reading | restore |
| --- | --- | --- |
| M1 pvs offset table back to the stride-8 single-table read | pvs popcount 1692 → 478 | byte-identical |
| M2 per-class winding sign flipped | flip census 47252 → 41766 | byte-identical |
| M3 class split collapsed (both classes take the brush sign) | side-aware `misfaced` 53724 → 23494 | byte-identical |

M3 is the new one and it exists to prove the new census is wired to the rule it
claims to measure: a mutation that left `misfaced` at 53724 would mean the
instrument reads a constant.

## 8. Evidence, worktree, processes

- `evidence/t11-world-geometry/DIAGNOSIS.md` — new section 5b, revised section 7
- `evidence/t11-world-geometry/analysis/rule-matrix.txt` — the eleven-rule table and its method
- `evidence/t11-world-geometry/analysis/probe-followup.txt` — the probe output above
- `evidence/t11-world-geometry/analysis/m2-pov.png`, `m6-pov.png`, `m6-top.png`, `m7-pov.png`, `m7-top.png`, `m8-pov.png` — the candidate rules as images
- `evidence/t11-world-geometry/frames-followup/` — the two fixed views plus the flat/depth diagnostics
- `evidence/t11-world-geometry/hashes.txt` — executables, frame hashes, repeat pair, coverage readings
- `evidence/visual-baseline/` — the gate's own refreshed pins (probe, trace, regions)

Worktree clean apart from the committed change; no product process left running
(the captures exit on their own and the gate's mutation builds are serialized).

## 9. Limitations that remain open

1. **The winding/plane disagreement (53724 of 99312)** — the open geometry defect
   of T11. Eight plane-derived rules measured, none draws both fixed views. The
   next instrument must be a per-face ground truth independent of the plane lump;
   the tree census is the first candidate and is too ambiguous to use.
2. **Top-down right side** — the visual face of (1); 0.4683 of a 0.6742 maximum
   under the shipped rule, 0.6668 under the stored order.
3. **4790 degenerate triangles** remain in the buffer; they draw nothing.
4. **357 static triangles with an edge above 1024 units** (largest 2560) — authored
   large brush faces, not decoding artifacts.
5. **Dark world materials** — F2/T12, not this task.
6. **Sky unavailable on pl_odyssey** — its own task.

No visual PASS is inferred from counters anywhere in this report: every claim
above is either a pinned number or a named image.
