# RUN19 — five chain steps measured outside the chain (2026-11-09 22:25–23:44)

VERDICT-RUN19 = PARTIAL: three steps carry a forward reading, one is red on a
product reading, one PASSed without keeping its evidence (gate bug, fixed but
unproven). Nothing in this directory is a full-chain release acceptance.

## 1. What this window was

verify-release.sh has fifteen steps. Four of them had never produced a reading on
this tree (outline, body-appearance, timeline, build-identity) and one of them
(hud-live) only had a reading from an earlier commit. RUN19 ran those steps one at
a time inside ONE held lock, so the machine had one product at a time and the
readings came from one build:

| field | value |
| --- | --- |
| script | D:/_shared/EXEC-2026-11-09/run19-keypath3.sh (49 lines) |
| lock | /d/TF2_Native_Worktrees/_shared/hostlock.sh, owner=t91-lead, ACQUIRED 2026-11-09 22:25:39 |
| stamp | 20261109T142540Z |
| HEAD | f2c4fbd89193969729a7155aae2c798af37b0902 |
| tree at start | dirty=0 |
| native/ tree sha256 | 7378ca192be9684e5aa6ee851910f06c7db0e6a2074f5f4545cdf157ecef8192 |
| product (built in this window) | native/build-nmake/tf2_demo_native.exe, 2,838,528 B, sha256 37388f4ad5bd82c699529c828b09c78b12d628b77ddbbb17d267b24d6d410aff |
| build stamp of that exe | sourceTree=f2c4fbd89193 dirty=0 dirtyFiles=0 |
| window exit | rc=0 (window log: RUN19-KEYPATH-window.log) |

The product exe was replaced later (23:44:21, sha256 95aff0db..., build stamp
dirty=1 dirtyFiles=32) by a separate build-identity attempt. **Every reading below
was produced by 37388f4a, not by the exe now on disk.** The hud mutation suite was
re-run after that replacement and records its own exe.

## 2. Per step

### 2.1 hud-live (verify-release step 14/15) — PASS

- command: bash hud-live-check.sh (no arguments: forward only)
- rc=0, 22:26:50 -> 22:32:36 (5m46s), gate log: hud2.log
- verdict line: HUD-LIVE-CHECK=PASS, 65 `OK` / 0 `FAIL`, seven sections
- readings kept: hud/readings-forward-run19-exe37388f4a.txt (= the file this run wrote
  to the tracked evidence/t97-hud/readings.txt; the tracked file has since been
  rewritten by RUN21, see 3.1); full field-by-field provenance in hud/PROVENANCE.md
  (that document describes exactly this run, exe 37388f4a, advance_skips=423)
- the run ran six product invocations: live, off, suppress, advance (tick 0 -> 9000),
  resized window, live again; the suppressed frame is byte-identical to the
  --hud-off frame (that is the assertion, not a claim)
- **this run carried no --mutation**: the three mutation cases are offline evidence
  from the gate as of commit 8290374 and were re-run in RUN21 (see 3.1)

### 2.2 world-geometry-budget (step 10) — PASS, but not in this window

The stage inside the window deadlocked and was killed: world-geometry-budget-check.sh
takes the shared lock itself (:129-142) and this window already held it, so the
nested wait sat for thirteen minutes with no readings and the stage timeout killed
it (budget.rc=137, log kept as budget/budget-stage-inside-window.log). The gate was
then run standalone in RUN20 (23:36 -> 23:44:03), where it locks per capture:

- command: bash world-geometry-budget-check.sh (default pins evidence/t88-maps/budget-pins.tsv)
- verdict line: WORLD-GEOMETRY-BUDGET=PASS, 0 FAIL; readings: budget/budget.log
- four maps at capacity 400000: snakewater requested=242361 dropped=0,
  sunshine 82859, bagel_rc12 98066, bagel_rc11 98029 — all state=within, dropped=0
- face partition (the D-42 defect this gate exists for): snakewater world=12471
  brush=3632 drawn=3246 skipped=386 unattributed=0; sunshine 16787/3662/3129/533/0;
  bagel_rc12 11157/7078/6761/317/0; bagel_rc11 11157/7074/6757/317/0
- shrink probes at 200000 and 100000: snakewater reads dropped=42643 / 144209 and
  facesDropped=3513 / 4296 (the demand does not move with the capacity); the other
  three maps fit and read dropped=0 at both limits
- real profile untouched: H1 pass (settings.ini 2786 B, ui.ini 978 B, same before/after)
- the four forward traces this run rewrote are tracked: evidence/t88-maps/trace-*`.trace
- **this gate prints a verdict with no assertion count and no per-assertion line on
  the green path** (judge_map: only failures print). Recorded as D-62 and being
  fixed; the readings above are what the gate measured, not a count of what it checked.

### 2.3 outline (step 5) — FAIL on one product reading

- command: bash outline-check.sh, rc=1, 22:46:05 -> 23:32, gate log: outline/outline.log
- verdict line: OUTLINE-CHECK=FAIL, 181 `OK` / 1 `FAIL`
- the one red: outline-check.sh:1279
  `FAIL layer A: no admitted world model failed to build = 1, expected 0`
  from the trace line
  `budget: prepare capacity=64 candidates=60 admitted=60 built=59 deferred=0 failed=1 admittedByPriority=player:9,weapon:20,overlay:0,prop:25,firstPersonOnly:6`
  with `gpu: models done 59 uploaded=59` on the same frame
- what this is: **one admitted world model failed to build on cp_snakewater_final1**.
  The ledger accounts for it (admitted 60 = built 59 + failed 1), so nothing vanished;
  the gate has a separate assertion that says a failure must be zero, and it is one.
  This is a product reading, not a gate bug; the gate has no line naming which model,
  so the identity is unknown (D-63, diagnosis open)
- every other section is green: the harness can go red, the five coverage cases as
  data, identity/team/pose at every sampled tick, both colours and both directions,
  depth awareness three ways, the toggle (one slot, one owner, foreign presses
  refused = 0), the corpse rule (removed=17 of 20 samples, kept=3 silhouettes,
  no silhouette without a body)
- measurements: 22,671,053 candidate triangles at tick 2000 on snakewater (cpu probe),
  prepared=59 uploaded=59, layer B resident=68 capacity=96, layer C submitted=96
  accepted=95 slots=236, layer D players=15 props=81 droppedProps=77
- **this run was the gate's first on this tree and its first ever red**; the reading
  `candidates=60 admitted=60 built=59` has no precedent in the repo (the older
  recorded reading is candidates=98 admitted=64 built=64 failed=0), so it is a new
  population, not a regression of a previously green number

### 2.4 timeline (step 13) — PASS

- command: bash timeline-check.sh, rc=0, 23:32 -> 23:37, gate log: timeline.log
- verdict line: TIMELINE-CHECK=PASS assertions=29, 29 `OK` / 0 `FAIL`
- four behaviours: seek to 12000/20001/24001, pause/resume (same-tick match=1,
  resume advances past the pause tick, wall time not replayed), out-of-range seeks
  clamp to the demo's own ends (tick 0 and 64352), playback speed 1 vs 32
- the gate writes into evidence/t85-timeline/ (git-ignored), so the small readings
  are copied here: timeline/<case>/stdout.txt and metrics.csv; the per-case product
  traces above 300 KB are left in the ignored directory and listed in
  .left-out/BIG-TRACES.txt

### 2.5 build-identity (step 9) — PASS with no evidence kept

- command: bash build-identity-check.sh, rc=0, 23:32 -> 23:37, gate log: buildid.log
- verdict line: BUILD-IDENTITY=PASS assertions=115 identity=exe-sha+HEAD+native-tree-sha head=f2c4fbd89193
- **and eleven `cp: cannot create regular file` errors**: the gate's default output
  directory was `$REPO/.scratch/build-identity/forward`, created at :108/:111 and
  then deleted by the gate's own `rm -rf "$SCRATCH"` at :126, so every copy failed
  and the verdict printed anyway. Recorded as D-60; the fix (default OUT moved out
  of the scratch tree, copies counted, and an assertion that the output directory
  holds every file) is in the working tree uncommitted at the time of writing
- therefore **this step has a verdict but no evidence**, and the verdict is not
  reusable: the gate is being re-run on a quiet, committed tree (see section 4)

## 3. Runs after the window that this directory also records

### 3.1 RUN21 — hud mutation suite (same day, later)

- because the RUN19 hud stage carried no --mutation, the three cases (m1 drop-text,
  m2 freeze clock, m3 fake name) were re-run under the lock after the exe had been
  replaced by the dirty-stamped build (sha256 95aff0db..., build stamp dirty=1)
- rc=0, verdict lines: MUTATION-SUITE=PASS and HUD-LIVE-CHECK=PASS; evidence:
  hud/hudmut-run21.log and hud/readings-forward-run21-exe95aff0db.txt (the forward
  half, and the file that the tracked evidence/t97-hud/readings.txt now holds:
  same assertions, advance_skips=606 instead of 423)
- each mutation turned NAMED assertions red and then came back byte-identical:
  m1-drop-text 13 assertions red, m2-freeze 4, m3-fake-name 2, and after each
  RESTORED-IDENTICAL native/src/main_hud.cpp (sha256 5475791b...)
- evidence/t97-hud/mutation/readings.txt is NOT from this run: the gate only ever
  writes $OUT/readings.txt (hud-live-check.sh:678), so that file is a hand-kept
  record from the 21:46 run (exe 65533cb3, gate d29bb95c) and is stale; the RUN21
  mutation readings exist only as the log lines quoted above

### 3.2 build-identity red run (23:43:55) — not a verdict on the code

- `BUILD-IDENTITY=FAIL assertions=115 red=4`: a no-op rebuild relinked the product
  and rewrote the stamp, because `dirtyFiles` moved 31 -> 32 -> 34 while the gate
  ran. The writes were evidence files of RUN20 (mtimes 23:40:15 / 23:41:33 /
  23:43:03 / 23:44:00.639) landing inside the gate's window
- **the lesson is not the gate's**: the shared lock serialises product runs, it does
  not stop a process from writing a tracked file. build-identity measures the tree's
  dirt by design, so it must run on a tree nobody is writing (D-61)

## 4. What this directory does not prove

1. It is not a verify-release run: no step ran in the chain's own output directory
   and step ordering was chosen by me, not by verify-release.sh.
2. outline is red. A chain run today would stop red at step 5 until the
   prepare-failure reading is explained and either fixed or re-pinned with a reason.
3. build-identity's reading is not reusable (2.5); a clean-tree re-run is required.
4. body-appearance (step 7) and the four never-run steps beyond these were not
   attempted in this window.
5. Every pixel comparison in the repo still predates the HUD, 8.3-2 and CB changes;
   class-pixels is deliberately deferred (D:/_shared/EXEC-2026-11-09/PIN-REFRESH-PLAN.md).

## 5. How to re-check

```
git -C /d/TF2_Native_Worktrees/t91-integration log -1 --format=%H   # expect f2c4fbd (or later)
sha256sum /d/TF2_Native_Worktrees/t91-integration/native/build-nmake/tf2_demo_native.exe
grep -c '^  OK'   evidence/t91-merge/RUN19-20261109/outline/outline.log    # 181
grep -c '^  FAIL' evidence/t91-merge/RUN19-20261109/outline/outline.log    # 1
grep -n 'no admitted world model failed to build' evidence/t91-merge/RUN19-20261109/outline/outline.log
grep -n 'budget: prepare' evidence/t91-merge/RUN19-20261109/outline/outline.log
tail -1 evidence/t91-merge/RUN19-20261109/timeline.log                     # TIMELINE-CHECK=PASS assertions=29
tail -1 evidence/t91-merge/RUN19-20261109/hud2.log                         # HUD-LIVE-CHECK=PASS
tail -1 evidence/t91-merge/RUN19-20261109/budget/budget.log                # WORLD-GEOMETRY-BUDGET=PASS
```
