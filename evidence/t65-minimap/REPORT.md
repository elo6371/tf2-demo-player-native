# T65 — SourceTV minimap: data & projection audit (static phase)

| field | value |
|---|---|
| task | shared task-5 (T65), revision 2 |
| owner | t54-verify |
| tree | D:/TF2_Native_Test, branch p0-entity-protocol |
| HEAD | 99eeea294cbe883e4c3e6b4b82c83de45d2ab926 |
| date | 2026-10-24 (Asia/Shanghai) |
| phase | STATIC ONLY — no product build, no product launch, no probe run, no host lock |
| verdict | projection verified for pl_badwater (Payload) and cp_snakewater_final1 (CP); koth_bagel_rc13 NOT VERIFIED (BSP absent); runtime player coverage and UI acceptance NOT RUN |

## 1. Goal and non-goals

Goal (T65 + T69): prove that a BSP map can be turned into a simple top-down base map and that a
player's world origin maps to a minimap pixel reversibly, with an explicit error ledger, explicit
multi-level handling, and defined interaction acceptance points.

Non-goals: implementing the minimap in the product; changing product source, gates or config;
running the product or any probe; judging visual art quality.

## 2. Inputs and identity

Maps audited:

| map | type | BSP path | bytes | sha256 | status |
|---|---|---|---|---|---|
| koth_bagel_rc13 | KOTH | (absent) | - | - | UNAVAILABLE — no rc13 BSP in this install |
| cp_snakewater_final1 | CP | tf/maps/cp_snakewater_final1.bsp | 59,590,420 | f09d406d2bbe397c8f9df9ca17f11b36b53d40fcf57e3f1532ec9af811b2a1eb | VERIFIED |
| pl_badwater | Payload | tf/maps/pl_badwater.bsp | 16,619,247 | 68654502abf02c205862fa034403f581a9df9d3131793e7fbf7bef926c61ac48 | VERIFIED |

Demos (identity read with T56 demo_header.py; no replay):

| demo | map | bytes | sha256 | playback s | ticks | frames |
|---|---|---|---|---|---|---|
| 4a9bfb...match-20260927-0239-koth_bagel_rc13.dem | koth_bagel_rc13 | 46,921,111 | 03529698db749e77a1df1c476c1c0cc06847e1173b5366a0602f1e15161b5436 | 1161.99 | 77,466 | 73,118 |
| bb841c...matcha-20260927-1347-cp_snakewater_final1.dem | cp_snakewater_final1 | 51,834,617 | 308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f | 965.28 | 64,352 | 64,330 |

Demo map name equals the BSP map name in both verified cases (identity check V6).
The T63 KOTH target map is koth_bagel_rc13; the T63/T65 Payload target is represented by pl_badwater.
cp_snakewater_final1 is a CP map and is the second map for which both a BSP and a SourceTV demo exist.

## 3. Availability findings

### 3.1 koth_bagel_rc13 BSP is missing (and must not be drawn as a blank map)

tf/maps contains koth_bagel_event.bsp (23,969,169 B) only; tf/download/maps contains
koth_bagel_rc10.bsp, koth_bagel_rc11.bsp, koth_bagel_rc12.bsp; a targeted whole-disk search finds no
*bagel_rc13* file. Consequence: the T63 KOTH demo cannot be projected at all in this install. The
UI rule "never draw an unavailable map as a seemingly-correct blank minimap" therefore has a concrete
instance: koth_bagel_rc13 must render a reason card (map name + missing BSP), not an empty canvas.

### 3.2 No official overview/radar data exists in this install

- tf/materials/overviews and tf/resource/overviews do not exist on disk (the TF2 install is trimmed).
- VPK scan (tools/vpk_search.mjs) of tf2_misc_dir.vpk (104,137 entries) and tf2_textures_dir.vpk
  (29,639 entries): materials/overviews holds only tc_hydro.vtf, caparrows.vtf, redteam.vtf,
  blueteam.vtf — HUD-level icons, no per-map overview VTF and no resource/overviews/*.txt.
- cp_snakewater_final1's pakfile (3,354 entries) has no overview/radar/minimap entry.
- The only map-specific .rad file in tf/maps is " /cp_snakewater_final1.rad" (379 B) whose content is
  "forcetextureshadow models/props_gameplay/security_fence512.mdl" — a radiosity (lighting) file, not
  a radar/overview definition.

Conclusion: the base map must be derived from BSP geometry (this audit) or from a runtime screenshot
(proposal P5). No official asset can be shipped.

### 3.3 nav files exist but are not parsed

tf2_misc_dir.vpk contains maps/pl_badwater.nav (982,935 B) and maps/cp_snakewater_final1.nav
(1,056,016 B). Extracted to /tmp for inspection: both headers have magic 0xFEEDFACE, version 16,
subVersion 2, and their saveBSPFileSize equals the matching BSP byte size exactly (16,619,247 /
59,590,420), so nav identity matches BSP identity. The body format was not reliably decoded (a first
record looks like a rectangle of floats, but a 96-byte stride immediately yields garbage; no
0,1,2,3 id sequence was found). Nav is therefore available-but-unparsed: it is NOT used as evidence
here and the authoritative playable area remains NOT VERIFIED (proposal P4).

## 4. Base map derivation (BSP -> floor raster)

Algorithm (tools/bsp_geometry.py, Python + Pillow, static, read-only):

1. Parse BSP v20 lumps 0/1/2/3/6/7/12/13/14/43/44; LZMA lumps ("LZMA" magic + sizes + 5 props +
   FORMAT_RAW LZMA1) are decompressed in place.
2. Floor face test: plane normal z >= 0.7 and material not sky / water / tools / dev.
3. Rasterize floor faces onto a 32-unit grid; at each cell center interpolate z barycentrically.
4. Multi-level: sort sampled z inside a cell; adjacent samples differing by more than 128 units count
   as separate layers.
5. z-filter: histogram gameplay-entity z in 256-unit bins, take the densest bin as the main band, keep
   cells whose center z is inside [bandLo-512, bandHi+1024]; filter the point set the same way before
   computing the core bbox.
6. Connected components (8-connected): keep components intersecting the gameplay bbox; dropped cells
   are recorded, never silently discarded.
7. Frame: gameplay-entity bbox + 1536 units margin, intersected with the BSP floor extent; project to
   1024x1024 with equal aspect, 2% padding, +Y up.

### 4.1 Three silent defects found and fixed (the audit's main lesson)

(a) Raw min/max is not the playable area. pl_badwater carries a 6,841-cell ground slab at z=-2452
    (NATURE/BLENDGROUNDTOGRAVEL005 6708 cells, CONCRETE/CONCRETEFLOOR003 100, NATURE/ROCKWALL013 29,
    WOOD/WOOD_BRIDGE001 4), 2,580 units below every gameplay point. With raw floor min/max the real
    map is squeezed into a corner of the canvas — visible in the ASCII preview before the fix.

(b) Entity z min/max includes the 3D skybox. Both maps contain 12 team-0 spawn copies at
    z = -14,592..-14,665 (the 3D skybox). A z-filter derived from raw entity min/max would cover the
    skybox and become a no-op; the histogram-band rule in step 5 is the fix.

(c) A "constant" frame is an assumption. Using raw floor extent leaves 2,176 units of empty canvas on
    badwater; using the reachable-flood bbox would clip legitimate map edges. The frame is therefore
    derived from named inputs (gameplay entity bbox + margin, clamped) and the derived numbers are
    printed (see ledger), so a different entity set is visible instead of silent.

Each defect was invisible in the tool's own output; each was caught only by an independent reading
disagreeing with it (ASCII preview, entity z histogram, raw-vs-gameplay bbox). This is the
constants-are-assumptions lesson applied concretely.

## 5. Geometry results

| item | cp_snakewater_final1 | pl_badwater |
|---|---|---|
| faceCount (all faces in lump) | 16,103 | 11,357 |
| floorFaces | 4,489 | 3,008 |
| vertexCount | 33,784 | 22,054 |
| skipped faces | not_floor 10,095; tools 647; water 182; sky 690 | not_floor 7,184; water 111; tools 465; sky 589 |
| entityCount | 986 | 999 |
| playableBounds (floor cell extent) | x[-5760, 6816] y[-4832, 5248] | x[-2944, 4576] y[-5120, 4480] |
| zRange | [-312, 951.69] | [-128, 896] |
| zBands (2/20/40/60/80/98%) | [-308, -176, -108, -2, 181.47, 520] | [-28, 128, 128, 256, 448, 896] |
| cellCount | 57,812 | 43,601 |
| multiLevelCells | 16,789 (29.0%) | 10,621 (24.4%) |
| levelHistogram | {1:41023, 2:10368, 3:4579, 4:1683, 5:158, 6:1} | {1:32980, 2:7950, 3:2577, 4:94} |
| components after filtering | 1 | 1 |

worldModel header data is recorded but must not be used for framing: cp_snakewater_final1 worldModel
mins(-14924, -4856, -14728) maxs(6824, 15452, 1792) numfaces=12471 (model 0 span; the 16,103 faces are
the whole lump); pl_badwater worldModel mins(-14924, -5136, -14728) maxs(4624, 15452, 2064)
numfaces=9830. The y max 15,452 and z min -14,728 are the 3D skybox.

## 6. Projection and error budget

Formula: px = (x - minX) * scale; py = (maxY - y) * scale (y flipped so +Y world is up).
Inverse: x = minX + px / scale; y = maxY - py / scale.

| item | cp_snakewater_final1 | pl_badwater |
|---|---|---|
| minX / maxX | -6011.52 / 7067.52 | -3094.40 / 4726.40 |
| minY / maxY | -6331.52 / 6747.52 | -3789.40 / 4031.40 |
| scale (px/unit) | 0.0782932081 | 0.1309328969 |
| units per pixel | 12.7725 | 7.6375 |
| frame (units) | x[-5760, 6816] y[-4188, 4604] | x[-2944, 4576] y[-3424, 3666] |
| cells outside frame | 2,348 | 6,629 |
| cells outside canvas | 0 | 4,723 |
| round-trip max abs error | 6.82e-13 world units | 4.55e-13 world units |
| half-pixel quantization | 6.386 world units | 3.819 world units |

Control-point pixel coordinates (full table in projection_ledger.md):

- cp_snakewater_final1: cp_blu1 (156.27, 368.02), cp_blu2 (352.28, 347.27), cp_3 (512.00, 512.00),
  cp_red2 (671.72, 676.73), cp_red1 (866.83, 656.06).
- pl_badwater: cap_1 (410.92, 689.41), cap_2 (245.68, 646.73), cap_3 (186.50, 270.43),
  cap_4 (417.73, 415.24).

Orientation: TF2 convention +X east, +Y north; image up = +Y.
- snakewater: BLU (team 3) centroid x=-2373 (west), RED (team 2) centroid x=+3429 (east); CP sequence
  blu1 -> blu2 -> cp_3 -> red2 -> red1 advances along +x.
- badwater: BLU (team 3) centroid y=-1202 (south), RED (team 2) centroid y=+70 (north); cap_1
  (y=-1234) -> cap_4 (y=+860) advances along +y.

Clipped cells: badwater has 4,723 floor cells outside the 1024 canvas (10.8%), all beyond the frame by
construction; if a player projects outside [0,1024] the UI must clamp the dot to the edge and show an
off-map indicator (ledger section 7).

## 7. Player position data — static audit

Full detail in player_position_coverage.md. Summary:

- Origin extraction exists: entity_model.cpp:87-126 (VectorXY x/y + sibling Float "m_vecOrigin[2]";
  missing sibling => complete=false, diagnostic "origin-z-sibling-missing").
- Duplicate origins: DT_TFLocalPlayerExclusive (full precision) vs DT_TFNonLocalPlayerExclusive
  (quantized); ranking by lastWriteTick then rank then name (entity_model.cpp:34-65). The two copies
  can differ by ~4000 units on bagel (comment at entity_model.cpp:28-38) — using the wrong one puts a
  dot on the wrong side of the map.
- No life state is read by the product (grep lifeState/m_lifeState in native/src+native/include = 0
  hits), but the wire send table for CTFPlayer (class id 247) does carry DT_BasePlayer.m_lifeState
  (UnsignedInt, slot 830) and DT_BasePlayer.m_iHealth (NormalVarInt, slot 19) — so death/respawn is
  measurable through the generic property dump even though the product never reads it
  (evidence/animation-availability/oracle-all-classes.txt:30035, 30055, 30866).
- Leave keeps class and last state (demo_header.cpp:1274-1277), so a player who left PVS retains a
  stale origin; Delete clears the slot and increments the generation, Enter increments it again
  (demo_header.cpp:1278-1345). Entity handle = 11-bit index + 10-bit serial, serial "decoded but not
  validated here" (entity_model.cpp:140-148) and not stored in EntityState — so handle reuse cannot be
  observed with the current probe (gap G5, proposal P2).
- Runtime measurement is feasible with the existing test-only probe entity_model_probe.exe:
  --trajectory N gives demo-wide aggregate coverage counters (samples, entities, noOriginProperty,
  zSiblingMissing, minZ/maxZ, digest); --rendered --trajectory-at <ticks> dumps per-entity rendered
  origins with class and diagnostic. No source change is needed for M1/M2/M3/M5 (proposal P1).

## 8. Interaction acceptance

Defined in interaction_acceptance.md (IA-1..IA-12) from T69/T66. Status: NOT RUN — the feature does
not exist (grep minimap/radar/overview in native/src+native/include = 0 hits).

## 9. Verification performed (what proves what)

| id | check | result |
|---|---|---|
| V1 | round-trip world->pixel->world for every control point (float64) | max abs error 6.82e-13 (snakewater) / 4.55e-13 (badwater) world units |
| V2 | every control point projects inside [0,1024] | yes: min px 156.27 (cp_blu1) / 186.50 (cap_3); max px 866.83 (cp_red1) / 417.73 (cap_4) |
| V3 | team spawn bboxes lie inside the frame | yes for both maps and both teams |
| V4 | levelHistogram sums equal cellCount | 41023+10368+4579+1683+158+1 = 57812; 32980+7950+2577+94 = 43601 |
| V5 | preview marker pixels sampled at expected coordinates | all 9 control-point markers read yellow (255,215,0) and all 4 spawn centroids read red (225,70,70) / blue (80,140,255) at their ledger pixel coordinates (re-checked this phase); make_preview.py draws path_track before control points so markers stay on top |
| V6 | demo map name equals BSP map name; hashes recorded | yes; sha256 in section 2 |
| V7 | z-filter dropped slab identified, not silently dropped | 6,841 cells, z[-2452,-640], bbox[-6784,288,704,10496], materials listed |
| V8 | code claims grepped, not assumed | lifeState 0 hits; minimap/radar/overview 0 hits; CLI flag list read from native/src/main.cpp |

Visual verification note: this agent cannot view images; previews were verified by ASCII rendering and
pixel sampling, not by human inspection. That limitation is recorded rather than hidden.

## 10. Gaps / NOT VERIFIED / INVALID

- G1 (NOT VERIFIED) KOTH projection: koth_bagel_rc13.bsp absent. Substitutes on disk are
  koth_bagel_event.bsp (same family, different version) and ~20 other koth_*.bsp; none matches the
  demo's map name. Decision needed (proposal P3).
- G2 (NOT RUN) runtime player coverage, update cadence, death/respawn, handle reuse (M1-M4).
- G3 (NOT VERIFIED) authoritative playable area from nav; nav identity verified but body unparsed.
- G4 (NOT RUN) all interaction acceptance items; feature absent.
- G5 (NOT OBSERVABLE) entity serial / handle-reuse validation; serial is decoded but not stored.
- G6 (GAP) no Payload SourceTV demo in the T63 corpus; badwater runtime measurement would use one of
  the 177 pl_badwater demos in tf/demos (POV recording) or a new SourceTV sample.

## 11. Acceptance status against the T65 requirement

| requirement | status |
|---|---|
| at least two target types each with a verifiable projection | Payload (pl_badwater) PASS, CP (cp_snakewater_final1) PASS, KOTH (koth_bagel_rc13) NOT VERIFIED (G1) |
| coordinate error and missing points have bounds/reasons | error budget defined (section 6); missing-point policy defined; runtime counts NOT RUN |
| do not draw an unavailable map as a seemingly-correct blank minimap | policy + IA-2 acceptance point defined; implementation NOT RUN |

## 12. State at end

- No build was run by this task; no product or probe was launched; no host lock was acquired.
- git status: only untracked evidence paths (evidence/logstf/reverify/, evidence/release-gates/,
  evidence/t56-map-audit/, evidence/t57-player-model/, evidence/t63-sourcetv/, evidence/t64-events/,
  evidence/t65-minimap/, evidence/trajectory/doctored/{r8c-growth-slow.txt,r8d-budget.txt},
  evidence/trajectory/meta-mutation.txt); no modified tracked files; HEAD unchanged at
  99eeea294cbe883e4c3e6b4b82c83de45d2ab926.
- Deliverables and hashes are listed in appendix A.

## 13. Next steps

Runtime measurement (P1), serial exposure (P2), KOTH map decision (P3), nav parser (P4), product
implementation and UI acceptance (P5) — all in proposals.md. P1 and P5 require the shared host lock and
must be scheduled serially.

## Appendix A — deliverables

| file | sha256 |
|---|---|
| data/cp_snakewater_final1.bsp.geometry.json | 26cd2748eaf4ba986a53ac8dfd59296712c336f6c32cb5a321539428f165ad4b |
| data/pl_badwater.bsp.geometry.json | f62a015059f7ea15b39277299234db301fdd91aa67e6981ffed87646df65c00c |
| data/cp_snakewater_final1.bsp.floorplan.png | b7b4c7a834ed6e30c70b875cd87f004843d5cbf1263ee85a3314932922a84095 |
| data/pl_badwater.bsp.floorplan.png | 3bbe170a0d5effce2a0eddee34f281a178e4fb73d1f0a93300aa3039cf17d6d8 |
| data/cp_snakewater_final1.preview.png | 90a8dcca64e7ff04a080b75b737811c37e52b14adc596d67051db4366fadc601 |
| data/pl_badwater.preview.png | 07387ab3031465e456ac76faa28a8b1d976fa444e1f1600a3c2a830578c45c1b |
| data/projection_ledger.json | e2d35aae52942cba14157b5f6fb7f798d161302c52c43b084a3c7427af4a67bc |
| projection_ledger.md | (generated by tools/write_ledger_md.py) |

Verification helpers: tools/_check_geom.py, tools/_check_ledger.py, tools/_check_ledger2.py,
tools/_check_preview.py (re-read the JSON/PNG artifacts and re-derive the numbers quoted in this report).

Tools (all under tools/, static only): bsp_geometry.py (BSP parse + floor raster), reachability.py
(grid flood lower bound), make_preview.py (preview PNG), make_ledger.py (machine ledger),
write_ledger_md.py (this document's companion), debug_entities.py / summarize_entities.py /
analyze_cells.py / ascii_preview.py / peek*.py (inspection helpers), vpk_search.mjs / vpk_extract.mjs
(VPK listing/extraction).
