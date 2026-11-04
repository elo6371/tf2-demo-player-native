# MAP-MATRIX — T62 focused map matrix (existing T50–T61 scope only)

**Evidence snapshot for this refresh:** `integration/t79-final @ 046f4cd52a6eae5eb952488bb0095bcce86500b1` (2026-11-04, not frozen). First refresh: 2026-10-29 against candidate `task/repo-cleanup @ 2f14873`. Original 2026-10-25 assessment was against `047bcfb` ("Merge T59: world materials through VMT patch includes").
**No map gate was re-run for this refresh.** The six gates run on 2026-11-04 are character gates (`player-animation-check`, `player-model-check`, `animation-availability-check`, `entity-material-check`, `entity-property-lookup-check`, `player-animation-pixels`); their readings are in [RUNS-2026-11-04.md](RUNS-2026-11-04.md). The four PASS cells below remain historical readings from their cited evidence, not fresh verdicts on `046f4cd`; no status is inferred from unchanged code paths.
**Working tree at evidence review:** the 2026-11-04 commit adds character-pixel evidence, a per-class driver and documentation. No map evidence or map gate output was changed.
**Not a release verdict.** T62 forbids one ("do not equate all-green release gates with a correct picture"); T55 video clip/export is out of scope and unimplemented; `verify-all.sh --quick` belongs to T73.

Status legend: **PASS** = a gate exists, a committed run exists, and the committed reading supports the claim. **FAIL** = committed evidence contradicts the pin or the claim it is used for. **NOT VERIFIED** = the axis is measurable today but no committed reading covers this map at this tree. **UNSUPPORTED** = the input the axis needs is absent from the install, so no run can produce it.

For this refresh, every PASS cell shown below is explicitly historical and was not re-tested at `046f4cd`.

---

## 1. The matrix

| Map | Materials: fetch / tile / pixels | Geometry: hole account / cull | Lighting: dark-material account | Material coverage: tile table / batches |
|---|---|---|---|---|
| **cp_snakewater_final1** | **PASS**<br>`evidence/material-coverage/product-readings.txt:1`<br>"gpu: world material-fetch distinct=148 tiled=123 … vmtMissing=4 vmtNoBase=4 vtfMissing=1 … patchDeclared=30 patchResolved=30 transformRefused=16"<br>pixels: `evidence/material-coverage/coverage-delta.txt:2-3` "changed=0 / max_channel_delta=0" | **NOT VERIFIED**<br>no committed hole/cull reading for this map. The only hole gate reads `world-geometry-hole-check.sh:930` (`BSP="$TF/maps/pl_odyssey.bsp"`) | **NOT VERIFIED**<br>`lighting-dark-account-check.sh:88` "MAP=maps/pl_odyssey.bsp" — the account is odyssey-only; no snakewater dark ledger exists | **PASS**<br>`evidence/material-coverage/tile-table-cp_snakewater_final1.txt` (123 rows) + `product-readings.txt:2` "atlas-plan batches=2 blendBatches=1 tiled=123 refused=0 … layers=129+64+64+0+0 MiB"; asserted `material-coverage-check.sh:300` "tile table rows" = 123 |
| **pl_odyssey** | **PASS** ‡<br>`evidence/world-material-face/refusals.txt:1-3`<br>"reason=no-basetexture materials=2 visibleTriangles=300" / "reason=basetexture-transform materials=4 visibleTriangles=160" / "reason=vmt-missing materials=3 visibleTriangles=144"<br>closed by `evidence/t59-materials/T59-MATERIALS-REPORT.md`: "99 + 3 (vmt-missing) + 2 (no-basetexture) + 4 (transform) = 108" | **PASS**<br>`evidence/t26-world-holes/analysis/hole-account.txt`<br>"HOLE split open=92840 enclosed=4613 enclosedUnexplained=0 components=3"<br>`…/cull-control.txt` "CULL state=ok … kept=710543 removed=52788 onlyInCull=0 distinctRemoved=1"<br>re-verified `evidence/t58-geometry/forward-verified-pose.log`: "PASS the binary this run built reproduces the pinned textured frame byte for byte" / "ok=53 fail=0" | **NOT VERIFIED**<br>`evidence/t36-lighting-dark-account/account.txt:1`<br>"frame 1264x681  dark(lum<16) = 208630 of 860784 (24.24%)"<br>but `…/frames.txt:1,3` "camera=the T29 reference" / "eye=-2294.8,348.8,383.4" — the pose `evidence/t26-world-holes/analysis/PINS-T35.md` §T58 documents as **inside solid geometry**; no post-clock-fix re-run exists | **NOT VERIFIED**<br>`evidence/material-coverage/` contains exactly one tile table, `tile-table-cp_snakewater_final1.txt`; the odyssey `tiled=99` figure exists only as report prose (`T59-MATERIALS-REPORT.md`), with no `tile-table-pl_odyssey.txt` and no batch table |
| **cp_sunshine** | **NOT VERIFIED**<br>the only gate that names the map asserts reload errors and sky faces, not materials: `world-render-reliability-check.sh:167` "=== 1/4 reliability input: SUNSHINE.dem (cp_sunshine) ==="; committed frames `evidence/t8-render-reliability-*/sunshine.bmp` carry no material counters | **NOT VERIFIED**<br>no sunshine hole account; the reliability gate captures a frame but asserts no geometry reading (`world-render-reliability-check.sh` checkSkyFaces only) | **NOT VERIFIED**<br>no sunshine dark ledger; `lighting-dark-account-check.sh:89` pins the odyssey demo | **NOT VERIFIED**<br>no sunshine tile table or batch reading; only the static BSP audit `evidence/t56-map-audit/inventory.md:59` "| cp_sunshine | 174 | 2 | 61 | 0 | 62 | vpk 110 / pak 62 |" |
| **koth_bagel_rc12** | **NOT VERIFIED**<br>`evidence/t56-map-audit/gate_matrix.md:155-156` "koth_bagel_rc12 (alternative, not in the three-map set): covered by **no gate** — no script names it"; static audit only: `evidence/t56-map-audit/inventory.md:60` "| koth_bagel_rc12 | 102 | 0 | 22 | 0 | 43 | vpk 59 / pak 43 |" | **NOT VERIFIED**<br>same line; no gate, no hole account | **NOT VERIFIED**<br>same line; no gate | **NOT VERIFIED**<br>same line; no gate. Its only installed BSP is `tf/download/maps/koth_bagel_rc12.bsp` (26,486,963 B, sha `c991a61e…`), which no `--bsp` gate accepts by name |
| **koth_bagel_rc13** | **UNSUPPORTED**<br>`world-material-check.sh:281` "OK   koth_bagel_rc13.bsp is not installed here, so the run reads an empty map"<br>`evidence/t56-map-audit/inventory.md:79` "the BSP is absent from tf/maps and tf/download/maps (gen_reachability.py: 'bagel_rc13 bsp: (None, None)')" | **UNSUPPORTED**<br>no BSP on this install, so no world frame can be rasterised; the testdata demo exists (77,466 ticks) but its map does not | **UNSUPPORTED**<br>same absent BSP | **UNSUPPORTED**<br>same absent BSP |

‡ Historical odyssey-material PASS rests on the committed refusal counters plus T59 report arithmetic; the raw product fetch line (`tiled=99`) is committed only as report text, with no tile table. The arithmetic is `99 + 3 + 2 + 4 = 108`; it is rechecked, not re-run.

**Refresh counts:** 4 historical PASS (not re-tested at `046f4cd`), 12 NOT VERIFIED, 4 UNSUPPORTED, 0 FAIL. No map cell was upgraded or freshly passed in this refresh.

**No cell in this table is FAIL.** The failures this tree actually contains are bookkeeping failures — a stale quoted counter, a stale audit claim, a stale camera — and they are recorded in §2 with both paths named. §2 is where a reader should look for "something is wrong", not the matrix.

### Why, and the cheapest test that would settle each open cell

Cost basis used everywhere below: the only measured per-capture costs in the tree are `evidence/visual-baseline/trace-ref.txt` "59903.2 capture: armed requested=2000 frame_tick=2000 frames=2056" (59.9 s) and `evidence/world-material-face/trace-albedo.txt` "42482.7 capture: armed requested=2000 frame_tick=2000 frames=1415" (42.5 s). A 3-capture gate is therefore ≈ 2.5–3 min, an 18-frame matrix ≈ 15–18 min.

- **cp_snakewater_final1 / geometry — NOT VERIFIED.** The hole account was built for one map only (`world-geometry-hole-check.sh:930` BSP odyssey, `:57` odyssey demo, `:58` TICK=2000). Nothing says a snakewater hole exists or does not; the T56 audit never looked at geometry. Cheapest test: run `world-geometry-hole-check.sh` with `DEMO` swapped to `D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`, `BSP=$TF/maps/cp_snakewater_final1.bsp`, `TICK=0` (the paused shape the material gates already use on this demo), 3 frames instead of 18 → **≈ 3 min** for a hole count, ≈ 15 min for the full matrix with cull control.
- **cp_snakewater_final1 / lighting — NOT VERIFIED.** No dark ledger for this map at any tick. Cheapest test: same 1-frame capture at `--capture-tick 0` plus `visual-baseline-check.sh`'s region style, or reuse `lighting-dark-account-check.sh` with `MAP=maps/cp_snakewater_final1.bsp` and the testdata snakewater demo → **≈ 5 min** (it takes ~5 captures) for a bucket split, no cull control.
- **pl_odyssey / lighting — NOT VERIFIED (stale camera).** The ledger is internally consistent ("product-attributable … 4123 px (1.9762% of dark)") but was measured at eye `-2294.8,348.8,383.4`, while `world-geometry-hole-check.sh:167-197` now pins `PIN_CAPTURE_EYE="815.5,-147.8,-194.3"` for the same demo and tick. The gate re-derives the eye from its own trace (`lighting-dark-account-check.sh:189` "EYE=$(… grep -oE 'eye=-?[0-9.]+,-?[0-9.]+,-?[0-9.]+')"), so nothing in it detects that the camera moved. Cheapest test: re-run `lighting-dark-account-check.sh` unchanged (odyssey, `autorecord_2026-07-02_13-26-46.dem`, tick 2000) and diff the new `account.txt` against the committed one → **≈ 8–10 min**.
- **pl_odyssey / coverage table-batches — NOT VERIFIED.** The T59 fix (tiled 85 → 99) is reported, not tabled. Cheapest test: `material-coverage-check.sh` with `MAP=pl_odyssey`, `DEMO=$TF/demos/autorecord_2026-07-02_13-26-46.dem`, `TICK=2000` — this also emits the missing `tile-table-pl_odyssey.txt` → **≈ 6–8 min** (2 captures + plan dumps).
- **cp_sunshine / all four axes — NOT VERIFIED.** The map is gated (contradicting `gate_matrix.md:152`), but only for reload errors and sky faces at tick 0. Cheapest tests, in order of value: (a) `material-coverage-check.sh` with `MAP=cp_sunshine`, `DEMO=$TF/demos/SUNSHINE.dem`, `TICK=0` → tile table + batches, **≈ 4 min**; (b) `world-geometry-hole-check.sh` with the same demo/BSP, tick 0, 3 frames → hole count, **≈ 3 min**; (c) `lighting-dark-account-check.sh` with `MAP=maps/cp_sunshine.bsp` → **≈ 6 min**. Expect (a) or (b) to expose the reload failure the reliability gate documents (`gpu: post-reload lastError=0x80004005`) rather than a clean reading.
- **koth_bagel_rc12 / all four axes — NOT VERIFIED.** No gate names the map; its 31 POV demos make a run cheap but the BSP sits in `tf/download/maps/`, which none of the `--bsp` gates accepts by name. Cheapest test: `world-material-face-check.sh` with `DEMO` pointed at `$TF/demos/autorecord_2026-06-24_21-46-16.dem` (POV, 66,810,721 B, sha `7c8b1c54…`) — that gate takes no `MAP` and renders whatever the demo loads → **≈ 3 min** for fetch + pixels; a tile table needs `material-coverage-check.sh` taught an absolute BSP path (one-line change) → **≈ 4 min**.
- **koth_bagel_rc13 / all four axes — UNSUPPORTED, not fixable here.** A hole/lighting/material reading is impossible without `koth_bagel_rc13.bsp`; the honest next step is to keep it as the product's negative control (`world-material-check.sh:278-284`) and never count it as a passing map.

---

## 2. Where the evidence files disagree

1. **T56's quoted counters no longer match the files they quote.** `evidence/t56-map-audit/REPORT.md:41` quotes "product counters: distinct=148 tiled=112 vmtMissing=4 vmtNoBase=31 vtfMissing=1 fromPakfile=32" from `evidence/material-coverage/product-readings.txt`. The live file says "tiled=123 … vmtNoBase=4 … patchDeclared=30 patchResolved=30 transformRefused=16" — T59 (HEAD `788f95c`) re-measured it. Both paths named; the live file wins.
2. **Same for the refusal tally.** `evidence/t56-map-audit/REPORT.md:40` quotes "aggregate refusals: no-basetexture materials=20, vmt-missing materials=3" from `evidence/world-material-face/refusals.txt`; the live file says "materials=2 … materials=4 … materials=3" (three reasons, not two). The live file wins; the report's 20/3 was the pre-T59 product.
3. **`gate_matrix.md` says cp_sunshine has no gate.** `evidence/t56-map-audit/gate_matrix.md:152` "cp_sunshine: covered by **no gate** — no script names it" contradicts `world-render-reliability-check.sh:71` (SUNSHINE="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/SUNSHINE.dem") and `:167` "=== 1/4 reliability input: SUNSHINE.dem (cp_sunshine) ===". The script wins — but the matrix cell stays NOT VERIFIED because that gate asserts reload and sky-face readings, not any of the four axes.
4. **The lighting and material-semantics cameras are the pre-fix pose.** `evidence/t26-world-holes/analysis/PINS-T35.md` §T58 states the T35/T44 pins "were photographed from inside the world … the camera at playback tick 2000 was a different pose than the demo's: eye -2294.8,348.8,383.4". Files still carrying that pose: `evidence/t36-lighting-dark-account/frames.txt:3`, `evidence/lighting-semantics/frames.txt:4` ("A sha256=6bb4d9a7…"), `evidence/material-semantics/frames.txt`, `evidence/visual-baseline/trace-ref.txt:31897`. All four are dated 2026-10-21 20:40; the clock fix `5ee9642` landed 2026-10-24 14:58. Post-fix counter-examples: `evidence/t58-geometry/forward-verified-pose.log` (2026-10-25 17:20) and `evidence/world-material-face/trace-albedo.txt:1725` ("eye=815.5,-147.8,-194.3 … src=demo", 2026-10-25 21:06). Both cannot be the demo's tick-2000 camera.
5. **Frame bytes named "textured" are three different frames.** `PIN_TEXTURED_SHA=1b0ab468…` (`world-geometry-hole-check.sh:178`) vs "textured sha256=6bb4d9a7…" (`t36-lighting-dark-account/frames.txt:5`) vs the T56 report's "PIN_TEXTURED_SHA=ac2cfe9…" (`gate_matrix.md:109`). Each belongs to a different tree; only the first is pinned by a gate that currently runs.

---

## 3. Gates that exist and the map/tick each one pins

Read from the scripts only; nothing was executed. Resolution is not a per-gate constant: no `--capture-size` flag exists anywhere, the product default is used, and the only assert on it is `spectate-check.sh:350` "capture width … 1264x681" (`visual-baseline-check.sh:63` states the same default in a comment).

### 3a. World/map gates (the ones the matrix rests on)

| Gate | Map | Demo | Tick | Res | Pins a frame SHA? |
|---|---|---|---|---|---|
| `world-geometry-hole-check.sh` | pl_odyssey (`:930`) | `tf/demos/autorecord_2026-07-02_13-26-46.dem` (`:57`) | 2000 (`:58`) | 1264x681 (from `hole-account.txt`) | **YES** — `:178-179` `PIN_TEXTURED_SHA=1b0ab468…`, `PIN_REF_SHA=e9218c42…`, asserted at `:712-717`. The only gate in the repo that pins frame bytes. |
| `lighting-dark-account-check.sh` | `maps/pl_odyssey.bsp` (`:88`) | same odyssey demo (`:89`) | 2000 (`:127` `target=2000`; `:579` writes "tick=2000 camera=the T29 reference") | 1264x681 (`account.txt`) | no — compares its own buckets |
| `lighting-semantics-check.sh` | `maps/pl_odyssey.bsp` (`:80`) | same odyssey demo (`:81`) | 2000 (`:131`; `:136` notes it lands on 2001) | 1264x681 (`deltas.txt`) | no |
| `world-material-face-check.sh` | **none** — renders the demo's map (odyssey) | same odyssey demo (`:64`) | 2000 (`:93`) | 1264x681 (`regions.txt`) | no — asserts counters and region luminance; `:183` "tile table rows" = `plan.tiled` |
| `world-material-semantics-check.sh` | `maps/pl_odyssey.bsp` (`:137`) | same odyssey demo (`:138`) | 2000 (`:259` `--capture-tick "$target"`) | 1264x681 | no |
| `visual-baseline-check.sh` | `maps/pl_odyssey.bsp` (`:74`) | same odyssey demo (`:75`) | 2000, retried until the draw reports exactly 2000 (`:114-127`) | 1264x681 (`:63`) | no — four region pins |
| `world-near-clip-check.sh` | none (odyssey demo) | same odyssey demo (`:66`) | `$target` per capture (`:141`) | 1264x681 (`frame-hashes.txt`) | no — region/mean pins `:117-127`, dark band `:111-112` |
| `world-geometry-completeness-capture.sh` | none (odyssey demo) | same odyssey demo (`:56`) | 2000 (`:106`) | 1264x681 | no (writes `evidence/t23-world-geometry/captures`) |
| `quality-audit-check.sh` | none (odyssey demo) | same odyssey demo (`:66`) | 2000 (`:113`) | default | no (prints `frame_sha256=` at `:193`) |
| `material-coverage-check.sh` | cp_snakewater_final1 (`:82`) | testdata snakewater (`:83`) | 0 (`:96`) | default | no — prints shas (`:341`); control sha in a comment (`:387`) |
| `world-material-check.sh` | cp_snakewater_final1 (`:60`) | testdata snakewater (`:61`) | 0 (`:316`, `:431`, `:494`) | default | no |
| `world-material-effects-check.sh` | cp_snakewater_final1 (`:42`) | testdata snakewater (`:43`) | 0 (`:172`) | default | no — "off != on" then "on == repeat", runtime-computed |
| `terrain-blend-check.sh` | cp_snakewater_final1 (`:56`) | testdata snakewater (`:57`) | 0 (`:66`) | default | no — records `frames.sha256` (`:603`), asserts none |
| `world-render-reliability-check.sh` | **cp_sunshine** (`:71`) | `tf/demos/SUNSHINE.dem` (`:71`) | 0 (`:104` `--start-paused --capture-tick 0`) | not asserted | no — asserts reload errors and six sky-face lines |
| `t2-visual-matrix.sh` | none (testdata snakewater, `:63`) | testdata snakewater (`:63`) | caller-supplied `$tick` (`:102`) | default | no — a matrix driver, not a gate |
| `frame-capture-check.sh` | koth_bagel_rc13 (testdata demo, `:76`) | `4a9bfb92…-koth_bagel_rc13.dem` | 0 (`:284`) | default | no |
| `playback-control-check.sh` | koth_bagel_rc13 (testdata demo, `:59`) | same rc13 demo | n/a — **captures no frames**, drives `--camera-script` seeks | n/a | n/a |

### 3b. Gates that capture frames for player/HUD axes (not the map matrix)

`spectate-check.sh:57` snakewater testdata demo, tick 19700 (`:60`), asserts 1264x681 (`:350`) · `entity-material-check.sh:57` snakewater testdata, tick 3000 (`:70`) · `hud-check.sh:64/:88` snakewater testdata, tick 0 · `hud-text-check.sh:54/:68` snakewater testdata, tick 0 · `outline-check.sh:83`, tick = `$FIRST` (`:384`) · `viewmodel-contract-check.sh:133/:160` snakewater testdata, tick 0 · `weapon-material-check.sh:76/:109` odyssey POV, tick 2000, pins the **recording** sha `POV_SHA=c8a58501…` (`:78`, asserted `:321`) · `weapon-body-check.sh:102/:131` same, `POV_SHA` asserted `:377-378` · `logstf-check.sh:57` tick 600 · `game-event-check.sh:40` `SAME_TICK_SHA=9d6817c9…` asserted `:273` (a same-tick equality, not a stored world frame). None of these asserts 1264x681 or a world frame hash.

**Only one gate pins a world frame's bytes; every other "frame identity" assertion in the repo is computed inside the run that asserts it** (`viewmodel-apply-check.sh:548` "the applied frame repeats byte for byte", `viewmodel-range-check.sh:524`, `viewmodel-presentation-check.sh:437`), so it proves determinism, not regression against a stored picture.

---

## 4. Cross-cutting gaps

**Maps with no gate at all**
- `koth_bagel_rc12` — no script names it (`gate_matrix.md:155-156`); 31 POV demos and a BSP in `tf/download/maps/` are waiting, and no `--bsp` gate accepts that path.
- `koth_bagel_rc13` — named only as the "map not installed" negative control (`world-material-check.sh:278-284`); no BSP, so no axis is reachable.
- `cp_sunshine` — has a gate, but only for reload errors and sky faces; it covers **none** of the four axes, and the T56 audit's "no gate" claim is stale on both sides.

**Axes covered on exactly one map**
- Geometry hole account + cull control: **pl_odyssey only** (`world-geometry-hole-check.sh:57-58`, `:930`).
- Lighting dark-material account: **pl_odyssey only** (`lighting-dark-account-check.sh:88-89`).
- Material tile table / batch plan: **cp_snakewater_final1 only** — one file, `evidence/material-coverage/tile-table-cp_snakewater_final1.txt`; the odyssey tiled=99 reading is report prose.
- Material fetch + pixels: two maps (snakewater at tick 0 via `world-material-check.sh`/`material-coverage-check.sh`; odyssey at tick 2000 via `world-material-face-check.sh`), **no map at both ticks**, so no map has a tick-0 and tick-2000 material reading to compare.

**Two postures, one tree**
- Tick 0 (paused, signon pose) is where every snakewater material gate sits; tick 2000 is where every odyssey gate sits. No gate runs the same axis on two maps, and no gate runs two ticks of one map — so "materials are right" is asserted per-map, never across the map set the T56 audit selected.
- The pre/post clock-fix split (§2.4) means the lighting, lighting-semantics, material-semantics and visual-baseline pins describe a camera the current binary no longer produces. T58 re-baselined geometry only (17:20 on 2026-10-25); T59 re-baselined snakewater materials (20:27). Nothing re-baselined the lighting axis.

**Owners this matrix implies (not assigned here)**
- lighting axis at the tick-verified pose — needs a re-run of `lighting-dark-account-check.sh` + `lighting-semantics-check.sh`, or an explicit "measured at the old pose, do not cite as current".
- third-map coverage — cp_sunshine has a demo, a BSP and a gate that does not look at the world.
- bagel_rc12 reachability — one absolute-path knob in the two `--bsp` gates.

_No PASS/FAIL in this document was produced by executing anything; the only failure-shaped readings quoted (`forward-old-pins-red.log` "FAIL pixels the cull removed from the control face: 52788 != 33167") are pre-fix demonstrations committed by T58, not current gate output._
