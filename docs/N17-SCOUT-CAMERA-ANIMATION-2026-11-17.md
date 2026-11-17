# N17 Scout, Camera, and Animation Findings - 2026-11-17

## 2026-11-18 follow-up (latest identity and acceptance boundary)

Final product: `f341ab1ea5cb9296c4685957ec89ed5d50891d107dae1e9841dc82b187010d15`, 3,185,664 B; probe: `09f6565dc153d6604df870643b79fe94f3661c8a2fa9b87c91f8980321f2ad37`. HEAD is still `23052154611eaca44d06a93e6cedf474bc340e3a`, changes uncommitted. The build sidecar records `sourceTree=23052154611e`, dirty=1, dirtyFiles=29 at build time; it is not a clean/release identity. The older hashes below are historical evidence.

Four locked captures used the same demo, playback cursor 1, eye `(500,-2240,240)`, pitch `17.8`, yaw `127.97`, 1264x681 client viewport; all exited 0. The earlier low camera was obscured by a railing and is not a valid full-body comparison. Final local outputs: `.scratch/n17-scout-final/`.

| Scout ent=14 / color ID=91 / team=2 | Default | Experimental `--player-animation --animation-drive states` |
|---|---|---|
| Input / sequence | no recorded `m_nSequence`, bind pose | inferred idle, `stand_PRIMARY` 175 |
| Sample | no usable frame, `cycle=-1` sentinel | `cycle=0`, `frame=0/45`, `localFrame=0` |
| Matrices / skinning | `skinBones=0 validBones=78`, disabled | `skinBones=78 validBones=78`, enabled |
| Visible ID-91 pixels / bbox | 4530 / `(610,258)-(658,420)` | 4164 / `(598,264)-(698,394)` |

Playback cursor 1 has `entryTick=2`, `serverTick=11058`, while the scene snapshot used for this pose resolves at 11057. Exact ledger evidence: `identity.trace:708`, `animated-id.trace:745`; capture completion `identity.trace:867`, `animated-id.trace:906`. Both rows use `models/player/scout.mdl` at `(365.7,-2068.3,129.4)`. This proves the same Scout is drawn with different poses; it does not prove either pose matches TF2. Idle's speed-driven clock holds its cycle while stationary; this sample is its first frame, not a reconstructed TF2 idle animation.

New corrections in this continuation:

1. The companion merge copied `animDesc` but left the base model's stub `blendCount=0`. It now copies the matched companion's count: Scout `stand_PRIMARY=1`, `run_PRIMARY=9`, `PRIMARY_aimmatrix_idle=12`. Probe output is in `scout-sequences.txt`. Other base-table properties are still base-table properties; this is not a full Source virtual-sequence implementation.
2. Helper matrices were already generated but omitted from `skinBones`. They now count as written; `acceptedChannels` separately preserves refusal when no valid direct animation channel exists. The helper self-test also checks both matrices are reported; bind/translation/rotation all pass. Earlier `76/78` was a counter defect, not proof that two matrices were absent.
3. The capture entity ledger now prints cycle/frame/localFrame/sectionFrames/skinBones/validBones from the same sample that feeds the draw. This closes the former gap between aggregate animation telemetry and the subject's frame.
4. `class-pose-family-check.sh` now defaults to `.scratch/t62-matrix/pose-families`. Its first run rewrote 50 tracked outputs and lockrun rejected it; those changes were restored. The safe rerun exited 0 under lock: nine classes, 40 sampled/decoded clips, 14 flinch sequences, failed=0. No tracked evidence remained dirty. This tests selected animation data, not gameplay pose selection or rendered equivalence.

Final BMP SHA-256 values:

| Mode | SHA-256 |
|---|---|
| Default ID | `6b848caf8dd6fe444f51f4ac888d06200eccbe9b151156a96d41ddff2b210125` |
| Default normal | `c0449fecc3e6d90c638f4adf67d360fd95da91ec61ff479d1a601dec7b7ec03d` |
| Animated normal | `9309b3d21215b3029a986e3c441d6d05cac0ef53802394f6712821a97a28ac5b` |
| Animated ID | `c57889fbcd401f427d1931c87af60e9e2f2c4d852c5d80609d5a712d079a0910` |

These hashes match the corresponding frames before the telemetry-only changes; no pixels changed in these four captures. Capture runners: `.scratch/n17-start-20261117/{scout-centered.sh,scout-animated-id.sh,verify-final.sh}`. The default camera/UI fixture now sets world FOV independently from ViewModel FOV; `CAMERA-UI=PASS` and `MAP-OVERVIEW=PASS (231 checks)` were obtained on earlier `666a1ac2…`, logged in `.scratch/n17-start-20261117/{camera-ui-world-fov.log,overview-current.log}`. They are historical runs, not whole-gate acceptance of final `f341ab1e…`.

**Still OPEN:** default bind-pose fallback, stationary idle progression, TF2 pose parameters and multi-blend sampling (the sampler still selects the first anim descriptor), activity/gesture layers, model/LOD/UV/material equivalence, old normalized bookmark migration, multi-pane/ViewModel integration, final-candidate camera/UI/overview and complete release gates. Implement deterministic idle/pose/blend behavior against the pinned TF2 source and matched in-game frames, with negative controls, before enabling experimental player animation by default. Scout visual acceptance remains OPEN; measured FOV/scale defects do not establish that all remaining appearance differences are camera errors.

## Current result

Four defects have been corrected in the current working tree:

1. World projection no longer reuses the ViewModel vertical FOV. `worldFov` is stored independently as Source's horizontal FOV at 4:3, converted to the vertical FOV expected by the DirectX projection, then projected using the active pane aspect ratio. The default 90 degrees converts to 73.7398 degrees vertically at 4:3.
2. Generic idle selection no longer accepts `AttackStand_*` by substring. It uses the existing whole-token and primary-token preference logic, selecting Scout `stand_PRIMARY` sequence 175 instead of `AttackStand_PRIMARY` sequence 9.
3. Render bones with no animation channel now publish their already-computed parent-relative current transform to the skinning buffer. Scout's `hlp_forearm_L/R` have no direct channel but are referenced by 271/253 entries of the de-indexed LOD0 draw list (524 entries total, not a count of unique source vertices). Previously these entries used identity skin matrices while their parent and neighboring vertices moved.

4. World normalization now uses one XYZ scale. On `pl_upward_f12`, the old Z scale was 1.351815 times XY; it changed 10.94-degree Source pitch into 14.643825 degrees in box space. Map uploads, entities, overlays, camera conversion/inversion, fog, debug geometry and CPU projection were corrected together.

These fixes remove measured projection, selection and helper-skinning defects. They do not establish that Scout now matches TF2. The Scout visual acceptance remains OPEN.

## TF2 source reference

The TF2-specific reference is [OthmanAba/TeamFortress2](https://github.com/OthmanAba/TeamFortress2), revision `1b81dded673d49adebf4d0958e52236ecc28a956`.

- `tf2_src/game/shared/tf/tf_playeranimstate.cpp`, `CTFPlayerAnimState::CalcMainActivity` and `TranslateActivity`: standing idle and locomotion are distinct activities; attack and gesture overlays are translated separately.
- `tf2_src/game/client/tf/c_tf_player.cpp`, `C_TFPlayer::UpdateClientSideAnimation`: updates the TF player animation state with eye yaw/pitch.
- `tf2_src/game/client/view.cpp`, `ScaleFOVByWidthRatio`: adapts FOV for viewport width ratio relative to 4:3.

The source helped identify the sequence-selection error and the correct FOV contract. Its constants and behavior remain a reference; the installed assets and matched runtime samples are the acceptance oracle. No external source was copied into the product. The official Valve SDK also contains TF2-specific code; the supplied document's contrary statement is outdated. Its exact checked revision and repository/task-domain reference procedure are in [TF2-SOURCE-REFERENCE-INDEX.md](TF2-SOURCE-REFERENCE-INDEX.md).

## Runtime and build evidence

- Current branch: `work/integrate`, base HEAD `23052154611eaca44d06a93e6cedf474bc340e3a`; changes are uncommitted.
- Targeted build under `tools/lockrun.sh` completed for `model_pose_probe` and `tf2_demo_native`.
- Product SHA-256 before helper fix: `6507f26b4e02c462421ebd633d5336a73dfcb86c8a52335018166416f38afc7b`; helper-only checkpoint: `a917b76b00bdcb33fb811a2ca379dc4d4bd21fb37892ef16ca0c5b5bc8255748`; current uniform-world product: `666a1ac2c2e8406f0cfecb9336702c4381405a66b8bb2b144b56557813604ee2`.
- `model_pose_probe --matcher-selftest`: `genericIdle=1 oneShot=1`; nine class probes select generic `stand_*` idle sequences. Scout selects 175; Spy's valid secondary idle remains `stand_SECONDARY`.
- `camera_probe` passes, including 90 -> 73.7398 degrees, 75 -> 59.8404 degrees, and invalid FOV rejection.
- Paired playback-cursor-1 captures were made with animation disabled, animation enabled, and entity-ledger tracing. All exited 0. The input demo is `pl_upward_f12` STV, SHA-256 `7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3`.
- Reproducible capture material is under ignored `.scratch/n17-start-20261117/`; this is local investigation evidence, not a checked-in acceptance package.
- Helper regression fixture: a weighted child without a channel has bind-space point `(1,0,0)`. Parent translation `(10,0,0)` must yield `(11,0,0)`; adding a 90-degree Z rotation must yield `(10,1,0)`. `--helper-skin-selftest` reports `bindControl=1 translation=1 rotation=1`. Temporarily restoring the former identity behavior gives `bindControl=1 translation=0 rotation=0`, exit 1; the verified implementation was then restored and rebuilt.
- Helper-only locked captures are under `.scratch/n17-final-20261117/`: both exit 0. Animated BMP SHA-256 is `053a7ea76ea8f0cad41961645fc34dfaa4ca7eaf7a9994ca17c574bdd0109e84`; identity BMP is `acedf542479493dc8d4817a1bd4a4c7c23f12517160ff01686cf5fdaf0ed98b7`. These are historical helper-checkpoint frames, not current-uniform-world evidence.
- Model-loader selftests and `git diff --check` pass. The new CLI audit is `model_pose_probe --player-animation-test --tf-root <tf> --model models/player/scout.mdl --dump-bone-usage`.
- Current-executable evidence: `.scratch/n17-uniform-sky/` and `.scratch/n17-uniform-fog/`; run logs: `.scratch/n17-start-20261117/uniform-{sky,fog}.log`. Sky: **17 passed / 0 failed**. Fog: **PASS (58 assertions)**; fog-on/off differ in 261,804 pixels (30.415%).
- `python tools/n17_camera_metric.py .scratch/n17-uniform-sky/positive.trace` passes using the actual uploaded MVP: equal 10-unit perpendicular reference segments project to **22.699882 / 22.699903 px**, independent expectation **22.700000 px**, center residual **0.053623 px**. The camera trace is rounded, so tolerance is 0.25 px. `--selftest` accepts a metric fixture and rejects a 1.351815 vertical-stretch mutant. Camera readings are bound to the matrix upload, not a later playback frame. This verifies matrix/metric consistency, not GPU vertex readback or TF2 visual equivalence.
- Additional uniform-world animated/entity-ledger captures: `.scratch/n17-uniform-20261117/`, both rc=0, intermediate exe `56972c13...`. The current exe differs by corrected comments embedded in the HLSL string; current identity is certified only by the sky/fog evidence above.

## Tick-domain correction

`--capture-tick` selects the **playback cursor**, not the server tick. Do not convert it by adding a fixed `11056` and call that a universal server-tick mapping. In the captured run, playback cursor 0 had `entryTick=1`, `serverTick=11057`; cursor 1 had `entryTick=2`, `serverTick=11058`, while the entity snapshot used for the scene was resolved at 11057. The mapping depends on demo entry/snapshot state and must be read from that run's trace.

The cursor-1 entity ledger identifies the nearby red Scout as entity 14, model `models/player/scout.mdl`, sequence 175 `stand_PRIMARY` in the animation-enabled capture. Natural mode leaves player animation disabled and therefore retains bind pose. This is evidence about the product's two paths, not a same-camera TF2 comparison.

## What remains open

- The supplied `analysis/integrated-up0.png` remains visually suspicious. The earlier screen-bound check found zero off-screen pixels, so clipping is not an established cause.
- `playerAnimationEnabled` remains false by default. The optional `--player-animation --animation-drive states` path infers activity from observed state; it is not yet a faithful implementation of TF2's complete activity, gesture, pose-parameter, and layer system. Do not enable it by default based on pose changes alone.
- The product's prior playback-cursor-20000 large foreground subject was Heavy entity 11, not Scout. Do not use that capture to diagnose Scout.
- Scout animation metadata reports 76 animation bones against 78 render bones, including helper bones. The weighted helper identity defect is fixed and regression-tested, but full TF2 procedural helper behavior is not implemented by this change. Existing `skinBones`/`boneMismatch` telemetry still counts accepted animation channels; do not interpret its aggregate as proof that no helper skin matrix was written.
- Uniform scale and inherited helper skinning are tested. Complete TF2 procedural helpers, activity/gesture/pose/layers and mesh/UV/material equivalence remain unverified. Default playback still retains bind pose when no usable recorded pose is available.
- Saved presets/bookmarks store normalized focus coordinates: existing framing and old map pixel pins need revalidation after this deliberate coordinate/FOV correction. No automatic migration or blanket re-pin was done. Full camera/UI/overview/split-view, seven-map and release-chain acceptance has not been performed on this binary.

## Next steps

1. Weighted-bone audit, inherited-helper regression, metric projection, sky and fog tests are done. Next collect selected LOD/bodygroup/skin and per-mesh material/index ranges, and audit helper use for other classes. A plausible-looking image is not TF2 visual equivalence.
2. Bind each candidate to playback cursor, entry/server/resolved scene tick, entity ID, model path, origin/angles, sequence/cycle, camera mode/target/eye/FOV, viewport and executable identity. Do not compare different entities or tick domains.
3. Use OthmanAba's activity/overlay flow to map only activities actually observable in the demo. Keep inferred state separate from recorded state and add positive/negative sequence-selection controls.
4. If geometry and pose data are coherent, compare the same Scout asset and camera in TF2. Then isolate material/UV and camera factors one at a time with paired captures. Change product rendering only after a measured mismatch names the responsible layer.
5. Rebuild and rerun only affected model/camera gates after each fix; complete full integration gates when the current tree is ready for integration acceptance.
6. Revalidate old normalized camera bookmarks/presets, split-view camera isolation, ViewModel alignment, HUD/overview and launch-map pins. Distinguish the intended metric/FOV correction from independent geometry/material defects before considering new pins.

