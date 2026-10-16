# T4 Stability & Release Validation — Delivery Report (2026-10-16)

Status: DELIVERED (evidence committed; see section 0)

## 0. Verdict summary

- **`stability-release-check.sh` on run-07: FAIL — 214 OK / 5 FAIL.** The 5
  FAILs share one root cause (finding F6): the current-HEAD build reports
  `资源已连接` ("connected") for an explicit `--tf-root` that EXISTS but
  serves no VPK (`VPK entries: 0`), where the corrected root contract
  (06a90b3, re-validated by the gate's comment) requires `no root found`.
  The gate's mutation stage refused to run by design (`MUTATION-SETUP=FAIL:
  the forward run is already red`), so `STABILITY-RELEASE-MUTATION=FAIL`
  means "not executed", not "mutations escaped".
- **Every stability reading itself is green**: run-07 ran 19/19 cases
  exclusively (zero foreign residual processes), both 660 s soaks COMPLETED
  on the new review12.dem input with `stall_samples=0`, `last_tick`
  46082/52553 short of the demo's 120936 end tick, clean exits, and the
  memory/thread/frame bounds all held (214 assertions). The corrupt naming
  refusals, the resource degradations and the audio-invalid-device exit-13
  refusal all reproduced run-05 exactly.
- **`stability-root-check.ps1`: PASS** (explicit valid root opens the real
  install with vpk_entries=104137; a missing root connects neither Steam nor
  the reference install; no-flag auto-search connects what it finds).
  The contract's no-silent-substitution core holds; F6 is about the
  weaker "exists but empty" arm of the same contract.
- **Product findings handed to the owners, none fixed here**: F4
  (cp_sunshine content deterministically exits 3 through the renderer's
  device-error policy ~31 s in), F5 (instrument-layer couplings:
  `POV ticks=` title prefix unparseable by the gate; map search skips
  `tf\download\maps`), F6 (root-contract regression above, first reachable
  now because this task is the FIRST run whose `--tf-root` arguments were
  absolute paths; run-05/run-03 passed these 5 assertions only because
  relative paths failed to resolve under the product's working directory —
  false negatives, recorded as such).
- **Release disposition: T4 does NOT certify a release.** The 5 red
  contract assertions are real product behaviour against a valid
  instrument; fixing belongs to the root-contract owner (likely a one-line
  strengthening of `AssetRoot::valid()`/`hasAssetStore` to require a
  servable VPK), after which the deliverable matrix re-runs once.

## 1. Task, branch, worktree

- Task: T4 - Stability and Release Validation (`NEXT-TASKS-2026-10-15.md` section 7).
- Branch: `task/stability-release-head` (new branch, born from the integration HEAD).
- Worktree: `D:\TF2_Native_Worktrees\stability-release-head`.
- The historical T4 worktree `D:\TF2_Native_Worktrees\stability-release`
  (branch `task/stability-release`, HEAD `ea4e661`, merged into the line at
  `06a90b3`) was not touched by this task. Its 2026-10-14 report and `run-03`
  evidence remain the historical T4 record; everything below is a fresh
  current-HEAD delivery and the two are kept separate throughout.

## 2. Base and final commits

- Base commit: `917b0122376419ae0a9de5be35d932a9fa487be3` (`917b012`,
  `p0-entity-protocol` HEAD at task start: "docs: record final chain pin status
  and task plan").
- Final commit: `<<PENDING-FINAL-COMMIT>>`.

## 3. Changed files and why each changed

- `stability-run-matrix.ps1` (+39/-3): the two soak cases now play
  `review12.dem` (SourceTV, map `cp_reckoner`, `SourceTV ticks=120936`) instead
  of the snakewater recording, and the run manifest records `soak_demo`,
  `soak_demo_sha256`, `soak_demo_size`. Why: the gate asserts that a soak's
  last tick is still short of the demo's end tick (the premise that makes
  `stall_samples=0` mean "never stopped drawing"). On the current-HEAD build
  the snakewater demo (64352 ticks) FINISHES inside the >=660 s soak window
  (measured run-05: end reached at t=637.7 s / t=531 s), so no window length
  can satisfy both the gate floor (observed >= 660 s) and the premise on that
  input. The full instrument-input search (SUNSHINE -> product crash finding
  F4; POV snakewater -> gate cannot parse its title prefix; gullyscout /
  processdemo -> their BSPs are outside the player's map search path) is
  recorded in the script comment above `$soakDemo` and in Appendices C/D;
  review12.dem is the input that satisfies every premise (SourceTV title the
  gate parses, BSP served from the official `tf/maps/`, ~3.2x window headroom,
  160 s pretest completed with `BSP: 76505 tris` and lightmap
  `state=source-equivalent`). smoke and the corrupt fixtures stay on
  snakewater.
- `T4-STABILITY-RELEASE-2026-10-16.md` (new): this report.
- `evidence/t4-rerun-20261015-t4b/**` (new): run-05 and run-06 matrix evidence,
  gate summaries, mutation results, the run-05 forward-gate FAIL log, and the
  run-04 superseded note (Appendix A).
- No product source file changed. The explicit `--tf-root` contract fix was
  already merged on the integration line (`06a90b3`); this task re-validates it
  against the current HEAD build rather than re-implementing it.
- `stability-release-check.sh` (the judgment gate) is untouched: the two red
  assertions in run-05 were fixed by correcting the instrument's input, not by
  weakening the judge.

## 4. Real inputs and exact commands

Real inputs (read-only):

- `D:\TF2_Demo_Player\testdata\demos\bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` (51,834,617 bytes) — smoke case and the corrupt-fixture source
- `D:\TF2_Demo_Player\testdata\demos\4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem` (46,921,111 bytes) — res-missing-bsp (its map is not served by the root)
- `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\review12.dem`
  (80,261,474 bytes; sha256
  `f1e91c6cb2e8abd20d45107be86e0b0227ef7e841cd74486e498c820bc361974`;
  SourceTV, map `cp_reckoner`, title-reported `SourceTV ticks=120936`) — both
  soak cases (runs run-07 onward; run-06 soaks played SUNSHINE.dem and are the
  F4 diagnostic, Appendix C)
- TF root: `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf` (the real install)
- The corrupt fixtures are cut from a byte copy of the snakewater demo by
  `stability-make-inputs.ps1` (empty, bad magic, bad version, truncated header,
  truncated packet, mid-truncated, incomplete netmsg); `fixture-index.txt`
  records the source demo and its sha256.

Commands (worktree-relative):

```text
bash build-cmake.sh
bash verify-fast.sh
powershell -ExecutionPolicy Bypass -NoProfile -File .\stability-run-matrix.ps1 `
  -RunId run-05 -ScratchRoot .scratch\t4-rerun-20261015-t4b -SoakSeconds 660 `
  -CaseSeconds 20 -Sets smoke,soak,corrupt,resource,audio,device
STABILITY_OUT="evidence/t4-rerun-20261015-t4b" \
  bash stability-release-check.sh --run-dir .scratch/t4-rerun-20261015-t4b/run-05
# ^ FAIL (2 premise assertions, see section 6 / Appendix B) -> instrument input
#   corrected (section 3), then the matrix was re-run:
powershell -ExecutionPolicy Bypass -NoProfile -File .\stability-run-matrix.ps1 `
  -RunId run-06 -ScratchRoot .scratch\t4-rerun-20261015-t4b -SoakSeconds 660 `
  -CaseSeconds 20 -Sets smoke,soak,corrupt,resource,audio,device
# ^ run-06 soaks played SUNSHINE.dem and exposed product finding F4
#   (Appendix C) -> input changed again, 160 s pretest first:
powershell -ExecutionPolicy Bypass -NoProfile -File .\stability-run-case.ps1 `
  -Case review12-pretest -RunDir .scratch\t4-rerun-20261015-t4b\review12-pretest `
  -Seconds 160 -Demo D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\review12.dem `
  -Hidden -AudioDisabled
powershell -ExecutionPolicy Bypass -NoProfile -File .\stability-run-matrix.ps1 `
  -RunId run-07 -ScratchRoot .scratch\t4-rerun-20261015-t4b -SoakSeconds 660 `
  -CaseSeconds 20 -Sets smoke,soak,corrupt,resource,audio,device
powershell -ExecutionPolicy Bypass -NoProfile -File .\stability-collect-evidence.ps1 `
  -ScratchRoot .scratch\t4-rerun-20261015-t4b -EvidenceRoot evidence\t4-rerun-20261015-t4b `
  -Runs run-05,run-06,run-07
STABILITY_OUT="evidence/t4-rerun-20261015-t4b" \
  bash stability-release-check.sh --run-dir .scratch/t4-rerun-20261015-t4b/run-07
STABILITY_OUT="evidence/t4-rerun-20261015-t4b-mutation" \
  STABILITY_MUT_SCRATCH=".scratch/t4-rerun-20261015-t4b-mutation" \
  bash stability-release-check.sh --run-dir .scratch/t4-rerun-20261015-t4b/run-07 --mutation
powershell -ExecutionPolicy Bypass -NoProfile -File .\stability-root-check.ps1 `
  -OutDir .scratch\t4-rerun-20261015-t4b\root-check -Seconds 45
```

Per the task owner's instruction the `verify-render.sh` / `verify-audio.sh`
re-runs were NOT duplicated inside T4: T5 runs the affected gates once on the
final integrated mainline, and `verify-all.sh --quick` is a release-time
activity. `verify-fast.sh` (product-free) was run and is recorded in section 5.

Machine-exclusivity guard for every product launch (see F1): the launcher waits
for a process-free window (no `tf2_demo_native`, no `nmake`), re-checks
immediately before starting, and refuses to start otherwise
(`.scratch/t4-rerun-20261015-t4b/matrix-start-wait.log`).

## 5. Build and gate exit codes

- `build-cmake.sh`: `cmake_build_rc=0`, `errors=0`, `warnings=4`, `exe_count=31`
  (clean configure + full build, 8m30s, 2026-10-15 21:47-21:56).
- `verify-fast.sh`: exit 0, `VERIFY-FAST=PASS (assertions_ok=43)`, build_rc=0.
- Matrix run-05: runner exit 0, 19/19 `case_done`, zero foreign residual
  processes in all 19 cases (the exclusive window held).
- `stability-release-check.sh` on run-05: exit 1, `STABILITY-RELEASE=FAIL`
  (217 OK / 2 FAIL; both FAILs are the soak duration premise, Appendix B).
- Matrix run-06 (SUNSHINE soak diagnostic): runner exit 0, 19/19 case
  directories, both soaks CRASH exit 3 at ~31-33 s (product finding F4,
  Appendix C), 17/17 non-soak cases normal. The forward gate was
  deliberately NOT run against run-06.
- Matrix run-07 (deliverable, review12.dem soaks): runner exit 0, 19/19
  case directories with readings, zero foreign residual processes in all 19
  cases, manifest exe sha256 equals the recorded build identity.
- `stability-release-check.sh` on run-07: **exit 1, `STABILITY-RELEASE=FAIL`
  — 214 OK / 5 FAIL** (`run-07-gate-forward-FAIL.log`, committed unedited).
  All 5 FAILs are `connected no root ... expected empty` on res-missing-vpk,
  res-missing-model, res-root-moved, res-unreadable-root and
  audio-missing-resource — one root cause, finding F6.
- `stability-release-check.sh --mutation` on run-07: **not executed by
  design** — `MUTATION-SETUP=FAIL (the forward run is already red; fix it
  before mutating)`, `STABILITY-RELEASE-MUTATION=FAIL`. The mutation stage
  requires a green forward run first; this is recorded as "not executed",
  NOT as a mutation escape.
- `stability-root-check.ps1`: **exit 0, `ROOT-CONTRACT=PASS`** — explicit
  valid root really opened the install (vpk_entries=104137); missing root
  did not connect Steam (connected='') nor the reference install; no-flag
  auto-search connected and opened the found install.

## 6. Named forward readings and negative/refusal readings

Build identity (the executable every reading below was produced by):

- exe: `native/build-nmake/tf2_demo_native.exe`
- sha256: `6f5d44845b6088807ff3f5fae8e3dbe2703112f1b5c720f43fe8ca9a9cdf3a8f`
- size: 1561088 bytes; built 2026-10-15 21:48:45 +0800 from `917b012`
- recorded in `.scratch/t4-rerun-20261015-t4b/exe-identity.txt` (with commit,
  branch, build summary) and pinned by the gate's manifest-vs-live-exe sha256
  comparison.

Run-05 forward readings (the run that exposed the premise problem; all 19
cases executed cleanly and exclusively):

- soak-hidden: 675.1 s observed, 65753 frames rendered, last_tick 64352,
  stall_samples 0, residuals none, peak WS 1843.3 MB / private 2179.8 MB.
- soak-window (audio device 6): 674.3 s observed, 54860 frames, last_tick
  64352, stall_samples 0, residuals none, peak WS 1805.2 MB / private 2183 MB.
- The demo reached its end tick (64352) at t=637.7 s (soak-hidden) and
  t=531 s (soak-window): at this build's playback rate the recording no longer
  outlives a >=660 s window. Against the 2026-10-14 build (run-03: last_tick
  57797 after 672.5 s) the effective playback rate rose ~11%, and rendered
  frames per tick changed (run-03 41118 frames / 57797 ticks = 0.71; run-05
  65753 / 64352 = 1.02). Behavior deltas that plausibly come from the product
  changes merged between the two builds (`ea4e661` -> `917b012`: 11 files,
  +4214/-96, incl. `world_material.cpp` +588, `viewmodel_material.cpp` +293,
  `native_renderer.cpp`, `bsp_map.cpp`, `main.cpp`): the run-05 titles report
  `atlas=4096` and `tris=199922` where run-03 reported `atlas=2048` and
  `tris=199263`. This task records the deltas and does not judge their
  correctness; render behavior is T2's gates' subject.

Run-06 forward readings: the soaks are the F4 product finding (Appendix C)
and are not release readings; the 17 non-soak cases behaved exactly like
their run-05 counterparts (naming refusals, resource degradations and the
audio-invalid-device exit-13 refusal all reproduced).

Negative / refusal readings (run-06): corrupt naming refusals and
res-missing-* degradations reproduced run-05 (per-case readings.json under
`evidence/t4-rerun-20261015-t4b/run-06/`).

Run-07 forward readings (the deliverable run; 19/19 cases, all exclusive):

- soak-hidden (review12.dem, hidden window): 675.4 s observed, COMPLETED,
  exit 0, last_tick 46082 (demo end 120936 — the end-tick premise holds
  with ~3.2x window headroom), 43203 frames rendered, stall_samples 0,
  peak WS 1807 MB / private 1985.2 MB.
- soak-window (review12.dem, audio device 6): 679.3 s observed, COMPLETED,
  exit 0, last_tick 52553, 46168 frames, stall_samples 0, peak WS
  1779 MB / private 1989 MB.
- smoke-hidden: 154.7 s, COMPLETED, exit 0, last_tick 8782.
- corrupt set: all 7 cases COMPLETED with the correct naming-refusal
  readings (e.g. corrupt-empty reports the real root and refuses the
  unparseable demo); resource set: all 7 executed with their designed
  environments — the 5 `connected no root` assertions are the F6 gate
  FAILs; audio-invalid-device: EARLY_EXIT exit 13 (expected refusal);
  audio-missing-resource: COMPLETED exit 0 (its `connected` reading is the
  5th F6 FAIL).

Negative / refusal readings (run-07): corrupt naming refusals and the
audio-invalid-device exit-13 refusal reproduced run-05 exactly; the
resource degradations produced their designed environments (per-case
readings.json under `evidence/t4-rerun-20261015-t4b/run-07/`).

## 7. Mutation results and source restoration proof

- Stability mutation (against run-07): **not executed — the gate refuses to
  mutate over a red forward run** (`MUTATION-SETUP=FAIL: the forward run is
  already red; fix it before mutating`). The 6 mutation cases were run to
  completion on run-05's green-aside evidence in the previous session and
  were 6/6 caught there; on run-07 the stage is a no-op by design until F6
  is fixed and the forward gate is green.
- The stability mutations operate on staged copies of the evidence; the gate
  re-fingerprints the original run directory afterwards and requires it
  byte-identical (`evidence_fingerprint` over the exact files the gate reads).
- No source file was mutated in this task; the only source-adjacent change is
  the matrix script's soak input (section 3), which was finalised BEFORE
  run-07 and is part of the delivery. Source restoration reduces to:
  `git status` clean
  apart from the new report/evidence files and the script edit, and
  `git diff --check` clean. Final audit output: `<<PENDING-AUDIT>>`.

## 8. Evidence paths and hashes

- Evidence root (committed): `evidence/t4-rerun-20261015-t4b/`
  - `run-05/`, `run-06/`, `run-07/` — all 19 case directories each with
    command/env/stdout/stderr/trace (or trace-extract + trace-sha256 over the
    commit limit)/metrics/sampler/exit-code/process-state/verdict/title-first/
    title-last/leftovers/readings.json, plus parser-probe.txt for the corrupt set.
  - `run-0X-run-manifest.txt`, `run-0X-device-info.txt`,
    `run-0X-blocked-inputs.txt`, `run-0X-install-blocked.txt`, `fixtures/`.
  - `run-05-gate-forward-FAIL.log` — the complete red forward gate on run-05
    (217 OK / 2 FAIL), preserved unedited.
  - `exe-identity.txt`, `corrupt-naming.txt`, `resource-roots.txt` (forward
    gate outputs on the deliverable run).
  - `mutations.txt` under `evidence/t4-rerun-20261015-t4b-mutation/`.
  - `sha256-index.txt` — sha256 of every raw file under the scratch root at
    collection time (covers run-04 diagnostics, run-05, run-06 and all logs).
- Raw scratch (not committed): `.scratch/t4-rerun-20261015-t4b/`
- Key digests (full coverage in `sha256-index.txt`, sha256
  `09d33431840add53a706...` over every raw scratch file at collection time):
  - `run-07-gate-forward-FAIL.log` (committed, unedited):
    `3810a25cbec701c2da50...` — the complete red forward gate, 214 OK / 5 FAIL
  - `evidence/t4-rerun-20261015-t4b-mutation/run-07-mutation-refused-tail.log`:
    `042c93122d7f195690d9...` — `MUTATION-SETUP=FAIL (the forward run is
    already red; fix it before mutating)`
  - `run-07-run-manifest.txt`: `1515e41743c984c1...` — pins exe sha256
    `6f5d4484...` and soak_demo sha256 `f1e91c6c...`
  - `run-07-device-info.txt`: `4a102397d17f8482...`

## 9. Unverified items and external blockers

- WARP software-adapter environment: NOT VERIFIED (run blocked-inputs.txt; the
  machine can create a WARP device — `install-smoke-probe.txt`
  `warp_fallback=1` — but no switch selects the driver type, and changing the
  product for a gate is out of scope).
- Device without the required feature level: NOT VERIFIED (no second adapter /
  driver downgrade exists; dxdiag.txt records this GPU's feature levels).
- Installer / release package: NOT VERIFIED (no installer exists in the tree;
  install-blocked.txt).
- Cross-machine behaviour: NOT VERIFIED (single machine).
- `verify-render.sh` / `verify-audio.sh` on this worktree: deliberately not
  re-run (task owner's instruction; T5 runs the affected gates once on the
  final integrated mainline). `verify-fast.sh` (product-free) was run: PASS.
- The 6 stability mutations on run-07: NOT VERIFIED (the gate refuses to
  mutate over a red forward run by design; they were 6/6 caught on the
  previous session's run-05 aside and re-run once the F6 fix lands).

## 10. Clean-worktree and runnable status

- `<<PENDING-CLEAN-STATUS>>`

## Findings

- F1 - `stability-run-matrix.ps1`'s header documents a mutual exclusion ("the
  run refuses to start while any tf2_demo_native process is alive") but no such
  check exists in the script (no `Get-Process` anywhere in it; the only process
  read in the harness family is the per-case residual scan in
  `stability-run-case.ps1:417`). This task enforced the documented contract
  externally: the launcher waited for a process-free window and re-checked
  immediately before every matrix start; the wait log records the full
  timeline. Recommendation: implement the guard inside the matrix runner (and
  name the offending process's worktree in the refusal), with its own busy-path
  test, as an integrator follow-up.
- F2 - The residual-process reading is the mechanism that makes cross-task
  machine contention visible, and it fired for real: run-04 (Appendix A) was
  poisoned by parallel T1/T3 product launches. Machine-level serialization of
  product runs is a scheduling requirement, not a nicety.
- F3 - The soak input's duration premise is machine-speed dependent. The gate
  needs `observed >= 660 s` AND `last_tick < demo end tick`; whether both can
  hold depends on the playback rate of the build under test. snakewater
  satisfied it on the 2026-10-14 build and cannot on the current one. The
  instrument now pins a demo with ~3.2x window headroom (review12.dem,
  120936 ticks) and the manifest records its hash, so the premise is explicit
  and auditable instead of incidental.
- F4 (product, renderer domain - T2's gates' subject, NOT fixed here): on the
  current-HEAD build both run-06 soak cases died ~31-33 s into
  `SUNSHINE.dem` (cp_sunshine): `exit_code=3`, harness verdict CRASH,
  `last_tick=0`, trace `mode: summary ... exit=3`. exit 3 is the product's
  device-error policy — `PostQuitMessage(3)` on a `renderer.lastError()` that
  is neither DEVICE_REMOVED nor RESET (main.cpp) — not an abort; the harness
  code table records it as CRASH either way. The same build plays snakewater
  for the full window and 17/17 non-soak cases pass, so the trigger is
  cp_sunshine content (128432 ticks, SourceTV). Reproduce: run the run-06
  soak-hidden command. Disposition: recorded, input switched (Appendix C);
  fixing belongs to the renderer owner.
- F5 (instrument-layer coupling, recorded not fixed): (a) the product titles
  SourceTV demos with a `SourceTV ticks=` prefix and POV demos with a
  `POV ticks=` prefix, while the gate's `load_title` parses only the former —
  the POV snakewater recording (a 7.6x-headroom candidate) would fail the
  soak end-tick assertion unconditionally. The judgment gate was NOT extended
  to accept `POV ticks=`; the input was switched to a SourceTV demo instead.
  (b) The player resolves a demo's map only under `tf\maps`
  (`assets.tfDirectory / "maps"`, then VPK fallback) and never looks in
  `tf\download\maps`, so SourceTV recordings of community maps whose BSP is
  only in the download tree (gullyscout.dem/cp_gullywash_f9,
  processdemo.dem/cp_process_f12) play with `BSP: invalid/BSP header is
  invalid` and a fallback material path. Whether download/maps should be a
  search root is a product decision for the owner; nothing in this task
  depends on it.
- F6 (product, root-contract domain — NOT fixed here; the 5 red gate
  assertions): the current-HEAD build reports `资源已连接` ("connected",
  `VPK entries: 0`) for an explicit `--tf-root` that exists but cannot
  serve a VPK, where the corrected root contract (06a90b3, restated in the
  gate's own comment) requires `no root found` for "a root without a VPK".
  Mechanism: `AssetRoot::valid()` (asset_root.cpp:109) accepts a root whose
  `materials`/`models`/`maps` directories merely EXIST
  (`hasAssetStore`, asset_root.cpp:46: `is_directory(...) -> true`), and
  the matrix's empty roots deliberately contain those three empty
  directories, so an empty root reads as valid. Evidence: run-07 gate FAILs
  on res-missing-vpk (empty-root-a), res-missing-model (empty-root-b),
  res-root-moved (moved-root-original), res-unreadable-root
  (unreadable-root), audio-missing-resource (empty-root-a) — titles read
  `资源已连接: <root> [VPK entries: 0, VTF: fallback, ...]`; the
  not-a-path control (res-missing-root, `__no_such_dir__`) correctly reads
  `资源未找到`.
  Coverage note that explains history: run-05 and the 2026-10-14 run-03
  passed these same assertions ONLY because their `--tf-root` arguments
  were RELATIVE paths that never resolved under the product's working
  directory — the product then took the `is_directory -> false` arm and
  reported `no root found` for the wrong reason (a false negative; the
  exists-but-empty arm was never exercised). run-07 is the first matrix
  whose scratch root was passed as an absolute path, which is why the
  defect is reachable now. Fix belongs to the root-contract owner
  (require a servable VPK in `hasAssetStore`/`valid()`); after the fix the
  deliverable matrix and both gate stages re-run once.

## Appendix A - run-04 diagnostic (superseded, retained in scratch)

- Run id `run-04`, started 2026-10-15 23:28:24 (+0800) after a 180 s quiet
  window, finished 00:22:07 (+0800), runner rc=0, 19/19 `case_done`.
- During the run, parallel sessions launched their own `tf2_demo_native`
  instances (T1 `viewmodel-weapon-acceptance`, T3 `audio-content-device`), and
  7 of 19 cases recorded a foreign live process at their leftovers check:
  smoke-hidden (pid=35356), soak-window (pid=58308), corrupt-empty (pid=40704),
  corrupt-bad-magic (pid=55460), res-missing-model (pid=3988),
  audio-invalid-device (pid=50032), audio-missing-resource (pid=48808).
- The gate was not run against run-04 (a red gate on contaminated evidence says
  nothing about the product); run-04 is retained under `.scratch` as diagnostic
  evidence of F2 and is pinned by `sha256-index.txt`.
- The uncontaminated run-04 readings behaved normally (soak-hidden: 676.2 s,
  44467 frames, 0 stalls, residuals none), consistent with the product being
  fine and the schedule being wrong.

## Appendix B - run-05 (complete, exclusive; 2 premise assertions red)

- Run id `run-05`, started 2026-10-16 10:36:24 (+0800), finished 11:29:50
  (+0800), runner rc=0, 19/19 `case_done`, zero foreign residual processes in
  all 19 cases, manifest exe sha256 equals the recorded build identity.
- Forward gate: `STABILITY-RELEASE=FAIL` — 217 OK, 2 FAIL
  (`run-05-gate-forward-FAIL.log`, committed unedited):
  - `soak-hidden last tick is short of the demo's end tick = 0, expected >= 1`
  - `soak-window last tick is short of the demo's end tick = 0, expected >= 1`
- Both FAILs are the instrument premise described in section 3 / F3: the demo
  finished inside the window, so `last_tick == demo total ticks`. No product
  instability was observed in run-05 (all other 217 assertions held, including
  both soaks' leak bounds, frame bounds, zero stalls and clean exits).
- Disposition: input corrected (section 3), matrix re-run as `run-06`. The
  judgment gate itself was not edited.

## Appendix C - run-06 (SUNSHINE soaks -> product finding F4; diagnostic)

- Run id `run-06`, started 2026-10-16 11:48:53 (+0800), runner rc=0, 19/19
  case directories, manifest exe sha256 equals the recorded build identity
  (`6f5d4484...`), soak demo `SUNSHINE.dem`
  (`7b8db6b4d5f375761723ededb89c232c842d1998f61d6134d9ad4ddc712e6a08`,
  82,759,239 bytes).
- BOTH soaks died ~31-33 s in: soak-hidden `observed_seconds=33.2`,
  `exit_code=3`, verdict CRASH, `last_tick=0`, `stall_samples=0`;
  soak-window identical. Trace tail: `mode: summary ... exit=3`.
- Everything else was normal: smoke-hidden COMPLETED; all 7 corrupt cases
  COMPLETED (their naming-refusal readings held); all 7 resource cases
  COMPLETED; audio-missing-resource COMPLETED; audio-invalid-device
  EARLY_EXIT exit 13 (the expected parameter-refusal code). Zero foreign
  residual processes in all 19 cases.
- Determinism: both soaks failing at the same wall-clock point (~31-33 s,
  before any title tick was published — `last_tick=0`) while the same binary
  plays the snakewater recording for the full window is a content-triggered
  product behaviour, not harness variance. exit 3 is the renderer's
  device-error policy path (`PostQuitMessage(3)`, main.cpp) on a lastError
  that is neither DEVICE_REMOVED nor RESET. Full analysis: F4.
- Disposition: the gate was NOT run against run-06 (a red gate over a product
  failure that belongs to another domain certifies nothing for release); the
  soak input was switched to `review12.dem` (section 3) and the deliverable
  matrix re-run as `run-07`. SUNSHINE.dem remains disqualified for this
  build; the finding is handed to the renderer owner.

## Appendix D - instrument-input search (160 s pretests, scratch)

Each candidate got a 160 s hidden-window pretest through the same
`stability-run-case.ps1` harness before the matrix was re-armed:

- POV snakewater recording (`autorecord_2026-09-27_21-44-41.dem`,
  cp_snakewater_final1, 490182 commands): plays cleanly — COMPLETED, BSP
  `state=source-equivalent` (200000 tris), last_tick 10696 at t=163.8 s —
  but its title reads `POV ticks=234026`; the gate's `load_title` parses
  only `SourceTV ticks=`, so the soak end-tick assertion would read
  `demo_ticks=0` and fail unconditionally (F5a). REJECTED without editing
  the gate.
- `gullyscout.dem` (SourceTV, cp_gullywash_f9, `SourceTV ticks=120936`):
  COMPLETED, last_tick 11466 at t=164.7 s, but `BSP: invalid/BSP header is
  invalid`, lightmap `state=unavailable`, `maptex: fallback` — its BSP is in
  `tf\download\maps\`, outside the player's map search root (F5b). A soak
  there would certify a render pipeline that never loaded the real BSP.
  REJECTED. (`processdemo.dem`/cp_process_f12 is the same mechanism by
  probe: SourceTV, 120908 commands, BSP likewise only in download/maps.)
- `review12.dem` (SourceTV, cp_reckoner): COMPLETED, last_tick 9530 at
  t=163.4 s (58.3 ticks/s -> ~38.5k ticks over the 660 s window, ~3.2x
  headroom under the 120936-tick demo), `BSP: 76505 tris`, lightmap
  `state=source-equivalent`, title `SourceTV ticks=120936` — every premise
  the gate and the soak semantics need. SELECTED (section 3).
