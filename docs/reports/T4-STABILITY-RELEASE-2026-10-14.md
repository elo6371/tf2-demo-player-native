# T4 Stability & Release Acceptance — Final Report (2026-10-14)

## Scope and anchors

- Branch: `task/stability-release`
- Worktree: `D:\TF2_Native_Worktrees\stability-release`
- Baseline commit: `4325dde` (docs: make integrated branch and handoff status visible)
- Final commit: this commit (see `git log -1`)
- Other worktrees (`D:\TF2_Native_Test`, `world-material-effects`, `manual-followup`) untouched; `verify-all.sh` / T2 gates not run.

## The explicit `--tf-root` fix (native/src/main.cpp)

Defect: with `--tf-root <path>` given, the Steam-library candidates were still
appended to the candidate list, so a missing/moved explicit root silently
fell back to the auto-detected Steam install (title read as if resources were
fine).

Fix: when an explicit root is given, it is the ONLY candidate
(`candidates = {commandTfRoot}`); Steam env/registry/library candidates are
appended only in the no-explicit-root branch.

Verified by `stability-root-check.ps1` (ROOT-CONTRACT=PASS, 12/12):

- `--tf-root <valid>` uses only that path (VPK entries = 104137).
- `--tf-root <missing>` reports "resources not found"; never connects Steam.
- no flag: auto-discovery of Steam/persistent root still works.

The gate's section 7/8 assert the same contract against run-03 evidence:
1 root honoured (res-missing-bsp, the real install), 6 roots refused without
substitution, and the "did not silently connect the auto-detected root"
assertions go red if the fallback defect returns.

## Build identity

- exe: `native/build-nmake/tf2_demo_native.exe`
- sha256: `fe9c9e55b36236b318a1360e399ffb98f93ebb3e66d24ae09f0bd0fe7035e1f6`
- size: 1446400 bytes
- built: 2026-10-14 17:14:56 +0800 (includes the root fix)
- recorded in `.scratch/t4-rerun-20261014-193500-t4a/exe-identity.txt` and in
  the run-03 manifest; identical hash re-verified at audit time.

## Run and evidence

- Run id: `run-03` (new; run-01 not reused)
- Scratch: `.scratch/t4-rerun-20261014-193500-t4a/` (not committed)
- Evidence: `evidence/stability-release/` (286 files; sha256-index.txt pins every file)

## Matrix results (19/19 cases, runner_rc=0)

- smoke-hidden: NORMAL_EXIT exit 0.
- soak-hidden (660 s): COMPLETED; frames advanced 40877 (sample series) /
  41118 last reading; 0 stall, 0 hung; working-set growth second half -0.2 MB
  (<= 32); private bytes +0.0; threads +0.0; peak WS 1632.2 MB (<= 2048);
  peak private 2099.8 MB (<= 3072); no residual process, no leftovers.
- soak-window (real audio, 673.9 s of a requested 660): COMPLETED; frames
  49527 / 49764; same bounds green.
- corrupt inputs (empty, bad magic, bad version, truncated header, truncated
  packet, mid-truncated, incomplete netmsg): all COMPLETED exit 0, no crash,
  no throw; every refusal is named — the product itself named 7 of 7 via the
  title ("demo file cannot be opened"), the repo parser probe named 0
  (kept as a second channel; mutation 1 proves the gate goes red when neither
  channel names a reason).
- resource environments (missing root, missing VPK, missing model, missing
  BSP, moved root, stale root, unreadable root): all COMPLETED exit 0; only
  res-missing-bsp connects a root (the real one it was given); the other 6
  report "no root found" with no silent substitution.
- audio-invalid-device: EARLY_EXIT exit 13 by design (device rejected before
  any window exists); audio-missing-resource: COMPLETED, no root connected.
- device-info / dxdiag / install-smoke-probe recorded per run.

## Forward gate

`STABILITY-RELEASE=PASS` — 219 OK assertions, 0 FAIL
(`.scratch/t4-rerun-20261014-193500-t4a/gate-forward4.log`).

## Mutation gate

`STABILITY-RELEASE-MUTATION=PASS` — 6 of 6 mutations went red with their
control assertion preserved:

1. named_refusal_removed — corrupt-truncated-packet lost both naming channels
   (probe file removed + title-derived name blanked); the "refuses without
   naming a reason" assertion went red. (Note: an earlier version of this
   mutation removed only the probe file and was inert, because in run-03 the
   product names all 7 refusals itself; the mutation was corrected to remove
   the actual naming source.)
2. crash_reported_as_completed — corrupt-empty crash marker went red.
3. soak_frames_frozen — soak-hidden rendered-frames bound went red.
4. residual_process_hidden — soak-window residual-processes went red.
5. steady_state_leak — soak-window working-set second-half growth went red.
6. blocked_inputs_removed — blocked-inputs.txt recording went red.

Original evidence byte-identical after all 6 mutations
(fingerprint `1a8dfab879a1091af0f816b74df3ce4001d92051d0d73e501de59ddca8d28da5`);
mutation evidence uses staged copies only.

## Blocked / NOT VERIFIED environments (recorded, not skipped)

- WARP software-adapter environment: 6 blocked entries.
- feature-level restriction environment: 3 blocked entries.
- installer-absent environment: 1 blocked entry.
Each entry says what was NOT verified (`run-03-blocked-inputs.txt`,
`run-03-install-blocked.txt`).

## run-01 invalid reason

run-01's binary identity does not match the recorded build identity (it ran
before the explicit-root fix was built in; its manifest has no exe_sha256 to
reconcile). Marked `evidence/stability-release/run-01-STALE.txt`; not used as
evidence anywhere in this report.

## Not verified

- WARP / feature-level / installer-absent environments above.
- Anything outside the matrix sets: smoke, soak, corrupt, resource, audio, device.

## Audit tail

- `git diff --check`: clean.
- `Get-Process tf2_demo_native`: none.
- `.scratch/` ignored, not committed.
- exe hash identical across exe-identity.txt, run-03 manifest, and audit-time
  re-hash.
- No T2 or mainline files modified; the only modified tracked file is
  `native/src/main.cpp` (the root fix).
