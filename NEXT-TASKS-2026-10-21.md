# Next Task Plan — 2026-10-21

## Current status

- Integration HEAD: local `p0-entity-protocol@8f73d0f`; remote remains
  `origin/p0-entity-protocol@bc31533` by instruction, until T49 is complete.
- T44-T48 are reviewed and merged locally with no conflicts and no `native/`
  changes. The frozen product/source baseline is the T43 tree plus gate,
  evidence, and task-record updates.
- T43's one `verify-all.sh --quick` run passed 24/24, but release verdict is
  **NOT PASS** because of the focused T26 red and open `NOT VERIFIED` readings.
- T49 is partially complete: T25/T26/T40/T41/T38 focused gates passed after a
  fresh T26 capture; install lifecycle/readiness, step 22/ViewModel, audio, and
  the one full chain have not run.
- Full report: `T43-RELEASE-REPORT-2026-10-21.md`. Its source of truth is the
  frozen validation commit `6888e6d`; the report and this follow-up plan are
  documentation-only commits on top of that frozen tree. T49 partial report is
  `.scratch/t49/T49-REPORT.md`; its logs remain under `.scratch/t49/`.

## Ownership and execution rules

- The integration owner creates task worktrees, implements tasks, runs their
  focused checks, commits, merges branches, and updates the merge log.
- I review the diff, evidence, mutation quality, attribution, and code quality;
  I handle difficult defects and issue follow-up tasks.
- Start every task from `34e1714` in its own
  `D:\TF2_Native_Worktrees\<task-name>` worktree.
- Product builds, launches, captures, mutations, install tests, and audio-device
  tests are single-instance operations. They must run serially even when tasks
  are developed in parallel.
- Render commands must include `--hidden-window --audio-disabled`.
- Never edit the integration worktree while a product or mutation process is
  running. Do not run the full chain during a merge or while another task owns
  the shared build/evidence paths.
- Do not widen a threshold to turn a red assertion green. Every new pin needs a
  same-tree frame, source identity, mutation coverage, and a population/semantic
  explanation.

## Task wave

### T39 — Weapon-animation 3-byte closure

**Blocker:** `verify-all.sh` step 22 remains red by one pixel / three bytes.

Steps:

1. Reproduce the exact step-22 inputs and capture the failing pair at the exact
   requested ticks with the final `34e1714` source tree.
2. Record frame hashes, byte offsets, RGB values before/after, camera pose,
   model bounds, near/far planes, draw calls, animation frame/sequence, and the
   identity-skinning control frame.
3. Determine whether the difference is a deterministic raster edge, animation
   pose, resource selection, or an actual product error. The prior T34 near-plane
   attribution was measured and rejected; do not revive it without new evidence.
4. If the product is correct, change the assertion to a semantic rule: strict
   `max_delta <= 1`, changed bytes confined to a measured silhouette/edge mask,
   bounded changed-pixel count, and a control that catches a real wrong pose.
5. Add a mutation that moves the animation/model transform outside the silhouette
   rule. Run the step-22 gate forward and mutation, then the affected ViewModel
   gates. Restore sources byte-for-byte.

Exit: step 22 passes on a clean tree without accepting a 59-byte delta or
restoring the obsolete near plane.

### T40 — Final pixel-pin and semantic gate closure

**Blocker:** T35 did not run `lighting-semantics-check.sh` or
`world-material-semantics-check.sh` on the final tree; specular population was
extinguished in the old camera region.

Steps:

1. Run both gates forward on the exact final tree with hidden captures and no
   concurrent build.
2. Run their mutation forms only after the forward readings are recorded.
3. Reconcile dynamic-light, detail, bump2, specular, terrain, and world-hole
   pins against T29's frame masks. Classify every moved population as occluded,
   replaced, or broken.
4. Re-pin only values supported by current frame hashes, source hashes, and
   mutation logs. Keep specular as `NOT VERIFIED` if the old population remains
   absent; prove its shader consumer separately on a wide-area diagnostic view.
5. Re-run T25/T26 focused visual gates after any pin or product change.

Exit: all affected semantic gates pass with current-tree evidence, or every
remaining red is a named blocker with a separate owner.

### T41 — Lighting/culling mutation completion

**Blocker:** T36 did not run m1/m3; `removedSide` was derived using the old
reversed sign convention.

Steps:

1. Re-derive front/back from the actual D3D11 cull mode and the emitted triangle
   winding. Validate the rule against face-id frames, not the inherited label.
2. Produce a dark-pixel ledger partitioned into front/back, zero/empty lightmap,
   albedo, and shader output. Pin pixel masks and counts.
3. Run m1/m3 mutation cases for lightmap selection and side classification.
   Each mutation must change a named ledger population and restore its source.
4. Re-run T25/T26/T27 and the lighting gate if product code or shared pins move.
5. Preserve the conclusion that 99.94% of dark pixels are map-baked lighting
   unless a new frame proves otherwise.

Exit: removed-side and mutation readings are derived from the real raster rule,
or the remaining limitation is explicitly `NOT VERIFIED`.

### T42 — VPK mutation and installer lifecycle closure

**Blocker:** T37 mutation M2 was not caught; installer lifecycle coverage is
still incomplete.

Steps:

1. Reproduce the M2 mutation and identify why the changed behavior does not
   reach a current assertion. Add a reachable clean-directory fixture for the
   mutated path; if it cannot be reached, mark `MUTATION-NOT-LINKED` and explain
   the missing product path instead of claiming capture.
2. Validate absent, unsupported, unreadable, truncated, and valid VPK states
   through both `vpk_query` and the product. Keep absent and corrupt reasons
   distinct.
3. Test clean install, first launch, restart, migration/upgrade, uninstall,
   non-admin directory, and a missing runtime file. Record file lists, sizes,
   exit codes, and refusal text.
4. Do not create a fake MSI/installer claim. If a real installer is required,
   implement the smallest supported packaging path and test it from a clean
   directory.
5. Run install forward and mutation gates after the fixture and any product
   change; ensure no staged directory is shared with another task.

Exit: M2 is caught by a reachable invariant or explicitly documented as
not-linked, and all remaining lifecycle gaps have named release status.

### T43 — Final release rerun (T28 follow-up)

T43 is the final acceptance task and is performed by the integration owner after
T39-T42 are reviewed and merged.

Steps:

1. Freeze one commit; stop all other tasks and verify clean worktree, free lock,
   no product/probe processes, and no pending merges.
2. Run all affected focused gates once on that exact commit: step 22,
   ViewModel, T25/T26/T27, T36 lighting, T37 install, T38 quality, and audio.
3. Verify the clean-directory install lifecycle and resource-root behavior.
4. Run `verify-all.sh --quick` exactly once at the declared release boundary.
5. Review every PASS/FAIL/NOT VERIFIED/UNSUPPORTED label against current
   evidence; preserve any red result instead of masking it with a pin update.
6. Push the final commit, verify `git ls-remote`, ahead/behind, artifact hashes,
   clean status, and zero residual processes.

## Parallel schedule

### May develop in parallel

- T39, T40, T41, and T42 may be implemented in separate worktrees.
- Static review, report writing, and evidence inventory may run in parallel when
  they do not launch the product or mutate shared files.

### Must run serially

- Every build, product launch, frame capture, mutation suite, audio-device run,
  and clean-install test on this host.
- T40 pin changes must wait for T39's camera/animation decision if they touch
  the same frame or gate.
- T43 must not overlap any task implementation, merge, validation, or cleanup.
- `verify-all.sh --quick` is always the final single operation.

## Completion rule

T43 has run. The project remains **NOT PASS** until the T26 focused red is
resolved with a semantic assertion and positive control, the cold-cache T37
readiness behavior is deterministic, and every release-relevant `NOT VERIFIED`
item below is measured or explicitly retained as a limitation. A final release
task must run on a new frozen tree after T44-T48 are reviewed and merged.

## Merge log (integration owner)

- 2026-10-21 — T39 `task/t39-weapon-animation@daea556` merged as `39c9b05`.
  No product source changed: the step-22 identity pair is a deterministic
  one-sample raster ownership tie (1 pixel / 3 bytes, delta 59, at (622,412);
  860,783 other pixels byte-identical), caused by the skinning shader's
  normalized weighted sum versus the passthrough. The whole-frame byte bound is
  replaced by `bmp-silhouette-diff.py` (outside a measured edge mask
  max_delta <= 1 strictly, every changed pixel inside the mask, changed pixels
  capped at 4, global ceiling 128, mask population printed and bounded) plus a
  real-pose control that the same rule rejects. Step-22 pin 56 -> 57 and a third
  mutation (`--mutation-identity`) added; forward 57 OK / 0 FAIL, mutations all
  caught with byte-identical restores, affected ViewModel gates 154/114 PASS.
  The T34 near-plane attribution stays refuted and the near plane is untouched.
  Open (NOT VERIFIED): per-sample rasterizer instrumentation; threshold 32 is a
  stated parameter (population printed); second-model check supplementary;
  exe hashes not reproducible across rebuilds. T43 re-runs the affected gates on
  the frozen tree.

- 2026-10-21 — T41 task/t41-lighting-mutation@bf2aa38 merged as daf6700. No product source changed. The raster rule is derived: D3D11_CULL_BACK + FrontCounterClockwise=FALSE, front iff dot(emitted normal, eye-p) > 0; the brush fan emits the plane normal whatever the side bit says (16,654/16,660; 1 against, 5 degenerate) while all 1,392 displacement grids are flipped per triangle onto the face outward, so the plane-side rule and the raster rule differ only in brush/side-1 (6,114/6,115). The first derivation used the once-per-run gpu: world vis eyeMap (the first drawn frame); that was wrong and is superseded. At the capture pose from the trace's last scene: camera (eye=-2294.8,348.8,383.4, now in frames.txt), the face-id frame is 698,501 px derived-front (93.08%) / 51,890 derived-back (6.92%): 51,620 disp/side-0 + 2 disp/side-1 (rule conservative on curved grids) + 334 brush (0.04%); the product's own 28-point trace agrees 22/24 with both exceptions on the same disp/side-0 face. T36's 170,701/35,227, its 86%-back headline and 44,046/37,095 are SUPERSEDED as wrong-pose numbers, re-derived as 98,160/107,768, 50.49%/49.51% and 6/81,135; the 208,630 dark total and 4,123 product-attributable share are pose-independent so the 99.94% map-baked conclusion stands. Forward 35 assertions / 0 FAIL; m1/m2/m3 all RED with byte-identical restores (m1 208,630 -> 3,913; m2 the split swaps at the same total; m3 81,141 -> 0). NOT VERIFIED: the pixel-level authored-side split and the per-face reading of the removed pixels; the 51,620 disp/side-0 residual is bounded, not explained per triangle (cheapest next step: dump face 17605's displaced triangle normals in one product run). Seam reading for T43: 0 of the 8 named seam faces are derived-back, so T26 seamCulled=0 is the correct reading and the old >=1 pin encodes a pre-T29 artefact.
- 2026-10-21 — T42 task/t42-installer-vpk@d956df9 merged as e46967f. Product: the installer compares packageVersionCode (upgrade note, downgrade refused exit 2 reason=downgrade), the VPK reader reports ABSENT / UNREADABLE / UNSUPPORTED / TRUNCATED / OPEN_FAILED as distinct named reasons, and the pre-T42 refusal line stays byte-compatible. Lifecycle forward cases=15 checks=213 INSTALL-LIFECYCLE=PASS; mutation cases=3 checks=25 all caught with byte-identical restores. T37's M2 miss was MUTATION-NOT-LINKED in its fixture: the installed uninstaller was the reference build (49239329...), so the mutated binary never ran; staged from the mutated build (caab6897...) the existing "a user file survives uninstall" invariant catches it (removed=4 preserved=0, user file absent). The gate now asserts the executing binary's sha256 and exits 90 MUTATION-NOT-LINKED on a stale stage. Readiness forward 13/107 PASS and mutation 4/40 with 3/3 caught, in one host-lock transaction that reads the lock owner at start and end; the earlier readiness red was real (T37 moved the smoke probe into .uninstall/payload, so removing install(TARGETS) no longer changed the package) and is fixed in 811b258 by removing the probe from both install rules and pinning the consequence against the forward reference. Two named gaps remain (no shortcuts/file association; TF2 assets not packaged) and no MSI/CPack claim is made.
- 2026-10-21 — T40 task/t40-pixel-pins@ab89e0a merged as b8302a3 and 75292a0. Both semantic gates PASS forward on the re-pinned tree: lighting 96 OK / 0 FAIL, material 183 OK / 0 FAIL. Lighting m1-m4 and material m1-m5 all RED with every source restored byte-identical (m1 tangent ignored 79 -> 0; m2 bump2 layer 7,343 -> 11,746; m3 detail mode 271,060 -> 255,344; m4 feature counter still 9/45 while the frame moves 79 -> 417,045; m5 rect 256x256 -> 512x256). T25 visual baseline PASS; T12 terrain-blend PASS 94/0 after building the missing terrain_blend_probe. T26 world-geometry-hole is one measured RED left red on purpose: seamCulled reads 0 because after the near-plane fix none of the surviving slivers is bracketed by a product-culled surface (32/32 directions resolve, 81,141 px are cull-off-only; T41's derived rule says 0 of the 8 named seam faces are derived-back). NOT VERIFIED: tick-2000 specular is occluded (tick-1200 pair is a different camera), the detail residual is 2,812 px on 27 non-displaced WOOD/WALL007A faces, and the tick-2000 bump direction is one-sided; cheapest next steps are in evidence/t40-pixel-pins/T40-REPORT.md and evidence/material-semantics/not-verified.txt.

## T43 release boundary

This commit is the freeze commit. After it, no source, gate, evidence, or merge change is allowed. T43 runs the focused gates once on this tree (step 22, ViewModel 23/24, T25/T26/T27, T36 lighting, T37 install and readiness, T38 quality, audio), then verify-all.sh --quick exactly once as the final operation, then pushes and verifies the remote.

Known red / NOT VERIFIED at the boundary: T26 seamCulled (measured red; owner T43/T41; cheapest next step = replace the pre-T29 cull-neighbour assertion with the derived-rule statement above); T40 tick-2000 specular, 2,812 px detail residual, one-sided bump; T41 pixel-level side split; T39 per-sample rasterizer instrumentation. None is described as a pass.

## T43 close-out — 2026-10-21

T43 is complete on frozen commit `6888e6d`:

- `verify-all.sh --quick`: **VERIFY=PASS**, 24/24 steps, rc=0, exactly one
  completed chain run. Full hashes and focused logs are in
  `T43-RELEASE-REPORT-2026-10-21.md`.
- Step 22: PASS 57/0; ViewModel steps 23/24: PASS 154/0 and 114/0.
- T25 visual baseline: PASS 70/0. T27 terrain blend: PASS 94/0. T36 dark
  account: PASS 35/0.
- T37 lifecycle: PASS 15 cases/213 checks. Readiness: first cold-cache run had
  four title-capture failures while trace already showed a valid `tf_root`; the
  warm-cache retry passed 13/107. Keep timing sensitivity open until cold runs
  behave deterministically.
- T38: PASS 28/0. Audio content: PASS 39/0 plus 25/25 mutations. Audio
  subsystem: PASS 240/0 plus its mutation suite.
- T26 world-hole gate: **FAIL** at `seamCulled=0`. T41/T43 measured all 8 named
  seam faces as derived-front at the capture pose; culling removes other
  non-seam faces. The old `>=1` assertion is obsolete, but it must be replaced
  with a seam-specific assertion and a positive cull control before the focused
  gate is called green.
- Release verdict stays **NOT PASS**. The full chain's pass does not erase the
  focused red or unverified material/lighting readings.

The T43 report was copied from `.scratch/t43/` into
`T43-RELEASE-REPORT-2026-10-21.md` in a documentation-only follow-up. The tested
freeze remains `6888e6d`; no product code or gates were changed after that run.

### T44-T48 merge log — 2026-10-21

- T44 `task/t44-seam-assertion@0f5f95d` merged as `a77dc33`. No product
  source. The pre-T29 `seamCulled >= 1` assertion is replaced by measured
  semantics: pose from the trace's last `scene: camera` (eye
  -2294.8,348.8,383.4, tick 2000; the once-per-run eyeMap is absent and printed
  as superseded), 6 points x 4 directions = **24/24** resolvable directions
  (the brief's "32/32" is a documentation error; T40's own log has 6 HOLE seam
  lines and 13 face-info requests), **0/8** named seam faces derived-back at
  that eye, and a positive cull control: 81,218 removed px / 19 faces, 18
  non-seam, top face **17605** disp/side-0 derived-back **33,167 px**, plus
  1 px of seam 17266 at a silhouette edge recorded not hidden. Forward PASS;
  M1-M6 all red, including M6 (cull never runs -> removed=0 while 8/8 stay
  derived-front), all sources restored byte-identically. NOT VERIFIED:
  per-pixel derived side, eye printed to 0.1 units, pre-existing stale header
  numbers, identity.txt exe-hash churn.
- T45 `task/t45-visible-material@19757f5` merged as `d3ce7eb`. No product
  source. The tick-2000 specular and bump-direction gaps are now measured on a
  second, wide-area diagnostic view (WALL013, --debug-camera, tick 2000):
  S1w vs S2w 775,135 bytes (386,760 brighter / 388,375 darker, max 24) and
  Cw vs Dw 688,102 bytes (203,847 / 484,255, max 60), with Aw==Aw2, S2w equal
  to the hold-A frame, and Aw-vs-A 2,570,233 bytes moved-view control. The
  tick-2000 reference camera still has only 28 px of material-normal coverage,
  so specular there remains a measured zero and the bump direction there
  one-sided; both stay **NOT VERIFIED**, not replaced. The 2,812 px detail
  residual is now attributed at frame level: T29's near-plane expression
  reverted -> pre-fix A/F reproduced byte-for-byte (f87f396a / 21cb8aa2),
  restored -> current pins (6bb4d9a7 / 2ffa7d2b); the pixel-level rasterizer
  path is still **NOT VERIFIED** and no shader/data defect is claimed. Gate:
  material forward 209 OK / 0 FAIL; m1-m5 all red and restored; lighting
  forward 96/0 = T43 baseline; lighting m1-m4 all red and restored.
- T46 `task/t46-authored-side@b169152` merged as `af9d4d7`. No product
  source. Per-triangle ledger closes T41's 51,890 px exactly: **51,624 world
  + 266 ViewModel**, world = 51,620 disp/side-0 + 2 disp/side-1 + 2 zero-area
  brush-fan; face **17605** 51,618 px / 32 grid triangles (17 front / 15 back),
  51,354 exactly on a front triangle, 64 sub-pixel, 202 edge-on px bounded
  (49 back-only + 32 back-only-sub + 121 no-triangle). Cull 81,141 = 81,123
  back-covered + 18 grazing, 0 uncovered. T36 forward PASS with 43 new exact
  pins; m1-m4 all red, m4 requires the exact swap 51,354->51,413, 49->0,
  17->15, 24,058->22,958; all restores OK. T36's per-face plane-side split and
  per-face removed readings stay SUPERSEDED / NOT VERIFIED; 163 over-bound id
  regions stay bounded.
- T47 `task/t47-cold-readiness@a1c34b9` merged as `c86418e`. No product
  source. Readiness title polling is bounded (TITLE_BOUND_SECONDS=120,
  CAPTURE_BUDGET_SECONDS=600, fail-fast) and the trace root is asserted
  independently; a 5 s forced bound produces exactly
  `TITLE-CAPTURE-TIMEOUT` with trace `valid=1`, never accepted as
  connected. Two independent cold-start cycles (separate staged packages and
  settings dirs) give identical root decisions (DECISION-MISMATCHES=0,
  ROOT-ACCOUNTING-FAILURES=0, TWO-CYCLE=PASS). Forward 13 cases /**145
  checks** (T42's 107 kept verbatim, +38 additive, 0 removed); mutation 3/3
  caught and restored, staged-binary hashes asserted per stage, no
  install/uninstall/version invocation during capture. True system-wide cold
  cache is **NOT VERIFIED**; the 20.4-24.0 s warm settle and the old 30 s bound
  explain T43's four title-capture failures as timing, not a wrong root.
- T48 `task/t48-raster-sample@1721b36` merged as `59ff6a9`. No product
  source, no silhouette-rule change. The one-sample identity/passthrough tie
  at (622,412) is **bounded, not proven**: identity multiply is exact, shift
  <=0.00022 px at 222.9 px/unit, 1,069 measured boundaries -> 0.48 expected
  ties worst case (0.07 realistic), observed 1; shader outputs reconcile
  exactly, but clip/NDC/viewport/depth are unavailable and per-sample
  ownership is **NOT VERIFIED**. Step 22 still PASS 57/0 with the pair
  changed=1/outside=0/interior_max=0 and the real-pose control still rejected;
  `--mutation-pose` (0.001f) turns the gate red (3,322 changed, 2,397
  outside, interior 73) and restores main.cpp (f8badad5...) byte-identically,
  with the restored pair a06f4e36 / 37e5eb2a cmp-identical. The tracked
  `evidence/weapon-animation/` snapshot is flagged superseded and was not
  refreshed.

All five merges are clean; no `native/` or `evidence/verify/` change rode
along. The T26 "32/32" brief is superseded by the measured 24/24 above; the
T49 report must state the measured total.

## Next wave: T44-T48

All tasks start from `6888e6d` in independent worktrees. Development may be
parallel; builds, launches, captures, mutations, install tests, and audio-device
tests are serialized. The integration owner merges reviewed branches; I review
each delivery and help resolve difficult issues.

### T44 — Correct the T26 seam assertion without hiding culling regressions

Owner: T26 gate owner / integration owner. Keep changes within
`world-geometry-hole-check.sh`, its report, and T26 evidence unless a probe bug
is demonstrated.

1. Reproduce the T43 result from the exact trace camera and tick. Do not use the
   first `world vis eyeMap` line if it belongs to another scene pose.
2. Assert **0 of the 8 named seam faces are derived-back** at this pose. Keep
   the 32/32 resolvable-direction check and face-id/oracle agreement.
3. Add a positive control proving the product cull removes at least one named
   **non-seam** face at the same pose; record its face ID and derived side.
4. Add a mutation that disables or reverses culling and prove the positive
   control turns red while seam classification remains meaningful.
5. Run `world-geometry-hole-check.sh` and `--mutation`; confirm all mutations
   are caught and mutated sources restore byte-for-byte.

Exit: the focused T26 gate passes for current geometry semantics and still
fails if culling is genuinely broken.

### T45 — Finish T40's visible material semantics

Owner: T40 pixel-pin owner. Do not relabel an occluded zero-delta pair as PASS.

1. Use one wide-area pose from T35's nine-pose set at tick 2000. Capture a
   baseline and repeat; prove exact tick, camera pose, and frame determinism.
2. Measure specular with and without the material normal on that pose; require
   a visible material population and directional pixel evidence.
3. Measure tick-2000 bump under the same wide-area pose; determine whether its
   79/0 direction is occlusion or a shader/data defect.
4. For the 2,812-pixel detail residual on 27 WOOD/WALL007A faces, run T29's
   near-plane mutation, capture F at tick 2000, and compare against the pre-fix
   F. Identify the exact face/material/resource/shader cause.
5. Keep one semantic switch per capture. Run
   `lighting-semantics-check.sh --mutation` and
   `world-material-semantics-check.sh --mutation` after forward evidence.
6. Re-run T25/T26 if a shared frame, pin, or world shader changes. Preserve
   source hashes and byte-identical mutation restores.

Exit: each item is measured on a visible population or remains `NOT VERIFIED`
with a named reason; no occluded zero-delta pair is called proof.

### T46 — Resolve T41's authored-side pixel population

Owner: T41 lighting/culling owner.

1. At the exact T43 capture pose, instrument face 17605's displacement grid and
   dump each emitted triangle's vertices, normal, plane side, emitted winding,
   eye dot product, and derived raster-facing result.
2. Map the triangles to the face-id frame and report which pixels are authored
   front/back, raster front/back, and actually removed by D3D11 culling.
3. Reconcile the 51,620 disp/side-0 residual and the two product-trace
   exceptions. Separate conservative face classification from pixel ownership.
4. Add a mutation that flips the selected side classification and prove the
   pixel ledger detects it; do not substitute a global percentage.
5. Run the focused T36 gate and its complete mutation suite after restoring the
   diagnostic path.

Exit: the authored-side versus raster-side split is explained for the named
face, or remaining curved-grid ambiguity is bounded and marked `NOT VERIFIED`.

### T47 — Make install readiness deterministic on a cold cache

Owner: T37/T42 install-gate owner. Product root and UI title are separate
signals; keep both checks.

1. Reproduce the T43 cold-cache title failure without clearing system-wide
   caches: use a fresh install/settings directory and record elapsed time,
   trace, and every title sample.
2. Make title capture poll within a documented bound instead of sampling too
   early. Independently assert trace
   `resources: ... tf_root=... valid=1`.
3. If trace says the root connected but the title never reaches its connected
   form, report `TITLE-CAPTURE-TIMEOUT` separately; do not accept it as a
   connected title.
4. Run two clean first-launch/restart/explicit-root cycles from separate
   temporary directories and require identical root-selection decisions.
5. Run `install-readiness-check.sh --forward` and `--mutation` with the staged
   binary hash check; verify no stale installer/uninstaller executable ran.

Exit: two cold-start cycles pass deterministically, or the gate reports a
separate reproducible title timeout while preserving correct root accounting.

### T48 — Close or bound T39's rasterizer-sample limitation

Owner: T39 weapon-animation owner. Diagnostic work only; do not change the
accepted silhouette rule unless measurement invalidates it.

1. Use the exact identity-skinning/passthrough pair at the T43 inputs and inspect
   pixel `(622,412)` plus neighboring coverage.
2. Record interpolated clip position, post-divide position, viewport position,
   depth, and shader output for both paths where instrumentation permits.
3. Verify the current rule: outside-mask max delta <=1, all changed pixels are
   inside the measured edge mask, changed-pixel count <=4, and the real-pose
   control is rejected.
4. Add a mutation that displaces the pose beyond the mask and prove the gate
   fails. Restore source and confirm the step-22 frame hash returns.
5. If hardware cannot expose per-sample detail, document the measurement limit;
   do not claim the exact one-sample tie mechanism as proven.

Exit: the difference is explained or explicitly bounded; step 22 continues to
pass without loosening the silhouette or directional rules.

## Execution order and non-parallel work

1. Start T44, T45, T46, T47, and T48 in separate worktrees from `6888e6d`.
   T44 and T47 are highest priority: one is a focused red; one makes install
   readiness timing-sensitive.
2. T45/T46/T48 may develop while T44/T47 are reviewed. Coordinate before merge
   if tasks touch the same gate or diagnostic source.
3. Run each task's build/capture/mutation validation one at a time under the
   host lock. Never run two capture or mutation suites concurrently.
4. Merge accepted branches one at a time. After each merge, run only the
   directly affected focused gate; do not run the full chain during this wave.
5. After T44-T48 are reviewed and merged, the integration owner prepares a new
   frozen commit and starts T49 final release acceptance.

Cannot run in parallel: any two builds/launches/captures/mutations; T47 staging
tests with another install test; T49 with code edits, merges, or other
validation; and the final `verify-all.sh --quick` with any other activity.

### T49 — Final release acceptance follow-up

Owner: integration owner. T44-T48 are reviewed and merged locally. Continue
from the clean T49 worktree; do not push before the final chain and report.

1. Freeze the new integration commit; verify clean files, free lock, no pending
   merges, and no product/probe/compiler processes.
2. Run T26, T40 lighting/material, T41 dark-account, T37 lifecycle/readiness,
   T39 step 22, ViewModel, T25, T38, and audio focused gates once on that tree.
3. Measure remaining `NOT VERIFIED` items or list them as explicit release
   limitations with owner; preserve every focused FAIL.
4. Run `verify-all.sh --quick` exactly once as the final test operation.
5. Report focused-gate and full-chain verdicts separately. Mark release PASS
   only when the chain passes and no unaccepted release blocker remains.
6. Push the final commit; verify `git ls-remote`, ahead/behind, artifact hashes,
   clean worktree, free lock, and zero residual processes.

### T49 continuation runbook (quality-preserving efficiency)

The remaining work is one serial validation batch. Avoid rerunning gates that
already passed unless a source, gate, evidence pin, or executable changes.

0. **Fix the stale T26 capture-cache acceptance before the final batch.** The
   gate currently treats `capture_written=1` plus tick 2000 as sufficient, even
   when a reused trace lacks `scene: camera ... eye=... src=demo`. Update cache
   reuse to require the armed tick and pose/provenance lines plus matching
   executable/input/flags identity. This is gate-only; then run T26 forward and
   mutation once on the resulting T49 candidate tree. Preserve the stale trace
   as superseded evidence; do not delete it.
1. Record the new candidate HEAD, `git status`, lock/process state, and existing
   44-executable SHA manifest. Rebuild only if the gate fix changes generated
   inputs or a required executable is stale; do not repeat the clean 44-exe
   Release build without a reason.
2. Run `D:\TF2_Native_Worktrees\_shared\t49\focused-d.sh` once under the host
   lock (install lifecycle forward/mutation, then readiness forward/mutation).
   Inspect each section's exit code and restoration result before continuing.
3. Run `focused-e.sh` once (weapon-animation step 22, ViewModel animation,
   companion). These tests are sequential inside the wrapper; do not launch a
   second copy or rerun individual green gates.
4. Run `focused-f.sh` once (audio content and audio subsystem, device/mutation).
   Ensure the selected device is available before starting; do not overlap any
   install or renderer process.
5. Review logs and verify all mutation sources are restored. If a section fails,
   stop before the chain, classify the failure, fix only that failure, and
   rerun its focused gate plus direct dependents. Do not discard a run because
   it is inconvenient; mark stale input/cache evidence as superseded with hashes.
6. After D/E/F pass, check clean status, lock FREE, and zero processes. Run
   `_shared/t49/chain.sh` exactly once as the final validation operation.
7. Finalize the report with exact HEAD, logs/hashes, focused and full-chain
   verdicts, remaining `NOT VERIFIED`, clean state, then push and verify remote.

### Efficiency rules for future task waves

- **Parallelize coding, serialize machines:** independent owners may implement
  in separate worktrees at the same time. Builds, captures, mutations, device
  tests, and install staging remain one-at-a-time on this host.
- **Use three validation layers:** per change, run the narrowest direct gate;
  after a task wave, run only the union of affected gates once; run the full
  24-step chain only at the release boundary. Never run the full chain per task.
- **Build once per candidate source tree:** reuse the clean build for multiple
  gates if source/configuration identity is unchanged. Record executable hashes
  and rebuild only when affected sources or build inputs changed.
- **Cache evidence by identity, not file existence:** include source commit,
  executable hash, command flags, demo/map hashes, target tick, and required
  trace pose markers. A BMP/trace pair is reusable only when all identity checks
  match. This prevents repeating long captures and prevents stale evidence from
  passing.
- **Bundle tests by host resource:** the T49 D/E/F wrappers already group
  install, render/ViewModel, and audio work. Run each wrapper once, inspect it,
  and do not separately rerun its green subcommands.
- **Stop on the first unexplained red:** preserve its log and inputs, diagnose
  whether it is product, gate, stale artifact, or environment, then rerun only
  the affected validation. Do not rerun all prior green groups.
- **Keep mutations meaningful:** every changed product or gate invariant needs
  at least one mutation that reaches the changed behavior and a byte-exact
  restore. Do not remove mutation coverage to save time.
- **Parallel review and paperwork:** while one locked test batch runs, another
  owner may review diffs or prepare reports, but must not touch the shared build,
  evidence, settings, lock, or staging paths.

## Product scope clarified by the user — roadmap after T49

The full product target is a TF2 demo review/editor tool, not only a demo parser
and renderer. Current player/render/audio/resource foundations are substantial,
but the following user-facing features are **not implemented or not accepted**
yet. Re-estimated progress against this full target is roughly **35–40%**;
the earlier 75% estimate described core-engine/release-gate progress and did not
include this feature scope. Clip editing/export is explicitly a later phase.

| Feature | Current state |
|---|---|
| Load/play TF2 demos, render BSP world and first-person models, read local TF2 resources, schedule sound | Implemented; broad focused evidence exists |
| Import/apply a custom HUD | Not implemented; current HUD/status display is not a user HUD import system |
| Free camera while paused | Camera/free-look controls exist, but the exact paused free-camera workflow needs a focused acceptance task |
| Save multiple fixed camera bookmarks and jump with custom hotkeys | Not implemented |
| Timeline under the player and choose/edit a time range | Not implemented |
| Export selected range to MP4 or another compatible video format | Deferred to a later phase, as requested |
| One-click X-ray player outlines through walls, red/blue by team | Not implemented |
| Projectile trajectory overlay, all players or selected player, configurable visibility/linger and performance interval | Not implemented |
| In-app logs.tf viewer: empty by default, import a logs.tf URL, resizable and scrollable like a browser | Not implemented |

### T50 — Camera director and paused free camera

Start after T49 establishes a stable player baseline. Design the controls before
implementing: pause must freeze demo time while camera motion continues; provide
clear orbit/free-fly controls, reset-to-player, save/name/delete camera
bookmarks, and user-configurable hotkeys with conflict detection and defaults.

Acceptance steps:

1. Test pause/resume and camera movement separately; assert demo tick stays fixed
   while camera pose changes, then advances after resume.
2. Save at least three camera bookmarks, jump among them with configured keys,
   and verify exact pose restoration and stable frame capture.
3. Test key conflicts, missing bindings, reset, and keyboard focus in text fields.
4. Keep rendering hidden during automated captures; add forward and mutation
   checks for a broken pause freeze and a broken bookmark jump.

### T51 — Custom HUD import and preview

Investigate which TF2 HUD file formats can be supported safely; then implement a
user-facing import flow with file/folder picker, validation, preview, apply,
reset-to-default, and a clear refusal reason for unsupported assets. Do not claim
full Source HUD compatibility from loading one layout file. Keep HUD scaling and
screen-resolution behavior visible in the preview.

Acceptance steps: import a known-good custom HUD, reject malformed/missing
files, preview before applying, preserve user files on reset/uninstall, and
verify HUD changes do not alter the captured world/model image underneath.

### T52 — Team-colored X-ray player outlines

Add a one-click overlay toggle that draws player silhouettes through walls, with
red-team red and blue-team blue outlines. First prove player entity identity,
team mapping, and pose at each sampled tick; render the outline in a separate
depth-aware pass so it does not change world geometry or ordinary visibility.

Acceptance steps: test one red and one blue player, visible and occluded poses,
toggle on/off pixel masks, unknown-team fallback, entity despawn, and no-player
fixtures. Mutations must catch swapped team colors, depth-tested-only outlines,
and a leaked always-on overlay. If a demo lacks reliable team/player state,
name that input limitation.

### T53 — Projectile trajectory overlay

Add a one-click trajectory toggle and controls for all players versus one
selected player. Let the user adjust sampling/performance quality and how long a
trail remains after the projectile disappears or the overlay is disabled; use
about 0.5 seconds as the default linger, with a slider and immediate preview.
The overlay must not assume constant velocity if the demo provides observed
projectile positions that capture gravity, collision, or deflection.

Acceptance steps: use real projectile entities, compare the line against sampled
positions from the same demo ticks, test all-player and selected-player filters,
owner changes/deflection, disappearance, toggle-off linger, and low/high sampling
cost. Include an explicit no-projectile case and a performance reading.

### T54 — Embedded logs.tf viewer

Add a logs panel that is empty until the user imports a URL. Support the supplied
logs.tf URL shape, validate the host before navigation, and provide a resizable
pane with normal scrolling and browser-like back/forward/reload controls. Keep
demo playback usable while the panel is open. Prefer an already available
Windows web-view runtime; report a missing-runtime explanation and fallback if
the host cannot initialize. Do not fetch or impersonate user credentials.

Acceptance steps: empty state, valid logs.tf URL, malformed/wrong-host URL,
resize, scroll, navigation history, close/reopen, and playback responsiveness.

### T55 — Timeline, clip selection, and video export (deferred)

This is deliberately after T50-T54. First build a timeline synchronized to demo
ticks with draggable in/out handles and precise numeric entry. Then export the
selected interval to MP4/H.264 when available, with a documented compatible
fallback format, progress/cancel behavior, audio sync, and output-path choice.
Do not start encoder integration until frame capture can reproduce the chosen
range and resource loading can sustain export without foreground test windows.

## Product-feature execution and parallelism

1. Finish T44-T49 release follow-up first; this stabilizes the renderer/player
baseline against which new features can be tested.
2. T50 and T51 may be designed in parallel, but coordinate UI/settings files
   before implementation because both add user controls.
3. T52 and T53 may be prototyped in parallel only in separate worktrees; they
   share entity/team/overlay rendering concepts and must agree on one ownership
   and tick-sampling API before either is merged.
4. T54 is comparatively independent and can be developed in parallel after the
   T49 baseline, but its WebView packaging/lifecycle tests remain serialized with
   all other installation tests.
5. T55 remains deferred until the review UI and camera workflow are accepted.
6. All builds, launches, captures, mutation suites, audio-device runs, and
   clean-install tests remain single-instance operations on this host.
