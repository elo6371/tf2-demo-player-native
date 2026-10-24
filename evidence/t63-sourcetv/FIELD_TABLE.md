# T63 STATIC FIELD AUDIT -- where each HUD field comes from

Static phase only. Nothing in this file was produced by building, launching or capturing.
All "code path" citations are from D:/TF2_Native_Test at HEAD 99eeea294cbe883e4c3e6b4b82c83de45d2ab926.

Schema source: evidence/animation-availability/oracle-all-classes.txt (class sections below).
Parser entry point for every SendProp: native/src/demo_header.cpp:993 readEntityPropUpdates(...)
-> EntityState.properties (native/include/demo_header.h:110-112). Values are keyed by the
flattened dotted property name (demo_header.cpp:610 builds flattenedProps).
HUD read path: native/src/playback_hud.cpp:38-53 readPlaybackHud(); called from
native/src/main.cpp:8337 only when a snapshot is Available or Checkpoint; rendered into the
window title by native/src/main.cpp:5413-5417 ("health=... team=... class=... clip=... uber=..."
with "?" for unknown).

## Gate coverage first (what source-tv-coverage-check.sh actually asserts)

File: D:/TF2_Native_Test/source-tv-coverage-check.sh (289 lines).
- A1 lines 163-172: header classifier and in-stream scan agree recording=SourceTV.
- A2 lines 174-185: hltv=1, replay_bit=0, source_tv_flag=1.
- A3 lines 187-196: index_state=ok and index_tail_bytes == file size.
- A4 lines 198-208: packets_scanned and map match the pinned value for the frozen file.
- A5 lines 210-224: entity_failures=0, malformed_packets=0, unknown_message_packets=0,
  delta_base_unavailable=0, unknown_message_types=<none>.
- W1 lines 233-239: demos_read == 9; W2 lines 240-246: packets >= 900000.
- Mutations lines 252-276: P1 packets+1 and P2 source_tv_flag=0, exactly 9 red => MUTATION-CAUGHT.
grep -cE 'm_i[A-Z]|m_fl[A-Z]|SendProp|properties' source-tv-coverage-check.sh == 0.
=> The gate asserts protocol/decode health only. It never reads a single entity property, so it
   covers NONE of the fields below. Every "evidence status" below is "not covered (gate)".

## Field table

| field | demo source (class.property, schema idx) | code path (file:line) | current HUD use | evidence status | proposed assertion |
|---|---|---|---|---|---|
| health | CTFPlayer [19] DT_BasePlayer.m_iHealth NormalVarInt (oracle-all-classes.txt:30035-30920) | parsed demo_header.cpp:993; stored EntityState.properties demo_header.h:110-112; read playback_hud.cpp:42; displayed main.cpp:8337 + 5413-5417 | health= shown, but only for the camera entity (lastViewEntity), never the observed player; "?" when no snapshot | not covered (gate); value path exists | A2 (bagel before-window must show health=?), A3 (props-at vs HUD value) |
| ammo reserve | CTFPlayer [241..272] m_iAmmo.000-.031 NormalVarInt; [259] m_iAmmo.018 (oracle-all-classes.txt) | parsed generically demo_header.cpp:993; stored demo_header.h:110-112; NO reader in native/src or native/include | none (no ammo in title) | not covered (gate); no HUD code path | A4 (props-at at S3-P1/S3-P2, entity 3/7/12) |
| clip (loaded ammo) | weapon entity DT_LocalWeaponData.m_iClip1 (witness idx 182, evidence/observer-focus/oracle-witness.txt); CTFPlayer has NO m_iClip1 | playback_hud.cpp:45 findNamed("m_iClip1") on the viewed PLAYER entity -> can never match | clip= always "?" (clipKnown never true) | not covered (gate); reader exists but targets the wrong entity | A5 (resolve m_hActiveWeapon & 0x7FF -> weapon entity, then m_iClip1); HUD assertion: clip=? stays (mutation M8) |
| uber charge | CTFPlayer [738] DT_TFPlayerShared.m_flChargeMeter Float (oracle-all-classes.txt:30774) | playback_hud.cpp:46-51 findNamed("m_flChargeLevel") Float finite -> property does not exist in the schema | uber= always "?" (uberKnown never true) | not covered (gate); reader reads a non-existent property | A6 (props-at medic m_flChargeMeter 0..100); HUD mutation M9 |
| uber state (conditions) | CTFPlayer [705] DT_TFPlayerShared.m_nPlayerCond NormalVarInt (+[753] m_nPlayerCondEx) | parsed generically; NO reader; no TF_COND_* constants in native/src or native/include (grep empty) | none | not covered (gate); mapping unknown => named gap G5 | A7 (property exists, int; only "present", not a condition meaning) |
| team score | CTFTeam entity 26..29 (ashville73) [1] DT_Team.m_iScore Int (oracle-all-classes.txt CTFTeam id=318 flat=9) | parsed generically demo_header.cpp:993; NO reader | none | not covered (gate); no HUD code path | A8 (props-at entity 26..29 at S3-P2; score >= 0, non-decreasing vs S3-P1) |
| team identity | CTFTeam [0] DT_Team.m_iTeamNum Int, [3] DT_Team.m_szTeamname String; CTFPlayer [795] DT_BaseEntity.m_iTeamNum Int | parsed generically; HUD reads only m_iTeamNum from the camera entity playback_hud.cpp:43 | team= shown (camera entity's team; bagel trace shows team=1, i.e. spectator) | not covered (gate); partial path | A3 (player entity team in {2,3}) |
| class | CTFPlayer [294] DT_TFPlayerClassShared.m_iClass UnsignedInt | parsed generically; read playback_hud.cpp:44 | class= shown (camera entity) | not covered (gate); path exists | A3 |
| death state | CTFPlayer [830] DT_BasePlayer.m_lifeState UnsignedInt; related [fFlags] FL_TRANSRAGDOLL 1073741824 | grep -rE 'lifeState|m_lifeState' native/src native/include => NO MATCH; no code path at all | none (no alive/dead field) | not covered (gate); no code path => named gap G6 (T66 contract) | A10 (props-at lifeState 0 vs 2); mutation M10 |
| player identity (name) | player name is not a SendProp; it is userinfo (svc_UserInfo) | no userinfo decode anywhere in native (only unrelated logstf_viewer.cpp:76/106/257-258 URL userinfo refusal) | none | not covered (gate); no code path => named gap G7 | A11 (title contains no name field; HUD contract out of T63 scope) |
| objective progress (5CP/KOTH) | CTFObjectiveResource entity 32 [8..15] m_flLazyCapPerc.000-.007 Float, [464..] m_bTeamCanCap.000-.063, [804..811] m_iOwner.000-.007, [894] m_iNumControlPoints Int, [892] m_iTimerToShowInHUD | parsed generically; NO reader | none | not covered (gate); no HUD code path | A9 (props-at entity 32 at S3-P1/P2: owner in {0,1,2,3}, 0<=perc<=1) |
| objective progress (payload) | CTeamTrainWatcher [44] DT_TeamTrainWatcher.m_flTotalProgress Float | parsed generically; NO reader | none | not covered; ALSO no Payload SourceTV demo exists locally (gap G1) | cannot be asserted in this phase; proposal only |
| match time / round state | CTFGameRulesProxy [198] m_iRoundState Int, [200] m_iWinningTeam UnsignedInt, [211] m_flCountdownTime Float, [207] m_nRoundsPlayed | parsed generically; NO reader | none | not covered (gate); no HUD code path | A12 (props-at game rules entity; ints/floats present) |
| scoreboard (total score/deaths/alive) | CTFPlayerResource entity 31 [1020] m_iTotalScore.000 NormalVarInt, [204] m_iDeaths.000 Int, [510] m_bAlive.000 UnsignedInt, [612] m_iHealth.000 NormalVarInt, [1122] m_iMaxHealth.000 | parsed generically; NO reader | none | not covered (gate); no HUD code path | A8b (props-at entity 31, per-slot arrays) |
| spectator target | svc_SetView message, native/src/demo_header.cpp:1731-1736 readSetView -> summary.lastViewEntity (line 1735); dispatch demo_header.cpp:2209 | HUD reads the value as the entity to query (main.cpp:8337) but exposes no observer-target index/mode | only indirectly: the title values belong to the camera entity | not covered (gate) | A1 (demo_open_probe --scan bagel: setview=1 viewent=1) |

## Findings that matter for T64/T65/T67

1. Clock/keying defect (static, confirmed by code + evidence): native/src/main.cpp:8226 sets
   sceneTick = firstPacketTick + g_playback.tick, and main.cpp:8230-8232 queries the entity
   snapshot at sceneTick. Entity history is keyed by net_Tick (demo_header.cpp:2215 passes
   networkTick; demo_header.cpp:1384 appendEntityHistory). Therefore every demo is behind by
   (packet_to_server_offset - first_packet_tick) ticks: snakewater 10317, review12 15342,
   ashville73 16348, prodcutscout 25593, bagel 51810. Before-window for playback ticks below
   first_server_tick (bagel 56148, snakewater 10321, ...).
2. clip and uber can never become known: wrong entity (clip) and a property that does not exist
   (m_flChargeLevel vs m_flChargeMeter). Current title evidence agrees: clip=? uber=? everywhere.
3. health/team/class DO resolve, but for lastViewEntity (the SourceTV camera slot 1), not for the
   observed player. bagel title trace shows team=1 class=0 health=1 = spectator entity.
4. Score, objective, match time, deaths, player names, death state: no code path at all. The
   generic property parser would carry the values if a reader existed (all props are in the
   flattened schema), so these are "missing reader", not "missing parser".
5. Gate coverage is protocol-only; it can never catch a HUD field regression. That is the main
   gap T63 identifies.
