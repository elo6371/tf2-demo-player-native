# P2-2 DESIGN -- server-data-driven player animation (crouch / crouch-walk / flinch)

Scope: T84, worktree D:/TF2_Native_Worktrees/t84-animdrive, branch task/t84-animdrive.
Read-only inputs: the demo recording (its own entity state stream) and the class
sequence tables. Nothing in this feature reads the client CVars, the user config
or any local file that the legacy path does not already read.

## 1. Why this exists

The T61 clock (`AnimationDriveState{Idle=0, Moving=1}` in
`native/include/animation_clock.h`) selects ONE of two sequences per player model:
the idle pick and the moving pick from
`choosePlayerAnimationSequence` (`native/src/main.cpp`). T62 measured that the
data for two more families is present and unused: every class carries crouch
clips, and four of the nine carry `a_flinch01`. The product never asked for them,
so a crouching player kept playing the standing idle.

This change adds a state layer on top of the T61 clock. It does not replace the
clock: the clock still owns the phase (cycle) of the locomotion clips, so a
moving player keeps moving and never freezes.

## 2. Signals -- what the server actually sends

Measured in T62's field census (`D:/_shared/t62-p2-server-anim/report.tsv`) and
re-verified for this task against the recording:

| signal | SendTable owner | type | used for |
|---|---|---|---|
| `m_fFlags` bit 1 (`0x2`) | `DT_BasePlayer` | Int | crouch (strong) |
| `m_bDucked` | `DT_Local` | Int | crouch (fallback) |
| `m_bDucking` | `DT_Local` | Int | crouch (fallback) |
| `m_flDucktime` | `DT_Local` | Float | crouch (fallback, >0 means ducking) |
| `m_iHealth` | `DT_BasePlayer` | Int | flinch (decrease) |
| `m_lifeState` | `DT_BasePlayer` | Int | liveness gate (0 = alive) |

Two facts shaped the code and are not obvious:

1. **`m_fFlags` bit 1 is not always set while ducking.** On the recorded demo the
   soldier at tick 14765 carries `m_bDucking=1` with `m_fFlags=131329` -- bit 1
   clear. A crouch detector that only reads the flag would miss it. The code
   therefore ORs the flag with the three `DT_Local` duck fields.
2. **The animation props are NOT on the CTFPlayer wire.** `m_nSequence`,
   `m_flCycle`, `m_flPlaybackRate` and `m_flPoseParameter` are stripped from the
   flattened `CTFPlayer` table (885 leaves, identical in both audited demos).
   The server never tells the client which clip to play; that is why the client
   picks one from data. This change does not pretend otherwise: it picks, and it
   records which pick it made.

## 3. The state machine

Per player model instance, per scene update, after the T61 clock has stepped:

1. **flinch** -- a one-shot. Started when `m_iHealth` read on this update is lower
   than the value read on an earlier tick for the same entity, the body is alive
   (`m_lifeState == 0`), and the class table has a flinch clip. The window is
   `flinchTicks` demo ticks (the clip's own frame count, 13 for `a_flinch01`);
   the cycle is `elapsed / flinchTicks` so the clip plays once and ends. Health is
   compared **per demo tick**, not per rendered frame: a capture run observes every
   earlier tick in order (the playback loop advances and clamps to
   `--capture-tick`), so the drop is seen exactly once.
2. **crouch-walk** -- crouch signal asserted AND the clock says moving. The
   state names the SERVER state (a crouched body that is moving is crouch-walking)
   and it is entered for every class, not only the three that own a crouch
   locomotion clip. When the class has one (`crouch_PRIMARY/SECONDARY/MELEE`;
   pyro, heavy, medic) that clip plays and the clock still drives the stride. When
   it does not, the crouch POSE plays and the trace line says
   `reason=no-crouch-locomotion`, so `label=` and `reason=` always disclose the
   substituted clip and the state name is never a claim about a clip.
3. **crouch** -- crouch signal asserted AND the clock says the body is standing
   still. Prefers the class crouch pose.
4. **fallback** -- the untouched T61 idle/moving pick.

Precedence is flinch > crouch-walk > crouch > fallback. Every branch writes the
same two fields the legacy path wrote (`request.sequence`, `request.cycle`) and
leaves `request.hasPlaybackRate` alone, so the downstream render path is
unchanged.

## 4. Sequence selection is reused, not copied

All label predicates and selectors live in
`native/include/animation_sequence_match.h`, next to `chooseSequenceByNeedles`
and the T61 `choosePlayerAnimationSequence` is untouched:

* `sequenceLabelIsCrouchLocomotion` -- primary token `crouch`, which excludes
  `AttackCrouch_*` (its primary token is `attackcrouch`), `Melee_Crouch_*` and
  `armslayer_*`;
* `sequenceLabelIsCrouchPose` / `sequenceLabelIsCrouchPoseCandidate`
  (rejects one-shots, attack/swing/fire/stun families);
* `sequenceLabelIsFlinch`;
* `chooseCrouchLocomotionSequence`, `chooseCrouchPoseSequence`,
  `chooseFlinchSequence`.

Needles are still matched through the same `chooseSequenceByNeedles` style
ranking, so the "leading token" rule that caused the T62 Pyro reload bug is
applied once, in one place.

## 5. Determinism and byte-identity

* `--animation-drive` absent, or `--animation-drive legacy`: `animationDriveStates`
  is false, every new block is skipped, and the trace is exactly today's
  (`anim: pick`, `anim: ent=`, `animsum:`). The gate asserts this by md5 equality
  of the captured frame and by filter of the `animdsum:` line.
* `states` adds one `anim: state` line per driven instance (capped) and one
  `animdsum:` summary line. No existing line changed.
* The feature reads only server state that is already decoded into
  `currentEntityStates`; it adds no clock, no random and no wall-time input, so
  two identical runs are byte-identical.

## 6. Non-claims

* **Attack is not verified.** T84's census found no CTEPlayerAnimEvent value that
  maps to attack/flinch: `player_death` and `player_hurt` correlate with no event
  id (exactDeath<=2 for every value, useridEqEntity=0), and the tree carries no
  enum. Guessing an enum would be a fabricated mapping, so attack stays on the
  idle/moving pick and this document does not claim it works.
* **`m_nSequence` is still not on the wire.** This is a client-side pick from
  server-derived state, not a replay of the server's own clip choice.
* **No first-person viewmodel change.** The viewmodel path is untouched.

## 7. Where the code lives

* `native/include/animation_sequence_match.h` -- label predicates and selectors.
* `native/src/main.cpp`:
  * `--animation-drive` parsing + rejection (exit 13);
  * `PlayerAnimationRuntime` (health memory, one-shot window) and
    `PlayerSequenceChoice` (resolved crouch/flinch sequences);
  * `PlayerDriveSignals` + `readPlayerDriveSignals` next to `readPlaybackState`;
  * the state layer inside the per-instance drive block, immediately before
    `request.sequence/cycle` are written;
  * counters and the `animdsum:` line.
* `animation-drive-states-check.sh` -- the gate (see REPORT.md for its readings).
