# T84 P2-2 / P2-3 — player animation driven by server data (crouch, crouch-walk, flinch)

Worktree: D:/TF2_Native_Worktrees/t84-animdrive, branch task/t84-animdrive (HEAD 14c0f1c at the time of the change).
Gate: animation-drive-states-check.sh at the repository root. Evidence: this directory.

## 1. What changed

Three states now layer on top of the T61 clock, which only had idle and moving:

| state | trigger | pose |
| --- | --- | --- |
| flinch | m_iHealth decreased between two observed scene ticks on a living body | \`a_flinch01\` family, one shot sized from the clip's frame count |
| crouchwalk | crouched AND speed >= the moving threshold AND the class has a crouch locomotion family | \`crouch_PRIMARY/SECONDARY/MELEE\` (pyro, heavy, medic) |
| crouch | crouched, no crouch locomotion family for this class (or not moving) | the crouch pose family, reason \`no-crouch-locomotion\` when the walk cycle is what is missing |
| fallback | none of the above | the pre-T84 idle/moving pick |

The switch is \`--animation-drive states|legacy\`. Absent or \`legacy\` is exactly the pre-T84 path: the state block is not entered, \`animdsum\` is not printed, and the frame is byte-identical to a run without the switch. \`states\` adds:

- \`anim: staterefs model=... crouchPoseSeq=... crouchMoveSeq=... flinchSeq=... flinchTicks=...\` — one line per model, so each layer's clip can be named without a body being on screen.
- \`anim: state ent=... tick=... state=... reason=... crouchSig=... flags=... ducked=... ducking=... ducktime=... health=... alive=... seq=... label=... cycle=... needle=...\` — the signal values next to the decision, one line per drawn body (capped at 24 per frame).
- \`animdsum: tick=... crouch=... crouchwalk=... flinch=... flinchStarts=... fallback=...\` — instance counters for the frame, so the gate can read the whole playback from the exit frame.

Sequence selection is not a second matcher: the needles live in \`native/include/animation_sequence_match.h\`, and the crouch pose/moving and flinch picks use the same leading-token rule as \`choosePlayerAnimationSequence\` (\`crouch\` token, \`flinch\` family, AttackCrouch_* excluded by its \`attackcrouch\` primary token).

## 2. Why these signals, and not the ones that look nicer

Measured on the gate demo with the P2-1 probe (entity_model_probe, --props-at over each body):

- **Crouch cannot be flags alone.** Entity 2 (red soldier) sits at m_fFlags=131329 (bit 0 FL_ONGROUND only) with m_bDucking=1 and m_flDucktime=940.0 at tick 14765 — bit 1 FL_DUCKING is CLEAR mid-duck. The trigger is therefore \`(m_fFlags & 0x2) || m_bDucked || m_bDucking || m_flDucktime > 0\`, and the trace prints all four signals so a reader can see which one fired.
- **Flinch cannot be the animation event.** The CTEPlayerAnimEvent census over these recordings found no value that maps to hurt/die/attack: 109 player_death events against 545 occurrences of the only die-sized candidate, and the event's player field never equals the game event userid. Reading an enum out of that would be a guess with a number on it. m_iHealth decreasing is directly observable and unambiguous: entity 6 (blue medic) is 150 at ticks 16000..16770 and 108 at 16780 and later.
- **The wire does not carry the clip.** m_nSequence, m_flCycle and m_flPlaybackRate are not on the CTFPlayer sendtable in either recording (P2-1 census), so the sequence is a client-side pick driven by server state — which is exactly what this change is.

## 3. Ticks used by the gate (all measured, not guessed)

| role | tick | reading |
| --- | --- | --- |
| FLINCH_TICK | 16780 | entity 6 m_iHealth 150 -> 108 (the drop happens inside 16771..16780) |
| CROUCH_TICK | 14765 | entity 2 alive after respawn, m_bDucking=1, m_flDucktime=940.0, flags 131329 |
| SAME_STATE_TICK_B | 14800 | same duck window (m_bDucking=1), so the same state twice |
| COVER_DEMO / COVER_TICK | run-time: `entity_history_last_tick - 20` of the first candidate recording whose playback contains every state | the cumulative counters are read from here |
| EARLY_TICK | 12000 | before the first crouch; used as a real-run negative control |

Ticks below ~11355 are status=before-window on this recording (only the tail of the match is retained), which is why the window starts at 12000.

The gate takes the cover recording from `T84_COVER_DEMO` / `T84_COVER_TICK` (default: the same recording at COVER_TICK 20000). The cover tick is bounded by playback cost: the product plays the demo at wall-clock speed, so a capture at tick T costs about (T - demo_start)/66.7 s; covers therefore sit near start+12000 ticks, never at the recording's last tick (15-25 min). Candidate covers were derived from `demo_open_probe --scan` (`ticks=` = duration, `entity_history_last_tick` = end, start = end - duration):
SN snakewater 20000, D29 dustbowl 11341, BS breadspace 12942, UP upward 22931, D30 swiftwater 29737.

## 4. Readings (animation-drive-states-check.sh)

**Status: written and dry-run verified in this tree; executed by the Lead on the integration tree.** This section states what the gate reads and how each reading is classified. It does not invent numbers: every value below is written to the gate's own artifacts at run time and the gate fails if a reading is missing.

### 4.1 What is trace-level and what is pixel-level

| id | assertion | level | how it is read |
| --- | --- | --- | --- |
| A1 | legacy is byte-identical to today's default | **pixel** | md5 of default.bmp == md5 of legacy.bmp |
| A2 | legacy still runs the old drive path | trace | legacy.trace has no `animdsum:`, but has >=1 `anim: pick ` and >=1 `animsum:` |
| B1 | states changes the picture | **pixel** | states-flinch.bmp != legacy.bmp |
| B2 | same-frame replay is deterministic | **pixel** | states-flinch.bmp == states-flinch-repeat.bmp |
| B3 | same state, different tick, same body, different frame | **pixel** + trace | bmps differ and both traces name `state=crouch*` |
| C1 | each state is reached with a needle and a reason | trace | one `anim: state ... needle=... reason=...` line for crouch, crouchwalk, flinch |
| C2 | the states actually happened during playback | trace (counters) | last `animdsum:` has crouch, crouchwalk, flinch, flinchStarts, fallback all >= 1 |

So: **byte identity, determinism and "a different state produces a different picture" are pixel-level** (they compare rendered BMPs). **Coverage — that crouch, crouch-walk and flinch were actually produced, and which clip/needle/reason was used — is trace-level** (counters and per-transition lines). Nothing in this gate asserts anything about pixels *inside* the body beyond "the frame differs"; limb-level claims belong to the existing pose/pixel gates (class-pose-family-check.sh, class-pixels-check.sh).

### 4.2 Reading one state line

```
anim: state ent=<entity> tick=<resolved tick> state=<state> reason=<flags-duck|duck-fields|no-crouch-locomotion> crouchSig=<0|1> flags=<m_fFlags> ducked=<m_bDucked> ducking=<m_bDucking> ducktime=<m_flDucktime> health=<m_iHealth> alive=<0|1> speed=<units/s> seq=<sequence> label=<clip label> cycle=<0..1> needle=<matched token>
```

The line is edge-triggered per body (one line per state change), so a 12000-tick capture writes a bounded number of lines. `label=` is the clip actually applied, which is why a class without a crouch locomotion family shows `state=crouchwalk reason=no-crouch-locomotion label=PRIMARY_aimmatrix_crouch_idle` honestly instead of pretending a cycle exists.

## 5. Negative controls

Three controls run against copies of the real artifacts; each must make the checker fail (a control that passes the checker is itself a gate failure). They are the proof that the readings above are not vacuous:

1. **neg-1-flinch-renamed** — a copy of the real traces with `state=flinch` renamed to `state=fallback`, `flinch=0`, `flinchStarts=0`. Fails C1/C2: renaming the state away must be detected.
2. **neg-2-legacy-as-states** — a copy where legacy.bmp is dropped over states-flinch.bmp. Fails B1: if the driven run had not actually changed the frame, this substitution would be indistinguishable and the control would pass.
3. **neg-3-early-window** — a copy where the real early.trace (a genuine product run before any crouch/flinch) replaces the four states traces. Fails C1/C2 while keeping B*, proving the state readings depend on the states occurring, not on the file merely existing.

Controls can be skipped only with `--skip-negative-controls`; the summary line then prints `controls=skipped`, so a skipped run cannot be mistaken for a controlled one.

## 6. What is NOT claimed

- **Attack is NOT verified.** No decoded server field or event value distinguishes an attack frame; the layout of CTEPlayerAnimEvent was not guessed. The code has no attack state, and no attack clip choice is wired.
- No wire-provided m_nSequence / m_flCycle is used — the product computes the cycle; nothing here proves the server's own cycle.
- No viewmodel change: this is the third-person model only.
- Crouch-walk names the SERVER state (crouched and moving). Only pyro, heavy and medic carry a crouch locomotion clip; for the other six the crouch POSE plays and `reason=no-crouch-locomotion` plus `label=` say so. The state name is not a claim that a crouch-walk cycle exists.
- The per-state trace is edge-triggered per body (one line per state change), not a per-frame cap; the counters, not the trace, are the coverage evidence.

## 7. Residual risk

1. **Determinism is asserted, not assumed away.** A1 (legacy == default) and B2 (repeat == first) are only meaningful if the engine renders the same frame for the same demo tick twice. B2 is the control for that: if a repeat run differs, B2 fails and A1/B1 are untrustworthy; the gate reports it rather than hiding it.
2. **Cover tick is a function of playback speed.** The product advances the demo in wall-clock time (~66.7 ticks/s), so a cover at tick T costs ~(T - start)/66.7 s and must also fit TIMEOUT=420. On this recording that caps the cover near start+14000 ticks. A cover chosen further into a match would need a longer ticket, not a different assertion.
3. **Crouch-walk coverage depends on the recording, not on the code.** `state=crouchwalk` needs a body that is crouched AND moving. In the snakewater recording the only crouch observed is a stationary/again-crouched soldier (no crouch locomotion family), so C2's `crouchwalk>=1` is the assertion most likely to fail on a cover recording that lacks one. The gate therefore accepts a different cover recording via `T84_COVER_DEMO`/`T84_COVER_TICK`; picking one is a data-selection step, not a code change.
4. **Signals are instantaneous.** Flinch fires on an m_iHealth decrease between two resolved ticks; a drop that lands between two sampled ticks is still caught (any decrease counts), but two drops within one tick collapse into one flinch. Crouch uses `m_fFlags & 0x2` OR the duck fields because in the measured recording bit 1 was CLEAR while `m_bDucking=1`; a server that set neither would read as standing.
5. **No attack state exists** (see section 6). `CTEPlayerAnimEvent` values were not mapped and no clip-drop signal was added, so attack remains NOT VERIFIED rather than guessed.
6. **The gate never touches repo evidence.** It writes under `evidence/t84-animdrive/animation-drive-states` or `$T84_STATES_OUT`; the regression gates are invoked with their own OUT overrides so the t62 evidence trees stay untouched.

## 8. Reproducing the byte-identity claim (legacy == default)

The claim is "the default path is byte-identical to today's behaviour". It is proved by rendering the *same demo tick twice*, once without the switch and once with `--animation-drive legacy`, and comparing the two BMPs. Two commands, one hash per frame:

```bash
cd D:/TF2_Native_Worktrees/t84-animdrive
DEMO=D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem
TFROOT="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
mkdir -p /tmp/legacy-proof && cd /tmp/legacy-proof

TF2_NATIVE_TRACE=/tmp/legacy-proof/default.trace \
  "D:/TF2_Native_Worktrees/t84-animdrive/native/build-nmake/tf2_demo_native.exe" \
  --tf-root "$TFROOT" --audio-device 6 --demo "$DEMO" \
  --capture-tick 16780 --capture-frame /tmp/legacy-proof/default.bmp \
  --metrics-file /tmp/legacy-proof/default.csv

TF2_NATIVE_TRACE=/tmp/legacy-proof/legacy.trace \
  "D:/TF2_Native_Worktrees/t84-animdrive/native/build-nmake/tf2_demo_native.exe" \
  --tf-root "$TFROOT" --audio-device 6 --demo "$DEMO" \
  --capture-tick 16780 --capture-frame /tmp/legacy-proof/legacy.bmp \
  --metrics-file /tmp/legacy-proof/legacy.csv --animation-drive legacy

sha256sum /tmp/legacy-proof/default.bmp /tmp/legacy-proof/legacy.bmp
```

Both hashes must be equal, and this is exactly gate reading A1 (the gate compares md5 of the same two files and also requires `legacy.trace` to contain no `animdsum:`, i.e. the new counters were not entered at all). The two source facts behind the byte identity are:

- `--animation-drive` defaults to `legacy` (native/src/main.cpp, the argument parser next to `--player-animation`), so every existing command line keeps the old path with no extra flag.
- The new states block is guarded by `animationDriveStates` and returns before touching the clock, the choice or the counters when the mode is legacy; the only read it would add (`readPlayerDriveSignals`) is inside that guard.

If the two hashes differ, the claim is false and the gate fails; there is no fallback wording that still calls it identical.
