# Next Task Plan — 2026-10-21

## Current status

- Integration HEAD: `p0-entity-protocol@34e1714`.
- Remote `origin/p0-entity-protocol` matches the local HEAD.
- T29-T38 are merged and reviewed. The release result is **NOT PASS**.
- The last full chain is not a clean final-tree result because the tree was
  merged during one run. Do not use that run as release evidence.
- Known open items: step 22 has a measured 3-byte frame difference; T35
  lighting/material gates were not run on this tree; T36 m1/m3 and removed-side
  derivation are incomplete; T37 mutation M2 was not caught; T28 must be rerun
  only after these items are closed or explicitly accepted.

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

The project is not release-pass until T39-T42 are reviewed, T43 runs on a frozen
tree, and the final chain exits 0. If any item remains unverified, the release
report must say **NOT PASS** and name the owner and cheapest next verification.

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
