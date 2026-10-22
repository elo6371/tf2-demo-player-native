# T54 -- logs.tf embedded viewer: what the gate proves, and what it does not

Verdict line from the single frozen-tree run (lock `t54-logstf`, 2026-10-22
22:16:53 - 22:47:57, logdir `.scratch/t54-locked-20261022-221653`):

```
T54-RUN-DONE build=0 forward=0 mutation=0 readiness=0
LOGSTF=PASS panel_content=1 runtime_available=1
LOGSTF-MUTATION=PASS (six capture mutations, four product mutations and one seam mutation all went red)
  cases=13 checks=145 mutations=0
  INSTALL-READINESS=PASS
SOURCES-RESTORED=PASS (every mutated source is byte-identical to before the run)
```

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
| resizable pane | `script=resize panel=944` clamped to the edge, `state=content` kept |
| normal scrolling | `script=scroll scrollCalls=1 scriptFailures=0 rc=0` (probe asserts the offset the page took: `scrollOffset=480`) |
| browser-like back/forward/reload | `script=history navigations=2 canBack=1 scriptFailures=0` |
| close/reopen | `script=reopen opens=2 closes=1 hostStarts=2 state=content` |
| playback stays usable | every product run above is a normal demo run: 6320-6460 trace lines, capture frames produced, `draw: end` through the whole run |
| no credentials, ever | the viewer stores a URL string and nothing else; there is no login path, no cookie store of ours, no token field in any structure the panel touches |

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

## The tools in this directory

`evidence/logstf/tools/` holds the four scripts that produced the run above. They
were written in `.scratch/`, which is git-ignored -- and a cross-check that lives
only in an ignored directory is a cross-check that does not exist for the next
reader. Each one is here for a reason, and each is safe to re-run:

| tool | what it does | when to reach for it |
|---|---|---|
| `t54-locked-run.sh` | the whole locked run in one script. It reads the tree status **after** it has copied this run's artefacts and regenerated the manifest -- the line used to sit above those writes and therefore reported a clean tree that could not have included the files the run had just produced: hashes the six sources, builds, install-preview, probe, gate-self-check, the forward gate, the readiness gate, the eleven mutations, restores and compares every mutated source byte-for-byte | reproducing this verdict, or re-running it after a change. It must run **under `hostlock.sh`**: `bash _shared/hostlock.sh run t54-logstf bash evidence/logstf/tools/t54-locked-run.sh` |
| `t54-locked-run.sh --traces-check` | **just** the trace-manifest check, on the real tree, no build and no launch: it **regenerates** the manifest and compares its line count with the number of traces on disk, so it proves **coverage** -- every trace present is pinned. It prints `TRACES-HASH=OK (14 trace file(s) pinned)` and exits 0; with the traces moved away it prints `TRACES-HASH=FAIL` and exits 1, which in a full run is the summary's `traces=1` and `exit 23`. It does **not** prove integrity against a previously stored manifest -- it rewrites the file before comparing, so "the traces are unchanged since the last run" is git's question and the run's `git status` line's question, not this tool's | when you doubt the manifest, or after touching it. A check reachable only through a twenty-minute run is a check nobody exercises |
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
