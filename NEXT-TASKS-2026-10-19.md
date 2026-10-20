# TF2 Native Demo Player - Next Integration and Work Plan

Date: 2026-10-19
Integration branch: `p0-entity-protocol`
Current integration HEAD: `36d68b0` (T12/T17/T21/T23/T24 merges plus T20 records; includes T11-T24 integration work)

## Workspace cleanup

On 2026-10-19, clean completed-task worktrees were removed from
`D:/TF2_Native_Worktrees` to reclaim disk space. Their Git branches and commits
remain available. Worktrees with uncommitted changes were retained and must be
reviewed before any later cleanup.

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

## Completed T21-T24

T21, T22, T23, and T24 are integrated and have current-tree focused results.
Their reports and commits remain the historical evidence; do not reassign these
numbers or use them for new fixes.

## Next tasks after T20

These tasks use separate worktrees and a committed base. Product launches,
builds, captures, mutations, and audio-device runs remain serialized on this
host. No task may edit another task's worktree or the integration branch while
T25 is being integrated.

### T25 - Final visual baseline closure (completed)

- Start from `36d68b0` and reproduce the hidden-window T17 baseline failure.
- Attribute every changed reading: material-fetch 85/85/70, blend 10/10/10,
  W1, and W2. Use T19/T21/T12 evidence to separate expected changes from a
  rendering regression.
- Produced reviewed final pins from the current hidden-window frame and trace;
  see `T25-VISUAL-BASELINE-CLOSURE-2026-10-19.md`.
- Forward and mutation forms passed; 5/5 mutations were caught and all source
  restores were byte-identical.
- The regression gate is green, while release visual status remains
  `NOT VERIFIED` because W2 still has the known world completeness defect.
- Do not run `verify-all.sh` during T25.

### T26 - World geometry completeness and visible holes

- Use the T23 accounting matrix to isolate the 38,328 missing pixels and the
  2,282 closed-hole component from the 36,015 top opening.
- Trace representative missing regions back to BSP faces, displacement cells,
  culling, depth, and material darkness.
- Change geometry only when a first-hit oracle and same-view frame show a
  measurable improvement without regressing T11 winding or T17 visibility.
- Deliver a focused gate and mutation coverage for each changed geometry rule.

### T27 - Material semantics that affect visible output (completed)

- Prioritize features that can change the final image: `$bumpmap2`, `%detailtype`,
  `$ssbump`, detail textures, specular, and dynamic lighting.
- For each feature, prove the full chain VMT declaration -> resource lookup ->
  shader input -> pixel delta. Keep unsupported features named when required
  source resources or shader consumers are absent.
- Use one semantic switch per capture and retain directional and deterministic
  controls. Do not accept a counter-only implementation.
- Delivered per-material feature atlases for `$bumpmap`, `$bumpmap2` and `$detail`
  with a texinfo-axis tangent basis; see `T27-MATERIAL-SEMANTICS-2026-10-19.md`.
- Forward gate: 163 assertions on the integration HEAD after the merge, 13 captures
  all first-hit. Mutation gate: 5/5 red, sources byte-identical.
- `$bumpmap` 624 px (355/269), `$detail` with a diagnostic texture 82,213 px
  (dark-only, as its mode 0 predicts), specular 135 px (123/12), and the
  `$bumpmap2` lerp 15,953 px moving both ways (8,790/7,163).
- The declared `$bumpmap2` resource chain is `NOT VERIFIED` on this demo: 4 of the
  5 materials have no layer-1 base (`$ssbump`) and the fifth is occluded at every
  camera tried, so the product path moves 0 px. The shader arm itself is measured
  on a diagnostic texture and a named diagnostic base.
- `$ssbump` and `%detailtype` stay named refusals (BUMPMAP == 2 needs three
  lightmaps; %detailtype is consumed by VBSP at map compile time).
- The first layer-2 reading (38,538 px, all darker) was an invalid vertex state and
  is archived under `evidence/material-semantics-superseded-invalid-state/`.
- T24's lighting pins follow the new `$bumpmap1` alias reading (44 -> 45 declared
  bump materials); the reason is recorded at each pin in
  `lighting-semantics-check.sh`.

### T28 - Release packaging and final acceptance

- Verify clean-directory install layout, first launch, restart, uninstall,
  explicit-root behavior, and resource migration on a clean test directory.
- Review README, CHANGELOG, HANDOFF, and all `PASS`, `FAIL`, `NOT VERIFIED`,
  `UNSUPPORTED`, and `BLOCKED` labels against current HEAD.
- Run the 24-step `verify-all.sh --quick` chain only after T25-T27 are merged,
  committed, and the host is idle. This is the release-boundary run.
- Push the final integration commit and verify `git ls-remote` plus ahead/behind.

## Parallel schedule

- T25 and T27 are merged; T26 is the only remaining task that may develop in an
  isolated worktree, and its validation and merge no longer wait for anything.
- T28 waits for T26 and is the only task allowed to start the full release chain.
- Product builds, mutations, captures, audio-device runs, and any test that
  writes shared build/evidence paths are serialized, even when the code tasks
  themselves are parallel.
- T20 focused work is complete except for the T17 visual baseline closure;
  that closure is T25. T26 and T28 require their own focused checks.
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
- Do not publish a release claim while T25-T27 contain unresolved visual
  blockers or while any required final-tree gate remains `NOT VERIFIED`.
