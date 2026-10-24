# T68 — svc_GameEvent / svc_GameEventList decode in the product, with the game-event gate

Status: implementation complete; baseline gate green; mutation suite `3/3`
caught (`MUTATION-MODE=PASS`), with each source byte-restored and the baseline
green again after every mutation.

- Worktree: `D:/TF2_Native_Worktrees/t68-events`, branch `task/t68-events`, base `99eeea2` (p0).
- Product code touched: `native/include/demo_header.h`, `native/src/demo_header.cpp`,
  `native/src/main.cpp`, `native/tools/demo_open_probe.cpp`. Nothing under `viewSamples`
  or the clock was changed (t63 owns those).
- New gate: `game-event-check.sh`. Golden oracle: `evidence/t68-events/golden/`
  (14 files copied from the T64 oracle; `same-tick-334.tsv` sha256
  `9d6817c937678129365871e736c6af74393597a46e12cc196947927fa9be60be`).

## Wire format implemented

`svc_GameEvent = 25` body: an 11-bit payload length in **bits**, then a payload of
exactly that many bits: 9-bit event id followed by one value per definition entry,
in definition order.

`svc_GameEventList = 30` body: 9-bit definition count, 20-bit payload length in
**bits**, then that many definitions. Each definition is a 9-bit id, a
null-terminated UTF-8 name, and repeated (3-bit value type, null-terminated UTF-8
entry name) pairs until the 3-bit type is 0.

Value widths: String = null-terminated bytes (terminator consumed), Float = 32,
Long = 32, Short = 16, Byte = 8, Boolean = 1, Local = 0 bits, None = 0 bits and a
terminator only. Every integer is read LSB-first. Float bits are memcpy'd into an
IEEE-754 f32 and rendered like Rust's `Display` (shortest round-trip digits,
fixed notation, no exponent), matching the T64 oracle.

## Product changes

`native/include/demo_header.h`

- `:452` `enum class GameEventValueType : std::uint8_t` (None..Local, wire values 0..7).
- `:473` `GameEventDefinition` (`id`, `name`, `entries`); `:465` `GameEventEntry`
  (`name`, `kind`).
- `:484` `DemoGameEvent`: `tick`, `eventId`, `name`, `fields` (name/value pairs in
  definition order), `packetOrdinal`, `messageBit`.
- `:763-771` summary counters: `gameEventDefinitions`, `gameEventDefinitionsParsed`,
  `gameEvents`, `gameEventDecoded`, `gameEventDecodeFailures`, `gameEventUnknownId`
  (plus the pre-existing `gameEventListCount`, `gameEventDefinitionCount`,
  `gameEventCount`, `gameEventPayloadBits`).
- `:960` `decodeDemoMessageStream(..., std::uint32_t packetOrdinal = 0)`.

`native/src/demo_header.cpp`

- `:412` `MessageBits::limited(std::size_t count)`: a bounded sub-view of the packet
  bit stream. It clamps the limit to the parent's remaining bits and clears the
  minimum-remaining requirement, so a declared event/list length can never read
  past the packet or past the message body.
- `:831` `readGameEvent(bits, summary, entryTick, packetOrdinal, messageBit)`:
  reads the 11-bit length, builds `payload = bits.limited(payloadBits)`, reads the
  9-bit id, then decodes one value per definition entry in definition order.
  - event tick is the cmd=2 packet tick passed in as `entryTick` (playback domain),
    never the signon entry's tick;
  - no definition table yet, an unreadable id, a value read failure, or a
    consumed-bits mismatch against the declared length all increment
    `gameEventDecodeFailures` (the packet is not failed);
  - an id past the end of the table increments `gameEventUnknownId` (no OOB read);
  - a good event increments `gameEventDecoded` and is appended to
    `summary.gameEvents` with `(tick, packetOrdinal, messageBit)`.
- `:915` `readGameEventList`: parses the count/length and every definition, rejects
  value types > 7, requires the payload to be fully consumed (real data has no
  padding), and replaces `summary.gameEventDefinitions` (last list wins).
- `:2308` `decodeDemoMessageStream` accepts `packetOrdinal`; `:2349` passes it to
  `readGameEvent`; `:2353` dispatches `svc_GameEventList`.
- `:2432` the scan loop passes the entry index as `packetOrdinal`, so the dedupe
  key distinguishes same-tick events from different packets.

`native/src/main.cpp`: `:5285-5289` print `events=`, `eventdefs=`, `eventdecoded=`,
`eventfail=`, `eventunknown=`.

`native/tools/demo_open_probe.cpp`

- `:43` `--events <n>` (0 = all, only valid with `--scan`).
- `:108` dumps `tick<TAB>event_id<TAB>name<TAB>key=value;...` in wire order, deduped
  by `(tick, packetOrdinal, messageBit)`.
- `:221` adds `eventlists=`, `events=`, `eventdefs=`, `eventdecoded=`, `eventfail=`,
  `eventunknown=`, `event_rows=`, `event_dupes=` to the summary line.
- `:257` writes the rows as raw UTF-8 after the wide summary line (the demo's
  player names are UTF-8; `std::wcout` would mangle them).

## Baseline evidence (probe over both demos)

| demo | eventlists | eventdefs | events | eventdecoded | eventfail | eventunknown | event_dupes | event_rows |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| bagel | 1 | 414 | 12982 | 12982 | 0 | 0 | 0 | 12982 |
| snakewater | 1 | 414 | 10703 | 10703 | 0 | 0 | 0 | 10703 |

All twelve golden event samples match first50 and last50 byte for byte; both
same-tick fixtures match in wire order (209 bagel / 213 snakewater); the tick-only
negative control keeps 1 row at tick 334 and 1 of 136 `ammo_pickup` rows, while
the full key keeps all 209/213 and 136. `--events 1` returns exactly one row and
it is identical to the first row of the full dump.

## Gate

`bash game-event-check.sh` (exit 0 = green). It refuses to run on a stale binary
(a source newer than `demo_open_probe.exe` is a FATAL), pins the same-tick fixture
sha256, and compares:

1. counters above on both demos;
2. the six golden event types, first50 and last50, in wire order, rendered in the
   oracle's alphabetical field order;
3. every tick-334 event against `same-tick-334.tsv` in wire order, values in
   definition order (the fixture's order);
4. tick-only negative controls computed from the decoded rows (distinct ticks,
   tick334 full/collapsed, ammo_pickup full/collapsed);
5. `--events 1`.

Baseline run: `GATE=PASS` (both demos, `CHECKS-FAILED=0`).

## Mutations (`bash game-event-check.sh --mutation`)

Each mutation patches one source file, rebuilds `demo_open_probe`, requires the
named assertion to go red, then restores the file byte for byte (`cmp`), rebuilds,
and requires the full baseline green again. A patch that does not apply or does
not compile is a FATAL, never a caught mutation.

| # | patch | expected red assertion | result |
| --- | --- | --- | --- |
| m1 | `readGameEvent` skips the payload instead of decoding it | `event_count` | `MUTATION-CAUGHT=PASS` — `FAIL event_count = 0, expected 12982` |
| m2 | the event tick comes from the signon entry (INT32_MIN on bagel) | `golden bagel.teamplay_round_start first50` | `MUTATION-CAUGHT=PASS` — got tick `-2147483648`, want `334` |
| m3 | probe dedupe key degraded to `(tick, 0, 0)` | `same_tick bagel rows` | `MUTATION-CAUGHT=PASS` — `FAIL same_tick bagel rows = 1, expected 209` |

After each mutation the gate rebuilt `demo_open_probe`, confirmed the source was
byte-identical to its backup (`cmp`), rebuilt, and printed
`BASELINE-GREEN=1` for both demos. The run ended with
`MUTATION-MODE=PASS (3/3 mutations caught; sources byte-restored; restored baseline green)`
and `LOCKED-COMMAND-EXIT rc=0`. Per-mutation evidence is in
`evidence/t68-events/runs/mutation/m{1,2,3}-check.txt`.

## Reproduction

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t68-events bash -c '\
  cd /d/TF2_Native_Worktrees/t68-events && \
  bash build-target.sh demo_open_probe tf2_demo_native && \
  bash game-event-check.sh && \
  bash game-event-check.sh --mutation'
```

## Acceptance mapping

- `DemoGameEvent` and `summary.gameEvents` / `summary.gameEventDefinitions` exist
  exactly as specified in `demo_header.h`.
- Both messages are decoded; the definition table arrives in signon cmd=1 and the
  event tick comes from cmd=2; unknown ids and decode failures are counted by name.
- `MessageBits::limited` bounds every read to the message body (no OOB reads).
- `demo_open_probe --events <n>` prints `tick<TAB>event_id<TAB>name<TAB>key=value;...`.
- `game-event-check.sh` is green on both demos against the T64 golden files, with
  the dedupe key `(tick, packetOrdinal, messageBit)`.
- The three mutations go red on the named assertions and restore to a green
  baseline.
