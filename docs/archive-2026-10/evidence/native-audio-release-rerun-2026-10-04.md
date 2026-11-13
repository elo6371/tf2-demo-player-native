# Native audio and release rerun (2026-10-04)

No runtime code change was needed. The existing WinMM cleanup path calls
`waveOutReset`, conditionally calls `waveOutUnprepareHeader`, then calls
`waveOutClose`; cleanup errors are retained in `lastError_`.

Release verification used the real TF2 sound VPK and an injected playback sink,
so the probe did not open WinMM or emit sound:

```text
device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3
audio_probe_exit=0
```

The clean install smoke also passed without a TF2 root or audio device:

```text
install_executable=1 tf_root_missing_prompt=1 warp_fallback=1 feature_level=b100
install_layout=ok tf2_assets_packaged=0
install_probe_exit=0
```

This verifies parsing, scheduler pause/resume boundaries, bounded sink
rejection, install layout, missing-root handling, and WARP fallback. It does
not close end-to-end audible playback or packaged-app desktop acceptance; no
sound was intentionally emitted during this run.
