# T56 runtime proposal (for the Lead to schedule serially)

Everything here is a PROPOSAL. Nothing in this file has been run: no build, no launch, no capture, no lock. All commands are cited to the gate script line that proves the recipe.

## BLOCKED — do not run (Lead, 2026-10-24, message m00561)

The Lead confirmed a product tick-origin defect: entity history is indexed by the server `net_Tick`, but scene queries use the signon entry tick (measured offsets: snakewater 67 ticks, bagel 56146 ticks). Until the fix lands, a camera capture can photograph the wrong tick, so the runtime phase is paused pending the Lead's "go".

Impact on this proposal:

- **A (snakewater, --start-paused --capture-tick 0)**: tick 0 is the signon entry tick itself, so at that one tick both index origins coincide — but that is a hypothesis to verify after the fix, not an assumption. Verification: trace must show `capture: armed requested=0 frame_tick=0`, and the entity-history tick and the scene tick must agree (record both).
- **C (cp_sunshine, tick 0)**: same shape and same hypothesis as A; recipe itself still unproven.
- **B (odyssey, --capture-tick 2000)**: exactly the class of non-zero-tick capture the defect can corrupt. Existing odyssey tick-2000 images (evidence/world-material-face/, evidence/visual-baseline/) were produced by a build carrying the defect and must be treated as "tick unverified" until re-run after the fix — not as ground truth for the ledger's delta_vs_reference.
- Static numbers in this directory are unaffected: the defect changes which frame a runtime capture shows, not what is on disk.

## Preconditions

- Binaries already exist (hashed in input_hashes.sha256): native/build-nmake/tf2_demo_native.exe sha256 f454718b..., world_geometry_probe.exe 81da0115..., world_material_probe.exe 697f67b2..., material_coverage_probe.exe 78bf8205..., lighting_semantics_probe.exe eebee5a0... . A forward runtime phase needs **no rebuild**.
- The product opens a window/GPU and reads the TF install: **host lock required** for every run; all runs serial.
- TF root: D:/SteamLibrary/steamapps/common/Team Fortress 2/tf (gate scripts use the same via TF variable).

## A. cp_snakewater_final1 — SourceTV (testdata demo)

Proven recipe: `--start-paused` + `--capture-tick 0`.
- material-coverage-check.sh:217-219 records that --start-paused holds a POV demo on tick 0 and that --capture-tick 2000 then waits forever (measured rc=124 after 400 s); :226-231 builds the command with `pause_args=(--start-paused)`; :80 sets TICK=0.
- world-material-check.sh:316-317 runs the snakewater capture at `--capture-tick 0` with `--world-sky` (no --start-paused there because that path drives the sky probe).

Proposed forward capture (no mutation flags):

```
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/snakewater-t0.trace")" \
timeout -k 5 300 native/build-nmake/tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --audio-device default --audio-disabled --hidden-window \
  --demo "D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem" \
  --start-paused --capture-tick 0 \
  --capture-frame "$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/snakewater-t0.bmp")" \
  --metrics-file "$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/snakewater-t0.csv")"
```

- Expected duration: 60-120 s per capture (bounded by timeout 300). Basis: comparable odyssey captures measured 42.6-59.9 s from trace timestamps (evidence/world-material-face/trace-albedo.txt first 0.0 -> last 42578.9 ms; evidence/visual-baseline/trace-ref.txt 59.9 s); snakewater is smaller (BSP 59.6 MB vs 131.2 MB).
- Lock: yes.
- Identity check after the run: `map: lookup map=cp_snakewater_final1 ... reason=found` and `capture: armed requested=0 frame_tick=0` in the trace (the validator in face_trace_to_ledger.py enforces exactly this).

## B. pl_odyssey — POV

### B0. Sample selection (Lead, 2026-10-24, message m00574) — run this BEFORE the capture

The Lead's T64 oracle measured that POV demo `autorecord_2026-05-17_11-57-32.dem` is a 6.555 s connection fragment (437 ticks, 429 frames, 1,256,764 B) with no combat/round events, so it must not be the payload reference sample. Verified against demo_headers_tfdemos.jsonl: it exists exactly as described and is **not** the sample this proposal used.

Selection rule (from the Lead): among demos with map=pl_odyssey, sort by header playbackSec descending, take the top 5, list path/size/ticks, choose the longest as B's sample.

`evidence/t56-map-audit/odyssey_demo_candidates.tsv` (42 pl_odyssey POV demos in tf/demos):

| rank | demo | bytes | ticks | playbackSec | frames |
|---|---|---|---|---|---|
| 1 | tf/demos/autorecord_2026-08-09_14-48-38.dem | 82,121,937 | 147,729 | 2215.935 | 145,678 |
| 2 | tf/demos/autorecord_2026-09-03_22-39-27.dem | 55,557,466 | 98,878 | 1483.170 | 97,448 |
| 3 | tf/demos/autorecord_2026-07-04_13-24-22.dem | 37,632,649 | 73,796 | 1106.940 | 72,711 |
| 4 | tf/demos/autorecord_2026-07-30_12-43-55.dem | 38,294,569 | 67,381 | 1010.715 | 66,339 |
| 5 | tf/demos/autorecord_2026-06-02_19-53-35.dem | 29,830,207 | 59,950 | 899.250 | 58,437 |

**Chosen for B: rank 1, `autorecord_2026-08-09_14-48-38.dem`** (sha256 a2d92bf5ecd329448d8c822915424e44856848372173e1c64d00fc64598bae5f). Why: longest playback of the 42 (2215.935 s), most ticks (147,729) so tick 2000 is deep inside live gameplay, and it is a full match-length POV rather than a connection fragment. Previous sample `autorecord_2026-07-02_13-26-46.dem` (39,122 ticks, 586.83 s) ranks 14/42 and is kept as the **fallback**: it is the demo the gate scripts' tick-2000 recipe was actually demonstrated on (world-material-face-check.sh:89-96, evidence/world-material-face/trace-albedo.txt), so if rank 1 fails twice, fall back to it and say so in the report. The 437-tick fragment is excluded from consideration entirely.

Proven recipe: **no** --start-paused, `--capture-tick 2000` with a retry loop until the draw reports exactly tick 2000.
- world-material-face-check.sh:89-96: `timeout -k 5 150 $PRODUCT --tf-root $TF --demo $DEMO --hidden-window --capture-tick 2000 --capture-frame ... --audio-disabled --viewmodel ... --viewmodel-render companion --world-water off --world-face-trace`.
- visual-baseline-check.sh:108-125 explains --capture-tick N = first draw with tick >= N and retries (6 attempts) until the draw reports exactly 2000; :121 the same command shape.
- world-geometry-hole-check.sh:58 TICK=2000; :211-212 the same.
- lighting-semantics-check.sh:131 --capture-tick 2000.

Proposed forward capture with face trace:

```
for attempt in 1 2 3 4 5 6 7 8; do
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/odyssey-t2000.trace")" \
  timeout -k 5 150 native/build-nmake/tf2_demo_native.exe \
    --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
    --demo "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-08-09_14-48-38.dem" \
    --hidden-window --capture-tick 2000 \
    --capture-frame "$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/odyssey-t2000.bmp")" \
    --audio-disabled --world-water off --world-face-trace \
    --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render companion
  tick=$(grep -a "draw: begin tick=" evidence/t56-map-audit/runtime/odyssey-t2000.trace | tr -d '\r' | tail -1 | sed -n 's/.*tick=\([-0-9]*\).*/\1/p')
  [ "$tick" = "2000" ] && break
done
```

- Expected duration: 45-90 s per attempt, 8 attempts max (world-material-face-check.sh:89 loop bound); measured single runs 42.6 s (trace-albedo) and 46.9 s (trace-textured). The rank-1 demo is 82.1 MB vs 21.9 MB for the measured one, so allow 60-120 s per attempt and budget 12 min worst case with retries.
- Lock: yes.
- Camera reality: the trace's `scene: camera mode=first-person target=1 follows=<n> holds=0 freeze=0` means the camera follows the POV player. The only world-space eye is `gpu: world vis ... eyeMap=(-2423.6,5566.0,-1280.0)` at tick 2000 — deterministic for that tick, but not a static scene camera; pitch/yaw are not emitted at all.

## C. cp_sunshine — SourceTV (NEW: no existing gate targets it)

No gate script hardcodes cp_sunshine (see gate_matrix.md). Two candidate recipes, both unproven for this map:
1. **Preferred: --start-paused + --capture-tick 0** (same shape as snakewater, which is also SourceTV). Lowest risk: no waiting for a tick.
2. Alternative: --capture-tick 2000 with the retry loop above (same shape as odyssey), using SUNSHINE.dem.

Proposed command (option 1):

```
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/sunshine-t0.trace")" \
timeout -k 5 300 native/build-nmake/tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --audio-disabled --hidden-window \
  --demo "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/SUNSHINE.dem" \
  --start-paused --capture-tick 0 \
  --capture-frame "$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/sunshine-t0.bmp")" \
  --metrics-file "$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/sunshine-t0.csv")"
```

- Expected duration: 60-150 s (BSP 30.3 MB, 66 demos on map, largest material set 174; first load may compile shaders).
- Lock: yes.
- Why it matters: sunshine has the most materials (174), the most pak-only materials (62) and a different missing-VMT profile (DEV/REFLECTIVITY_10B + PROPS/HAZARDSTRIP001A) — it exercises atlas/material paths snakewater and odyssey do not.

## D. Negative control — koth_bagel_rc13 (missing BSP)

world-material-check.sh:278-283 uses the bagel_rc13 testdata demo as the "map must not be found" control. Proposed:

```
timeout -k 5 120 native/build-nmake/tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --demo "D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem" \
  --hidden-window --capture-tick 0 --capture-frame "$(cygpath -m "$PWD/evidence/t56-map-audit/runtime/bagel13-t0.bmp")"
```

- Expected: non-zero exit and a `map: lookup ... reason=` failure naming koth_bagel_rc13; no BMP. Duration 10-30 s. Lock: yes (it still opens the product).
- Basis for the gap: gen_reachability.py printed `bagel_rc13 bsp: (None, None)`; tf/maps and tf/download/maps both lack the file.

## Serial schedule (proposal to the Lead)

| # | run | expected | lock |
|---|---|---|---|
| 1 | A snakewater t0 | 60-120 s | yes |
| 2 | B odyssey t2000 face-trace | 45-90 s (up to 10 min with retries) | yes |
| 3 | C sunshine t0 | 60-150 s | yes |
| 4 | D bagel_rc13 negative | 10-30 s | yes |

Total: roughly 4-8 minutes of product time if all first attempts land, 20 minutes worst case with retries. All four are forward runs; none rebuilds or mutates the product.

## Gate pairing recommendation

- snakewater -> world-material-check.sh (map pinned there at :60) and material-coverage-check.sh (primary map at :66).
- odyssey -> visual-baseline-check.sh (:74), world-material-face-check.sh (:64), world-geometry-hole-check.sh (:57), lighting-semantics-check.sh (:80).
- sunshine -> no gate; the proposed C run is a new forward capture, not a gate, until someone adds a MAP override to a gate (all six hardcode or pin odyssey/snakewater).
- bagel_rc12/bagel_rc13 -> negative control only; not part of the three-map set.
