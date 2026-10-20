# Next Task Plan — 2026-10-20

## Collaboration contract

- Current integration base: `p0-entity-protocol@36303c2`.
- The implementer owns task worktrees, implementation, focused tests, commits,
  and task reports.
- The integration owner merges task branches and resolves ordinary merge
  conflicts.
- I review the submitted diff, check the evidence and mutation quality, solve
  difficult defects, propose targeted optimizations, and issue the next task
  assignments. I do not merge task branches on the implementer's behalf.
- Every task must use its own `D:\TF2_Native_Worktrees\<task-name>` worktree.
  Do not edit `D:\TF2_Native_Test` or another task worktree directly.
- Product launches, builds, captures, mutations, and audio-device runs are
  serialized on this host. Development may be parallel; validation is one
  active producer at a time.
- Render tests must pass `--hidden-window --audio-disabled`. Do not place a
  test window over the user's active window.
- Do not run `verify-all.sh` until T29-T33 are reviewed and T28 is explicitly
  started.

## Delivery contract for every task

1. Start from `36303c2` unless the task says to rebase on a newer integration
   commit.
2. Record the exact input demo/map/resource root and all command lines.
3. Build only the affected targets first; record compiler errors and warnings.
4. Add or update a focused forward gate and a real mutation gate. A mutation
   must change a product behavior or pixel that the forward gate can observe.
5. Restore mutated sources byte-for-byte and prove the restored build reproduces
   the reference reading or frame.
6. Include a report with changed files and reasons, source/binary hashes,
   forward and mutation exit codes, evidence inventory, limitations, and clean
   worktree/process status.
7. Do not rewrite an old failure as a pass. Mark unavailable inputs or
   unobservable paths `NOT VERIFIED` with the cheapest next verification.

## Parallel tasks

### T29 — World visual quality and material correctness

**Goal:** Make the rendered map look like a readable TF2 scene after T26 proved
that the apparent empty regions are map sky, not missing triangles.

**Scope:** W1/W2/W3/W4 frame regions, terrain/albedo/lightmap composition,
atlas UVs and batch binding, sky/background ordering, material shader semantics
that are still visibly wrong. Use T25/T26/T27 readings as controls. Do not
change winding, PVS, or culling without a first-hit oracle and a same-view
pixel improvement.

**Steps:**

1. Capture the current hidden reference frame at tick 2000 and save region
   statistics plus a visually inspectable PNG outside the tracked evidence
   path.
2. Classify each bad region as geometry coverage, resource lookup, UV/tile
   mapping, lightmap, shader semantic, or camera composition.
3. Trace at least 12 representative pixels through BSP face -> material -> VMT
   -> VTF/lightmap -> shader input. Name refusals instead of using fallback
   colors as proof.
4. Implement the smallest fix that changes only the named region; include a
   control region that must remain unchanged.
5. Run `visual-baseline-check.sh`, the relevant material gate, and their
   mutation forms with hidden captures. Keep T26 sky-account pins intact.

**Exit:** W2/W3 visual defect fixed with pixel evidence, or a precise remaining
`NOT VERIFIED` cause and a new task-sized blocker. No threshold-only fix.

### T30 — Model, animation, and viewmodel robustness

**Goal:** Ensure player hands, weapons, and animated models render correctly
across real multi-range/multi-LOD inputs, not only the pinned scattergun.

**Scope:** MDL/VVD/VTX range selection, companion animation binding, applied
bone transforms, weapon material slots, LOD-0 refusal behavior, malformed model
inputs, and at least one non-sniper model with distinct per-range textures.

**Steps:**

1. Inventory installed `c_*.mdl` and weapon models by range count, texture-slot
   count, animation source, and required companion files.
2. Select three real inputs: single-range, multi-range with different VTFs, and
   malformed/missing companion data.
3. Add deterministic probes for selected range count, bound slot, bone coverage,
   applied transform, fallback reason, and draw-call count.
4. Capture bind/applied frames with a fixed tick and hidden window. Use masks
   and contact measurements to prove that hands remain attached to the weapon.
5. Run animation, companion, range, weapon-material, and presentation gates;
   add mutations for wrong range, wrong slot, missing companion, and transposed
   bone data. Restore every source byte-for-byte.

**Exit:** all three model classes either render correctly or produce a named,
deterministic refusal. Do not claim full animation coverage from one sequence.

### T31 — Audio content completeness

**Goal:** Close the remaining audio content gaps while keeping MP3 decoding on
the mature platform decoder path.

**Scope:** MP3 events that are actually scheduled, Source sound-character
prefixes, music/ambient classification, device versus null sink accounting,
and explicit refusal of unsupported formats. Do not invent TempEntity/PCF
coverage when the selected corpus contains none.

**Steps:**

1. Scan the full available corpus and separate playable MP3, WAV/ADPCM,
   music, filtered voice, TempEntity, and PCF candidates.
2. For every selected MP3 event record tick, source-table index, normalized
   path, decoder status, duration, and playback result.
3. Verify one null-sink run and one real-device run with the same demo/tick
   window. Keep device tests serialized and hidden where possible.
4. Add negative fixtures for invalid MP3, silent MP3, missing decoder, and
   unsupported format. The product must report a named refusal, not silently
   drop the event.
5. Run `verify-audio.sh --mutation` and `--device --mutation` only after the
   forward content gate is green.

**Exit:** MP3 playback chain is verified for real scheduled content; music,
TempEntity, PCF, or other devices remain explicitly `NOT VERIFIED` when inputs
are absent.

### T32 — Resource discovery and installation readiness

**Goal:** Make a fresh user install find the right resources and fail clearly
when resources are absent or corrupt.

**Scope:** loose files, VPK directory/volume entries, BSP pakfiles,
`tf/download/maps`, explicit `--tf-root`, clean install layout, first launch,
restart, migration, uninstall, and the existing CMake install staging rules.

**Steps:**

1. Build a clean temporary install directory; do not use the developer tree as
   the only test root.
2. Test loose > VPK > BSP pakfile precedence, downloaded maps, missing maps,
   empty/truncated/corrupt VPKs, and 32/64-bit offset bounds.
3. Run a first launch, close, restart, explicit-root launch, migration, and
   uninstall test. Capture exit codes and selected resource store for each.
4. Verify every installed executable and required runtime file from a fresh
   directory; record the exact package file list and sizes.
5. Add mutation cases for precedence reversal, corrupt-entry acceptance, and
   missing install files. Keep product behavior unchanged outside the named
   failure.

**Exit:** clean-directory install and lifecycle are verified, or each missing
installer feature is listed as a release blocker with an owner. No installer
claim from `cmake --install` staging alone.

### T33 — Code quality, performance, and footprint audit

**Goal:** Reduce avoidable CPU/GPU work, memory, binary/evidence bloat, and
warning noise without changing rendering or playback semantics.

**Mode:** The audit may run in parallel with T29-T32. Any product edit must wait
until the affected task owners have merged or explicitly frozen their files.

**Steps:**

1. Inventory tracked and untracked files by size; identify generated BMP/trace
   duplication, stale scratch outputs, and evidence that is not referenced by
   a report. Delete only files proven disposable and record the deletion.
2. Build Release and record executable sizes, target count, compile warnings,
   peak working set, texture upload/reuse counts, and frame time on the pinned
   demo.
3. Inspect hot paths for repeated VMT/VTF parsing, atlas copies, per-frame
   allocations, redundant CPU mirrors, and unnecessary shader resources.
4. Propose changes with before/after measurements. Do not optimize by removing
   diagnostics or weakening refusal checks.
5. Run the smallest affected gates plus a deterministic frame comparison;
   preserve source hashes and mutation coverage.

**Exit:** submit an audit report first. Only measured, low-risk fixes may be
committed; broad refactors and new dependencies are out of scope.

## Final task

### T28 — Release packaging and final acceptance

Start only after T29-T33 have been reviewed and their release blockers are
closed or explicitly accepted. T28 owns the only full-chain run:

1. Freeze the integration commit and ensure a clean worktree and idle host.
2. Run the focused gates once more on that exact commit.
3. Verify clean install, first launch, restart, explicit root, migration, and
   uninstall.
4. Run `verify-all.sh --quick` once at the declared release boundary.
5. Review all `PASS`, `FAIL`, `NOT VERIFIED`, and `UNSUPPORTED` labels against
   the final code; publish only claims supported by current evidence.
6. Push the final integration commit and verify `git ls-remote`, ahead/behind,
   artifact hashes, and no residual processes.

## Assignment order

- Start T29, T30, T31, and T32 in separate worktrees; they may develop in
  parallel, but their validation runs are serialized.
- Start T33 as an audit-only task in parallel. Delay overlapping code edits
  until the affected task branch is integrated.
- I review each delivered branch and report blockers or safe fixes.
- The integration owner merges accepted branches and updates this file after
  each merge.
- T28 is blocked until all preceding reports, focused gates, and current-tree
  evidence are reviewed.
