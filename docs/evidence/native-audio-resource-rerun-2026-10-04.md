# Native sound index and WAV resource rerun (2026-10-04)

The Release `audio_scheduler_probe` was run with the real TF2 `tf` directory.
It opened `tf2_sound_misc_dir.vpk`, resolved
`weapons/stickybomblauncher_det.wav`, parsed its RIFF/WAVE PCM payload, and
accepted it through an injected sink:

```text
device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3
real_exit=0
```

The same probe covers malformed input without touching a device: empty bytes,
a truncated RIFF header, and a data chunk exceeding the input are rejected.
The bounded sink accepts eight events and rejects the ninth, while the
scheduler records one missing/failed playback resource.

The invalid-resource negative control uses a missing TF root and fails while
opening the VPK:

```text
invalid_root_exit=9
```

`playbackCalls=0` is intentional. This verifies sound-index normalization,
VPK lookup, WAV/PCM decoding, malformed-input rejection, and scheduler
accounting only. It is not evidence of audible WinMM playback. PaintKit/VPD
mapping remains `Unknown`/`Unavailable` and is not inferred from sound or VPK
paths.
