# T47 -- cold-cache install-readiness determinism (2026-10-21)

Branch `task/t47-cold-readiness` (base `6888e6d`, worktree
`D:\TF2_Native_Worktrees\t47-cold-readiness`). Files touched:
`install-readiness-check.sh` and `install-readiness-capture.ps1`.
`title-capture.ps1` was NOT touched (shared T9 harness; out of this scope), and no
T42/T37 assertion was removed or weakened.

## 1. What T43's cold run actually measured

T43's cold run printed four red assertions on first-launch / restart /
explicit-root, each in this shape:

    TITLE-CAPTURE=PASS (len=30 brackets=no)      <- the window's load-time title
    FAIL first-launch connected root = , expected <root>
    FAIL restart reused the saved root ...
    FAIL explicit-root connected root = ...

while the trace of the same runs already carried

    resources: requested=... tf_root=D:\SteamLibrary\...\Team Fortress 2\tf valid=1

So the product had already decided the root (valid=1, at 0.15 s of run time) when
the gate read the title; the title simply had not been replaced yet. The old gate
read the title as the root decision, so a slow title settle looked like a wrong
root. This is a TITLE-CAPTURE timing failure, not a root-selection regression.

## 2. Reproduction on this host (two cycles, separate packages and settings)

Method: `_shared/t47/repro-cold.ps1` -- the installed binary is staged with
`cmake --install` into two separate prefixes (install-A / install-B, both
byte-identical to the built product), run from its own directory with
`--demo --hidden-window --audio-disabled --start-paused --settings-file`, and the
window title is polled every 200 ms while the product trace is read incrementally.
Every poll is kept in `samples.csv`. `_shared/t47/two-cycle.py` recomputes the
root decision from the raw trace and title (`tf_root`/`valid` on the resources
line, and the drive path before `[VPK entries:`) -- not from a convenience copy.

The cache was NOT cleared (out of bounds on this host). These are warm-cache
measurements; they bound the old failure instead of re-creating it:

    case            A: window / resources / settle    B: window / resources / settle    A polls  B polls
    first-launch    0.243 s / 161.7 ms / 20.893 s      0.328 s / 165.3 ms / 20.264 s      97       94
    restart         0.220 s / 155.8 ms / 20.371 s      0.223 s / 191.2 ms / 24.014 s      96       113
    explicit-root   0.215 s / 148.1 ms / 21.475 s      0.219 s / 195.3 ms / 20.986 s      101      99
    missing-root    0.214 s / 155.5 ms / 15.660 s      0.236 s / 188.1 ms / 14.799 s      74       70
    empty-root      0.220 s / 156.9 ms / 17.822 s      0.223 s / 180.2 ms / 23.396 s      84       110
    (window = first poll that saw the product window; resources = the trace's root
    decision line; settle = first poll with the connected/named title; 200 ms poll;
    every sample is kept in the repro samples.csv; bound_hit=false for every case.)

Readings, in words:

- The window exists ~0.22 s after launch and the `resources:` line (root decision)
  is written ~0.15 s in; the connected `[VPK entries:` title arrives ~20-24 s in.
  The trace root decision is therefore available ~140x earlier than the title.
- Under the old 30 s bound a warm run had only ~6-9 s of margin. Any cold-cache
  slowdown above 1.25-1.43x closed the product before the title settled -- which is
  exactly the T43 reading (title still len=30, trace already valid=1).
- Two cycles from separate staging prefixes and fresh settings directories produced
  identical root-selection decisions for all three required cases (and for the two
  unconnectable roots): DECISION-MISMATCHES=0, ROOT-ACCOUNTING-FAILURES=0,
  TWO-CYCLE=PASS. Full decision tuples:
  `exit|trace_valid|trace_root|connected_root|title_bracket` =
  `0|1|<real root>|<real root>|yes` for first-launch/restart/explicit-root, and
  `0|0|-||no` for missing-root/empty-root, in both cycles.

## 3. The gate change

`install-readiness-check.sh`:

- **Documented poll bound**: `TITLE_BOUND_SECONDS=120` (was a hardcoded 30 s).
  120 s is 5.0x the slowest warm settle measured here (24.0 s) and 4x the old 30 s
  bound that the cold run exceeded. It is a CAP: the capture stops polling as soon
  as the connected title appears, so a warm case still costs ~21 s.
- **Aggregate budget + fail-fast** so one red run cannot hold the shared lock:
  `CAPTURE_BUDGET_SECONDS=600` of title-capture wall time per capture invocation
  (measured warm forward capture spends ~7.5 min in capture), plus the driver
  aborting the remaining cases after the first classified timeout
  (`CAPTURE-ABORT`) and before a case once the ceiling is passed
  (`CAPTURE-BUDGET-EXHAUSTED`). Worst case is one bound (120 s) plus the 600 s
  ceiling plus fixed non-capture work: forward stays under ~15 minutes, so
  forward+mutation fits one host-lock hold well inside 30 minutes.
- **Trace root asserted on its own**, for every lifecycle case, from the
  `resources:` line, instead of inferring the decision from the title.
- **Second cold-start cycle** (section 9b): its own staged prefix and its own
  settings directories, and the decision tuple must equal the first cycle's for
  first-launch, restart and explicit-root. The staged binary is hashed for that
  cycle too, and the capture command line must name its own staged copy.
- **Positive control** (section 8b): the same capture is run with a deliberately
  too-short 5 s bound (shorter than the measured ~21 s warm settle). It must exit
  non-zero with exactly one `TITLE-CAPTURE-TIMEOUT case=...` line, trace still
  valid=1, title with no bracket, no generic `TITLE-CAPTURE=FAIL`, no connected
  root accepted, and `class=TITLE-CAPTURE-TIMEOUT` in `readiness.txt`.
- **Staged-binary hash check preserved and extended**: sha256 of the staged
  `tf2_demo_native.exe` is compared to the built exe at capture time and after the
  captures, at every mutation stage (mutated and restored), and each capture's
  recorded command line must name that staged copy -- so no stale installer,
  uninstaller or older executable can be measured.
- **No lifecycle mode may run during readiness captures**: every `command.txt`
  under the capture roots is grepped for `--install/--uninstall/--version/
  --install-status` and must be zero, before and after the mutation suite; no
  product process may exist before the forward run or before/after the mutation.
- **Assertion base preserved**: T42's forward run was `cases=13 checks=107`; every
  one of those 107 labels still exists and still passes. T47 adds assertions on
  top (see section 4). Mutation base: 40 assertions, T47 adds 13.

`install-readiness-capture.ps1`:

- New `-BudgetSeconds` ceiling and `Complete-Capture`/`Test-CaptureBudget`
  helpers so every exit path prints the same `cases=... failed=...
  titleTimeouts=...` summary.
- `Measure-CaseReadiness` classifies each case from trace + title:
  `OK`, `TITLE-CAPTURE-TIMEOUT` (expected connected, trace valid=1, no bracket),
  `ROOT-DISAGREEMENT` (trace says connected but the expected root is
  unconnectable, or valid=0 for a root that must connect), `TRACE-ROOT-MISSING`
  (no `resources:` line), `UNEXPECTED-CONNECTED-TITLE` (an unconnectable root
  produced a bracket), `TRACE-ROOT-UNPARSED`; and writes `readiness.txt` per
  case.
- `TITLE-CAPTURE-TIMEOUT` is printed on its own line and counted in the summary
  (`titleTimeouts=`); it makes the capture exit non-zero and is never accepted as
  a connected title. A second `CAPTURE-ABORT` line names the abort.
- New `cycle` case set for the second determinism cycle.

**One bug found in this new driver and fixed before the green run**: the
classifier wrote its READINESS lines on the PowerShell pipeline, so the caller
captured an array plus the object and strict-mode `$readiness.Class` failed after
the first case. That is a gate bug in T47's own change, not a product regression;
the first T47 forward run is red with `fail=59` for that reason (cascade of empty
case readings). The lines are now returned inside the object and emitted by the
caller; the mutation's single-case captures in the same run already show the
classifier working (`class=OK`, trace_valid=1, 17.1 s).

## 4. Results

Held 2026-10-21 16:57:21-17:09:57 on the shared lock (one hold). Build under
test at run start: `82b6c18e1d8bf23687d1a172f323abfa03309ec59c21966c3e9f7ac5a763ae47`
(built from the current sources, which the forward section 1 asserts are not newer
than the exe). Forward 9 m 39 s; mutation 2 m 57 s; total 12 m 40 s -- inside the
30-minute hold with the fail-fast and budget described in section 3.

### Forward: `INSTALL-READINESS=PASS`, `cases=13 checks=145 mutations=0`

- Assertion audit against T42's own evidence log (107 assertions, all OK):
  **107 -> 145, 38 added, 0 removed** (`_shared/t47/assertion-diff.py`; every
  added label is listed verbatim in
  `evidence/t37-install/t47-cold-readiness/gate/added-assertions-forward.txt`).
  They are: the positive control (8), the two-cycle assertions (15), the per-case
  readiness classes (6), the independent trace roots (3), the capture-time and
  after-capture staged hashes with the launched-binary and installer-mode checks
  (4), and the two capture-classification checks (2).
- Staged binary: `staged binary hash at capture time` = `staged binary hash after
  the captures` = `82b6c18e...` = the built exe; the first-launch command line
  names `.scratch/t47/gate/forward/install/tf2_demo_native.exe`, so the reading
  belongs to the staged copy. Cycle-2 stages the same hash from its own prefix.
- No readiness run asked for `--install/--uninstall/--version/--install-status`
  (0 command lines) and no product process existed before the run.
- Per-case readings (`readiness.txt` in each case dir):

      case            elapsed   trace valid  trace root                                  class
      first-launch     21.2 s       1        D:\...\Team Fortress 2\tf                 OK
      restart          20.7 s       1        D:\...\Team Fortress 2\tf                 OK
      explicit-root    21.9 s       1        D:\...\Team Fortress 2\tf                 OK
      missing-root    121.3 s       0        -                                           OK
      empty-root      121.2 s       0        -                                           OK
      relocated        24.3 s       1        D:\...\Team Fortress 2\tf                 OK
      store-precedence 17.7 s       1        ...\fixtures\roots\precedence            OK
      store-vpk-only   17.8 s       1        ...\fixtures\roots\vpk-only              OK
      store-download-maps 17.0 s    1        ...\fixtures\roots\download-maps         OK
      store-missing-map 18.1 s      1        ...\fixtures\roots\missing-map           OK
      store-vpk-corrupt 19.4 s      1        ...\fixtures\roots\vpk-corrupt           OK
      store-vpk-empty  18.7 s       1        ...\fixtures\roots\vpk-empty             OK
      store-vpk-truncated 17.7 s    1        ...\fixtures\roots\vpk-truncated         OK

  `cases=13 failed=0 titleTimeouts=0`: no case needed the 120 s bound.
  The two unconnectable roots pay it by construction (121 s each, including the
  close handshake) and are still class OK because the trace and the title agree
  that the root is not connected.
- Second cycle (own staged prefix, own settings): 21.7 / 21.1 / 21.3 s, class OK,
  and the decision tuple is identical to cycle 1 for all three required cases:
  `0|1|d:/steamlibrary/steamapps/common/team fortress 2/tf|...|yes` (the three
  "two cold-start cycles agree" assertions are OK, and both cycles' saved
  `tfRoot=` in `settings.ini` is the real root).
- Positive control (5 s bound, shorter than the 21 s warm settle of the same
  case): capture rc=1, `cases=1 failed=1 titleTimeouts=1`, exactly one
  `TITLE-CAPTURE-TIMEOUT case=first-launch ...` line, trace valid=1, title
  bracket=no, title_load_time=1, connected root empty, `class=TITLE-CAPTURE-TIMEOUT`.
  Its `elapsed_s=21.0` is the poll cut at 5 s plus the close handshake: the
  mid-load product only services WM_CLOSE after its load, which is exactly T43's
  situation (the trace had the root, the title had not settled).

### Mutation: `INSTALL-READINESS-MUTATION=PASS (3 mutations caught, sources restored)`

- `cases=4 checks=55 mutations=3` in the first green run. Against the
  40-assertion T42 log T47 adds 13 unique assertions, 0 removed (listed in
  `evidence/t37-install/t47-cold-readiness/gate/added-assertions-mutation.txt`): 10 staged-binary
  identity checks (hash of the staged exe at every mutated and restored stage,
  plus the recorded command line of four captures), 2 product-process
  preconditions, 1 no-lifecycle-mode check.
- Count note: in that first green run the new `no product process before the
  mutation suite` assertion sat inside the three-iteration forward-artifact loop,
  so it was counted three times (15 instances / 13 unique labels, `checks=55`). It
  was hoisted out of that loop afterwards with the same predicate, once and before
  any mutation; the committed code therefore prints `cases=4 checks=53` with the
  same 13 unique T47 assertions and the same readings below. The forward count
  (145) is from the committed code and did not change. A mutation-only rerun is
  scripted at `_shared/t47/mutation.sh` (~3 min under the lock) for whoever wants
  the matching log.
- M1 (candidate order reversed): caught -- `store=vpk bsp=invalid` (the BSP was
  lost), restored reference `store=maps`, 2512 tris.
- M2 (64-bit offset narrowed): caught -- `store=vpk refused=0` (the out-of-range
  entry was read as the map), restored reference `refused=1` with the named reason
  `VPK entry range outside directory archive`.
- M3 (smoke probe dropped from both install rules): caught -- the package lost both
  probe copies and nothing else; restored reference package list is the full
  5-file list.
- Staged binary hashes per stage (each asserted equal to the current build):
  M1 `cf25a19c...`, M1-restored `8665b143...`, M2 `d4da5410...`,
  M2-restored `373ca876...`, M3 `016fe748...`, M3-restored `2449b2be...`.
  The last one is also the build exe after the run: the mutation rebuilds are real
  and the final rebuild is of the restored sources (same sources as the forward
  run's `82b6c18e...`; MSVC link output is not byte-identical across rebuilds,
  which is why every capture is judged against the *current* build hash, not
  against a hash recorded earlier).
- Sources restored byte-identically: `map_lookup.cpp`, `vpk_archive.cpp`,
  `CMakeLists.txt` all = yes, and `git status` after the run shows only the two
  gate files (plus the new report/evidence files), no product source change.
- No mutation capture asked for a lifecycle mode (0) and no product process was
  left after the suite.


## 5. NOT VERIFIED

- True cold cache: system-wide caches were not cleared (explicitly out of bounds),
  so the cold failure is bounded by warm measurements plus the forced-timeout
  control, not re-created end-to-end. A host whose cold load exceeds 120 s would
  report TITLE-CAPTURE-TIMEOUT (red, named) rather than pass.
- The two unconnectable roots (missing-root, empty-root) cannot stop early in the
  shared title-capture harness (it only breaks on the VPK bracket), so each pays
  the full bound: ~2 minutes per forward run. That is the documented cost of
  keeping a uniform, non-weakened bound; a per-case shorter bound was considered
  and rejected as a second marginal number.
- The T43-era `30 s` behaviour was not re-run (no old build kept); the argument is
  warm-settle vs bound arithmetic, plus the T43 log itself.

## 6. Merge risk

- Only `install-readiness-check.sh` and `install-readiness-capture.ps1` plus new
  files under `evidence/t37-install/t47-cold-readiness/` and this report.
- `cases=13` forward shape and `cases=4 / mutations=3` mutation shape are
  preserved for T49's summary parsing; the capture summary line gained
  `titleTimeouts=N` after `failed=N`, so parsers that read `cases=... failed=...`
  by prefix keep working.
- Forward runtime grows from ~6 min to ~9-10 min (two unconnectable cases at the
  new bound, plus the second cycle); mutation unchanged. Worst case is bounded by
  the fail-fast and aggregate budget.
- Conflict surface: T45/T49 also edit `install-readiness-check.sh`; the T47
  hunks are additive sections (8b, 9b) and additive assertion lines, so a textual
  conflict is possible but semantic overlap should be small. T42's assertions were
  kept verbatim.
