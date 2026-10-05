# Native Stability Evidence (2026-10-05)

This record covers the Native MVP only. It does not claim that animation
decoding or advanced material passes are complete.

## Passing checks

The Release build completed with MSBuild and produced all configured probes and
`tf2_demo_native.exe`. `native_install_smoke_probe.exe` reported:

```text
install_executable=1 tf_root_missing_prompt=1 warp_fallback=1 feature_level=b100
```

The audio scheduler probe completed without opening a playback device:

```text
device=0 playbackCalls=0 sinkCalls=1 queuedPcm=1 sinkQueueLimit=8 accepted=8 rejected=1 vpkWav=1 jumpSeen=2 pausedSeen=2 resumeSeen=2 repeatedSeen=2 reverseSeen=0 recoveredSeen=2 missingPlayed=0 wavMalformed=3
```

The installed binary was started with `--start-paused --no-vsync` and a real
TF2 demo for three seconds. It remained alive until the controlled stop.

## Real demo indexing

`73.dem` (106,397,681 bytes) indexed successfully:

```text
header=1 index=1 malformed_offset=0 commands=112179 packets=112175
```

The POV sample `autorecord_2026-05-14_16-09-14.dem` also indexed successfully:

```text
header=1 index=1 malformed_offset=0 commands=125302 packets=58815
```

The local `cp_snakewater_final1` POV sample also completed a full scan:

```text
header=1 index=1 commands=64337 packets=64333 datatable_classes=363
entities=1427501 entity_failures=0 entity_unknown_state_failures=0
entity_prop_index_failures=0 entity_prop_value_failures=0 temp_failures=0
```

Full scans now report zero entity reconstruction failures. `73.dem` reports
`entities=3030444`, `entity_failures=0`, `entity_unknown_state_failures=0`,
and zero property index/value failures. The POV sample reports
`datatable_classes=363`, `entities=1741112`, `entity_failures=0`,
`entity_unknown_state_failures=0`, and zero property index/value failures.
This POV sample has no `svc_ClassInfo`; class bits are taken from the
DataTables server-class list.

## Resource fallback

Map loading now tries the loose `tf/maps/<map>.bsp` path first and then reads
the same normalized path from opened VPK archives. This keeps a missing loose
BSP from silently producing an empty world when the map is packaged in
`pak01_dir.vpk`.

The runtime still uses bounded decoded-audio caching (32 MiB and 512 entries)
and resolves uncached WAV data on demand. Hardware D3D11 creation falls back
to WARP; the smoke probe validates WARP independently without audio output.

The controlled five-second startup sample with the real TF2 root and POV demo
remained responsive, with approximately 106--113 FPS and a stable working set
of about 152 MiB:

```text
1.00285,109,108.69,0,151785472,191737856
2.00287,222,112.998,0,151810048,191737856
3.00303,328,105.983,0,151810048,191737856
```

The repeatable gate `native/tools/native_stability_gate.ps1` was run against
the current Release binary for a three-second silent startup sample:

```text
{"stability":"pass","samples":4,"average_fps":94.3,"peak_working_set_bytes":151670784,"play_mode":false}
```

This is a local smoke result only. The gate accepts `-Play` for a real demo
playback sample and an optional `-MinFps` threshold; it does not replace
long-duration or clean-machine acceptance.

A real playback run against the local TF2 root and
`tf/demos/autorecord_2026-09-30_23-06-57.dem` also passed for twelve seconds:

```text
{"stability":"pass","samples":13,"average_fps":105.68,"peak_working_set_bytes":151650304,"play_mode":true}
```

The same playback path after adding the bounded entity-origin marker pass
passed a five-second run with the current Release build:

```text
{"stability":"pass","samples":6,"average_fps":100.23,"peak_working_set_bytes":154652672,"play_mode":true}
```

The marker pass is a diagnostic world-space cross for up to 128 decoded entity
origins. It proves entity snapshots reach the GPU scene without claiming that
player or weapon StudioMDL meshes are complete.

Entity-state regression on four real samples remains clean after the marker
integration:

```text
73.dem: entities=3030444 entity_failures=0 unknown_state=0 prop_index=0 prop_value=0
POV autorecord_2026-05-14_16-09-14.dem: entities=1741112 entity_failures=0 unknown_state=0 prop_index=0 prop_value=0
autorecord_2026-09-30_23-06-57.dem: entities=1819342 entity_failures=0 unknown_state=0 prop_index=0 prop_value=0
cp_snakewater_final1 SourceTV: entities=1427501 entity_failures=0 unknown_state=0 prop_index=0 prop_value=0
```

A thirty-second real playback long run also passed the same gate:

```text
{"stability":"pass","samples":31,"average_fps":107.31,"peak_working_set_bytes":152055808,"play_mode":true}
```

The short and long runs show no monotonic working-set increase in this local
environment. They still do not prove a clean-machine 120 FPS target.

The resilience gate also passed with the same real Demo: normal indexing was
accepted, a 4 KiB truncated copy was rejected at the header boundary, and an
explicitly missing TF2 root entered safe fallback without a crash.

Projectile and particle diagnostic lines now use the same fallback draw pass
as entity markers, so decoded Demo effects remain visible when BSP geometry or
world textures are unavailable.

The four-sample regression gate passed after these changes:

```text
73.dem: entities=3030444
autorecord_2026-05-14_16-09-14.dem: entities=1741112
autorecord_2026-09-30_23-06-57.dem: entities=1819342
cp_snakewater_final1 SourceTV: entities=1427501
regression=pass entity_failures=0 entity_unknown_state_failures=0
entity_prop_index_failures=0 entity_prop_value_failures=0 temp_failures=0
```

An explicit missing `--tf-root` path was also accepted by the stability gate
without a crash, while remaining in the resource fallback state. Explicit
roots now have strict precedence: a moved or missing operator-supplied path is
not silently replaced by another Steam installation.

Scene uploads are now cached per Demo tick, so paused frames and repeated
renders at one tick do not rescan entity state or rebuild particle buffers. A
post-change thirty-second playback run passed:

```text
{"stability":"pass","samples":30,"average_fps":103.47,"peak_working_set_bytes":151576576,"play_mode":true}
```

## Not yet proven

- Long-duration playback and memory ceiling checks still require a longer run.
- Clean-machine packaged startup and visual frame-rate acceptance require an
  external Windows validation host.
