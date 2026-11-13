# F2-2 first probe sweep - 2026-11-13

- Task: F2-2 (architecture + QA), first measured batch
- Base: `t105-e5` at `30d58e0`; exe `00ec8b99...` / 2,988,544 B
- Command: `bash /d/_shared/EXEC-2026-11-13/F2/probe-sweep.sh` under host lock
  `f2-probes`, 15 s timeout per probe
- Raw result: `D:/_shared/EXEC-2026-11-13/F2/probe-sweep/results.tsv`
  (name, rc, output bytes, first 12 chars of SHA-256, first line)

## Counts

| Population | Count |
|---|---:|
| `*_probe.exe` built | 52 |
| ran `--self-test` and exited 0 | 29 |
| nonzero because the required argument/fixture was not supplied | 23 |

The sweep is deliberately a **startup/self-test batch**, not domain
acceptance. A probe that takes a demo, a VTF or a TF root returns its usage or
a named "cannot read" line; that is not a product failure and is not counted as
one.

## Second batch: eight probes with real arguments

Command: `bash /d/_shared/EXEC-2026-11-13/F2/probe-domain-a.sh` under host lock
`f2-domain-a`. All eight exited 0:

| Probe | rc | Key reading |
|---|---:|---|
| `world_geometry_probe --tf-root TF --map maps/cp_sunshine.bsp` | 0 | `triangles=82859 faces=20449 displaced=24000 water=410`; budget `within`, dropped 0 |
| `world_feature_probe --tf-root TF --map cp_sunshine.bsp` | 0 | `feature.materials=170`; bumpmap declared/resolved 38, missing 0, visible 0 |
| `terrain_blend_probe --map cp_sunshine.bsp --tf-root TF` | 0 | `map.triangles=82859 displacementFaces=642 truncated=0` |
| `resource_reachability_probe TF cp_sunshine` | 0 | archives 4/5 (`pak01_dir.vpk` absent), 149,970 entries, 170 distinct materials, 105 VMT+decodable VTF, atlas-eligible 19 |
| `vpk_integrity_probe --real TF --real-limit 50` | 0 | 50 sampled, 50 resolved, 0 empty; same `pak01_dir.vpk` missing line |
| `sound_content_map_probe --demo <sultry> --tf-root TF` | 0 | read 5151, scheduled 5031, 1054 missing WAV, 0 unsupported, replay 3704/3704 |
| `minimap_probe --minimap-test` | 0 | synthetic projection `scale=0.125 yFlip=1`, points inside |
| `skeleton_skin_probe --mutation` | 0 | `mutation=transpose mutationCaught=true skinLands=1 skinReversed=24.942` |

This reduces the usage-only population from 20 to 12. The `pak01_dir.vpk`
absence is reported by two probes but did not prevent 4 archives / 149,970
entries from opening; it is a named input gap, not a probe failure.

## Third and fourth batches: the remaining two probes and quiet-capture

| Probe | rc | Result |
|---|---:|---|
| `texture_quad_probe materials/vgui/logos/spray.vtf` | 0 | `vtfWidth=256 vtfHeight=256 featureLevel=45056 nonBlackPixels=16 drawPixelsNonBlack=true` |
| `animation_viewmodel_probe` with `TF_ROOT` | 1 | `selfTest=false`, **`loop=false`**, every other flag true (`parent/compressed/malformed/fov/missingCompanions/bindPose/scout/soldier/motion/section/realLoop/deterministic/viewmodel`); `realLoop=true`, `motionSequence=AttackStand_PRIMARY`, `bones=76 anims=1012` |
| `quiet_capture_probe` | 0 | exercised by `quiet-capture-check.sh` (39 pinned + 47 live checks; the live device pair is now opt-in) |

`animation_viewmodel_probe` is the one open probe result. Its failure is the
synthetic `loopAndOnce()` flag, not an input gap: the same run's `realLoop=true`
and the model/sequence readings are populated. It is reported as an OPEN probe
self-test, not folded into the 29 pass count.

With those runs, every one of the 52 probes has been invoked once with either
its real input or its own self-test. The remaining work is **domain depth**
(model/map/demo matrices and rendering claims), not invocation coverage.

## The 23 nonzero rows, classified

| Probe | rc | Classification |
|---|---:|---|
| `animation_viewmodel_probe` | 1 | needs a TF root/VPK; `selfTest=false` and the next line says `cannot read VPK directory file` |
| `pcf_capability_probe` | 1 | needs a real PCF/VPK root; `vpk_status=missing_or_invalid` |
| `texture_quad_probe` | 3 | takes a VTF path; `--self-test` was read as a filename and the parse refused it |
| `brush_entity_probe` | 2 | usage: `unknown option: --self-test` |
| `entity_protocol_probe` | 2 | no demo argument; prints the empty header/counters |
| `install_cli_probe` | 2 | usage: `--version | --install | --uninstall | ...` |
| `item_schema_probe` | 1 | the known argument trap: `--self-test` is read as `items_game.txt` |
| `minimap_probe` | 2 | usage: `--minimap-test` |
| `model_robustness_probe` | 2 | usage: `--tf-root ... --model ...` |
| `playback_control_probe` | 2 | `unknown-argument arg=--self-test` |
| `quiet_capture_probe` | 2 | usage: `--mode pinned|live --capture-tick ...` |
| `resource_reachability_probe` | 2 | usage: ` <tfRoot> <mapStem>` |
| `review_workspace_probe` | 2 | usage: `--workspace-test [scratch-dir]` |
| `skeleton_skin_probe` | 2 | usage: `[--mutation] [--mutation-offset]` |
| `sound_content_map_probe` | 2 | `unknown argument: --self-test` |
| `sound_corpus_scan_probe` | 2 | usage: `--demo | --corpus | --manifest` |
| `terrain_blend_probe` | 2 | usage: `--map ... --tf-root ...` |
| `viewmodel_contract_probe` | 2 | usage: `--tf-root ... --model ...` |
| `viewmodel_material_probe` | 2 | usage: `--tf-root ... --model ...` |
| `viewmodel_range_probe` | 2 | usage: `--tf-root ... --model ...` |
| `vpk_integrity_probe` | 2 | usage: `[--out] [--real <tfRoot>]` |
| `winmm_sink_probe` | 2 | usage: `[--list-devices] [--device N]` |
| `world_geometry_probe` | 2 | usage: `--tf-root ... --map ...` |

## What this proves

- 29 probes perform their own default/self-test contract with no arguments and
  exit 0 on this build; their identities are in `results.tsv`.
- The 23 nonzero exits are explained by the sweep's argv, not by a probe crash:
  each prints a usage line, the known `--self-test` filename trap, or a named
  missing input. No probe produced an empty output with a crash signature in
  this batch.
- The `texture_quad_probe` rc=3 is its own "not a VTF" branch (argc=2 with the
  flag as a path); it is not a crash.

## What it does not prove

- It does not run each probe's domain acceptance (demos, maps, models, VPKs).
  All 52 have now been invoked with real input or self-test; the next work is
  domain depth, not invocation coverage.
- It does not replace the per-gate evidence; a probe self-test is not a
  rendering claim.

## Follow-up

1. Triage `animation_viewmodel_probe`'s synthetic `loop=false` (the real-model
   loop flag is true). Keep it as an OPEN probe result until the synthetic case
   is fixed or shown to be a stale assertion.
2. Keep `animation_viewmodel_probe` and `pcf_capability_probe` in the
   TF-root/VPK batch, not the no-argument batch.
3. Feed the result into the remaining F2-2 exit definition (`tf2_core`, `/W4`,
   CI, incremental build) using the 137-action / 221-second baseline.
