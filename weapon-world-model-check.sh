#!/usr/bin/env bash
# weapon-world-model-check.sh -- a weapon's render request must name the weapon,
# not the hands that hold it.
#
# Why this exists
# ---------------
# TF2 splits a weapon's model in two, and the two indices sit next to each other
# in the send table:
#
#   DT_BaseEntity.m_nModelIndex            the first-person composite: on a weapon
#   DT_BaseCombatWeapon.m_iViewModelIndex  entity this is its class's c_*_arms
#   DT_BaseCombatWeapon.m_iWorldModelIndex the weapon itself -- what a dropped or
#                                          third-person weapon is drawn with
#
# Measured on the POV demo at server tick 55418, all eight held weapons read
# m_nModelIndex == m_iViewModelIndex and both differ from m_iWorldModelIndex
# (pistol 1097/1097 vs 255, medigun 1060/1060 vs 261, knife 1088/1088 vs 240),
# and modelprecache resolves 255 to c_pistol and 1097 to c_engineer_arms. So a
# world pass driven by m_nModelIndex draws a pair of hands where the weapon
# should be -- and it draws it *successfully*, from a real asset, which is why no
# count-based gate could see it.
#
# What is asserted
# ----------------
#   1. the fixture -- ten synthetic entities against a synthetic modelprecache
#      table, exercising every branch by construction (see the fixture comment in
#      entity_model_probe.cpp). Entity 10 is deliberately a weapon left naming its
#      arms, so the counter the real demos must read 0 on is known to be able to
#      fire at all;
#   2. bagel and the POV demo, end to end -- the counters plus the identities that
#      tie them together (resolved + zero + unresolved + outOfRange == known;
#      every non-zero world index took the route; no weapon is left with an arms
#      path; no weapon has a view index that neither matches its model index nor
#      reads 0);
#   3. the oracle witness -- tf_demo_parser reads m_iWorldModelIndex out of a
#      weapon's Enter packet, and that value, for the same entity index, is what
#      this decoder reports and what modelprecache maps to the path in the report.
#
# What is NOT asserted, and why
# -----------------------------
# There is no tick-for-tick value comparison against the oracle, because there
# cannot be one: neither index is ever written in the live window on either demo.
# Both ride the *instance baseline*, so a packet only carries them when it changes
# one, and this decoder is tick-exact only inside the ~70-packet live window. The
# scan is kept in the script rather than described here -- section 3 prints how
# many of the window's packets carried the identifier, and fails if that count is
# nonzero, so the day the premise stops holding is the day this gate says so
# instead of quietly comparing nothing.
#
# Usage: bash weapon-world-model-check.sh [--mutation]
#   --mutation perturbs the oracle's value by one and requires this script to go
#   red, which is how the witness is shown to be able to fail.
# Exit:  0 = every assertion held (or, with --mutation, the witness went red).
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_model_probe.exe
ORACLE="D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
POV="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
# mutate.sh m10 runs this script with the defect reintroduced; it redirects the
# output so the red readings cannot overwrite the fixed-build evidence.
OUT="${WEAPON_WORLD_MODEL_OUT:-evidence/weapon-world-model}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
for path in "$PROBE" "$ORACLE"; do
  [ -x "$path" ] || { echo "FATAL: missing binary: $path"; exit 1; }
done
for path in "$TF" "$BAGEL" "$POV"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

jvalue() { grep -oE "\"$2\":[0-9]+" "$1" | head -1 | cut -d: -f2; }

echo "=== 1/3 wiring fixture (synthetic entities, every branch by construction) ==="
"$PROBE" --self-test > "$OUT/fixture.json" 2> "$OUT/fixture.txt"
grep '^weapon-wiring-fixture' "$OUT/fixture.txt" | tee "$OUT/fixture-line.txt"
F_REFS=$(sed -n 's/.* refs=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_KNOWN=$(sed -n 's/.* known=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_RESOLVED=$(sed -n 's/.* resolved=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_ZERO=$(sed -n 's/.* zero=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_UNRES=$(sed -n 's/.* unresolved=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_RANGE=$(sed -n 's/.* outOfRange=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_ONLY=$(sed -n 's/.* onlyWorld=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_FROM=$(sed -n 's/.* fromWorld=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_AGREE=$(sed -n 's/.* agrees=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_VZERO=$(sed -n 's/.* viewZero=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_DIFF=$(sed -n 's/.* differs=\([0-9]*\) .*/\1/p' "$OUT/fixture-line.txt")
F_WARMS=$(sed -n 's/.* armsWeapon=\([0-9]*\).*/\1/p' "$OUT/fixture-line.txt")
echo "fixture refs=$F_REFS known=$F_KNOWN resolved=$F_RESOLVED zero=$F_ZERO unresolved=$F_UNRES outOfRange=$F_RANGE onlyWorld=$F_ONLY fromWorld=$F_FROM agrees=$F_AGREE viewZero=$F_VZERO differs=$F_DIFF armsWeapon=$F_WARMS"
# Exact, not bounded: the fixture is fixed, so every one of these is a reading the
# fixture's design pins. A bound here would let the whole route stop firing as long
# as it stopped firing quietly.
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_eq "fixture refs" "$F_REFS" "11"
assert_eq "fixture known" "$F_KNOWN" "10"
assert_eq "fixture resolved" "$F_RESOLVED" "6"
assert_eq "fixture zero" "$F_ZERO" "2"
assert_eq "fixture unresolved" "$F_UNRES" "1"
assert_eq "fixture outOfRange" "$F_RANGE" "1"
assert_eq "fixture onlyWorld" "$F_ONLY" "2"
assert_eq "fixture fromWorld" "$F_FROM" "6"
assert_eq "fixture agrees" "$F_AGREE" "6"
assert_eq "fixture viewZero" "$F_VZERO" "2"
assert_eq "fixture differs" "$F_DIFF" "1"
assert_eq "fixture armsWeapon (the counter the demos must read 0 on)" "$F_WARMS" "1"

echo
echo "=== 2/3 bagel and POV, end to end ==="
# One run per demo produces the JSON counters, the per-entity weapon listing and
# the precache table section 3 needs, so the slow demo is parsed once.
"$PROBE" --tf-root "$TF" --demo "$BAGEL" --history-stats --dump-weapon-models --dump-precache "*" \
  > "$OUT/bagel.json" 2> "$OUT/bagel-dump.txt"
"$PROBE" --tf-root "$TF" --demo "$POV" --dump-weapon-models --dump-precache "*" \
  > "$OUT/pov.json" 2> "$OUT/pov-dump.txt"

check_demo() { # check_demo <name> <expectedWorldPaths> <expectedKnown> <expectedZero>
  local name="$1" want_paths="$2" want_known="$3" want_zero="$4" file="$OUT/$1.json"
  local known resolved zero unres out_range from_world non_zero only arms_weapon arms_non_weapon
  local agrees view_zero differs with_world request_count
  known=$(jvalue "$file" assetWorldModelKnown)
  resolved=$(jvalue "$file" assetWorldModelResolved)
  zero=$(jvalue "$file" assetWorldModelZero)
  unres=$(jvalue "$file" assetWorldModelUnresolved)
  out_range=$(jvalue "$file" assetWorldModelOutOfRange)
  from_world=$(jvalue "$file" assetModelPathFromWorldModel)
  only=$(jvalue "$file" assetModelPathWorldModelOnly)
  non_zero=$(jvalue "$file" worldIndexNonZero)
  arms_weapon=$(jvalue "$file" armsWeaponRefs)
  arms_non_weapon=$(jvalue "$file" armsNonWeaponRefs)
  agrees=$(jvalue "$file" weaponViewModelAgrees)
  view_zero=$(jvalue "$file" weaponViewModelZero)
  differs=$(jvalue "$file" weaponViewModelDiffers)
  with_world=$(sed -n 's/^weapon-dump .* withWorldIndex=\([0-9]*\) .*/\1/p' "$OUT/$name-dump.txt")
  request_count=$(jvalue "$file" worldModelRequests)
  echo "$name refs=$(jvalue "$file" assetRefs) requests=$(jvalue "$file" requests) withWorldIndex=$with_world known=$known resolved=$resolved zero=$zero unresolved=$unres outOfRange=$out_range pathFromWorld=$from_world onlyWorld=$only worldIndexNonZero=$non_zero armsWeapon=$arms_weapon armsNonWeapon=$arms_non_weapon viewAgrees=$agrees viewZero=$view_zero viewDiffers=$differs"
  # The counters have to partition the population, or one of them is silently not
  # counting something. Checked before the values, because a broken partition makes
  # every value below meaningless.
  if [ "$((resolved + zero + unres + out_range))" -eq "${known:-0}" ] && [ "${known:-0}" -gt 0 ]; then
    echo "  OK   resolved + zero + unresolved + outOfRange = known = $known"
  else
    echo "  FAIL $resolved + $zero + $unres + $out_range != known ${known:-<none>}"; fail=1
  fi
  # The wiring claim itself: every entity whose world index is a real value took
  # the world route. This is the identity that makes the count meaningful -- a
  # build that read the property but ignored it would have non_zero > 0 and
  # from_world = 0.
  if [ "${non_zero:-0}" -gt 0 ] && [ "${from_world:-0}" -eq "${non_zero:-0}" ]; then
    echo "  OK   every one of $non_zero non-zero world indices named the path"
  else
    echo "  FAIL worldIndexNonZero=${non_zero:-<none>} but only ${from_world:-<none>} paths came from it"; fail=1
  fi
  # The defect's signature, asserted so that a regression cannot pass by being
  # merely "mostly right": the POV demo had 11 held weapons reading
  # m_nModelIndex == m_iViewModelIndex before this wiring existed.
  if [ "${arms_weapon:-1}" -eq 0 ]; then
    echo "  OK   no weapon is left naming its first-person arms"
  else
    echo "  FAIL $arms_weapon weapon(s) still name an arms model"; fail=1
  fi
  if [ "${unres:-1}" -eq 0 ]; then
    echo "  OK   every world index resolved in modelprecache"
  else
    echo "  FAIL $unres world index(es) named no declared entry"; fail=1
  fi
  # An index that is neither unset nor equal to m_nModelIndex would mean one of the
  # two slots is being read wrong; the POV demo's syringe gun (249/0) and
  # Crusader's Crossbow (381/0) are the unset case, counted separately. A real
  # conflict is a corpus shape rather than a wiring verdict, and it does exist off
  # these two demos: saytext2 reads 8 of them, all engineer items whose two slots
  # swap 851/961 (shotgun, builder, both PDAs, wrangler), with every path still
  # taken from the world route and armsWeaponRefs still 0 there. Pinned at 0 here
  # because on the two demos this gate covers, a conflict would be a new shape.
  if [ "${differs:-1}" -eq 0 ]; then
    echo "  OK   no view/model index conflict (viewZero=$view_zero is the unset case)"
  else
    echo "  FAIL $differs weapon(s) have a view index that is neither 0 nor m_nModelIndex"; fail=1
  fi
  # Expected readings, pinned. These are what mutate.sh m10 moves away from.
  assert_eq "$name pathFromWorld" "$from_world" "$want_paths"
  assert_eq "$name known" "$known" "$want_known"
  assert_eq "$name zero" "$zero" "$want_zero"
  assert_eq "$name worldModelRequests" "$request_count" "$want_paths"
  # onlyWorld is what claim 3b of check-probe-output-additive.sh ties the movement
  # of asset_model_path_known to; on both probe demos the world route replaces a
  # path rather than adding one, so this is 0 here and 3 on snakewater.
  assert_eq "$name onlyWorld" "$only" "0"
}

check_demo bagel 32 33 1
check_demo pov 69 71 2

echo
echo "=== 3/3 oracle witness ==="
# (a) The value. tf_demo_parser prints m_iWorldModelIndex when a packet writes it,
# and a weapon's Enter packet always writes it because the instance baseline holds
# 0: measured on bagel, entity 822's Enter at demo tick 52427 reads
# DT_BaseCombatWeapon.m_iWorldModelIndex = Integer(363) and modelprecache maps 363
# to models/weapons/c_models/c_rocketlauncher/c_rocketlauncher.mdl. The tick and
# the entity are derived from the oracle's own enter list rather than hard-coded,
# so this keeps working if the demo's packet stream shifts.
"$ORACLE" "$BAGEL" 1 > "$OUT/oracle-enters-bagel.txt" 2>&1
WITNESS_CLASSES='CTFRocketLauncher|CTFScatterGun|CTFKnife|CTFPistol|CTFMedigun|CTFMinigun'
WITNESS=$(grep -E "class=[0-9]+ ($WITNESS_CLASSES)/" "$OUT/oracle-enters-bagel.txt" | tail -1)
if [ -z "$WITNESS" ]; then
  echo "FATAL: the oracle reported no weapon Enter event on bagel; the witness has no anchor"
  exit 1
fi
TICK=$(printf '%s' "$WITNESS" | sed -n 's/^enter tick=\([0-9]*\) .*/\1/p')
ENTITY=$(printf '%s' "$WITNESS" | sed -n 's/.*entity=EntityId(\([0-9]*\)).*/\1/p')
CLASS=$(printf '%s' "$WITNESS" | sed -n 's/.*class=[0-9]* \([A-Za-z_0-9]*\)\/.*/\1/p')
echo "witness anchor: demo tick $TICK entity $ENTITY class $CLASS (oracle enter list, last match)"
"$ORACLE" "$BAGEL" "$TICK" > "$OUT/oracle-witness.txt" 2>&1
# The property has to come out of that entity's own block: a plain -A window can
# run into the next entity's properties and report its neighbour's value.
ORACLE_INDEX=$(awk -v want="$ENTITY" '
  /^  entity=EntityId\(/ { inblock = ($0 ~ ("^  entity=EntityId\\(" want "\\)")) ; next }
  inblock && /m_iWorldModelIndex = Integer\(/ { print; exit }
' "$OUT/oracle-witness.txt" | sed -n 's/.*Integer(\([0-9]*\)).*/\1/p')
if [ -z "$ORACLE_INDEX" ]; then
  echo "FATAL: the oracle's packet at tick $TICK did not carry m_iWorldModelIndex for entity $ENTITY"
  exit 1
fi
if [ "$MUTATION" -eq 1 ]; then
  # A witness whose expectation can be nudged by one and still agree is not
  # comparing anything.
  ORACLE_INDEX=$((ORACLE_INDEX + 1))
  echo "mutation: oracle expectation perturbed to $ORACLE_INDEX"
fi
OURS=$(grep "^weapon entity=$ENTITY " "$OUT/bagel-dump.txt")
OURS_INDEX=$(printf '%s' "$OURS" | sed -n 's/.*worldModelIndex=\([0-9]*\) .*/\1/p')
OURS_CLASS=$(printf '%s' "$OURS" | sed -n 's/^weapon entity=[0-9]* class=[0-9]* \([A-Za-z_0-9]*\) .*/\1/p')
OURS_PATH=$(printf '%s' "$OURS" | sed -n 's/.*path=\([^ ]*\) source=.*/\1/p')
OURS_SOURCE=$(printf '%s' "$OURS" | sed -n 's/.*source=\([a-z]*\).*/\1/p')
echo "oracle m_iWorldModelIndex=$ORACLE_INDEX / ours=$OURS_INDEX class=$OURS_CLASS path=$OURS_PATH source=$OURS_SOURCE"
if [ "$OURS_CLASS" != "$CLASS" ]; then
  echo "FATAL: entity $ENTITY is $OURS_CLASS in our snapshot but $CLASS in the oracle's packet;"
  echo "       the slot was reused after the packet, so this witness compares two different things"
  exit 1
fi
if [ -n "$OURS_INDEX" ] && [ "$OURS_INDEX" = "$ORACLE_INDEX" ]; then
  echo "  OK   the two parsers read the same world model index for entity $ENTITY"
else
  echo "  FAIL oracle=$ORACLE_INDEX ours=${OURS_INDEX:-<none>}"; fail=1
fi
if [ "$OURS_SOURCE" = "world" ]; then
  echo "  OK   the path came from the world route"
else
  echo "  FAIL source=${OURS_SOURCE:-<none>}"; fail=1
fi
# The path has to be what the index names in the table, not merely a weapon path:
# that closes the chain identifier -> table -> file.
if [ -n "$OURS_INDEX" ] && grep -qF "precache idx=$OURS_INDEX $OURS_PATH" "$OUT/bagel-dump.txt"; then
  echo "  OK   modelprecache[$OURS_INDEX] is that path"
else
  echo "  FAIL modelprecache[$OURS_INDEX] does not name $OURS_PATH"; fail=1
fi

# (b) The skip, with its proof. The window is derived rather than hard-coded: its
# lower bound is the oldest live checkpoint (`queryEntitySnapshotAtOrBeforeTick`
# answers everything at or after it by replaying the retained packets), and its
# upper bound is the last packet's own tick, both read from the run above. Every
# packet in it is checked, and the count that carried the identifier is printed. A
# run that could not reach the oracle lands here too: `packets` would be 0, and 0
# packets is a failure, not a skip.
LIVE_LO=$(sed -n 's/^history liveCheckpoints=[0-9]* ticks=\[\([-0-9]*\)\.\..*/\1/p' "$OUT/bagel-dump.txt" | head -1)
if [ -z "$LIVE_LO" ]; then
  echo "FATAL: the probe did not report a live window; the skip claim has no extent"
  exit 1
fi
"$ORACLE" "$BAGEL" 0 > "$OUT/oracle-survey-bagel.txt" 2>&1
LIVE_HI=$(awk '/^pkt tick=/{match($0, /ServerTick\(([0-9]+)\)/, s); if (s[1] != "" && s[1] + 1 > hi) hi = s[1] + 1} END {print hi + 0}' "$OUT/oracle-survey-bagel.txt")
PACKETS=0
CARRIED=0
while read -r demotick server; do
  [ "$server" -lt "$LIVE_LO" ] && continue
  [ "$server" -gt "$LIVE_HI" ] && continue
  PACKETS=$((PACKETS + 1))
  if "$ORACLE" "$BAGEL" "$demotick" 2>&1 | grep -q 'm_iWorldModelIndex'; then
    CARRIED=$((CARRIED + 1))
  fi
done < <(awk '/^pkt tick=/{match($0, /tick=([0-9]+)/, t); match($0, /ServerTick\(([0-9]+)\)/, s); if (t[1] != "" && s[1] != "") print t[1], s[1] + 1}' "$OUT/oracle-survey-bagel.txt")
echo "live window [$LIVE_LO..$LIVE_HI]: packets checked=$PACKETS that carried m_iWorldModelIndex=$CARRIED"
if [ "$PACKETS" -gt 0 ] && [ "$CARRIED" -eq 0 ]; then
  echo "  OK   no packet in the live window writes the property, so no tick-exact"
  echo "       comparison exists -- the property rides the instance baseline, and"
  echo "       this decoder is tick-exact only inside that window"
else
  echo "  FAIL packets=$PACKETS carried=$CARRIED -- the premise changed; either the"
  echo "       window now contains a write (so compare it) or the oracle never ran"
  fail=1
fi

echo
if [ "$MUTATION" -eq 1 ]; then
  if [ "$fail" -ne 0 ]; then
    echo "MUTATION-CAUGHT=PASS (the witness went red on a one-off perturbation)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (a one-off perturbation went unnoticed)"
  exit 1
fi
if [ "$fail" -eq 0 ]; then
  echo "WEAPON-WORLD-MODEL=PASS"
else
  echo "WEAPON-WORLD-MODEL=FAIL"
fi
exit "$fail"
