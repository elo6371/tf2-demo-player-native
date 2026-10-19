# TF2 Native Demo Player - Next Integration and Work Plan

Date: 2026-10-19
Integration branch: `p0-entity-protocol`
Current integration HEAD: `98766b5` (T12/T17/T21/T23/T24 merges plus T20 records; includes T11-T24 integration work)

## Project state

The project is a native Windows TF2 demo player. It parses Source demos,
rebuilds entity state, resolves resources from loose files, VPK archives, and
BSP pakfiles, and renders world geometry, materials, models, ViewModels, and
audio.

Completed task deliveries currently present on the integration line include
T11, T12, T13, T14, T15, T17, T18, T19, T21, T22, T23, and T24. T12 was
integrated by T16 merge commit `f304abe`; T21 by `4827e73`, T22 by `4a5bbd3`
and follow-up `5a49ae1`, T23 by `6d8d454`, and T24 by `9fdff66`. The final-tree
focused regression remains T20. T22
(`task/t22-viewmodel-presentation`: body slot override + presentation gate +
evidence + report) was merged as `4a5bbd3` on 2026-10-19; its gate re-ran green
on the integration HEAD (90/90 forward; 3/3 mutations on the branch), and the
integration-HEAD run is archived as
`evidence/viewmodel/presentation/summary-integration-head.txt`.

T17 (`0c43e3e`) has already been merged into `p0-entity-protocol` as
`9591499`. Its source and gate changes are present. The T17 branch's own gate
results were green, but those results predate integration with T19's material
coverage changes and are not final-tree evidence.

## Current diagnostic state

A post-merge `visual-baseline-check.sh` run on the current integration tree
returned failures. Its run artifacts changed three tracked files under
`evidence/visual-baseline/`; these local changes are diagnostic output only and
must not be committed as accepted evidence. Preserve them until the run is
archived or deliberately superseded.

Observed readings that differ from the old pins:

- `material-fetch`: tiled 85, from pakfile 85, `vtfTooLarge` 70 (old pins 64,
  59, 54).
- `blend-fetch`: declared/resolved/tiles 10, weighted vertices 85920 (old pins
  9 / 9 / 9 / 85536).
- W1: `nearBackground=0.0592`, above the old 0.05 limit.
- W2: `meanLum=57.0`, `stdLum=18.30`, `nearBackground=0.2409`, and
  `meanGradient=3.72`, outside old limits.

These differences may be expected consequences of T19's expanded atlas
coverage, but that has not been proven. Do not update pins until a fresh run
from the final integrated tree ties each changed reading to the responsible
render path and confirms the resulting frame is acceptable. No old executable,
frame, or trace may stand in for evidence from the current source tree.

## Task T16 - Integrate T12 blend close (completed)

Owner: integration lead
Starting point: `p0-entity-protocol` at `9591499`
Delivery: integrated `task/t12-blend-close` through `d64b6cc` without losing
later T13-T19 work. Merge commit: `f304abe`.

Steps:

1. Record the current worktree status and preserve the three diagnostic visual
   baseline files. Do not stage or commit them.
2. Inspect T12's merge base, commit range, and changed paths against current
   HEAD. Resolve shared-file conflicts by preserving the current T17-T19
   behavior and applying only T12's blend-close changes.
3. Build the affected targets from the merged source tree:
   `tf2_demo_native`, `terrain_blend_probe`, `world_geometry_probe`, and
   `material_coverage_probe`.
4. Run the focused T12 forward and mutation gates serially. Mutation must only
   start after the forward gate passes, and source restoration must be checked
   byte-for-byte.
5. Run `visual-baseline-check.sh` once on the final T12-integrated tree. Treat
   all changed pins as investigation items; do not loosen ranges or copy pins
   from an older branch to obtain green.
6. Commit the integration only after the focused checks and source/evidence
   identity are recorded. Do not run `verify-all.sh` as part of T16.

Recorded result:

- T12's product and gate changes are present, with no T13-T19 changes lost.
- `terrain-blend-check.sh` passed on the integrated tree; its six mutations
  were caught and source restoration was verified.
- The m6 mutation now detects rank movement under T19's multi-batch atlas
  instead of assuming a 64-tile hard cap.
- Post-merge visual-baseline differences remain `NOT VERIFIED`; diagnostic
  files are intentionally uncommitted.
- No unrelated evidence output was staged in `f304abe`.

## Task T20 - Focused final-tree regression (completed with one NOT VERIFIED gate)

Owner: integration lead or assigned validator
Start only after T16 is committed. Use one worktree and serialize all builds,
product launches, captures, and mutations because they share binaries and
evidence paths.

Run only the gates affected by T11-T24 and T12 integration:

1. Build `tf2_demo_native`, `world_geometry_probe`, `terrain_blend_probe`,
   `material_coverage_probe`, `viewmodel_animation_probe`, and
   `weapon_body_probe` from the committed tree.
2. Run the forward checks serially: `visual-baseline-check.sh`,
   `world-material-check.sh`, `terrain-blend-check.sh`,
   `material-coverage-check.sh`, `viewmodel-apply-check.sh`,
   `viewmodel-animation-check.sh`, `viewmodel-companion-check.sh`,
   `viewmodel-range-check.sh`, and `weapon-material-check.sh`.
3. Run each corresponding mutation check only when its forward check passes.
   Do not rerun unrelated audio, entity-protocol, stability, or installer
   suites unless a changed dependency requires it.
4. Record exact HEAD, source hashes, command exit codes, executable identities,
   frame hashes, mutation restoration results, and any remaining failures.
5. Do not run the 24-step `verify-all.sh --quick` chain for this task. Reserve
   it for an explicitly declared release-boundary run after all focused gates
   are committed and the host is idle.

Acceptance:

- Every affected focused gate has a current-tree result; no result is borrowed
  from a task branch's pre-merge run.
- Mutation checks ran only after green forward checks and left source byte
  identical to the committed tree.
- Remaining visual or format limitations are reported as `NOT VERIFIED`, not
  inferred from counters or resource lookup success.

Current T20 results on `9fdff66`:

- T21 face check: forward PASS; mutation 4/4 PASS, with hidden-window product
  launches after the initial script correction.
- T22 presentation: PASS, 90 assertions / 0 failures, hidden-window captures.
- T23 accounting: `rc=0`; 653277 drawn, 167030 backface, 0 visibility,
  38328 missing pixels.
- T24 lighting: forward PASS; mutation 4/4 PASS; final-tree delta pins are
  1584117 dynamic, 267343 specular, 746559 dynamic+bump, bump-alone 0.
- T12 terrain blend: forward and mutation 6/6 PASS before T21/T24 merges;
  rerun after the final merge is still required.
- T19 material coverage: final-tree rerun is still required.
- T17 visual baseline: final-tree rerun is still required. It uses hidden-window
  capture; T23 established visible/hidden byte identity.

Final T20 conclusion:

- T12, T19, T21, T22, T23, and T24 focused checks are current-tree green.
- `visual-baseline-check.sh` ran on the final tree with hidden captures and
  returned `FAIL` only because old T17 pins do not describe the T19/T21/T12
  material and UV results. The original output is preserved locally as
  diagnostic evidence; no pin was loosened or relabeled as PASS.
- The release status remains `NOT VERIFIED` for the visual baseline until a
  reviewed re-baseline or a product fix explains W1/W2 and the changed fetch
  counts.

## Follow-on tasks after T20

These are independent investigation/implementation tracks. They may be
started now in separate worktrees against a committed base; their changes
must be rebased or merged only after T16 and must pass T20 on the combined
tree. Do not run builds,
mutations, captures, or product processes concurrently on this host; coordinate
those validation windows serially. Each task must use a current committed base
and must not edit another task's worktree.

### T21 - World material coverage and visual correctness

- Reproduce the current W1/W2 and material-fetch deltas on the final tree.
- Trace representative visible faces through BSP material name -> VMT -> VTF
  -> atlas tile -> shader inputs, including pakfile and oversized-texture
  cases.
- Separate expected newly covered surfaces from incorrect material selection,
  lighting, UV, or atlas sampling.
- Deliver before/after captures at the same pinned demo/tick/camera, named
  refusal counts, and a focused gate. Do not use aggregate tile counts as
  proof of visual correctness.

### T22 - ViewModel and weapon presentation

- Reassess the applied-animation camera/model framing issue and the T18
  rendered pose on a real captured frame.
- Confirm hand mesh ranges, weapon body material slots, and animation pose
  placement independently; do not treat `skinning=1` or `drawCalls>0` as visual
  acceptance.
- Cover at least one model with multiple ranges and different albedo textures.
- Deliver masked frame comparisons, control regions, and mutation checks for
  any changed product logic.

### T23 - World geometry completeness

- Continue the known F1 investigation: terrain/displacement coverage, holes,
  and suspicious empty regions after T11 winding and T17 visibility fixes.
- Use ray/triangle oracles and region-specific captures to distinguish missing
  geometry, visibility rejection, backface rejection, and material darkness.
- Do not change winding, culling, or PVS pins without a measured first-hit
  comparison and a same-view visual control.

### T24 - Lighting and shader semantics

- Measure the currently unsupported or deferred material controls, including
  `dynamicLighting`, specular, bump, `$bumpmap2`, `%detailtype`, and `$ssbump`.
- Change one semantic at a time and compare deterministic captures before
  deciding whether to implement it.
- Keep unsupported features explicitly named until both parsing and rendered
  behavior have evidence.

## Parallel schedule

- T16 is the sole merge owner for T12 and must not overlap another integration
  edit to `p0-entity-protocol`.
- T21-T24 may inspect or develop now in isolated worktrees; avoid
  overlapping ownership of shared renderer, BSP, VMT, or shader files unless
  the owners agree on file boundaries first. Their product validation and
  integration wait for T16 and T20.
- Product builds, mutations, captures, audio-device runs, and any test that
  writes shared build/evidence paths are serialized, even when the code tasks
  themselves are parallel.
- T20 runs after T16 and before claiming the combined T11-T19/T12 tree
  validated. T21-T24 may continue independent code work after T20, but their
  integration requires their own focused checks.
- The 24-step full chain is not routine task validation. Run it only at a
  declared release boundary on a committed, clean, idle worktree.

## Release-status rules

- A green branch gate is not a green post-merge gate.
- A successful build does not prove the executable is fresh; record the source
  tree and build identity used for each run.
- Resource resolution and counters do not prove that the rendered image is
  correct.
- Preserve original failures and superseded runs; label them rather than
  rewriting them as passes.
- Do not publish a release claim while T21-T24 contain unresolved visual
  blockers or while any required final-tree gate remains `NOT VERIFIED`.
