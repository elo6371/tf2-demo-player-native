# Native Demo sound classification attempt (2026-10-06)

## Samples

The requested local samples are present:

```text
D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\73.dem 106397681 bytes
D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\maps\cp_snakewater_final1.bsp 59590420 bytes
D:\TF2_Demo_Player\work\protocol-rescue-20261005\73.dem 106397681 bytes
```

The installed sound archive used by the audio probes is:

```text
D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\tf2_sound_misc_dir.vpk
```

## Current native-mvp baseline result

The available `native-mvp-source` Release `demo_open_probe.exe` was invoked
against the real 73.dem. With an absolute path it returned:

```text
path=<wide-path> header=0 header_error=demo file cannot be opened index=0 index_error= malformed_offset=0 commands=0 packets=0
```

Running the same binary with a relative `demos/73.dem` path from the TF root
produced no output within the 55 second probe window and was terminated. The
same open failure is recorded by the native runtime smoke evidence after a
clean Release rebuild. The snakewater sample has a matching native runtime
limitation: its existing evidence covers map/runtime startup, but no valid
sound counters are emitted before the demo open path completes.

Because the parser does not reach packet scanning, no trustworthy values can be
classified for `decodedSoundEventFailures`, `sound_matches`, or `sound_misses`.
The failure is at demo file opening, before the code can distinguish protocol
bitstream failures from sound-index/VPK misses. Assigning those failures to
either audio category would be unsupported.

## Independent VPK control

The audio-only control remains green against the same TF2 installation:

```text
device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3
exit=0
```

This separates the known-good VPK WAV lookup and silent injected playback path
from the unresolved Demo open failure. A current-branch Demo probe needs to be
repaired or supplied with the correct file-opening path before sound-event
classification can be rerun.
