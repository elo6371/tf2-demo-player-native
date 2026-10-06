# Presentation state (module 3) 2026-10-05

Local work only. Do not treat this file as a GitHub browse target.

## What changed

- Demo packet command info is the 76-byte block in front of a signon/packet payload. The reader keeps view origin and view angles. `svc_FixAngle` now decodes three 16-bit angles (`raw * 360 / 65536`) instead of skipping them. A relative fixangle adds to the previous decoded angle.
- `demoViewForward` uses Source's pitch sign: positive pitch looks down. Yaw 0 looks along +X. The renderer can place the camera on that view when a sample has both origin and angles. Dragging the orbit camera turns that lock off until the next tick.
- Playback asks for entities, projectiles, and the view sample at `first demo packet tick + playback tick`. Sound events stay on the 0-based playback clock.
- SourceTV is the header server-name match or the first flag bit after the server count in `svc_ServerInfo`. The bit after the host name is stored as `serverInfoReplayBit` and is not treated as the HLTV flag. Before a serverinfo message is parsed, a non-empty client name stays `POV (heuristic)`.
- `CTEClientProjectile` maps `m_vecOrigin`, `m_vecVelocity`, model index, owner, and lifetime onto the projectile timeline. A non-finite origin stays unmarked. The older bullet, particle, and explosion fields are unchanged.
- Entity history still keeps an exact event window. When that window rolls forward, its checkpoints are archived (at most 96 after thinning). A tick inside the live window is `available`. A tick covered only by an archived checkpoint is `checkpoint`: the state is the snapshot, not every event between snapshots. A decode gap is still `gap`. A tick before the first snapshot is `before-history`.

## Probe

```
native/build-mingw/presentation_probe.exe
```

Observed with llvm-mingw on 2026-10-05. Exit code 0.

```
selfTest=true viewMath=true recording=true projectileFields=true fullSpanCheckpoint=true
```

The view check covers bit angles 0/90/180, forward vectors for yaw 0, yaw 90, and pitch 90, a 76-byte command-info round trip, and sample lookup. The seek check forces a tiny history limit, then reads an early tick as `checkpoint` and the newest tick as `available`.

## Not claimed

- No real Demo was scanned for this module. The HLTV bit is the flag the existing serverinfo reader already consumed; it was not checked against a SourceTV packet capture.
- The D3D camera was not executed. This machine has no Visual Studio. mingw still stops in `native_renderer.cpp` on `XMVECTOR` / `XMMatrix*`.
- Checkpoint seek is not a per-tick replay of the whole Demo. Ticks between archived snapshots reuse the previous snapshot.
- Water, cubemap sampling, animation, and ViewModel merge are unchanged.
