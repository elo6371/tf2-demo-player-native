# N21 audio inventory, 2026-11-16 (read-only, no new run)

Status: **inventory, not diagnosis.** The card asked what audio the build actually consumes. This
document records what the demo carries, what the product does with it, and the one accounting question
that cannot be settled with the binaries on the shelf today. It deliberately does **not** pick a cause.

Evidence reused, not regenerated: the P3a capture under the host lock,
`D:/_shared/EXEC-2026-11-15/INST4/p3a-n20/fx-84000.trace` (16,019,920 B), exe sha256
`e61c7fd9e5dba5167463f493937b23be799c96bcdd35bee7e0970d747a3bb078`, demo sha256
`7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3` (pl_upward_f12), captured with
`--capture-tick 84000 --playback-speed 32 --audio-disabled`. The live-inventory constraint was
read-only (no build, no product run, no probe), so everything below is either a code reading or a
grep over that trace.

## 1. What the demo carries

`./native/build-nmake/demo_open_probe.exe --scan <demo>` (read-only, CPU) prints:

```
sounds=35278 sound_failures=20923
```

`sounds=` here is `summary.soundEventCount` - the **declared** event count summed from each sound
message's header (`native/tools/demo_open_probe.cpp:844`), i.e. what the demo says it contains.
`sound_failures=` is `summary.decodedSoundEventFailures` (`demo_open_probe.cpp:845`).

## 2. What the product does with it

Trace readings from the same capture:

```
audio: timeline firstPacketTick=1 soundOriginTick=1 originFound=1 read=5528 scheduled=5482
                 unresolved=46 unplaceable=0 refused=0 kinds=w3185/f0/u4/o1727/v562/a4/m0
audio: summary soundEventsRead=5528 scheduled=5482 played=0 skippedVoice=566 missingResource=2302
                inaudible=1007 skippedDisabled=2614
audio: advance tick=84000 cursor=84000 seen=5482 played=0 skippedVoice=566 missing=2302 dup=0
```

`read=5528` is `buildSoundTimeline`'s `counts.read = events.size()`
(`native/src/demo_header_sound.cpp:136`), i.e. the decoded-event list, not the declared count.
`missingResource=2302`, `inaudible=1007`, `skippedVoice=566` are scheduler outcomes, not decode
failures; `played=0` is `--audio-disabled` by construction (`native/src/main.cpp:7088-7098`).

Chain, with anchors:

1. `readSounds` (`native/src/demo_header_sound.cpp:23`) reads a message header, keeps the raw payload
   only when `payloadBits < 64*1024` and `rawSoundMessages.size() < 4096` (:42; the else branch bumps
   `rawSoundMessagesDropped` at :52), then decodes `soundCount` delta events (:65).
2. Each event starts from the previous event (`summary.soundDeltaState`, :66) so unchanged fields are
   inherited; `event.valid = event.soundIndex != 0` (:120); the event is stored only when valid and the
   list is under 16384 (:123 - the same cap family as the effect list, D76).
3. After the whole demo is scanned, `resourceName` is resolved from the sound precache string table
   (`native/src/demo_header.cpp:437-442`).
4. `buildSoundTimeline` (`native/src/demo_header_sound.cpp:132-168`) drops nameless events as
   `unresolved` (:141), unplaceable ones (:147) and refusals (:164) - all counted, none silent.

## 3. The gap, and the two candidate mechanisms (both open)

`35278` declared vs `5528` in the list. **Do not present either mechanism as the answer.** They are
not mutually exclusive, and today's binaries cannot weigh them.

**M1 - the message breaks early.** Every read failure inside the per-event loop does
`++summary.decodedSoundEventFailures; break;` (:69-118). The `break` leaves the loop but the function
still finishes and `summary.soundEventCount += soundCount` (:126) adds the **full declared** count. So a
message that declares 4 events and dies on the second contributes 4 to the declared total and 1 to the
decoded list. `sound_failures=20923` is the number of such **messages**, not events.
Discriminator needed: events decoded per message before the break (a new counter).
Why not today: no binary prints it (see §4).

**M2 - delta events that carry no new sound are not stored.** `event.valid` requires
`soundIndex != 0` (:120). Because each event is seeded from the previous one (:66), an update that
resends only volume/pitch/origin normally **inherits** the previous index and *is* stored - so M2 is
only the case where no index was ever established (first event of a stream, or after a state reset).
Discriminator needed: count events whose `soundIndex` is 0 at the validity gate, split into
"had prior state" / "no prior state".
Why not today: same missing counter, and the inheritance path makes the weight of M2 a measurement
question rather than an argument.

**M3 - a structural third path, recorded because it will confuse the next person.** Messages that fail
before the per-event loop - bad header, `payloadBits > bits.remaining()`, or the raw-payload drop path
(:27-54) - return `false` to `native/src/demo_header.cpp:180`, which only marks the packet invalid.
Those messages are added to **neither** `soundEventCount` (:126) nor `decodedSoundEventFailures`;
and when the raw payload was dropped, the decode loop then runs against an empty buffer, so the same
message also counts one failure. Any reconciliation of declared-vs-decoded has to account for messages
that no counter sees.
Discriminator needed: `rawSoundMessagesDropped` and a total sound-message count. Neither is printed.

## 4. Why the split is not observable today (verified, not assumed)

The capture's trace contains the temp-entity counters per frame (`tecached=16384`,
`teparticle=986`, `tebullets=4307`, `teproj=0`, `teprops=198593`, 934 lines each) but contains
**no** sound counter line: `grep -a -c 'sounds=' trace` = 0 and `grep -a -c 'sounddecoded' trace` = 0,
even though those counters exist in the tree at `native/src/main.cpp:7203-7216`. Whatever block those
belong to is not the one this binary emits. So the emitted evidence stops at
`--scan` (declared + message-level failures) plus the three `audio:` lines (decoded list size,
scheduling outcomes). `decodedSoundEvents.size()` is the only decoded-side number and it collapses
M1, M2, M3 and the 16384 cap into one integer.

## 5. Two name collisions that will produce fake findings

1. `sounds=` means **declared events** in `--scan` (`demo_open_probe.cpp:844` -> `soundEventCount`)
   but **message count** in the tree's demo summary (`native/src/main.cpp:7203` -> `soundMessageCount`).
   Comparing the two numbers across sources produces a bogus gap.
2. `projectiles=` means `projectileTimeline.size()` in `--scan` (`demo_open_probe.cpp:843`) but
   `tempClientProjectileCount` in the trace (`native/src/main.cpp:7175` -> `teproj=0` vs
   `projectiles=1538`). Same word, different quantities.

One genuine internal consistency check that does line up: the HUD token `snd=3185/0/4` equals the
`kinds=w3185/f0/u4` weapon/footstep/uber triplet from `audio: timeline`.

## 6. How the next person recomputes every number here

Read-only, needs no lock and no build:

1. `./native/build-nmake/demo_open_probe.exe --scan D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem`
   -> `sounds=` (declared) and `sound_failures=` (message-level decode breaks).
2. If the P3a capture is present, no run is needed:
   `grep -a 'audio: timeline\|audio: summary\|audio: advance' <trace> | tail -3` gives read/scheduled/
   unresolved/kinds and the scheduler outcomes; `grep -a -o 'snd=[0-9/]*' <trace>` gives the HUD triplet.
3. To settle M1/M2/M3, a source change is required (a build, therefore a lock window): add
   (a) events decoded per message before the break, (b) events rejected by `event.valid` split by prior
   state, (c) print `rawSoundMessagesDropped` and a total message count. Until those three exist, the
   gap stays "unexplained, candidate mechanisms M1/M2/M3".

## 7. What this does and does not say

- Does say: the demo declares 35278 sound events, 20923 messages break mid-decode, and the product's
  decoded list is 5528 with the scheduler turning 5482 into timeline entries, 2302 of which have no
  resolved resource.
- Does not say: that audio is broken, that any one mechanism dominates, or that the 16384 cap matters
  here (5528 is below it). No claim about playback quality is possible from `--audio-disabled` runs.

Related: D76 (effect list built from a truncated table, no discard counter) covers the same cap family
on the effect side; this document does not open a new defect entry.
