# T56 multi-map audit — static inventory

Repo: D:/TF2_Native_Test, branch p0-entity-protocol, HEAD 99eeea2 (working tree has only untracked evidence/ dirs; no product file touched by this task).
TF root: D:/SteamLibrary/steamapps/common/Team Fortress 2/tf.
Everything below is static: file reads and parsing only. Nothing was built, launched or captured.

## 1. Demo corpus (what actually exists)

| item | value | source |
|---|---|---|
| tf/demos files | 3805 | dir walk |
| tf/demos *.dem | 1748 | dir walk |
| distinct maps in those demos | 146 | demo_headers_tfdemos.jsonl |
| demos with SourceTV client | 9 over 7 maps | demo_headers_tfdemos.jsonl |
| testdata demos (D:/TF2_Demo_Player/testdata/demos) | 2 | dir list |
| testdata maps | cp_snakewater_final1 (STV, 64352 ticks), koth_bagel_rc13 (STV, 77466 ticks) | demo headers |

The task list's premise that the demo folder holds only two .dem files is **false for tf/demos** (1748 files) and true only for D:/TF2_Demo_Player/testdata/demos (2 files). Both were verified.

Per-map demo counts (tf/demos): cp_snakewater_final1 61, pl_odyssey 42, cp_sunshine 66 (1 STV), cp_reckoner 17 (1 STV), koth_bagel_rc12 31, koth_bagel_rc13 0.

## 2. Installed maps (BSP)

| item | value | source |
|---|---|---|
| BSP files | 335 | bsp_inventory.tsv (335 rows) |
| in tf/maps | 254 | bsp_inventory.tsv path |
| in tf/download/maps | 81 | bsp_inventory.tsv path |
| total bytes | 14569.7 MB | sum of bsp_inventory.tsv |
| BSPs with >=1 demo in tf/demos | 147 | bsp_inventory.tsv demos_in_tf_demos |
| BSPs with no demo | 188 | bsp_inventory.tsv |

Map lookup order (product): native/src/map_lookup.cpp:75-77 — `maps/<file>` (store "maps"), then `download/maps/<file>` (store "download-maps"), then vpk. So a map in download/maps is loadable.

### Demo-reachable map set (BSP + demo on disk)

All 7 SourceTV maps have a BSP: cp_sunshine and cp_reckoner in tf/maps; koth_ashville_final1 (39,970,060 B), cp_gullywash_f9 (13,231,135 B), cp_process_f12 (18,558,778 B), koth_product_final (18,143,384 B), cp_sultry_b8a (38,394,319 B) in tf/download/maps. This corrects the assumption that only 2 of 7 have BSPs.

## 3. VPK inventory

| archive | entries | materials | bytes | sha256 |
|---|---|---|---|---|
| tf2_misc_dir.vpk | 104137 | 26060 .vmt | 5,402,349 | b1e36819d6ae5f5f7a97831265d97f16901d8d72a218fb7742cb55e6fd92eda3 |
| tf2_textures_dir.vpk | 29639 | 29639 .vtf | 1,974,706 | a65f0dbe4a91847a2b8d8be0ff32f1105faf11fa3eaceb86118f2e83ecd9db7b |
| tf2_sound_misc_dir.vpk | 3230 | - | - | dfbcc92beb6e9dd86994ad37be8bc7d8d7da66d01d2f4d441accdab776894bd9 |
| tf2_sound_vo_english_dir.vpk | 12728 | - | - | f9b0518925cd7b7b3b4373214e8fc9b8552a631bbe43661d8ff111d1fc539076 |

Volumes: 28 tf2_misc_0*.vpk, 112 combined textures/sound volumes (all VPK v2, sig 0x55aa1234; header 28 bytes).
GAP: pak01_dir.vpk does not exist in tf/ (evidence/vpk-build/real-archives.txt: 'pak01_dir.vpk: OPEN FAILED'), although evidence/resource-reachability/*.json expect 5 archives.

Loose/other material sources: tf/materials 3 vmt / 1 vtf; tf/download/materials 2810 vmt / 2723 vtf; tf/custom 381 vmt / 396 vtf.

## 4. Material reachability per map (561 rows, map_material_reachability.tsv)

| map | materials | missing VMT | no $basetexture | missing VTF | only in BSP pak | vmt source |
|---|---|---|---|---|---|---|
| cp_snakewater_final1 | 155 | 5 | 31 | 1 | 32 | vpk 118 / pak 32 |
| pl_odyssey | 130 | 3 | 37 | 0 | 65 | vpk 62 / pak 65 |
| cp_sunshine | 174 | 2 | 61 | 0 | 62 | vpk 110 / pak 62 |
| koth_bagel_rc12 | 102 | 0 | 22 | 0 | 43 | vpk 59 / pak 43 |

Cross-map: PROPS/HAZARDSTRIP001A is missing on snakewater, odyssey and sunshine — one absent stock VMT hits every selected map. snakewater also misses DEV/DEV_MEASUREGENERIC01, GLASS/GLASSWINDOW008D, DEV/REFLECTIVITY_50, DEV/REFLECTIVITY_40; odyssey misses METAL/METALRAIL013C, METAL/METALGRATE013A; sunshine misses DEV/REFLECTIVITY_10B.
No-$basetexture materials are mostly `patch` shaders inheriting the base via `include` (27/30/20 rows resolved through include) plus `Water` shaders; they are a semantics limitation, not missing assets.

### The BSP pakfile is a required resolution path

BSP lump 40 (LUMP_PAKFILE) is a ZIP archive. Without it, 38/155 snakewater, 69/130 odyssey and 44/102 bagel_rc12 materials look missing. Evidence: bsp_pakfile.json (snakewater 21,089,341 B / 3354 entries; odyssey 87,721,634 B / 4941; bagel_rc12 18,036,915 B / 3707; all begin PK\x03\x04). This is the playbook pitfall recorded separately.

## 5. Selected maps for the audit

1. cp_snakewater_final1 — 5CP; SourceTV demo in testdata; BSP 59,590,420 B sha f09d406d...; proven capture recipe --start-paused tick 0 (material-coverage-check.sh:80, world-material-check.sh:316).
2. pl_odyssey — payload; POV demo; BSP 131,192,482 B sha 3e41e132...; proven recipe no --start-paused, tick 2000 (world-material-face-check.sh:64, world-geometry-hole-check.sh:58).
3. cp_sunshine — 5CP; SourceTV demo SUNSHINE.dem (128432 ticks); BSP 30,319,857 B sha ca9ad7d0...; 174 materials (largest), 62 pak-only — different material distribution from snakewater/odyssey. **No gate script currently targets it** (gap; see gate_matrix.md and runtime_proposal.md).

Alternatives kept documented: cp_reckoner (STV review12.dem, 45,759,167 B), koth_bagel_rc12 (POV, BSP only in download/maps).

## 6. Gaps (NOT VERIFIED / missing)

- koth_bagel_rc13: testdata demo exists but the BSP is absent from tf/maps and tf/download/maps (gen_reachability.py: 'bagel_rc13 bsp: (None, None)'). Its demo cannot be pixel-audited; it is the product's negative control (world-material-check.sh:278-283).
- pak01_dir.vpk missing (see above).
- PROPS/HAZARDSTRIP001A missing on all three selected maps.
- Product does not follow `include` when counting no-basetexture materials, so its vmtNoBase counter (31 for snakewater) includes patch materials that do resolve.
- POV camera semantics: a POV demo records the player's own first-person eye — the camera follows that player, so it is deterministic per tick but not a static/stable scene camera over time. SourceTV demos give an observer camera; the product's trace shows `scene: camera mode=first-person target=1 follows=<n>` for both kinds. The odyssey POV at tick 2000 has a fixed eyeMap=(-2423.6,5566.0,-1280.0) because the tick is fixed, which is reproducible; a "stable camera" in the scene sense is not available from a POV demo.
