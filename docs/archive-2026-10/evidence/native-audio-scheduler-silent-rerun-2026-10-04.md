# Native audio scheduler silent rerun (2026-10-04)

This rerun uses the existing offline scheduler probe. It sets an explicit
device id (`0`) but its resolver always fails, so `AudioPlayer::play` is never
called and no default or explicit audio device is opened.

```text
device=0 playbackCalls=0 jumpSeen=2 pausedSeen=2 resumeSeen=2 reverseSeen=0 recoveredSeen=2
```

The jump, pause, resume, reverse-boundary, and forward-recovery counts match
the previous acceptance. No scheduler defect was observed.

Validation:

```text
cmake --build native/build --config Release --parallel 2  # passed
git diff --check                                      # passed
```
