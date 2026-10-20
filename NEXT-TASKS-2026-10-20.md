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

### Required order

1. Start T29, T30, T31, and T32 from `36303c2` in separate worktrees. Their
   implementation work may proceed in parallel.
2. Run their builds, product captures, mutation suites, and device tests one at
   a time. Do not start another validation run until the previous process,
   lock, and shared build output are gone.
3. Deliver and review T29-T32 one by one. After each review, the integration
   owner merges the accepted branch and updates this task file.
4. T33 may audit code, file sizes, warnings, and performance in parallel with
   T29-T32. T33 product edits must wait until the owner of the affected files
   has merged or frozen them; otherwise T33 is audit-only.
5. After all accepted T29-T33 branches are merged, rebuild the final tree and
   run the affected focused gates once more on the merge result.
6. Only then start T28. T28 performs the clean install/lifecycle checks and
   the single `verify-all.sh --quick` release-boundary run.

### Cannot run in parallel

- Any two product builds that use the same host build directory.
- Any two launches that write traces, BMPs, settings, locks, or evidence under
  shared paths.
- Any two mutation suites; a mutation may leave a temporary source change or
  binary until its restore step completes.
- Audio-device tests and any other test that owns the audio device.
- T28 with T29-T33 validation, branch merges, or code edits.
- T33 edits touching files currently being changed by T29-T32.
- Full-chain `verify-all.sh` with every other task activity.

### Can run in parallel

- T29, T30, T31, and T32 implementation in isolated worktrees.
- T33 read-only audit while T29-T32 implement.
- Report writing, static inspection, and evidence inventory that do not invoke
  the product or mutate shared files.

I review each delivered branch and report blockers or safe fixes. The
integration owner merges accepted branches and updates this file after each
merge; I do not merge task branches on the implementer's behalf.

## Release review result

T28 was executed on frozen commit `ac7e2b7` and pushed successfully, but the
release verdict is **NOT PASS**. The full chain had 23/24 steps passing; step 22
`WEAPON-ANIMATION` remains red because T29's near-plane fix makes the pinned
camera frame stable and occludes the old animation population. Four pixel pins
also need a task-owned re-baseline with mutation evidence. This is a release
blocker record, not permission to weaken thresholds.

## Next task wave

All tasks below start from `ac7e2b7` in separate worktrees. They may be
implemented in parallel, but each task's build, capture, mutation, and device
validation is serialized with every other task.

### T34 — Near-clip and weapon-animation regression closure

**Owner:** follow-up to T29/T30. **Blocker:** `verify-all.sh` step 22.

1. Reproduce `WEAPON-ANIMATION` on the frozen tree at the exact pinned ticks.
2. Compare the pre-T29 near plane and the 4-map-unit plane using the same
   hidden-window inputs; record camera position, near/far planes, model bounds,
   draw calls, frame hashes, and animation deltas.
3. Determine whether the gate should use a camera/model-aware view, a different
   valid tick, or a product correction. Do not restore the incorrect 602-map-unit
   near plane just to make the gate green.
4. Add a mutation that reintroduces the old near-plane behavior and a control
   that proves the room remains visible.
5. Run the weapon-animation forward/mutation gate and the affected ViewModel
   gates after the fix.

**Exit:** step 22 passes without regressing the T29 room visibility, or the
remaining limitation is explicitly measured and assigned before T28 reruns.

### T35 — Final pixel-pin re-baseline

**Owner:** visual acceptance. **Blocker:** four T29-shifted pins and specular
population extinction.

1. Capture the final hidden-window frame at every pin's exact tick on `ac7e2b7`.
2. Re-measure material semantics, lighting semantics, T26 hole seams, and all
   dependent visual regions; compare against T29's before/after frame masks.
3. For each moved pin, prove whether the old population is occluded, replaced,
   or genuinely broken. Keep `specular -> pixel` as `NOT VERIFIED` if the
   original population no longer exists.
4. Update pins only with a same-tree forward frame, mutation, source hashes, and
   a population explanation. Preserve superseded readings.
5. Run the affected forward and mutation gates twice where required for
   deterministic confirmation.

**Exit:** all moved pins have current evidence, or remain named blockers. No
   broad tolerance widening.

### T36 — Lighting, lightmap, and back-face dark-region closure

**Owner:** rendering audit. **Blocker:** T29 residual black is 98.7% lightmap
driven; back-face share is still `NOT VERIFIED`.

1. Use `--world-face-id`, lightmap coordinates, and the T26 account on the
   T29-fixed frame to classify dark pixels as front/back face, empty lightmap
   texel, zero lightmap data, albedo, or shader output.
2. Produce a population ledger with frame masks, not only aggregate counters.
3. Test one controlled lightmap decode/encoding change and one back-face control;
   reject changes that fill pixels by drawing the wrong surface.
4. Add mutations for lightmap UV/page selection and face-side classification.
5. Re-run T25, T26, T27, and lighting-focused gates after any product fix.

**Exit:** dark-region root cause is fixed with pixel evidence, or the remaining
black population is a named Source/map semantic limitation.

### T37 — Installer and `pak01_dir.vpk` readiness

**Owner:** installation/resource pipeline. **Blockers:** no installer lifecycle
features and shipped `pak01_dir.vpk` unreadable on this machine.

1. Reproduce the `pak01_dir.vpk` refusal with `vpk_query`, the product, and a
   clean copied archive; distinguish corrupt archive, unsupported format, and
   path/permission failure.
2. Verify all required VPK volumes and directory indexes from a clean resource
   root; report selected store and refusal reason for each.
3. Implement the smallest packaging/lifecycle slice required by the product
   contract: version metadata, install/uninstall, upgrade/migration, and clean
   first launch. Do not bundle TF2 resources.
4. Add fixtures for missing, truncated, unreadable, and valid VPKs, plus
   precedence reversal; run forward and mutation tests in a clean directory.
5. Record installed file list, sizes, exit codes, and restart behavior.

**Exit:** installer lifecycle is real and tested, and `pak01_dir.vpk` is either
usable or rejected with a fully attributed external/environment cause.

### T38 — Final performance and footprint cleanup review

**Owner:** audit-only unless a measured low-risk fix is approved.

1. Review T33's 565 MiB duplicate evidence inventory and remove only files that
   no report, hash list, or gate references.
2. Measure Release executable sizes, peak working set, atlas memory, texture
   upload/reuse counts, and steady-state frame time on the fixed demo.
3. Inspect repeated VMT/VTF parsing, atlas copies, per-frame allocations, and
   diagnostics that remain in production paths.
4. Submit before/after measurements and a deletion manifest. Do not delete
   tracked evidence or change product behavior without a focused gate.

## Next execution order

1. Start T34, T35, T36, and T37 in isolated worktrees from `ac7e2b7`.
2. Run T38 as a read-only audit in parallel; its code edits wait for affected
   task branches to freeze.
3. Review and merge accepted branches one at a time; after each merge, run only
   the affected focused gates.
4. After T34-T38 are merged, run one serialized final-tree regression covering
   T25/T26/T27, ViewModel, audio, install, and quality gates.
5. Only after that regression is green or every remaining blocker is explicitly
   accepted may T28 be rerun. The full `verify-all.sh --quick` run remains the
   last operation.

### Cannot run in parallel

- Any product build, launch, mutation, capture, or audio-device test with
  another such operation.
- T28 with T34-T38 validation, merging, or code edits.
- T35 pin changes while T34 changes the camera/near-plane behavior.
- T36 lighting edits while T29/T35 are modifying the same world shader or
  frame-accounting files.
- T37 clean-install tests while another task uses the same staged directory.
- The full `verify-all.sh --quick` chain with any other task activity.

## Merge log (integration owner)

- 2026-10-20 — T33 `task/t33-quality-audit@132caa7` merged as `8628ab8`.
  Audit-only delivery: one new gate (`quality-audit-check.sh`), the audit
  report, and frozen evidence; no product source changed. Owner readings on
  `36303c2`: 20 deterministic load-time world pins + same-scene determinism +
  steady-state envelope; mutation `maxTilesPerBatch 64->20` captured (batches
  2->5, atlases 2->5, frame hash moved), source restored byte-identical, and
  the restored build reproduced the reference. The earlier FAIL run is
  committed as `evidence/quality-audit/run-log-superseded.txt` (gate-script
  bug: `assert_pins` read `ref-a.pins` while `forward()` wrote `ref-a.out`).
  Decisions: the deletion proposals are NOT applied (committed evidence is
  not deleted in this wave; 565.0 MiB of duplicate content is inventoried for
  a separate cleanup task); the non-reproducible link finding (three relinks
  of one source produced three binary hashes) is recorded for T28, which will
  hash only the final build's artifacts and will not claim cross-build
  reproducibility. The post-merge gate rerun for this branch is folded into
  the combined T29-T33 merge verification, because this merge changes no
  product source and no other branch has been merged yet.
- 2026-10-20 — T32 `task/t32-resource-install@84b67c7` (+docs follow-up `3bf941d`)
  merged as `4cc6f15` / `3bf941d` cherry-picked. Product: `native/src/main.cpp`
  now reports a VPK that is present but unreadable as a named refusal
  (`vpk: refused file=<name> reason=<reader reason>`, `vpk: archives opened=N
  refused=M`, title suffix `VPK refused: N`) instead of skipping it silently,
  and the `resources:` line separates the requested root from the resolved
  root, so a refused `--tf-root` names the path it tried. Owner readings on
  the branch: forward `INSTALL-READINESS=PASS` (13 cases / 102 checks / 0
  mutations); mutation `INSTALL-READINESS-MUTATION=PASS (3 caught, sources
  restored)` — precedence reversal, 32-bit narrowing accepting an out-of-range
  VPK entry, and a missing staged install file; all three sources restored
  with `cmp` and the rebuild reproduced the reference readings.
  Release blockers recorded with owner T28: no installer package, no
  uninstaller, no version/upgrade metadata, no shortcuts/file associations,
  and TF2 resources are not bundled (by design). Staging is not an installer
  and the branch does not claim one. Post-merge gate rerun is folded into the
  combined T29-T33 verification.
- 2026-10-20 — T29 `task/t29-world-visual@21bfffd` merged as `1c24d22`.
  Product: the world pass built its projection with `near = 0.05` in the
  normalized box unit `uploadWorldGeometry` maps the map into, i.e. 611 map
  units on `pl_odyssey`, so the room the camera stands in was clipped out of
  every frame. The near plane is now stated in map units (4 map units, the
  Source first-person distance) and converted through `worldHorizontalScale_`,
  and `gpu: world clip near/far nearMapUnits farMapUnits horizontalScale
  state=published` publishes the planes the matrix was built from. Only
  `native_renderer.{h,cpp}` + `main.cpp` changed; winding, PVS and culling
  untouched.
  Evidence: `world-near-clip-check.sh` forward PASS (nearMapUnits=4.00,
  18 of 25 named pixels nearer than the old clip, W1 18.8/21.90, W2
  57.0/24.19, W3 133.9/31.03 on the re-pinned baseline, W4 42.9/55.55/
  nearBlack 0.6784 exactly unchanged); mutation PASS with 3 reads RED
  (nearMapUnits 610.22, W1 75.3, 0 of 25), sources restored byte-identical and
  the restored build reproduced the reference frame; `world-material-face-check.sh`
  PASS; `visual-baseline-check.sh` forward PASS on the re-pinned baseline.
  The re-pin is justified by the control region and by the revert mutation,
  not by widening a tolerance. The old readings are kept as superseded.
  Decisions/blockers: T26's sky-account gate was not re-run on the fixed frame
  (Lead runs `world-geometry-hole-check.sh` on the merged tree and re-pins
  deliberately if the frame change moves it); the residual black is classified
  sky 17 px (0.008%), lightmap-driven 205,911 (98.7%), albedo/shader 2,702
  (1.3%), which is recorded as a new task-sized blocker (T34 candidate:
  back-face / empty-lightmap-rect dark regions) with its cheapest next step;
  back-face share NOT VERIFIED (needs `--world-face-id`), near/far depth ratio
  1:305,101 with the depth-resolution table recorded and z-fighting NOT
  VERIFIED. `visual-baseline-check.sh --mutation` was not run by the owner and
  is part of the combined verification.
- 2026-10-20 — T31 `task/t31-audio-content@ef11a61` merged as `81c6c94`.
  Product: `WavPcmData` carries the codec its bytes came from, the scheduler
  gains five MP3 counters plus a per-event `ContentObserver`, `main.cpp`
  prints one `audio: mp3 …` decision line per MP3 event and appends
  `soundEventsMp3*`/`soundEventsInaudible` to `audio: summary`; the `inaudible`
  outcome (resource decoded, listener out of range, 1267 of 1546 previously
"missing" events on the pinned recording) is now named instead of folded into
  `eventsMissingResource` (the existing accounting identity is unchanged). The
  ACM decoder path is untouched.
  Gates: new `verify-audio-content.sh` — forward `VERIFY-AUDIO-CONTENT=PASS`
  (32 assertions, 20/20 capture mutations), device `PASS` (39 assertions,
  25/25) with the same window 0→7700 on a null sink and on device 6: the two
  scheduled world MP3 events (tick 4650 `inaudible` in both; tick 7675
  `skipped` vs `played`) are pinned per event; refusal fixtures name
  `invalid_wav` and `unsupported_format` and count refused events. A real
  source mutation (drop `out.codec = SoundCodec::Mp3`) is invisible to every
  total and is caught by the forward gate; source restored byte-identical
  (`c20bddd3…`) and the record reproduced. Existing `verify-audio.sh` stays
  green (235 assertions / 25 caught; 240/27 with device); its only edit is the
  scheduler mutation-count pin 10→11, with the new flip documented.
  Cross-task finding recorded for the final report: their corpus probe found
  the shipped install's `pak01_dir.vpk` OPEN FAILED — with T32's fix the
  product now names that as `vpk: refused`, so this is an install defect to
  attribute, not to hide. NOT VERIFIED (no input): temp_entity/PCF sound
  events, MP3-named music events, human listening, non-device-6 devices,
  MP3 loudness/latency, whole-corpus 1687-recording count.
- 2026-10-20 — T30 `task/t30-model-animation@fb62c82` merged as `3ffd6cb`.
  Product sources are unchanged (`git diff 36303c2 -- native/src native/include`
  empty). Delivery: `native/tools/model_robustness_probe.cpp` + CMake target,
  `viewmodel-robustness-check.sh` (6 sections), `model-inventory-sweep.sh`, the
  full 363-model inventory (326 `c_*` + 37 `v_*`; 186 single-range, 148
  multi-range with distinct VTFs, 9 `*_animations.mdl` refusals), and the report.
  Real inputs: `c_rocketlauncher` (1 range / 20,748 verts / 1 VTF / 4 LODs),
  `c_syringegun` (2 ranges on `c_syringegun.vtf` + `c_syringegun_glass.vtf`),
  and a real `v_scattergun_scout` whose VVD/VTX were omitted -> named refusal
  `viewmodel companions are missing`. Named LOD-0 refusal, named LOD-1
  fallback (`lodFallback=1`), and the animation-companion absence reported as a
  named degradation (not a refusal), kept separate as requested.
  Gates: `VIEWMODEL-ROBUSTNESS=PASS (203 assertions, 0 failures)`;
  `MUTATION-CAUGHT=PASS (4 of 4)` with byte-exact source restoration against
  HEAD and the restored build re-drawing the committed applied frame;
  contact `nearFraction 0.0705 bind / 0.0694 applied`, `contactDistance=0`,
  98.4% kept; pose 10,097 px changed, 10,095 inside VM coverage. `.gitattributes`
  gains `evidence/t30/** -text` (same rule as `evidence/weapon-material/**`).
  NOT VERIFIED (the branch changes no product code, so expected to hold but not
  a run): sibling viewmodel/weapon gates; the combined verification runs
  `viewmodel-range-check.sh` and `viewmodel-companion-check.sh`. Also NOT
  VERIFIED: animation-companion absence as a product refusal (no shipped model
  has an empty own table + declared companion), LOD fallback only via fixture,
  one paused tick/one sequence, only three models drawn in pixels.





