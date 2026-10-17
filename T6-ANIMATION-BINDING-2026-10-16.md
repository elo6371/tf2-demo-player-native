# T6 -- ViewModel Animation Binding

- Task: T6 of NEXT-WAVE-TASKS-2026-10-16.md.
- Branch / worktree: `task/viewmodel-animation-binding` / `D:\TF2_Native_Worktrees\viewmodel-animation-binding`
- Base commit: `4360403` (the T5 planning baseline)
- Final commit: `91280ba` (product `cedbaf9`, gate pins `91280ba`)
- Product change: `native/src/animation_binding.cpp` only (one function,
  `AnimationBindingCache::resolve`). Instrument changes:
  `native/tools/viewmodel_animation_probe.cpp` (self-test fixtures) and
  `viewmodel-animation-check.sh` (pins + second mutation patch).

## 1. What was measured before the change

Dumped from the shipped files with an independent reader (`.scratch/dump-mdl.py`,
structure only; extracted bytes hashed in `modeldumps/extracted-bytes.sha256`):

| file | bones | own animations | own sequences | sequences with an animdesc | declares |
|---|---|---|---|---|---|
| c_sniper_arms.mdl | 58 | 1 (`@ref`, 1 frame) | 43 | **1** (`ref`) | c_sniper_animations.mdl |
| c_sniper_animations.mdl | 58 | 88 | 76 | **76** | -- |
| c_soldier_arms.mdl | 67 | 1 | 54 | **1** | c_soldier_animations.mdl |
| c_soldier_animations.mdl | 67 | 109 | 97 | **97** | -- |
| scout.mdl | 78 | 2 | 306 | **2** | scout_user_animations / scout_animations / scout_workshop_animations |

So the arms/player models carry a STUB animation table and declare the real one
through their include list. The resolver's route 1 tested `animations.empty()`,
which a stub passes: the declared companion was never read, and every sequence
the POV recording's entity actually plays (m_nSequence 1..30) refused with
`sequence has no animation binding`. That is the state the T1 manual round
recorded, and why it could only CONSTRUCT an acceptance from the one stub pose.

The two tables are NOT index-compatible: the base table carries an extra
sequence at index 0, so base sequence 30 and companion sequence 30 name
different sequences. The name is the only deliberate link; the mapping in the
fix (`modeldumps/by-name-binding.txt`, the whole table) is by name and never by
index. For the sniper, 38 of 43 sequences bind by name; the five that do not
are `ref` (the stub's own label) and the four `rifolver_*` overlays, and they
keep the decoder's named refusal.

## 2. The fix (smallest mapping the evidence supports)

`AnimationBindingCache::resolve` now asks the declared companion (the first
`*_animations.mdl` include, else the conventional `<stem>_animations.mdl`) in
the case route 1 used to answer for a stub, and adopts it only when it binds
STRICTLY MORE of the model's own sequences by name. On adoption:

- the sequence table stays the model's own (the entity's `m_nSequence` indexes
  the model it named -- the index space does not move);
- each sequence is resolved by name against the companion; an unmatched
  sequence keeps `animDesc = -1` and refuses with the decoder's existing words;
- the companion's bones are mapped onto the render skeleton by name+parent as
  before, and a companion that fails that mapping is NOT adopted -- the model's
  own table is retried first, so a working per-sequence refusal is never traded
  for a wholesale one.

A model whose own table is real keeps it byte for byte (the condition is a
strict improvement), and the existing `_animations.mdl` route for models with an
EMPTY table is unchanged.

## 3. Readings (real input, POV recording, capture tick 2000)

Entity 1077 (`c_sniper_arms`, modelIndex 1032), weapon entity 1008.

| run | vmAnim | order |
|---|---|---|
| default (opt-in off) | status=`ok` poseAvailable=1 applied=0 frame=`60/61`, sequence 30 | vmAnimApplied=0 skinning=0 |
| `--viewmodel-apply-animation` | status=`ok` applied=1 frame=`60/61` | skinning=1 boneTableCoverage=`58/58` |

Before the fix the same input read `status=missing poseAvailable=0 frame=0/0
reason=sequence has no animation binding` (T1 evidence, immutable).

Pixels: the applied pose vs the bind pose at the same tick differs by 61305
bytes (2.3740%, max_delta 229), and the difference is confined -- all five
T1 world-control regions read 0 differing bytes (`frames/motion-diff.txt`).
Stub sequence 0 (base label `ref`) still refuses by name (in range, no pose);
the one-frame `r_handposes` pose renders byte-identical to the bind pose, which
is recorded as a reading about that pose, not about the flag (an earlier gate
revision asserted movement on it and went red).

## 4. Instrument

- Probe self-test: 96 checks, 0 failures. New/moved fixtures: the accepting
  sequence is `r_handposes` (bound by name), the no-pose case is the stub label,
  the adopted shape is pinned as data (53 of 54 sequences bound, 52 multi-frame,
  the longest 89 frames), the adoption itself (`separateAnimSource`, `animPath`),
  and the first multi-frame sequence's reachability.
- Gate (`viewmodel-animation-check.sh`) moved with the change, pins and prose
  together: section 3 now pins animated=38 of 43 and firstAnimated=1; section
  5(b) is an acceptance (opt-in, frame 51 for sequence 29); section 6 is
  re-shaped around three separated cases (natural history all accepted; the stub
  kept as its named refusal; the real motion, sequence 30/61 frames, applied and
  moving the picture).
- `--mutation` now makes TWO coherent source edits: the existing model-proof
  neuter (viewmodel.cpp) and a second patch that neuters the adoption
  (animation_binding.cpp, anchored exactly once). The mutation verdict requires
  four moved readings (refusal status, refusal reason, refusal mismatch count,
  history ok) and four held controls (poseAvailable, companion match, matcha
  entity, index space) with `fail=0`. Mutation-mode pins were measured and
  written for the adoption-affected assertions (self-test 76/18/94, modelseq
  1/0, companion missing/0/0, history all-refused, stub accepted, motion diff 0).

## 5. Gates on the final commit

- `bash viewmodel-animation-check.sh` -> `VIEWMODEL-ANIMATION=PASS` rc=0
  (measured on `91280ba`; the probe's raw-words change in `bd99f44` does not add
  or remove checks, and the follow-up mutation run re-ran the whole battery).
- `bash viewmodel-animation-check.sh --mutation` -> `MUTATION-CAUGHT=PASS`
  (two-patch mutation; four readings moved, four controls held, `fail=0`).
  The first mutation run of this round caught the mutation 4/4 + 4/4 but went red
  on its own pre-T6 pins; that output is kept as `logs/t6-gate-mut.log` and its
  failure list is exactly what the pin commit `91280ba` was written from.
- `bash viewmodel-companion-check.sh` -> `VIEWMODEL-COMPANION=PASS` rc=0 (its six
  pre-T6 pins moved in `bd99f44`, measured, not guessed).
- `bash verify-render.sh` -> **NOT RERUN as a tier** on `bd99f44`. The tier run
  of 2026-10-17 00:46 read 8/9 with `viewmodel-companion` red on its pre-T6 pins
  (kept as `logs/t6-render.log`); those pins were then moved, and the gate passes
  standalone (above). The tier rerun was stopped at the owner's instruction not
  to run the full chain, so `VERIFY-RENDER=PASS` on this commit is NOT VERIFIED
  as a tier verdict -- what is verified is every gate in it that this change can
  touch, run individually.
- `bash verify-render.sh --mutation` -> **NOT RUN** (same instruction).

Two killed-run incidents, recorded because both left the tree dirty and were
cleaned before any verdict was taken: (1) a killed `--mutation` tier left
`animation_binding.cpp` patched and two stale lock directories; (2) a second
killed mutation left BOTH `animation_binding.cpp` and `viewmodel.cpp` patched,
and -- measured -- its mutated binaries survived the source restore (a standalone
gate read `drawCalls=2` where the committed build reads 1). Sources were
restored from git, locks removed, and every affected target rebuilt from the
commit before any pin was taken.

## 6. Evidence

`evidence/t6-animation-binding/`: `modeldumps/` (the structural dumps and the
full by-name binding table), `frames/` (bind and motion PNGs + the diff),
`logs/` (self-test, gate runs, product runs), `traces/` (extracts + raw sha256),
`SHA256SUMS`. Raw extracted model binaries stay in .scratch (Valve assets; what
is committed is the structure and the hashes).

## 7. Unverified / limitations

- The world entity path shares the resolver and therefore also adopts companions
  for player models now; the affected gates pass, but no before/after capture of
  a PLAYER entity posing in the world was taken: `NOT VERIFIED` for world-space
  player animation.
- `m_flCycle` is still absent from the wire (class table, re-pinned); the cycle
  branch stays covered by the probe's `v_*` witness, not by real input.
- The five sniper sequences without a companion name-match (`ref`,
  `rifolver_*`) remain refused by name; nothing maps them by index.
