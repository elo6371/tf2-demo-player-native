# T54 -- logs.tf embedded viewer: what the gate proves, and what it does not

Verdict line from the single frozen-tree run (lock `t54-logstf`, 2026-10-23
14:07:29 - 14:33:39, logdir `.scratch/t54-locked-20261023-140729`):

```
T54-RUN-DONE build=0 forward=0 mutation=0 readiness=0 traces=0
LOGSTF=PASS panel_content=1 runtime_available=1
LOGSTF-MUTATION=PASS (eight capture mutations, eight product mutations and one seam mutation all went red)
  INSTALL-READINESS=PASS
SOURCES-RESTORED=PASS (every mutated source is byte-identical to before the run)
SCROLL-SELFTEST_RC=0
```

`assertions_ok=43 assertions_bad=0`. The scroll reading that was 0 in the
previous run is now `scrollOffset=480 scrollRaw=480.0000305175781 scrollPage=1729
scrollViewport=608` -- the page is 1729 px tall in a 608 px viewport and the
offset the page reported after the scroll is 480. See "The vacuity question"
below for why that number is not the evidence.
Witnesses, all in this directory: `build.log`, `probe.txt`, `forward.log`,
`mutation.log`, `readiness.log`, `install-preview.log`,
`install-preview-files.txt`, `self-check.log`, `syntax-check.log`,
`sources-before.sha256`, `sources-after.sha256`, `traces/`.

## The acceptance points, each with the reading that settles it

| acceptance point | reading |
|---|---|
| empty until a URL is imported | `script=empty state=empty host=none navigations=0 accepted=0`; probe `emptyHostCalls=0` |
| the logs.tf URL shape is accepted | `script=import state=content host=alive navigations=1 accepted=1 url=https://logs.tf/1` |
| the host is validated before navigation | wrong host: `refused=1 navigations=0 refusalReason=host-not-allowed`; `http`: `refusalReason=not-https`; `javascript:`: `refusalReason=unsupported-scheme`; `unavailable=-` in all three |
| resizable pane | `script=resize panel=944` clamped to the edge, `state=content` kept (the request was 100000) |
| normal scrolling | `script=scroll scrollCalls=1 scriptFailures=0 scrollOffset=480 scrollRaw=480.0000305175781 scrollPage=1729 scrollViewport=608` -- the offset the page itself reported, on a page taller than its viewport |
| browser-like back/forward/reload | `script=history navigations=2 canBack=1 scriptFailures=0 historyMoved=1 urlBeforeHistory=https://logs.tf/1 urlAfterHistory=https://logs.tf/2` -- the address changed across the settled move, which a flag alone would not prove |
| close/reopen | `script=reopen opens=2 closes=1 hostStarts=2 state=content` |
| playback stays usable | every product run above is a normal demo run: 6320-6460 trace lines, capture frames produced, `draw: end` through the whole run |
| no credentials, ever | the viewer stores a URL string and nothing else; there is no login path, no cookie store of ours, no token field in any structure the panel touches |

## The reconciliation: the earlier scroll/history failure, classified

The Lead required a classification of the earlier user-reported "scroll/history
failing" against the later green run, with an identity for each. The answer is
**(a) different identity** for the red runs, and **a real defect that the green
run could not see** for the green one. Both halves matter.

### What was compared

Seven locked runs exist in `.scratch/t54-locked-*`, each with its own
`sources-before.sha256` over the same six files. Reduced to a snapshot hash of
`native/src/*` + the probe:

| run | sources | verdict |
|---|---|---|
| 20261022-202302 | snapshot A | `LOGSTF=FAIL` |
| 20261022-204440 | snapshot B | `LOGSTF=FAIL` |
| 20261022-205735 | snapshot B | `LOGSTF=FAIL` |
| 20261022-211720 | snapshot C | `LOGSTF=FAIL` |
| 20261022-212508 | snapshot D | `LOGSTF=FAIL` |
| 20261022-213710 | snapshot D | `LOGSTF=PASS` |
| 20261022-221653 | snapshot D | `LOGSTF=PASS` + `LOGSTF-MUTATION=PASS` |

**The five red runs are all on sources that no longer exist** (snapshots A, B, C)
or on a probe that was still changing (D at 21:25). They are not evidence about
the current tree: same task, different identity. That is class **(a)**, and the
specific reds are named in each run's `forward.log` -- e.g. 20:44 read
`FAIL scroll reached the page (wanted: scrollCalls=2)` and
`FAIL history kept both directions (wanted: canBack=1 canForward=1)`.

### The red runs were the gate being wrong, not the product

All three red causes were **wrong readings**, which is the pattern recorded in
`gate-design.md`:

| red reading | what it actually measured | correct form |
|---|---|---|
| `scrollCalls=2` | `pageExtent` counts `scriptCalls`, not `scrollCalls`; the scroll step issues one | `scrollCalls=1` |
| `canForward=1` at the end of the history script | the script ends on the newest entry after back/forward/reload, so an empty forward stack is correct | `canBack=1` + `scriptFailures=0` |
| `reopenState=content` | the empty-panel case had already set `reopenState`; the second `set()` overwrote it | separate keys |

### The green run was green for a reason that was not the product working

The 21:37 and 22:16 runs pass, and the gate's own readings are honest. But the
scroll assertion they pass (`scrollCalls=1 scriptFailures=0`) is the assertion the
Lead identified as vacuous: it holds when the page does not move. The 2026-10-23
run proved it -- `scrollCalls=1 scriptFailures=0 scrollOffset=0` on a page that
does scroll, caused by a truncated script. So the classification of the *green*
run is: **the readings were true, and insufficient.** Not a nondeterministic
defect and not a different identity -- the same identity, a real defect, and an
assertion that could not see it.

### The identity of the readings quoted in this report

| field | value |
|---|---|
| branch | `task/t54-logstf` |
| commits | `9b916c0` (the integrated baseline) + `4a5db94`, `b0f10ac`, `cbd4ef5`, `0086810` |
| exe sha256 | recorded in `sources-after.sha256` of the run's logdir; the binary is rebuilt by every run and is not committed |
| trace path | `.scratch/logstf/<run>.trace`, copied to `evidence/logstf/traces/` and pinned by `traces.sha256` |
| page | `https://logs.tf/1` (200, 48183 bytes, 4678 visible chars, 21 rows) |
| runtime | `webview2 155.0.4283.62 source=registry loader=present` |
| time | 2026-10-23 14:07:29 - 14:33:39 (+08:00) |

## The vacuity question: what goes red if the script never executes

The Lead asked, of the previous PASS: *which assertion goes red if the WebView
script never executes?* The honest answer about the previous round is **none of
them**. `scrollCalls=1` counts a call site; `scriptFailures=0` says no call was
refused; and a scroll offset of `0` satisfied "the offset is what the page took"
at `0 == 0`. The 2026-10-23 run demonstrated it with a real defect rather than an
argument (see below), which is why the question is now answered in code.

### The readings are four claims, not one

`scroll_verdicts()` (in `logstf-check.sh`) asserts them separately, and every
negative control reports **which one went red first**:

| assertion | what it claims | what counterfeits it |
|---|---|---|
| `scroll_offset_was_read` | an answer came back from the page at all | nothing: a script that never ran leaves the `none` sentinel |
| `scroll_raw_is_number` | the answer decoded to digits | an undecoded JSON envelope (`"480"`) or the literal `null` |
| `scroll_page_moved` | the offset is greater than 0, asserted only when the page is taller than the viewport | `0 == 0` on a page that cannot scroll -- hence the conditional and the `NOT-VERIFIED` branch |
| `scroll_script_did_not_fail` | `scriptFailures=0` | a successful call that answered nonsense |

### The negative controls, side by side with the healthy run

All four lines below are real product output. The last three come from the
mutation suite in the same locked run; the first is the forward scroll run.

```
healthy        scrollCalls=1 scriptFailures=0 scrollRaw=480.0000305175781 scrollOffset=480  scrollPage=1729 scrollViewport=608
m6 no script    scrollCalls=1 scriptFailures=0 scrollRaw=none                 scrollOffset=0    scrollPage=1729 scrollViewport=608
m8 undecoded    scrollCalls=1 scriptFailures=0 scrollRaw="480.0000305175781"  scrollOffset=0    scrollPage=0    scrollViewport=608
m9 truncated    scrollCalls=1 scriptFailures=0 scrollRaw=null                 scrollOffset=0    scrollPage=1729 scrollViewport=608
```

**The four counters are identical in every row.** `scrollCalls=1` and
`scriptFailures=0` are green in the healthy run and in all three defects -- those
are exactly the readings the previous PASS quoted. What moves is the page's own
answer, which is why the first two assertions are the ones that name the failure:

| control | injected defect | first red assertion |
|---|---|---|
| m6 | the scroll call is counted and never issued | `scroll_offset_was_read` |
| m8 | the browser's JSON envelope is not decoded | `scroll_raw_is_number` |
| m9 | the scroll script is truncated to 159 characters | `scroll_raw_is_number` (product) **and** `scrollScriptComplete=0` (probe) |

m9 requires **both** halves to go red: the probe holds the script as a string and
can refuse a truncated expression without a browser, while the product can only
observe what a page answers. Either alone would leave one half unwatched.

### The defect that answered the question

The first run with the new assertions (2026-10-23 13:00, `t54-locked-20261023-130000`) read
`scrollCalls=1 scriptFailures=0 scrollOffset=0` on a page 1729 px tall in a 608 px
viewport. The cause was not the page: `scrollContent` built its script with
`snprintf` into `char[160]`, and the script is **174 characters**, so the browser
received a truncated function, answered a `SyntaxError` as the JSON literal
`null`, and `atoi("null")` returned 0. A second defect sat behind it:
`ICoreWebView2::ExecuteScript` answers with **JSON**, so a script returning
`String(480)` arrives as `"480"` with quotes, and `atoi` stopped at the quote.
Both are fixed (`logsScrollScript`, `logsScriptValue`), and both have a mutation
that reintroduces them.

**This is the answer to the vacuity question in the form the acceptance asked
for**: the previous gate would have called that run PASS. The current one calls it
FAIL, on an assertion named `scroll_raw_is_number`, while the two counters the old
gate asserted stay green.

### The three named first reds, from the final run (14:07:29)

```
m6  (script never issued)
      FAIL scroll_offset_was_read
      FAIL scroll_raw_is_number
      FAIL scroll_page_moved
      OK   scroll_script_did_not_fail
  m6 readings: scrollCalls=1 scriptFailures=0 first_bad=scroll_offset_was_read

m8  (JSON envelope not decoded)
      OK   scroll_offset_was_read
      FAIL scroll_raw_is_number
      NOT-VERIFIED scroll_page_moved (page=0, so a scroll cannot move it)
      OK   scroll_script_did_not_fail
  m8 readings: scrollCalls=1 scriptFailures=0 first_bad=scroll_raw_is_number

m9  (script truncated)
      OK   scroll_offset_was_read
      FAIL scroll_raw_is_number
      FAIL scroll_page_moved
      OK   scroll_script_did_not_fail
  m9 readings: scrollCalls=1 scriptFailures=0 scrollRaw=null first_bad=scroll_raw_is_number
```

Note m8's `NOT-VERIFIED scroll_page_moved`: with the decode removed, `pageExtent`
also fails to parse, so `scrollPage` reads 0 and the "could this page scroll?"
premise is false. The gate says so instead of counting a 0==0 as a pass. That is
the conditional branch doing its job on a real defect, not a hypothetical.

### The control runs without a lock

`evidence/logstf/tools/scroll-verdicts-selftest.sh` replays the recorded readings
through the gate's **own** `scroll_verdicts` (extracted with `sed` from
`logstf-check.sh`, not copied) and requires the specific first red in each case:

```
SCROLL-VERDICTS-SELFTEST=PASS (4 cases, extracted from logstf-check.sh)
  OK   healthy run: every scroll verdict held
  OK   script never ran (m6): first red = scroll_offset_was_read
  OK   JSON not decoded (m8): first red = scroll_raw_is_number
  OK   script truncated (m9): first red = scroll_raw_is_number
  OK   all four lines carry the SAME counters (scrollCalls=1 scriptFailures=0)
```

It needs no lock, no build and no browser, so the control is cheap enough to
re-run -- a check reachable only through a twenty-minute locked run is one nobody
re-runs, and the next person to break the injection would hear about it from a
user. It is wired into `t54-locked-run.sh` and prints `SCROLL-SELFTEST_RC`.

**What it proves and what it does not**: it proves the assertions discriminate the
recorded reading shapes. It does not prove a live browser produces those shapes;
that is the product.run assertions above, and their mutations (m6/m8/m9) rebuild
and re-run the product for real.

## (a) Why the vendored WebView2 SDK is not downloaded at build time

The 2.7 MB `third_party/webview2` is a build **input**, not a build product, and it
is vendored because the alternative is a build that cannot be reproduced offline or
audited:

- **A download at configure time makes the build depend on a third party's uptime and
  on whatever it serves that day.** The SDK is C++ headers plus `WebView2Loader.dll`;
  a silent version change between two builds of the same commit is exactly the kind of
  difference this repository's evidence model cannot accept -- two runs of the same
  source would no longer be the same artefact.
- **It cannot be optional.** `logstf_webview.cpp` includes `WebView2.h`; without the
  headers that translation unit does not compile, and the product would silently lose
  the panel the task asks for. "Optional" here means "a different product", not "the
  same product with one feature off" -- the ticket requires the real runtime path, not
  a build that cannot have it.
- **Licence coverage is part of the package.** `third_party/webview2/LICENSE.txt` and
  `NOTICE.txt` are installed to the package root by `native/CMakeLists.txt`, and the
  run above **observed them landing**: `install-preview-files.txt` lists both, and the
  readiness gate checks the exact 11-name list, the count, and the payload count of 4
  against that file. That is the difference between "we vendored a licence file" and
  "the licence file ships".
- **The runtime itself is not vendored.** The SDK is a build input; the WebView2
  *runtime* belongs to the machine and is detected at run time (`source=registry
  version=155.0.4283.62 loader=present`).

## (b) The host already having a runtime: NOT VERIFIED

The task allows a fallback "if the host already has a runtime, use it". What the run
proves is the opposite direction -- the runtime **was** present and the panel used it:

- `logstf: runtime kind=webview2 available=1 source=registry version=155.0.4283.62 loader=present`
- `hostStarts=1 hostFailures=0`, and the embedded page accepted scripts
  (`scrollCalls=1 scriptFailures=0`).

**NOT VERIFIED: that a host which already carries the runtime in another location
(for example a per-machine evergreen install that the registry lookup does not name)
is detected by a path other than the registry.** T54 detects the runtime by the
registry key the SDK's loader uses and by the `WebView2Loader.dll` beside the
executable; no run has exercised a host where the loader is found but the registry
lookup is not, or vice versa. The fallback the product reports when nothing is found
is `unavailable=<reason>` plus `fallback=<action>` in the summary line, and the probe
asserts the unavailable state names its reason (`unavailableHasReason=1`), but that
is a **contract** test against a fake host, not an observation of a runtime-less
machine.

**REAL-PATH=EVIDENCED**
`(state=content host=alive url=https://logs.tf/1 navigations=1 hostStarts=1)`

This is the only reading in the task that comes from the real WebView2 runtime. The
probe's fake host cannot produce it: it is synchronous, it has no window, and it
never runs a browser process. The scroll defect that took two rounds to find
(`rc=127`, no summary line, process gone) lived exactly here -- `runScript` pumped
messages filtered to the panel's parent window, while WebView2 delivers the
completion to a window of its own, so every call waited out its deadline. No probe
could have seen it. Everything the probe proves is a statement about the contract;
only this line is a statement about the runtime.

## NOT VERIFIED (each one is a claim the run does not settle)

1. **A runtime-less host.** No run has been made on a machine where WebView2 is
   absent, so `state=unavailable` + reason + fallback are covered only by the fake
   host.
2. **A host that carries the runtime outside the registry lookup** (see (b)).
3. **Install/uninstall lifecycle semantics.** `install-lifecycle-check.sh`'s twelve
   constants are **aligned and derived, never run** -- this branch has not executed
   that script. The readiness gate's 13 cases / 145 checks are a different question
   (does the package contain what we say) and they pass.
4. **Long-run page behaviour** (a page that navigates itself, a login wall, a huge
   log). The viewer refuses to impersonate a session and no test drives one.
5. **Screen-reader/focus semantics of the embedded pane.** The panel reports
   geometry and the browser owns focus; no observation exists.
6. **Back/forward across a close/reopen boundary.** The reopen case counts
   `opens=2 closes=1` and re-navigates; history across that boundary is not asserted.

7. **A page that is shorter than the pane.** `scroll_page_moved` is then
   `NOT-VERIFIED` by design (0 == 0 would be vacuous). The 2026-10-23 run had no
   such page: `scrollPage=1729 scrollViewport=608`. The branch is exercised -- m8
   drove it, reading `page=0 viewport=608` -- so the code path is proven, but the
   "short page, offset legitimately 0" case has no run on a real short page.
8. **A scroll that starts from a non-zero offset.** The script asks for +480 from
   wherever the page is. No run has scrolled twice, so "the second scroll reports
   an absolute offset rather than a delta" is a property of the script, not an
   observation (the probe checks the string uses `scrollTo`, not `scrollBy`).

## The tools in this directory

`evidence/logstf/tools/` holds the four scripts that produced the run above. They
were written in `.scratch/`, which is git-ignored -- and a cross-check that lives
only in an ignored directory is a cross-check that does not exist for the next
reader. Each one is here for a reason, and each is safe to re-run:

| tool | what it does | when to reach for it |
|---|---|---|
| `t54-locked-run.sh` | the whole locked run in one script. It reads the tree status **after** it has copied this run's artefacts and regenerated the manifest -- the line used to sit above those writes and therefore reported a clean tree that could not have included the files the run had just produced: hashes the six sources, builds, install-preview, probe, gate-self-check, the scroll-verdict self-test, the forward gate, the readiness gate, the seventeen mutations, the scroll-verdict self-test, restores and compares every mutated source byte-for-byte | reproducing this verdict, or re-running it after a change. It must run **under `hostlock.sh`**: `bash _shared/hostlock.sh run t54-logstf bash evidence/logstf/tools/t54-locked-run.sh` |
| `t54-locked-run.sh --traces-check` | **just** the trace-manifest check, on the real tree, no build and no launch: it **regenerates** the manifest and compares its line count with the number of traces on disk, so it proves **coverage** -- every trace present is pinned. It prints `TRACES-HASH=OK (14 trace file(s) pinned)` and exits 0; with the traces moved away it prints `TRACES-HASH=FAIL` and exits 1, which in a full run is the summary's `traces=1` and `exit 23`. It does **not** prove integrity against a previously stored manifest -- it rewrites the file before comparing, so "the traces are unchanged since the last run" is git's question and the run's `git status` line's question, not this tool's | when you doubt the manifest, or after touching it. A check reachable only through a twenty-minute run is a check nobody exercises |
| `scroll-verdicts-selftest.sh` | replays **real recorded summary lines** (the healthy scroll run and the three mutation runs) through the gate's **own** `scroll_verdicts`, extracted with `sed` from `logstf-check.sh` rather than copied, and requires the specific first red in each case: healthy -> none, m6 -> `scroll_offset_was_read`, m8 -> `scroll_raw_is_number`, m9 -> `scroll_raw_is_number`. It also asserts that all four lines carry the **same** `scrollCalls`/`scriptFailures`, which is what proves those counters could not have caught the defect. No lock, no build, no browser | before and after touching the scroll path: it is the negative-control proof required by the acceptance, and it costs two seconds. **A green run of this tool is not a green gate** -- it tests the assertions, not the product |
| `verify-gate.sh` | extracts the gate's **own** `safety_check` predicate and **asserts** three argv shapes: missing `--audio-disabled` -> 98, missing `--hidden-window` -> 99, both present -> 0; and counts launch sites | after touching the launch guard. It tests the real predicate rather than a copy, which is the only way that check means anything |
| `rc-probe.sh` | the exit-code table measured on this machine: command-not-found 127, SIGSEGV 139, SIGABRT 134, native SEH access violation 1, `exit(127)` 127 | whenever an exit code is used as evidence. `rc=127` alone does **not** mean "command not found" -- this table is why that claim needed five independent readings |
| `rc-probe2.sh` | the same probes through MSYS bash's own wrappers, for the cases where the outer shell rewrites the status | extending the table. Read both before quoting a code |

## A note on the older logs

`forward.log` and `mutation.log` were written by the run before the safety
self-test grew its third case, and their first line still reads
`LAUNCH-SAFETY=OK (1 launch site, predicate rejects a flagless argv)`. That
wording is kept **deliberately**: editing a log so it matches the current code
would make the log stop being a record of what actually ran. The corrected
wording and the pinned 98/99 codes live in the code and in the readings of the
next run.

## Gate design notes

See `evidence/logstf/gate-design.md`: the four defects fixed in the last round were
all **wrong readings, not wrong behaviour** (`scrollCalls` object, `canForward` at
the end of the script, a relative `$LOGDIR`, and a claimed-but-absent third
self-test), and the asynchronous-host branch has both a probe case and mutation m5
that prove it can go red.
