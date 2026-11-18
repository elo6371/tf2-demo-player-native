# ViewModel companion gate -- the 2026-11-16 re-pin

Date: 2026-11-16
Gate: `viewmodel-companion-check.sh`
Companion gate commit: this batch (re-pin of the history/witness family; see the commit message).

## What changed, in one paragraph

The companion gate's Section 2 was characterised on `autorecord_2026-07-02_13-26-46.dem`
(`21,856,667 B`, sha `c8a58501...`) when the gate was introduced in `9cfc9139` (2026-10-12).
That recording is no longer on disk (`docs/GATE-UNBLOCK-2026-11-15.md:9`), and the gate has
read `autorecord_2026-08-08_21-43-59.dem` since `435b4d5c` (2026-11-16 12:43) -- which is the
recording the N01 plan A replacement left in place. On the 8-08 recording the entity carries
ONE sequence for the whole window (`28`), not three, so eleven assertions that described the
07-02 recording's sequence traffic turned red (113 OK / 11 FAIL on matrix run 2,
`evidence/fast/e1-round5-matrix/20261116T121759Z/matrix/companion.log`). The proof itself never
moved: in the same run `entityModelMatchesDrawn=1` on every witness, `modelMismatch=0`,
`modelMatches=observations=1645`.

This is a re-pin of the description, not a weakening of the proof: every number below is read
off the pinned recording (or the model the entity names), and each replaced assertion now
carries a control that shows it can still fail. Nothing was relaxed by deleting a reading: the
four assertions that could only read a single-sequence constant were replaced by mechanism
assertions over the tick plumbing (`--capture-tick`), and the one claim they used to carry --
does the reader FOLLOW a sequence change -- is asserted where a cursor actually moves.

## The recording identity (read from the pinned inputs, not copied here)

The recording is pinned by size and sha256 in `evidence/viewmodel/animation-inputs.txt` and
verified at gate section 0/6, which prints `OK <name> pinned (<size> bytes, <sha prefix>...)`.
Today that resolves to `autorecord_2026-08-08_21-43-59.dem` (archive root `archive-pov`,
`E:/demo测试/pl_upward_f12/POV/`). The comment block above the history run in the gate states
the characterisation and the two previous sources; it deliberately does not copy the sha.

Characterisation of the 8-08 recording, from this run:

| reading | value | where it is read |
| history distinct sequences | 1 | trace `scene: vmAnim entity` lines, `sequence=` |
| history sequence range | 28..28 | same lines, min..max |
| history changes | 0 | `scene: vmAnim summary` line |
| history observations | 1645 | `scene: vmAnim summary` line |
| entity model index (recording-local) | 998 | `scene: vmAnim entity` line, `modelIndex=` |
| drawn / named model | `c_sniper_arms.mdl` | `scene: vmAnim entity` line, `model=` |
| entityModelMatchesDrawn | 1 | same line; the proof, not the characterisation |
| model's first posable sequence | 1 | model table, section 3 (`POSE_SEQUENCE`) |
| model's 61-frame clip | sequence 30 | model table, forced as an override in section 3 |

The 07-02 pin that these replace (kept here for the next reader): distinct sequences 3,
range 1..30, modelIndex 1032, sequences 1 / 29 / 30. Source: `9cfc9139`'s commit message
("on this recording the entity carries sequences 1, 29 and 30 and its model can pose sequence 0
and nothing else"), corroborated by the old trace copied at
`evidence/visual-baseline/trace-ref.txt` (mtime 2026-11-10): `sequence=1` x751, `=29` x666,
`=30` x242, `modelIndex=1032`.

## Eleven assertions, old -> new

Old line numbers are the file as of `435b4d5c`; new line numbers are this commit.

| # | assertion | old line | new line | old expected | new expected | why the number is the number |
| 1 | history distinct sequences | :589 | :659 | 3 | 1 | The 8-08 recording carries one sequence; counted from the trace, not from the summary |
| 2 | history sequence range | :592 | :660 | 1..30 | 28..28 | Same; range 28 is the recording's local entity sequence, not the model's table |
| 3 | witness ticks | :598 (derived) | :683, :687, :703/:760/:782 | 3 ticks derived from sequence changes | 3 explicit ticks inside the window, each asked for and reached | A single-sequence recording has no changes to derive ticks from; the mechanism asserted instead is the tick pipeline itself |
| 4 | witness-a model index | :634 | :715 | 1032 | 998 | Recording-local entity table slot; asserted next to `entityModelMatchesDrawn=1`, which is the proof and stays green |
| 5 | first two witnesses different sequences | :648 | :759, :762 | sequences differ | ticks differ; b played further than a | With one sequence the sequence number is the same at all ticks; the runs are distinguished by request and by how far they played |
| 6 | witness-b observations exceed a's | :654 | :769 | > a's | unchanged | Now meaningful: different ticks mean different observation counts |
| 7 | witness-b sequence changes | :656 | removed (comment :771-776) | >= 1 | -- | changes=0 is the recording's characterisation; the change claim moved to gate5 |
| 8 | last two witnesses different sequences | :661 | :781, :784 | sequences differ | ticks differ; c played further than b | As row 5 |
| 9 | witness-c sequence changes | :665 | removed (same comment) | >= 2 | -- | As row 7 |
| 10 | witness-c sequence range over the run | :671 | :796 | 1..30 | 28..28 | The run reads the recording; one sequence means the range is a point |
| 11 | natural run carries it | :724 | :849, :855, :856 | = model's first posable sequence (1) | split: model table = 1 (:849, unchanged); recording's carried sequence = 28 (:855); natural run carries the recording's sequence (:856) | Two different objects were being compared: the model's sequence table and the recording's network state. They now get separate assertions |

### Why 998 is 998 (row 4)

`modelIndex` is the index the demo's entity network state carries -- the entity table slot this
recording puts the entity in. It is NOT an index into `c_sniper_arms.mdl`'s sequence table
(that table is read in section 3: 43 sequences, first posable sequence 1). The assertion
sitting next to it, `entityModelMatchesDrawn = 1`, says the pass drew the model this index
names; the modelIndex assertion says the index is this recording's. On the lost 07-02 recording
the same claim read 1032.

### The witness mechanism (rows 3, 5, 8)

Old: `TICKS` were derived from the first scene update of each distinct sequence in the history
trace -- a real mechanism, but one that only works when the recording changes sequence. New:

- the window is read from the history trace itself (`trace_first_tick` / `trace_last_tick`),
  and the span must exceed three ticks (`:683`);
- the three ticks are explicit points inside that window (start, one third, two thirds;
  `:687`), and `witness_ticks_ok` refuses a tick outside the window or a repeated tick;
- each witness run is checked against the product's own `capture: armed` line: it was asked
  for its tick (`requested=` equals T0/T1/T2) and it reached it (`frame_tick >= requested`);
- the runs are told apart by request and by how far they played
  (`trace_last_tick(b) > trace_last_tick(a)`, same for c), and by the pairwise byte-different
  witness frames that were already asserted;
- duplicate tick and out-of-window tick are exercised as controls (`:691`, `:693`).

### Controls that show the new pins can fail

| control | line | what it does |
| a forged second sequence in a copy of the history trace | :671, :673 | changes one `sequence=28` to `sequence=29` in `.scratch/viewmodel-companion-controls/history-second-sequence.trace`; the shared reader must return distinct=2 and range 28..29, so rows 1-2 would redden on a genuinely different recording |
| the tick selection rejects a tick outside the window | :691 | expects `outside-window` from `witness_ticks_ok` |
| the tick selection rejects a repeated tick | :693 | expects `not-increasing` |

All controls are offscreen (copies under gitignored `.scratch/`, trace files only); no product
run, no source mutation, no `--mutation`.

## Where the removed claim went (bidirectional handoff)

- Companion gate, above the history run and in the witness block: the claim "the reader
  FOLLOWS a sequence change" is not askable on this recording; it is asserted in
  `viewmodel-animation-check.sh:574-577` (`pov ViewModel sequence changes >= 2`,
  `pov distinct sequences >= 2`) on the matcha recording, which switches 627 times over 41
  sequences.
- Gate5, N01 characterisation block (`viewmodel-animation-check.sh:957-960`): a reverse
  pointer says the companion gate pins the same recording's proof and does not assert a
  switch either, so the block below in gate5 is the one that carries the change claim.

## When to move these numbers again

- If the `$INPUTS` pin changes to another recording, re-characterise (distinct, range,
  changes, modelIndex) from a history run and re-pin rows 1, 2, 4, 10, 11; re-check the
  window in row 3.
- If the entity model changes, rows 11 (model table) and the section 3 override (`POSE_SEQUENCE`,
  the 61-frame sequence 30) are the ones to re-read.
- If the same recording on the same model starts reporting `changes > 0`, the characterisation
  is no longer true -- stop and look, do not re-pin quietly.

## Evidence

- Re-pin run: `companion-repin/gate.log` next to the matrix logs (`/d/_shared/EXEC-2026-11-15/INST1/E1-round5/companion-repin/gate.log`), traces under `companion-repin/trace/`.
- Previous red run: `evidence/fast/e1-round5-matrix/20261116T121759Z/matrix/companion.log` (113 OK / 11 FAIL).

## Host throughput (observation, not changed here)

The pre-existing guard `history observations >= 1000` (`:665`) reads how many frames the
history run drew inside the fixed 2000-tick window, so it measures render throughput as much as
the trace channel: 1645 in the 2026-11-16 matrix run, 669 on a busy host (22:28 run), 1127 on a
quiet host (22:50 run). It is not part of this re-pin and was not touched -- raising it to a
throughput-independent claim (e.g. "the run advanced to its tick") is a separate decision.

## Status

`VIEWMODEL-COMPANION=PASS`, **136 OK / 0 FAIL**, exit 0 -- full gate run on a quiet host
(2026-11-16 22:50-23:17), log `/d/_shared/EXEC-2026-11-15/INST1/E1-round5/companion-repin/gate.log`.
The only red in the runs before it was the throughput guard above (`gate-run-obs669.log`,
1 OK-set short of pass: 135 OK / 1 FAIL).