# T64 / task-8 - Game event oracle (tf-demo-parser reference dump)

Status: complete. Static/offline only - no product build, no product launch, no host lock.
Write scope: evidence/t64-events/oracle/** only.

## Purpose

Build an independent, reference-parser event reading to establish what the demo stream actually contains:
every svc_GameEvent decoded with its definition, tick, name and field values. This is the oracle against
which the native product event handling (which counts events but does not decode them) can be judged.

## Tool

- Source: D:/TF2_Demo_Player/work/_refs_demostf/examples/dump_events.rs (new file in that scratch clone,
  untracked there; sha256 f460999aa469985607a201af0d8b37cec3eca77e9054fed3a491cf3be7722b9c).
  A copy is stored at tools/dump_events.rs in this directory.
- Build: `cargo build --release --example dump_events` -> BUILD_RC=0.
  rustc 1.96.0 (ac68faa20 2026-05-25), cargo 1.96.0 (30a34c682 2026-05-25).
  Crate tf-demo-parser v0.5.1 (scratch clone HEAD e1425a7). Only two pre-existing lib warnings
  (mismatched_lifetime_syntaxes at src/demo/parser/mod.rs:194 and src/demo/parser/state.rs:148);
  the example itself compiles clean.
- Method: two passes. Pass 1 reads the signon GameEventList to obtain the 414 event definitions.
  Pass 2 pre-installs those definitions with the event type forced to GameEventType::Unknown(name),
  so every event decodes as RawGameEvent and its values stay in wire order; definition entries are
  then zipped with raw values positionally.
- Why this design: the crate's typed GameEvent enum only exposes read()/event_type(); there is no
  generic field accessor on the enum (get_field exists only on per-variant structs, e.g.
  PlayerDeathEvent at src/demo/gameevent_gen.rs:1165). Raw positional decoding is the only generic way.
- String-table fix: svc_UpdateStringTable decoding requires ParserState.string_tables (see
  src/demo/message/stringtable.rs:243-263, ParseError::StringTableNotFound). The first run failed with
  `Error: String table with index 8 not found`; the example now feeds svc_CreateStringTable and
  string-table packet metadata into ParserState in both passes, after which all three demos parse clean.

## Output format

TSV, one row per event:

    tick <TAB> event_id <TAB> event_name <TAB> key=value;key=value

Two leading header lines (demo path + sha256 + bytes + event count; demo header map/ticks/protocol/demo_type),
then 414 definition lines (`# def id=.. name=.. fields=name:type,...`), then the event rows.
Tick is the tick of the svc_GameEvent packet that carried the event (packet tick) - the same domain
T64 established for event ticks (reference parser: src/demo/packet/mod.rs:42, src/demo/parser/analyser.rs:405/416).

## Samples

| demo | sha256 | bytes | map | header ticks | events | defs | distinct names | event tick range |
|---|---|---|---|---|---|---|---|---|
| bagel SourceTV | 03529698db749e77a1df1c476c1c0cc06847e1173b5366a0602f1e15161b5436 | 46,921,111 | koth_bagel_rc13 | 77,466 | 12,982 | 414 | 52 | 45..77,465 |
| snakewater SourceTV | 308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f | 51,834,617 | cp_snakewater_final1 | 64,352 | 10,703 | 414 | 62 | 7..64,352 |
| pl_odyssey POV | e0f00ceb19bd135881535c5e5eddc88e664e5b754a17ca37e72a8d3932af43bc | 1,256,764 | pl_odyssey | 437 | 11 | 414 | 5 | 5..338 |

Demo paths:

- bagel: D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem
- snakewater: D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem
- pl_odyssey POV: D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-05-17_11-57-32.dem

## Cross-check against the product parser

- bagel: product reading `eventlists=1 events=12982` at evidence/camera-ui/e2e.driver.txt:3.
  Oracle: 12,982 events. Exact match.
- snakewater: product reading `eventlists=1 events=10703` at evidence/entity-material/gate.txt:41.
  Oracle: 10,703 events. Exact match.

The oracle therefore decodes exactly the same svc_GameEvent stream the native product counts, and
adds name/field/tick detail the product does not currently decode.

## Presence matrix (counts per demo)

| event name | bagel | snakewater | pl_odyssey POV |
|---|---|---|---|
| player_death | 168 | 109 | 0 |
| teamplay_round_start | 3 | 5 | 0 |
| teamplay_round_win | 3 | 5 | 0 |
| controlpoint_starttouch | 551 | 244 | 0 |
| controlpoint_endtouch | 551 | 244 | 0 |
| teamplay_point_startcapture | 33 | 27 | 0 |
| teamplay_point_captured | 15 | 15 | 0 |
| teamplay_point_locked | 4 | 0 | 0 |
| teamplay_point_unlocked | 3 | 0 | 0 |
| teamplay_capture_broken | 18 | 12 | 0 |
| teamplay_capture_blocked | 15 | 6 | 0 |
| capper_killed | 12 | 3 | 0 |
| killed_capping_player | 12 | 4 | 0 |
| teamplay_round_active | 4 | 6 | 0 |
| teamplay_win_panel | 3 | 5 | 0 |
| tf_game_over | 1 | 1 | 0 |
| setview | 0 | 0 | 0 |

Empty-field events (no fields on the wire, e.g. teams_changed): 51 in bagel, 68 in snakewater, 1 in POV.
These are valid zero-field events, not parse failures.

## SetView answer

svc_SetView is a message, not a game event: the 414-definition event list has no SetView entry and the
TSV contains 0 such rows. The product message histogram shows svc_SetView=1 for bagel
(evidence/final/bagel.txt:18) and 1 for snakewater (evidence/baseline/snakewater.txt:12). The oracle
does not decode non-event messages, so SetView remains a message-level datum and cannot be obtained
from this event dump.

## Key event tick ranges (count / first tick / last tick)

bagel:

- player_death 168 / 98 / 77,075
- teamplay_round_start 3 / 334 / 48,715
- teamplay_round_win 3 / 27,830 / 76,464
- controlpoint_starttouch 551 / 5,698 / 76,287
- controlpoint_endtouch 551 / 5,779 / 76,899
- teamplay_point_captured 15 / 6,926 / 67,958
- teamplay_point_startcapture 33 / 6,678 / 72,676
- player_spawn 206 / 334 / 77,465

snakewater:

- player_death 109 / 71 / 63,705
- teamplay_round_start 5 / 334 / 57,069
- teamplay_round_win 5 / 9,690 / 63,683
- controlpoint_starttouch 244 / 1,744 / 63,689
- controlpoint_endtouch 244 / 1,767 / 63,767
- teamplay_point_captured 15 / 4,008 / 63,683
- teamplay_point_startcapture 27 / 1,744 / 63,231
- player_spawn 220 / 277 / 64,352

Full per-name tables (name / count / min tick / max tick):
bagel.koth_bagel_rc13.event-names.tsv (52 names), snakewater.cp_snakewater_final1.event-names.tsv (62),
pl_odyssey.pov.event-names.tsv (5).

## POV sample caveat

The only pl_odyssey demo on disk is a 6.6 s connect fragment (header ticks=437): 11 events, all player
lifecycle (post_inventory_application 4, player_spawn 3, player_changeclass 2, teams_changed 1,
player_disconnect 1), zero death/round/control-point events. It cannot serve as a gameplay-event
sample and provides no cart-progress reading.

## BSP static check (pl_odyssey)

- File: D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_odyssey.bsp, VBSP v20.
  Entity lump (lump 0) is LZMA-compressed: Valve header `LZMA` + actualSize 385,573 + lzmaSize 28,639
  + 5-byte LZMA props, then a raw LZMA stream. Decoded with bundled Python lzma (FORMAT_ALONE).
  Script: tools/bsp_lump.py.
- 1,408 entities, 75 unique classnames. Counts: team_control_point 5, team_control_point_master 1,
  team_round_timer 1, tf_gamerules 1, team_train_watcher 1, func_tracktrain 1, path_track 252,
  func_door 26, prop_dynamic 60.
- The single team_train_watcher block (classname team_train_watcher, hammerid 1048280):

      origin 1664 -128 -80
      train sspl_train
      targetname sspl_watcher
      start_node sspl_path_start
      goal_node sspl_path_end
      train_can_recede 1
      handle_train_movement 1
      speed_forward_modifier 1
      env_spark_name sspl_cartsparks
      linked_pathtrack_1 sspl_path_cp_one3
      linked_pathtrack_2 sspl_path_cp_two3
      linked_pathtrack_3 sspl_path_cp_three3
      linked_pathtrack_4 sspl_path_end
      linked_cp_1 cp_one
      linked_cp_2 cp_two
      linked_cp_3 cp_three
      linked_cp_4 cp_four
      hud_min_speed_level_1 1
      hud_min_speed_level_2 60
      hud_min_speed_level_3 80

- Control: cp_snakewater_final1.bsp (uncompressed entity lump) has 986 entities / 51 classes,
  team_train_watcher 0, func_tracktrain 0, path_track 0, team_control_point 5. Confirms the
  watcher/pathtrack setup is payload-specific, not universal.

## Cart progress conclusion

Cart progress is entity state, not a game event: none of the 414 event definitions is a cart/train
event, and the pl_odyssey entity lump proves the map exposes team_train_watcher (train sspl_train)
plus one func_tracktrain and 252 path_track entities. Any cart-progress navigation must read the
watcher/train entity state from svc_PacketEntities / entity history, not from the event stream.
No real demo sample with cart progress exists on disk (the only pl_odyssey demo is the 6.6 s fragment
above), so cart-progress values remain NOT VERIFIED.

## Deliverables (all in evidence/t64-events/oracle/)

- bagel.koth_bagel_rc13.events.tsv - 13,398 lines (2 header + 414 defs + 12,982 events)
- snakewater.cp_snakewater_final1.events.tsv - 11,119 lines (2 + 414 + 10,703)
- pl_odyssey.pov.events.tsv - 427 lines (2 + 414 + 11)
- bagel.koth_bagel_rc13.event-names.tsv, snakewater.cp_snakewater_final1.event-names.tsv,
  pl_odyssey.pov.event-names.tsv - per-name count/min/max tables
- tools/dump_events.rs (source copy), tools/bsp_lump.py (entity lump decoder)
- REPORT.md (this file)

## Reproduce

    cd D:/TF2_Demo_Player/work/_refs_demostf
    cargo build --release --example dump_events
    ./target/release/examples/dump_events <demo.dem> <sha256> > <out>.events.tsv
    python tools/bsp_lump.py <map.bsp>

## Limits

- Event tick = packet tick; no display-time conversion is applied in this dump (T64 covers tick->time).
- No player names: the crate has no userinfo decode; identity is only the userid fields present in
  some events.
- No message-level data (SetView, sounds, temp entities) in the oracle.
- POV sample is a 6.6 s connect fragment; no cart-progress sample exists.
- The example lives in the scratch clone D:/TF2_Demo_Player/work/_refs_demostf and is untracked there;
  the source copy in this directory is the authoritative artifact.
