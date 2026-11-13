# Large demo runtime stability (2026-10-04)

After the SHA-256 streaming buffer fix, the local `73.dem` was started with
the explicit TF2 root, `--audio-device 0`, and `--no-vsync`.

```text
--start-paused: alive after 10 seconds
playing:        alive after 8 seconds
```

The process was terminated by the test harness after each observation window;
neither run exited or crashed. The audio device remained explicitly selected
and no default-device playback was requested by the test.
