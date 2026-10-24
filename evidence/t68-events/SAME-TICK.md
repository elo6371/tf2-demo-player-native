# T68 SAME-TICK — same-tick multi-event statistics and negative-control basis

Status: static analysis of the T64 oracle TSVs (evidence/t64-events/oracle/*.events.tsv).
Inputs (sha256):
- bagel.koth_bagel_rc13.events.tsv: cc1b32c65a4d8bc56330afdc16796f1bf986814124a9fbfb9a426bb263d3361d (1,043,811 bytes, 12,982 events)
- snakewater.cp_snakewater_final1.events.tsv: 4cc7527798f91133b5fa00e3ee88ee57b3baa968de4bf8ceb525c23ad1b58627 (893,707 bytes, 10,703 events)
Generator: evidence/t68-events/tools/same_tick_report.py (exit 0). Raw stats: evidence/t68-events/same-tick-stats.json.
"Tick" is the cmd=2 packet tick written by the oracle (same tick domain the product uses: first data packet tick = 1).

## 1. Headline numbers

| metric | bagel (koth_bagel_rc13) | snakewater (cp_snakewater_final1) |
|---|---|---|
| total events | 12,982 | 10,703 |
| distinct event names | 52 | 62 |
| distinct ticks carrying events | 6,071 | 4,850 |
| ticks with >1 event | 2,915 | 2,301 |
| max events on one tick | 209 | 213 |
| min event tick | 45 | 7 |
| max event tick | 77,465 | 64,352 |

So 48.0% of bagel event-bearing ticks and 47.4% of snakewater event-bearing ticks carry more than one event. A tick is not a unique key for an event.

## 2. Top five ticks by event count (with their events)

Counts are aggregated by event name; the full wire-order list is in evidence/t68-events/golden/same-tick-334.tsv for tick 334.

### bagel

1. tick 334 — 209 events: ammo_pickup 136, player_spawn 24, post_inventory_application 24, remove_nemesis_relationships 13, recalculate_holidays 2, scorestats_accumulated_update 2, stats_resetround 2, teamplay_point_locked 2, scorestats_accumulated_reset 1, teamplay_map_time_remaining 1, teamplay_restart_round 1, teamplay_round_start 1.
2. tick 48715 — 98 events: ammo_pickup 67, player_spawn 12, post_inventory_application 12, weapon_equipped 2, recalculate_holidays 1, scorestats_accumulated_update 1, stats_resetround 1, teamplay_point_locked 1, teamplay_round_start 1.
3. tick 28831 — 97 events: ammo_pickup 68, player_spawn 12, post_inventory_application 12, recalculate_holidays 1, scorestats_accumulated_update 1, stats_resetround 1, teamplay_point_locked 1, teamplay_round_start 1.
4. tick 77465 — 48 events: ammo_pickup 34, player_spawn 6, post_inventory_application 6, teamplay_round_active 1, tf_game_over 1.
5. tick 27195 — 30 events: ammo_pickup 22, player_spawn 4, post_inventory_application 4.

### snakewater

1. tick 334 — 213 events: ammo_pickup 136, player_spawn 24, post_inventory_application 24, remove_nemesis_relationships 19, recalculate_holidays 2, scorestats_accumulated_update 2, stats_resetround 2, scorestats_accumulated_reset 1, teamplay_map_time_remaining 1, teamplay_restart_round 1, teamplay_round_start 1.
2. tick 64352 — 101 events: ammo_pickup 73, player_spawn 15, post_inventory_application 13.
3. tick 22901 — 99 events: ammo_pickup 68, player_spawn 12, post_inventory_application 12, weapon_equipped 3, recalculate_holidays 1, scorestats_accumulated_update 1, stats_resetround 1, teamplay_round_start 1.
4. tick 38059 — 99 events: ammo_pickup 68, player_spawn 12, post_inventory_application 12, weapon_equipped 3, recalculate_holidays 1, scorestats_accumulated_update 1, stats_resetround 1, teamplay_round_start 1.
5. tick 10357 — 96 events: ammo_pickup 68, player_spawn 12, post_inventory_application 12, recalculate_holidays 1, scorestats_accumulated_update 1, stats_resetround 1, teamplay_round_start 1.

## 3. Worst single-name collision

Both demos: 136 `ammo_pickup` events on tick 334 (one per player per ammo type), plus 24 `player_spawn` and 24 `post_inventory_application`. This is the strongest negative control: 136 rows share (tick, event name) and differ only in their values (ammo_index/amount/total), so a decoder that keys by (tick, name) loses 135 of them.

## 4. Golden-event co-occurrence (how often a navigation event shares its tick)

"share tick with others" = occurrences of that event on a tick that also carries at least one different event.

| event | bagel occurrences / ticks / shared / max others on tick | snakewater occurrences / ticks / shared / max others on tick |
|---|---|---|
| player_death | 168 / 167 / 167 / 12 (at tick 66142) | 109 / 108 / 108 / 8 (at tick 2515) |
| teamplay_round_start | 3 / 3 / 3 / 208 (at tick 334) | 5 / 5 / 5 / 212 (at tick 334) |
| teamplay_round_win | 3 / 3 / 3 / 4 (at tick 27830) | 5 / 5 / 5 / 5 (at tick 9690) |
| controlpoint_starttouch | 551 / 550 / 75 / 7 (at tick 13533) | 244 / 244 / 47 / 3 (at tick 21525) |
| controlpoint_endtouch | 551 / 548 / 65 / 12 (at tick 66142) | 244 / 242 / 32 / 6 (at tick 9629) |
| teamplay_point_captured | 15 / 15 / 15 / 6 (at tick 61123) | 15 / 15 / 15 / 5 (at tick 9690) |

Round starts are the extreme case: `teamplay_round_start` at tick 334 is 1 of 209 (bagel) / 213 (snakewater) events; a UI that navigates to "the round-start event" must show the other 208/212 events on the same tick, and a decoder must not collapse them into one row.

## 5. Concrete wire-order cases (what the gate must reproduce)

- bagel tick 334, first 8 of 209 (wire order): scorestats_accumulated_update; stats_resetround; teamplay_point_locked (cp=0, cpname=the Tower, team=0); recalculate_holidays; ammo_pickup (ammo_index=1, amount=20, total=20); ammo_pickup (2/32/32); ammo_pickup (3/100/100); post_inventory_application (userid=3).
- snakewater tick 334, last 3 of 213: ammo_pickup (ammo_index=6, amount=1, total=1); player_spawn (userid=20, team=3, class=1); teamplay_round_start (full_reset=true).
- bagel tick 48715 contains `teamplay_round_start` among 98 events; bagel tick 28831 likewise among 97; snakewater tick 22901/38059 among 99.
- bagel tick 77465 mixes `tf_game_over` and `teamplay_round_active` with 46 pickup/spawn events.

The full 209+213-row fixture is evidence/t68-events/golden/same-tick-334.tsv (24,173 bytes, sha256 9d6817c937678129365871e736c6af74393597a46e12cc196947927fa9be60be; 3 header lines + 209 bagel rows + 213 snakewater rows; columns demo, seq, tick, event, values).

## 6. Negative-control requirements for T68

1. Dedupe/navigation key MUST be (tick, packetOrdinal, messageBit). Tick alone is ambiguous on 2,915/2,301 ticks; (tick, name) is ambiguous 136 times at tick 334 alone.
2. A same-tick multi-event fixture must keep every row: 209 rows for bagel tick 334 and 213 for snakewater tick 334, in wire order.
3. Ordering must be wire order (file order of packets, then message order inside the packet, then event order inside the message stream) — the fixture is built in that order.
4. Counts must stay exact: total events 12,982 / 10,703; distinct ticks 6,071 / 4,850; max-per-tick 209 / 213.
5. A decoder that silently drops same-tick events must redden the fixture; a decoder that merges them into one row must redden the row count.

## 7. Notes

- Event tick 334 is the same in both demos because both recordings start at the same first data packet tick (1) and the round-start burst happens 334 ticks in.
- The oracle TSV rows are already in wire order (dump_events.rs appends events in packet/message order); the fixture preserves it.
- Values in the fixture are the same sanitised form as the golden files: tab/newline/CR -> space, ';' -> ',' inside a value, empty string for Local/empty.
