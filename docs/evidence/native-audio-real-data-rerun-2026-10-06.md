# Native audio real-data rerun (2026-10-06)

## VPK WAV and silent playback path

The current branch `audio_scheduler_probe` was built and run against the real
TF2 sound archive. `TF_ROOT` pointed to `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf`.

```text
cmake --build native/build-audio-probe --config Release --target audio_scheduler_probe --parallel 2
set TF_ROOT=D:\SteamLibrary\steamapps\common\Team Fortress 2\tf
native/build-audio-probe/Release/audio_scheduler_probe.exe
```

Observed output:

```text
device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3
exit=0
```

This confirms that the real `tf2_sound_misc_dir.vpk` contains and resolves
`weapons/stickybomblauncher_det.wav`, that its RIFF/WAVE PCM payload reaches the
injected sink, and that no WinMM device is opened (`playbackCalls=0`). The same
run covers malformed WAV rejection and scheduler boundary accounting.

## Real Demo scan limitation

The current checkout's `demo_open_probe` does not build: it references four
`DemoNetworkSummary` fields that are absent from the current header:

```text
packetEntityDeltaCount
packetEntityDeltaBaseUnavailableCount
firstPacketEntitiesDeltaTick
firstPacketEntitiesDeltaFrom
```

The available older Release binary was also tested against the installed
`32snake.dem` (80,956,958 bytes) and returned `header=0 header_error=demo file
cannot be opened`. Therefore this rerun does not claim a real Demo sound-event
count; a current-branch Demo probe must first be repaired against the present
summary interface in a task allowed to touch the demo probe.
