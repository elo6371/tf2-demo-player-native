# T49 — fullscreen non-interference: what this gate proves, and what it does not

Status of this file: **final for the merged tree**. The verdict comes from
`evidence/t49/run-20261023-114221/` (HEAD `66c4327`, the tree that ran is the tree that was
committed; the runner hashes the working tree before and after and requires it unchanged).
Every other line is a reading that already exists on disk. §8 lists a defect this run's own
readings exposed and that was deliberately NOT changed afterwards, with the reason.

## 1. The claim

The operator asked for one thing, in their own words: *a test window that pops up must not
take me out of my fullscreen application, and the chain should run hidden in the background.*

The product already has a gate (quiet-capture-check.sh) that answers "is the product window
hidden", using the program's own `IsWindowVisible` reading. That is a different question. A
window can be created hidden and still take the foreground while it initialises, and a
process can steal activation without ever showing a window of its own. So this gate measures
**the desktop**, not the product:

| what is measured | how |
|---|---|
| who holds the foreground | `GetForegroundWindow` + `GetWindowThreadProcessId`, every 20 ms |
| whether the guard lost activation | the form's `Deactivate` event |
| whether the display mode moved | `EnumDisplaySettings` before/after, width/height/refresh/bpp |
| whether a window of the product was on screen | `EnumWindows` + `IsWindowVisible`, every 200 ms, matched by process name |
| whether the counter can fire at all | an independent window asks for the foreground; the guard must charge it by name |

Three phases on every invocation: **baseline** (guard alone), **forward** (`--hidden-window
--audio-disabled`), **control** (same run with the window shown). The control phase is the
point: a quiet forward reading means nothing unless something in the same run is known to be
visible to the instrument.

## 2. What went wrong, as readings

### 2.1 The blind window (the one that mattered)

The gate ran a 16 s guard. The product opens its VPK archives for ~18.5 s before it creates
and shows its window:

Then a window dump from an independent process settled what the guard was actually looking
at, by listing every window the product's pid owned without a visibility filter:

```
OWNED hwnd=0x3A09BC pid=27048 class=TF2DemoNativeRenderer rect=286,286,1566,1006
      vis=0  WS_CHILD=0 WS_POPUP=0 parent=0x0  ex=0x110 (TOPMOST|WINDOWEDGE)
sample 3..10  t=4.1s..18.2s   vis=0 WS_VISIBLE=0
sample 11     t=20266 ms      vis=1 WS_VISIBLE=1
```

The window is a **top-level** window (not a child), it belongs to the product's own pid, and
it was **created hidden at ~4 s and shown at ~19.5 s**. The guard's zero was therefore a true
reading of a window that was not there yet -- and a 42 s guard run on the same product records
`watchVisibleSamples=25`, `watchWindowFound=1`, `coveredMs=41987`: the enumeration was never
the problem. 25 samples of 200 ms is ~5 s, which matches "shown at 19.5 s, process exits at
23.7 s".

The window's styles are worth one line: `WS_EX_TOPMOST | WS_EX_WINDOWEDGE`, with **no**
`WS_EX_NOACTIVATE` and **no** `WS_EX_TOOLWINDOW`. It is not a window designed never to
activate; it simply was not shown yet.

The rest of this section is the original reading:

```
control.trace   19506.5  mode: window_created=1 window_visible=1 hidden=0
control.trace   23401.9  mode: summary ... exit=0
forward.trace   19702.4  mode: window_created=1 window_visible=0 hidden=1
```

The guard starts ~1 s before the product, so its observation interval was `[-1 s, 15 s]` in
product time. **The product's window did not exist yet when the guard closed.** Every counter
it printed was a clean zero, and a clean zero is exactly what a product that never interferes
looks like.

Two consequences, both worse than the original symptom:

- the forward phase's claims ("never took the foreground", "no product window was visible")
  were **vacuous** in that window: they could not have failed;
- the control phase's sensitivity assertion could never pass, so six runs produced readings
  that all looked "almost there".

The gate now uses **32 s** and, more importantly, **checks the coverage** instead of trusting
the number: the guard writes `startEpochMs`/`stopEpochMs`, and the gate asserts that the
interval contains the trace's `window_created` instant with 2 s of lead and 2 s of tail, and
that it outlived the product's process. A migration of the product's startup time past the
margin fails the gate instead of silently emptying it.

The launch skew between the shell's epoch and the product's own counter is printed on every
run (`launch_skew_ms`) and bounded at 2 s, because a wrong clock basis shows up as a
negative or seconds-scale offset, not as a slow fork.

### 2.2 A missing reading was read as a value

```
baseline stolenBy=ABSENT
FAIL the baseline did not contain the product = ABSENT (want 0)
```

The guard writes `guard.stolenBy=` empty when nobody stole the foreground — a real reading.
The gate's reader could not tell "empty" from "missing" and returned a sentinel that was then
compared with `0`. Fixed by adding `guard.stolenByCount` (a number, present even when zero):
count 0 means zero thieves; count > 0 with no names is an inconsistent file and is reported as
unreadable; only then does the name list decide. There is no `ABSENT -> 0` translation layer.

The same family appeared three more times in this task:

1. `control.trace` never written (a path built twice, `$PWD/$absolute`), and the empty read
   was reported as "the product says its window IS visible";
2. the queue listing: a ticket is deleted the moment the lock is acquired, so "not in the
   queue" is by design, not evidence of queue jumping;
3. this gate's own `assert_eq "still captured a frame" 0 (want 0)` — an assertion whose
   message described a file that was never checked.

### 2.3 Assertions that could not fail

```
OK   the hidden run still captured a frame = 0
```

The value and the expected value are the same exit code. "Captured a frame" is now asserted
against the artefacts (`--capture-frame` bytes > 1000, `--metrics-file` rows > 0), and the
exit code is asserted separately and described as what it is ("exited cleanly").

## 3. Instrument sensitivity, measured

A window shown by a process the guard did **not** start takes the foreground from the
fullscreen topmost guard on this machine:

```
target:  setForegroundWindow=True  foregroundPid=47688  myPid=47688
guard:   stolenBy=t49-target  deactivations=1  modeChangedFields=0
```

and inside the gate, every run re-measures it (`--instrument`, 10 s):

```
instrument setForegroundWindow=True  guard.stolenBy=instrument-target  guard.deactivations=1
OK   an independent window was accepted as the foreground owner
OK   the guard charged that foreground change to the window's process by name
```

On the merged tree (2026-10-23) the same phase needed no arrangement at all: the target's own
`SetForegroundWindow` was accepted (`target.setForegroundWindow=True`), the guard charged it
by name (`stolenByCount=1 stolenBy=instrument-target`), `handOffSelfOk=1 handOffOk=1`,
`handOffAttributed=1`, and no thread attachment was left behind
(`attachedThreadsOutstanding=0`). That is the `instrument_source=natural` case: the evidence
used was a foreground change the harness did not arrange.

What that proves: the foreground counter fires and the by-name attribution chain works.
What it does **not** prove: that the product has the same permission or behaviour — the
instrument window asks for the foreground on purpose, while the product's window may be
created without activation by design. The two are never conflated in the verdict.

### Instrument self-test without the product

```
guard.watch=t49-target
guard.watchVisibleSamples=49   (49 of 50 samples over 10 s)
guard.watchWindowFound=1  handOffSent=1  handOffOk=1  handOffAttributed=1
guard.stolenBy=t49-target
```

The first attempt used `notepad` as the watched process and reported zero visible windows:
on this machine Notepad is a Store app whose own window answers `IsWindowVisible == false`
(`MainWindowHandle=0`). A bad target makes the instrument look broken; the same reading is
produced by a product that is quiet. The target has to be a plain Win32 window.

## 4. Products facts noticed here, deliberately not fixed

- The product needs **~20 s** before it shows a window (`vpk: archives opened=4` at
  18.5 s, window at 19.5 s). This is user-visible: a cold start looks like nothing happened
  for 20 seconds. Recorded as a reading for whoever owns startup latency; not changed here.
- The window-creation instant is ~0.2 s apart between hidden and shown runs (19702.4 vs
  19506.5 ms), so it is decided by the startup path, not by visibility.

## 5. What the gate proves / does not prove

**Proves, when green, in two parts** (both required, neither is a count):

```
1. 产品窗口出现的那一瞬间，守卫在前台。
   guard.fgTransitions evaluated at the window instant: state = foreground
   (run-20261023-114221: timeline +110,...,-31406, window at +19002 ms -> foreground)

2. 整段正向阶段里，每一次前台丢失，拿走前台的进程都不是产品。
   guard.deactivateBy (who was foreground when the guard lost the activation) does not
   name the product; guard.stolenBy (who owned the foreground in any 20 ms sample) does
   not name it either. (run: deactivateBy=powershell, stolenBy=DeepSeek)
```

Part 2 is the provable form of "non-interference": the desktop on this machine is not
quiet (see §9), so "the guard was never disturbed" is false as a statement about the
machine, while "every disturbance came from a process that is not the product" is exactly
the claim the operator made. The counts are printed and are not the verdict.

Both parts are conditional on coverage, and the coverage is asserted, not assumed: the
guard outlives the product and was watching 2 s before and after the window instant; the
window instant is placed on the guard's clock from two clocks whose offset is printed and
bounded (`launch_skew_ms` in [0, 2000]); the foreground timeline is not truncated; and the
instrument proved, in the same run, that it can see and name a foreground change
(`instrument-source=natural`, no arranged hand-off needed).

**Also proves** for the same run: the display mode (width/height/refresh/bpp) did not
change; no **visible** top-level window of the product existed at any 200 ms sample during
the hidden run; the product's own trace agrees (`window_visible=0`); the run wrote a
capture frame and metric rows.

**Does not prove:** non-interference under **exclusive fullscreen (Direct3D exclusive-mode
flip queue)**. The guard is a WinForms borderless window (not topmost -- see the header of
desktop-guard.ps1 for why topmost was removed), not an exclusive swap chain. **NOT
VERIFIED**, and it is never folded into a PASS.

**No longer proves (stated, not silently dropped):** that the product's own window, when
shown, takes the foreground. That is printed as a reading, because whether the window
activates is a product decision.



**Proves (when green, with coverage checked):** during a `--hidden-window --audio-disabled`
run, the product's process never held the foreground, no visible top-level window of it
existed at any 200 ms sample, and the display mode (width/height/refresh/bpp) did not change;
the guard was watching from before the product's window existed until after the process
exited; and the instrument could see and attribute a foreground change in the same run.

**Does not prove:** non-interference under **exclusive fullscreen (Direct3D exclusive-mode
flip queue)**. The guard is a WinForms borderless topmost window, not an exclusive swap
chain. **NOT VERIFIED.**

**No longer proves (stated, not silently dropped):** that the product's own window, when
shown, takes the foreground. That is now printed as a reading, because whether the window
activates is a product decision.

## 6. Mutations, by class

The gate never edits the product. Its mutations are classified so a count cannot be read as
"many product mutations":

| # | class | what it does | reading that has to move |
|---|---|---|---|
| M1 | gate-config (arguments only) | runs the forward phase with the window SHOWN | `watchVisibleSamples` 0 -> 25 |
| M2 | harness-source (guard edited, restored byte-for-byte, `cmp -s`) | blinds the foreground filter | `stolenByCount` -> 0, by-name attribution in the control phase fails |
| M3 | gate-config (arguments only) | the old 16 s guard | `window_created=20399 ms` with `watchVisibleSamples=0` -- the blind window, on demand |
| M4 | not in this gate | `outsideDiff` (pixels outside the HUD region) belongs to quiet-capture-check.sh, a different file owned elsewhere | -- |
| M5 | gate-config (no file touched, no product) | feeds the hud-dispatch leak reader a pre-merge trace and a clean one | leak = 1 and leak = 0 |

M3 exists so that the 32 s is a measured requirement, not a number somebody liked. M5 exists
because the key-dispatch phase's decision is grep-shaped, and a grep-shaped decision has to be
shown to return both answers -- the same rule the Lead applied to the HUD pixel assertion,
applied to an assertion this gate owns.

## 7. Final run

`evidence/t49/run-20261023-114221/` -- `T49-RUN=PASS`

```
head=66c4327   tree-before = tree-after = 9737541ad1aaaba4e80e8026a96563402946f40cbdfe689067aab34b8de0d48a
build rc=0     freshness: no source newer than the exe
gate: assertions_ok=53 failures=0 attribution_proven=1 instrument_source=natural guard_seconds=32
      FULLSCREEN-QUIET=PASS
mutation: FULLSCREEN-QUIET-MUTATION=PASS product_source=0 harness_source=1(restored byte-for-byte, cmp) gate_config=3(files untouched)
leftovers=no   tree-unchanged=yes
```

The readings the verdict rests on, quoted from the log:

```
instrument  setForegroundWindow=True  everForeground=1  handOffOk=1  handOffAttributed=1
            stolenByCount=1 stolenBy=instrument-target        <- natural, one named thief
            attachedThreadsOutstanding=0  hotkeyRegistered=1
baseline    deactivations=0  stolenSamples=123  stolenBy=ChatGPT,explorer,DeepSeek
            watchVisibleSamples=0  modeChangedFields=0
forward     rc=0  stolenSamples=286  watchVisibleSamples=0  window_visible=0  modeChangedFields=0
            timeline +110,-2016,+2047,-5235,...,-31406   (window at +19002 ms -> state foreground)
            retakes attempts=19 succeeded=12   (see section 8: this is the defect)
            deactivateBy=powershell            <- the product is not in it
            launch_skew_ms=731  (bounded [0,2000])
            artefacts: frame 2582406 bytes, metrics 7 rows
keyprobe    dispatch_lines=1 hud.toggle=1 non-hud_dispatch_lines=0 hud_enabled_after=1
            keys: table actions=27 dispatched=1 suppressed=0 last=camera.freeFly
control     rc=0  watchVisibleSamples=29  watchWindowFound=1  handOffOk=1  handOffAttributed=1
            window_visible=1  launch_skew_ms=847
M1  m1 watchVisibleSamples=25                    OK caught
M2  m2 stolenByCount=0 handOffAttributed=0       OK caught (guard restored byte for byte)
M3  m3 guard=16s window_created=20399ms watchVisibleSamples=0   OK caught
M5  m5 leak: pre-merge=1 clean=0                 OK caught
```

### The key-dispatch phase (2b/4), and why it exists

When T51's HUD bindings and T50's camera bindings were merged, the dispatch was "any action the
table returns -> handle it and return 0". F9 is `camera.freeFly`, so it produced
`hud: keys dispatch action=camera.freeFly` and returned: the camera keys were dead while the
trace looked like a dispatch. The fix narrowed the handler to actions starting with `hud.`.
This gate is the only independent evidence for that fix, and it asks the product to do both
things at once through two posted `WM_KEYDOWN`s to its own hidden window: no synthesized
input, no focus change, nothing taken from the operator (see
`evidence/t49/key-dispatch-probe.ps1`). The four assertions are: H dispatched
`hud.toggle`; the HUD state changed (enabled 0 -> 1); no dispatch line names a non-`hud.`
action; and F9 still reached the camera table (`last=camera.freeFly`).

## 8. Known defect: the guard reclaims the foreground outside the measurement window

**What it does.** While it is in the background the guard takes the foreground back, once a
second, for as long as it is in the background (it stops once the foreground has been handed
away). Measured in the passing run:

```
forward retakes: attempts=19 succeeded=12   in 32 s
forward timeline: +110,-2016,+2047,-5235,+5266,-7797,+14860,-16391,+16422,-16672,... ,-31406
```

**Why that is a defect, not an implementation detail.** To the operator a process that pulls the
foreground back within a second is a process they cannot switch away from -- the exact
complaint that the ESC abort and the removal of TopMost were added to answer ("我在黑屏的时候
什么也干不了，也切不出去"). It is invisible in the verdict: the readings it exists for (the
guard being the foreground at the window instant) come out correct.

**Why it was not fixed after the run.** The passing evidence describes the tree that ran. A
change made afterwards would be a change nobody verified, which is worse than a known defect,
and this gate is not being run again tonight.

**The narrowed design that is deliberately NOT applied** (kept as a record, not as code):

```
.scratch/t49/guard-narrowed-retake.ps1    the version written and then put aside
evidence/t49/retake-narrowing.patch       the same change as a diff
```

It allows a reclaim only (a) within 3 s of the startup acquisition, or (b) within 5 s of the
loss it is answering; at most 5 times per run; never after a hand-off. Losses outside those
windows are recorded and not answered (`guard.backgroundOutsideWindowMs`), and
`guard.retakesOutsideWindow` must be 0. With that policy the guard may not be able to hold the
foreground through a 32 s phase on this desktop at all, so part 1 of the claim (§5) could
become unprovable here -- which would be a finding about this machine and must be reported,
not hidden by loosening the assertion.

**What the next change must do:** apply the narrowing, then re-run this gate in full (five
phases + mutation) and read `guard.retakeAttempts`, `guard.retakesOutsideWindow`,
`guard.reclaimsRefusedByWindow`, `guard.backgroundOutsideWindowMs` and the timeline again.
The pass above does not transfer to a narrowed instrument.

## 9. What this run does not prove, and which runs are void

**Not proven here:**

- **exclusive fullscreen / Direct3D exclusive-mode flip queue** -- NOT VERIFIED (see §5);
- that the product leaves the operator's **audio** alone: every phase passes
  `--audio-disabled`, so the run shows the device was not opened
  (`audio_device_opened=0`), not what would happen with audio enabled;
- that the desktop was quiet: it was not, and the gate names who took it
  (`ChatGPT,explorer,SearchHost,DeepSeek,Taskmgr,powershell` across phases). The sample count
  for two phases of the same length differed 8x between two runs (123 vs 943), which is why a
  count comparison between two phases was removed from the verdict after it failed in
  `run-20261023-113547`;
- anything about a **second monitor**, a different refresh rate, or a machine where the
  foreground is refused to a background process (this machine refused a hand-off 8 consecutive
  times in an earlier run; that is reported as a reading, not smoothed away).

**Void runs, kept in the evidence:** `run-20261023-094729`, `run-20261023-103247` and the
ad-hoc "run 3". In their forward phases `guard.everForeground=0`: the guard was never the
foreground, so `deactivations=0` could not have fired, and the conclusion "the product did not
steal the foreground" **could not have been false -- those conclusions are void.**
`run-20261023-113547` is not void: it is the run whose two red assertions exposed the
background-window problem (§5 part 1) and the machine-noise count comparison, and the fixes for
both are what the passing run measures.
