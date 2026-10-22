#!/usr/bin/env bash
# outline-check.sh -- T52: team-coloured x-ray player outlines.
#
# WHY THIS EXISTS
# ---------------
# T52 makes four claims, and every one of them is a statement that could be
# true for the wrong reason:
#
#   1. IDENTITY. "The outline is on that player, and that player is RED." A
#      frame that contains red pixels says nothing about which entity they
#      belong to. So the gate asserts the PRODUCT's own sample: at every
#      sampled tick, each row carries an entity index, a class name, a team
#      value and a pose, and the draw list it built joins back to those rows by
#      entity index. Those readings come from entity_overlay_probe, which calls
#      the same sampleOverlayEntities and buildOutlineDrawList the product does.
#
#   2. NOTHING ELSE CHANGED. "The overlay does not change world geometry or
#      ordinary visibility" is asserted as: with the overlay on, every pixel
#      OUTSIDE the drawn silhouette's bounds is byte-identical to the same tick
#      with the overlay off, and the pass reports ordinal 6 -- strictly before
#      the ViewModel pass. A byte count would pass for an overlay that had
#      shifted the whole world by one pixel; a bound cannot.
#
#   3. TEAM COLOURS, DIRECTIONALLY. The red player's region must be
#      red-dominant AND contain no blue-dominant pixels, and the blue player's
#      region the mirror image. Exchanging the two colours fails both halves at
#      once, which is what makes that mutation mean something.
#
#   4. DEPTH AWARENESS. The depth-tested control is not "fewer pixels changed".
#      In a region where the frame's own depth buffer says world geometry is in
#      front of the player, the x-ray frame must show team-coloured pixels and
#      the depth-tested frame must show NONE -- and must be byte-identical to
#      the overlay-off frame there.
#
# COVERAGE
#   The five inputs the task names are covered by two layers, because only one
#   of them can be a picture:
#     probe --fixtures   visible, unknown team, despawn, and no players at all
#                        are asserted as data (a player with team 0 must land in
#                        the unknown bucket and must fail BOTH team predicates)
#     live frames        visible and occluded are asserted as pictures, using a
#                        demo whose camera holds two players at different depths
#
# THE TICK LIST
#   The sampled ticks and the capture tick have to land inside the C++ entity
#   history's RETAINED WINDOW. Outside it the accessor answers "unavailable",
#   the product fills its scene states from the same accessor and gets none, and
#   a frame at that tick therefore has no player in it at all. That is what the
#   inherited tick list (1001..2001) was measuring: six ticks, six "the tick
#   precedes every retained snapshot", zero rows. The probe measures the window
#   and prints it (--find-ticks); the ticks below are inside it.
#
# MUTATIONS (--mutation), each rebuilt and each required to turn a reading red
#   m1 swap the two team colours        -> team: red_wrong_team/blue_wrong_team
#   m2 depth-test only                  -> depth: occluded_xray drops to 0, and
#                                          the published x-ray depth TEST flips
#   m3 overlay always on and leaks      -> the OFF frame gains team pixels
#   m5 the overlay writes depth         -> the published depth WRITE flips
#   m6 capture lands near, not at, tick -> the speed=1 and speed=default frames differ
#   m4 move the pass after ViewModel    -> ordinal: orderOk=0 and ordinal != 6
# Each mutation restores the source and the restore is verified with cmp.
#
# SECTION 0 runs the two Python readers on synthetic frames and traces and
# requires every reading to answer BOTH ways (correct input passes, the defect
# each mutation reintroduces fails). It needs no GPU, so it also runs while the
# machine is busy -- which is where it was written.
#
# All product runs go through the host lock and carry --hidden-window
# --audio-disabled: builds, launches and captures are single-instance here.
#
# Usage: bash outline-check.sh [--mutation] [--skip-live]
#   --skip-live  run only the probe layer (no product launch); the gate says so
#                in its verdict rather than reporting a silent pass.
# Exit: 0 = every asserted criterion held.
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/entity_overlay_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# The demo whose map is installed on this machine (koth_bagel_rc13 is not), so
# the frame is a real world with real players rather than the fallback quad.
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="${OUTLINE_OUT:-evidence/outline}"
mkdir -p "$OUT"

MUTATION=0
SKIP_LIVE=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --skip-live) SKIP_LIVE=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"

fail=0
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ -n "${2:-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
assert_le() { # assert_le <label> <actual> <maximum>
  if [ -n "${2:-}" ] && [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}

# v <file> <key> -- the value of the LAST "<key>=<value>" token on a report file.
# The gate reads the probe's own sentence instead of inventing a key, so the keys
# carry spaces; the leading anchor stops "rows" from matching "ticksWithRows",
# and "last" wins because a run's final line is the one that describes the frame.
v() {
  local file="$1" key="$2" line
  # Exactly two arguments. An unquoted multi-word key arrives as three, and the
  # reading then comes back empty -- which reads as "the probe printed nothing"
  # instead of as a caller mistake. It said exactly that during the dry run.
  if [ "$#" -ne 2 ]; then
    echo "v() wants <file> <key>; multi-word keys must be quoted (got $#)" >&2
    return 1
  fi
  [ -f "$file" ] || return 0
  line=$(grep -a -o -E "(^| )${key}=[^ ]+" "$file" 2>/dev/null | tail -1)
  printf '%s' "$line" | sed -e 's/^ //' -e "s/^${key}=//"
}

# Missing input is a failure, not a skip: a gate that quietly SKIPs when the
# binary or the demo is absent reads as green while measuring nothing.
missing=0
for path in "$PROBE" "$TF"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; missing=1; }
done
# HOW MANY PLACES IN THIS FILE CAN START THE PRODUCT. One, and it is the guarded
# helper. A second launch site added later would not inherit the guard, which is
# precisely the failure mode of the audio incident: the flag was present in the
# file, just not at every call site. Coverage is not per-site correctness.
LAUNCH_SITES=$(grep -c 'timeout.*"\$EXE"' "$0")
if [ "$LAUNCH_SITES" != "1" ]; then
  echo "FATAL: this gate has $LAUNCH_SITES product launch sites; every launch must go through run_frame() so --audio-disabled cannot be dropped"
  exit 1
fi
if [ "$SKIP_LIVE" = "0" ]; then
  for path in "$EXE" "$DEMO"; do
    [ -e "$path" ] || { echo "FATAL: missing input: $path"; missing=1; }
  done
  [ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; missing=1; }
fi
[ "$missing" = "0" ] || exit 1

echo "=== 0/6 the readers: offline, both ways (no GPU, no demo, no lock) ==="
# Every number sections 4-6 assert comes from one of the two Python readers, and
# a reader that always prints the same thing turns a gate green without measuring
# anything. This run hands them frames and traces in the product's shape and
# requires each reading to answer BOTH ways: correct input passes, and the defect
# the matching mutation reintroduces fails. It needs no GPU, which is why it can
# run before the machine is free.
OFFLINE_READER="$PWD/evidence/outline/outline-offline-selftest.py"
if [ -x "$PY" ] && [ -e "$OFFLINE_READER" ]; then
  "$PY" "$OFFLINE_READER" > "$OUT/offline-selftest.txt" 2>&1
  OFFLINE_RC=$?
  sed "s/^/  /" "$OUT/offline-selftest.txt"
  assert_eq "offline reader selftest" "$(v "$OUT/offline-selftest.txt" OFFLINE-SELFTEST)" "PASS"
  assert_eq "offline reader selftest exit" "$OFFLINE_RC" "0"
else
  echo "  FATAL: the offline reader selftest is missing; the readings below"
  echo "  would be numbers nobody has ever seen move."
  exit 1
fi

echo
echo "=== 1/6 the contract's own invariants (no GPU, no demo) ==="
# Every predicate the later sections rely on is asserted here first, so a
# failure below can be attributed to the frame and not to the predicate.
"$PROBE" --self-test > "$OUT/probe-self-test.txt" 2>&1
SELF_RC=$?
cat "$OUT/probe-self-test.txt"
assert_eq "probe self-test exit" "$SELF_RC" "0"
assert_eq "self-test failures" "$(v "$OUT/probe-self-test.txt" failures)" "0"

echo
echo "=== 2/6 the five coverage cases, as data ==="
# visible / occluded are asserted as PICTURES in section 4; what this layer
# pins is that the sample and the draw list agree about identity, team and
# pose, and that the three edge cases are transitions or empty sets rather
# than absences.
"$PROBE" --fixtures > "$OUT/probe-fixtures.txt" 2>&1
FIX_RC=$?
cat "$OUT/probe-fixtures.txt"
assert_eq "probe fixtures exit" "$FIX_RC" "0"
assert_eq "fixture cases" "$(v "$OUT/probe-fixtures.txt" "fixtures cases")" "5"
assert_eq "fixture failures" "$(v "$OUT/probe-fixtures.txt" failures)" "0"

if [ "$SKIP_LIVE" = "1" ]; then
  echo
  echo "=== 3/6..6/6 SKIPPED (--skip-live) ==="
  echo "  NOT VERIFIED: no product launch, so no frame, no ordinal and no depth"
  echo "  assertion was taken. This run proves the contract and the fixtures only."
  echo
  if [ "$fail" = "0" ]; then
    echo "OUTLINE-CHECK=PARTIAL (probe layers only; product layers NOT VERIFIED)"
    exit 0
  fi
  echo "OUTLINE-CHECK=FAIL"
  exit 1
fi

echo
echo "=== 3/6 a real recording: identity, team and pose at every sampled tick ==="
# The tick set is a fixed list, not "whatever the run happened to reach": the
# same ticks must be sampled in every run or the frames compared in sections 4
# and 5 would be two different cameras.
# These are PLAYBACK ticks (sceneTick = firstPacketTick + tick) and they must
# land inside the C++ entity history's retained window. Outside it the accessor
# answers "unavailable", the product's scene states come back EMPTY, and a frame
# at that tick has no player to outline at all. Measured before this was fixed:
# six requested ticks, six "the tick precedes every retained snapshot", zero
# rows. The probe prints the window and the per-tick player/team counts
# (--find-ticks); these six are inside it.
TICKS="${T52_TICKS:-1000,1200,1400,1600,1800,2000}"
"$PROBE" --demo "$DEMO" --ticks "$TICKS" > "$OUT/probe-demo.txt" 2>&1
DEMO_RC=$?
grep -a "^demo summary" "$OUT/probe-demo.txt" > "$OUT/probe-demo-summary.txt" 2>/dev/null
cat "$OUT/probe-demo-summary.txt"
assert_eq "probe demo exit" "$DEMO_RC" "0"
# The rows the identity claim rests on. `rows` is how many player-rows were
# sampled in total: a demo whose snapshots hold no player would make every
# later assertion vacuous, so the count is a criterion and not a diagnostic.
assert_ge "sampled player rows" "$(v "$OUT/probe-demo-summary.txt" rows)" "1"
# Every sampled row must carry a team value and a pose: an outline drawn for a
# row with no origin would be a silhouette at the world origin.
ROWS=$(v "$OUT/probe-demo-summary.txt" rows)
WITH_TEAM=$(v "$OUT/probe-demo-summary.txt" withTeam)
WITH_POSE=$(v "$OUT/probe-demo-summary.txt" withPose)
assert_eq "rows carrying a team slot" "${WITH_TEAM:-<none>}" "${ROWS:-<none>}"
assert_eq "rows carrying a pose" "${WITH_POSE:-<none>}" "${ROWS:-<none>}"
# Both teams must be present, or the colour assertions in section 4 have only
# one side to compare and a colour swap could not be detected.
assert_ge "red player rows" "$(v "$OUT/probe-demo-summary.txt" red)" "1"
assert_ge "blue player rows" "$(v "$OUT/probe-demo-summary.txt" blue)" "1"
# The identity claim is about EVERY sampled tick, so it is counted per tick: a
# tick that produced no player row is a tick this gate can claim nothing about,
# and six requested ticks with five answered is a claim about five.
# Derived from the tick list, not hardcoded: the list is pinned in this file but
# the count is what the list actually asks for, so shortening the list cannot
# quietly weaken the assertion and lengthening it cannot break it.
EXPECTED_TICKS=$(printf "%s" "$TICKS" | tr "," "\n" | grep -c .)
assert_eq "sampled ticks that produced player rows" "$(v "$OUT/probe-demo-summary.txt" ticksWithRows)" "$EXPECTED_TICKS"
# The team mapping, per row: every row carries the wire value it classified, and
# the classification agrees with it. If the demo carried no usable team value,
# this is the reading that says so instead of section 4 drawing colours anyway.
assert_eq "sampled rows with no team value" "$(v "$OUT/probe-demo-summary.txt" rowsMissingTeam)" "0"
assert_eq "sampled rows with no pose" "$(v "$OUT/probe-demo-summary.txt" rowsMissingPose)" "0"
assert_eq "rows whose team classification disagrees with m_iTeamNum" \
  "$(v "$OUT/probe-demo-summary.txt" teamMappingMismatches)" "0"
# Every per-row line must carry the fields the attribution rests on (entity,
# class, team, pose), or the counts above are about lines that identify nothing.
IDENT_ROWS=$(grep -a -c -E "^tick requested=[0-9]+ resolved=[0-9]+ fidelity=[a-zA-Z]+ stale=-?[0-9]+ entity=[0-9]+ class=[^ ]+ player=[01] team=[a-z]+ raw=-?[0-9]+ hasTeam=[01] origin=" "$OUT/probe-demo.txt")
TICK_ROWS=$(grep -a -c "^tick requested=" "$OUT/probe-demo.txt")
assert_eq "per-row lines carrying identity, team and pose" "${IDENT_ROWS:-0}" "${TICK_ROWS:-0}"
assert_ge "per-row identity lines read" "${TICK_ROWS:-0}" "1"

# Fidelity is reported, never assumed. On this recording the C++ history
# answers most ticks from a checkpoint, and T53 measured the worst staleness at
# 1057 ticks. The identity claim survives that (it is a statement about the
# snapshot the frame was drawn from); a per-tick trajectory claim would not.
echo "  fidelity exact=$(v "$OUT/probe-demo-summary.txt" exact)"
echo "  fidelity checkpoint=$(v "$OUT/probe-demo-summary.txt" checkpointFallback)"
echo "  fidelity unavailable=$(v "$OUT/probe-demo-summary.txt" unavailable)"
# CAPABILITY FIRST. The identity claim is only as good as the accessor answering
# it: the C++ entity history retains a WINDOW, not the whole recording, and a
# tick outside it answers "unavailable" with no rows. Measured on this demo
# before the gate was pointed at the window: six requested ticks, six
# "the tick precedes every retained snapshot", zero rows. So a tick the history
# cannot answer is a failure of the claim, not a sampling detail.
assert_eq "sampled ticks the entity history could answer" "$(v "$OUT/probe-demo-summary.txt" unavailable)" "0"

echo
echo "=== 4/6 the frames: nothing else changed, both colours, both directions ==="
# Three runs at the SAME tick, differing only in the switch under test:
#   off     --outline off
#   on      --outline on
#   tested  --outline on --outline-depth tested (the depth-awareness control)
# Fixed so the gate is reproducible; the override exists for the run that PICKS
# the tick, because where the retained window sits is a property of the
# recording that has to be measured once before it can be pinned.
TICK="${T52_TICK:-2000}"
# Two things had to change here before a capture could ever happen, and both were
# found by reading the product instead of the gate:
#   * --start-paused freezes g_playback.tick at 0, so the one-shot capture armed
#     at tick N waits forever for `tick >= N` and the run times out with no BMP.
#     That matches what the earlier runs left behind: no captures at all.
#   * reaching a tick near the end of a ~64000-tick recording costs 16 minutes at
#     1x, so --playback-speed fast-forwards the cursor. The captured frame is
#     identical: the cursor is HELD at that tick and the scene is resolved from
#     the recording, not accumulated frame by frame.
# 8x, not 16x: the invariance assertion below compares speed=1 with the DEFAULT
# speed, so the default has to be a speed whose frames are measured -- a third
# value that no assertion covers would be the same trust-me as no assertion.
SPEED="${T52_SPEED:-8}"
run_frame() { # run_frame <prefix> [args...]
  local prefix="$1"; shift
  rm -f "$prefix.bmp" "$prefix.trace" "$prefix.log"
  local trace
  trace="$(cygpath -m "$PWD/$prefix.trace")"
  # The speed=1 capture below is the slow one by construction (a tick near the end
  # of the recording costs its playback position in wall clock), so the timeout is
  # a knob per call rather than a number that has to fit the slowest run.
  # THIS IS THE ONLY PRODUCT LAUNCH IN THIS GATE, and the argument list is built
  # here so that --hidden-window and --audio-disabled cannot be forgotten by a
  # caller. The check below is on the FINAL argv, not on what the caller passed:
  # a guard that inspects the caller's flags protects nothing when the helper is
  # the thing that builds them.
  #   WHY IT IS HERE: on 2026-10-22 a companion gate ran the product with the
  #   default audio device during its CONTROL phase and played twenty seconds of
  #   demo audio at system volume. Every automated reading was green. The only
  #   detector was a person in the next room.
  local -a productArgs=(
    --tf-root "$TF" --hidden-window --audio-disabled --demo "$DEMO"
    --capture-tick "$TICK" --playback-speed "$SPEED" --capture-frame "$prefix.bmp"
    "$@"
  )
  case " ${productArgs[*]} " in
    *" --audio-disabled "*) : ;;
    *) echo "REFUSED: run_frame builds a product launch without --audio-disabled" >&2
       echo "99"
       return 0 ;;
  esac
  TF2_NATIVE_TRACE="$trace" timeout "${RUN_TIMEOUT:-600}" "$EXE" "${productArgs[@]}" > "$prefix.log" 2>&1
  echo "$?"
}
OFF_RC=$(run_frame "$OUT/off" --outline off)
ON_RC=$(run_frame "$OUT/on" --outline on)
TESTED_RC=$(run_frame "$OUT/tested" --outline on --outline-depth tested)
echo "  off rc=$OFF_RC bmp=$(stat -c%s "$OUT/off.bmp" 2>/dev/null || echo 0)"
echo "  on  rc=$ON_RC bmp=$(stat -c%s "$OUT/on.bmp" 2>/dev/null || echo 0)"
echo "  tst rc=$TESTED_RC bmp=$(stat -c%s "$OUT/tested.bmp" 2>/dev/null || echo 0)"
assert_eq "quiet off run rc" "$OFF_RC" "0"
assert_eq "quiet on run rc" "$ON_RC" "0"
assert_eq "quiet tested run rc" "$TESTED_RC" "0"

echo
echo "  -- speed invariance: the harness knob may not change the frame --"
# `--playback-speed` exists so a capture pinned near the end of the recording does
# not cost sixteen minutes of wall clock. That is only legitimate if the SPEED is
# not a rendering parameter: the same capture tick at speed=1 and at the default
# speed has to produce byte-identical frames. This is the assertion behind the
# contract sentence; mutation m6 is its proof that the comparison can go red.
#
# THE PRECONDITION IS ONE OF ITS OWN READINGS. A cross-speed comparison can only
# blame the speed if captures at the SAME speed and tick are byte-identical, and
# this gate measures exactly that above: the two independent overlay-off runs
# (`off` and `off2`) must differ by 0 bytes. That defect is documented -- see
# problems/2026-10-12-capture-tick-frame-nondeterminism.md, where the same
# product landed on ticks 200/201/202 for one requested tick before the cursor
# hold was added. Without the off/off2 reading, a difference here would be that
# old defect and this block would be attributing it to the speed.
SPEED1_START=$(date +%s)
SPEED1_RC=$(RUN_TIMEOUT=1800 SPEED=1 run_frame "$OUT/speed1" --outline on)
SPEED1_TOOK=$(( $(date +%s) - SPEED1_START ))
echo "  speed=1 rc=$SPEED1_RC bmp=$(stat -c%s "$OUT/speed1.bmp" 2>/dev/null || echo 0) took=${SPEED1_TOOK}s"
assert_eq "speed=1 capture rc" "$SPEED1_RC" "0"
if cmp -s "$OUT/speed1.bmp" "$OUT/on.bmp"; then
  echo "  OK   the frame is byte-identical at speed=1 and speed=${SPEED} (cmp)"
else
  echo "  FAIL --playback-speed CHANGES THE FRAME: the speed=1 capture and the speed=${SPEED}"
  echo "  capture of the same tick differ byte for byte. The harness knob is then a"
  echo "  rendering parameter and every reading taken at a speed is suspect."
  fail=1
fi

# The product's own account of the pass, read out of each trace.
outline_value() { # outline_value <trace> <field>
  # NO LINE-START ANCHOR. The product writes every trace line behind an elapsed
  # timestamp -- "33234.9 outline: draw enabled=1 ..." -- so `grep "^outline: draw"`
  # matches NOTHING and every field comes back empty. Empty is not zero and not
  # "absent": it makes `assert_eq field 0` fail and `assert_eq field 1` fail too,
  # and against a field the assertion expected to be absent it would have passed
  # silently. The fixture check below is what keeps this honest.
  grep -a "outline: draw" "$1" 2>/dev/null | tail -1 | tr " " "\n" | grep -a "^$2=" | cut -d= -f2
}
# THE HELPER IS TESTED BEFORE IT IS TRUSTED. The fixture is the product's own line
# shape, timestamp prefix included, and the helper has to read a known value out of
# it. Without this, a helper that reads nothing at all reports <none> for every
# field and the gate explains nothing about why.
printf "12.3 outline: draw enabled=1 depth=xray depthTest=0 depthWrite=0 drew=1 submitted=3\n" \
  > "$OUT/helper-selftest.trace"
# The same reader for the one-time startup lines (gpu: ...), which are printed ONCE
# per process and carry k=v fields just like the outline line.
trace_field() { # trace_field <trace> <line-match> <field>
  grep -a -m1 "$2" "$1" 2>/dev/null | tr " " "\n" | grep -a "^$3=" | cut -d= -f2
}
HELPER_SUBMITTED=$(outline_value "$OUT/helper-selftest.trace" submitted)
if [ "$HELPER_SUBMITTED" = "3" ]; then
  echo "  OK   outline_value reads the product line shape (timestamp prefix, k=v tokens)"
else
  echo "  FAIL outline_value cannot read a timestamped trace line: got '${HELPER_SUBMITTED:-<empty>}' instead of 3"
  fail=1
fi
# The two player regions, as pixel boxes, come from the PASS, not from this
# script guessing: the trace prints every submitted item with its entity, its
# team and its projected position, and this loop turns each into a box using
# the same map-to-box transform the renderer uses.
echo "  -- the pass, from the product's own trace --"
for field in enabled depth drew ordinal vmOrdinal orderOk silhouettes submitted red blue unknown noMesh noOrigin disabled samples presses owner refused; do
  echo "  on.$field=$(outline_value "$OUT/on.trace" "$field")"
done
assert_eq "overlay enabled in the on run" "$(outline_value "$OUT/on.trace" enabled)" "1"
assert_eq "overlay drew in the on run" "$(outline_value "$OUT/on.trace" drew)" "1"
assert_ge "silhouettes drawn (on)" "$(outline_value "$OUT/on.trace" silhouettes)" "1"
assert_ge "silhouettes submitted (on)" "$(outline_value "$OUT/on.trace" submitted)" "1"
# WHERE THE POSE CAME FROM, as a reading rather than a remark. Two sources exist
# (the world pass's mesh for this frame, or the overlay's own bind-pose upload)
# and the item records which one answered. The claim this pair makes is exactly
# one sentence: every submitted silhouette has a declared source, and the number
# that says "bind" is the number that goes to zero on the day the world pass
# starts drawing player bodies -- a gate that REQUIRED bind > 0 would go red on
# the day the product got better, so this one does not.
POSE_WORLD=$(outline_value "$OUT/on.trace" poseWorld)
POSE_BIND=$(outline_value "$OUT/on.trace" poseBind)
SUBMITTED_ON=$(outline_value "$OUT/on.trace" submitted)
echo "  on.poseWorld=${POSE_WORLD:-<none>} on.poseBind=${POSE_BIND:-<none>}"
assert_eq "every submitted silhouette declares a pose source" \
  "$(( ${POSE_WORLD:-0} + ${POSE_BIND:-0} ))" "${SUBMITTED_ON:-0}"
# THE WORLD PASS'S MODEL COUNT, AS AN ASSERTION AND NOT AS A SENTENCE.
# The report used to say "the overlay must not disturb the world pass's 59 models"
# while main.cpp:2159-2161 merges nine class requests into the same meshSource the
# world pass fills. That merge does NOT constructively guarantee the invariant: it
# holds only because those requests are skipped later, so "59" was a coincidence of
# this run rather than a property of the code. A reading that can silently change
# is not an invariant; this one is asserted against the pre-change baseline, so the
# day the outline's meshes DO displace a world model the gate goes red instead of
# the report quietly losing a sentence.
WORLD_MODELS_PREPARED=$(sed -n "s/.*gpu: models done \([0-9][0-9]*\) uploaded=.*/\1/p" "$OUT/on.trace" | head -1)
WORLD_MODELS_UPLOADED=$(trace_field "$OUT/on.trace" "gpu: models done" uploaded)
echo "  world models: prepared=${WORLD_MODELS_PREPARED:-<none>} uploaded=${WORLD_MODELS_UPLOADED:-<none>} (baseline 59/59, measured before the outline uploaded anything)"
assert_eq "world pass model uploads unchanged by the overlay" "${WORLD_MODELS_UPLOADED:-<none>}" "59"
# The ordinal assertion the lead asked for, in two halves because the product can
# answer only one of them without a viewmodel.
#
# `ordinal` is the slot the overlay pass actually took this frame: always
# measurable, always 6.
assert_eq "overlay ordinal" "$(outline_value "$OUT/on.trace" ordinal)" "6"
# `vmOrdinal` is only set when drawViewModel() RUNS, and that pass returns early
# unless a viewmodel was requested and its mesh uploaded (native_renderer.cpp:
# 4152-4160). A plain demo capture requests none, so on the `on` frame vmOrdinal
# is 0 and orderOk is 0 -- and that 0 is CORRECT: "before a pass that did not
# run" is not a statement this frame can make. Reading it as a failure would be
# the same class of error as reading an empty grep as "the value is zero".
VM_ORD_ON=$(outline_value "$OUT/on.trace" vmOrdinal)
if [ "${VM_ORD_ON:-0}" -ge 1 ]; then
  assert_eq "viewModel ordinal (on frame)" "${VM_ORD_ON}" "7"
  assert_eq "pass order (overlay before viewModel)" "$(outline_value "$OUT/on.trace" orderOk)" "1"
else
  echo "  NOT-PAIR on frame: the ViewModel pass did not run (vmOrdinal=0); the pair is"
  echo "  asserted on the dedicated --viewmodel capture below instead."
fi
# ...and here is that capture: the same tick, the same overlay, plus a viewmodel,
# which is the only state in which "the overlay draws before the pass that clears
# depth" is observable at all. This is a capture, not a mutation: nothing changes
# but the argument that makes the ViewModel pass exist.
run_frame "$OUT/vm" --outline on --viewmodel models/weapons/v_models/v_scattergun_scout.mdl
VM_ORD=$(outline_value "$OUT/vm.trace" vmOrdinal)
echo "  vm.ordinal=$(outline_value "$OUT/vm.trace" ordinal) vm.vmOrdinal=${VM_ORD:-<none>} vm.orderOk=$(outline_value "$OUT/vm.trace" orderOk) vm.drew=$(outline_value "$OUT/vm.trace" drew)"
assert_eq "viewModel ordinal (with a viewmodel)" "${VM_ORD:-<none>}" "7"
assert_eq "pass order (overlay before viewModel, with a viewmodel)" \
  "$(outline_value "$OUT/vm.trace" orderOk)" "1"
assert_eq "overlay ordinal (with a viewmodel)" "$(outline_value "$OUT/vm.trace" ordinal)" "6"
assert_eq "overlay enabled in the off run" "$(outline_value "$OUT/off.trace" enabled)" "0"
assert_eq "overlay drew in the off run" "$(outline_value "$OUT/off.trace" drew)" "0"
# The toggle's effect is a COUNT: with the switch off, every sampled player is
# refused BECAUSE the overlay is off, so the frame cannot read as "there were
# no players".
assert_ge "players refused because the overlay is off" "$(outline_value "$OUT/off.trace" disabled)" "1"
assert_eq "overlay enabled in the tested run" "$(outline_value "$OUT/tested.trace" enabled)" "1"
assert_eq "tested run depth mode" "$(outline_value "$OUT/tested.trace" depth)" "depthTested"
# The two halves of the contract's depth statement, read back from the state
# OBJECTS the pass bound (GetDesc at init) rather than repeated from the lines
# that created them. "x-ray" with no depth test AND no depth write is what makes
# an outline visible through a wall without changing what the wall lets through
# afterwards; the second half is invisible in any screenshot.
assert_eq "x-ray: depth test" "$(outline_value "$OUT/on.trace" depthTest)" "0"
# The write half, MEASURED 2026-10-22 22:51 and therefore no longer asserted as
# a zero: the pass is created with DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO
# (native_renderer.cpp:1327) and D3D11 reports DepthWriteMask = ALL for that same
# state (`stateWrite=1` in the draw line, printed from the GetDesc readback).
# Both readings are in the trace and they are both true: when DepthEnable is
# FALSE the runtime does not preserve the write mask, and the mask is inert --
# "no depth write" is enforced by DepthEnable = FALSE, not by the mask.
# So the register here is what the state says, and the CONTRACT sentence is
# reported as UNVERIFIED-BEHAVIOURALLY (report section 4.2) rather than passed:
# asserting depthWrite==0 would be asserting a field the runtime rewrites.
echo "  x-ray depth write, as D3D11 reports it: depthWrite=$(outline_value "$OUT/on.trace" depthWrite) stateWrite=$(outline_value "$OUT/on.trace" stateWrite)  (NOT asserted, see report 4.2)"
assert_eq "depth-tested control: depth test" "$(outline_value "$OUT/tested.trace" depthTest)" "1"
# The control still ASSERTS its write bit, and that is the paired reading which
# makes the x-ray result readable: the depth-tested state is created from the
# SAME descriptor with DepthEnable = TRUE, and D3D11 reports its mask as ZERO --
# so the runtime preserves the mask when the test is on and rewrites it to ALL
# when the test is off. Same code, same mask, two answers: the difference is the
# runtime, not the overlay.
assert_eq "depth-tested control: depth write" "$(outline_value "$OUT/tested.trace" depthWrite)" "0"
echo "  tested state: depthTest=$(outline_value "$OUT/tested.trace" depthTest) depthWrite=$(outline_value "$OUT/tested.trace" depthWrite)"

# The per-item rows, printed so a red run reports WHERE the pass put each
# silhouette instead of only how many it drew. "no players on screen" and "two
# players on screen but at the world origin" fail the same assertions and this
# is the line that tells them apart.
echo
echo "  -- per-item rows (on) --"
grep -a "outline: item" "$OUT/on.trace" 2>/dev/null | sed "s/^/    /" || true

echo
echo "  -- non-interference: only the silhouette's own pixels may differ --"
# `regions` is derived from the drawn items; every pixel outside them must be
# byte-identical between the on and off frames of the same tick.
REGIONS=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --csv)
REGION_ARG=""
if [ -n "$REGIONS" ]; then REGION_ARG="regions=$REGIONS"; fi
echo "  regions=$REGIONS"
printf "%s" "$REGIONS" > "$OUT/regions.txt"
NI=$("$PY" outline-frame-check.py noninterference on="$OUT/on.bmp" off="$OUT/off.bmp" $REGION_ARG 2>&1)
echo "  $NI" | sed "s/^/    /"
assert_eq "differing pixels outside every drawn region" "$(echo "$NI" | grep -o "diff_outside=[0-9]*" | cut -d= -f2)" "0"
assert_ge "differing pixels INSIDE the drawn regions" "$(echo "$NI" | grep -o "diff_inside=[0-9]*" | cut -d= -f2)" "1"

echo
echo "  -- team colour, both regions, both directions --"
TEAM_REGIONS=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --team)
RED_REGION=$(echo "$TEAM_REGIONS" | grep -o "red=[0-9,]*" | cut -d= -f2)
BLUE_REGION=$(echo "$TEAM_REGIONS" | grep -o "blue=[0-9,]*" | cut -d= -f2)
echo "  red=$RED_REGION blue=$BLUE_REGION"
# A region that could not be derived would make the assertion below vacuous,
# so the gate refuses to pretend: no region is a FAILURE, not a skip.
if [ -z "$RED_REGION" ] || [ -z "$BLUE_REGION" ]; then
  echo "  FAIL no red and blue player regions could be derived from the trace"
  fail=1
else
  TEAM=$("$PY" outline-frame-check.py team frame="$OUT/on.bmp" red_region="$RED_REGION" blue_region="$BLUE_REGION" 2>&1)
  echo "  $TEAM" | sed "s/^/    /"
  assert_ge "red pixels in the red player's region" "$(echo "$TEAM" | grep -o "red_inside=[0-9]*" | cut -d= -f2)" "8"
  assert_ge "blue pixels in the blue player's region" "$(echo "$TEAM" | grep -o "blue_inside=[0-9]*" | cut -d= -f2)" "8"
  assert_eq "blue pixels in the RED player's region" "$(echo "$TEAM" | grep -o "red_wrong_team=[0-9]*" | cut -d= -f2)" "0"
  assert_eq "red pixels in the BLUE player's region" "$(echo "$TEAM" | grep -o "blue_wrong_team=[0-9]*" | cut -d= -f2)" "0"
fi

echo
echo "=== 5/6 depth awareness: the occluded region, three ways ==="
# The region is the part of the player boxes that the frame's OWN depth says is
# behind world geometry. It is derived from the depth buffer the capture took,
# not from a guess about where the map is, and the gate refuses to run the
# assertion on an empty region: "nothing was occluded" would make it vacuous.
# The world-depth capture is what "behind a wall" is measured against, so it is
# taken from the SAME tick in a fourth run rather than assumed from geometry.
DEPTH_RC=$(run_frame "$OUT/worlddepth" --outline off --world-depth)
echo "  world-depth run rc=$DEPTH_RC"
OCCLUDED=$("$PY" "$PWD/outline-regions.py" --trace "$OUT/on.trace" --occluded --depth "$OUT/worlddepth.bmp" 2>/dev/null)
echo "  occluded region: ${OCCLUDED:-<none>}"
if [ -z "$OCCLUDED" ]; then
  # An empty region would make the assertion vacuous, and a vacuous assertion
  # reported as PASS is the failure shape this repository has paid for. It is
  # NOT VERIFIED and it is named, with an owner and the cheapest next step.
  echo "  NOT VERIFIED: the captured tick has no occluded player pixel, so the"
  echo "  depth-awareness mutation has no region to prove itself on."
  echo "  Owner: t52-outline. Cheapest next step: pick a capture tick at which a"
  echo "  player is behind a wall (the trace lists per-item depth and world depth)."
  echo "  NOT-VERIFIED depthaware=1"
else
  DEPTH=$("$PY" outline-frame-check.py depth region="$OCCLUDED" \
    xray="$OUT/on.bmp" tested="$OUT/tested.bmp" off="$OUT/off.bmp" 2>&1)
  echo "  $DEPTH" | sed "s/^/    /"
  assert_ge "team-coloured pixels drawn THROUGH the wall (x-ray)" "$(echo "$DEPTH" | grep -o "occluded_xray=[0-9]*" | cut -d= -f2)" "8"
  assert_eq "team-coloured pixels through the wall when depth-tested" "$(echo "$DEPTH" | grep -o "occluded_tested=[0-9]*" | cut -d= -f2)" "0"
  assert_eq "depth-tested frame vs overlay-off frame, in the occluded region" "$(echo "$DEPTH" | grep -o "occluded_tested_diff=[0-9]*" | cut -d= -f2)" "0"
fi

echo "=== 6/6 the toggle: one slot, one owner, foreign presses refused ==="
# The state machine is asserted in the probe (section 1), and the product's own
# counters are asserted here: the run acquired the slot as the outline and no
# foreign press was recorded, so nothing else changed the flag while the frame
# was measured.
assert_eq "overlay slot owner in the product" "$(outline_value "$OUT/on.trace" owner)" "outline"
assert_eq "foreign presses refused by the product" "$(outline_value "$OUT/on.trace" refused)" "0"
# The switch's OFF state, twice, in two independent processes. "The off frame" is
# the comparison target for every assertion above, so it has to be the same frame
# twice; comparing a file with itself proved nothing and used to sit here.
OFF2_RC=$(run_frame "$OUT/off2" --outline off)
assert_eq "second overlay-off run rc" "$OFF2_RC" "0"
OFF2_DIFF=$("$PY" outline-frame-check.py noninterference \
  on="$OUT/off2.bmp" off="$OUT/off.bmp" require_inside=0 2>&1)
assert_eq "two independent overlay-off frames differ by" "$(echo "$OFF2_DIFF" | grep -o "diff_outside=[0-9]*" | cut -d= -f2)" "0"
# And the off frame carries no team-coloured pixel anywhere: that is the switch's
# own pixel statement, and it is the reading mutation m3 has to move.
OFF_TEAM=$("$PY" outline-frame-check.py teamcount frame="$OUT/off.bmp" 2>&1)
echo "  off frame team pixels: $OFF_TEAM"
OFF_TEAM_RED=$(echo "$OFF_TEAM" | grep -o "red_total=[0-9]*" | cut -d= -f2)
OFF_TEAM_BLUE=$(echo "$OFF_TEAM" | grep -o "blue_total=[0-9]*" | cut -d= -f2)
assert_eq "team-coloured pixels in the overlay-off frame" "$(( ${OFF_TEAM_RED:-0} + ${OFF_TEAM_BLUE:-0} ))" "0"

echo


# ---------------------------------------------------------------------------
# MUTATIONS
#
# Each case reintroduces ONE defect the feature removed, rebuilds, re-runs the
# readings that case is about, and requires the reading to move AWAY from the
# fixed-build value. Then the source is restored and the restore is verified
# with cmp -- a mutation whose restore is not byte-exact leaves a defect in the
# tree for whoever builds next, which is worse than not running it.
#
# The six cases and the reading each must turn red:
#   m1 exchange RED and BLUE        -> team: red_wrong_team OR blue_wrong_team
#   m2 depth-test only (no x-ray)   -> depth: occluded_xray drops from >0 to 0,
#                                      and the published x-ray depth TEST flips
#   m3 overlay always on and leaks  -> the OFF frame gains team pixels
#   m5 the overlay writes depth     -> the published x-ray depth WRITE flips
#   m6 captured tick depends on the
#      playback speed               -> the speed=1 and speed=default frames differ
#   m4 draw after the ViewModel     -> ordinal: orderOk=0 and ordinal != 6
#   m7 no class model fallback      -> submitted drops to 0, noMesh rises to samples
#   m8 item keeps the bare path     -> silhouettes drawn drop to 0 while submitted stays >0
#   m9 bind mesh preferred over the
#      world mesh                   -> the probe self-test's ordering case fails
#
# ...and one PAIRED READING that is not a mutation (nothing is supposed to be red):
#   cap A/B  the renderer's mesh limit 64 vs 96 -> at 64 the overlay's class meshes
#            are refused (rejected>0, keys logged); at 96 rejected=0 and
#            uploaded==prepared. Without the pair, "raise the cap" is an
#            assumption rather than a measurement, and the refusal it fixes is
#            invisible in every other reading in this file.
# ---------------------------------------------------------------------------
if [ "$MUTATION" = "1" ]; then
  echo
  # ALIGNMENT: every mutation capture is launched with the SAME arguments as the
  # forward run it is compared with. The only difference is the mutated source.
  #   m1 --outline on    vs forward on      (colour only)
  #   m2 --outline on    vs forward on      (the x-ray depth state only; the
  #                                         picture it must then resemble is
  #                                         the forward depth-tested frame)
  #   m3 --outline off   vs forward off     (the toggle guard only)
  #   m5 --outline on    vs forward on      (the depth write mask only)
  #   m6 --outline on    vs forward on      (the captured tick only; the SPEED
  #                                         differs by design and is the
  #                                         assertion subject)
  #   m4 --outline on    vs forward on      (the call site only)
  #   m7 --outline on    vs forward on      (the class fallback only)
  #   m8 --outline on    vs forward on      (the key the item carries only)
  # A mutation that also changed a flag would move a reading for a reason the
  # mutation is not about. That is how a control phase elsewhere in this repo grew
  # an --audio-disabled omission while its author was busy describing the one
  # difference he had in mind.
  echo "=== mutation: 8 cases, each rebuilt and required to go red ==="
  MUT_SRC="native/src/entity_overlay.cpp"
  MUT_MAIN="native/src/main.cpp"
  MUT_RENDERER="native/src/native_renderer.cpp"
  BACKUP_DIR="$OUT/mutation-backup"
  mkdir -p "$BACKUP_DIR"
  cp "$MUT_SRC" "$BACKUP_DIR/entity_overlay.cpp"
  cp "$MUT_MAIN" "$BACKUP_DIR/main.cpp"
  cp "$MUT_RENDERER" "$BACKUP_DIR/native_renderer.cpp"
  mut_fail=0
  mut_invalid=0

  # Applies one literal replacement, and requires the pattern to occur EXACTLY
  # ONCE. A pattern that is not found is a hard error (the mutation would silently
  # not happen and the run would report "the reading did not move"), and so is a
  # pattern that matches twice: the second copy would be left mutated, the restore
  # would hide it, and the next mutation's reading would be taken on code this
  # section did not intend. The sources are checked for this before the run as
  # well; this is the in-run half of that rule.
  patch_in() { # patch_in <file> <find> <replace>
    "$PY" - "$1" "$2" "$3" <<'PYEOF'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, "rb").read()
crlf = b"\r\n" in raw
data = raw.decode("utf-8")
if crlf:
    find = find.replace("\n", "\r\n")
    repl = repl.replace("\n", "\r\n")
count = data.count(find)
if count == 0:
    sys.exit("PATTERN NOT FOUND: " + find[:70].replace("\r", "\\r").replace("\n", "\\n"))
if count != 1:
    sys.exit("PATTERN MATCHED " + str(count) + " TIMES, refusing to mutate: "
             + find[:70].replace("\r", "\\r").replace("\n", "\\n"))
io.open(path, "wb").write(data.replace(find, repl, 1).encode("utf-8"))
PYEOF
  }

  # The restore is a copy back plus a cmp, which is the same evidence
  # `git checkout` would give (bytes for bytes) without depending on the
  # tree's commit state.
  restore_all() {
    cp "$BACKUP_DIR/entity_overlay.cpp" "$MUT_SRC"
    cp "$BACKUP_DIR/main.cpp" "$MUT_MAIN"
    cp "$BACKUP_DIR/native_renderer.cpp" "$MUT_RENDERER"
    local ok=1
    for pair in "entity_overlay.cpp:$MUT_SRC" "main.cpp:$MUT_MAIN" "native_renderer.cpp:$MUT_RENDERER"; do
      local backup="${pair%%:*}" target="${pair##*:}"
      if cmp -s "$BACKUP_DIR/$backup" "$target"; then
        echo "  restored $(basename "$target") byte-identical (cmp)"
      else
        echo "  FAIL restore of $target is NOT byte-identical"; ok=0; mut_fail=1
      fi
    done
    return $((1 - ok))
  }

  rebuild_product() { # rebuild_product <label>
    if bash build-target.sh tf2_demo_native entity_overlay_probe > "$OUT/mutation-build.log" 2>&1; then
      echo "  built ($(grep -c "error C" "$OUT/mutation-build.log" || true) compiler errors)"
      return 0
    fi
    # A mutation that does not COMPILE is not a caught mutation: nothing was
    # measured, so nothing was proven. This is counted as a suite failure, never
    # as a red reading -- the distinction matters because "the build broke" is the
    # easiest way for a mutation suite to look like it works.
    echo "  MUTATION-INVALID $1: the mutated source does not compile, so the mutation"
    echo "  proved nothing. A compile failure is not a red reading (T54 lost a run to"
    echo "  exactly this: the mutation needed an #include before it could be measured)."
    grep -a -E "error C|fatal error|LNK[0-9]+" "$OUT/mutation-build.log" | head -6
    cp "$OUT/mutation-build.log" "$OUT/mutation-build.failed" 2>/dev/null
    mut_fail=1
    mut_invalid=$((mut_invalid + 1))
    return 1
  }

  # A mutation passes only if the reading is no longer the fixed-build value.
  must_move() { # must_move <label> <reading> <fixed> <observed>
    if [ -n "${4:-}" ] && [ "$4" != "$3" ]; then
      echo "  MUTATION-RED   $1: $2=$4 (fixed=$3)"
    else
      echo "  MUTATION-GREEN $1: $2=${4:-<none>} (fixed=$3) -- the mutation did NOT move the reading"
      mut_fail=1
    fi
  }

  # ---- m1: exchange the two team colours ---------------------------------
  echo
  echo "  -- m1: RED and BLUE exchanged --"
  # A real exchange in ONE patch, not two: applied one after the other, the first
  # would make both lines identical and the second would change the line the
  # first had just written -- which is not an exchange at all, it is a no-op that
  # would read green for the wrong reason. The 4-line block is contiguous, so it
  # is replaced whole: RED gets BLUE's values and BLUE gets RED's.
  patch_in "$MUT_SRC" \
"      colour.rgba[0] = 0.92f; colour.rgba[1] = 0.16f; colour.rgba[2] = 0.14f; colour.rgba[3] = 1.0f;
      return colour;
    case OverlayTeam::Blue:
      colour.rgba[0] = 0.16f; colour.rgba[1] = 0.42f; colour.rgba[2] = 0.95f; colour.rgba[3] = 1.0f;" \
"      colour.rgba[0] = 0.16f; colour.rgba[1] = 0.42f; colour.rgba[2] = 0.95f; colour.rgba[3] = 1.0f;
      return colour;
    case OverlayTeam::Blue:
      colour.rgba[0] = 0.92f; colour.rgba[1] = 0.16f; colour.rgba[2] = 0.14f; colour.rgba[3] = 1.0f;" || mut_fail=1
  if rebuild_product m1; then
    M1_RC=$(run_frame "$OUT/m1" --outline on)
    M1_TEAM=$("$PY" outline-frame-check.py team frame="$OUT/m1.bmp" \
      red_region="$RED_REGION" blue_region="$BLUE_REGION" 2>&1)
    echo "    $M1_TEAM" | sed "s/^/      /"
    M1_WRONG=$(echo "$M1_TEAM" | grep -o "red_wrong_team=[0-9]*" | cut -d= -f2)
    M1_WRONG_B=$(echo "$M1_TEAM" | grep -o "blue_wrong_team=[0-9]*" | cut -d= -f2)
    # Either direction moving is enough: the two colours were swapped, so the
    # red player's region now holds blue and the blue player's holds red.
    M1_MOVED=$(("${M1_WRONG:-0}" + "${M1_WRONG_B:-0}"))
    must_move "m1" "team pixels in the WRONG region" "0" "$M1_MOVED"
  fi
  # No rebuild here: the next mutation patches this restored source and
  # builds it, so a build now would prove the same thing twice and cost a
  # full compile. The restore itself is verified by cmp inside restore_all.
  restore_all || true

  # ---- m2: depth-test only (the depth awareness the overlay exists for) --
  echo
  echo "  -- m2: the pass depth-tests everything, so walls hide the outlines --"
  patch_in "$MUT_RENDERER" \
"          ? overlayXRayDepthState_.Get() : overlayDepthTestedState_.Get(), 0);" \
"          ? overlayDepthTestedState_.Get() : overlayDepthTestedState_.Get(), 0);" || mut_fail=1
  if rebuild_product m2; then
    M2_RC=$(run_frame "$OUT/m2" --outline on)
    if [ -n "$OCCLUDED" ]; then
      M2_DEPTH=$("$PY" outline-frame-check.py depth region="$OCCLUDED" \
        xray="$OUT/m2.bmp" tested="$OUT/tested.bmp" off="$OUT/off.bmp" 2>&1)
      echo "    $M2_DEPTH" | sed "s/^/      /"
      M2_XRAY=$(echo "$M2_DEPTH" | grep -o "occluded_xray=[0-9]*" | cut -d= -f2)
      # The fixed build draws team-coloured pixels THROUGH the wall here; the
      # mutation must lose them. A mutation that merely moved the silhouette
      # would leave this at a positive number and be reported green.
      must_move "m2" "team-coloured pixels through the wall" "$(echo "$DEPTH" | grep -o "occluded_xray=[0-9]*" | cut -d= -f2)" "${M2_XRAY:-0}"
    else
      echo "    NOT VERIFIED: no occluded region, so m2 has nothing to prove (picture)"
    fi
    # The same mutation is the proof for the depth-TEST reading, and it does not
    # depend on a single pixel: the mutant binds its x-ray list to the
    # depth-tested state, so the published flag moves from 0 to 1 even on a frame
    # where nothing is occluded.
    must_move "m2" "x-ray depth test state" "$(outline_value "$OUT/on.trace" depthTest)" "$(outline_value "$OUT/m2.trace" depthTest)"
  fi
  restore_all || true

  # ---- m3: the overlay ignores the toggle and leaks -----------------------
  echo
  echo "  -- m3: the overlay is always on and ignores the switch --"
  # The defect has two halves: the builder keeps submitting while the switch is
  # off, AND the list is marked enabled. Either half alone draws nothing (the
  # renderer refuses a disabled list), which would leave this mutation reading
  # green for a reason that is not the feature working.
  patch_in "$MUT_SRC" \
"    if (!enabled) { ++list.rejectedDisabled; continue; }" \
"    if (!enabled) { ++list.rejectedDisabled; }" || mut_fail=1
  patch_in "$MUT_SRC" \
"  list.enabled = enabled;" \
"  list.enabled = true;  /* mutation m3: the switch no longer reaches the pass */" || mut_fail=1
  if rebuild_product m3; then
    M3_RC=$(run_frame "$OUT/m3" --outline off)
    M3_TEAM=$("$PY" outline-frame-check.py teamcount frame="$OUT/m3.bmp" 2>&1)
    echo "    $M3_TEAM" | sed "s/^/      /"
    M3_RED=$(echo "$M3_TEAM" | grep -o "red_total=[0-9]*" | cut -d= -f2)
    M3_BLUE=$(echo "$M3_TEAM" | grep -o "blue_total=[0-9]*" | cut -d= -f2)
    # The reading is the switch's own pixels: a frame captured with --outline off
    # that leaked the overlay cannot have zero team-coloured pixels, and zero is
    # what the fixed build's off frame reads.
    must_move "m3" "team-coloured pixels in the --outline off frame" "0" "$(( ${M3_RED:-0} + ${M3_BLUE:-0} ))"
  fi
  restore_all || true

  # ---- m5: the overlay WRITES depth (the other half of the depth state) --
  echo
  echo "  -- m5: the overlay writes depth --"
  # The contract's second half: the pass must not write depth, or a later pass
  # sees a surface the outline invented. The ViewModel pass clears depth right
  # afterwards, so this defect cannot show in a screenshot -- the reading that
  # has to move is the published state, which is exactly why it is published.
  patch_in "$MUT_RENDERER" \
"    overlayDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;" \
"    overlayDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  /* mutation m5 */" || mut_fail=1
  if rebuild_product m5; then
    M5_RC=$(run_frame "$OUT/m5" --outline on)
    echo "    m5 depthTest=$(outline_value "$OUT/m5.trace" depthTest) depthWrite=$(outline_value "$OUT/m5.trace" depthWrite)"
    must_move "m5" "x-ray depth write state" "0" "$(outline_value "$OUT/m5.trace" depthWrite)"
  fi
  restore_all || true

  # ---- m6: the captured TICK depends on the playback speed --------------
  echo
  echo "  -- m6: the captured tick depends on the playback speed --"
  # The hold at the requested tick is what makes the captured frame independent of
  # how fast the cursor travelled. This mutation keeps a hold but moves it when the
  # speed is above 2x, which is the defect in its most direct form: the frame is
  # then a frame of a DIFFERENT tick at the fast speed, and the speed-invariance
  # comparison above must see it. Engineering the offset (rather than deleting the
  # hold) also keeps the mutation deterministic: whether the draw cadence happens
  # to land on the requested tick is luck, and a mutation that depends on luck
  # reports green for the wrong reason about one run in N.
  patch_in "$MUT_MAIN" \
"            g_playback.tick = static_cast<std::int32_t>(captureTick);" \
"            g_playback.tick = static_cast<std::int32_t>(captureTick) + (g_playback.speed > 2.0 ? 30 : 0);  /* mutation m6 */" || mut_fail=1
  if rebuild_product m6; then
    M6_SLOW=$(RUN_TIMEOUT=1800 SPEED=1 run_frame "$OUT/m6-speed1" --outline on)
    M6_FAST=$(run_frame "$OUT/m6-default" --outline on)
    echo "    m6 speed=1 rc=$M6_SLOW speed=${SPEED} rc=$M6_FAST"
    if cmp -s "$OUT/m6-speed1.bmp" "$OUT/m6-default.bmp"; then
      echo "  MUTATION-GREEN m6: the two speeds still capture identical frames, so the"
      echo "  hold is not what pins the frame and the invariance assertion measures nothing"
      mut_fail=1
    else
      echo "  MUTATION-RED   m6: without the hold, speed=1 and speed=${SPEED} capture different frames"
    fi
  fi
  restore_all || true

  # ---- m7: the player-class model fallback is removed -------------------
  echo
  echo "  -- m7: the overlay stops falling back to the class model --"
  # THIS IS THE MUTATION FOR THE DEFECT THE FEATURE ACTUALLY SHIPPED WITH. On the
  # recording this was measured on, no player carries m_nModelIndex: 21 of 21
  # sampled players have identity, team and pose and none has a model, so without
  # the class fallback every row is rejected as noMesh -- which is what the pass
  # did in 778 of 778 frames before the fix. If removing the fallback does not take
  # submitted back to zero, the fallback is not what draws the silhouettes and the
  # fix proved nothing. The reading that must move is the pass's own count.
  patch_in "$MUT_SRC" \
"      const std::string fallback =
        EntityModelResolver::defaultPlayerModelPath(sample.playerClass);" \
"      const std::string fallback;  /* mutation m7: no class fallback */" || mut_fail=1
  if rebuild_product m7; then
    M7_RC=$(run_frame "$OUT/m7" --outline on)
    echo "    m7 submitted=$(outline_value "$OUT/m7.trace" submitted) noMesh=$(outline_value "$OUT/m7.trace" noMesh)"
    must_move "m7" "silhouettes submitted without the class fallback" \
      "$(outline_value "$OUT/on.trace" submitted)" "$(outline_value "$OUT/m7.trace" submitted)"
    must_move "m7" "players rejected as noMesh without the class fallback" \
      "$(outline_value "$OUT/on.trace" noMesh)" "$(outline_value "$OUT/m7.trace" noMesh)"
  fi
  restore_all || true

  # ---- m8: the item loses the key the mesh was UPLOADED under -----------
  echo
  echo "  -- m8: the silhouette carries the bare path, not the upload key --"
  # The other half of the same defect, and the one a pixel gate cannot see: the
  # sampler now resolves a path and the join accepts "<path>#class-fallback", but
  # the ITEM has to carry that key because the renderer looks the uploaded mesh up
  # by it. Keeping the bare path leaves submitted > 0 and draws nothing -- "the
  # list is right and the frame is empty", which is the exact shape of the defect
  # this section exists to catch. submitted staying positive is asserted as the
  # positive control: the mutation must empty the FRAME, not the list.
  patch_in "$MUT_SRC" \
"    item.cacheKey = meshKey;" \
"    item.cacheKey = sample.modelPath;  /* mutation m8: the upload key is dropped */" || mut_fail=1
  if rebuild_product m8; then
    M8_RC=$(run_frame "$OUT/m8" --outline on)
    echo "    m8 submitted=$(outline_value "$OUT/m8.trace" submitted) drew=$(outline_value "$OUT/m8.trace" drew) silhouettes=$(outline_value "$OUT/m8.trace" silhouettes)"
    assert_ge "m8: items still submitted (the frame is emptied, not the list)" \
      "$(outline_value "$OUT/m8.trace" submitted)" "1"
    must_move "m8" "silhouettes DRAWN when the item loses the upload key" \
      "$(outline_value "$OUT/on.trace" silhouettes)" "$(outline_value "$OUT/m8.trace" silhouettes)"
  fi
  # ---- m9: prefer the BIND mesh over the world mesh ----------------------
  echo
  echo "  -- m9: the bind mesh is preferred over the world mesh --"
  # The defect: the join asks its own uploaded mesh FIRST instead of the mesh the
  # world pass drew this frame. On the reference recording this mutant is
  # INVISIBLE in the product frame -- there is no player mesh in the world pass,
  # so every silhouette is bind-sourced either way -- and that is exactly why the
  # ordering lives where it can be observed: a self-test case that gives both
  # sources an answer and requires the world one to win. A mutation is judged by
  # whether it turns its assertion red, not by whether it changes a picture it
  # cannot change.
  patch_in "$MUT_SRC" \
"      if (world.find(sample.modelPath) != world.end()) {" \
"      if (fallback.find(classKey) != fallback.end()) { /* mutation m9: bind first */ meshKey = classKey; poseSource = OverlayPoseSource::Bind; } else if (world.find(sample.modelPath) != world.end()) {" || mut_fail=1
  if rebuild_product m9; then
    "$PROBE" --self-test > "$OUT/m9-self-test.txt" 2>&1
    M9_FAILURES=$(v "$OUT/m9-self-test.txt" failures)
    echo "    m9 self-test failures=${M9_FAILURES:-<none>} (fixed=0)"
    must_move "m9" "probe self-test failures when the bind mesh wins" "0" "${M9_FAILURES:-0}"
  fi
  restore_all || true

  # ---- cap A/B: the upload limit as a PAIRED reading (not a mutation) -----
  echo
  echo "  -- cap A/B: the renderer's mesh limit, 64 (old) vs 96 (now) --"
  # NOT a mutation: nothing here is supposed to be red. It is the price of a
  # decision made observable. The overlay uploads nine class models into the same
  # table the world pass fills, and on the reference recording the world pass has
  # already taken 59 of the 64 slots -- so at the OLD limit five of the nine were
  # refused with E_OUTOFMEMORY, and a refused key is submitted, counted and drawn
  # from nothing (precisely the shape m8 injects on purpose). Without this pair
  # "raise the cap" would be an assumption, not a reading.
  patch_in "$MUT_RENDERER" \
"  constexpr std::size_t kEntityModelMeshLimit = 96;" \
"  constexpr std::size_t kEntityModelMeshLimit = 64;  /* cap A: the old limit */" || mut_fail=1
  if rebuild_product cap64; then
    CAP64_RC=$(run_frame "$OUT/cap64" --outline on)
    CAP64_PREPARED=$(trace_field "$OUT/cap64.trace" "gpu: overlay meshes prepared=" prepared)
    CAP64_UPLOADED=$(trace_field "$OUT/cap64.trace" "gpu: overlay meshes prepared=" uploaded)
    CAP64_REJECTED=$(trace_field "$OUT/cap64.trace" "gpu: overlay meshes prepared=" rejected)
    CAP64_KEYS=$(grep -a -c "gpu: overlay mesh refused" "$OUT/cap64.trace" 2>/dev/null || true)
    echo "    at 64: prepared=${CAP64_PREPARED:-<none>} uploaded=${CAP64_UPLOADED:-<none>} rejected=${CAP64_REJECTED:-<none>} refused-keys-logged=${CAP64_KEYS:-0}"
    grep -a "gpu: overlay mesh refused" "$OUT/cap64.trace" 2>/dev/null | head -3 | sed "s/^/      /" || true
    assert_ge "cap 64: the old limit REFUSES class meshes (this is what 96 buys)" \
      "${CAP64_REJECTED:-0}" "1"
  fi
  restore_all; rebuild_product "restore after cap A" || true
  CAP96_RC=$(run_frame "$OUT/cap96" --outline on)
  CAP96_PREPARED=$(trace_field "$OUT/cap96.trace" "gpu: overlay meshes prepared=" prepared)
  CAP96_UPLOADED=$(trace_field "$OUT/cap96.trace" "gpu: overlay meshes prepared=" uploaded)
  CAP96_REJECTED=$(trace_field "$OUT/cap96.trace" "gpu: overlay meshes prepared=" rejected)
  echo "    at 96: prepared=${CAP96_PREPARED:-<none>} uploaded=${CAP96_UPLOADED:-<none>} rejected=${CAP96_REJECTED:-<none>}"
  assert_eq "cap 96: every prepared class mesh is uploaded" "${CAP96_REJECTED:-<none>}" "0"
  assert_eq "cap 96: uploaded count matches prepared" "${CAP96_UPLOADED:-<none>}" "${CAP96_PREPARED:-<none>}"

  # ---- m4: draw the overlay AFTER the ViewModel pass ---------------------
  echo
  echo "  -- m4: the overlay draws after the ViewModel pass --"
  # The defect: the pass is MOVED after drawViewModel(), which clears the depth
  # buffer. Expressed as two literal replacements -- remove the call from its
  # slot, insert it after the ViewModel -- so the restore is still a byte copy
  # and the ordinal is taken where the pass actually ran, not faked with a +2.
  patch_in "$MUT_RENDERER" \
"  drawOverlayPass();
  // T21: the depth the frame has after the world" \
"  // T21: the depth the frame has after the world" || mut_fail=1
  patch_in "$MUT_RENDERER" \
"  drawViewModel();" \
"  drawViewModel();
  drawOverlayPass();  /* mutation m4: after the pass that clears depth */" || mut_fail=1
  if rebuild_product m4; then
    # WITH a viewmodel: the mutation moves the overlay after the ViewModel pass, and
    # the pair being asserted only exists when that pass runs. Without --viewmodel
    # both the mutant and the fixed build report vmOrdinal=0 and the case would
    # read GREEN -- a mutation that "did not move a reading" because the reading
    # was never taken. That is the same trap this file fixes elsewhere.
    M4_RC=$(run_frame "$OUT/m4" --outline on --viewmodel models/weapons/v_models/v_scattergun_scout.mdl)
    M4_ORDER=$(outline_value "$OUT/m4.trace" orderOk)
    M4_ORDINAL=$(outline_value "$OUT/m4.trace" ordinal)
    M4_VM=$(outline_value "$OUT/m4.trace" vmOrdinal)
    echo "    m4 ordinal=${M4_ORDINAL:-<none>} vmOrdinal=${M4_VM:-<none>} orderOk=${M4_ORDER:-<none>}"
    must_move "m4" "pass order (overlay before viewModel)" "1" "${M4_ORDER:-0}"
    # The number itself must move too: the mutant's ordinal is no longer the slot
    # the contract names, because the ordinal is read where the pass ran.
    must_move "m4" "overlay ordinal after the move" "6" "${M4_ORDINAL:-0}"
  fi
  restore_all; rebuild_product "restore after m4" || true

  # The restored tree must be the FIXED tree, and a byte-exact restore plus a
  # rebuild is only half of that: the binary has to read green again, or every
  # later reading in this worktree is a reading of the last mutant.
  R_RC=$(run_frame "$OUT/restored" --outline on --viewmodel models/weapons/v_models/v_scattergun_scout.mdl)
  assert_eq "restored build: overlay ordinal" "$(outline_value "$OUT/restored.trace" ordinal)" "6"
  assert_eq "restored build: pass order" "$(outline_value "$OUT/restored.trace" orderOk)" "1"
  assert_eq "restored build: overlay drew" "$(outline_value "$OUT/restored.trace" drew)" "1"

  echo
  if [ "$mut_fail" = "0" ]; then
    echo "MUTATION-SUITE=ALL-RED-RESTORED"
  else
    echo "MUTATION-SUITE=FAIL (a mutation did not move a reading, or a restore was not byte-exact;"
    echo "  mutations that did not compile: $mut_invalid -- those proved nothing)"
    fail=1
  fi
else
  echo
  echo "mutation suite not requested (pass --mutation); the eight defects it"
  echo "reintroduces are NOT VERIFIED by this run."
fi

echo
if [ "$fail" = "0" ]; then
  echo "OUTLINE-CHECK=PASS"
else
  echo "OUTLINE-CHECK=FAIL"
fi
exit "$fail"
