# T68 REPORT — svc_GameEvent / svc_GameEventList 解码规格 + oracle 金样本

Status: static task complete. No product source, gate, config or build file was modified; no build, no product/probe launch, no demo capture, no host lock taken.
Repo: D:/TF2_Native_Test, branch p0-entity-protocol, HEAD 99eeea2.
Write scope used: evidence/t68-events/** only (plus read-only access to evidence/t64-events/oracle and to the reference crate scratch copy outside the repo).

## 1. Task

T68 needs product-side event decoding plus tick-based navigation. T64 established that the product currently only counts events (native/src/demo_header.cpp:790-796 reads an 11-bit length and skips the payload; :807-815 reads the definition count and length and skips the payload). This task writes the reference wire-format specification, a diffable oracle golden sample, and the same-tick statistics that the T68 negative control will use.

## 2. Environment state at start and end

| item | value |
|---|---|
| branch | p0-entity-protocol |
| HEAD | 99eeea2 (docs: read the coding-problem-playbook before starting work) |
| git status --porcelain lines, start | 11 |
| git status --porcelain lines, end | 11 |
| native/ changes | 0 |
| host lock | LOCKED owner=t57-model (not taken by this task; product_processes=0) |
| product/probe processes | none (tasklist grep for tf2_demo_native / *_probe.exe: no output) |
| build | not run (static task) |

All 11 status lines are untracked evidence directories (`?? evidence/...`), including the new `?? evidence/t68-events/`; no tracked file changed.

## 3. Source provenance

The cargo registry does not contain the reference crate source:

```text
$ ls -d C:/Users/Administrator/.cargo/registry/src/*/tf-demo-parser-0.5.1
(no output)
$ find /c/Users/Administrator/.cargo -maxdepth 6 -name tf-demo-parser*
(no output)
$ ls C:/Users/Administrator/.cargo/registry/cache/index.crates.io-1949cf8c6b5b557f/ | grep -i tf-demo
(no output)
```

The exact published tarballs were therefore downloaded and used as the immutable reference:

| artifact | command | exit | size | sha256 |
|---|---|---|---|---|
| tf-demo-parser 0.5.1 | `curl -sS -L -o tf-demo-parser-0.5.1.crate https://crates.io/api/v1/crates/tf-demo-parser/0.5.1/download` | 0 | 423,555 B | c77f64e1dac5137a201d92afaf441fbe4018595bb8ec770fe6e02f6cad4410ef |
| bitbuffer 0.10.9 | `curl -sS -L -o bitbuffer-0.10.9.crate https://crates.io/api/v1/crates/bitbuffer/0.10.9/download` | 0 | 32,338 B | 525586993a118417512a49bada2d143319310891f48b0b116c8f64fbb6486c87 |

Extraction (`tar xzf`, exit 0) to /tmp/t68-crate/. All file:line citations in GAME-EVENT-SPEC.md are from those two trees.

The working clone D:/TF2_Demo_Player/work/_refs_demostf (github demostf/parser, e1425a7, Cargo.toml version 0.5.1) is not byte-identical to the published 0.5.1 (post-release changes; e.g. src/demo/data/game_state.rs exists only in the clone; after CRLF normalisation src/demo/message/gameevent.rs, src/demo/gamevent.rs, src/demo/parser/state.rs, src/demo/message/mod.rs, src/demo/parser/analyser.rs, src/demo/gameevent_gen.rs differ). It is used only to run the oracle tool (dump_events.rs, T64) and the which_packet probe; the specification cites the published tarball.

## 4. Method and commands

| # | command | exit | result |
|---|---|---|---|
| 1 | `python evidence/t68-events/tools/entry_scan.py <bagel> <snakewater> <pov>` | 0 | demo_protocol=3 on all three; entry offsets/sizes/ticks recorded; first cmd=2 tick = 1 on all three |
| 2 | `cargo build --release --example which_packet` (in D:/TF2_Demo_Player/work/_refs_demostf) | 0 | probe built; only pre-existing lib warnings |
| 3 | `which_packet.exe <demo> --first 6` on the three demos | 0 | exactly one svc_GameEventList per demo, inside a cmd=1 signon entry (packet_index=3), count=414, first definition (0, "server_spawn"); parse_all dump shows the signon message order |
| 4 | `python evidence/t68-events/tools/make_golden.py` | 0 | 12 golden event TSVs + SUMMARY.tsv + same-tick-stats.json |
| 5 | `python evidence/t68-events/tools/make_tick334.py` | 0 | golden/same-tick-334.tsv (209 bagel + 213 snakewater rows) |
| 6 | `python evidence/t68-events/tools/check_defs.py` | 0 | 414 definitions, ids sequential 0..413, both demos; golden/input sha256 |
| 7 | `python evidence/t68-events/tools/def_map.py` | 0 | golden event name -> definition id + field list; empty-field event counts |
| 8 | `python evidence/t68-events/tools/same_tick_report.py` | 0 | same-tick statistics (section 6) |
| 9 | `python evidence/t68-events/tools/type_coverage.py` | 0 | used field type coverage; max string length 527/524 |
| 10 | git/status/lock/process checks | 0 | section 2 |

No product build, no product or probe execution against a demo, no host lock.

## 5. Inputs and identity

Demos (read-only):

| demo | path | bytes | sha256 | map | header ticks | events (oracle) |
|---|---|---|---|---|---|---|
| bagel | D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem | 46,921,111 | 03529698db749e77a1df1c476c1c0cc06847e1173b5366a0602f1e15161b5436 | koth_bagel_rc13 | 77,466 | 12,982 |
| snakewater | D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem | 51,834,617 | 308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f | cp_snakewater_final1 | 64,352 | 10,703 |
| pl_odyssey POV (fragment) | D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-05-17_11-57-32.dem | 1,256,764 | e0f00ceb19bd135881535c5e5eddc88e664e5b754a17ca37e72a8d3932af43bc | pl_odyssey | 437 | 11 |

Oracle TSVs (T64, tf-demo-parser 0.5.1 clone; tool sha256 f460999aa469985607a201af0d8b37cec3eca77e9054fed3a491cf3be7722b9c):

| file | bytes | sha256 |
|---|---|---|
| bagel.koth_bagel_rc13.events.tsv | 1,043,811 | cc1b32c65a4d8bc56330afdc16796f1bf986814124a9fbfb9a426bb263d3361d |
| snakewater.cp_snakewater_final1.events.tsv | 893,707 | 4cc7527798f91133b5fa00e3ee88ee57b3baa968de4bf8ceb525c23ad1b58627 |

Cross-check against existing product readings: events=12982 in evidence/camera-ui/e2e.driver.txt:3 and events=10703 in evidence/entity-material/gate.txt:41; eventlists=1 in both. The oracle totals match the product counters exactly, which is why the golden sample can be diffed by the gate.

## 6. Conclusions

1. Wire format is fully specified in GAME-EVENT-SPEC.md: message ids 25/30, 11-bit event length (bits), 9-bit event id, 3-bit value types with widths, 9-bit/20-bit definition table, null-terminated inline names (not a string table), LSB-first bit order. Every claim cites published-crate file:line.
2. The definition table arrives in the signon segment: the only svc_GameEventList in each demo is in a cmd=1 signon entry (packet_index=3; bagel entry@1030782 size 17,083; snakewater entry@1027907 size 16,889; POV entry@346104 size 17,119), before the first cmd=2 data packet (tick=1 in all three). Signon entry tick fields are not usable ticks (bagel INT32_MIN; snakewater 10252; POV 15170). Event ticks must come from cmd=2 entries.
3. Product change points are enumerated with exact line numbers (GAME-EVENT-SPEC.md section 9): demo_header.cpp:790-796, :807-815, call sites :2214/:2218, stream signature :2174-2179, scan call :2296-2297, MessageBits :400-435 (needs a bounded sub-stream), include/demo_header.h:446+/:710-713, main.cpp:5284-5285. Existing counters gameEventListCount / gameEventDefinitionCount / gameEventCount / gameEventPayloadBits are reusable and must not change semantics.
4. Golden samples: 12 event files (6 event names x 2 demos) + SUMMARY.tsv, 104 lines each for 50+50 events (smaller events list all rows twice), fixed columns tick/event/alphabetical values, UTF-8, LF. Full hashes in section 7.
5. Same-tick: 2,915/6,071 bagel ticks and 2,301/4,850 snakewater ticks carry more than one event; max 209/213 events on one tick (tick 334); 136 identical-name ammo_pickup events on tick 334 in both demos. Dedupe key must be (tick, packetOrdinal, messageBit).
6. Definition ids are 0..413 sequential in both demos; 52/62 distinct event names are actually used; max event string value length 527/524 bytes; Local value type is defined but unused in the samples.

## 7. Deliverables (all under evidence/t68-events/)

| file | bytes | lines | sha256 |
|---|---|---|---|
| GAME-EVENT-SPEC.md | 25,138 | 437 | 6b7763043672d43fc7d6e2c7a1961ad3f463ba4571778f1a5965eee04d2f790f |
| SAME-TICK.md | 7,320 | 84 | 4aeab6f64c054fbebb493ccb704a7cfea3cd74be05ffd7a8441122701da06ee7 |
| REPORT.md (this file) | 14,083 B (pre-note) | 161 (pre-note) | 765df99b778e34f67155ff625b15dfd3d7eddc9158cb932dc75999a4f014b94d (pre-note; see section 11) |
| golden/SUMMARY.tsv | 1,721 | 15 | e8c11a37586c544b4e9ac5b4237cf74dae69e866bbddc12011fbfcbd8fdee908 |
| golden/bagel.player_death.tsv | 12,470 | 104 | a46e90113300f1590eda25ec769ba8063cb82d39d85e18f97c4708b80dafee36 |
| golden/bagel.teamplay_round_start.tsv | 531 | 10 | 0d959e1e5006de9827b88ffc0d38d53ff60aca6a60c2cbe73817907435502406 |
| golden/bagel.teamplay_round_win.tsv | 694 | 10 | 06e5aa86325875498a6a2ec04fd722c31ca348bd46b5dd545c0ddd4e0c22aa88 |
| golden/bagel.controlpoint_starttouch.tsv | 3,731 | 104 | 66df1c1a9e8d8f9ae4307fc92cf200f51f5fffed342dcbfdf61e116e9290a728 |
| golden/bagel.controlpoint_endtouch.tsv | 3,530 | 104 | 2c51f932e9c55e3d58e7728cce8d7393c6ca408a6d85c1c2d59b5faa1eb998bf |
| golden/bagel.teamplay_point_captured.tsv | 1,778 | 34 | 5ddebc57d3015e9bee082635dd06b4c94a3da001c7b4404e0ba588b0062e70fa |
| golden/snakewater.player_death.tsv | 12,563 | 104 | 3c2ce96ba0da5618b623b086575ae40055953c9ef1ebe3125db396990c953390 |
| golden/snakewater.teamplay_round_start.tsv | 675 | 14 | 91c60a333a7b3204f5524e9ff4885a17e01fdb526faeb26ebbfcb7a191d373cf |
| golden/snakewater.teamplay_round_win.tsv | 897 | 14 | 0ec37f915be182e234cf5de487437331ab688c00b7912de683e558df91d0261e |
| golden/snakewater.controlpoint_starttouch.tsv | 3,775 | 104 | 9b02943ca703a12bfd80911f3913fe114f9871ac8815e536b6b6a6f96d548472 |
| golden/snakewater.controlpoint_endtouch.tsv | 3,573 | 104 | 148c4b119ef2ef8be54205360917c2a68ba0743094c49a0467af95797f627150 |
| golden/snakewater.teamplay_point_captured.tsv | 1,976 | 34 | b683eab471ed91b2d59f3265a69b6c6ec66eb0a454ed552a16e5c76a863a2135 |
| golden/same-tick-334.tsv | 24,173 | 425 | 9d6817c937678129365871e736c6af74393597a46e12cc196947927fa9be60be |
| same-tick-stats.json | 32,595 | 1,192 | e0769167bd709ecff54330c6bf0883090bc19e045539b9bee26ad3aff24b7012 |
| tools/make_golden.py | 8,749 | 202 | fb124f4eb85c3b4b6956abaf8ce65b4483fef60918dcf4c35ff464ae2103af1f |
| tools/entry_scan.py | 1,755 | 43 | 23da4bc59dcdba32026811a9bc5f88d7d71fe61a8514c96007d06006a9430ea4 |
| tools/make_tick334.py | 1,398 | 34 | f20003b4263ca6446306a6d179d2904ec15382dc8f6537adc9290554aacae1a8 |
| tools/check_defs.py | 1,626 | 38 | 6c72272c879e73e97615f4f19213ae43e49170783493f81e66f865c879d439cf |
| tools/def_map.py | 1,299 | 28 | 6fc392f0fdea3573bf992e034d893ccd0c89d1e67da4271f08a5d837c2e33fa7 |
| tools/same_tick_report.py | 2,217 | 42 | 38aeec4cb5644a7fd2e0d7be10e7fddc36b65dc59a39a83974c1ec0e8de471a3 |
| tools/type_coverage.py | 1,414 | 31 | 1c6cac57886c3594f27601e083b1a30ee1b1a22d9b8a3e0f16445bb5783a530d |

Golden file format (identical for all 12 event files, diffable):

```text
# demo_path=<path> sha256=<demo sha256> demo=<bagel|snakewater> map=<map> event=<name> total=<n> first_tick=<n> last_tick=<n>
# fields=<alphabetical comma-separated field names>
# section=first50
<tick>	<event>	<value1>	<value2>...
# section=last50
<tick>	<event>	<value1>	<value2>...
```

Rows are in wire order (tick ascending, then packet/message order). Values are in the alphabetical field order declared on line 2. Events with <=50 rows repeat the same rows in both sections. Sanitisation matches the oracle TSV: tab/newline/CR -> space, ';' inside a value -> ',', empty cell for Local/empty string.

## 8. NOT VERIFIED

- No pl_* payload demo exists on disk. Cart progress/checkpoint is entity state (team_train_watcher), not a game event; no cart event sample was verified. (Static BSP check in T64 found team_train_watcher only in pl_odyssey.bsp.)
- The only POV demo is a 437-tick / 11-event connect fragment (post_inventory_application 4, player_spawn 3, player_changeclass 2, teams_changed 1, player_disconnect 1); it is not a payload sample.
- Local-typed fields are defined by the format but no used definition in either sample contains one; Local decode is unexercised.
- No event with an id outside the definition table and no malformed/truncated event payload were observed; those failure paths are design requirements only.
- No userinfo/player-name decode exists in the product (T64 G5), so userid -> player name is not verifiable; golden rows keep numeric userids.
- Definition ids are sequential 0..413 in both samples; non-contiguous or duplicate ids are unverified.
- Event coverage is limited to two SourceTV demos (KOTH and 5CP); other game modes and POV payload events are unverified.
- Same-tick ordering inside one tick follows packet/message order in the oracle; whether the product reproduces the exact same order is a T68 implementation acceptance item, not measured here.

## 9. Acceptance mapping (what a T68 gate can assert with this evidence)

1. `events=` unchanged at 12,982 / 10,703 (matches oracle totals).
2. `eventlists=1`, `eventdefs=414`.
3. Six golden event types x two demos diff byte-for-byte against golden/*.tsv.
4. Tick 334 fixture: 209 bagel + 213 snakewater rows in wire order.
5. Negative control: tick-only dedupe loses 135 of 136 ammo_pickup rows at tick 334; (tick, packetOrdinal, messageBit) keeps all.
6. Failure counters: unknown id / value-count mismatch must be counted and named, never silently dropped.

## 10. Self-check

- No file outside evidence/t68-events/ was written. No git commit/checkout/stash/clean. No build. No product/probe launch. No host lock. native/ has 0 changes (git status -- native empty).
- Every format claim cites published-crate file:line; every statistic cites a generated file with sha256; zero/missing values are reported as NOT VERIFIED, not as pass.

## 11. Post-run hash note

REPORT.md self-reference: the content of this file before this section was appended was 14,083 bytes / 161 lines / sha256 765df99b778e34f67155ff625b15dfd3d7eddc9158cb932dc75999a4f014b94d. This section was added after that measurement, so the delivered file has a different byte count and hash; re-measure with 'wc -l' and 'sha256sum'. No other deliverable was modified after its recorded hash was taken. The stale scratch tool tools/entry_probe_tmp.py was deleted after hashing; it is not a deliverable.
