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

- 2026-10-20 — T38 `task/t38-perf@6de31fb` (audit `90b3e83`) merged as `82e5c66`.
  Read-only pass: no product source, no pin, no .gitignore, nothing deleted.
  Re-measured on `ac7e2b7` (not copied from T33's 36303c2 numbers): 3,447 tracked
  files / 1,324,098,756 B (T33: 3,351 / ~1,301 MiB); 212 duplicate sha256 groups /
  1,379 files / 598,588,586 B wasted = 570.9 MiB (45.2%); only 4 candidates
  (4.93 MiB) are last copies of their bytes with no reference anywhere — the
  per-candidate keep/drop manifest is committed as
  `evidence/t38-perf/deletion-manifest.txt` for the integration owner, and nothing
  was deleted.
  Gates: committed `quality-audit-check.sh` unmodified (`git diff HEAD` empty;
  working-tree sha256 95e63d9c…, blob af1390f1…, difference is core.autocrlf
  rendering LF as CRLF, `git ls-files --eol` = i/lf w/crlf) — forward exit 0, all 20
  pins held, two captures byte-identical (6bb4d9a7…); mutation exit 0 with
  `maxTilesPerBatch 64→20` going red as required (atlas_batches 2→5, atlas_count
  2→5, layerBytes 229,322,128→133,154,784), source restored byte-identical
  (5d403654…), rebuilt tree reproduced the reference readings and frame.
  Build: rc=0, 366 s, 0 errors, 10 warnings (same ten as T33), exe_count=42,
  total_exe_bytes 21,800,960.
  Runtime vs T33 on the same inputs: atlas 2 textures / 89.7 MiB and atlas-plan
  layerBytes 218.7 MiB unchanged; mean frame 15.89→15.45 ms; fps 65.9–66.7→64.8/64.9
  median; peak working set 2.18–2.83 GiB→2188/2185 MiB; steady growth negative.
  **The 66.7→64.9 fps delta is not attributed** and stays a named next step
  (side-by-side 36303c2 run diffing the CSVs).
  Correction to the previous pass recorded here: scene updates are 84.2/s
  (1,922 `scene: begin` over 22.815 s), not 66.7/s, so the per-update
  `ModelInstance` vector rebuild (main.cpp:6177-6438, native_renderer.cpp:3121-3179)
  is priced at roughly 1,500–2,500 allocs/s **by reading code only** — no
  wall-clock attribution was made and it is not claimed as a regression. VMT
  parsing and VTF decoding are load-time once each (no repeated-parse defect);
  bone buffer grows geometrically (no thrash); 18.2 trace lines per scene update
  are all gated on TF2_NATIVE_TRACE. Two stale "TEMP DIAGNOSTIC" labels remain
  (main.cpp:52, :6174) — inert, flagged for revisit if the 0xE06D7363 crash in that
  try/catch is ever fixed.

- 2026-10-20 — T36 `task/t36-lighting@08786cb` merged as `e209328` (41 files, 36
  evidence files, new gate 510 lines, report 289 lines; **zero product source
  changes**). T36 answers the item T29 left `NOT VERIFIED` — the back-face share
  of the residual black — and the answer overturns the earlier reading.
  The inherited gate's plane-side rule called 86.00% of the drawn face-id
  population BACK, but that population has already passed `D3D11_CULL_BACK`, so
  the rasterizer calls every pixel in it FRONT. Scoring four candidate rules
  against that known population selects the inverted sign (86% agreement) over
  the rule as written (14%) — every front/back number in the account was swapped.
  Calibrated result: drawn front 645,326 (86.00%) / back 105,065 (14.00%); dark
  front 171,030 (82.85%) / back 35,410 (17.15%); dark rate 26.50% front vs 33.70%
  back over a 27.51% base = **relative risk 1.272**. So the back-face share is a
  mild 1.27x enrichment, **not** the explanation for a fifth of the frame, and
  T29's back-face hypothesis is not supported at that strength.
  What survives: 97.51% (front) / 99.38% (back) of dark pixels sit on lightmap
  rectangles the map baked below 96/255, and 98.69% of the dark population has a
  dark lightmap term over a bright albedo, so **99.94% is the map's own lighting,
  not a product defect**; 1.98% (4,123 px) is product-attributable and stays named.
  Forward: `bash lighting-dark-account-check.sh --reuse` exit 0, 21 assertions,
  0 failures, reproducible; the gate launches no product so it held no host lock.
  It gains §3b, which scores the side rule against the rasterizer's own cull and
  fails if the sign flips back, so the inversion cannot return silently.
  Mutation: m2 (side-rule inversion) is a real mutation — it swaps the two
  populations exactly (back/front 170,701/35,227 → 35,227/170,701) while the total
  holds at 208,630 — source restored byte-identical. **m1 (RGBExp32 exponent sign)
  and m3 (`--no-world-cull` ignored) were NOT run** (they need a product build plus
  a capture under the lock); they are wired and fail closed, reported as deferred.
  Four defects fixed that meant this gate had **never** passed forward: a missing
  account directory; `--eye "-2423.6,..."` rejected by argparse at all five call
  sites (it needs `--eye=`); two invented side-sum pins; and the serious one,
  **every mutation treated a crashed tool as RED** (m2's original patch was a
  Python SyntaxError, so the tool died, the count fell back to 0 through
  `|| echo 0`, and `0 != 170698` printed RED; it would have "passed" on any
  syntax error).
  Carried forward as named TODOs: the cull control's `removedSide` (44,046 front /
  37,095 back) is computed with the **same inverted rule**, so that pin currently
  carries a flipped label and needs re-derivation; m1/m3 unrun; the 4,123
  product-attributable pixels are not further split; per-texel (not per-rectangle)
  lightmap projection is still unclaimed; the account covers one camera and one tick.

- 2026-10-20 — wave 2/3 (T34-T38) integrated. Five branches from `ac7e2b7`
  merged into `p0-entity-protocol`; all validation serialized through
  `_shared/hostlock.sh`.

  **T38** `task/t38-perf` → `82e5c66` (log `a00ecf9`). Read-only footprint and
  steady-state audit; nothing deleted, no product change, no pin moved. Re-measured
  on `ac7e2b7` rather than reusing T33's `36303c2` numbers: 3,447 tracked files /
  1,324,098,756 B (T33: 3,351 / ~1,301 MiB); 212 duplicate sha256 groups / 1,379
  files / 570.9 MiB wasted (45.2%); only 4 candidates (4.93 MiB) are last copies
  of their bytes with no reference anywhere — per-candidate keep/drop manifest
  committed as `evidence/t38-perf/deletion-manifest.txt`. Committed
  `quality-audit-check.sh` unmodified: forward exit 0 with all 20 pins held and two
  byte-identical captures (`6bb4d9a7…`); mutation exit 0 with
  `maxTilesPerBatch 64→20` red as required. Runtime vs T33: atlas 2 textures /
  89.7 MiB and layerBytes 218.7 MiB unchanged; mean frame 15.89→15.45 ms; fps
  65.9–66.7→64.8/64.9 median (delta **not attributed**); peak ws 2188/2185 MiB.
  Corrects the previous pass: scene updates are 84.2/s, not 66.7/s, so the
  per-update `ModelInstance` rebuild is ~1,500–2,500 allocs/s **by reading code
  only** — no wall-clock attribution, not claimed as a regression.

  **T36** `task/t36-lighting` → `e209328` (log `f303063`). Zero product changes;
  answers the item T29 left `NOT VERIFIED` and **overturns** the earlier reading.
  The inherited plane-side rule called 86.00% of the drawn face-id population
  BACK, but that population already passed `D3D11_CULL_BACK`, so the rasterizer
  calls all of it FRONT; scoring four rules against that known population picks
  the inverted sign (86% vs 14%). Calibrated: drawn front 645,326 (86.00%) / back
  105,065 (14.00%); dark front 171,030 (82.85%) / back 35,410 (17.15%); dark rate
  26.50% vs 33.70% over a 27.51% base = **relative risk 1.272** — a mild
  enrichment, **not** the explanation for a fifth of the frame. 97.51% (front) /
  99.38% (back) of dark pixels sit on lightmap rectangles the map baked below
  96/255, so **99.94% is the map's own lighting**; 1.98% (4,123 px) is
  product-attributable. Forward `lighting-dark-account-check.sh --reuse` exit 0,
  21 assertions, 0 failures, reproducible; gains §3b calibrating the side rule
  against the rasterizer's own cull so the inversion cannot return silently.
  m2 swap mutation is real (170,701/35,227 → 35,227/170,701, total holds at
  208,630, source restored byte-identical); **m1/m3 NOT run** (need a product
  build + capture) and reported as deferred. Four defects fixed that meant the
  gate had **never** passed forward, including the serious one: **every mutation
  treated a crashed tool as RED**, so any syntax error would have "passed".

  **T35** `task/t35-pins` → `2584d50` + `91b217e`. Zero product changes. Re-pinned
  the pixels T29 moved and **split the pins by origin** as the honesty check:
  every FRAME-derived pin moved (missing 38,328→1,648; enclosed 2,313→1,648; open
  36,015→0; components 44→34; backface 167,030→80,917; both frame shas), every
  BSP-derived pin held (sky faces 731, sky triangles 1,842, buffer triangles
  99,312, enclosed components ≥200px 3, face account 18,052/17,321/731), and
  `unexplained` is 0 on both sides. The terrain occlusion assertion was kept RED
  rather than widened; the 7 pixels are now attributed by **two independent
  instruments that agree exactly** — the product's own `--world-hole-trace`
  (`drawnHits=7 of 7`, face 9,764 WOOD/WALL020B at depth 0.9722) and a
  `--world-face-id` decode (`face_id=9,764` at all seven) — so the gate gains a
  colour-blind second arm (`composed != none`) instead of a looser tolerance.
  `seamCulled 2→0` is **left red deliberately**: the gate's own failure message
  blames `world-ids-nocull.bmp` not being read, which T35 disproves with the
  account tool's own loader (all 32 directions around the 8 representative points
  resolve from the CULL-ON frame, seam faces 8555/15324/17217/17260/17262/17266/
  17268/17326 named, and 81,218 px are still drawn only with the cull off), so
  the mechanism lives and simply no longer falls on a surviving sliver.

  **T34** `task/t34-nearclip` → `97c10b2` (pin fix `a5c00d8`). Closes verify-all
  step 22 **without touching T29's near plane**. Route chosen by measurement:
  route (a) "use a tick where the camera moves" is impossible — 5,009 scene ticks
  on the near-clip demo and 1,862 on the weapon demo each resolve ONE camera pose
  (eye exactly 0,0,0, pitch/yaw exactly 0.00), while `mode=first-person`,
  `target=1` and per-tick `follows` prove the product really applies a pose every
  tick. So section 3 reads the pose from the trace sections 1–2 already write (no
  extra launch) and asserts the branch the reading selects: still camera → the
  three frames must be ONE picture and must not be a flat fill; camera moved →
  they must differ (the old assertion, kept as the other arm). A premise
  assertion requires `nearMapUnits ∈ [3,5]` so the conclusion holds only through
  T29's plane. Section 3 fully green: `distinct frames = 1 of 3`, 547 distinct
  sampled colours, `nearMapUnits=4.00`, camera drift 0.000. Control: 28 points,
  20 resolve, **17 nearer than the old 603-map-unit clip**, nearest 121.6 map
  units. Mutation: `nearMapUnits=602.78` (the Lead's 602.78 reproduced exactly),
  named points 20→5, 0 nearer than 603, picture moved 98.1% of its bytes. Both
  frame hashes match the Lead's independent readings (`f841c39f…` T29 build,
  `d91dd163…` old plane). Gate: forward EXIT=1 OK=55 FAIL=1 (the one red is the
  identity-skinning pair); mutation EXIT=0 OK=46 FAIL=10 MUTATION-CAUGHT=PASS.
  The identity 3 bytes are attributed, not masked: 1 pixel / 3 bytes at (622,268),
  max Δ59 on an edge whose neighbourhood also differs by 59 — a silhouette
  sampling difference, not float reassociation; tolerance **not** widened.
  **Lead correction `a5c00d8`**: T34 pinned step 22 at 57 by arithmetic that does
  not match the gate — measured per-section counts are 27/6/5/11/6/1 = **56**
  (section 3 prints five assertions; the branch token is a reading line). A pin of
  57 would have kept step 22 red forever even after the identity fix, so it is
  pinned at the measured 56 with the measurement in the comment.

  **T37** `task/t37-installer` → `d305211` + `ab06a75`. The product becomes its
  own installer and the VPK diagnostic stops lying. **Corrects a blocker this
  report carried**: `pak01_dir.vpk` is **absent**, not corrupt — verified
  independently by the Lead (`Test-Path` false, no `pak01*` entries; the install
  ships `tf2_misc` 5,402,349 B, `tf2_sound_misc` 158,146 B,
  `tf2_sound_vo_english` 560,962 B, `tf2_textures` 1,974,706 B `_dir.vpk`
  indexes, all of which open). The original reading came from
  `vpk_query.cpp:50-56`, which printed `OPEN FAILED` for any `open()` failure and
  discarded the error string, so "absent" and "corrupt" were indistinguishable;
  the real defect was the diagnostic losing the reason. Lifecycle implemented in
  the product: `--version` (from the shipped `VERSION.txt`), `--install <dir>`
  (stages payload under `.uninstall/payload/`, writes a manifest with path, size,
  sha256, state and backup), `--install-status` (VERIFIED/modified + version), and
  `--uninstall` (removes only what its manifest says it installed, restores
  backed-up user edits, names a running image it cannot delete). Three product
  fixes found by gate runs: reinstall made the package's own files unremovable
  (ours-and-identical now stays `new`, ours-but-edited becomes `changed` with a
  backup); installing onto a directory whose image is running no longer steals the
  caller's redirected stdout and records `VERSION.txt` in the manifest; the VPK
  diagnostic keeps the reason. Readiness gate re-pinned for the new package shape:
  **107 checks, 0 failures, exit 0** (the Lead re-ran it clean after a first run
  was polluted by a mid-edit worktree).

  Integration edits this round: `2429791` withdraws the pak01 blocker in
  `T28-RELEASE-ACCEPTANCE-2026-10-20.md`; `4a51668` pins `exe_count` 42→44 for
  T37's `install_cli_probe` and `vpk_readiness_probe` (measured two ways:
  `grep -c '^add_executable(' = 44` and a clean full Release build printing
  `cmake_build_rc=0 errors=0 exe_count=44`, `exes_on_disk=44`; the warning count
  moved 10→11 for a C4189 in T37's own code, warnings not asserted by design).

  Merged-tree focused gates: `verify-fast --no-build` PASS (43),
  `quality-audit-check.sh` PASS, `world-near-clip-check.sh` PASS,
  `lighting-dark-account-check.sh --reuse` PASS,
  `world-geometry-hole-check.sh` FAIL on the one `seamCulled` assertion T35
  deliberately left red and attributed.

### Wave 2/3 integration close-out — what the chain found in the integration, and what is still red

**The last complete chain run (24 steps, one clean tree, HEAD `e7a44ae`):
`VERIFY=FAIL`, 22 of 24 steps green.** Two steps were red, and they are
different in kind:

| step | reading | cause |
|---|---|---|
| 22 weapon-animation | `WEAPON-ANIMATION=FAIL (assertions_ok=55)` | **real**: one assertion, the identity-vs-control bind-pose pair, differs by 3 bytes |
| 23 viewmodel-animation | `VIEWMODEL-ANIMATION=FAIL (assertions_ok=154)` | **stale pin**: the gate itself printed `VIEWMODEL-ANIMATION=PASS` with 154 assertions and `verify-all.sh` demanded 139 |

**The step-23 pin was a forgotten edit in `b343728`** (from the T29-T33 wave, not
this one). That commit's message and the comment it added both say "139 -> 154",
and in the same commit it really did move step 24's pin 108 -> 114 — but it left
step 23's number at 139. Step 23 has therefore been red on every chain run since,
while the gate printed PASS. Repaired in `ff75490`: the pin is the measured 154,
and the per-section reading taken from the chain's own log is
5 / 6 / 17 / 13 / 11 / 42 / 48 / 12 = 154. Re-measured three times now (that
wave's chain run and standalone rerun, both 154, plus this run, 154).

**Step 22 is the one real red, and it is 3 bytes.** Sections 1-6 are green except
that pair; the chain also ran both of its mutations green
(`WEAPON-ANIMATION-MUTATION=PASS`, `WEAPON-ANIMATION-MUTATION-MODEL=PASS`), so
the gate is still load-bearing. The measured reading:

```
BMP-DIFF differing=3 total=2582352 percent=0.0001 max_delta=59 extent=1264x681x24
status=FAIL reason=whole frame max_delta=59 exceeds 1
```

one pixel, three channel bytes, channel delta 59, on a silhouette. The claim
"identity skinning does not deform the bind pose" survives at the percentage bound
(0.0001% of 8.6 px allowed) and fails only the `--max-delta 1` bound — which is
exactly the bound a pixel that crosses a pixel boundary cannot satisfy, no matter
how small the underlying float difference is.

**T34 attributed those 3 bytes to T29's near plane. The Lead's test refuted that
attribution, and the refutation is recorded rather than dropped.** The mechanism
T34 proposed is real in the code: `worldHorizontalScale_` defaults to `1.0f`
(`native_renderer.h:1558`) and is only assigned by `uploadWorldGeometry`
(`native_renderer.cpp:1395`, `:2113`), so a run that uploads no world geometry
states its near plane as `max(1.0 * 4.0)` = **4.0 box units**, 80x the `0.05` the
same path used before T29. The Lead implemented the corresponding repair — apply
the map-unit conversion only when world geometry is actually uploaded — rebuilt,
and re-ran the pair. **The pair still differed by exactly 3 bytes at delta 59.**
So either the `--model` path does upload the map's world geometry (making the
new condition true and the change a no-op, which is what the identical reading
suggests) or the near plane is not the cause. Either way the attribution is
**not established**, the change bought nothing, and it was reverted
(`git diff native/src/native_renderer.cpp` vs `ac7e2b7` is the T34 camera-trace
commit only; `kWorldNearMapUnits = 4.0f` untouched). The honest state of the
3 bytes is: **measured, not attributed**, and the next step is named below.

**T37's last three commits** (`7ef48a3`, `d529b84`, `b418d94`) landed in
`0614d33`: the mutation half now proves a mutation ran its own binary (compare
the exe hash across the rebuild), **M2 is recorded as an UNCAUGHT mutation rather
than rewritten as a pass** (M1 and M3 caught; the single-point `if(false)` rebuild
succeeded and the reading still showed `preserved=1`, so one mutation does not
change the deletion candidate set), and the VPK four-way attribution experiment is
committed as evidence.

**What is not claimed.** The full chain was **not** re-run after `ff75490`. On the
measured step verdicts the chain should now read **23 of 24 with step 22 the only
red**, because step 23's gate verdict was already PASS with exactly 154 assertions
and only the pin disagreed — but that is a derivation from two measured numbers,
not a re-run, and it is written down as a derivation. The integration owner
stopped the final chain deliberately: a merge landed while it was running (its own
serialization mistake, caught), and the user then asked for the report instead of
another two-hour run. **A chain re-run on `0614d33` is the first thing the next
session should do.**

**Named next step for step 22, in order of preference.** The assertion's own prose
already allows what its number forbids ("a handful of bytes may round differently
… but nothing may move"), so the repair that keeps the claim intact is to measure
the claim: assert that *interior* pixels are identical (max delta <= 1 strict) and
that every differing pixel lies on a silhouette (adjacent to a strong gradient in
the control frame), with the silhouette count capped. That is strictly stronger
where deformation would show and correctly permissive where a pixel boundary
falls. It is a semantic change to a gate, so it needs its own measurement and a
deliberate edit — not a widened tolerance, and not a `--max-delta 59`.

