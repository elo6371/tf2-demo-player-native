# T63 -- SourceTV HUD data audit (static phase)

Date: 2026-10-24. Owner: teammate t63-sourcetv. Shared task: task-3.
Status: static phase complete; runtime phase is a proposal only (nothing built/launched/captured).

## 1. Goal / non-goal

Goal: for health, ammo, uber (charge + state), team score, objective progress and death state
(plus the team/class/player identity fields they depend on), establish -- with file:line evidence --
where each field is parsed from the demo, where it is stored, whether it reaches the HUD, and what
the existing source-tv-coverage-check.sh actually covers. Freeze one shared demo sample list that
T63/T64/T65 (and T67) can cite without re-deciding.

Non-goal (this phase): no build, no product or probe launch, no capture, no host lock, no source /
gate / config modification. Any behaviour claim below is either a static code reading or a citation
of pre-existing evidence files, and is labelled as such. Runtime verification is proposed in
RUNTIME_PROPOSAL.md.

## 2. Identity

- Repo: D:/TF2_Native_Test, branch p0-entity-protocol, HEAD 99eeea294cbe883e4c3e6b4b82c83de45d2ab926.
- Product binary (present, not run): native/build-nmake/tf2_demo_native.exe sha256 f454718b...
- Probes (present, not run): entity_protocol_probe 9588f239..., entity_model_probe eb02d7e8...,
  demo_open_probe 7de04995... (full hashes in sample_list.json identity).
- Gate: source-tv-coverage-check.sh at repo root (289 lines).

## 3. Corpus census (why the sample list looks like it does)

D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos: 1748 top-level .dem, 146 distinct maps;
126 maps have a BSP in tf/maps. Exactly 9 are SourceTV (all server "Matcha Bookable"):
73.dem koth_ashville_final1, gullyscout.dem cp_gullywash_f9, proc2.dem cp_process_f12,
processdemo.dem cp_process_f12, prodcutscout.dem koth_product_final, review1.dem koth_ashville_final1,
review12.dem cp_reckoner, review44.dem cp_sultry_b8a, SUNSHINE.dem cp_sunshine.
982 demos are pl_* (Payload); ALL of them are client-recorded POV (recorders Sinister Mark / SpawN /
mewboli / 皮革warrior). pl SourceTV = 0. Two more SourceTV demos exist in D:/TF2_Demo_Player/testdata
(snakewater, bagel; clientName "SourceTV Demo").
=> There is NO Payload SourceTV demo locally. That is a hard gap for the payload-objective part of
the task and is recorded as gap G1, not substituted with a POV demo.

## 4. Frozen sample list (deliverable 1)

evidence/t63-sourcetv/sample_list.json (schema t63-sample-list-v1), rationale in
SAMPLE_LIST_RATIONALE.md, builder build_sample_list.py. Six demos, 15 inspection points:
S1 snakewater (BSP yes, clock const 10319), S2 review12 (BSP yes, 15344), S3 73.dem (roster known,
16349), S4 prodcutscout (25595), S5 bagel (camera/target known, packet-stream offset 51811),
X1 SUNSHINE (BSP yes, clock NON-constant -> pixel only).
Each row pins path, map, recording, header ticks/frames, bytes, sha256(full), sha256(1072-byte
header), BSP presence, measured clock and the inspection points (playback tick, current sceneTick,
correct server tick, expected current HUD). Points: P0 before-window negative control (playback 600),
P1 mid-demo, P2 last-100. All P1/P2 correct server ticks are inside each demo's history range.

## 5. Static field audit (deliverable 2) and field table (deliverable 3)

Full table with citations: FIELD_TABLE.md. Summary:

- Every SendProp is parsed by native/src/demo_header.cpp:993 readEntityPropUpdates() and stored in
  EntityState.properties (native/include/demo_header.h:110-112), keyed by flattened dotted name
  (demo_header.cpp:610). This is generic: the parser already carries score/objective/ammo/lifeState.
- HUD reader: native/src/playback_hud.cpp:38-53 readPlaybackHud(), called from
  native/src/main.cpp:8337 only when a snapshot is Available/Checkpoint; shown in the window title
  at native/src/main.cpp:5413-5417.
  health  playback_hud.cpp:42   m_iHealth            (CTFPlayer idx 19)
  team    playback_hud.cpp:43   m_iTeamNum           (CTFPlayer idx 795)
  class   playback_hud.cpp:44   m_iClass             (CTFPlayer idx 294)
  clip    playback_hud.cpp:45   m_iClip1             -> NOT a CTFPlayer property (weapon entity
                                                       DT_LocalWeaponData.m_iClip1) -> always "?"
  uber    playback_hud.cpp:46-51 m_flChargeLevel     -> does not exist in the schema; real property
                                                       is DT_TFPlayerShared.m_flChargeMeter idx 738
                                                       -> always "?"
- Fields with NO code path anywhere in native/src or native/include: team score (CTFTeam.m_iScore
  idx 1), team name (m_szTeamname idx 3), objective (CTFObjectiveResource m_flLazyCapPerc idx 8-15,
  m_iOwner idx 804-811, m_iNumControlPoints idx 894; payload CTeamTrainWatcher.m_flTotalProgress
  idx 44), match time/round state (CTFGameRulesProxy idx 198/200/211), scoreboard
  (CTFPlayerResource m_iTotalScore idx 1020, m_iDeaths 204, m_bAlive 510), death state
  (CTFPlayer.m_lifeState idx 830 -- grep for lifeState over native returns nothing), player names
  (no svc_UserInfo decode at all).
- Identity: the HUD reads lastViewEntity (svc_SetView handler demo_header.cpp:1731-1736, set at
  line 1735). For SourceTV that is camera slot 1, so health/team/class are the SPECTATOR's values
  (bagel trace: health=1 team=1 class=0), not the observed player's.
- Gate coverage: source-tv-coverage-check.sh asserts A1-A5 (lines 163-224), W1-W2 (233-246) and two
  mutations (252-276) -- protocol/decode counters only. grep of property-name patterns in it = 0
  matches. It covers NONE of the HUD fields above. This is the central static finding.

## 6. Key findings

F1 Clock/keying defect (static, code): main.cpp:8226 sceneTick = firstPacketTick + g_playback.tick;
main.cpp:8230-8232 queries entity snapshots at sceneTick. Entity history is keyed by net_Tick
(demo_header.cpp:2215 -> demo_header.cpp:1384 appendEntityHistory). Hence every demo is behind by
offset - first_packet_tick: snakewater 10317, review12 15342, ashville73 16348, prodcutscout 25593,
bagel 51810. Below first_server_tick the HUD shows entity=before-window and all "?" (bagel playback
600, evidence/camera-ui/e2e.driver.txt:3).
F2 clip and uber can never become known (wrong entity / non-existent property). Existing title
evidence agrees (clip=? uber=? everywhere).
F3 health/team/class resolve but for the camera entity, not the observed player.
F4 Score/objective/match-time/death/name are "missing reader", not "missing parser": the generic
parser stores the props, nothing reads them. Adding a reader is a HUD/contract task (T66/T67).
F5 The gate can never catch a HUD data regression; T63's assertions (RUNTIME_PROPOSAL.md A0-A12)
are the only proposed coverage.
F6 Sample list must be SourceTV-only: POV demos put a player entity in lastViewEntity (e.g. entity
18), which would silently change every HUD reading.

## 7. Gaps (named, with the command/output that proves them)

G1 No Payload SourceTV demo locally: 982 pl_ demos are POV; pl SourceTV = 0 (corpus_scan.json /
map_summary.json, built from evidence/t56-map-audit/demo_headers_tfdemos.jsonl).
G2 bagel/snakewater/review12/prodcutscout player rosters unknown statically (their oracle enters
files are 0-byte placeholders; only ashville73 has a populated roster) -> runtime prop-candidates
discovery step in RUNTIME_PROPOSAL.md P1.
G3 No BSP for koth_bagel_rc13 / koth_ashville_final1 / koth_product_final -> those demos are
data-only; pixel evidence must use S1/S2/X1.
G4 SUNSHINE.dem packet->server offset non-constant -> excluded from tick-exact assertions.
G5 Uber condition bits: m_nPlayerCond (idx 705) is parsed but no TF_COND_* constants exist in the
repo, so bit->state mapping is unknown.
G6 Death state: no code path (m_lifeState never referenced) -> T66 contract needed.
G7 Player name: no userinfo decoding -> unsupported; not in scope for T63 assertions.

## 8. Next step

1. Lead schedules RUNTIME_PROPOSAL.md P0-P3 under the shared lock (one ~35 min block; commands,
   durations and lock usage are exact there).
2. T64/T65 cite sample_list.json rows and P0/P1/P2 ticks; pixel work only on S1/S2/X1.
3. T66 uses G6/F3 as the death/player-state contract input; T67 uses F1-F4 as the score/health/
   ammo/uber/objective contract input.
4. Build mutations M7-M10 are proposed for a second block (build + 4 runs, ~40 min) to prove the
   assertions are live; they need source edits + byte restore, which this phase forbids.

## 9. Evidence index (this directory)

corpus_scan.json -- SourceTV subset + pl counters from the 1748-file scan.
map_summary.json -- full per-map census (146 maps: total/SourceTV/POV/ticks/bsp_present).
scan_source_tv.py, packet_clock.py, build_sample_data.py, build_sample_list.py -- generators
(packet_clock.py parses demo commands + net_Tick; verified against known packet->server pairs).
sample_detail.json -- per-demo hashes/sizes/BSP/clock.
sample_list.json -- THE frozen list (deliverable 1).
SAMPLE_LIST_RATIONALE.md -- why these demos/points, rerun recipe.
FIELD_TABLE.md -- full field table with file:line (deliverables 2+3).
RUNTIME_PROPOSAL.md -- commands, durations, lock, assertions A0-A12, mutations M7-M10 (deliverable 4).
REPORT.md -- this file.

Compliance: this phase only read files, wrote inside evidence/t63-sourcetv/, and ran two local
Python scripts (no product/probe binary, no build, no capture, no host lock, no repo staging).
