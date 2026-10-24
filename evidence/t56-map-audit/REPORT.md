# T56 — multi-map audit (static phase) — REPORT

## Goal / non-goal

Goal: prove, without running anything, what a multi-map visual audit can actually cover on this
machine — which maps are demo-reachable, what materials they resolve to, what the existing gates
assert and which map each one can serve, what a per-item problem ledger must contain, and the exact
runtime commands a later phase should schedule.
Non-goal: this phase did **not** build, launch the product or any probe, run captures, mutate the
product, or take the host lock. Anything needing a run is written down in
`runtime_proposal.md` as a proposal.

## Identity

| item | value | source |
|---|---|---|
| repo | D:/TF2_Native_Test | cwd |
| branch | p0-entity-protocol | git rev-parse --abbrev-ref HEAD |
| HEAD | 99eeea294cbe883e4c3e6b4b82c83de45d2ab926 (short 99eeea2) | git rev-parse |
| working tree | only untracked evidence/ dirs; no product file modified by this task | git status --porcelain |
| TF root | D:/SteamLibrary/steamapps/common/Team Fortress 2/tf | gate scripts |
| product binary | native/build-nmake/tf2_demo_native.exe sha256 f454718b... (present, not launched) | input_hashes.sha256 |

## What was inspected (file:line)

| file:line | what it establishes |
|---|---|
| native/src/map_lookup.cpp:75-77 | lookup order maps/ -> download/maps/ -> vpk (so download/maps is a real map store) |
| material-coverage-check.sh:80 | TICK=0 for the snakewater SourceTV demo |
| material-coverage-check.sh:217-219 | --start-paused on a POV demo hangs at tick 0 waiting for tick 2000 (measured rc=124 after 400 s) |
| material-coverage-check.sh:226-231 | builds the product command with pause_args=(--start-paused) |
| world-material-check.sh:316-317 | snakewater capture at --capture-tick 0 --world-sky |
| world-material-face-check.sh:89-96 | odyssey --capture-tick 2000 + --world-face-trace capture helper |
| visual-baseline-check.sh:108-125 | --capture-tick N = first draw with tick >= N; retry until the draw reports exactly 2000 |
| world-geometry-hole-check.sh:58,211-212 | odyssey TICK=2000 capture |
| lighting-semantics-check.sh:131 | odyssey --capture-tick 2000 |
| world-material-check.sh:278-283 | koth_bagel_rc13 used as the "map not installed" negative control |
| evidence/world-material-face/trace-albedo.txt | real odyssey face trace: map lookup line 2, capture armed 42482.7, vis eyeMap line, face-trace rows |
| evidence/world-material-face/regions.txt:1 | capture geometry 1264x681, background (36,28,24) |
| evidence/world-material-face/refusals.txt:1-2 | aggregate refusals: no-basetexture materials=20, vmt-missing materials=3 |
| evidence/material-coverage/product-readings.txt:1 | product counters: distinct=148 tiled=112 vmtMissing=4 vmtNoBase=31 vtfMissing=1 fromPakfile=32 |
| evidence/world-material/real-cp_snakewater_final1.txt:1 | lightmap: samples 2793687, faces 11527, atlas 4096x1156, sky sky_well_01, cubemaps 33, vis cluster-pvs 2940 |
| evidence/vpk-build/real-archives.txt | pak01_dir.vpk OPEN FAILED (absent) |

## Deliverables in this directory

| file | what |
|---|---|
| inventory.md | demo corpus, BSP census, VPK index, material reachability, selected maps, gaps |
| reachability.tsv | 6 map/demo rows: BSP + demo path/bytes/sha256, recording type, ticks, frames, demo counts |
| bsp_inventory.tsv | 335 installed BSPs: map, path, bytes, mtime, demos on map, sha256 for selected |
| map_material_reachability.tsv | 561 rows (4 maps): material -> vmt/vtf store, sha256, basetexture, include |
| map_material_summary.json | per-map counts (materials/missing vmt/no-base/missing vtf/pak-only) |
| bsp_pakfile.json | BSP lump-40 ZIP entry counts per map |
| vpk_inventory.json | VPK v2 tree/volume inventory |
| demos_selected.tsv, demo_headers_tfdemos.jsonl | demo headers (1748 rows) |
| input_hashes.sha256 | 693 lines: demos, BSPs, VPK _dir files, gate scripts, 5 binaries, per-material VMT/VTF |
| gate_matrix.md | all six gates: asserts, map/demo/tick/camera file:line, duration basis, limitations, recommendation |
| ledger_schema.json | 51-column machine-readable ledger schema, per-column source/status, joins, rejection rules, gaps |
| ledger_template.tsv | 3 real ledger rows emitted from the odyssey trace |
| face_trace_to_ledger.py | trace -> ledger rows + sample validator (map/tick/pose/rows), --selftest (4/4 fixtures pass; template header == schema order, 51/51 columns) |
| runtime_proposal.md | per-map tick/camera/command, durations, lock need, serial schedule |
| vpk_materials.py, gen_reachability.py, stats.py, trace_durations.py, make_fixture.py | the static tooling that produced the above |

## Key findings

1. **The demo corpus is far larger than the task premise.** tf/demos holds 3805 files, 1748 .dem,
   146 distinct maps; only D:/TF2_Demo_Player/testdata/demos has exactly 2 demos
   (cp_snakewater_final1 and koth_bagel_rc13, both SourceTV).
2. **All 7 SourceTV maps have BSPs on disk.** cp_sunshine/cp_reckoner in tf/maps; koth_ashville_final1,
   cp_gullywash_f9, cp_process_f12, koth_product_final, cp_sultry_b8a in tf/download/maps, which
   native/src/map_lookup.cpp:75-77 proves is a real lookup store.
3. **The BSP lump-40 pakfile is required for material resolution.** Without it, 38/155 snakewater,
   69/130 odyssey and 44/102 bagel_rc12 materials look missing. With it, snakewater resolves to
   5 missing VMT / 31 no-base / 1 missing VTF — matching the product's own counters
   (vmtNoBase=31, vtfMissing=1, fromPakfile=32 exact; vmtMissing=4 vs static 5 because one
   material is not visible in that frame).
4. **PROPS/HAZARDSTRIP001A is missing on all three selected maps** (snakewater, odyssey, sunshine):
   one absent stock VMT affects every map. Per-map extras: snakewater DEV/DEV_MEASUREGENERIC01,
   GLASS/GLASSWINDOW008D, DEV/REFLECTIVITY_50, DEV/REFLECTIVITY_40; odyssey METAL/METALRAIL013C,
   METAL/METALGRATE013A; sunshine DEV/REFLECTIVITY_10B.
5. **Third map = cp_sunshine.** SourceTV demo + BSP in tf/maps + 66 demos; 174 materials (largest),
   62 pak-only — a materially different distribution from snakewater (155) and odyssey (130).
   **No existing gate targets it**, so it needs a new baseline capture, not a gate pass.
6. **Ledger feasibility is proven on real data**: 51-column schema, 3 real rows emitted from
   evidence/world-material-face/trace-albedo.txt, and the validator self-test is 4/4
   (positive accepted; missing-trace, wrong-map, wrong-pose all refused for the right reason).
7. **Trace gaps that must be recorded, not fabricated**: no camera pitch/yaw anywhere (only
   `scene: camera mode/follows/freeze` and `gpu: world vis ... eyeMap=`); no per-item lightmap page
   (only an aggregate `gpu: world lightmap atlas=1`); no per-item cull state (needs the 4-combo
   vis/cull matrix); no per-item depth-match guarantee.

## Material summary (from map_material_summary.json)

| map | materials | missing VMT | no basetexture | missing VTF | only in BSP pak | vmt source |
|---|---|---|---|---|---|---|
| cp_snakewater_final1 | 155 | 5 | 31 | 1 | 32 | vpk 118 / pak 32 |
| pl_odyssey | 130 | 3 | 37 | 0 | 65 | vpk 62 / pak 65 |
| cp_sunshine | 174 | 2 | 61 | 0 | 62 | vpk 110 / pak 62 |
| koth_bagel_rc12 (alt) | 102 | 0 | 22 | 0 | 43 | vpk 59 / pak 43 |

## Gate -> map recommendation (details in gate_matrix.md)

| gate | recommended map | why |
|---|---|---|
| visual-baseline-check.sh | pl_odyssey | map/demo pinned at :74-75, tick 2000 |
| world-material-check.sh | cp_snakewater_final1 | map pinned at :60, tick 0 at :316 |
| material-coverage-check.sh | cp_snakewater_final1 (primary) | map pinned at :66, TICK=0 at :80 |
| world-material-face-check.sh | pl_odyssey | renders the demo's map, tick 2000 at :92 |
| world-geometry-hole-check.sh | pl_odyssey | renders the demo's map, tick 2000 at :58; heaviest (18 captures, ~15-20 min) |
| lighting-semantics-check.sh | pl_odyssey | map pinned at :80, tick 2000 at :131 |
| (none) | cp_sunshine | no gate names it; new baseline needed |

## Runtime sample selection for B (pl_odyssey) — Lead m00574

The Lead's T64 oracle measured that POV demo `autorecord_2026-05-17_11-57-32.dem` is a 6.555 s
connection fragment (437 ticks, 429 frames, 1,256,764 B, no combat/round events); it must not be a
payload reference sample. Verified in demo_headers_tfdemos.jsonl and **it was never this proposal's
sample** (B used `autorecord_2026-07-02_13-26-46.dem`, 39,122 ticks / 586.83 s, rank 14 of 42).

Rule applied (from the Lead): sort the 42 map=pl_odyssey demos by header playbackSec descending,
take the top 5, choose the longest. Full table in `odyssey_demo_candidates.tsv`; the top 5 are
82.1 MB/147,729 ticks/2215.9 s, 55.6 MB/98,878/1483.2 s, 37.6 MB/73,796/1106.9 s,
38.3 MB/67,381/1010.7 s, 29.8 MB/59,950/899.3 s.

**B now uses rank 1, `tf/demos/autorecord_2026-08-09_14-48-38.dem`** (sha256 a2d92bf5ecd329448d8c822915424e44856848372173e1c64d00fc64598bae5f): longest playback of the
42, most ticks, so tick 2000 falls inside live gameplay. `autorecord_2026-07-02_13-26-46.dem`
stays as the fallback because it is the demo the gate scripts' tick-2000 recipe was demonstrated
on (world-material-face-check.sh:89-96, evidence/world-material-face/trace-albedo.txt).
`reachability.tsv` keeps the gate-demonstrated sample; `runtime_proposal.md` §B0 is authoritative
for the runtime sample.

## Gaps / NOT VERIFIED

- **Nothing was executed.** No gate, no product, no probe, no capture, no build, no lock. Every
  duration in gate_matrix.md and runtime_proposal.md is an estimate or derived from trace
  timestamps in existing evidence, never from a run in this phase.
- **koth_bagel_rc13 BSP is missing** from tf/maps and tf/download/maps (gen_reachability.py:
  `bagel_rc13 bsp: (None, None)`), so its testdata demo cannot be pixel-audited; it is the
  product's negative control (world-material-check.sh:278-283).
- **pak01_dir.vpk is absent** (evidence/vpk-build/real-archives.txt) although
  evidence/resource-reachability/*.json expects 5 archives; only 4 open.
- **The product does not follow `include`** when counting no-basetexture materials, so its
  vmtNoBase counter includes patch materials that do resolve via include (27/30/20 per map).
- **POV camera cannot give a stable scene camera**: it records the player's first-person eye, which
  moves with the player. It is deterministic per tick (fixed tick -> fixed pose), which is what the
  gates rely on, but it is not a static/overhead camera. Only SourceTV demos + --start-paused give
  a frozen first frame, still from the demo's own camera.
- **Third-map gate coverage is zero**: sunshine must be measured with the new capture in
  runtime_proposal.md §C before anyone claims gate parity across three maps.
- **Runtime phase is blocked (Lead, m00561)**: a product tick-origin defect (entity history keyed by
  server net_Tick, scene queries keyed by signon entry tick; offsets 67 ticks on snakewater, 56146 on
  bagel) means any camera capture can show the wrong tick until fixed. See runtime_proposal.md
  "BLOCKED". Existing non-zero-tick captures are tick-unverified.

## Next steps

1. Lead schedules the four forward runs in runtime_proposal.md serially (all need the host lock;
   ~4-8 min nominal, ~20 min worst case with retries).
2. Run `face_trace_to_ledger.py --trace <new trace> --map <map> --tick <t>` on each new trace; the
   validator refuses any sample that lacks map/tick/pose/rows.
3. Fill `vmt_sha256`/`vtf_sha256` by joining map_material_reachability.tsv on (map, material).
4. Playbook entry for the BSP lump-40 pakfile pitfall: see the playbook repo commit (rule 0).
