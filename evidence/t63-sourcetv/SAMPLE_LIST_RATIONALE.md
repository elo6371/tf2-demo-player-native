# T63 FROZEN SAMPLE LIST -- rationale and rerun recipe

Machine-readable list: evidence/t63-sourcetv/sample_list.json (schema t63-sample-list-v1).
Builder: evidence/t63-sourcetv/build_sample_list.py (reads sample_detail.json + demo headers).
Consumers: T63, T64, T65, T67. Frozen 2026-10-24 against HEAD 99eeea294cbe883e4c3e6b4b82c83de45d2ab926.

## What "frozen" means

Each row pins: demo path, map, recording type (header + stream), header tick/frame counts,
byte size, sha256 of the full file, sha256 of the parsed 1072-byte header region, BSP presence,
the packet->server clock measured on that demo, and the exact inspection points. If any of these
change, the row is invalid: do not silently regenerate, re-freeze and tell the Lead.

## Why these demos

1. SourceTV only. The HUD reads lastViewEntity; in a POV demo that is the recorder's player
   entity (e.g. entity 18), in SourceTV it is camera slot 1. The shared list must not mix them.
   All six rows are clientName "SourceTV Demo" (verified in the 1072-byte header).
2. Constant packet->server offset only. SUNSHINE.dem has offsets [16960,17707,20852,24456]
   (non-constant), so it is included only as a pixel-only extra, never for tick-exact assertions.
3. BSP preference. Only snakewater (59,590,420 B), review12 (45,759,167 B) and sunshine
   (30,319,857 B) have a BSP in tf/maps. bagel/ashville73/prodcutscout have no BSP, so they are
   data-only for pixel tasks (gap G3).
4. Known entity ids. ashville73 is the only demo with a populated oracle enter roster
   (evidence/final/oracle-enters-ashville73.txt: entities 0..19 CTFPlayer class=247, 26..29
   CTFTeam class=318, 31 CTFPlayerResource class=249, 32 CTFObjectiveResource class=235), so it is
   the anchor for score/objective/ammo/lifeState assertions. bagel has known camera/target ids
   (entity 1 camera, entity 3 target) and a known before-window HUD trace.
5. Coverage of the requested cases. koth: bagel, ashville73, prodcutscout. cp: snakewater,
   review12, sunshine. One demo with a known before-window trace (bagel) and one with a known
   entity roster (ashville73) cannot be replaced without losing evidence.

## Why these inspection points

- P0 (playback 600): negative control. Entity history starts at server tick first_server_tick
  (bagel 56148, snakewater 10321, review12 15346, ashville73 16350, prodcutscout 25597), so the
  current build's sceneTick = first_packet_tick + 600 is below the window: expected HUD is
  entity=before-window and health/team/class/clip/uber all "?". This is the assertion that catches
  a reader that defaults missing values to 0/1 (T63 rule: missing must not read as benign).
  Evidence for bagel P0: evidence/camera-ui/e2e.driver.txt:3.
- P1 (playback 20000, prodcutscout 50000, bagel 60000): mid-demo, inside the window. Current build
  resolves to sceneTick and is stale by the clock mismatch (10317/15342/16348/25593/51810 ticks).
  The canonical probe uses the correct server tick (playback + offset) to read what the demo
  actually says at that playback position.
- P2 (last packet tick - 100): near end of demo, when scores/objective/uber/ammo are most
  informative and the player roster is stable. Also inside history for every demo.
- Every correct_server_tick is <= that demo's last_server_tick (verified in builder output), so a
  --props-at query can actually resolve.

## Rerun recipe (static verification, no product launch)

  cd /d/TF2_Native_Test
  python evidence/t63-sourcetv/build_sample_data.py     # regenerates sample_detail.json + map_summary.json
  python evidence/t63-sourcetv/build_sample_list.py     # regenerates sample_list.json
  python - <<'PY'                                       # hash check
  import hashlib,json
  d=json.load(open("evidence/t63-sourcetv/sample_list.json"))
  for x in d["demos"]:
      h=hashlib.sha256(open(x["path"],"rb").read()).hexdigest()
      assert h==x["sha256_full"], (x["name"],h)
  print("ok",len(d["demos"]))
  PY

Runtime verification of the list itself is proposal P0 in RUNTIME_PROPOSAL.md (demo_open_probe
--scan + sha256). Note: entity_model_probe --props-at takes a server tick, not a playback tick.

## Demo summary (values from sample_list.json)

| id | demo | map | recording | header ticks | frames | size B | BSP | clock offset | P2 correct tick |
|---|---|---|---|---|---|---|---|---|---|
| S1 | bb841c6d...cp_snakewater_final1.dem (testdata) | cp_snakewater_final1 | SourceTV | 64352 | 64330 | 51,834,617 | yes | 10319 const | 74568 |
| S2 | review12.dem | cp_reckoner | SourceTV | 120936 | 120914 | 80,261,474 | yes | 15344 const | 136178 |
| S3 | 73.dem | koth_ashville_final1 | SourceTV | 112396 | 112172 | 106,397,681 | no | 16349 const | 128642 |
| S4 | prodcutscout.dem | koth_product_final | SourceTV | 88311 | 88304 | 62,389,932 | no | 25595 const | 113804 |
| S5 | 4a9bfb92...koth_bagel_rc13.dem (testdata) | koth_bagel_rc13 | SourceTV | 77466 | 73118 | 46,921,111 | no | 51811 (packet stream) | 129175 |
| X1 | SUNSHINE.dem | cp_sunshine | SourceTV | 128432 | 120926 | 82,759,239 | yes | 16960 NON-const | n/a (pixel only) |

## Known readings usable as expected values (from existing evidence, not from this phase's runs)

R1 bagel server 129277 entity 1 DT_TFLocalPlayerExclusive.m_vecOrigin = -3244.337891,198.821442,352.031250
   (evidence/fast/entity/slot-freshness.txt; lastWrite=129277 age=0).
R2 bagel server 129277 observer-focus entity 1: mode=4 targetHandle=1718275 targetIndex=3 targetSerial=839
   follows=1 (evidence/observer-focus/gate.txt).
R3 ashville73 packet tick 1 (server 16350) roster: 0..19 CTFPlayer 247, 26..29 CTFTeam 318, 31
   CTFPlayerResource 249, 32 CTFObjectiveResource 235 (evidence/final/oracle-enters-ashville73.txt).
R4 bagel playback 600 HUD: entity=before-window stale=0 health=? team=? class=? clip=? uber=?
   (evidence/camera-ui/e2e.driver.txt:3).
R5 snakewater playback 3001: health=1 team=1 class=0 clip=? uber=? -- SUPERSEDED: recorded before
   HEAD 99eeea2, not reproducible under the current clock keying; kept only as a pointer.
