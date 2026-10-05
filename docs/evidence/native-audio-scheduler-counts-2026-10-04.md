# Native audio scheduler count evidence (2026-10-04)

`audio_scheduler_probe` uses explicit device id `0` but a resolver that always
returns false. It exercises scheduler state and counting without calling
`AudioPlayer::play` or opening an audio device.

Timeline events are at ticks `2`, `5`, `9`, and `9`.

```text
device=0 playbackCalls=0 jumpSeen=2 pausedSeen=2 resumeSeen=2 reverseSeen=0 recoveredSeen=2
```

This proves jump counting, stable counters while paused/stopped, resume without
replaying tick 5, zero reverse-boundary playback, and recovery to forward
playback from an earlier tick with the expected two tick-9 events.

Build/run:

```text
cmake --build native/build --config Release --parallel 2
native/build/Release/audio_scheduler_probe.exe
```
