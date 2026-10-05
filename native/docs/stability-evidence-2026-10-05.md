# Native Stability Evidence (2026-10-05)

This record covers the Native MVP only. It does not claim that PacketEntities
or animation decoding is complete.

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

Full scans expose the existing protocol limitation: `73.dem` reports
`entity_unknown_state_failures=112172` from tick 16350; the POV sample reports
`malformed_packets=3` and the first entity failure at tick 8320. These are
tracked by the entity/protocol work and are not hidden by this stability pass.

## Resource fallback

Map loading now tries the loose `tf/maps/<map>.bsp` path first and then reads
the same normalized path from opened VPK archives. This keeps a missing loose
BSP from silently producing an empty world when the map is packaged in
`pak01_dir.vpk`.

The runtime still uses bounded decoded-audio caching (32 MiB and 512 entries)
and resolves uncached WAV data on demand. Hardware D3D11 creation falls back
to WARP; the smoke probe validates WARP independently without audio output.

## Not yet proven

- A long-running frame-time or memory budget sample is not available from the
  current executable; the 120 Hz loop is implemented but does not emit a
  machine-readable FPS/RSS counter.
- Entity state reconstruction remains blocked by the known real-demo protocol
  failures recorded above.
- Clean-machine packaged startup and visual frame-rate acceptance require an
  external Windows validation host.
