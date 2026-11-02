# T66 closeout: the seven NV readings, measured or accepted

T66 shipped green on everything it measured and named seven readings NOT VERIFIED
(report §9). This file records the round that closed them: what changed in the
product, what the gate now drives, what the runs read, and what is deliberately
left accepted instead of measured.

Written 2026-11-02 on `task/t66-nv` (branched from `integration/t79-final` at
`660ff40`). Companion files: `T66-NV-MATRIX.md` (per-item reasoning and what
would close each one), `T66-SOURCETV-REPORT.md` §9 (the same seven items in the
report's own words).

## 1. The one real product defect: a stall at high speed read as a seek

`SpectateDirector::update` decided "the user seeked" from the server tick delta
alone: `serverTick - lastServerTick_ > kSeekGapTicks` with the fixed
`kSeekGapTicks = 200`. At 16x a frame that takes long enough produces a delta
over 200, and the relay abandons the handover for a snap. Measured on the T66
round: `resync=2` at 16x, which is why the gate pinned 8x and never tested
above it.

The delta cannot separate a seek from a stall, but the director is already given
a monotonic wall clock (`nowSeconds`) on every update, and the playback speed is
known. A jump is a seek when it is larger than continuous playback at the
current speed could have advanced since the previous update:

```
speed   = playbackSpeed > 0 ? playbackSpeed : 1.0
travel  = kTickRate * speed * frameSeconds * 2.0        // 66 ticks/s, factor 2 = margin
allowed = travel > kSeekFloorTicks ? (int)travel : kSeekFloorTicks
seek    = abs(delta) > allowed
```

```
native/include/spectate_target.h    kSeekGapTicks removed; kSeekFloorTicks = 32,
                                    kTickRate = 66.0; update() takes playbackSpeed
                                    as a REQUIRED parameter (no default: a default
                                    reintroduces the defect silently)
native/src/spectate_target.cpp      the bound above, plus lastNow_/hasLastNow_
native/src/main.cpp:10412-10415     passes g_playback.speed
```

The probe now covers both directions of the new rule (S7b, self-test 49 → 53):

| case | reading |
|---|---|
| 3 s of wall clock at 16x advancing 240 ticks | not a seek |
| 10 ms of wall clock advancing 240 ticks | a seek |
| zero elapsed time, one tick | not a seek |
| 0.5 s of wall clock at 1x advancing 240 ticks | a seek |

## 2. The three readings that became measurements

**16x (report §9 item 1, gate section 11).** `--playback-speed 16
--capture-tick 19700`, same recording. Measured: `resync=0`, `deaths=3`,
`handovers=3`, `notarget=0`, trace 5,493,771 bytes. The 8x section reads the same
3 deaths and 3 handovers, so the high-speed run is playback, not a different
replay.

**A real seek (report §9 item 2, gate section 10).** The gate drives the
product's own transport through `--camera-script`: play to tick 3400, `seek
2500`, run 24 frames, quit. Measured from one trace:
`camera: accept step=3 cmd=seek result=ok detail=tick=2500`;
`playback: request id=3 kind=jump-ticks source=camera-script requestedTick=2500
appliedTick=2500`; `before_tick=13748 before_resync=0`, `after_tick=12820
after_resync=1`, and the whole run's summary reads `resync=1` -- one seek, counted
once, and the first frame published afterwards is `\~930` ticks back at the new
position instead of gliding from the old one.

The fixture had a pause in its first form (`pause`, `frames 2`, `seek`,
`frames 8`) and that form is not deterministic, which the third mutation run
measured rather than guessed: with the pause the handover in flight finishes on the
wall clock before the assert reads it (`before_tr=0`), and the jump sometimes had
not landed before the script quit, so the run read `after_resync=<none>` and the
restored tree failed its own baseline. Both effects are host-load dependent. The
pause is gone, the jump lands while the playback is running, and the same section
was run three times on the restored product for this file:
`before_tick` 13743/13748/13763, `after_tick` 12820 every time, `before_resync=0`,
`after_resync=1`, summary `resync=1` -- rc=0 three times
(`/d/_shared/t66-nv/seekdet1..3.log`).

The section is titled on what it reads, not on the line that implements it. The
clearing of the in-flight handover (`transitionActive_ = false`) cannot be proved
by mutation on this recording: the patch that keeps the handover was applied, built
and run, and the frame after the seek still reads `tr=0` (trace
`evidence/t66-sourcetv/runtime-t66close/m7probe.trace`), because the next frame's
target change ends the transition anyway. That mutant was replaced by one this
fixture can see -- a seek that is abandoned but not counted -- and the residual
equivalence is recorded here rather than papered over.

**A manual pick held across a pause (report §9 item 6, gate section 9).**
`--spectate-target 6 --spectate-pause-at 2500 --spectate-pause-seconds 2`. The
pause tick is not arbitrary: the picked player (6) dies at server tick 13097, so a
pause later than about 2750 would measure a pick the relay has already
legitimately left. Measured: one pause window opened and resumed; inside the
frozen window the relay re-published nothing (the quiet form of holding a pick);
the last line before the freeze reads `target=6 src=manual`; the first change
after the resume carries `deaths=1`, i.e. the change came from a death, not from
the pause. `manual=1 rejected=0 notarget=0` for the run.

## 3. What proves the new sections are load-bearing

Two mutants were added; the suite now runs eight. A mutant earns its place only
if the gate goes red on the named assertion, so each case names the assertion it
must break:

| case | mutant | must redden |
|---|---|---|
| m7 | the seek is abandoned but not counted | `seeks the relay reported after the seek` (section 10) |
| m8 | the seek bound goes back to the fixed 200 ticks | S7b's 16x stall case (section 2) |
| m1..m6 | unchanged from the T66 round | as before |

m7 was retargeted after the first run of this suite came back 7/8: the original
mutant (count the seek, keep the handover) stayed green because the fixture cannot
see it. The replacement was checked the same way the suite checks it, on its own:
`python spectate-patchlib.py apply m7` → build rc=0 → `SPECTATE_SECTIONS=10` →
**rc=1** with `FAIL seeks the relay reported after the seek = 0, expected >= 1`
and `FAIL seeks counted in the whole seek run = 0, expected 1`
(`/d/_shared/t66-nv/m7new-sec10.log`); restored and rebuilt, the same narrow run
reads rc=0 (`/d/_shared/t66-nv/sec10-fixed.log`).

Measured on the final run of the suite (`/d/_shared/t66-nv/mutations4.log`, 16:07-16:36,
`bash spectate-mutations.sh` rc=0). Each row is the assertion that went red, copied
from the log:

| case | mutant | the assertion it reddened |
|---|---|---|
| m1 | the handover window is zero seconds | `FAIL handovers analysed = 0, expected 3` |
| m2 | the corpse rule removes nothing | `FAIL instances the corpse rule removed = 0, expected >= 100` |
| m3 | the pick is not held before the first death | `FAIL handovers that happened before the first death in the manual run = 3, expected 0` |
| m4 | the director handle is invented on a demo that has none | `FAIL replayed deaths = 1, expected 3` |
| m5 | an empty roster jumps the camera to the world origin | `FAIL S4 the camera never falls back to the world origin` |
| m6 | the handover clock is derived from the demo tick | `FAIL handover lines the product printed while the clock was frozen = 1, expected >= 8` |
| m7 | the seek is abandoned but not counted | `FAIL seeks counted in the whole seek run = 0, expected 1` |
| m8 | the seek bound goes back to the fixed 200 ticks | `FAIL S7b a 240-tick step over 3 s at 16x is playback, not a seek` |

```
mutants caught:     8/8
mutants green:      0 (an unasserted clause)
mutants invalid:    0 (did not apply or did not compile)
SPECTATE-MUTATION=PASS
BASELINE-GREEN
```

Every case restored its sources byte-for-byte (`RESTORED mN byte-for-byte`) and
rebuilt before the next one; the final baseline is the full twelve-section gate on
the restored tree: `SPECTATE-CHECK=PASS`, **95 OK / 0 FAIL**
(`evidence/t66-sourcetv/mutations/baseline-gate.log`). 95 and not 96 because the seek
section now carries four assertions instead of five (the pause it no longer has took
two with it, and the tick of the first published line added one); `verify-all.sh`
pins the starved-host floor at 93 for the same reason.

The traces and frames these runs leave in `evidence/t66-sourcetv/` are machine
artifacts -- one suite rewrites 87 MB of traces -- so `.gitignore` now excludes
`evidence/t66-sourcetv/**/*.trace` and `**/*.bmp`, the same rule the repository
already applies to `evidence/weapon-animation/*.trace`. What is committed is the
readings (`*.log`, `*.txt`) and the two reports; the files stay on disk for anyone
re-running the gate in this tree.

## 4. The run that was interrupted, and how it was recovered

An honest note, because it changes nothing in the readings above but it did cost
35 minutes: the first mutation run (14:50) was launched as a background job and
was killed mid-`m3` when a tool call was interrupted at a turn boundary. The
product process died between two trace writes, the lock stayed held with a stale
heartbeat, and the m3 mutant stayed applied to `native/src/spectate_target.cpp`.

Recovery, in this order:

1. `python spectate-patchlib.py restore m3` → `PATCH-RESTORED ... sha=9a32b934567cc59a`,
   `PATCH-STATE case=m3 applied=0` (the sources are back to the round's own diff);
2. `bash build-target.sh spectate_probe` and `bash build-target.sh tf2_demo_native`
   → both rc=0, `error C` count 0 (the binaries were the mutant's build);
3. `bash hostlock.sh break t66-nv-mut --force` → `RELEASED ... by=t66-nv-mut`,
   status `FREE`, `product_processes=0`;
4. the suite relaunched detached (`Start-Process`), so a turn boundary cannot kill
   it again.

Lesson recorded for the gate tooling: a mutation run must not be a child of a
tool call that can be interrupted. The lock's heartbeat (60 s ticker) is the only
way to tell a live holder from a dead one, and it went stale here as designed.

## 5. Commands and return codes

| command | rc | reading |
|---|---|---|
| `bash build-target.sh spectate_probe` | 0 | `error C` = 0 |
| `bash build-target.sh tf2_demo_native` | 0 | `error C` = 0, one pre-existing C4127 (`spectate_target.cpp:303`) |
| `SPECTATE_SECTIONS=2,9,10,11 ... bash spectate-check.sh` | 0 | `SPECTATE-CHECK=PASS` (narrow run, /d/_shared/t66-nv/gate-narrow2.log) |
| `SPECTATE_SECTIONS=10 ... bash spectate-check.sh`, three times | 0, 0, 0 | same reading every time (/d/_shared/t66-nv/seekdet1..3.log) |
| `bash spectate-check.sh` (all 12 sections, final baseline of the suite) | 0 | `SPECTATE-CHECK=PASS`, **95 OK / 0 FAIL**, 16:31-16:36 (`evidence/t66-sourcetv/mutations/baseline-gate.log`) |
| `python spectate-patchlib.py apply m7` + `SPECTATE_SECTIONS=10 bash spectate-check.sh` | 1 | the retargeted m7 is caught (2 FAILs, /d/_shared/t66-nv/m7new-sec10.log) |
| `python spectate-patchlib.py restore m7` + the same narrow run | 0 | the restored product passes section 10 (/d/_shared/t66-nv/sec10-fixed.log) |
| `bash spectate-mutations.sh` | see §3 | eight mutants + a restored baseline |

## 6. What is still not measured, and why that is the answer

Four of the seven are accepted rather than closed, each with its reason on
record in `T66-NV-MATRIX.md`: the demo's own observer target (this corpus has
none that is alive), death causes (the relay reads two fields and never asks
why), attached models (`m_hMyWearables` is not parsed anywhere in the product),
and multi-monitor/full-screen/audio (inherited from T49.1; this harness has one
window on one display). None of them is "unproven, and nobody knows whether it
works" -- each is either outside the product's contract, outside the corpus, or
outside this machine.
