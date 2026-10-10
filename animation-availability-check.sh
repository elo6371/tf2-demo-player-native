#!/usr/bin/env bash
# animation-availability-check.sh -- per-entity skeletal animation must not be
# wired until the input it needs is shown to exist, and on a TF2 demo it does not.
#
# Why this exists
# ---------------
# The 2026-10-07 recon put "bone animation" at step 3 of the render gap list:
# compile animation_decoder.cpp into the main target, open
# uploadBoneMatrices(..., true), advance the sequence by demo tick. That plan
# assumes the demo says which animation each entity is playing. It does not, for
# the entity the plan is about.
#
# source-sdk-2013 declares the slots (src/game/server/baseanimating.cpp:245-246,
# IMPLEMENT_SERVERCLASS_ST(CBaseAnimating, DT_BaseAnimating)):
#
#   SendPropInt   ( SENDINFO(m_nSequence),     ANIMATION_SEQUENCE_BITS, SPROP_UNSIGNED ),
#   SendPropFloat ( SENDINFO(m_flPlaybackRate),ANIMATION_PLAYBACKRATE_BITS, ... ),
#
# and routes m_flCycle through a separate sub-table, DT_ServerAnimationData,
# attached with SendProxy_ClientSideAnimation and annotated in the same file
# (line 223): "Sendtable for fields we don't want to send to clientside animating
# entities" -- the sub-table holds only m_flCycle (BEGIN_SEND_TABLE_NOBASE at
# line 224) and is attached at line 260 by SendPropDataTable("serveranimdata",
# ...). The client's own source says the rest, src/game/client/
# c_baseanimating.cpp:1168: "not all entities network down their m_nSequence
# (like multiplayer game player entities)". TF2 is the game that strips them.
#
# Measured, not remembered: in a live TF2 demo's flattened send table, CTFPlayer
# has none of m_nSequence / m_flCycle / m_flPlaybackRate / m_flPoseParameter,
# while 197 other classes -- weapons, projectiles, wearables -- do. So a player's
# animation state is client-predicted and the demo does not carry it. Wiring a
# per-entity playback loop would consume a sequence that never arrives, and the
# loop would run on whatever the decoder left at its default, which is the exact
# "counter is green, the value is a constant" failure this project has already
# paid for twice.
#
# What is asserted
# ----------------
#   1. the absence, exactly -- CTFPlayer is present in the schema and carries
#      zero slots for all four animation properties. Counting the class itself
#      (tfPlayerFound=1) is what makes this a measurement instead of a typo: a
#      class name that silently stopped matching would read 0 and look like a
#      pass;
#   2. the positive control, on the same run -- the same rule reports 197 classes
#      that DO carry m_nSequence. A rule that can only ever say "no" is not
#      measuring anything;
#   3. the independent witness -- tf_demo_parser (the Rust oracle, a separate
#      implementation) flattens CTFPlayer's table from the same demo and its
#      listing must also be free of the four names, and must still contain
#      DT_BaseAnimating (so a table that failed to load entirely is a failure, not
#      a pass).
#
# What is NOT asserted
# --------------------
# This gate does not say animation is unimplementable. It says the *demo* cannot
# supply it for players, so a viewer cannot reproduce player animation by reading
# the demo, and any bone-animation work must be driven from something else
# (client-side prediction fed by usercmds and weapon state, or a real game
# connection) -- which is a different, larger piece of work than "wire it up".
# Weapons and projectiles DO carry m_nSequence, so the same wiring is applicable
# to them; that is recorded here as the positive control, not as a claim that it
# has been done.
#
# Usage: bash animation-availability-check.sh [--mutation]
#   --mutation rewrites CTFPlayer's name in the class sweep on the fly, which must
#   make clause 1 fail -- proving the "present with zero slots" reading is load
#   bearing rather than a class that simply is not there.
# Exit:  0 = every assertion held (or, with --mutation, the check went red).
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_model_probe.exe
ORACLE="D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
# A POV demo and the SourceTV match demo, so the negative is not a property of
# one recording. Both are already pinned elsewhere in the chain.
POV="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
SOURCETV="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/73.dem"
OUT="${ANIM_AVAILABILITY_OUT:-evidence/animation-availability}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
for path in "$PROBE" "$ORACLE"; do
  [ -x "$path" ] || { echo "FATAL: missing binary: $path"; exit 1; }
done
for path in "$TF" "$BAGEL" "$POV" "$SOURCETV"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

echo "=== 1/3 the player class carries no animation state (three recordings) ==="
# The premise has to hold on every recording this project treats as an input, not
# on the one demo whose numbers happened to be in front of me. Each run prints the
# player line even when all four counts are zero, so the assertion is about the
# class being present and empty, never about it being absent from the output.
check_player_absent() { # check_player_absent <name> <demo>
  local name="$1" demo="$2"
  "$PROBE" --tf-root "$TF" --demo "$demo" --anim-props > "$OUT/$name.json" 2> "$OUT/$name.txt"
  local found seq cyc rate pose
  found=$(sed -n 's/.* tfPlayerFound=\([0-9]*\) .*/\1/p' "$OUT/$name.txt" | tail -1)
  local line
  line=$(grep '^animprop-player ' "$OUT/$name.txt" | tail -1)
  seq=$(printf '%s' "$line" | sed -n 's/.* sequence=\([0-9]*\) .*/\1/p')
  cyc=$(printf '%s' "$line" | sed -n 's/.* cycle=\([0-9]*\) .*/\1/p')
  rate=$(printf '%s' "$line" | sed -n 's/.* rate=\([0-9]*\) .*/\1/p')
  # pose is the last field on the line, so it has no trailing space to anchor on.
  # The first version of this used the same ' pose=\([0-9]*\) .*' pattern as the
  # others and extracted nothing, which the gate reported as a FAIL rather than a
  # skip -- the right outcome, and the reason the four counts are asserted
  # individually instead of as one blob.
  pose=$(printf '%s' "$line" | sed -n 's/.* pose=\([0-9]*\)$/\1/p')
  echo "$name: player line -> '$line'"
  assert_eq "$name CTFPlayer present in schema (found)" "$found" "1"
  assert_eq "$name m_nSequence slots" "$seq" "0"
  assert_eq "$name m_flCycle slots" "$cyc" "0"
  assert_eq "$name m_flPlaybackRate slots" "$rate" "0"
  assert_eq "$name m_flPoseParameter slots" "$pose" "0"
}

if [ "$MUTATION" -eq 1 ]; then
  # --mutation is the in-script proof that this gate can go red. It does NOT
  # rebuild: mutate.sh m13 is the rebuilt, source-level proof. What this mode
  # checks is narrower and cheap -- that the two readings the gate rests on are
  # extracted from the output rather than assumed. It rewrites the probe's own
  # output lines and requires the gate's assertions to notice, which is what
  # catches an extraction regex that silently returns nothing (exactly the bug
  # the ' pose=' field had on the first run: it extracted <none> and the gate
  # correctly refused rather than passing).
  #
  # The mutation writes to its own directory, NOT to $OUT. The first version
  # rewrote "$OUT/bagel.txt" in place, and since $OUT defaults to the committed
  # evidence directory, the mutated line -- 'class=CTFPlayer_MUTATED' -- was
  # committed as if it were a reading. That is the documented 'the mutation forgot
  # to roll back and polluted the artifact' failure, and the fix is structural:
  # mutation output cannot land where the clean evidence lives.
  MUTDIR="$OUT/mutation"
  rm -rf "$MUTDIR"
  mkdir -p "$MUTDIR"
  echo "mutation: rewriting the captured probe output so the readings must move"
  "$PROBE" --tf-root "$TF" --demo "$BAGEL" --anim-props > "$MUTDIR/bagel.json" 2> "$MUTDIR/bagel.txt"
  # (a) The class is not found -> clause 1 must fail. If tfPlayerFound is hard-coded
  # or the regex does not read it, the rewrite is invisible and the gate says so.
  sed -i 's/ tfPlayerFound=1 / tfPlayerFound=0 /' "$MUTDIR/bagel.txt"
  found=$(sed -n 's/.* tfPlayerFound=\([0-9]*\) .*/\1/p' "$MUTDIR/bagel.txt" | tail -1)
  if [ "${found:-1}" = "0" ]; then
    echo "  OK   rewrote tfPlayerFound to 0 and the gate's extractor saw it (clause 1 would fail)"
  else
    echo "  FAIL the tfPlayerFound rewrite did not reach the gate's extractor"; fail=1
  fi
  # (b) The positive control is removed -> clause 2 must fail, which is what stops
  # "the rule always says no" from reading as a pass.
  sed -i 's/ classesWithSequence=197 / classesWithSequence=0 /' "$MUTDIR/bagel.txt"
  ctrl=$(sed -n 's/.* classesWithSequence=\([0-9]*\) .*/\1/p' "$MUTDIR/bagel.txt" | tail -1)
  if [ "${ctrl:-1}" = "0" ]; then
    echo "  OK   rewrote classesWithSequence to 0 and the gate's extractor saw it (clause 2 would fail)"
  else
    echo "  FAIL the classesWithSequence rewrite did not reach the gate's extractor"; fail=1
  fi
  # (c) The pose field extraction is the one that already failed once. It must come
  # back as a number, not as empty, when the line is well formed.
  pline=$(grep '^animprop-player ' "$MUTDIR/bagel.txt" | tail -1)
  posev=$(printf '%s' "$pline" | sed -n 's/.* pose=\([0-9]*\)$/\1/p')
  if [ -n "$posev" ]; then
    echo "  OK   the pose field extracts as '$posev' (the regex that failed on the first run works)"
  else
    echo "  FAIL the pose field extracts as empty from '$pline'"; fail=1
  fi
  if [ "$fail" -eq 0 ]; then
    echo "MUTATION-CAUGHT=PASS (every rewritten reading reached the gate's own extractor)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (a reading was assumed rather than extracted)"
  exit 1
fi

check_player_absent bagel "$BAGEL"
check_player_absent pov "$POV"
check_player_absent sourcetv "$SOURCETV"

echo
echo "=== 2/3 the same rule reports the classes that DO carry it (positive control) ==="
# On the same bagel run, the rule must say yes somewhere. 197 classes carry
# m_nSequence; the exact number is pinned so a regression that quietly stopped
# flattening includes would move it. classesWithRate is pinned to the same figure
# as classesWithSequence because they arrive together in DT_BaseAnimating; a split
# would mean the include chain broke.
CTRL_SEQ=$(sed -n 's/.* classesWithSequence=\([0-9]*\) .*/\1/p' "$OUT/bagel.txt" | tail -1)
CTRL_CYC=$(sed -n 's/.* classesWithCycle=\([0-9]*\) .*/\1/p' "$OUT/bagel.txt" | tail -1)
CTRL_RATE=$(sed -n 's/.* classesWithRate=\([0-9]*\) .*/\1/p' "$OUT/bagel.txt" | tail -1)
echo "bagel control: classesWithSequence=$CTRL_SEQ classesWithCycle=$CTRL_CYC classesWithRate=$CTRL_RATE"
if [ "${CTRL_SEQ:-0}" -gt 0 ]; then
  echo "  OK   the rule can report presence, so its 'no' for the player is a measurement"
else
  echo "  FAIL the rule reported no class with m_nSequence; it cannot measure the absence"; fail=1
fi
assert_eq "classesWithSequence" "$CTRL_SEQ" "197"
assert_eq "classesWithRate" "$CTRL_RATE" "197"
# m_flCycle rides DT_ServerAnimationData, a sub-table, and two classes carry the
# other two without it -- the difference is itself a reading, so it is pinned.
assert_eq "classesWithCycle" "$CTRL_CYC" "195"
# A weapon must be among them: that is the case the wiring IS applicable to.
if grep -qE '^animprop class=CTFWeaponBase .* sequence=1 ' "$OUT/bagel.txt"; then
  echo "  OK   CTFWeaponBase carries m_nSequence (1 slot), so the wiring has a target"
else
  echo "  FAIL CTFWeaponBase did not report a sequence slot"; fail=1
fi

echo
echo "=== 3/3 the independent oracle agrees CTFPlayer's table is free of them ==="
# tf_demo_parser is a separate implementation of the same wire format. If our
# flattener had a bug that dropped these properties, the oracle would still print
# them. Its table must contain DT_BaseAnimating (so "no sequence" is not a table
# that failed to load) and must contain none of the four names.
"$ORACLE" "$BAGEL" 3 CTFPlayer > "$OUT/oracle-ctfplayer.txt" 2>&1
BASEPROPS=$(grep -c 'DT_BaseAnimating\.' "$OUT/oracle-ctfplayer.txt")
ORACLE_ANIM=$(grep -cE '\.m_nSequence$|\.m_nSequence |\.m_flCycle |\.m_flPlaybackRate |\.m_flPoseParameter ' "$OUT/oracle-ctfplayer.txt")
echo "oracle: DT_BaseAnimating slots=$BASEPROPS animation-property slots=$ORACLE_ANIM"
if [ "${BASEPROPS:-0}" -gt 0 ]; then
  echo "  OK   the oracle flattened CTFPlayer's DT_BaseAnimating block ($BASEPROPS slots)"
else
  echo "  FAIL the oracle printed no DT_BaseAnimating block; the witness has no anchor"; fail=1
fi
assert_eq "oracle animation-property slots for CTFPlayer" "$ORACLE_ANIM" "0"
# And the oracle must see the same classes we do: its own send-table mode can list
# every class, and CTFPlayer must be among them, so 0 above is a table that loaded
# and lacks the slots rather than a class that was never found.
"$ORACLE" "$BAGEL" 3 > "$OUT/oracle-all-classes.txt" 2>&1
if grep -q 'CTFPlayer' "$OUT/oracle-all-classes.txt"; then
  echo "  OK   the oracle's schema contains CTFPlayer"
else
  echo "  FAIL the oracle never named CTFPlayer; the negative has no subject"; fail=1
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "ANIMATION-AVAILABILITY=PASS"
else
  echo "ANIMATION-AVAILABILITY=FAIL"
fi
exit "$fail"
