# gate-verdicts.md — five gates, real runs, 2026-11-14

Author: gate verifier (delegated). Worktree: `D:/TF2_Native_Worktrees/t105-e5`, branch candidate/e5.
Window: 2026-11-14 22:52:46 → 23:07 local (+0800). Every gate ran under
`bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run`. No tracked file was written by me (§2).

**Reading labels.** `[M]` = measured in this session, at run time, from a log or a product artifact.
`[S]` = static count or line reference read out of the source (a call-site count, a literal, a pinned
value) — not a measurement. Where a `[S]` number is also pinned by an assertion, the assertion is named.

Output root (mine, outside the repo): `D:/_shared/EXEC-2026-11-15/gateverdict/`

---

## 0. Conclusion table

| # | gate (repo root) | true rc | rc source | asserts, in one line | negative control | classification |
|---|---|---|---|---|---|---|
| 1 | class-pose-family-check.sh | **0** | last log line `POSE-FAMILIES=PASS` [M] | every sampled crouch/flinch sequence decodes (all `finite=1`), sampled label == decoded label, ≥1 crouch clip moves | N1: probe wrapper flips `finite=1`→`0` → **rc=1**, 50 FAIL lines, `decoded=0 failed=49`, first FAIL `a sampled frame was not finite` [M] | **GREEN, real run** |
| 2 | player-animation-check.sh | **1** | log `PLAYER-ANIMATION-CHECK=FAIL` + hostlock `GATE-RC=1` [M] | demo/synthetic tick streams advance when moving, not when idle (D55 reader) + binary freshness | N2: one `state=moving` tick line with no `advanced=` field → reader prints `bad_parse=1` and the named FAIL fires [M] | **RED — 2 causes, neither is D55** (§1.2) |
| 3 | playback-error-paths-check.sh | **1** | log `PLAYBACK-ERROR-PATHS=FAIL`, gate.rc=1 [M] | A1 missing demo refuses before a window; A2 no demo still starts; B camera mode applies on a paused frame; C forward seek; D start-paused resume; E unchanged run | own `--selftest`: **PASS**, 36 mutants killed / 0 survived, 35 clean fixtures 0 red, 12/12 mutation verdicts [M] | **RED — false red from a missing pinned recording** (A1/A2/B genuinely green) |
| 4 | animation-drive-states-check.sh | **2** | FATAL line, gate.rc=2 [M] | legacy vs drive frames differ at the crouch tick; `state=flinch` named at the flinch tick; `animdsum` counter reaches every state (peak over frames) | none possible — **never reached a reading**. D59 refusal verified: rc=2 ×3 [M] | **CANNOT RUN on this host** (`[ -d ]` false on the TF root) |
| 5 | viewmodel-animation-check.sh | **1** | log `VIEWMODEL-ANIMATION=FAIL`, gate.rc=1 [M] | 7 sections; §5/§6 assert `max_frames` exactly 0/31 and 0/61 | none possible — dies at §0/7. D59 refusal verified: rc=2 ×3 [M] | **CANNOT RUN — same missing recording** |

All five reproduce their outcome within this window (4: 1 s, 5: 0 s). 1 is 31 s, 2 is 44 s, 3 is 75 s.

**One sentence for the lead:** a single deleted recording (`tf/demos/autorecord_2026-07-02_13-26-46.dem`)
is enough to make gates 3 and 5 red, and a `[ -d ]`-vs-`[ -e ]` quirk on the TF root makes gate 4 exit 2
before it starts. None of those three outcomes says anything about the code under test.

---

## 1. Per-gate evidence

### 1.1 class-pose-family-check.sh — GREEN [M]

- Command: `T62_POSEFAMILY_OUT=D:/_shared/EXEC-2026-11-15/gateverdict/g1-class-pose/out bash run-gate.sh g1-class-pose class-pose-family-check.sh`
  which wraps `hostlock.sh run gateverdict-g1-class-pose bash exec-gate.sh …`.
- Gate sha256 `f4c5aa878f3912d857a1a2ed9610de303a6c73ad218db4b1614efc0490f354a9` (clean vs HEAD).
- Probe `native/build-nmake/model_pose_probe.exe` sha256 `4d2f4bbc77ac4bdd3b0f198d22f5384f2025f19e5f451045c9774eda305edfdf`,
  952320 B [M]. No product exe, no GPU, no host lock required (header :31); the lock was taken anyway.
- Window 22:52:46 → 22:53:18, **31 s** [M]. hostlock FREE before, FREE after, `product_processes=0` [M].
- Readings [M]: `POSE-FAMILIES classes=9 crouchClasses=9 flinchSequences=14 sampled=40 decoded=40 failed=0`
  then `POSE-FAMILIES=PASS`. 40 samples = the whole 9-class × up-to-3 × 2-family probe; `decoded=40`
  means F2's "sampled label equals decoded label" held on all 40.
- What it asserts, where [S]: F1 crouch sequence exists excluding reload/layer_ (:94-98); F2 every animpose
  frame `finite=1`, no `status=sample-failed`, labels match (:117-128, extraction :147-153); F3 ≥1 crouch
  clip moves — two frames with different bone checksums (:159-161); F4 flinch family exists across the nine
  tables (:164-166). The D53 fix is `tr -d CR` at :137 ahead of the unchanged awk.
- How it misbehaves when broken: pre-D53 the awk saw a trailing quoted field plus CR, printed `finite=0` on
  healthy frames, and F2 turned every class red — exactly the N1 shape.
- Negative control N1 (mine): `gateverdict/g1-class-pose/neg/n1-probe-wrapper.sh`
  wraps the real probe, rewrites `finite=1`→`finite=0` on the probe's own output, exits with the probe's rc;
  `neg/gate-n1.sh` is a copy whose **only** difference from the shipped gate is line 34 `PROBE=` (diff: 1 line).
  Result **rc=1, 33 s** [M]: `sampled=40 decoded=0 failed=49`, 50 FAIL lines, first
  `FAIL scout crouch seq 10 (AttackCrouch_PRIMARY): a sampled frame was not finite` [M]. Validity: the input is
  the real probe's output with one semantic flip — it reproduces the exact pre-D53 failure mode the gate's own
  F2 sentence names, rather than corrupting a byte.
- Evidence: `gateverdict/g1-class-pose/` — gate.log (52 lines, sha256 `e2fae6c071440879…`), meta.txt,
  hostlock-*.log, bins-{before,after}.sha256 (**identical** [M]), out/ (50 files), artifacts.sha256 (64 lines),
  neg/.

### 1.2 player-animation-check.sh — RED, and the red is not D55 [M]

- Command: `PLAYER_ANIMATION_OUT=…/g2-player-animation/out bash run-gate.sh g2-player-animation player-animation-check.sh`
- Gate sha256 `6b4faf4cff75b15b63e307a6b88be0085f0b4b93ac990cba4fc00d8db5e7dd8d` (clean vs HEAD).
- Window 22:53:29 → 22:54:14, **44 s** [M]; hostlock FREE before/after; binaries unchanged [M].
- **Correction to a prior:** the lead has "g2 rc=0 44s". That is my wrapper's bug — `exec-gate.sh` ended with a
  bare `echo GATE-RC=$?`, so the wrapper exited 0 while the gate log printed `GATE-RC=1`, and hostlock saw 0.
  The gate's true rc is **1**; last log line `PLAYER-ANIMATION-CHECK=FAIL` [M]. I fixed the wrapper (captures rc,
  writes gate.rc, propagates) for runs 3-5. Please record g2 as red.
- Two FAIL lines [M]:
  1. `FAIL STALE-BINARY=1 probe=native/build-nmake/model_pose_probe.exe older than D:/TF2_Native_Worktrees/t105-e5/native/tools/model_pose_probe.cpp`
     (probe Nov 13 23:17 vs source Nov 14 20:46 [M]). Section :118-137 — a build-state check, not a behavioural
     reading; it compares the probe against `CMakeFiles/model_pose_probe.dir/DependInfo.cmake`'s file list plus
     `*.h/*.hpp` newer than the probe [S], so it is deliberately not a full-source freshness proof.
  2. `FAIL E synthetic movingAdvanced (baseline excluded) = 30, expected >= $(( 31 - 1 ))`
- Cause of FAIL 2, established by three reproductions: :329 assigns `moving_min=$(( $(field "$ticks" movingTicks) - 1 ))`;
  at run time `moving_min` held the 13-byte **string** `$(( 31 - 1 ))` instead of `30`. The line is clean LF, has no
  escaped `\$(…)`, and bins-before == bins-after. The same assignment shape on this host
  (GNU bash 5.3.20(1)-release, x86_64-pc-msys) gives `30` in 7 of 9 variants and the raw text in 1 and `-1` in
  another (`gateverdict/min-repro.sh`, printed in full) — a shell expansion fault; `assert_ge`'s
  `[ $2 -ge $3 ] 2>/dev/null` swallows it instead of reporting a non-integer. :329-330 predates D55, so this red is
  **host-borne**.
- The D55 readings themselves are green on real output [M]: `lines=8 bad_move=0 bad_idle=0 bad_parse=0`, then
  `OK E demo printed tick lines = 8 (>= 1)`, `OK advanced= field parsed = 0`, `OK moving ticks that did not advance = 0`,
  `OK idle ticks that advanced = 0`. So D55's reader **was exercised on real probe output** — what gatefix-report §10
  said was unproven. Other green readings [M]: demo `transitions=39 entities=13 movingTicks=39 movingAdvanced=26`,
  `staleTicks=1997 idleAdvanced=0 maxIdleDelta=0.000000000 maxMovingDelta=11.905984879`; synthetic
  `ticks=61 movingTicks=31 movingAdvanced=30 idleTicks=30 maxMovingDelta=0.020000001`. The three built-in probe
  controls fired with their named reasons [M]: wrong-tf-root rc=2/`tf-root-invalid`, wrong-model rc=2/`model-missing`,
  nonexistent-sequence rc=2/`sequence-out-of-range`.
- Negative control N2 (mine): `neg/gate-n2.sh` differs in exactly two lines — line 44 `cd` retargeted to the repo
  root (a copy outside the repo must run from it) and one injected line after `DEMO_RC=$?`:
  `printf "animtick mode=demo tick=1 entity=1 speed=0 state=moving\n" >> "$DEMO_LOG"`.
  That line is `state=moving` with **no** `advanced=` field — the exact shape the OLD reader counted clean.
  Result [M]: **rc=1**, reader prints `lines=9 bad_move=0 bad_idle=0 bad_parse=1`, and
  `FAIL E demo printed tick lines whose advanced= field parsed = 1, expected 0` while the other three assertions stay OK.
  It reddens the *new* assertion by name and would have been silent under the old reader — that is what makes it
  a discriminative control for D55 rather than merely a red gate.
- Evidence: `gateverdict/g2-player-animation/` — gate.log (269 lines, sha256 `2dd21f59373b5961…`), meta.txt,
  hostlock-*.log, bins-*.sha256, out/ (14 files), artifacts.sha256, neg/gate-n2.log; reproductions
  `gateverdict/min-repro.sh`, `repro-moving-min.sh`, `probe-gate-line.sh`.

### 1.3 playback-error-paths-check.sh — RED, false red from a missing recording; D56 proved separately [M]

The lead's three requested forensics:

1. **Product command line as executed** (captured with `bash -x` on `--only C`):
   `…/native/build-nmake/tf2_demo_native.exe --tf-root 'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf' --demo 'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem' --hidden-window --audio-disabled --camera-script …/trace-x/probe-c.script`
   (plus --settings-file/--ui-settings-file inside the gate's own mktemp -d iso dir, and `TF2_NATIVE_TRACE=$O.trace`).
   Capture: `gateverdict/g3-playback/argv-capture.log`.
2. **Why rc=2** — not the argument parser, not my OUT isolation:
   - product stderr (140 B) [M]: `demo open failed: D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem: demo file cannot be opened`
   - product trace (281 B) [M]: `7.5 window: created outer=1280x720 …` / `24.5 args: unrecognized=0 first=- all=-` /
     `29.4 demo: open failed path=… reason=demo file cannot be opened` — `unrecognized=0` rules out a --strict-args
     rejection, and the window already exists when the demo open fails.
   - **The file does not exist** [M]: `ls` on the full path → No such file or directory; `tf/demos/` holds 2044
     entries, all 2026-08-02/08-… series, none of 2026-07-02; a search of `D:/TF2_Demo_Player` and `D:/_shared` finds
     only `*.dem.vm.err`/`.vm.out` sidecars of that name (`D:/_shared/EXEC-2026-11-12/R4/corpus-hunt/raw/`), never the .dem.
   - Isolation exonerated by the gate's own controls in the same run: A2 (no demo) rc=0 with a 119302 B trace, B
     (CAM_DEMO, which exists) rc=0 with a 1311933 B trace, both through the same run()/iso/settings mechanism [M].
     Only the cases fed `AUDIO_DEMO` died.
   - I did **not** re-run on the gate's native `evidence/t93-playback` path: the cause is a fixed input, not a path, so
     the rerun buys nothing and risks the tracked-dir pollution the task forbids (lockrun exit 97).
3. **Verdict on the red:** false red from a missing input file hardcoded at :55 with **no environment override**
   (only `TF2_EXE`/`TF2_ROOT` exist [S]) — not D56, not the OUT isolation. And D56's field is green: case B read
   `camera: summary … modeApplies=1` from a real trace on the current binary, `B-summary` passed [M].

- Readings [M]: `GATE tag=green cases=6 pass=21 red=14 seconds=74`, `GATE-RESULT=FAIL`, `PLAYBACK-ERROR-PATHS=FAIL`.
  A1 6/6 green, A2 3/3 green, B 10/10 green (B-exit, B-step, B-applied, B-paused, B-mark, B-summary, B-pixels,
  B-script, B-clock, B-frozen). C/D/E produced 14 reds. Case seconds [M]: A1 0, A2 12, B 61, C 0, D 0, E 0.
  Two of the 21 passes are vacuous on an empty trace (C-burst, E-noseek) — see the misbehaviour note.
- Window 22:59:37 → 23:00:53, **75 s** [M]; gate sha256 `095bc2447bc4f9322c7b4f0fbbb22fe839044333f4337944dce2877b8adc4c1a`; binaries unchanged [M].
- Assertion points [S]: judge_A1 :122-130 (rc==2; stderr and trace both carry `demo open failed`; no
  `mode: window_created=1`; no `map: lookup`; ≤15 s). judge_A2 :132-137. judge_B :139-187 (rc==0;
  `cmd=freefly result=ok detail=on mode=free-look`; `camera: mode applied=free-look`; paused=1 there; mark tick ==
  applied tick and ≠ -1; `modeApplies` ≥1 via fnum on `camera: summary`; before/after BMP sha differ; accept
  failures=0; applied tick == both snapshot ticks; `camera: expect what=same-tick` match=1). judge_C :189-208 (seek reset
  names tick 3000; an `audio: advance` follows it; max events after the reset ≤60; `audio: advance tick=3000`).
  judge_D :210-231. judge_E :232-252. run() :336-363 refuses an argv without --hidden-window/--audio-disabled
  (return 99) — why every case carries both flags.
- Misbehaviour if broken: C-burst would pass by vacuity on an empty window; judge_C guards exactly that at :203
  ("the window is empty, so C-burst would pass by vacuity"). The guard worked — C-window went red and C-burst still
  printed PASS, so a report that read only the aggregate count would have been wrong in the other direction.
- Negative control (the gate's own, and a real one): `--selftest` → **rc=0, 9 s** [M],
  `SELFTEST-CLEAN-OK 35 pass / 0 red`, `mutants 36 killed / 0 survived; clean fixtures 35 pass / 0 red`,
  `SELFTEST-MUTATION-VERDICT: 12 pass / 0 fail (M4 must be NOT-CAPABLE, never KILLED and never SURVIVED)`,
  `PLAYBACK-GATE-SELFTEST=PASS` [M]. It feeds the **shipping** judgement functions synthetic traces with deliberately
  wrong values (:370+) and requires each to go red plus one clean fixture per case — a named negative control for
  every case, including the three that could not run.
- Evidence: `gateverdict/g3-playback/` — gate.log (41 lines, sha256 `1a88122c072cbd49…`), gate.rc=1, meta.txt,
  hostlock-stdout.log (`ACQUIRED` … `LOCKED-COMMAND-EXIT rc=1`), hostlock-before.txt, out/ (31 files incl.
  results.tsv, six .trace/.err/.rc triples, out/selftest.log), argv-capture.log.

### 1.4 animation-drive-states-check.sh — exits 2 before any reading [M]

- Command: `T84_STATES_OUT=…/g4-drive-states/out bash run-gate.sh g4-drive-states animation-drive-states-check.sh`
- Gate sha256 `ede0d8fa3e7f64cc3f6db828cd5e21f50aff362c1776b8ebf3215091b7e18607` (clean vs HEAD, zero CRs [M]).
- Result: **rc=2 in 1 s** [M]; the whole log is one line [M]:
  `FATAL: missing TF root: D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`
- Why: the gate tests `[ -d "$TF" ]` at :137. On this host that one directory **fails `-d` while passing `-e`** [M]:
  the parent `…/Team Fortress 2` and the child `…/tf/demos` both test TRUE for `-d`; only `…/tf` itself returns
  false — and it is a plain directory (`stat -c %F` → directory, `cd` into it + `pwd -P` works, `ls -d` works). A
  filesystem/ACL oddity on that path, surfaced by the gate's choice of `-d` where its siblings use `-e`
  (player-animation hands the same path to the probe and only `[ -e ]`s its inputs).
- The OUT was never created — `g4-drive-states/out` does not exist [M] — because `mkdir -p "$OUT"` is at :139, two
  lines after the FATAL. Isolation cannot be implicated: nothing of mine was touched.
- Consequence: **no reading of this gate exists.** Nothing about A (legacy untouched), B (state reaches the picture
  at CROUCH_TICK 14765 / FLINCH_TICK 16780) or C (`animdsum` peak covers every state) is verified, and its four
  built-in negative controls never ran.
- What was verified instead [M]: the D59 refusal. `--playback-speed`, `--playback-speed=1` and `--playback-speed=2`
  each → **rc=2** in <1 s with `FATAL: --playback-speed is refused by this gate (D59): the flinchStarts peak it asserts
  is an EDGE reading that depends on sampling density…` and no product line in the log. Refusal happens before the
  product launches, which is the property D59 needed.
- Evidence: `gateverdict/g4-drive-states/` — gate.log (1 line, sha256 `d3287300cdd2cf63…`), gate.rc=2, meta.txt,
  hostlock-*.log, x-trace.log (`bash -x` capture of the check), refusal-*.log ×3.

### 1.5 viewmodel-animation-check.sh — exits at §0/7, same missing recording [M]

- Command: `VIEWMODEL_ANIMATION_TRACE_DIR=…/g5-viewmodel/out bash run-gate.sh g5-viewmodel viewmodel-animation-check.sh`
- Gate sha256 `15fef7fd25737c0fdf0a0eff1f4e1c5aa8b0013342ab992f3632d7a273efe4e2` (clean vs HEAD, zero CRs [M]).
- Result: **rc=1 in 0 s** [M], 7-line log [M]:
  `FAIL pinned recording is absent: D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem` /
  `OK bb841c6d…matcha-20260927-1347-cp_snakewater_final1.dem pinned (51834617 bytes, 308cc1453672b26d…)` /
  `FAIL pinned recordings verified = 1, expected 2` / `FAIL the pov recording was not identified by the pin file` /
  `VIEWMODEL-ANIMATION=FAIL (a pinned recording could not be resolved)` [M].
- The same file as §1.3, and it is pinned in a tracked input list: `evidence/viewmodel/animation-inputs.txt:29`
  records it as 21856667 B, sha256 `c8a58501d222bdd9aec6db92d2782931a29a13d963a87364d603263e6dab091e`, root `steam`
  [S from the file; the absence is measured [M]]. §0/7 resolves pinned paths against a root table and fails closed
  (:425-472; `assert_eq pinned recordings verified 2` at :455).
- Consequence: **no reading of this gate exists.** §5/7's `max_frames` = 0/31, §6/7's 0/61, the census/sequence/
  entity-supply/model-class checks, and the five runtime mutations (§7) all never ran. None of the D59 max_frames
  exactness work is observed — only its refusal branch.
- Verified instead [M]: `--playback-speed`, `=1`, `=2` → **rc=2** each with
  `FATAL: --playback-speed is refused by this gate (D59): max_frames is asserted as an exact clip length (0/31/61)…`,
  and it refuses *before* taking its self-lock — `.scratch/viewmodel-animation.lock` is absent and the trace dir stayed
  empty, so a refused run cannot leave the gate locked.
- Evidence: `gateverdict/g5-viewmodel/` — gate.log (7 lines, sha256 `591263b69a3e823f…`), gate.rc=1, meta.txt,
  hostlock-*.log, refusal-*.log ×3.

---

## 2. Evidence hygiene

- Every gate wrote only to my OUT via its documented env override — all five have one [S]:
  `T62_POSEFAMILY_OUT` (class-pose :33), `PLAYER_ANIMATION_OUT` (player-animation :50), `PLAYBACK_OUT` (playback :57,
  also `--out`), `T84_STATES_OUT` (drive-states :78), `VIEWMODEL_ANIMATION_TRACE_DIR` (viewmodel :152). None names evidence/.
- `tools/audit-tree.sh` was run after **every** gate and after the negative controls. Every invocation ended
  `VERDICT: OK (no forbidden writes, line endings intact)` [M]; its own timestamped copies are in
  `D:/_shared/EXEC-2026-11-15/LEAD/audit-2026*.txt`.
- Nothing was restored because nothing needed restoring: `git status --porcelain` never gained an evidence/ or
  native/tests/fixtures entry under me [M]. The dirty set I saw is other agents' work (docs, .gitattributes,
  native/src/main.cpp, native/src/demo_header_camera.cpp, native/tools/demo_open_probe.cpp, one untracked handoff
  doc) and HEAD moved under me (0974860 → a5c70eb) — this worktree is shared.
- bins-before.sha256 == bins-after.sha256 for g1, g2, g3 [M] — no gate rebuilt the product (g4/g5 never launched it).
- Per-gate manifests: `artifacts.sha256` (sha256 + byte size + mtime + path + the five product binaries) for g1,
  g2 and g3, produced by `gateverdict/manifest.sh <id>`; g4/g5 have no OUT to hash (their `out/` was never created).
- Runner `gateverdict/run-gate.sh <id> <gate> [args]` records meta.txt (gate, start/end, cwd, hostlock command, the
  five OUT vars), bins-{before,after}.sha256, gitstatus-{before,after}.txt, hostlock-{before,after}.txt and gate.rc.

## 3. NOT VERIFIED

1. **Gates 4 and 5 have zero readings** — neither a pass nor a fail of anything they assert about the code.
2. **D59's actual readings** — drive-states' `flinchStarts` peak (:318-320 read, :327-333 assert ≥1) and viewmodel's
   `max_frames` exactness (0/31/61) — never observed. Only both refusal branches (rc=2 ×3 each).
3. **Playback C/D/E** — forward seek, start-paused resume, unchanged baseline — including `E-modeapplies` (the D56
   field asserted 0 on a run that never changes mode), `E-timeline scheduled=5323`, `E-events_seen=759`, `D-first ≥700`.
   Only their selftest fixtures ran.
4. **player-animation's synthetic streaming path** — the four D55 assertions were proven on the 8-line demo sample;
   the synthetic half is red here for a shell reason, so "D55 works on the synthetic stream too" is not claimed.
5. **No hardware/GPU claim beyond B** — case B did run the product with a window for 61 s and produced two BMPs that
   differ; that is a real render, but one case on one map, not a frame-quality claim.
6. **Any gate's behaviour with the recording restored.** No copy exists on this host and I did not fabricate one.
7. `--mutation` (viewmodel's source-level half) and `--mutation-judge M4` (playback) were not run: the former needs a
   clean native/ tree (this shared worktree has a dirty demo_open_probe.cpp) and the latter a product-side mutant build.
8. No verify-all.sh / verify-release.sh / CI was run, by instruction. No commit, no source or gate edit.

## 4. Verdict for the lead

**May claim green:** #1 class-pose-family-check.sh only — rc=0, 31 s, 40/40 sequences decoded, D53's `tr -d CR`
path exercised on the real model files, and its negative control (N1) reddens it with the gate's own sentence.
A real, end-to-end, audited pass on this host.

**May NOT claim green, and must not be recorded as a code failure:**
- #3 playback-error-paths — A1/A2/B green (21 passes); C/D/E blocked by a **missing input file** hardcoded at :55
  with no override. Its D56 reading (B-summary, `modeApplies=1` on a real trace) is **green**, the strongest positive
  D56 evidence yet.
- #4 animation-drive-states — exits 2 at :137 because `[ -d ]` is false on the TF root here while `[ -e ]` is true.
  Not isolation (OUT never created), not the code.
- #5 viewmodel-animation — fails closed at §0/7 on the same missing recording, pinned at
  `evidence/viewmodel/animation-inputs.txt:29`.

**Red for reasons unrelated to D55:** #2 player-animation — (a) `STALE-BINARY=1` (probe ~3 h older than its source;
a rebuild clears it) and (b) a host bash arithmetic fault on pre-existing :329-330 that makes `assert_ge` report a
non-integer as a failure. Its D55 assertions printed green on real probe output.

**Do not report g2 as rc=0.** The prior "g2 rc=0 44s" is my wrapper bug; the gate's rc is 1.

**Highest-value next actions, in order:**
1. Restore `autorecord_2026-07-02_13-26-46.dem` at `…/tf/demos/` (21856667 B, sha256 `c8a58501…6dab091e`, per the
   tracked pin file). That one file unblocks playback C/D/E and viewmodel's §0/7 plus its pov half of §5/§6.
2. Change `animation-drive-states-check.sh:137` to `[ -e ]` (or repair the ACL on `…/tf`) — its siblings already use `-e`.
   Until then this gate cannot run on this host at all.
3. Rebuild `model_pose_probe.exe` to clear `STALE-BINARY`, then re-run player-animation; if the `movingAdvanced` line is
   still red it is the shell fault — quote the arithmetic or precompute into an integer variable with a numeric guard.
   A gate-side portability fix, not a product fix.
4. Only then re-run the chain and treat gates 3/4/5 as decidable.

--- End of report. Run artifacts: `D:/_shared/EXEC-2026-11-15/gateverdict/`; audit trail:
`D:/_shared/EXEC-2026-11-15/LEAD/audit-2026*.txt`.
