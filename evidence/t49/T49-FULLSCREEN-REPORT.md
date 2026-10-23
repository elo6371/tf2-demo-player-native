# T49 — fullscreen non-interference: what this gate proves, and what it does not

Status of this file: **draft**. The final readings are marked `TBD` until the run listed
at the bottom completes; every other line is a reading that already exists on disk.

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

The gate never edits the product. Its mutations are classified so the count cannot be read as
"many product mutations": `product_source=0`, `harness_source=1` (the guard's foreground
filter, restored byte-for-byte and checked with `cmp -s`), `gate_config=2` (M1 runs the
forward phase with the window shown; M3 runs the old 16 s guard, which reproduces the blind
window on demand).

M3 exists so that the 32 s is a measured requirement: the mutation proves that 16 s cannot
see the window, and requires `watchVisibleSamples=0` with `window_created > 16000` ms.

## 7. Final run

`TBD` — `evidence/t49/run-<timestamp>/locked-run.log`.
