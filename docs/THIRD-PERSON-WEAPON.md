# Third-person weapon attach (--player-weapon-attach)

A player's active weapon is drawn in the hand that holds it, at the pose the
skeleton is already in. The world pass keeps drawing every weapon entity the demo
recorded; the player's *own* active weapon entity is skipped in the world pass and
drawn once, as an extra instance, from the hold bone. The flag is off by default
and, when absent, the frame is byte-identical to the pre-feature path -- the gate
below asserts exactly that.

## The chain

1. `m_hActiveWeapon` (handle, low 11 bits = entity index) ->
   `findPlayerWeaponInput()` in `native/src/entity_model.cpp`. Every failure is
   named: `NoPlayerEntity`, `PlayerEntityAbsent`, `NoActiveWeaponSlot`,
   `EmptyWeaponHandle`, `WeaponPastSnapshot`, `WeaponAbsent`,
   `NoWorldModelSlot`, `WorldModelIndexZero`, `WorldModelIndexOutOfRange`,
   `WorldModelNotDeclared`. A refusal is drawn as nothing and traced, never
   guessed at.
2. Weapon entity -> `m_iWorldModelIndex` -> precached world-model path.
3. Hold point on the player model: an attachment named `weapon_bone` when the
   model has one (its local 3x4 matrix is used as is), otherwise the *bone* named
   `weapon_bone` with an identity local matrix. `demo.mdl` has the bone only,
   `scout.mdl` has the attachment.
4. The instance transform is `skinMatrices[holdBone] * holdLocal`, i.e. the
   hold bone's current model-space matrix. For a bone with no animation channel
   the matrix is the composed bind frame `p2b[parent] * inverse(p2b[bone])` off
   the parent's frame: `poseToBone` maps model -> bone, so the bind pose is its
   inverse. Using identity there put every such weapon at the model origin --
   which is the feet, and that was the visible bug this feature first had.

## Flags and trace

`--player-weapon-attach on|off` (default off; any other value prints
`invalid --player-weapon-attach; it takes on|off.` and exits 13).

Per placement, once per signature:
`weaponAttach: class=... ent=... source=bone|attachment bone=N body=<mdl>
weaponEnt=N weaponClass=... weapon=<mdl> worldIndex=N family=N
boneAt=x,y,z holdAt=x,y,z` -- model space, Y up, model units.

Per frame, when the flag is on:
`scene: weaponAttach on=1 held=.. drawn=.. poseMissing=.. overBudget=..
refused=.. skippedStandalone=.. noHoldPoint=.. noWorldMesh=.. firstRefusal=..`.

In `--world-entity-id` frames the attached instance carries an id of its own
(`entity: id=N ... attach=1 heldEnt=<player> source=... bone=N`), which is what
makes its pixels measurable.

## Gate: third-person-weapon-check.sh

Run it under the shared host lock, one capture at a time:

```
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t83-weapon \
  bash -c 'cd /d/TF2_Native_Worktrees/t83-weapon && bash third-person-weapon-check.sh'
```

`--dry-run` prints the four targets x five frames plan and checks the inputs
without launching anything.

Per class (demo, scout, soldier, medic) it takes five captures from the
class-pixels camera and tick: `off`, `attached`, `attached-2` (same request, second
process), `identity`, and `identity-attached` (45 degrees to the side). It then
asserts:

1. `off` and `identity` are byte-identical to
   `evidence/t62-matrix/class-pixels/<class>/{animated,identity}.bmp` (sha
   equality, mode 1). A missing baseline file is a FATAL exit 2, never a skip.
2. `attached` twice is byte-identical, and `attached` differs from `off`.
3. The off run prints no weapon-attach line at all: the feature is inert when
   the flag is absent.
4. The attach instance's pixels are a weapon at this body. The pixel checker
   (`t83-weapon-pixels.py`) reads that id's mask: at least 25 px, touching the
   body (<= 30 px dilated distance), centre above the body's bottom quarter,
   height <= 90% of the body. Its ledger line must also name a model-space hold
   point at hand height (y in [20,75], |x| <= 60, |z| <= 45, >= 15 units from the
   model origin, which is the feet).
5. The weapon entity is drawn only as the attached instance: no second
   `entity: id=` line with the same `ent=` and no `attach=1`.
6. A class the recording gives no weapon at that tick is graded as a refusal
   row instead: attached equals off, no placement line, and the refusal is in
   the trace.

Negative controls, run by the gate itself: N1 the off identity capture fed to
the pixel checker must fail; N2 the attach ledger id rewritten to the body id
must fail; N3 `--player-weapon-attach bogus` must exit 13.

`synth_checker_test.py` proves the checker can fail without the product: eight
hand-built frames (good hand, no ledger row, body id, 300 px away, at the feet,
hold point at the origin, and two multi-player frames where the measured body's
hold point is bad while another player's is good and vice versa) all reach the
expected verdict.

## Branch measurement (to be re-confirmed by the frozen integrated-tree run)

Demo, entity 5, tick 64161, identity capture at 45 degrees: body id 93,
1733 px; attached weapon id 94, 204 px, bbox 659,341-690,354, 1 px from the body;
`holdAt=-11.5,55.1,8.2` on bone 21 (= `weapon_bone`), `driftFromBone=0.00`;
`scene: weaponAttach on=1 held=2 drawn=2 poseMissing=0 refused=0
skippedStandalone=2`. Same frame, scout entity 15: attachment `weapon_bone`
bone 23, `c_scattergun.mdl`, `boneAt=-3.1,49.9,4.1`. Medic entity 13 at tick
64000 has an empty `m_hActiveWeapon`, so it is graded as a refusal row.

## Not claimed

- Not weapon animation: the weapon is a rigid grip pose. Its own sequences
  (idle, reload, deploy) do not run.
- Only the `weapon_bone` hold point; `effect_hand_*`, `prop_bone*` and custom
  attachments are out of scope.
- Only the *active* weapon's world model; thrown projectiles and dropped weapons
  keep their world-pass draw.
- No collision fix-up: the rigid weapon can clip into geometry or a body.
- The first-person view-model path and ragdolls are untouched.
