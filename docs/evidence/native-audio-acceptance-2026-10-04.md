# Native audio acceptance (2026-10-04)

The checks below were run against the Release binary with an explicit
`--audio-device` argument. No check used the WinMM default mapper.

| Case | Result |
| --- | --- |
| `--audio-device 0 --start-paused` | Process remained alive during the startup sample; playback state stayed paused and the scheduler did not advance audio. |
| `--audio-device 0` | Process remained alive during the startup sample; device selection stayed explicit (`0`). |
| `--audio-device 9999 --start-paused` | Rejected before window/audio initialization with exit code `13`; a message box states that the default device will not be used. |
| `--audio-device nope` or missing value | Rejected with the same exit code and message. |

The invalid-device rejection is deliberate: passing an invalid WinMM id to the
playback layer would otherwise defer failure until the first sound event and
would not provide a clear operator-facing result. `WAVE_MAPPER` remains the
internal default only when the user did not request `--audio-device`.

The native path remains `svc_sounds -> soundprecache -> WAV PCM -> bounded
AudioEventScheduler`; no Web Audio or Electron code is involved.
