# F1 — projectile sample retention made opt-in (2026-10-30)

## The defect this closes

Opening a 70 MB corpus demo made the product — and every probe that scans it —
stall for 15+ minutes:

- `native/src/main.cpp:4167-4173` calls `scanKnownDemoMessages` unconditionally for
  `--demo`, so the product pays for the scan on every demo open.
- `native/src/demo_header.cpp:2867` `appendProjectileTrackSamples` ran for **every**
  packet, and the container is bounded by `ProjectileTrackLimits::maxSamples = 400000`
  (`native/include/demo_header.h:392`).
- Once the bound is reached, `native/src/demo_header.cpp:2976-2982` does
  `erase(begin())` before every push. `ProjectileTrackSample` is ~56 bytes, so each
  append memmoves ~22.4 MB.
- On `autorecord_2026-07-23_12-57-17.dem` the scan produces **722,891** rows, i.e.
  322,891 appends past the bound ≈ 7 TB of copying.

Measured before the fix (same demo, `_shared/t6-diag` run 2026-10-29):

| case | wall | rc | rows / dropped |
| --- | --- | --- | --- |
| maxSamples=400000 | 900,093 ms (killed) | 137 | — |
| maxSamples=1 | 100,028 ms | 0 | rows=1 dropped=722,890 |
| maxSamples=4,000,000 | 66,819 ms | 0 | rows=722,891 dropped=0 |

The bound, not the scan, was the cost: with no bound the identical scan finishes in
66.8 s. Only `projectile_track_probe` has ever read those rows; the product and
`entity_protocol_probe` scan demos and never look at them.

## The fix (commit 50fdc23)

- `native/include/demo_header.h`: `ProjectileTrackLimits` gains `bool enabled = false;`.
- `native/src/demo_header.cpp`: `appendProjectileTrackSamples` returns immediately when
  disabled, before the counter — nothing is reserved, nothing appended.
- `native/tools/projectile_track_probe.cpp`: the only consumer explicitly enables it
  (`--no-retention` runs the same scan with the default, for the gate below).
- `trajectory-overlay-check.sh`: new case "2b/4 retention is opt-in" requires
  `retention: enabled=false capacity=0 rows=0` **and** a non-zero packet count, so the
  default is exercised by a real demo rather than assumed.

The upper bound was not raised, and no other consumer's semantics changed.

## Measurements after the fix

Host lock owner `f1-verify`, run directory `_shared/t6-f1/run-20261030-000020`,
candidate HEAD `50fdc23`, sources clean and byte-identical before and after
(`pre.txt`/`post.txt`).

| check | result |
| --- | --- |
| build (tf2_demo_native, entity_protocol_probe, projectile_track_probe, entity_overlay_probe) | BUILD_RC=0 |
| product on 07-23 (73,690,064 B), `--capture-tick 1 --playback-speed 8 --capture-frame` | **rc=0, 72 s, bmp=2,582,406 B** (before: rc=137 at the 300 s kill, no bmp) |
| `entity_protocol_probe --summary` on 07-23 | **rc=0, 61 s** (before: killed at 900 s) |
| `bash trajectory-overlay-check.sh` | rc=0, 88 s, `TRAJECTORY-OVERLAY=PASS` (42 OK / 0 FAIL) |
| `bash trajectory-overlay-check.sh --mutation` | rc=0, 140 s, `MUTATION-CAUGHT` (META-CATCH 1 → 2) |

The T53 readings are unchanged on the gate demo: `retention: enabled=true
capacity=400000 rows=257880` and R1–R13 all OK, so the probe that does consume the
rows still sees exactly what it saw before. The new default-off case on the same
demo reports `retention: enabled=false capacity=0 rows=0` with `scan: packets=73121`,
i.e. the scan still decoded the demo.

Product proof frame: `big-07-23.bmp` (2,582,406 B, same size as the 73.dem control
frame that always rendered).

## Hygiene

- Sources restored/unchanged; `git status` clean before commit.
- No product processes left; host lock FREE after the run.
- Binaries: tf2_demo_native 2,657,792 B `e82e139b…`, entity_protocol_probe 551,936 B
  `5ee70fe8…`, projectile_track_probe 566,784 B `09880fc6…`, entity_overlay_probe
  591,360 B `0bf67200…` (see `exes.txt`).

## Not claimed

This closes the stall only. T73 remains NOT READY: T49.1's desktop behaviour is still
blocked, the eight product NOT-VERIFIED defects are untouched, and T6 has not been
re-run. Nothing here is a substitute for the single final full-chain run.
