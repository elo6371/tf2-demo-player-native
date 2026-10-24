# T68 GAME-EVENT-SPEC — svc_GameEvent / svc_GameEventList 线格式与产品解码规格

Status: static specification. No product source, gate or build file was changed by this task.
Repo: D:/TF2_Native_Test, branch p0-entity-protocol, HEAD 99eeea2.
Reference implementation: tf-demo-parser 0.5.1, published crates.io tarball (sha256 c77f64e1dac5137a201d92afaf441fbe4018595bb8ec770fe6e02f6cad4410ef), plus its dependency bitbuffer 0.10.9 (published tarball sha256 525586993a118417512a49bada2d143319310891f48b0b116c8f64fbb6486c87).
Samples: bagel (koth_bagel_rc13) and snakewater (cp_snakewater_final1) SourceTV demos; identity, sha256 and counts in REPORT.md.

All bit offsets below are in bits, counted from the first bit of the demo entry payload. "bit N" means the N-th bit in transmission order.

## 0. TL;DR for the implementer

1. Message ids (6-bit message type): svc_GameEvent = 25, svc_GameEventList = 30.
2. svc_GameEvent body: 11-bit `length` (bits), then a bit-packed payload of exactly `length` bits: 9-bit event id, then one value per definition entry, in definition order.
3. svc_GameEventList body: 9-bit `count`, 20-bit `length` (bits), then `count` definitions. Each definition: 9-bit id, null-terminated UTF-8 name, then repeated (3-bit value type, null-terminated UTF-8 entry name) until a 3-bit type == 0.
4. Value widths: String = null-terminated bytes (terminator consumed); Float = 32; Long = 32; Short = 16; Byte = 8; Boolean = 1; Local = 0 bits (no wire data); None = 0 (only a terminator, never a value).
5. All multi-bit integers are LSB-first: the first bit read is bit 0 of the value (bitbuffer little-endian).
6. The definition table arrives in the signon segment (cmd=1 entries), before the first data packet (cmd=2, tick=1). Both are decoded by the product's existing scan loop, so definitions are installed before any event-bearing packet. Do NOT take an event tick from a signon entry.
7. Product change points: demo_header.cpp:790-796 (readGameEvent), :807-815 (readGameEventList), call sites :2214 and :2218, stream signature :2174-2179 (needs packet ordinal), MessageBits :400-435 (needs a bounded sub-stream), include/demo_header.h:446+ (new structs and members), main.cpp:5284-5285 (new counters).
8. Event tick = the cmd=2 packet tick already passed as `entry.tick` (demo_header.cpp:2296). The dedupe/navigation key is (tick, packetOrdinal, messageBit); tick alone is not unique (see SAME-TICK.md: up to 209/213 events on one tick, 136 copies of one event name on one tick).

## 1. Where the specification comes from (and what was verified)

- The cargo registry has no tf-demo-parser source tree. Commands run and results:
  - `ls -d C:/Users/Administrator/.cargo/registry/src/*/tf-demo-parser-0.5.1` -> no output.
  - `find /c/Users/Administrator/.cargo -maxdepth 6 -name tf-demo-parser*` -> no output.
  - `ls C:/Users/Administrator/.cargo/registry/cache/index.crates.io-1949cf8c6b5b557f/ | grep -i tf-demo` -> no output.
- Therefore the exact published crate was downloaded and used as the immutable reference:
  - `curl -sS -L -o tf-demo-parser-0.5.1.crate https://crates.io/api/v1/crates/tf-demo-parser/0.5.1/download` -> exit 0, 423,555 bytes, sha256 c77f64e1dac5137a201d92afaf441fbe4018595bb8ec770fe6e02f6cad4410ef.
  - Extracted to /tmp/t68-crate/tf-demo-parser-0.5.1.
- The working clone D:/TF2_Demo_Player/work/_refs_demostf is github demostf/parser at e1425a7, Cargo.toml version 0.5.1, but it is NOT byte-identical to the published 0.5.1 (several files differ after CRLF normalisation: src/demo/message/gameevent.rs, src/demo/gamevent.rs, src/demo/parser/state.rs, src/demo/message/mod.rs, src/demo/parser/analyser.rs, src/demo/gameevent_gen.rs). This document cites the published tarball only; the clone is used only for the oracle tool (dump_events.rs) whose output was cross-checked against product counters.
- All quotes below are from /tmp/t68-crate/tf-demo-parser-0.5.1 and /tmp/t68-crate/bitbuffer-0.10.9.

## 2. Message ids and dispatch

src/demo/message/mod.rs:29-64 (6-bit discriminant):

```rust
#[derive(
    BitRead, BitWrite, Debug, Clone, Copy, PartialEq, Eq, Serialize_repr, Deserialize_repr,
)]
#[repr(u8)]
#[discriminant_bits = 6]
pub enum MessageType {
    Empty = 0,
    File = 2,
    NetTick = 3,
    ...
    SetView = 18,
    FixAngle = 19,
    ...
    UserMessage = 23,
    EntityMessage = 24,
    GameEvent = 25,
    PacketEntities = 26,
    TempEntities = 27,
    PreFetch = 28,
    Menu = 29,
    GameEventList = 30,
    GetCvarValue = 31,
    CmdKeyValues = 32,
}
```

Dispatch: src/demo/message/mod.rs:180 `MessageType::GameEvent => Message::GameEvent(GameEventMessage::parse(stream, state)?),` and :189-191 `MessageType::GameEventList => { Message::GameEventList(GameEventListMessage::parse(stream, state)?) }`.

The product already dispatches both types (native/src/demo_header.cpp:2214 type 25, :2218 type 30); no loop change is needed, only the two body decoders.

## 3. svc_GameEventList — definition table

src/demo/message/gameevent.rs:214-223:

```rust
impl BitRead<'_, LittleEndian> for GameEventListMessage {
    fn read(stream: &mut Stream) -> ReadResult<Self> {
        let count: u16 = stream.read_sized(9)?;
        let length: u32 = stream.read_sized(20)?;
        let mut data = stream.read_bits(length as usize)?;
        let event_list: Vec<GameEventDefinition> = data.read_sized(count as usize)?;

        Ok(GameEventListMessage { event_list })
    }
}
```

src/demo/message/gameevent.rs:160-182:

```rust
impl BitRead<'_, LittleEndian> for GameEventDefinition {
    fn read(stream: &mut Stream) -> ReadResult<Self> {
        let event_type: GameEventTypeId = stream.read()?;
        let name: String = stream.read()?;
        let mut entries = Vec::new();

        let mut entry_type = stream.read()?;
        while entry_type != GameEventValueType::None {
            let entry_name = stream.read()?;
            entries.push(GameEventEntry {
                name: entry_name,
                kind: entry_type,
            });
            entry_type = stream.read()?;
        }

        Ok(GameEventDefinition {
            id: event_type,
            event_type: GameEventType::from_type_name(name.as_str()),
            entries,
        })
    }
}
```

Layout (bit offsets relative to the start of the message body, i.e. after the 6-bit message type):

| field | width | notes |
|---|---|---|
| count | 9 bits | number of definitions, max 511 |
| length | 20 bits | payload length in BITS, max 1,048,575 |
| payload | `length` bits | `count` consecutive definitions |
| definition.id | 9 bits | GameEventTypeId, src/demo/message/gameevent.rs:140 `pub struct GameEventTypeId(#[size = 9] u16);` |
| definition.name | null-terminated UTF-8 | event name, e.g. "player_death"; terminator byte is consumed |
| entry.kind | 3 bits | GameEventValueType, 0 terminates the entry list |
| entry.name | null-terminated UTF-8 | key name, e.g. "userid" |

The 20-bit length is the exact bit length of the definition payload, so a decoder can skip the whole table even if it only wants counts.

## 4. svc_GameEvent — one event instance

src/demo/message/gameevent.rs:20-52:

```rust
impl Parse<'_> for GameEventMessage {
    fn parse(stream: &mut Stream, state: &ParserState) -> Result<Self> {
        let length: u16 = stream.read_sized(11)?;
        let mut data = stream.read_bits(length as usize)?;
        let event_type_id: GameEventTypeId = data.read()?;

        // game event definitions haven't been sent yet, ignore
        if state.event_definitions.is_empty() {
            return Ok(GameEventMessage {
                event_type_id,
                event_type: GameEventType::Unknown(String::new()),
                event: GameEvent::Unknown(RawGameEvent {
                    event_type: GameEventType::Unknown(String::new()),
                    values: Vec::new(),
                }),
            });
        }

        let event = match state.event_definitions.get(usize::from(event_type_id)) {
            Some(definition) => GameEvent::read(&mut data, definition)?,
            None => {
                return Err(ParseError::MalformedGameEvent(GameEventError::UnknownType(
                    event_type_id,
                )));
            }
        };
        Ok(GameEventMessage {
            event_type_id,
            event_type: event.event_type(),
            event,
        })
    }
}
```

Layout (relative to message body start):

| field | width | notes |
|---|---|---|
| length | 11 bits | payload length in BITS, max 2047 |
| event id | 9 bits | index into the definition table |
| values | `length - 9` bits | one value per definition entry, in definition order |

src/demo/gamevent.rs:172-183:

```rust
impl RawGameEvent {
    pub fn read(stream: &mut Stream, definition: &GameEventDefinition) -> Result<Self> {
        let mut values: Vec<GameEventValue> = Vec::with_capacity(definition.entries.len());
        for entry in &definition.entries {
            values.push(read_event_value(stream, entry)?);
        }

        Ok(RawGameEvent {
            event_type: definition.event_type.clone(),
            values,
        })
    }
}
```

There is no per-value tag on the wire: the definition is the only schema. A definition/table mismatch silently produces garbage values, so the implementation must validate that the consumed bit count equals `length`.

## 5. Value types and bit widths

src/demo/gamevent.rs:45-57:

```rust
#[derive(BitRead, BitWrite, Debug, Clone, Copy, PartialEq, Display, Serialize, Deserialize)]
#[discriminant_bits = 3]
pub enum GameEventValueType {
    None = 0,
    String = 1,
    Float = 2,
    Long = 3,
    Short = 4,
    Byte = 5,
    Boolean = 6,
    Local = 7,
}
```

src/demo/gamevent.rs:71-82:

```rust
fn read_event_value(stream: &mut Stream, definition: &GameEventEntry) -> Result<GameEventValue> {
    Ok(match definition.kind {
        GameEventValueType::String => GameEventValue::String(stream.read()?),
        GameEventValueType::Float => GameEventValue::Float(stream.read()?),
        GameEventValueType::Long => GameEventValue::Long(stream.read()?),
        GameEventValueType::Short => GameEventValue::Short(stream.read()?),
        GameEventValueType::Byte => GameEventValue::Byte(stream.read()?),
        GameEventValueType::Boolean => GameEventValue::Boolean(stream.read()?),
        GameEventValueType::Local => GameEventValue::Local,
        GameEventValueType::None => return Err(GameEventError::NoneValue.into()),
    })
}
```

| type | id | wire width | value domain | notes |
|---|---|---|---|---|
| None | 0 | 0 | - | only terminates an entry list; never a value |
| String | 1 | variable | UTF-8 bytes | null-terminated; terminator byte consumed; empty string = one 0x00 byte |
| Float | 2 | 32 bits | IEEE-754 f32 | read as 32 LSB-first bits, then reinterpret bits |
| Long | 3 | 32 bits | u32 | |
| Short | 4 | 16 bits | u16 | |
| Byte | 5 | 8 bits | u8 | |
| Boolean | 6 | 1 bit | bool | bit==1 -> true |
| Local | 7 | 0 bits | - | no wire data; value comes from the client's own event state |

Float detail: bitbuffer 0.10.9 src/read.rs:210-225 `impl BitRead for f32 { ... stream.read_float::<f32>() ... fn bit_size() -> Option<usize> { Some(32) } }`, and src/readstream.rs:191-201 reads `size_of::<T>() * 8` bits and advances by that count; src/readbuffer.rs:745-755 reinterprets the assembled bytes with the buffer endianness (LittleEndian), which for LSB-first bit order is equivalent to `u32::from_bits` of the 32 bits read in order. Product equivalent: `read(32, u32)` then memcpy to float (the same pattern already used for send-prop floats at native/src/demo_header.cpp:502-505).

## 6. Bit order proof (LSB-first)

bitbuffer 0.10.9 src/readbuffer.rs:195-207:

```rust
pub(crate) fn get_bits_from_usize<E: Endianness>(
    val: usize,
    bit_offset: usize,
    count: usize,
) -> usize {
    let shifted = if E::is_le() {
        val >> bit_offset
    } else {
        val >> (usize::BITS as usize - bit_offset - count)
    };
    let mask = !(usize::MAX << count);
    shifted & mask
}
```

src/readbuffer.rs:225-238 reads `usize::from_le_bytes(bytes)` then `get_bits_from_usize::<E>(container, bit_offset, count)`; src/readbuffer.rs:301-304 reads a bool as `let shifted = byte >> bit_offset; shifted & 1u8 == 1`. Therefore bit N of the stream is bit (N mod 8) of byte (N / 8), and an n-bit integer's first-read bit is its least significant bit. This matches the product's MessageBits::read (native/src/demo_header.cpp:406-410: `value |= ((bytes_[bit / 8] >> (bit % 8)) & 1u) << i`).

Strings: src/readbuffer.rs:600-631 `read_string` with `byte_len == None` finds the null byte, reads bytes up to it, and src/readstream.rs:304-325 advances by `(result.len() + 1) * 8` bits, i.e. the terminator byte is consumed. Non-UTF-8 input is an error (not lossy).

## 7. Definition table construction and lookup

src/demo/parser/state.rs:224-233:

```rust
pub fn handle_message(&mut self, message: Message, _tick: DemoTick) {
    match message {
        Message::ServerInfo(message) => { ... }
        Message::GameEventList(message) => {
            self.event_definitions = message.event_list;
        }
        ...
```

and src/demo/parser/state.rs:212-222 lists GameEventList in `does_handle`, so the table is captured by the normal parse loop.

Lookup is by index, not by search: src/demo/message/gameevent.rs:38 `state.event_definitions.get(usize::from(event_type_id))`. In both samples the 414 definitions are sent with ids 0..413 in order (verified: ids sequential, min 0, max 413), so index == id. Do not rely on this blindly: if an id >= table size arrives, count it as an unknown event instead of indexing out of bounds, and if a second GameEventList arrives, replace the table (last one wins, matching the crate).

Crate behaviour to NOT copy: if the table is still empty when an event arrives, the crate returns an empty event and does not consume the payload (src/demo/message/gameevent.rs:26-36). The product must treat this as a decode failure (count it), not as a valid empty event, because the event id is still known and the payload must still be skipped.

## 8. Signon vs data packets — where the table lives (empirical)

Reference-crate handler treats signon and data packets identically: src/demo/parser/handler.rs:127-148 `Packet::Message(packet) | Packet::Signon(packet) => { ... for message in packet.messages { ... } }`. So a GameEventList in the signon block installs definitions before any data-packet event.

Empirical scan of the demo entry list (evidence/t68-events/tools/entry_scan.py, exit 0; demo protocol 3 means no player-slot byte):

| demo | entry containing the only svc_GameEventList | cmd | entry tick field | payload bytes | first data packet (cmd=2) tick |
|---|---|---|---|---|---|
| bagel | entry@1030782 (packet index 3) | 1 (signon) | -2147483648 | 17,083 | 1 |
| snakewater | entry@1027907 (packet index 3) | 1 (signon) | 10252 | 16,889 | 1 |
| pl_odyssey POV | entry@346104 (packet index 3) | 1 (signon) | 15170 | 17,119 | 1 |

Supporting tool run (D:/TF2_Demo_Player/work/_refs_demostf/examples/which_packet.rs, `cargo build --release --example which_packet` exit 0):

- bagel: `packet_index=3 kind=Signon tick=2147483648 message_index=0 messages_in_packet=1 count=414 first_def=Some((0, "server_spawn"))` `packets=73125 gameeventlists=1`.
- snakewater: same shape, tick=10252, count=414, packets=64337.
- POV: same shape, tick=15170, count=414, packets=896.

The signon entry's tick field is not a usable tick (bagel sends INT32_MIN; snakewater 10252 and POV 15076/15170 are pre-recording server ticks while the first data packet is tick 1 in all three). Therefore:

- Event tick must come from the cmd=2 data-packet `entry.tick` (product already passes it as `entry.tick` at native/src/demo_header.cpp:2296).
- Definitions must be installed when the GameEventList message is decoded, regardless of command; the product scan loop already decodes cmd=1 and cmd=2 in file order (native/src/demo_header.cpp:2280), so no reordering is needed.
- If a demo ever carries events before its GameEventList (not observed), count them as undecodable instead of inventing values.

## 9. Product code comparison — exactly what has to change

Current product state (native/):

| location | current behaviour | T68 change |
|---|---|---|
| src/demo_header.cpp:790-796 `readGameEvent` | reads 11-bit `payloadBits`, skips it, `++gameEventCount`, `gameEventPayloadBits += payloadBits` | decode id + values inside the payload; keep the same two counters and add decoded/failure counters |
| src/demo_header.cpp:807-815 `readGameEventList` | reads 9-bit `eventCount`, 20-bit `payloadBits`, skips it, `++gameEventListCount`, `gameEventDefinitionCount += eventCount` | parse the definitions (id, name, entries) into a table; keep the counters, add `gameEventDefinitionsParsed` |
| src/demo_header.cpp:2214 (type 25 call) | `readGameEvent(bits, summary)` | pass `entryTick`, `packetOrdinal`, `messageBit` |
| src/demo_header.cpp:2218 (type 30 call) | `readGameEventList(bits, summary)` | unchanged signature (definitions go into summary) |
| src/demo_header.cpp:2174-2179 `decodeDemoMessageStream` | params (payload, payloadBits, entryTick, initialNetworkTick, summary, result) | add a packet-ordinal parameter (the entry index from the scan loop) so events can be keyed (tick, packetOrdinal, messageBit) |
| src/demo_header.cpp:2192 | `messageBit` is already captured before each message | reuse as the third dedupe key component; do not drop it |
| src/demo_header.cpp:2296-2297 call site | passes `entry.tick` | also pass the entry ordinal (loop index over index.entries) |
| include/demo_header.h:427-434 `DemoViewSample` | existing tick/sample struct; use as the model for new structs | insert new event structs nearby |
| include/demo_header.h:446 `DemoNetworkSummary` | has gameEventListCount/gameEventDefinitionCount/gameEventCount/gameEventPayloadBits at :710-713 | add `std::vector<GameEventDefinition> gameEventDefinitions;`, `std::vector<DemoEventSample> gameEvents;`, `gameEventDefinitionsParsed`, `gameEventDecoded`, `gameEventDecodeFailures`, `gameEventUnknownId` |
| src/main.cpp:5284-5285 | prints `eventlists=` and `events=` only | append `eventdefs=`, `eventdecoded=`, `eventfail=` (keep existing fields byte-identical) |
| src/demo_header.cpp:400-435 `MessageBits` | flat reader over the whole payload; `read`/`skip` check only the global end and `minimumRemaining_`; no way to bound a sub-message | add a bounded sub-stream (e.g. constructor taking start bit + bit length, or a `setLimit`), so event value reads cannot cross the 11-bit event boundary; otherwise a malformed length silently eats the next message |

Existing counters and helpers that can be reused unchanged:

- `gameEventListCount`, `gameEventDefinitionCount`, `gameEventCount`, `gameEventPayloadBits` (include/demo_header.h:710-713).
- `MessageBits::read` (LSB-first, ≤32 bits), `readString` (null-terminated, default maxLength 1024), `skip`, `offsetBits`, `remaining` (src/demo_header.cpp:406-417).
- The 6-bit message loop already records `messageBit` per message (src/demo_header.cpp:2192) and the 11-bit event length is already read correctly.

New data structures (suggested, names are not part of the contract):

```cpp
enum class GameEventValueType : std::uint8_t { None = 0, String = 1, Float = 2, Long = 3, Short = 4, Byte = 5, Boolean = 6, Local = 7 };
struct GameEventEntry { std::string name; GameEventValueType kind = GameEventValueType::None; };
struct GameEventDefinition { std::uint32_t id = 0; std::string name; std::vector<GameEventEntry> entries; };
struct GameEventValue { GameEventValueType kind = GameEventValueType::None; std::string text; std::uint32_t number = 0; float real = 0.0f; bool boolean = false; };
struct DemoEventSample {
  std::int32_t tick = 0;            // cmd=2 packet tick (playback tick domain)
  std::uint64_t packetOrdinal = 0;  // entry index in the demo file
  std::uint32_t messageBit = 0;     // bit offset of the 6-bit message type inside the packet payload
  std::uint32_t eventId = 0;
  std::string name;
  std::vector<GameEventValue> values;  // same order as definition.entries
};
```

## 10. Decode algorithm (exact)

```text
readGameEventList(bits, summary):
  read 9  -> count
  read 20 -> payloadBits
  start = bits.offsetBits()
  if start + payloadBits > start + bits.remaining(): fail
  sub = bounded_view(bits, start, payloadBits)
  defs = []
  for i in 0..count:
    read 9 -> id
    readString -> name
    entries = []
    loop:
      read 3 -> kind
      if kind == 0: break
      readString -> entryName
      entries.push({entryName, kind})
    defs.push({id, name, entries})
  if sub.remaining() != 0: fail (or record padding; real data has none)
  summary.gameEventDefinitions = defs      // replace, last one wins
  ++summary.gameEventListCount
  summary.gameEventDefinitionCount += count   // existing field, server-reported count
  summary.gameEventDefinitionsParsed = defs.size()

readGameEvent(bits, summary, entryTick, packetOrdinal, messageBit):
  read 11 -> payloadBits
  start = bits.offsetBits()
  if start + payloadBits > start + bits.remaining(): fail
  sub = bounded_view(bits, start, payloadBits)
  read 9 -> id
  if id >= summary.gameEventDefinitions.size():
    ++summary.gameEventUnknownId
    skip to start + payloadBits
    return true   // structurally valid, semantically unknown; counted
  def = summary.gameEventDefinitions[id]
  values = []
  for entry in def.entries:
    switch entry.kind:
      String:  readString(text)                       // bound: sub.remaining()/8
      Float:   read 32 -> bits; memcpy to float
      Long:    read 32 -> number
      Short:   read 16 -> number
      Byte:    read 8  -> number
      Boolean: read 1  -> boolean
      Local:   no bits
      None:    fail
    values.push(value)
  if sub.remaining() != 0: ++summary.gameEventDecodeFailures (do not fail the packet)
  else: ++summary.gameEventDecoded
  summary.gameEvents.push_back({entryTick, packetOrdinal, messageBit, id, def.name, values})
  ++summary.gameEventCount
  summary.gameEventPayloadBits += payloadBits
  skip to start + payloadBits   // always, even on value failure
```

Implementation cautions:

1. Never read past `start + payloadBits`: the next 6-bit message type starts exactly there.
2. `readString` with the default 1024-byte cap is enough for the observed data (max event string length 527 bytes in bagel, 524 in snakewater) but is not a correctness bound; derive the limit from the remaining payload.
3. `Local` consumes 0 bits, so a definition with several Local fields produces no wire bytes; do not advance.
4. Boolean is one bit; do not round up to a byte.
5. Event values are bit-packed at arbitrary bit offsets (the message header is 17 bits), so byte-level copying is invalid.
6. Keep the existing `gameEventCount`/`gameEventPayloadBits` semantics; a gate comparing old readings must not see them change.

## 11. Acceptance criteria for the T68 implementation

1. `events=` stays 12982 (bagel) / 10703 (snakewater) — equal to the T64 oracle totals.
2. `eventlists=1`, `eventdefs=414` on both demos.
3. For each of the six golden event names in evidence/t68-events/golden/, the decoded (tick, name, values) rows equal the golden TSV byte-for-byte after the documented formatting (values alphabetically by key, tab-separated, LF).
4. Same-tick fixture evidence/t68-events/golden/same-tick-334.tsv: 209 rows for bagel and 213 for snakewater at tick 334, in wire order.
5. Negative control: a tick-only dedupe must fail the fixture (209/213 events on one tick; 136 ammo_pickup events at tick 334), while (tick, packetOrdinal, messageBit) keeps all rows.
6. Any decoded event with an id not in the table or a value count mismatch is counted in a failure counter and named in the report — never silently dropped, never turned into an empty event.

## 12. NOT VERIFIED / risks

- No pl_* payload demo exists on disk. Cart progress/checkpoint is entity state (team_train_watcher), not a game event, so no cart event is verified here. The static BSP check (T64) found team_train_watcher only in pl_odyssey.bsp.
- The only POV demo is a 437-tick / 11-event connect fragment; it is not a usable payload sample.
- No Local-typed field appears in any definition actually used by the two samples (all used field types: String, Float, Long, Short, Byte, Boolean). Local decoding is unexercised.
- No event with an id outside the definition table was observed, so the unknown-id path is unexercised.
- No malformed/truncated event payload was observed; the failure paths above are design requirements, not measured behaviour.
- userid -> player name is not decodable: the product has no userinfo string-table decoder (T64 G5), so golden rows carry numeric userids only.
- Definition ids are sequential 0..413 in both samples; non-contiguous or duplicate ids are unverified.
- All six golden event types come from two SourceTV demos of two map types (KOTH, 5CP); event field coverage for other game modes is unknown.
