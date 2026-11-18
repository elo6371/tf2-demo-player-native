# T38 — final performance and footprint cleanup review (task-11)

Owner: t38. Branch: task/t38-perf. Base: ac7e2b7e40e4391241466b921495149a61b5e0ef
("docs(t28): record the release acceptance, the one full-chain run, and the
attributable blockers"). Date: 2026-10-20.

This is a READ-ONLY audit pass. Nothing was deleted, no product source was
changed, no pin was re-baselined, .gitignore is untouched. The only files this
branch adds are this report and the deletion manifest under evidence/t38-perf/.
Every number below was measured on this machine in this worktree on ac7e2b7;
none of it is copied from the T33 audit on 36303c2.

## 1. Inputs (exact)

| input | value |
|---|---|
| demo (pinned) | D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem |
| reference map (context only, not loaded by this run) | D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_odyssey.bsp |
| resource root | D:/SteamLibrary/steamapps/common/Team Fortress 2/tf |
| viewmodel | models/weapons/v_models/v_scattergun_scout.mdl (--viewmodel-render companion) |
| capture point | --capture-tick 2000, --hidden-window --audio-disabled |

## 2. What was run, and with what exit codes

All product runs went through the host lock, one logical sequence per lock:

| # | command | exit code | duration |
|---|---|---|---|
| 1 | hostlock run t38 -> .scratch/t38/01-build-and-measure.sh | 0 | 458 s (build 366 s) |
| 2 | hostlock run t38 -> .scratch/t38/02-gate.sh (forward) | 0 | 74 s |
| 3 | same script, second invocation (--mutation) | 0 | 283 s |

Exact product command line (identical in step 1 and inside the committed gate):

    TF2_NATIVE_TRACE=<trace> native/build-nmake/tf2_demo_native.exe \
      --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
      --demo "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem" \
      --hidden-window --audio-disabled --capture-tick 2000 --capture-frame <bmp> \
      --viewmodel models/weapons/v_models/v_scattergun_scout.mdl \
      --viewmodel-render companion --metrics-file <csv>

Gate invocations, byte-for-byte the committed script (sha256 of the working-tree
file 95e63d9c…, the git blob af1390f1…; the difference is core.autocrlf=true
rendering LF as CRLF on checkout — `git ls-files --eol` reports i/lf w/crlf, and
`git diff HEAD -- quality-audit-check.sh` is empty, so the file that ran is the
committed one):

    bash quality-audit-check.sh
    bash quality-audit-check.sh --mutation

## 3. Build (before = ac7e2b7, clean Release, full build-cmake.sh)

* build_rc = 0, build_seconds = 366
* errors = 0, warnings = 10 (identical set to T33's ten: 5x C4244 double->float
  in main.cpp:1103-1107, C4457 main.cpp:6257, 2x C4267 native_renderer.cpp:3642-3643,
  2x C4456 native_renderer.cpp:3950/4120)
* exe_count = 42 (T33 measured 41 on 36303c2; the four T30 probe targets moved
  it, already recorded by 43a9949)
* total_exe_bytes = 21,800,960 (20.8 MiB); largest tf2_demo_native.exe = 1,821,696 B
* per-exe sha256 inventory: .scratch/t38/exe-sha256.txt

## 4. Footprint of the tracked tree (static, re-measured on ac7e2b7)

| | ac7e2b7 (T38, now) | 36303c2 (T33, then) |
|---|---|---|
| tracked files | 3,447 | 3,351 |
| tracked bytes | 1,324,098,756 (1262.8 MiB) | ~1,301 MiB (1240.8 MiB) |
| duplicate sha256 groups | 212 | 208 |
| files inside those groups | 1,379 | — |
| wasted by duplication | 598,588,586 B (570.9 MiB, 45.2 % of tree) | 565.0 MiB |
| members with no reference at all | 35 (7.94 MiB) | — |
| … of those, copies whose identical bytes survive elsewhere | 31 (3.01 MiB) | — |
| … of those, last copy of their content | 4 (4.93 MiB) | — |

Duplication by extension: .dem 385.9 MiB, .bmp 172.4 MiB, .png 7.4, .txt 3.3,
.tsv 1.9 (full table in .scratch/t38/duplicates.json). Nothing was deleted; the
per-candidate proposal with path/size/sha256 and the keep-or-drop reason is
evidence/t38-perf/deletion-manifest.txt, which the Lead approves or rejects.

## 5. Runtime readings (before = ac7e2b7 measured)

Two consecutive tick-2000 captures of the same scene:

    ref-a frame_sha256 = 6bb4d9a712e82ef4cf58929651cea1070d11c1ac082bdb25a6e6d7097e2f974b (2,582,406 B)
    ref-b frame_sha256 = 6bb4d9a712e82ef4cf58929651cea1070d11c1ac082bdb25a6e6d7097e2f974b

Load-time world readings (verbatim trace lines, identical in both runs):

    18156.2 gpu: world material-fetch distinct=108 tiled=85 capped=0 vmtMissing=3
            vmtNoBase=20 vtfMissing=0 vtfTooLarge=70 decodeEmpty=0 maxWidth=1024
            maxHeight=2048 fromPakfile=85 downscaled=70 blendFirst=10
            blendLastRank=10 blendCapped=0
    19977.8 gpu: world atlas-plan batches=2 blendBatches=1 tiled=85 refused=0
            malformed=0 duplicate=0 budget=0 layerBytes=229322128 reason=85 of 85
            materials tiled in 2 batch(es); refused=0 (malformed=0, duplicate=0,
            budget=0); layers=89+64+64+0+0 MiB
    18903.1 gpu: world atlas size=4112 bytes=67634176 atlases=2
            totalBytes=94053776 state=bound
    18903.1 gpu: world blend enabled=1 terrainOnly=0 textures=85 layers=10
            accepted=85 acceptedLayers=10 tiles=10 modulateTiles=10
            malformedLayers=0 weightedVertices=85920 modulatedVertices=85920
            weightMin=0.000 weightMax=1.000 distinctWeights=34
    18767.6 gpu: world sky-upload state=active vertices=36 triangles=12 faces=6
            side=512 resampled=1 reason=six sides, 36 vertices
    19981.5 gpu: world isolation weaponDrawsWithWorldAtlas=0
            worldDrawsWithWeaponTexture=0 slot0Binds=5 batchDraws=2
            texturedBatchDraws=2
    19977.8 gpu: world clip near=0.00032775 far=100.000000 nearMapUnits=4.00
            farMapUnits=1220444 horizontalScale=0.00008194 state=published

Comparison with T33's table for the same inputs (their numbers, re-measured here):

| reading | T33 on 36303c2 | T38 on ac7e2b7 |
|---|---|---|
| atlas textures | 2, 89.7 MiB | 2, totalBytes 94,053,776 = 89.7 MiB — unchanged |
| atlas-plan layerBytes | 218.7 MiB | 229,322,128 = 218.7 MiB — unchanged |
| mean frame time | 15.89 ms | 15.45 ms (ref-a) / 15.47 ms (ref-b) |
| fps | 65.9–66.7 | 64.8 / 64.9 median after 5 s |
| peak working set | 2.18–2.83 GiB | 2188.0 / 2185.2 MiB |
| steady growth after 5 s | — | −12.5 / −8.0 MiB (no growth; negative = the ramp releasing) |

So T29's near-plane fix, T30/T31/T32 and the new probes did not move any pinned
footprint reading. The one number that moved is frame rate: 66.7 -> ~64.9 median
fps and 15.89 -> 15.45 ms mean frame. That is a ~2.7 % change on a different
commit and it is not attributable to any single change from this audit alone; the
gate's own floor (>= 30 fps) is far below it and the run-to-run spread between
two captures of the same scene was 0.1 fps. Recorded, not explained — see §7.

Scene-update cadence, measured from the trace: 1,922 "scene: begin" blocks over
22.815 s = 84.2 scene updates/s, while "draw: begin" fired 1,413 times = 62 fps.
Each scene update reports buildInstances=256 instances and setInstances
draws=94 markers=32; each of the 256 ModelInstance objects carries four
std::string members (className, modelPath, cacheKey, diagnostic) plus a fifth
inside its transform, and the returned vector is rebuilt from scratch every
update with no capacity reuse on the caller side (main.cpp:6192). That is the
per-scene-update allocation churn the previous pass flagged; the measured rate
is 84 updates/s, not the 66.7/s that pass estimated, so its "180–250 allocs/s"
figure is low by roughly a third — order of magnitude right, exact number wrong.
This remains a read-code conclusion: no wall-clock attribution was made, and it
is NOT presented as a measured regression.

## 6. The committed gate on ac7e2b7 — forward and mutation

Forward (`bash quality-audit-check.sh`): exit code 0, PASS. All 20 pins held at
their committed values, the two captures of the same scene were byte-identical
(6bb4d9a7…), the deterministic readings of the two runs matched line for line,
and the envelope held (peak working set 2181.2 MiB <= 3072, steady growth
−7.8 MiB <= 32, median fps 64.8 >= 30, 1,419 frames >= 500, 23 metrics rows >= 5,
34,763 trace lines >= 1000). readings.txt/gate.txt were then restored to T33's
frozen contents with git checkout, because this branch does not revise T33's
evidence.

Mutation (`bash quality-audit-check.sh --mutation`): exit code 0, PASS — the
mutation went red as required and everything came back:

* mutation: native/include/world_tile_atlas.h maxTilesPerBatch 64 -> 20
* mutated readings: atlas_batches 2 -> 5, atlas_count 2 -> 5,
  atlas totalBytes 94,053,776 -> 90,883,424, layerBytes 229,322,128 -> 133,154,784
* mutated frame sha256 85bdba36… vs reference 6bb4d9a7… (the pixels do move,
  which is more than the gate asks of this mutation)
* source restore: byte-identical (verified independently of the gate:
  sha256 5d4036541b76074323c11920aaaf5dcd4c399a93d4b3951537760eb55bed034a both
  before and after)
* rebuilt binary: 212f4b52… vs the pre-mutation fb86c452… — a relink artifact,
  exactly as the gate's own note anticipates; the restoration proof that matters
  is the next line
* restored tree reproduces the reference readings exactly and the reference
  frame hash 6bb4d9a7…

No pin was touched, and nothing had to be: every pin is a load-time reading and
T29 changed a projection matrix, so the pins did not move.

## 7. Read-code review of the named hot paths

Asked: duplicate VMT/VTF parsing, atlas copies, per-frame allocation, diagnostic
output left in the production path. Findings, worst first:

1. Per-scene-update instance rebuild (main.cpp:6177-6438). Every scene update
   builds a fresh `std::vector<ModelInstance>` (256 instances), three further
   vectors (draws, fallbackMarkers, entityBoneMatrices), and then re-buckets
   everything by cacheKey into a fresh `std::vector<Bucket>` +
   `std::unordered_map<std::string, size_t>` inside
   Renderer::setEntityModelInstances (native_renderer.cpp:3121-3179). At 84
   updates/s that is on the order of 1,500–2,500 heap allocations per second for
   data that is 94% identical to the previous frame's. Read-code only: no
   profiler was attached, so this is a structural observation, not a measured
   cost. The cheapest way to price it is a counter on the instance vector's
   capacity and the bucket map's node count, not a wall-clock experiment.
2. Per-instance strings rebuilt every update. instance.className / modelPath /
   cacheKey / diagnostic / transform.diagnostic are std::string and are
   re-assigned per instance per update (entity_model.cpp:626-650). Most are
   short, so small-string optimisation hides a lot of it, but it is the same
   churn as (1).
3. VMT parsing is per-material, not per-triangle, and is not in the per-frame
   path: vmt_material.cpp parses a material once per fetch and the material
   census is a load-time reading (85 tiled of 108 distinct). No repeated-parse
   defect found in the steady path.
4. VTF decode: 70 of 85 textures are vtfTooLarge and get downscaled once at
   load; CallableResourceProbe (world_material.cpp:604-623) caches per capture
   so no path is read twice. No repeated-decode defect found.
5. Atlas copies: the atlas plan is computed once at world upload
   (layerBytes/atlases readings are load-time and stable across the two runs);
   setEntityBoneMatrices grows its buffer geometrically and only recreates it
   when capacity is exceeded (native_renderer.cpp:3247-3269), so bone uploads do
   not thrash. No atlas-copy defect found.
6. Residual diagnostics in the production path: two remain.
   * main.cpp:52 — the "TEMP DIAGNOSTIC" trace channel itself. It is inert
     unless TF2_NATIVE_TRACE names a file (main.cpp:111-118) and this audit
     depends on it, so it stays.
   * main.cpp:6174-6191 — the buildInstances try/catch that rethrows with a
     trace line. It was added to locate an unhandled 0xE06D7363 and the comment
     still says so. It is a guard, not a per-frame cost (it costs nothing on the
     non-throwing path beyond a try frame), but the TEMP label is stale: if that
     crash is fixed, the guard and its label should be revisited rather than
     inherited forever.
   * 18.2 trace lines are emitted per scene update, all gated on the trace file
     being open. With the trace off they cost one pointer test each.

Nothing in this review changed a tolerance, a pin, or a product source file.

## 8. Limitations / not verified

* No wall-clock or profiler attribution of the frame-time difference in §5
  (66.7 -> 64.9 median fps). Two captures of the same scene differed by 0.1 fps,
  so the measurement is stable; what is missing is the same measurement on
  36303c2 side by side on this machine in this session, which is the cheapest
  next step if the Lead wants the 2.7 % attributed (run 01-build-and-measure.sh
  on a 36303c2 worktree and diff the two CSVs).
* The per-scene-update allocation churn (§7.1) is priced by reading code, not by
  a counter. Cheapest next step: add a capacity reading to the trace line that
  already prints buildInstances/setInstances and re-run the pinned capture.
* The 4.93 MiB in deletion-manifest section B (two sunshine-sky.bmp and two
  sky-face-samples.txt under evidence/t8-render-reliability-*) are the last
  copies of their bytes; whether they may go is the Lead's call, not mine.
* The .dem duplication (385.9 MiB) is almost entirely fixtures repeated across
  run directories (manifest section C, 7 sha256 groups x 4 copies). Nothing was
  deleted; the manifest lists every candidate with its sha256.

## 9. Evidence

| file | what it is |
|---|---|
| .scratch/t38/01-build-and-measure.log | full log of the build + two captures + metrics |
| .scratch/t38/01-build.txt | build_rc/errors/warnings/exe_count/bytes summary |
| .scratch/t38/exes.txt | 42 exes with sizes, sorted |
| .scratch/t38/exe-sha256.txt | sha256 of all 42 exes (post-gate binary included) |
| .scratch/t38/readings-raw.txt | the verbatim trace lines of §5 |
| .scratch/t38/ref-a.trace / ref-b.trace / .csv / .bmp | the two pinned captures |
| .scratch/t38/cmake-build.log | the compiler's own log |
| .scratch/t38/02-gate.log | full log of forward + mutation + restore |
| .scratch/t38/gate/forward-readings.txt, forward-gate.txt | forward verdict as the gate wrote it |
| .scratch/t38/gate/mutation.txt, mutation-run-gate.txt | mutation verdict as the gate wrote it |
| .scratch/t38/duplicates.json | the 212 duplicate groups with sizes and members |
| .scratch/t38/deletion-manifest.txt | the per-candidate proposal (copy of evidence/t38-perf/deletion-manifest.txt) |
| evidence/t38-perf/deletion-manifest.txt | the committed manifest for the Lead's approval |

## 10. Process state

* git status --short on task/t38-perf: clean apart from the two files this
  branch intentionally adds (T38-PERF-FOOTPRINT-2026-10-20.md and
  evidence/t38-perf/deletion-manifest.txt).
* No tf2_demo_native.exe process left running (tasklist: no matching tasks).
* Host lock released after each run; the final hostlock status was FREE.
* The mutation source file was verified byte-identical to its pre-mutation
  sha256, independently of the gate's own check.
