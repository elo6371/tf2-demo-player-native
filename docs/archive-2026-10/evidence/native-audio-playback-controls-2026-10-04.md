# Native audio playback control evidence (2026-10-04)

The native scheduler was checked against the playback loop in
`native/src/main.cpp` and `native/src/audio_player.cpp`. All runtime checks used
`--audio-device 0`; no default-device fallback was used.

## Control transitions

- **Start paused:** `--start-paused` sets the initial state before the window
  loop. The loop does not enter tick advancement while paused; the scheduler is
  stopped and no audio event is opened.
- **Pause:** a transition to paused calls `soundScheduler.stop()`, which stops
  all eight voices. Tick accumulation is not advanced while paused.
- **Resume:** a transition back to playing calls `reset(currentTick)`. This
  makes the next forward advance begin after the current tick, so an event is
  not replayed merely because pause/resume occurred.
- **Speed:** the speed multiplier only scales `tickAccumulator`; events still
  pass through `advance()` in monotonically increasing tick order. No second
  scheduler or device is created for speed changes.
- **Reverse:** changing direction resets the scheduler at the current tick and
  clears the accumulator. While reverse is active the loop intentionally does
  not call `advance()`, so reverse playback cannot play events in the wrong
  temporal direction. Switching forward re-establishes the current tick as the
  lower boundary.

## Runtime checks

| Check | Result |
| --- | --- |
| `--audio-device 0 --start-paused` | Process remained alive during the startup sample; no tick/audio advancement occurred. |
| `--audio-device 0` | Process remained alive during the startup sample with an explicit device id. |
| Invalid device control | Already rejected before window/audio initialization with exit code `13`; no fallback device is opened. |

No scheduler defect was found in this pass. A full user-facing pause/speed/
reverse run with visible title telemetry remains an interactive Windows QA
step, but the state transitions themselves are bounded and deterministic in the
native loop.
