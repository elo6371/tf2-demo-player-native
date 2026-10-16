# TF2 Native Demo Player - Integration and Release Tasks

Date: 2026-10-16
Integration branch: `p0-entity-protocol`
Current integration commit: `616fef3` before the F6 fix commit

## Project state

The project is a native Windows TF2 demo player. It parses Source demo files,
rebuilds entity state, resolves TF2 resources from loose files and VPK archives,
renders world and first-person ViewModel content, schedules audio, and exposes
camera and playback UI controls.

T1, T2, T3, and T4 deliveries are now merged into the integration worktree:

- T1 ViewModel and weapon manual acceptance: evidence-only, merged from `e0d0711`.
- T2 world visual acceptance: visual evidence and matrix gate, merged from `5941550`.
- T3 audio content and device acceptance: product, gates, and evidence, merged from `829497c`.
- T4 stability and release validation: evidence and release gate, merged from `eb68c61`.

The merge commits are local at this point. Push only after the final release
checks below are complete.

## F6 fix completed

T4 found a real root-contract defect: an existing directory containing empty
`materials`, `models`, and `maps` directories was accepted as a valid TF root,
then reported as connected with zero VPK entries. `AssetRoot::valid()` now
requires at least one `*_dir.vpk` archive in the root before accepting it.

Changed files:

- `native/src/asset_root.cpp`: reject roots without a VPK archive.
- `stability-run-matrix.ps1`: update fixture comments to describe empty roots
  as intentional rejection cases.
- `stability-root-check.ps1`: add an explicit existing-but-empty root case.

Verification already completed:

```text
build-target.sh tf2_demo_native                 rc=0
stability-root-check.ps1                        ROOT-CONTRACT=PASS
explicit-valid-root                             VPK entries=104137
explicit-missing-root                           connected_root=<empty>
explicit-empty-root                             connected_root=<empty>
no-root-auto-search                             VPK entries=104137
```

The full 19-case T4 matrix has not been rerun after this fix. Therefore the
release gate remains `NOT VERIFIED` until the focused stability rerun below is
complete.

## Remaining work

### R1 - Commit and push the F6 fix

1. Review `git diff --check` and confirm no `tf2_demo_native` or `nmake` process.
2. Commit the three F6 files with a focused message.
3. Record the commit in this file and in the final handoff.

### R2 - Focused T4 stability rerun

Run the stability matrix against a binary built from the current integration
commit. Use a fresh run directory and preserve all logs and readings.

Required cases:

- All seven resource-root cases, especially existing-but-empty roots.
- Corrupt demo refusal cases.
- Audio invalid-device refusal.
- Hidden-mode smoke and both soak windows.

Required results:

- `STABILITY-RELEASE=PASS`.
- No residual product or build processes.
- Mutation runs only after the forward gate is green.
- Source and evidence identity recorded from the same commit.

Do not run this matrix concurrently with any other product, build, render,
audio, or mutation process.

### R3 - Affected gate checks

After R2, run these serially from a clean tree:

```text
build-target.sh tf2_demo_native world_material_probe viewmodel_material_probe
verify-fast.sh
verify-render.sh
verify-render.sh --mutation
verify-audio.sh
verify-audio.sh --mutation
```

Use `--device` only for the separate real-device audio check. Do not reuse old
frames or traces as current-HEAD evidence.

### R4 - Final full-chain release check

Run only after R1-R3 are complete and committed:

```text
verify-all.sh --quick
```

This is the only long final-chain run required. Do not edit source, scripts, or
evidence while it is running. A partial run cannot be combined with an older
run to claim a pass. If a pin is stale, fix the pin and rerun the complete
chain.

### R5 - Final release record

1. Update `README.md`, `CHANGELOG.md`, and `HANDOFF-2026-10-07.md` from actual
   final command output.
2. Record all `PASS`, `FAIL`, `NOT VERIFIED`, `UNSUPPORTED`, and `BLOCKED`
   results without relabeling them.
3. Check `git diff --check`, clean status, locks, and child processes.
4. Push the final integration branch and verify `git ls-remote` matches HEAD.

## What must remain explicit

- Real ViewModel animation binding remains `NOT VERIFIED` where the source demo
  has no usable sequence binding.
- Water refraction and Source-equivalent render-target behavior remain outside
  the implemented basic water path.
- Music, TempEntity/PCF audio, MP3 playback, and additional devices remain
  unverified unless a new input and device run is recorded.
- WARP, feature-level variants, installer behavior, cross-machine capture, and
  human visual/GPU performance checks remain environment-dependent.
- T2 visual differences prove input sensitivity; they do not alone prove
  Source-equivalent visual correctness.

## Concurrency rules

- R1, documentation review, and static inspection may be done while no product
  process is running.
- R2 owns the product process, build directory, and stability evidence output.
- R3 gates are serial because they share build artifacts and mutation state.
- R4 is fully serial and cannot overlap any task work or validation run.
- Never edit the integration worktree from another task worktree.
- Never modify `D:\TF2_Demo_Player`.
- New paths, code, scripts, and documentation must use ASCII names and text.
