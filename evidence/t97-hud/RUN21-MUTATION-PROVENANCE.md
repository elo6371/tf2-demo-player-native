# RUN21 mutation provenance — the T97 HUD gate, 2026-11-09 23:44–23:57

The tracked `evidence/t97-hud/mutation/readings.txt` is **not** a reading of the
mutation round that ran on the night of the ninth: it names exe `65533cb3…` and
gate `d29bb95c…` (commit 8290374, 21:46), and the round below ran on a later
gate and a later binary. The mutation round left its readings in `.scratch`,
which `.gitignore:35` does not track. This file is the durable record of that
round; it adds no numbers the logs do not contain.

## 1. Run identity

| what | value |
|---|---|
| lock owner | `t91-lead` — `ACQUIRED owner=t91-lead at 2026-11-09 23:44:58` (hudmut.log:1), `LOCKED-COMMAND-EXIT rc=0 owner=t91-lead` (hudmut.log:135) |
| lock release instant | not recorded (hostlock keeps no release journal; the log's last write is 23:57) |
| HEAD | `f2c4fbd89193969729a7155aae2c798af37b0902` — no commit landed between f2c4fbd (22:18:42) and f76c276 (23:58:25), so the run happened on f2c4fbd |
| exe | `native/build-nmake/tf2_demo_native.exe` sha256 `95aff0db01f8785560235aed29208eb90573f8aa62f3d180107d1c64ac7cba3c`, 2,838,528 B, mtime `2026-11-09 23:44:21` (hudmut.log:3) |
| gate | `hud-live-check.sh` — disk CRLF `18ea8165…` (**the form the run recorded**), LF blob `7a789d6e…`, blob `f822a5fc…` (= `git rev-parse f2c4fbd:hud-live-check.sh`) |
| demo | `…matcha-20260927-1347-cp_snakewater_final1.dem`, 51,834,617 B, sha256 `308cc145…` |
| ticks | `tick=0 advance_tick=9000` |
| scratch | `.scratch/t97-hudlive/20261109234459-279962/` (gitignored) |
| verdicts | 65 OK / 0 FAIL forward; `MUTATION-SUITE=PASS` (hudmut.log:132); `HUD-LIVE-CHECK=PASS` (:133); `HUDMUT-EXIT=0` |
| raw log | `/d/_shared/EXEC-2026-11-09/RUN21-HUDMUT/hudmut.log`, 135 lines / 7,325 B, sha256 `fe8ca4034f7bea8319c0a9fd2123a6f5e7687753d620854acb114f5f52edd28f` |

The exe sha is the one the forward pass wrote into
`evidence/t97-hud/readings.txt` before the mutation suite started; that block runs
before any patch is applied (gate :660-680, log line 94), so `95aff0db…` describes
the unmutated binary. The exe was rebuilt at 23:44:21 — earlier in the same night
the gate measured `37388f4a…` on the same HEAD (RUN19, 22:26); why the binary moved
between 22:26 and 23:44 is not recorded in these logs and is **unverified** here.

## 2. The three mutations, with the named assertions each one reddened

Each case patches one surgical edit into `native/src/main_hud.cpp` (`patch_case`,
gate :711-738), rebuilds, runs the gate as a subprocess with `--sections=` and
`HUD_LIVE_CHECK_OUT="$SCRATCH/mut-<case>-out"` (gate :754-766, the subprocess at :765), then requires the
expectation line to appear **and** counts the named FAILs. Full lists below; the
per-case readings the log counted them from are in `$SCRATCH/mut-<case>.txt`.

### m1-drop-text — 13 named reds (hudmut.log:101-109, all 13 in `mut-m1-drop-text.txt`)
```
FAIL text3 does not print an honest unknown name: "<none>"
FAIL the clock line is not empty = 0, expected >= 6
FAIL text1 has no clock shape: "<none>"
FAIL text2 has no score shape: "<none>"
FAIL text3 has no target shape: "<none>"
FAIL the live frame draws every primitive it published = 0, expected 3
FAIL the HUD pass runs after the world = 0, expected >= 1
FAIL the readout publishes three text primitives = 0, expected 3
FAIL every drawn primitive is a text primitive = 3, expected 0
FAIL the pixel verifier exits clean = 1, expected 0
FAIL the published text paints pixels = 0, expected >= 100
FAIL the pixel verifier's verdict = 0, expected 1
FAIL the hud-off frame is readable and differs from the live frame = identical, expected different
```
Mutated binary: `d41e2f57e064eb2155fe477fd2033cdd75964817d9e9ec36270a185fe5b6b386` (`mut-m1-drop-text-out/readings.txt`).

### m2-freeze — 4 named reds (hudmut.log:115-119)
```
FAIL the clock reading changes when the demo advances = TIME 10:00 PAUSED, expected something else than TIME 10:00 PAUSED
FAIL the advancing clock did not count down (600 vs 600)
FAIL the text is rebuilt when a reading changes = 1, expected >= 2
FAIL the clock text differs between the first and the last publish = TIME 10:00 PAUSED, expected something else than TIME 10:00 PAUSED
```
Mutated binary: `bacdc52cd8824fc65aa664b52eadc82aba0d39061152458eb6ac9b53aa648597`.

### m3-fake-name — 2 named reds (hudmut.log:125-127)
```
FAIL the reading declares the name unavailable = Player, expected unavailable
FAIL text3 does not print an honest unknown name: "NAME Player  HP 1  ?  ?"
```
Mutated binary: `cde7a979459b927b939ab52b9ad8117d7eb96b490f00e7eb7d4b31d7360b2e14`.

What this establishes: three separate edits — the text never composed, the reading
frozen after the first publish, and an invented player name — each redden a
**named** assertion whose subject is that edit. What it does not establish: that
these are the only ways the feature can break, or that any other gate behaves this
way (the audit's F4/F5/F6 weaknesses in the gate header still stand).

## 3. Restore, and the restored build

```
RESTORED-IDENTICAL native/src/main_hud.cpp (5475791bace9525328361afa5dd948380c30710cc34a7f921c998abfef01c33f)   (hudmut.log:110, :120, :128)
source restored sha=5475791bace9525328361afa5dd948380c30710cc34a7f921c998abfef01c33f                          (hudmut.log:134)
OK   the restored build reproduces the forward reading                                                        (hudmut.log:131)
```
`5475791b…` is the **worktree** file the gate hashed (37,060 B). The same source is
`c50adf9e…` as a git blob (36,271 B, LF; `core.autocrlf=true` adds one byte per
line on disk), blob id `32cdc12d…`, and it is unchanged between f2c4fbd and HEAD.

The restored rebuild produced `654771f5ef2dbf4957606583f667ab8b112cb7372e15a8152f4b9db580e60ff5`
(`restored-out/readings.txt`) — a different binary from the forward `95aff0db…`
because the rebuild carries a different dirty count in its stamp (D-54) — and its
run reproduced the pinned forward values (`publishes=1`, `pixels outside=0
inside=3504 glyphBad=0`, `suppress_frame=identical`).

## 4. Relation to the forward runs

| run | when | HEAD | gate (disk) | exe | forward verdict |
|---|---|---|---|---|---|
| RUN19 hud2 | 22:26:50–22:32:36 | f2c4fbd | 18ea8165 | `37388f4a…` | `HUD-LIVE-CHECK=PASS`, 65 OK / 0 FAIL |
| RUN21 forward (this round) | 23:44:59–23:57 | f2c4fbd | 18ea8165 | `95aff0db…` | `HUD-LIVE-CHECK=PASS`, 65 OK / 0 FAIL |

Same gate bytes and same HEAD, different binaries: both forwarded green, and the
pinned readings agree (`TIME 10:00 PAUSED`, `glyphs=47`, `region=24,24,288,60`,
`inside=3504`, `suppress_frame=identical`). One unpinned reading differs: the frame
tally is 512 in RUN19 and 608 here (`advance_skips` 510 vs 606); both satisfy the
asserted invariant `calls == updates + skips`, which is exactly what F5 says that
identity proves.

Because the forward block always writes `$OUT/readings.txt`, this round
**overwrote** `evidence/t97-hud/readings.txt`; the file now tracked in the tree
names exe `95aff0db…` and is therefore a reading of this round, not of RUN19.

## 5. Known limits

1. **The mutation suite writes no reading into the tree.** Gate :678 writes
   `$OUT/readings.txt`; each case runs with `HUD_LIVE_CHECK_OUT="$SCRATCH/mut-<case>-out"`
   (:763) and the restored run with `…/restored-out` (:788). So the per-case
   readings exist only under `.scratch` (gitignored, .gitignore:35), and the tracked
   `evidence/t97-hud/mutation/readings.txt` — exe `65533cb3…`, gate `d29bb95c…` —
   predates this round and must not be read as its reading.
2. **The chain proves the forward half only.** `verify-release.sh:89` lists
   `hud-live` as step 14 of 15 and runs `bash hud-live-check.sh` with no
   `--mutation`; :386 only redirects `HUD_LIVE_CHECK_OUT`. A green chain says
   nothing about the mutation suite.
3. **A `--mutation` run overwrites the forward reading**, so
   `evidence/t97-hud/readings.txt` describes whichever run wrote it last — here a
   mutation round, whose forward half is the thing it measures.
4. **Nothing machine-checkable pins this document.** The per-case binaries and
   scratch traces are not in the tree; the exe sha256 values above are the only
   anchors, and they are gone from disk (the worktree binary has been rebuilt since).
5. F4/F5/F6 from the gate header (readings pinned to tick-0 literals; counter
   identities that are control-flow tautologies; `ordinal>=1` being one-directional)
   are unchanged by this round and are not re-litigated here.

Open design question for the lead (not decided here): should `--mutation` write its
own reading — e.g. `$OUT/mutation-readings.txt` or `$SCRATCH` promoted into
`evidence/` — so that a mutation round is reviewable from the tree instead of from a
hand-written provenance file? Until that is decided this file is the record.
