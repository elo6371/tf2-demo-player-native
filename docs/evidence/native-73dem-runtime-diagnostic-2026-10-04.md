# Native 73.dem runtime diagnostic (2026-10-04)

The local sample exists and is readable by the host:

```text
D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\73.dem
106397681 bytes
readable=1
```

After rebuilding the Release target, the native process was launched with the
real TF root, `--demo ...\73.dem`, `--start-paused`, `--audio-device 0`, and
`--no-vsync`. It stayed alive for 20 seconds, and the title reported valid TF2
resource indexing and map material diagnostics, but the demo parser reported:

```text
paused20_alive=1
Demo error: demo file cannot be opened
```

The same title reproduced after a clean Release rebuild. A missing TF root was
also previously observed to stay alive without a crash. Because the demo open
failure occurs before the demo suffix is populated, this run cannot provide
valid `entity=`, `te*`, `assetRefs=`, or `resources=` counters for `73.dem`.
That is a reproducible parser/path defect to investigate separately, not a
justification for changing runtime logic in this smoke pass.

No native process remained after the harness terminated the 20-second paused
run. No crash or leak was observed in this check.
