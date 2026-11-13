# Native weapon sound classification evidence (2026-10-05)

## Change

`SoundEventKind::Weapon` now recognizes the TF2 resource directory token
`weapons/` in addition to existing weapon-name tokens. This matters because
TF2 precache names such as `weapons/stickybomblauncher_det.wav` do not contain
the singular substring `weapon`.

## Verification

Command:

```powershell
cmake --build native/build-mvp --config Release --target audio_scheduler_probe --parallel 2
native/build-mvp/Release/audio_scheduler_probe.exe
```

Observed:

```text
device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3
```

The probe now asserts that the first `weapons/...` event is classified as
`SoundEventKind::Weapon` (exit code 14 on regression). The run used the real
TF2 VPK selected through `TF_ROOT` when present and kept playback injected and
silent; no audio device was opened by the probe.
