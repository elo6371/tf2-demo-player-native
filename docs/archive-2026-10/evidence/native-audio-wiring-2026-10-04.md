# Native audio wiring evidence (2026-10-04)

## Scope

This note records the native Windows path only. The retired Web/Electron audio
implementation is not part of this path and was not copied or used.

## Verified connection

The native pipeline has a complete diagnostic-level connection from a recorded
Demo to a WAV playback request:

1. `native/src/demo_header.cpp::readSounds` consumes wire message 17 using the
   protocol-specific delta fields and records tick plus sound index/position.
2. `soundprecache` string-table creation and updates populate
   `DemoNetworkSummary::soundPrecache`; the final pass resolves each decoded
   sound index to its resource name and counts misses.
3. `native/src/main.cpp` converts resolved non-voice/non-music events into
   `SoundEventTimeline`, sorts by tick, and advances `AudioEventScheduler` with
   playback ticks.
4. The resolver searches the opened TF2 VPK archives through
   `readSoundResource`; WAV RIFF chunks are parsed and only valid PCM is sent to
   `AudioPlayer`.
5. `AudioEventScheduler` uses eight bounded voices, a 512-entry/32 MiB PCM
   cache, explicit `--audio-device N` selection, and `--start-paused` for silent
   diagnostics. No test or startup path falls back to the default output device
   when an explicit invalid device is supplied.

## Boundary and remaining gaps

- The native demo reader currently proves the sound/resource/playback path, but
  it does not claim a full player UI or export audio integration.
- MP3 resources are signature-validated but are not decoded by the current
  playback backend; they are counted as missing/unsupported rather than sent to
  WinMM as if they were PCM.
- Pause, reverse playback, and speed changes reset or stop the scheduler; a
  user-facing audio-sync acceptance run is still required on a real Windows
  output device.
- This evidence does not authorize bringing the old Web Audio code into the
  native target.

## Build evidence

Configured with:

```text
cmake --preset windows-release -S native
```

The Release build command is the required follow-up:

```text
cmake --build native/build --config Release --parallel 2
```
