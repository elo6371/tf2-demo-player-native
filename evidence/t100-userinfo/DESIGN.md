# t100-userinfo: svc_UserInfo is not where the names are

Task: the product can draw a HUD, but every player name is `NAME ?` because no
code path decodes a player name at all (T63 FIELD_TABLE G7, T68 G5, T70 DESIGN
§5.2 item 9). This worktree adds the decoding, a probe that prints it, and a gate
that fails when the decoding silently produces nothing.

## 1. The finding that shaped the fix

The task title said `svc_UserInfo`. Team Fortress 2 demos in this corpus do not
carry that message, and the name is not in a net message at all:

* The demostf parser (`src/demo/message/mod.rs`, fetched 2026-11-09) enumerates
  the message types it decodes in production for TF2 and has no userinfo variant;
  it does have `Print = 7`. 32 is the highest type it knows.
* This tree already treats type 7 as `svc_Print` and has no arm for 16
  (`native/src/demo_header.cpp`, dispatch). Every existing corpus gate asserts
  `unknown_message_packets = 0` on all nine demos, so a real type 16 would have
  been counted as unknown long ago. It never was.
* The tree's own evidence says where the names are:
  `evidence/t65-minimap/player_position_coverage.md:101` ("names come from the
  userinfo string table, which the probe does not print"),
  `evidence/t64-events/DATA-CONTRACT.md:51-52` (`name=null`,
  `nameReason="no-userinfo"`), `evidence/t68-events/GAME-EVENT-SPEC.md:435`.

So the fix reads the **`userinfo` string table**, whose per-entry user data is a
verbatim `player_info_t`. No new message type is added, and the message-type
histogram does not move: that is why the additive probe output can be compared
against the older gates without a story about a new packet appearing.

## 2. One entry encoding, three call sites

The create / update / `dem_stringtables` paths all read the same entry encoding
(sequential flag, index, text with the 32-entry history, user data either fixed
width or 14-bit length). Before this change only the precache tables walked it,
and the walker was private to the precache path, so the userinfo table was read
past and thrown away.

`walkStringTablePayload` is now the single implementation, templated on a sink:

* `PrecacheSink` keeps the old sound/model precache behaviour.
* `InstanceBaselineSink` keeps the old instancebaseline behaviour (it replaced
  the duplicate `readInstanceBaselinePayload`).
* `UserInfoSink` is new: it hands each entry to `recordUserInfoEntry`.

The alternative -- a second, userinfo-only copy of the entry decoder -- would
have had to reproduce history stacks and the 14-bit length field, and any future
protocol fix would have had to be made twice. One decoder that the precache gates
already exercise is the safer shape.

Each sink is called with the table entry index, which for this table is the
client slot; the entity number the HUD needs is `slot + 1` and is recorded as
`entityIndex`.

## 3. player_info_t, and why an unparsed entry is counted

The engine writes `player_info_t` (public/cdll_int.h) verbatim into the entry's
user data. Two layouts are accepted:

| layout | name | userid | guid | friendsId | friendsName | fake | hltv | min bytes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 0 | 32 | 36 | 72 | 76 | 108 | 109 | 132 |
| 1 | 16 | 48 | 52 | 88 | 92 | 124 | 125 | 148 |

Layout 0 is the plain struct; layout 1 is the versioned variant that starts with
a version and an xuid. Layout 0 is tried first and both are gated on
`raw.size()`, so layout 1 cannot win on a short blob.

Wrong offsets do not fail politely -- they yield a plausible-looking name made of
random bytes. The decoder therefore rejects a layout unless **all** of these hold:

* the two boolean bytes are exactly 0 or 1 (a wrong offset passes that with
  probability 255/256 per byte);
* the name field contains a NUL inside 32 bytes, is non-empty, and has no byte
  below 0x20 or equal to 0x7F (the engine strips control bytes; random ones
  would otherwise sail through);
* the guid field is NUL-terminated printable ASCII within 33 bytes;
* the userid is in `[1, 2^20]` -- the engine hands out sequential ids from 1, so
  0 or a huge value is a misread, not a player.

An entry whose user data fails every layout is **counted**
(`userInfoUnparsedCount`) and its name left empty; it is never replaced by a
guess. Entries with no user data at all are counted separately
(`userInfoMissingUserDataCount`) and also keep an empty name: the table's entry
text is the client slot number (a decimal string), not a name, so copying it
would dress an empty slot up as a player called "18". That is exactly the silent
substitution this gate exists to reject.

The table text is still checked, in the other direction: for a parsed entry it
must equal the decimal slot number. A decoder that reads the entry index from the
wrong half of the entry, or that indexes slots by the text, breaks
`userInfoTextSlotMismatchCount` even when the name looks fine.

## 4. How a dirty tree is expressed (and why silence is the bug)

The defect this gate exists to prevent is not a crash; it is a decoder that
returns an empty table and reports success. So the summary keeps the
distinguishing counts, and the probe prints them:

* `userInfoEntryCount` -- entries seen;
* `userInfoDecodeFailures` -- the table payload could not be walked (truncated
  or corrupt);
* `userInfoUnparsedCount` -- walked, but no layout matched;
* `userInfoMissingUserDataCount` -- walked, no user data;
* `userInfoEmptyNameCount`, `userInfoTextSlotMismatchCount`,
  `userInfoTableResetCount`, `userInfoLayoutMask`.

The negative control uses four synthetic messages fed straight into
`decodeDemoMessageStream` (no .dem needed; the same public entry point
`entity_message_fixture_probe.cpp` uses). `gen_fixtures.py` writes them, so the
bytes are in the tree and can be regenerated and compared:

| fixture | bytes | what it is | signal the gate requires |
| --- | --- | --- | --- |
| `userinfo-good.demmsg` | 296 | two players, Alpha/7 and Bravo/9 | 2 entries, 2 names, 0 failures, both layouts |
| `userinfo-truncated.demmsg` | 256 | the same message, last 40 bytes gone, lengths still declare the full payload | the stream must **not** look clean: `packet_valid=0` |
| `userinfo-shortpayload.demmsg` | 164 | honest lengths, 1056 bits short of what the entries need | `userInfoDecodeFailures >= 1` while the packet itself stays valid |
| `userinfo-badname.demmsg` | 296 | correct structure, name bytes are control characters | `unparsed = 2`, `named = 0`, entries still 2 |

A gate that only ran the good fixture would pass on a decoder that never
validated anything. The two damaged fixtures catch the two different ways a
decoder lies: the truncated one is stopped by the outer stream, the short-payload
one walks into the end of the payload with the outer lengths all satisfied. Both
must be **distinguishable** from the clean reading, and the gate explicitly
rejects the silent shape `entries == 0 && failures == 0 && unparsed == 0`.

## 5. Seek stability

Each slot's value is kept twice: the accumulated entry (first/last tick, update
count) and a sample list of every distinct `(name, userId)` observation in wire
order. `findUserInfoAtTick` picks the sample with the greatest tick `<= T`, so a
query does not depend on the order in which ticks are asked for, and re-asking
the same tick gives the same answer. `--userinfo-at` prints those answers; the
gate runs the same demos with the tick list forward, reversed, and with a tick
repeated, and requires byte-identical rows for the same tick. The repeated tick is
600000, i.e. after the table exists: repeating a pre-signon tick would give two
empty blocks, which compare equal and assert nothing.

Two things this is **not**: it is not "the name never changes" (a sample list
exists precisely so a changed name can be answered at the right tick), and it is
not "ask at any tick and get something" -- the table is created at signon, so a
tick before it must resolve nothing. The gate asserts both directions: every
named slot resolves at the last requested tick, and tick 2000 resolves no row at
all. A query that ignored its tick argument would pass the first and fail the
second.

This is what the HUD needs: a checkpoint's entity state and the name shown for it
must come from the same moment, not from "whatever the last pass over the demo
saw".

## 6. What the gate does and does not assert

Asserted (each value assertion is preceded by an existence assertion on the same
reading, so a missing line cannot pass as a match):

Preconditions, not claims: the probe binary and every input exist, and the binary
is newer than every source file under `native/src`, `native/include`,
`native/tools`. These have no mutant -- they gate whether the rest is meaningful.

Fixtures:

* good: two entries, two slots, two names, two layout-0 rows, exact
  `entry/entity/userId/name/layoutId/tableText` for both players, zero failures,
  zero unparsed, zero text/slot mismatches;
* truncated: the stream is rejected (`packet_valid = 0`) and the reading is not
  the shape `packet_valid` and `entries==0` and `failures==0` and `unparsed==0`;
* short payload: one complete entry decoded, `failures >= 1` -- the walk that ran
  into the end of the payload is counted instead of looking like a one-player table;
* bad name: two entries, two unparsed, zero names, and not zero entries (a decoder
  that dropped the entries would "pass" a `named == 0` check).

Corpus (each of the three demos): `table_id != 0xffffffff`, `create >= 1`,
`entries >= 1`, `slots >= 1`, `named >= 1`, zero failures, zero unparsed, zero
text/slot mismatches, zero duplicate userids, a layout matched; every row has
`entity == slot + 1` and `tableText == slot`, every name is printable, every row
is either (name non-empty, layout 0) or (name empty, layout 0xff), the number of
named rows equals the `named` counter, and the rows equal a recorded snapshot.

Seek stability as described in §5.

Mutants (`--mutation`), one per claim:

| tag | one-line change | named assertion that must go red |
| --- | --- | --- |
| m1 | drop `++userInfoUnparsedCount` | badname counted as unparsed |
| m2 | drop the create-arm walk failure count | shortpayload counted the truncated walk |
| m3 | read the name four bytes late | good rows are the players that were written |
| m4 | `entityIndex = entryIndex` | good rows are the players that were written |
| m5 | compare the text with `slot + 1` | good entry text matches its slot |
| m6 | drop the tick ceiling in the query | a tick before the table resolves no name |
| m7 | never take the create arm | good entries (2) |

Each mutant must compile (`build rc = 0`), make that named assertion fail, then be
restored byte-for-byte (`cmp`), rebuilt, and the restored gate must be green.

Not asserted: that a name equals what a human would see on that server today.
The corpus names are a snapshot of nine frozen demo files, recorded so a decoder
change is visible; they are not a claim about the servers.
