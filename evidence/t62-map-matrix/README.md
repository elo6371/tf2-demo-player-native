# T62 map material matrix — spot check (2026-11-03)

Five maps named by the T62 matrix, read with the same instruments
`material-coverage-check.sh` uses, but for every map and at two ticks
(0 and 2000). This is **not a gate**: no threshold is asserted, nothing in
`map-material-matrix.sh` compares a reading against a bound, and the exit code
only says whether a reading the matrix needs is present. `material-coverage-check.sh`
was not modified; `evidence/material-coverage/` was not touched.

Reproduce:

    bash map-material-matrix.sh                     # all five maps, ~50 min
    bash map-material-matrix.sh --only=pl_odyssey   # one map (keeps the other rows)
    bash map-material-matrix.sh --only=nope         # exit 2, unknown map

Exit codes: 0 = every map has a probe plan, both capture runs, a tile table and
a pixel diff; 1 = one of those is missing or a run failed (details in
`failures.txt`); 2 = usage/environment error. A missing BSP is not a failure:
the map is recorded as `UNSUPPORTED` and skipped, and no row is invented for it.

## What the instruments can and cannot give

**Probe** — `native/build-nmake/material_coverage_probe.exe --map <bsp> --tf-root <tf>`
prints 33 `{"reading":"key","value":"value"}` lines (map.distinctMaterials,
plan.tiled, map.vmtMissing, map.vmtNoBase, map.vtfMissing, plan.batches,
plan.refused, plan.beyondOldCap, feature.* …). It prints **aggregates only**; it
has no per-material tile detail at all. Every row therefore carries
`probe_tile_table=NOT-PROVIDED`, and the tile tables below come from the
product trace instead. Nothing was invented to fill that column. The probe
also has no `patchDeclared`, `patchResolved` or `transformRefused` reading;
those three exist only in the product's `gpu: world material-fetch` line, so
they appear only in the product table below.

**Product** — `native/build-nmake/tf2_demo_native.exe --demo … --capture-tick N`
with `TF2_NATIVE_TRACE` pointing at a scratch file. The relevant lines are
emitted by `traceWorldDraw` (`native/src/main.cpp:6918-6926`), which prints them
**once per run, on the first world draw**:

    gpu: world material-fetch distinct=… tiled=… capped=… vmtMissing=… vmtNoBase=…
        vtfMissing=… patchDeclared=… patchResolved=… transformRefused=…
    gpu: world atlas size=… bytes=… atlases=… totalBytes=… state=…
    gpu: world atlas-plan batches=… tiled=… refused=… malformed=… duplicate=… budget=…
    gpu: world tile material=… tris=… visible=… batch=… tile=… size=… blend=… modulate=… reason=…
    capture: armed requested=N frame_tick=M frames=F

Consequence, stated plainly: the material/atlas/tile block of the **tick-2000
run is the first drawn frame of that run**, not a table recomputed at tick 2000.
For all five maps `tile-tick0.raw` and `tile-tick2000.raw` are byte-identical
(`cmp` checked), and the two `gpu: world material-fetch` lines are equal. What
does differ between the two runs is `capture: armed frame_tick` (0 vs 2000) and
the captured frame itself, which is what the pixel diff measures. So this
evidence supports "same material plan at both ticks", not "materials are
provably unchanged at tick 2000".

**Fixed camera** — both runs of a map use the same `--debug-camera x y z pitch
yaw`, so the two frames differ by when they were drawn, not by a moved
observer. All ten runs printed `scene: camera … src=debug` with identical
eye/pitch/yaw for tick 0 and tick 2000. Pose source per map:

* POV demos (pl_odyssey, cp_sunshine, koth_bagel_rc12):
  `demo_open_probe --scan --camera-tick 2000` → camera_origin/pitch/yaw.
* SourceTV demos (cp_snakewater_final1, koth_bagel_rc11): the recording has no
  POV camera samples (origin 0,0,0), so one throwaway *orient run* of the
  product at tick 2000 is made and its own `scene: camera` line
  (`eye=… pitch=… yaw=… src=follow`) is used as the fixed pose. The orient run
  is also inside the lock; its trace stays in `.scratch/t62-map-matrix`.

**Pixel diff** — `bmp-pixel-diff.py` reports differing *bytes*; the matrix needs
"pixels whose any RGB channel moved by more than 8", so the diff is computed by
an embedded Python block (24/32bpp BI_RGB, RGB only, alpha ignored). Reported
per map: `pixels_channel_gt8`, its percentage, `max_channel_delta`, both
sha256s. `diff-is-per-map`: the same diff is attached to both tick rows because
it describes the pair.

## Maps, demos, cameras

| map | BSP | bytes | demo | demo ticks | recording | camera source | eye | pitch | yaw |
|---|---|---|---:|---|---:|---|---|---|---:|---:|
| cp_snakewater_final1 | `maps/cp_snakewater_final1.bsp` | 59,590,420 | `bb841c6d…-cp_snakewater_final1.dem` | 64,352 | SourceTV | orient run @2000 | 1185.5,-813.5,207.1 | -6.71 | 118.59 |
| pl_odyssey | `maps/pl_odyssey.bsp` | 131,192,482 | `autorecord_2026-05-26_23-14-06.dem` | 55,294 | POV | demo camera @2000 | -1221.21,462.209,650.252 | 14.3087 | 171.148 |
| cp_sunshine | `maps/cp_sunshine.bsp` | 30,319,857 | `autorecord_2026-05-14_23-09-16.dem` | 144,583 | POV | demo camera @2000 | -3760,376,-111.969 | 0 | 90 |
| koth_bagel_rc12 | `download/maps/koth_bagel_rc12.bsp` | 26,486,963 | `autorecord_2026-05-14_21-28-01.dem` | 153,502 | POV | demo camera @2000 | 533.246,-803.326,320.031 | 11.0551 | 3.79553 |
| koth_bagel_rc11 | `download/maps/koth_bagel_rc11.bsp` | 26,370,688 | `dfbce608…-koth_bagel_rc11.dem` | 100,632 | SourceTV | orient run @2000 | 914.0,-1000.0,563.8 | 25.76 | 157.30 |

All five BSPs exist under `tf` (tested with `-f` before any run), so there is no
`UNSUPPORTED` map in this matrix. The two SourceTV demos are in
`D:/TF2_Demo_Player/testdata/demos`; the POV `autorecord_*` demos are in
`tf/demos`. Each demo's header map matches its BSP and each has >2000 ticks
(`<map>/camera-probe.txt` carries the `map=… ticks=… recording=…` line).

### Map set vs the T62 matrix

T62's map matrix names five maps, one of which is `koth_bagel_rc13`. There is no
`koth_bagel_rc13.bsp` anywhere in this install (2026-11-03: `tf/maps` holds
`koth_bagel_event.bsp` and `koth_bagel_rc12.bsp`; `tf/download/maps` holds
`koth_bagel_rc5/rc10/rc11/rc12.bsp`), so that cell stays **UNSUPPORTED** in
`evidence/t62-matrix/MAP-MATRIX.md` and is not covered here either. This run
covers the other four T62 map names plus `koth_bagel_rc11`, the bagel version
whose BSP and demo are both present and whose rc11 demo is the one the T79
camera evidence uses. So the five maps here are four of T62's five plus rc11;
rc13 is neither replaced nor silently claimed.

## Probe plan readings (per map, tick-independent)

| map | distinct | plan.tiled | vmtResolved | vmtMissing | vmtNoBase | vtfResolved | vtfMissing | batches | refused | beyondOldCap | tile table |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| cp_snakewater_final1 | 148 | 112 | 114 | 4 | 31 | 112 | 1 | 2 | 0 | 48 | NOT-PROVIDED |
| pl_odyssey | 127 | 92 | 93 | 3 | 32 | 92 | 0 | 2 | 0 | 28 | NOT-PROVIDED |
| cp_sunshine | 169 | 106 | 107 | 2 | 61 | 106 | 0 | 2 | 0 | 42 | NOT-PROVIDED |
| koth_bagel_rc12 | 99 | 77 | 78 | 0 | 22 | 77 | 0 | 2 | 0 | 13 | NOT-PROVIDED |
| koth_bagel_rc11 | 99 | 77 | 78 | 0 | 22 | 77 | 0 | 2 | 0 | 13 | NOT-PROVIDED |

pl_odyssey's `plan.tiled` is **92** — the "tiled=99" that the earlier report text
quoted for that map is not what the probe prints, and is not used anywhere here.

## Product readings (per map, identical at both ticks)

| map | fetch distinct | fetch tiled | capped | vmtMissing | vmtNoBase | vtfMissing | patchDeclared | patchResolved | transformRefused | plan tiled / batches / refused | atlas size / bytes / totalBytes | layerBytes | tile rows |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---:|---:|
| cp_snakewater_final1 | 148 | 123 | 0 | 4 | 4 | 1 | 30 | 30 | 16 | 123 / 2 / 0 | 4112 / 67,634,176 / 135,268,352 | 270,536,704 | 123 |
| pl_odyssey | 127 | 118 | 0 | 3 | 2 | 0 | 31 | 31 | 4 | 118 / 2 / 0 | 4112 / 67,634,176 / 126,814,080 | 262,082,432 | 118 |
| cp_sunshine | 169 | 128 | 0 | 2 | 3 | 0 | 60 | 60 | 36 | 128 / 2 / 0 | 4112 / 67,634,176 / 135,268,352 | 270,536,704 | 128 |
| koth_bagel_rc12 | 99 | 85 | 0 | 0 | 2 | 0 | 21 | 21 | 12 | 85 / 2 / 0 | 4112 / 67,634,176 / 94,053,776 | 229,322,128 | 85 |
| koth_bagel_rc11 | 99 | 85 | 0 | 0 | 2 | 0 | 21 | 21 | 12 | 85 / 2 / 0 | 4112 / 67,634,176 / 94,053,776 | 229,322,128 | 85 |

`atlas size`/`bytes` are the first atlas's numbers and are the same on every map
(67,634,176 = 4112² × 4); `totalBytes` is the one that varies (two atlases).
`readings.tsv` stores `atlas_state` and `atlas_bytes` (the first value);
`totalBytes` and `layerBytes` are in `<map>/trace-tick<N>.txt` verbatim.

The probe/product gap is the known one (`probe_tiled` < `fetch_tiled`): the probe
does not follow patch includes and the product does. On cp_snakewater_final1
these product numbers reproduce `evidence/material-coverage/product-readings.txt`
exactly (distinct=148 tiled=123 vmtMissing=4 vmtNoBase=4 vtfMissing=1
patchDeclared=30 patchResolved=30 transformRefused=16), which is the sanity check
that these are the same instruments, not a new definition.

## Captures and pixel diff (tick 0 vs tick 2000, same demo, same camera)

| map | capture requested | frame_tick | frames | trace bytes | frame sha256 (tick0 / tick2000, first 16) | pixels any channel >8 | percent | max delta |
|---|---:|---:|---:|---:|---|---:|---:|---:|
| cp_snakewater_final1 | 0 / 2000 | 0 / 2000 | 0 / 1,313 | 86,311 / 5,209,923 | d72d9c300ab48a68 / 467b2490c342657f | 64,898 | 7.5394% | 224 |
| pl_odyssey | 0 / 2000 | 0 / 2000 | 0 / 1,819 | 86,120 / 6,607,771 | b3acaa2998b0bc6b / 8f4b3911b8a02b1e | 475,423 | 55.2314% | 226 |
| cp_sunshine | 0 / 2000 | 0 / 2000 | 0 / 1,645 | 88,660 / 6,676,308 | edd92e3b24fbac45 / bc86d1c167a921db | 547,656 | 63.6229% | 209 |
| koth_bagel_rc12 | 0 / 2000 | 0 / 2000 | 0 / 1,614 | 78,352 / 6,504,239 | 24fcf23b1d0abb96 / 9644a1e6f0f65073 | 173,961 | 20.2096% | 255 |
| koth_bagel_rc11 | 0 / 2000 | 0 / 2000 | 0 / 2,049 | 79,620 / 7,232,165 | 0d92f09975e9e85f / b2f4a3a18458b403 | 132,942 | 15.4443% | 200 |

Every run's last `capture: armed` line has `frame_tick` equal to the requested
tick (0 and 2000), so neither capture silently landed on the other tick, and no
map produced two identical frames (identical_files=0, different sha256, and
7.5%–63.6% of pixels moved by more than 8 levels). `frames=0` on the tick-0 runs
is the reading the product printed (`capture: armed requested=0 frame_tick=0
frames=0`), not a failed capture: those runs wrote their BMP, which the diff
then read. Captures are 1264×681 BMP (860,784 pixels).

The BMP frames are kept in `.scratch/t62-map-matrix` (not committed): ~73 MB of
traces + frames for the five maps. Only the grep extracts, tile rows and the
diff under `evidence/t62-map-matrix/` (≈400 KB) are checked in.

## Commands actually run (template, one map shown)

Every probe/product launch is wrapped in the shared host lock, one product
process at a time:

    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t62-map bash -c '<cmd>'

probe (per map):

    native/build-nmake/material_coverage_probe.exe \
      --map "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_odyssey.bsp" \
      --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"

camera probe (POV demos only; SourceTV maps get an orient run instead):

    native/build-nmake/demo_open_probe.exe --scan --camera-tick 2000 \
      "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-05-26_23-14-06.dem"

product, tick 0 (`--start-paused`; a paused demo holds at tick 0) and tick
2000 (no pause):

    TF2_NATIVE_TRACE=D:/…/.scratch/t62-map-matrix/pl_odyssey.tick0.trace \
      timeout 400 native/build-nmake/tf2_demo_native.exe \
      --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
      --audio-device 6 --audio-disabled --hidden-window \
      --demo "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-05-26_23-14-06.dem" \
      --start-paused \
      --capture-tick 0 \
      --capture-frame D:/…/.scratch/t62-map-matrix/pl_odyssey.tick0.bmp \
      --metrics-file D:/…/.scratch/t62-map-matrix/pl_odyssey.tick0.csv \
      --debug-camera -1221.21 462.209 650.252 14.3087 171.148

Extraction (what `<map>/trace-tick<N>.txt` and `tile-tick<N>.raw` contain):

    grep -a 'gpu: world material-fetch'       <trace> | head -1
    grep -a 'gpu: world atlas'                <trace> | head -1
    grep -a 'gpu: world atlas-plan'           <trace> | head -1
    grep -a 'gpu: world tile material='       <trace>   > tile-tick<N>.raw
    grep -a 'capture: armed'                  <trace> | tail -1
    grep -a 'scene: camera'                   <trace> | tail -1

The full commands for each map are reproducible from `map-material-matrix.sh`
(`probe_cmd`, `product_cmd`, `extract_trace`); the lock wrapper echoes
`ACQUIRED owner=t62-map …` and `LOCKED-COMMAND-EXIT rc=… owner=t62-map` into
`<map>/probe-plan.txt` and `<map>/run-tick<N>.log`, which is how "it ran inside
the lock" is checkable rather than asserted.

## Files

    readings.tsv                  one row per map × tick (48 columns, long format)
    failures.txt                  empty on a successful matrix
    <map>/probe-plan.txt          probe stdout verbatim (with the lock banner)
    <map>/camera-probe.txt        demo_open_probe output verbatim
    <map>/camera.txt              fixed camera, its source, the orient-run line
    <map>/trace-tick<N>.txt       grep-extracted trace readings (not the trace)
    <map>/tile-tick<N>.raw        gpu: world tile rows verbatim (the tile table)
    <map>/tile-tick<N>.tsv        same rows, tab-separated + header
    <map>/diff.txt                pixel diff between the two captures
    <map>/run-tick<N>.log         product log (lock banner + rc)
    <map>/status.txt              rc/probe/tile-row summary for that map

## Limitations (read these before quoting a number)

1. `probe_tile_table=NOT-PROVIDED` on every map — the probe has no per-material
   detail. The tile tables exist only via the product trace.
2. The material/atlas/tile block is emitted once per run on the first world
   draw, so the "tick 2000" table is the run's first-frame table. Both ticks
   showing the same numbers is an instrument property here, not evidence that
   the material plan is unchanged at tick 2000.
3. Two ticks only (0 and 2000), one demo per map, one camera per map. A
   material that only appears later in a match is invisible to this matrix.
4. SourceTV maps have no POV camera track; their pose is the product's own
   follow camera at tick 2000 (recorded in `camera.txt` as
   `orient_scene_camera_line`), not a recorded player view.
5. The pixel diff proves the two captures are different frames; it does not
   attribute the difference to a material change (players, HUD, effects and
   lightmaps all move between ticks 0 and 2000). It is a "these are not the
   same frame" check, not a material-change measurement.
6. No threshold and no pass/fail on any number: 7.5% moved pixels on
   cp_snakewater_final1 and 63.6% on cp_sunshine are both simply recorded.
7. Trace summaries are grep extracts. The raw traces (5–7 MB per tick-2000 run)
   live in `.scratch/t62-map-matrix` and are not part of this evidence.

8. The two captures are not bit-reproducible run to run: the number of frames
   drawn before the capture lands (frames=…) varies with wall-clock playback,
   so a rerun of cp_sunshine moved 547,656 px instead of 547,760 and changed
   both sha256s, while frame_tick stayed 2000 and the material/atlas/tile
   readings were unchanged. Treat the diff numbers as order-of-magnitude
   evidence ("these are different frames"), not as fixed constants.

