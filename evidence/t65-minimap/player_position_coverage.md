# T65 — player position coverage in SourceTV snapshots

Static audit + measurement plan. Status: NOT RUN — no product or probe was launched in this phase.

## 1. Where a player position comes from (code evidence)

- Two origin copies per player: DT_TFLocalPlayerExclusive (full precision) and
  DT_TFNonLocalPlayerExclusive (quantized). The comment at entity_model.cpp:28-38 records a measured
  difference of about 4000 units (a map width) between the two at bagel tick 129211. Picking the wrong
  copy puts a minimap dot on the wrong side of the map.
- entity_model.cpp:34-38 exclusiveRank: Local=0, plain=1, NonLocal=2.
- entity_model.cpp:42-65 preferCandidate: freshest lastWriteTick first, then rank, then name.
- entity_model.cpp:87-126 readVectorProperty: a VectorXY property carries x and y only; z comes from
  the sibling Float "m_vecOrigin[2]" (its .x field). Missing sibling => complete=false with
  diagnostic "origin-z-sibling-missing" (a z of 0 is a guess, not a reading).
- entity_model.cpp:129-131 isPlayerClassName matches substrings TFPlayer|Player. Note entity_overlay.cpp:59-69
  uses the exact string "CTFPlayer" and its comment records the historical bug: substring matching also
  matched CTFPlayerResource, producing 21 samples for 20 players and a red gate (120/126). Any new
  player enumeration must pick one rule deliberately and prove the count.
- entity_model.cpp:140-148: entity handle = 11-bit index + 10-bit serial; serial is "decoded but not
  validated here". EntityState (native/include/demo_header.h:110-113) stores only classId and a
  properties map — no serial, no team, no life state field.
- entity_model.cpp:206-215 extractTransform(state, "m_vecOrigin", ...) is the single call the renderer
  path uses; entity_overlay.cpp:27-42 duplicates the same freshness ordering for the overlay.
- demo_header.cpp:1048-1052 stamps lastWriteTick = packetTick before copying a property, so a replayed
  value cannot look fresher than it is.
- Update types (demo_header.cpp:1248-1345): Preserve keeps state; Leave (1274-1277) keeps class and
  last state — it means "left the PVS", not destroyed; Delete (1278-1290) clears the class and
  increments the slot generation; Enter (1291-1345) reads class bits + 10-bit serial and increments
  the generation.
- No life state is read by the product: grep "lifeState|m_lifeState" over native/src + native/include
  = 0 hits (checked this phase).

## 2. Risks a minimap inherits

| id | risk | why it matters | mitigation |
|---|---|---|---|
| R1 | stale origin after Leave | a player who left PVS keeps the last origin; the dot would freeze in place like a ghost | hide dots older than N ticks / require life state; mark stale |
| R2 | duplicate origin copies disagree by up to a map width | dot on the wrong side of the map | use extractTransform only (rank rule) and print which copy was chosen for the target |
| R3 | missing z sibling -> z guessed as 0 | wrong layer / wrong dot position; multi-level maps amplify it | surface diagnostic count; fail measurement if >0.5% |
| R4 | handle reuse without serial validation | a dot could jump to a different player's position when a slot is recycled | validate serial/generation (proposal P2); interim: detect index class changes |
| R5 | dead players may retain their last origin | ghost dot at death location | use m_lifeState / m_iHealth (on the wire, see section 3) to hide or gray |
| R6 | snapshot staleness | the query returns the newest checkpoint at or before the requested tick, so positions can lag | report checkpoint/unavailable counts; a dot is only as fresh as its snapshot |

## 3. What the wire actually carries (measured from static evidence)

CTFPlayer is server class id 247 with a 885-property flat table
(evidence/animation-availability/oracle-all-classes.txt:30035). Inside that block:

- slot 19: DT_BasePlayer.m_iHealth NormalVarInt (line 30055)
- slot 830: DT_BasePlayer.m_lifeState UnsignedInt (line 30866)
- DT_BaseEntity.m_iTeamNum Int is present in the table family (many class blocks list it).

So death/respawn and team ARE observable through the generic property dump even though the product
never reads life state. The numeric encoding of m_lifeState (0 alive, 1 dying, 2 dead) is the Source
convention and must be confirmed at runtime rather than asserted here.

## 4. Measurement plan (runtime; existing probe)

| id | question | method | pass threshold | status |
|---|---|---|---|---|
| M1 | do players have a complete origin on every sampled tick? | entity_model_probe --trajectory 4000 (aggregate) + --rendered --trajectory-at <sampled ticks> | >= 99.5% of CTFPlayer samples with finite origin; zSiblingMissing <= 0.5%; every tick without players has a named status (checkpoint / unavailable / no-history), never silent | NOT RUN |
| M2 | how often does a position update? | consecutive-tick windows from --rendered --trajectory-at (e.g. 300-tick windows) | for players with total displacement > 500 units: p99 interval between origin changes <= 3 ticks, max <= 10 ticks | NOT RUN |
| M3 | when do death and respawn happen, and what does the dot do? | m_lifeState and m_iHealth from --props-at --entity N; origin trace from --rendered --trajectory-at | >= 20 deaths per demo: origin frozen (<= 1 unit/tick drift) while dead; respawn jump lands within 64 units of a spawn point within 2 ticks; dot hidden or gray while dead | NOT RUN |
| M4 | is an entity handle reused, and does a dot follow the wrong player? | index/class/life-state transitions in per-tick dumps; serial requires P2 | no position attribution across a class change; every reuse paired with a Delete/Enter generation increment; serial validation deferred | NOT RUN (serial blocked, gap G5) |
| M5 | how stale is the snapshot a dot is drawn from? | trajectory JSON counters trajectoryAvailable/Checkpoint/Unavailable + per-sample status lines | available+checkpoint >= 95% of samples; unavailable == 0 or every case named; maxStep reported | NOT RUN |

## 5. Probe capability audit (why M1/M2/M3/M5 need no source change)

native/tools/entity_model_probe.cpp (375 lines is the protocol probe; this file is larger) exposes:

- --demo <path> — open a recording.
- --trajectory N — sample N ticks evenly across the demo's history window; prints a JSON block with
  samples, entities, moved, noOriginProperty, originNotFinite, zZero/zNonZero, rawZZero/rawZNonZero
  (control), zSiblingMissing, totalDistance, maxStep, minZ/maxZ, digest, plus checkpoint/available
  counters.
- --trajectory-dump — per-tick trace of the entity that travelled furthest (diagnostic to stderr).
- --trajectory-at <comma list> — per-tick origins of every entity at the listed ticks; with --rendered
  it prints "rendered entity=... class=... x=... y=... z=... [diag=...]" using extractTransform.
- --props-at <comma list> [--entity N] — all properties (or origin-only when --entity is omitted) at
  the listed ticks; with --entity N this includes m_iHealth, m_lifeState, m_iTeamNum.

Limits: tick lists are comma-separated only (no ranges); the Windows command line is ~32k characters,
so full-demo sampling uses step >= 30 or chunked windows. The binary
native/build-nmake/entity_model_probe.exe (2026-10-24 10:23) is older than entity_overlay.cpp,
main.cpp and native_renderer.cpp, so it must be rebuilt before measuring (build-target.sh).

## 6. Exact commands

See proposals.md P1. Summary:

    cd /d/TF2_Native_Test
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh acquire t65-runtime 3600
    bash build-target.sh entity_model_probe
    native/build-nmake/entity_model_probe.exe --demo "<snakewater.dem>" --trajectory 4000 > /tmp/t65-sw-traj.json 2> /tmp/t65-sw-traj.err
    native/build-nmake/entity_model_probe.exe --demo "<snakewater.dem>" --rendered --trajectory-at "<comma list>" > /dev/null 2> /tmp/t65-sw-rendered.err
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh release t65-runtime

## 7. Open questions

- Player names: entity props carry no name; names come from the userinfo string table, which the probe
  does not print. Coverage can be measured per entity index + class + team; hover-name acceptance
  needs a name source (separate task).
- m_lifeState encoding must be confirmed on a demo with known deaths before it is used as a gate.
- Handle serial is decoded but not stored; M4 cannot be fully measured without P2.
