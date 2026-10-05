# Native WinMM sink smoke (2026-10-04)

Added an opt-in `winmm_sink_probe`. Its default mode is fully silent and does
not call WinMM. `--list-devices` only enumerates device names. An explicit
`--device N` is required before opening a device; the probe writes only a short
zero-filled PCM buffer, then records and performs `waveOutReset`,
`waveOutUnprepareHeader`, and `waveOutClose` cleanup.

Default and invalid-device checks:

```text
mode=dry_run device_opened=0 playback_written=0 reset=skipped close=skipped status=pass
dry_exit=0
mode=explicit device=4294967295 device_count=13 open_attempt=0 device_opened=0 playback_written=0 status=invalid_device
invalid_exit=3
```

The probe now emits raw `MMRESULT` values for `waveOutOpen`, prepare, write,
reset, unprepare, and close. Uncalled cleanup APIs are reported as `-1`; the
silent and invalid-device paths therefore show:

```text
mode=dry_run ... reset_result=-1 unprepare_result=-1 close_result=-1 status=pass
mode=explicit ... open_attempt=0 ... reset_result=-1 unprepare_result=-1 close_result=-1 status=invalid_device
```

When an operator explicitly selects a valid device, the output includes the
actual integer return code for every called API and whether each cleanup call
was made and succeeded. That branch was not run here to avoid opening or
playing through the user's audio hardware.

Device enumeration was read-only:

```text
mode=list_devices device_count=13
device=0 query=0 name=...
...
device=12 query=0 name=...
list_exit=0
```

The invalid ID is rejected before WinMM, so it cannot fall back to
`WAVE_MAPPER`. The explicit valid-device branch is present for an operator who
chooses a real device; it was not run here to avoid opening or playing through
the user's audio hardware. Therefore this run verifies the default-silent and
invalid-device safety paths, while actual `waveOutOpen/write/reset/close`
success and failure behavior remains an external controlled-device test.

The existing real VPK WAV probe still passes without opening a device:

```text
device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3
audio_exit=0
```
