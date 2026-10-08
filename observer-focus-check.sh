#!/usr/bin/env bash
# observer-focus-check.sh -- the observation target resolves; the camera must not
# follow it yet, and this script says why with numbers.
#
# Why this exists
# ---------------
# Nothing in this pipeline read either half of TF2's observer pair:
#
#   DT_BasePlayer.m_iObserverMode    send-table slot 835, 3 bits
#   DT_BasePlayer.m_hObserverTarget  send-table slot 836, 21 bits -- a CBaseHandle
#
# Source's observe_mode.h defines 0=none 1=deathcam 2=freezecam 3=fixed 4=in-eye
# 5=chase 6=roaming, and only in-eye and chase put the camera on the target. This
# round resolves both halves, checks them against an independent parser, and then
# measures what believing them would cost.
#
# What it measured
# ----------------
#   POV, archive checkpoints 53539 and 53976: the recorder (entity 18 -- the demo's
#     own setview target, the entity carrying DT_LocalPlayerExclusive in every
#     packet, and the entity whose own origin the recorded camera sits on) declares
#     mode 4, target handle 1775619 == index 3 | serial 867 << 11, holding that for
#     the whole 437-tick stretch before it respawns by 54747.
#   That camera (dem_cmdinfo at demo tick 37677, which the oracle's packet list
#     pairs with server tick 54000) sits at x=-1112.031250 y=505.593719
#     z=459.031250, and its pitch, 13.764709, is byte-identical to the recorder's
#     own m_angEyeAngles[0].
#   Entity 3 carries the same quantity twice. When this round first ran, the
#     coordinate this pipeline's renderer would use for it was
#     x=1511.459961 y=894.130005 z=-186.000000 -- its
#     DT_TFLocalPlayerExclusive.m_vecOrigin, the slot the rank rule preferred for
#     its full precision. 2729 units from that camera, 2623 of them along x alone.
#     Its own m_nTickBase read 51597 against the checkpoint's 53976, and the slot
#     was last written at 51596 and never again: the rank rule was resolving a
#     value 2379 ticks stale, while the target's
#     DT_TFNonLocalPlayerExclusive copy (written at 53976, reading
#     x=-1112.000000 y=461.250000 z=455.250000) sat 44 units from the camera.
#
# The slot-freshness round (2026-10-08) made the rule prefer the later-written
# slot, and this script's own pinned distance moved with it: 2729 -> 45. That is
# the correction, and it is why this file is a diff rather than a sentence. The
# rank rule had been right often enough that nothing else caught it -- on bagel the
# Local copy *is* the truth, and the divergence there is zero by construction.
#
# What is still true, and what is no longer true
# ----------------------------------------------
# The renderer still does not follow the target. What changed is the reason, twice.
# It used to be a measurement -- believing the mode would move this demo's view 2729
# units off where the demo recorded it. After the freshness round it read 45, which
# looked like a small residual. It is neither a residual nor a cost.
#
# The 2026-10-08 attribution round read the stretch the number comes from and found
# three things that were not in this file before:
#
#   * the recorded camera origin is *held*, not sampled per tick. The raw
#     democmdinfo blocks at demo ticks 37640, 37650, 37655, 37677 and 37679 are
#     byte-identical except for the angles, so the origin the subtraction uses is
#     the value from demo tick 37639 and not a position at 37677. Section 3 re-reads
#     three of those ticks through this pipeline and asserts they are one value.
#   * that value is the recorder's *own* origin, and the recorder is dead there:
#     m_lifeState = 2 and FL_TRANSRAGDOLL set at the checkpoint the subtraction
#     uses. The camera is a deathcam frozen where the player died.
#   * the observer pair names the recorder's own body. A 541-query census finds 13
#     distinct checkpoints where the pair asks for a follow, and at 11 of them the
#     target's resolved coordinate sits on the recorder's own resolved coordinate to
#     within 0.15 units -- both read from the same snapshot, so not a cross-instant
#     comparison.
#
# Put together: 45 units is a frozen deathcam measured against the same body 12
# server ticks later, after that body had fallen 44.28 in y and 3.78 in z. It is a
# time/state difference, not a position error -- and because the pair only ever names
# the recorder's own body, this demo contains no checkpoint where the camera is asked
# to follow a different player. That is the answer the wiring question needs: this
# demo cannot decide it, so the review's deferral stands, but no longer on the
# strength of a 45 that was never the follow's cost.
#
# What is asserted
# ----------------
#   1. the fixture -- eleven synthetic entities exercising every branch by
#      construction (see the fixture comment in entity_model_probe.cpp). Entity 6 is
#      a deathcam carrying a perfectly good handle, so the counter that has to stay
#      0 is known to be able to fire at all;
#   2. POV, end to end -- the recorder's observing life across five checkpoints with
#      the tick each answer actually came from, and the handle split into index and
#      serial, and mode 4 only where the demo says in-eye;
#   3. the divergence and its attribution -- the distance from the recorded camera to
#      the target's resolved coordinate, pinned at 45 with the per-axis split printed
#      and the target's own tickbase witness; that the camera origin is held across
#      the stretch (three samples, one value); that the recorder is dead where the
#      subtraction happens (m_lifeState 2, FL_TRANSRAGDOLL); and the 541-query follow
#      census (13 distinct checkpoints, 11 of them on the recorder's own body, worst
#      37.812 units). This is the line that caught the slot rule -- 2729 before the
#      freshness round, 45 after -- so the claim it makes is a measurement that has
#      already moved twice, not a preference;
#   4. the oracle witness -- tf_demo_parser's raw packet at demo tick 33242 carries
#      m_iObserverMode = 1 and m_hObserverTarget = 606217 for entity 18, and both
#      come out of this decoder's own snapshot of the same packet.
#
# What is NOT asserted, and why
# -----------------------------
# There is no "camera == targetOrigin" assertion. Measured on the POV demo it is
# false both before and after the freshness fix -- 2729 units then, 45 now -- and
# pinning a claim that is false at either distance green is worse than leaving it
# unasserted. What the 45 is made of is attributed instead, per axis and per state
# (a held deathcam against the same body twelve ticks later), and the census is the
# assertion that says why this demo cannot settle the wiring question.
#
# What is still NOT asserted, and should not be read as settled: that the observer
# pair ever names a *different* player. Two of the thirteen census checkpoints are
# 37.812 and 9.426 units off the recorder's own body; whether those are a second
# player, a coarser snapshot of the same body, or a target that has gone stale is
# not decided here. The census counts what the pair asks for, not who it is.
# Bagel does agree -- entity 1's own coordinate equals its target's exactly at
# 129277 -- but cannot serve as the witness either: its dem_cmdinfo stream ends at
# demo tick ~69881 while its entity history runs to 129277, so there is no recorded
# camera there to compare against. That fact is printed, not asserted past.
#
# Usage: bash observer-focus-check.sh [--mutation]
#   --mutation nudges three compared values by one each -- the oracle's expected mode,
#   the pinned divergence, and the census's follow-checkpoint count -- and requires all
#   three assertions to go red, so each of them is shown able to fail.
# Exit:  0 = every assertion held (or, with --mutation, all three went red).
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_model_probe.exe
ORACLE="D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
POV="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
OUT="${OBSERVER_FOCUS_OUT:-evidence/observer-focus}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
ok=0
bad=0
for path in "$PROBE" "$ORACLE"; do
  [ -x "$path" ] || { echo "FATAL: missing binary: $path"; exit 1; }
done
for path in "$TF" "$BAGEL" "$POV"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
    ok=$((ok + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; bad=$((bad + 1))
  fi
}
# field <text> <key> -- one `key=value` token; the value runs to the next space.
field() { printf '%s' "$1" | sed -n "s/.* $2=\\([^ ]*\\).*/\\1/p" | head -1; }

echo "=== 1/4 wiring fixture (synthetic entities, every branch by construction) ==="
"$PROBE" --self-test > "$OUT/fixture.json" 2> "$OUT/fixture.txt"
grep '^observer-focus-fixture' "$OUT/fixture.txt" > "$OUT/fixture-line.txt"
cat "$OUT/fixture-line.txt"
FL=$(cat "$OUT/fixture-line.txt")
# Exact, not bounded: the fixture is fixed, so each of these is a reading its design
# pins. A bound would let a branch stop firing as long as it stopped quietly.
assert_eq "fixture cases" "$(field "$FL" cases)" "11"
assert_eq "fixture withMode" "$(field "$FL" withMode)" "9"
assert_eq "fixture modeNonZero" "$(field "$FL" modeNonZero)" "8"
assert_eq "fixture hasTarget" "$(field "$FL" hasTarget)" "8"
assert_eq "fixture inRange" "$(field "$FL" inRange)" "5"
assert_eq "fixture present" "$(field "$FL" present)" "4"
assert_eq "fixture missing (slot present, entity gone)" "$(field "$FL" missing)" "1"
assert_eq "fixture outOfRange (index past the snapshot)" "$(field "$FL" outOfRange)" "3"
# Two follow: one in-eye, one chase. Entity 6 is a deathcam holding a present
# handle, so this counter reading 2 and not 3 is the whole point of the fixture.
assert_eq "fixture follows (in-eye + chase only, not the deathcam)" "$(field "$FL" follows)" "2"

echo
echo "=== 2/4 POV -- the recorder's observing life, with the tick each answer came from ==="
# Five queries, one run. --entity 18 walks only the recorder's row; `resolved` is
# printed by the probe rather than inferred, because an archive answer is stale by
# construction and comparing values across instants compares nothing.
"$PROBE" --tf-root "$TF" --demo "$POV" --observer-focus-at 51900,53400,53976,54747,55394 --entity 18 \
  > "$OUT/pov.json" 2> "$OUT/pov-row.txt"
cat "$OUT/pov-row.txt"
ROWS=$(grep '^  observer entity=18 ' "$OUT/pov-row.txt")
if [ "$(printf '%s\n' "$ROWS" | wc -l)" -ne 5 ]; then
  echo "FATAL: expected 5 observer rows for entity 18, got $(printf '%s\n' "$ROWS" | wc -l)"
  exit 1
fi
RESOLVED=$(sed -n 's/^observer-focus at tick=[0-9]* status=[^ ]* resolved=\([0-9]*\) .*/\1/p' "$OUT/pov-row.txt" | tr '\n' ' ')
echo "resolved ticks: $RESOLVED"
# Pinned, so a change in the archive's thinning shows up here instead of silently
# moving the readings below.
assert_eq "resolved ticks (archive grid, in query order)" "$RESOLVED" "51742 53075 53976 54747 55394 "
R1=$(printf '%s\n' "$ROWS" | sed -n '1p')
assert_eq "row 1 mode (alive between deaths)" "$(field "$R1" mode)" "0"
assert_eq "row 1 follows" "$(field "$R1" follows)" "0"
# Checkpoint 53976 is the one worth every digit: it is inside the in-eye stretch and
# only 24 ticks from where the camera in section 3 is taken.
R3=$(printf '%s\n' "$ROWS" | sed -n '3p')
assert_eq "row 3 mode (in-eye)" "$(field "$R3" mode)" "4"
assert_eq "row 3 targetHandle" "$(field "$R3" targetHandle)" "1775619"
# 1775619 = 3 | 867 << 11. The handle is split two ways on purpose: only the index
# can be looked up in a snapshot, and EntityState carries no serial to match it to.
assert_eq "row 3 targetIndex" "$(field "$R3" targetIndex)" "3"
assert_eq "row 3 targetSerial" "$(field "$R3" targetSerial)" "867"
assert_eq "row 3 follows" "$(field "$R3" follows)" "1"
# Before the freshness round this read entity 3's DT_TFLocalPlayerExclusive value,
# x=1511.459961 y=894.130005 z=-186.000000 -- the slot last written at 51596. The
# rule now takes the slot written at 53976, so the reading moved to the NonLocal
# copy. Both names are pinned by name elsewhere (slot-freshness-check.sh); what is
# pinned here is the coordinate a follow would use.
assert_eq "row 3 targetOrigin (what a follow would use)" "$(field "$R3" targetOrigin)" "-1112.000000,461.250000,455.250000"
assert_eq "row 3 selfOrigin (where the camera actually is)" "$(field "$R3" selfOrigin)" "-1112.019531,461.310516,455.251282"
R4=$(printf '%s\n' "$ROWS" | sed -n '4p')
assert_eq "row 4 mode (respawned, watching nothing)" "$(field "$R4" mode)" "0"
assert_eq "row 4 follows" "$(field "$R4" follows)" "0"
R5=$(printf '%s\n' "$ROWS" | sed -n '5p')
assert_eq "row 5 mode (live window, tick-exact)" "$(field "$R5" mode)" "0"
assert_eq "row 5 follows" "$(field "$R5" follows)" "0"
SUM=$(grep '^observer-focus-summary' "$OUT/pov-row.txt")
assert_eq "POV ticks walked" "$(field "$SUM" ticks)" "5"
assert_eq "POV entities examined" "$(field "$SUM" examined)" "5"
assert_eq "POV follows total" "$(field "$SUM" follows)" "1"

echo
echo "=== 3/4 the divergence -- what following the target would cost ==="
# The recorded camera at demo tick 37677: the oracle's packet list pairs that demo
# tick with server tick 54000, and the probe's answer for 54000 resolves to
# checkpoint 53976, so the two instants are 24 ticks apart instead of misaligned.
# Two earlier ticks ride along in the same run because the next assertion needs them
# (the origin is held across the stretch), and one pass over the demo is enough for
# all three.
"$PROBE" --tf-root "$TF" --demo "$POV" --camera-at 37640,37650,37677 > /dev/null 2> "$OUT/pov-camera.txt"
cat "$OUT/pov-camera.txt"
CAM=$(grep 'tick=37677 ' "$OUT/pov-camera.txt" | tail -1)
CAM_ORIGIN=$(printf '%s' "$CAM" | sed -n 's/.*origin=\([^ ]*\) .*/\1/p')
CAM_PITCH=$(printf '%s' "$CAM" | sed -n 's/.*angles=\([^,]*\),.*/\1/p')
echo "recorder own origin $(field "$R3" selfOrigin) / recorded camera $CAM_ORIGIN / pitch $CAM_PITCH"
# The recorded camera's pitch is the recorder's own eye pitch to the last decimal
# (verged from entity 18's DT_TFLocalPlayerExclusive.m_angEyeAngles[0] in the
# oracle's packet at the same demo tick). If it were the target's view, it would be
# the target's pitch instead.
assert_eq "recorded camera pitch is the recorder's own eye pitch" "$CAM_PITCH" "13.764709"
DIVERGENCE=$(awk -v c="$CAM_ORIGIN" -v t="$(field "$R3" targetOrigin)" 'BEGIN{
    n=split(c,A,","); for(i=1;i<=n;i++) if (A[i] !~ /^-?[0-9]/) exit 1
    m=split(t,B,","); s=0
    for(i=1;i<=n;i++) s+=(A[i]-B[i])*(A[i]-B[i])
    printf "%d", int(sqrt(s)+0.5)}')
if [ -z "$DIVERGENCE" ]; then
  echo "FATAL: could not measure the camera-to-target distance from '$CAM_ORIGIN'"
  exit 1
fi
# The same subtraction, axis by axis. 45 units is small enough to be waved through as
# "close enough, everywhere" -- but it is not spread evenly, and which axis carries it
# is the part a reader needs before deciding whether the follow is safe to wire.
# Printed rather than summarised away.
AXIS_SPLIT=$(awk -v c="$CAM_ORIGIN" -v t="$(field "$R3" targetOrigin)" 'BEGIN{
    n=split(c,A,","); split(t,B,",")
    for(i=1;i<=n;i++) printf "%s%.3f", (i>1 ? "," : ""), A[i]-B[i]}')
echo "per-axis camera-minus-target (x,y,z): $AXIS_SPLIT"
if [ "$MUTATION" -eq 1 ]; then
  DIVERGENCE=$((DIVERGENCE + 1))
  echo "mutation: expected divergence perturbed to $DIVERGENCE"
fi
echo "camera-to-target distance = $DIVERGENCE units (2729 before the freshness round moved it)"
assert_eq "the pinned camera-to-target distance" "$DIVERGENCE" "45"

# What the 45 is made of, part one: the camera origin is not a position at 37677. The
# demo's own cmdinfo stream repeats one origin across a 39-tick stretch -- the 76-byte
# democmdinfo block at demo ticks 37640, 37650, 37655, 37677 and 37679 is byte-identical
# except for the angles, read straight out of the file. The three ticks sampled in the
# run above are re-read here as a set, so the hold is a reading this script makes rather
# than a claim about a file it opened once. A held value cannot be subtracted from a
# per-tick one and called a distance.
HOLD_VALUES=$(sed -n 's/.*origin=\([^ ]*\) .*/\1/p' "$OUT/pov-camera.txt" | sort -u)
HOLD_COUNT=$(printf '%s\n' "$HOLD_VALUES" | grep -c .)
echo "camera origin at demo ticks 37640 / 37650 / 37677: $HOLD_COUNT distinct value(s)"
assert_eq "the recorded camera origin is held across the 45-unit stretch" "$HOLD_COUNT" "1"
assert_eq "and the held value is the one this section subtracts" "$HOLD_VALUES" "$CAM_ORIGIN"

# Part two: the recorder is dead where the subtraction happens, so what is being
# measured is a deathcam against a corpse. FL_TRANSRAGDOLL is 0x40000000 and
# m_lifeState 2 is LIFE_DEAD; both are read out of the recorder's own row rather than
# inferred from the shape of the camera track.
"$PROBE" --tf-root "$TF" --demo "$POV" --props-at 53976 --entity 18 > /dev/null 2> "$OUT/pov-e18-dead.txt"
E18_LIFE=$(sed -n 's/.*DT_BasePlayer\.m_lifeState .*int=\([0-9]*\).*/\1/p' "$OUT/pov-e18-dead.txt" | head -1)
E18_FLAGS=$(sed -n 's/.*DT_BasePlayer\.m_fFlags .*int=\([0-9]*\).*/\1/p' "$OUT/pov-e18-dead.txt" | head -1)
echo "recorder at checkpoint 53976: m_lifeState=$E18_LIFE m_fFlags=$E18_FLAGS (FL_TRANSRAGDOLL bit = $(( ${E18_FLAGS:-0} & 1073741824 )))"
assert_eq "recorder lifeState at the checkpoint (LIFE_DEAD)" "$E18_LIFE" "2"
assert_eq "recorder carries FL_TRANSRAGDOLL there" "$(( ${E18_FLAGS:-0} & 1073741824 ))" "1073741824"

# Part three: the census. Every 100th server tick across the demo -- 541 queries, 387
# of them inside the demo's own window -- asking the pair what it wants. `follows` is 1
# only where the mode is in-eye or chase *and* the target resolves. For each of those
# the target's resolved coordinate is compared with the recorder's own resolved
# coordinate from the same snapshot; that difference is a reading about who the pair
# names, and it is the line that says whether this demo can answer the wiring question
# at all.
CENSUS_TICKS=$(seq 1000 100 55000 | tr '\n' ',' | sed 's/,$//')
"$PROBE" --tf-root "$TF" --demo "$POV" --observer-focus-at "$CENSUS_TICKS" --entity 18 \
  > "$OUT/pov-census.json" 2> "$OUT/pov-census.txt"
CENSUS=$(awk '
  /^observer-focus at tick=/ { for (i=1;i<=NF;i++) { split($i,a,"="); if (a[1]=="resolved") res=a[2] } ; next }
  /^  observer/ && /follows=1/ {
    to=""; so=""; for (i=1;i<=NF;i++) { split($i,a,"="); if (a[1]=="targetOrigin") to=a[2]; if (a[1]=="selfOrigin") so=a[2] }
    if (to=="" || so=="") next
    n=split(to,T,","); split(so,S,","); d=0; for (i=1;i<=n;i++) d+=(T[i]-S[i])*(T[i]-S[i])
    printf "%s %.3f\n", res, sqrt(d)
  }' "$OUT/pov-census.txt" | sort -u)
CENSUS_CK=$(printf '%s\n' "$CENSUS" | grep -c .)
CENSUS_ONBODY=$(printf '%s\n' "$CENSUS" | awk '$2 <= 0.15' | grep -c .)
CENSUS_WORST=$(printf '%s\n' "$CENSUS" | awk 'BEGIN{m=0} { if ($2>m) m=$2 } END { printf "%.3f", m }')
echo "follow checkpoints (541 queries): $CENSUS_CK distinct; target on the recorder's own coordinate at $CENSUS_ONBODY of them (<=0.15u); worst $CENSUS_WORST u"
if [ "$MUTATION" -eq 1 ]; then
  CENSUS_CK=$((CENSUS_CK + 1))
  echo "mutation: expected follow-checkpoint count perturbed to $CENSUS_CK"
fi
assert_eq "distinct follow checkpoints in the census" "$CENSUS_CK" "13"
assert_eq "of those, the target resolves onto the recorder's own body" "$CENSUS_ONBODY" "11"
assert_eq "worst target-to-recorder gap on one snapshot" "$CENSUS_WORST" "37.812"
# Entity 3's own staleness witness, read from the entity rather than assumed: the
# tick its m_nTickBase reports against the checkpoint it was answered from. It is the
# reason the slot the rule used to take was 2379 ticks behind, and it is still read
# from the entity so the explanation cannot drift from the state it explains.
"$PROBE" --tf-root "$TF" --demo "$POV" --props-at 53976 --entity 3 > /dev/null 2> "$OUT/pov-e3.txt"
E3_TICKBASE=$(sed -n 's/.*DT_LocalPlayerExclusive\.m_nTickBase .*int=\([0-9]*\).*/\1/p' "$OUT/pov-e3.txt" | head -1)
E3_NONLOCAL=$(sed -n 's/.*DT_TFNonLocalPlayerExclusive\.m_vecOrigin type=3 x=\([^ ]*\) y=\([^ ]*\) z=.*/\1,\2/p' "$OUT/pov-e3.txt" | head -1)
echo "entity 3 m_nTickBase=$E3_TICKBASE against checkpoint 53976; the slot the rule now takes reads $E3_NONLOCAL"
assert_eq "entity 3 tickbase (2379 ticks behind the checkpoint)" "$E3_TICKBASE" "51597"
assert_eq "entity 3 chosen-slot x,y (the one ~45 units from the camera)" "$E3_NONLOCAL" "-1112.000000,461.250000"
# Bagel, where the same divergence is zero and therefore harmless -- which is why the
# rank rule went unchallenged for a round. It has no recorded camera anywhere near
# its entity window, so no camera claim is made for it.
"$PROBE" --tf-root "$TF" --demo "$BAGEL" --observer-focus-at 129277 --entity 1 \
  > "$OUT/bagel.json" 2> "$OUT/bagel-row.txt"
cat "$OUT/bagel-row.txt"
B=$(grep '^  observer entity=1 ' "$OUT/bagel-row.txt")
assert_eq "bagel mode (in-eye)" "$(field "$B" mode)" "4"
assert_eq "bagel targetIndex" "$(field "$B" targetIndex)" "3"
assert_eq "bagel follows" "$(field "$B" follows)" "1"
assert_eq "bagel selfOrigin == targetOrigin (why the rank rule looks fine there)" \
  "$(field "$B" selfOrigin)" "$(field "$B" targetOrigin)"
echo "(no camera comparison for bagel: its dem_cmdinfo stream ends at demo tick ~69881," \
     "its entity history runs to server tick 129277)"

echo
echo "=== 4/4 oracle witness (tf_demo_parser, independent of this decoder) ==="
# Demo tick 33242 is the packet where the recorder dies, so the mode is written
# there for the first time and a Preserve update carries it. Both values are read
# out of entity 18's own block: a plain -A window runs into the next entity's
# properties and would report its neighbour's.
"$ORACLE" "$POV" 33242 > "$OUT/oracle-witness.txt" 2>&1
O_MODE=$(awk '
  /^  entity=EntityId\(/ { inblock = ($0 ~ /^  entity=EntityId\(18\) class=/) ; next }
  inblock && /m_iObserverMode = Integer\(/ { print; exit }' "$OUT/oracle-witness.txt" \
  | sed -n 's/.*Integer(\([0-9]*\)).*/\1/p')
O_TARGET=$(awk '
  /^  entity=EntityId\(/ { inblock = ($0 ~ /^  entity=EntityId\(18\) class=/) ; next }
  inblock && /m_hObserverTarget = Integer\(/ { print; exit }' "$OUT/oracle-witness.txt" \
  | sed -n 's/.*Integer(\([0-9]*\)).*/\1/p')
if [ -z "$O_MODE" ] || [ -z "$O_TARGET" ]; then
  echo "FATAL: no observer pair for entity 18 in the oracle's packet at demo tick 33242"
  exit 1
fi
if [ "$MUTATION" -eq 1 ]; then
  O_MODE=$((O_MODE + 1))
  echo "mutation: oracle expectation perturbed to mode $O_MODE"
fi
# 49589 is the first archive checkpoint past that packet (27 ticks after it), so this
# is this decoder's own reading of the packet the oracle just printed.
"$PROBE" --tf-root "$TF" --demo "$POV" --observer-focus-at 49589 --entity 18 \
  > /dev/null 2> "$OUT/pov-death.txt"
D=$(grep '^  observer entity=18 ' "$OUT/pov-death.txt")
D_STATUS=$(sed -n 's/^observer-focus at tick=49589 status=\([^ ]*\).*/\1/p' "$OUT/pov-death.txt")
echo "oracle mode=$O_MODE target=$O_TARGET / ours mode=$(field "$D" mode) handle=$(field "$D" targetHandle) idx=$(field "$D" targetIndex) serial=$(field "$D" targetSerial) status=$D_STATUS"
assert_eq "witness mode" "$(field "$D" mode)" "$O_MODE"
assert_eq "witness target handle (packed)" "$(field "$D" targetHandle)" "$O_TARGET"
# 606217 = 9 | 296 << 11.
assert_eq "witness handle -> index" "$(field "$D" targetIndex)" "9"
assert_eq "witness handle -> serial" "$(field "$D" targetSerial)" "296"
assert_eq "witness answers deathcam, not a follow" "$(field "$D" follows)" "0"

echo
if [ "$MUTATION" -eq 1 ]; then
  # Three perturbations, one per compared value: the pinned divergence, the oracle's
  # expected mode, and the census's checkpoint count. All three have to land. A single
  # red line would let a perturbation that fired for the wrong reason pass as a caught
  # one, which is exactly how a gate stops testing what it claims to test.
  if [ "$bad" -eq 3 ]; then
    echo "MUTATION-CAUGHT=PASS (3/3 perturbations went red)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (expected 3 red lines, saw $bad)"
  exit 1
fi
if [ "$fail" -eq 0 ]; then
  echo "OBSERVER-FOCUS=PASS (assertions_ok=$ok)"
else
  echo "OBSERVER-FOCUS=FAIL"
fi
exit "$fail"
