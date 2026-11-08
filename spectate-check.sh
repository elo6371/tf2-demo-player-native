#!/usr/bin/env bash
# spectate-check.sh -- T66: the SourceTV death relay camera gate.
#
# WHAT THIS GATE IS
# -----------------
# T66 makes the camera follow the match the way a SourceTV broadcast does: when
# the player being watched dies, the body leaves the frame at the death tick, the
# camera prefers the demo's own observer target, otherwise walks a stable order of
# living players, and moves between them over about a third of a second on a clock
# that does not stop when playback does. Each of those clauses can fail on its own,
# and each is asserted here against the product's own code path:
#
#   A. The decision itself (spectate_probe --self-test): 49 checks over the death
#      test, the stable order, the director handle, the manual pick, the handover
#      clock and the seek rule.
#   B. The demo replay (spectate_probe --demo): the same recording the gate ships
#      against replays to three deaths, three handovers, and never a frame with no
#      target and never a seek.
#   C. The product (tf2_demo_native.exe): eight times playback to the capture tick
#      reports the same three deaths and three handovers, prints a handover in
#      progress rather than only its ends, and removes exactly the dead players
#      from the submitted set.
#   D. The handover shape (spectate-transitions.py): every handover records at
#      least five intermediate positions, its progress never goes backwards, and no
#      single step covers more than 30% of the move. A cut passes A-C and fails
#      only here, which is why it is a separate section.
#   E. The manual pick: a user picking a player gets that player in the same frame
#      with NO handover, and the pick is not stolen back by the automatic rule.
#   F. The picture: with nobody dead yet, running with the corpse rule on and off
#      must produce the same pixels -- the rule may not touch a living frame.
#   G. The pause: the handover clock is the WALL clock, so a paused demo must not
#      freeze a handover that is already in flight. Every other section reads a
#      tick-keyed quantity and stays green when the relay is stopped dead by a
#      pause, which is exactly why this clause needed its own run.
#   H. The pick held across a pause: E never pauses and G never picks, so the
#      conjunction -- a pick is still the target after the recording was frozen and
#      resumed -- was covered by neither. This section runs both at once.
#   I. A real seek: the product's own jump path (the same one the UI uses) moves the
#      transport backwards while a handover is in flight. The relay has to count the
#      seek once and drop the handover, instead of interpolating between two moments
#      of the match that never coexisted.
#   J. The speed: a 16x replay reports no seeks. The bound that decides "is this
#      jump a seek" used to be a fixed 200 ticks, which reported every other frame
#      of a 16x replay as a seek (T66 note 1); it is derived from the live speed now
#      and the reading has to be taken at 16x, because at 8x the old rule looked
#      correct.
#
# NEGATIVE CONTROLS (first, each must fail with a NAMED reason)
#   * a demo path that does not exist          -> FAIL demo header <path>
#   * a window that ends before it starts      -> FAIL range --from-tick ..
#   * a --spectate-target past the world       -> rejectedFlags >= 1
#
# BINARY FRESHNESS
#   The probe and the product are prebuilt. Gate numbers produced by an older
#   binary describe the previous tree while claiming this one's name, so the gate
#   prints BINARY-FRESH=1 only when both executables are newer than every source
#   file they compile and fails with STALE-BINARY=1 otherwise.
#
# Usage: bash spectate-check.sh [--skip-negative-controls]
# Exit:  0 = the negative controls failed with their named reasons AND every
#            forward assertion held.
#        1 = one of those did not hold.
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/spectate_probe.exe
EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="${SPECTATE_OUT:-evidence/t66-sourcetv/runtime}"
SPEED="${SPECTATE_SPEED:-8}"
CAPTURE_TICK="${SPECTATE_CAPTURE_TICK:-19700}"
ALIVE_TICK="${SPECTATE_ALIVE_TICK:-12000}"

# The product rejects absolute output paths. Fail here with a useful message
# instead of later with exit 16 and an empty trace; keep all evidence in-repo.
case "$OUT" in
  /*|[A-Za-z]:[\\/]*|[\\/][\\/]*)
    echo "FATAL: SPECTATE_OUT must be a repository-relative path, got: $OUT"
    echo "       Use, for example, evidence/t66-sourcetv/runtime-fresh."
    exit 2 ;;
esac
mkdir -p "$OUT"
# --- isolated settings (D-22) ----------------------------------------------
# The product persists its settings and UI layout under %LOCALAPPDATA%/TF
# Demo Player. A gate that reads or writes there shares state with the desktop
# player and with every other gate on the host, so a previous run (or a human)
# can change what this gate measures. Each run gets a throwaway directory
# instead, and it is removed on exit.
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-iso.XXXXXX") || {
  echo "FATAL: cannot create isolated settings directory" >&2; exit 2; }
ISO_SETTINGS="$ISO_DIR/settings.ini"
ISO_UI="$ISO_DIR/ui.ini"
# Chain any EXIT trap already installed instead of replacing it: an overwritten
# trap would skip a mutation restore and leave a mutated build behind, which is
# the "failure looks like success" shape this suite forbids.
. "$(dirname "$0")/verify-iso.sh"
iso_chain_exit "$ISO_DIR"


RUN_NEGATIVE=1
[ "${1:-}" = "--skip-negative-controls" ] && RUN_NEGATIVE=0

# Which sections to run. The gate runs them all by default; the mutation suite
# narrows this to the sections its mutant is supposed to redden, because a
# mutant is proved by the assertion it breaks, not by how long the run took.
# Section 5 reads the trace section 4 wrote, so those two travel together.
SECTIONS="${SPECTATE_SECTIONS:-0,1,2,3,4,5,6,7,8,9,10,11}"
section_wanted() {
  case ",$SECTIONS," in
    *",$1,"*) return 0 ;;
    *) return 1 ;;
  esac
}

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
assert_lt() { # assert_lt <label> <actual> <maximum>
  if [ -n "${2:-}" ] && [ "$2" -lt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (< $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected < $3"; fail=1
  fi
}
assert_le_float() { # assert_le_float <label> <actual> <maximum>
  if [ -n "${2:-}" ] && awk -v a="$2" -v b="$3" 'BEGIN { exit !(a <= b) }' 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}
assert_contains() { # assert_contains <label> <haystack> <needle>
  case "${2:-}" in
    *"$3"*) echo "  OK   $1 contains $3" ;;
    *) echo "  FAIL $1 = ${2:-<none>}, expected to contain $3"; fail=1 ;;
  esac
}
field() { # field <line> <name> -> value of the first "name=value" token
  local line="$1" name="$2" rest
  case "$line" in
    *"$name="*) rest="${line#*"$name="}" ;;
    *) printf ''; return 0 ;;
  esac
  printf '%s' "${rest%% *}"
}

for path in "$PROBE" "$EXE" "$TF" "$DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

# ===========================================================================
if section_wanted 0; then
echo "=== 0/11 binary freshness: both executables are newer than what they compile ==="
STALE=""
# Each executable is compared with the sources it actually compiles. The relay class is
# linked into both, so it has to be older than both binaries; main.cpp belongs to the
# product alone and spectate_probe.cpp to the probe alone. The first version of this
# block required EVERY source to be older than BOTH binaries, which reports
# STALE-BINARY on a perfectly built tree the moment main.cpp is edited -- relinking the
# product does not, and must not, touch the probe. That false red was found by the
# mutation suite's closing baseline (2026-10-25 15:38), not by reasoning.
for source in native/src/spectate_target.cpp native/include/spectate_target.h; do
  if [ "$source" -nt "$PROBE" ]; then STALE="$STALE $PROBE<$source"; fi
  if [ "$source" -nt "$EXE" ]; then STALE="$STALE $EXE<$source"; fi
done
if [ native/tools/spectate_probe.cpp -nt "$PROBE" ]; then
  STALE="$STALE $PROBE<native/tools/spectate_probe.cpp"
fi
if [ native/src/main.cpp -nt "$EXE" ]; then
  STALE="$STALE $EXE<native/src/main.cpp"
fi
if [ -z "$STALE" ]; then
  echo "  OK   BINARY-FRESH=1"
else
  echo "  FAIL STALE-BINARY=1$STALE"
  echo "       run: bash build-target.sh spectate_probe && bash build-target.sh tf2_demo_native"
  fail=1
fi

fi
# ===========================================================================
if section_wanted 1; then
echo "=== 1/11 negative controls: each must fail with a NAMED reason ==="
if [ "$RUN_NEGATIVE" = "1" ]; then
  NEG_OUT="$OUT/negative-missing-demo.txt"
  "$PROBE" --demo "D:/no/such/demo.dem" > "$NEG_OUT" 2>&1
  NEG_RC=$?
  if [ "$NEG_RC" -ne 0 ] && grep -q 'FAIL demo header' "$NEG_OUT"; then
    echo "  OK   missing demo: exit $NEG_RC and names the path"
  else
    echo "  FAIL missing demo: exit $NEG_RC, expected non-zero and 'FAIL demo header'"; fail=1
  fi
  NEG_RANGE="$OUT/negative-range.txt"
  "$PROBE" --demo "$DEMO" --from-tick 20000 --to-tick 10000 > "$NEG_RANGE" 2>&1
  RANGE_RC=$?
  if [ "$RANGE_RC" -eq 2 ] && grep -q 'FAIL range --from-tick 20000 is past --to-tick 10000' "$NEG_RANGE"; then
    echo "  OK   inverted window: exit 2 and names both ticks"
  else
    echo "  FAIL inverted window: exit $RANGE_RC, expected 2 and the range message"; fail=1
  fi
else
  echo "  SKIP negative controls (--skip-negative-controls); a skipped control is NOT RUN, not a pass"
fi

fi
# ===========================================================================
if section_wanted 2; then
echo "=== 2/11 the decision itself: spectate_probe --self-test ==="
"$PROBE" --self-test > "$OUT/self-test.txt" 2>&1
SELF_RC=$?
tail -1 "$OUT/self-test.txt" | sed 's/^/  /'
# A self-test that reports only a count is not a diagnosis. When it fails, print the
# assertions that failed: a red gate has to say WHICH rule broke -- for a human, and
# for the mutation suite, which requires every mutant to redden a NAMED assertion.
# Case m5 is why this block exists: the world-origin rule is only reachable from the
# self-test (the sample recording contains no target-less frame), so before this block
# the gate went red with "failures = 2" and named nothing.
if grep -q '^FAIL ' "$OUT/self-test.txt" 2>/dev/null; then
  grep '^FAIL ' "$OUT/self-test.txt" | sed 's/^/  /'
fi
assert_eq "self-test exit" "$SELF_RC" "0"
SELF_LINE=$(grep 'SPECTATE-SELF-TEST=' "$OUT/self-test.txt" | tail -1)
assert_contains "self-test verdict" "$SELF_LINE" "SPECTATE-SELF-TEST=PASS"
assert_eq "self-test failures" "$(field "$SELF_LINE" failures)" "0"
assert_ge "self-test checks" "$(field "$SELF_LINE" checks)" "49"

fi
# ===========================================================================
if section_wanted 3; then
echo "=== 3/11 the demo replay: three deaths, three handovers, no seek, no empty frame ==="
"$PROBE" --demo "$DEMO" --from-tick 10321 --to-tick 20000 --step 5 > "$OUT/replay-1.txt" 2>&1
REPLAY_RC=$?
assert_eq "demo replay exit" "$REPLAY_RC" "0"
REPLAY=$(grep 'SPECTATE-DEMO=' "$OUT/replay-1.txt" | tail -1)
assert_contains "demo replay verdict" "$REPLAY" "SPECTATE-DEMO=PASS"
REPLAY_FRAMES=$(grep '^demo: frames=' "$OUT/replay-1.txt" | tail -1)
assert_eq "replayed deaths" "$(field "$REPLAY_FRAMES" deaths)" "3"
assert_eq "replayed handovers" "$(field "$REPLAY_FRAMES" handovers)" "3"
assert_eq "handovers that opened" "$(field "$REPLAY_FRAMES" transitions_open)" "3"
assert_eq "handovers that closed" "$(field "$REPLAY_FRAMES" transitions_closed)" "3"
assert_eq "frames with no target" "$(field "$REPLAY_FRAMES" notarget)" "0"
assert_eq "seeks detected in a linear replay" "$(field "$REPLAY_FRAMES" resync)" "0"
assert_eq "the director handle was never usable on this demo" "$(field "$REPLAY_FRAMES" director)" "0"
assert_ge "replayed frames" "$(field "$REPLAY_FRAMES" frames)" "1000"

# Repeat the identical command: the relay is a pure function of the recording, so
# a second run has to produce the same verdict line. A run that only sometimes
# finds the same target is a different product.
"$PROBE" --demo "$DEMO" --from-tick 10321 --to-tick 20000 --step 5 > "$OUT/replay-2.txt" 2>&1
if cmp -s "$OUT/replay-1.txt" "$OUT/replay-2.txt"; then
  echo "  OK   a repeat replay of the same window is byte-identical"
else
  echo "  FAIL a repeat replay of the same window differs: $(diff "$OUT/replay-1.txt" "$OUT/replay-2.txt" | head -4)"
  fail=1
fi

fi
# ===========================================================================
if section_wanted 4; then
echo "=== 4/11 the product: the same three deaths at ${SPEED}x, a handover in progress, corpses removed ==="
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/product.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --playback-speed "$SPEED" --capture-tick "$CAPTURE_TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/product.bmp")" > "$OUT/product.txt" 2>&1
PRODUCT_RC=$?
assert_eq "product run exit" "$PRODUCT_RC" "0"
assert_ge "product trace bytes" "$(stat -c %s "$OUT/product.trace" 2>/dev/null || echo 0)" "1000000"
PRODUCT_SUMMARY=$(grep 'spectate: summary' "$OUT/product.trace" | tail -1)
echo "  summary: $PRODUCT_SUMMARY"
assert_eq "product deaths" "$(field "$PRODUCT_SUMMARY" deaths)" "3"
assert_eq "product handovers" "$(field "$PRODUCT_SUMMARY" handovers)" "3"
assert_eq "product frames with no target" "$(field "$PRODUCT_SUMMARY" notarget)" "0"
assert_eq "product seeks" "$(field "$PRODUCT_SUMMARY" resync)" "0"
assert_eq "product manual picks" "$(field "$PRODUCT_SUMMARY" manual)" "0"
# The same clause as section 3, read on the product instead of the probe. The demo's
# observer handle names a permanently dead spectator slot on this recording, so a relay
# that trusted the handle would park the camera on a corpse for the whole match -- the
# jump the task forbids. Two readings, because "the probe refuses it" and "the product
# refuses it" are different claims about different binaries.
assert_eq "the product never used the demo director handle either" "$(field "$PRODUCT_SUMMARY" director)" "0"
assert_ge "instances the corpse rule removed" "$(field "$PRODUCT_SUMMARY" hiddenInstances)" "100"
CORPSE_LINES=$(grep -c 'scene: corpseFilter' "$OUT/product.trace" 2>/dev/null || true)
assert_ge "frames that published a corpse filter line" "${CORPSE_LINES:-0}" "3"
LAST_FILTER=$(grep 'scene: corpseFilter' "$OUT/product.trace" | tail -1)
assert_contains "the last corpse filter names the dead player" "$LAST_FILTER" "indices="
FIRST_FILTER=$(grep 'scene: corpseFilter' "$OUT/product.trace" | head -1)
assert_contains "the first corpse filter names the first dead player (2)" "$FIRST_FILTER" "2"

fi
# ===========================================================================
if section_wanted 5; then
echo "=== 5/11 the handover shape: intermediate positions, monotone progress, no single jump ==="
python spectate-transitions.py "$OUT/product.trace" > "$OUT/handovers.txt" 2>&1
TRANSITIONS_RC=$?
assert_eq "handover analysis exit" "$TRANSITIONS_RC" "0"
cat "$OUT/handovers.txt" | sed 's/^/  /'
assert_eq "handovers analysed" "$(field "$(grep 'HANDOVERS=' "$OUT/handovers.txt")" HANDOVERS)" "3"
HANDOVER_INDEX=0
while IFS= read -r line; do
  case "$line" in
    handover\ n=*)
      HANDOVER_INDEX=$((HANDOVER_INDEX + 1))
      assert_ge "handover $HANDOVER_INDEX intermediate positions" "$(field "$line" samples)" "5"
      assert_le_float "handover $HANDOVER_INDEX largest step as a share of the move" "$(field "$line" maxstep)" "0.30"
      assert_eq "handover $HANDOVER_INDEX progress never went backwards" "$(field "$line" monotone)" "1"
      assert_le_float "handover $HANDOVER_INDEX settled exactly on the target" "$(field "$line" endgap)" "1.0"
      ;;
  esac
done < "$OUT/handovers.txt"

fi
# ===========================================================================
if section_wanted 6; then
echo "=== 6/11 the manual pick: same frame, no handover, not taken back ==="
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/manual.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --playback-speed "$SPEED" --capture-tick "$ALIVE_TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/manual.bmp")" --spectate-target 6 > "$OUT/manual.txt" 2>&1
MANUAL_RC=$?
assert_eq "manual pick run exit" "$MANUAL_RC" "0"
MANUAL_SUMMARY=$(grep 'spectate: summary' "$OUT/manual.trace" | tail -1)
echo "  summary: $MANUAL_SUMMARY"
assert_eq "manual pick accepted" "$(field "$MANUAL_SUMMARY" manual)" "1"
assert_eq "manual pick rejected" "$(field "$MANUAL_SUMMARY" rejected)" "0"
# What the summary must NOT show is a pick that was refused; the handovers it
# counts happened after the first death and are asserted on their own below.
# The pick has to take effect in the frame it is made in. The rest of the run
# still contains real deaths (this recording has three), so what is asserted here
# is the state BEFORE the first death: no handover, and the camera already on the
# player that was picked. Asserting "the whole run has no handover" would be a
# claim about the recording, not about the pick.
FIRST_MANUAL=$(grep 'spectate: tick=' "$OUT/manual.trace" | head -1)
assert_contains "the first product frame already carries the manual target" "$FIRST_MANUAL" "target=6"
assert_contains "the first product frame reports the pick as manual" "$FIRST_MANUAL" "src=manual"
assert_contains "the first product frame is not mid-handover" "$FIRST_MANUAL" "tr=0"
MANUAL_EARLY=$(awk 'BEGIN { done = 0; early = 0 }
  /spectate: tick=/ {
    if (done) next
    if (!match($0, /deaths=[0-9]+/)) next
    d = substr($0, RSTART + 7, RLENGTH - 7)
    if (d + 0 >= 1) { done = 1; next }
    if (match($0, /handovers=[0-9]+/)) {
      h = substr($0, RSTART + 10, RLENGTH - 10)
      if (h + 0 >= 1) early++
    }
  }
  END { print early + 0 }' "$OUT/manual.trace")
assert_eq "handovers that happened before the first death in the manual run" "${MANUAL_EARLY:-<none>}" "0"
MANUAL_FRAMES=$(grep -c 'spectate: tick=' "$OUT/manual.trace" 2>/dev/null || true)
assert_ge "product frames printed by the manual run" "${MANUAL_FRAMES:-0}" "3"

# A pick past the end of the world is refused rather than silently clamped to a
# player the user did not choose.
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/rejected.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --playback-speed "$SPEED" --capture-tick "$ALIVE_TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/rejected.bmp")" --spectate-target 5000 > "$OUT/rejected.txt" 2>&1
REJECTED_SUMMARY=$(grep 'spectate: summary' "$OUT/rejected.trace" | tail -1)
assert_ge "out-of-range pick flagged" "$(field "$REJECTED_SUMMARY" rejectedFlags)" "1"
assert_eq "out-of-range pick was never handed to the relay as a target" "$(field "$REJECTED_SUMMARY" rejected)" "0"

fi
# ===========================================================================
if section_wanted 7; then
echo "=== 7/11 the picture: the corpse rule does not touch a frame with nobody dead ==="
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/alive-normal.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --playback-speed "$SPEED" --capture-tick "$ALIVE_TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/alive-normal.bmp")" > "$OUT/alive-normal.txt" 2>&1
ALIVE1_RC=$?
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/alive-corpses.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" --spectate-show-corpses \
  --playback-speed "$SPEED" --capture-tick "$ALIVE_TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/alive-corpses.bmp")" > "$OUT/alive-corpses.txt" 2>&1
ALIVE2_RC=$?
assert_eq "alive capture with the rule on exit" "$ALIVE1_RC" "0"
assert_eq "alive capture with the rule off exit" "$ALIVE2_RC" "0"
PIXEL_LINE=$(python spectate-pixels.py "$OUT/alive-normal.bmp" "$OUT/alive-corpses.bmp" 2>&1)
echo "  $PIXEL_LINE"
assert_eq "pixels changed on a frame where nobody is dead" "$(field "$PIXEL_LINE" PIXELS)" "0"
assert_eq "capture width" "$(field "$PIXEL_LINE" SIZE)" "1264x681"
CORPSES_FILTERS=$(grep -c 'scene: corpseFilter' "$OUT/alive-corpses.trace" 2>/dev/null || true)
assert_eq "corpse filter lines with the rule disabled" "${CORPSES_FILTERS:-0}" "0"
CORPSES_SUMMARY=$(grep 'spectate: summary' "$OUT/alive-corpses.trace" | tail -1)
assert_eq "instances removed with the rule disabled" "$(field "$CORPSES_SUMMARY" hiddenInstances)" "0"

fi
# ===========================================================================
if section_wanted 8; then
echo "=== 8/11 the pause: the handover keeps moving while the demo clock is frozen ==="
# The requirement says the handover clock is independent of the demo clock: pausing
# the recording must not stop the camera on its way to the next player. The product
# used to run the relay inside the branch that executes only when the tick CHANGES,
# so a pause froze it -- and no tick-keyed reading can see that, because every
# tick-keyed reading is still correct. This run pauses the recording one frame after
# the first death and then reads the trace while it is stopped: the SAME scene tick
# must appear on many lines, with the progress rising and the window closing on its
# own, all before playback resumes.
PAUSE_TICK="${SPECTATE_PAUSE_TICK:-3310}"
PAUSE_SECONDS="${SPECTATE_PAUSE_SECONDS:-2.0}"
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/pause.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --playback-speed "$SPEED" --spectate-pause-at "$PAUSE_TICK" --spectate-pause-seconds "$PAUSE_SECONDS" \
  --capture-tick "$CAPTURE_TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/pause.bmp")" > "$OUT/pause.txt" 2>&1
PAUSE_RC=$?
assert_eq "pause run exit" "$PAUSE_RC" "0"
assert_ge "pause trace bytes" "$(stat -c %s "$OUT/pause.trace" 2>/dev/null || echo 0)" "1000000"
awk '/spectate: pause-at/ { open = 1; next } /spectate: resume-after/ { open = 0 } open' \
  "$OUT/pause.trace" > "$OUT/pause-window.txt"
PAUSE_OPEN=$(grep -c 'spectate: pause-at' "$OUT/pause.trace" 2>/dev/null || true)
PAUSE_RESUMED=$(grep -c 'spectate: resume-after' "$OUT/pause.trace" 2>/dev/null || true)
assert_eq "pause windows opened" "${PAUSE_OPEN:-0}" "1"
assert_eq "pause windows resumed" "${PAUSE_RESUMED:-0}" "1"
echo "  frozen window: $(tr '\n' '|' < "$OUT/pause-window.txt" | cut -c1-360)"
# The window is read from what the product printed for itself, so the reading is
# only as fine as the frames the host actually ran inside it. Two kinds of clause
# are kept apart here: the ones that decide the CONTRACT hold on any host, and the
# ones that measure SAMPLING DENSITY only apply when the host gave the loop enough
# frames. A single 842 ms frame inside the window swallows the whole 0.3 s handover
# (the 2026-10-31 chain run hit exactly that: two lines and one progress value,
# with the handover demonstrably completing), and a starved host is not evidence
# that the relay stopped. Density is therefore gated on the frame count the window
# actually contains; the contract clauses below never are.
FROZEN_LINES=$(grep -c 'spectate: tick=' "$OUT/pause-window.txt" 2>/dev/null || true)
FROZEN_FRAMES=$(grep -c 'draw: begin' "$OUT/pause-window.txt" 2>/dev/null || true)
# Even a starved window must show the handover in flight and finishing: a relay
# that froze with the demo clock prints one line and never a tr=0.
assert_ge "handover lines printed while the clock was frozen" "${FROZEN_LINES:-0}" "2"
FROZEN_TICKS=$(grep -o 'spectate: tick=[0-9]*' "$OUT/pause-window.txt" | sort -u | wc -l)
assert_eq "distinct demo ticks seen during the pause" "$FROZEN_TICKS" "1"
FROZEN_CLOSED=$(grep -c 'tr=0' "$OUT/pause-window.txt" 2>/dev/null || true)
assert_ge "handovers that finished while the clock was frozen" "${FROZEN_CLOSED:-0}" "1"
FROZEN_PROGRESS=$(grep -o 'prog=[0-9.]*' "$OUT/pause-window.txt" | sort -u | wc -l)
# Only lines where the handover is still in flight (tr=1) carry a progress value;
# the line that reports the completion resets it, so reading the reset as a
# backwards step would fail every run.
FROZEN_BACKWARDS=$(awk '
  /spectate: tick=/ && /tr=1/ && match($0, /prog=[0-9.]+/) {
    v = substr($0, RSTART + 5, RLENGTH - 5) + 0
    if (seen && v < prev) bad++
    prev = v; seen = 1
  }
  END { print bad + 0 }' "$OUT/pause-window.txt")
assert_eq "handover progress went backwards while the clock was frozen" "${FROZEN_BACKWARDS:-<none>}" "0"
if [ "${FROZEN_FRAMES:-0}" -ge 8 ]; then
  assert_ge "handover lines the product printed while the clock was frozen" "${FROZEN_LINES:-0}" "8"
  assert_ge "distinct handover progress values while the clock was frozen" "$FROZEN_PROGRESS" "5"
else
  echo "  NOTE starved window: frames = ${FROZEN_FRAMES:-0} (< 8), so line and progress counts cannot read the handover; completion, monotonic progress and the single tick still decide"
fi
PAUSE_SUMMARY=$(grep 'spectate: summary' "$OUT/pause.trace" | tail -1)
assert_eq "deaths in the paused run" "$(field "$PAUSE_SUMMARY" deaths)" "3"
assert_eq "handovers in the paused run" "$(field "$PAUSE_SUMMARY" handovers)" "3"
assert_eq "seeks in the paused run" "$(field "$PAUSE_SUMMARY" resync)" "0"

fi
# ===========================================================================
if section_wanted 9; then
echo "=== 9/11 the pick: a manual target survives a pause, and nothing takes it back ==="
# Sections 6 and 8 each hold one half of this: section 6 picks from the launch line
# and never pauses, section 8 pauses a mid-handover relay and never picks. The
# requirement is the conjunction -- the pick a user made must still be the target
# after the recording was frozen and resumed -- and neither section can see the
# other half. The evidence is what the product printed INSIDE the frozen window: a
# relay that re-derives its target while the clock is stopped shows it there.
# 2500 playback ticks (= server 12820 on this recording), chosen for a reason: the
# player the pick names (6) dies at server tick 13097, so a pause any later than
# ~2750 measures a pick that the relay has already legitimately left. The reading
# below is only meaningful while the picked player is alive.
PICK_PAUSE_TICK="${SPECTATE_PICK_PAUSE_TICK:-2500}"
PICK_PAUSE_SECONDS="${SPECTATE_PICK_PAUSE_SECONDS:-2.0}"
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/pause-manual.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --playback-speed "$SPEED" --spectate-target 6 \
  --spectate-pause-at "$PICK_PAUSE_TICK" --spectate-pause-seconds "$PICK_PAUSE_SECONDS" \
  --capture-tick "$CAPTURE_TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/pause-manual.bmp")" > "$OUT/pause-manual.txt" 2>&1
PICK_RC=$?
assert_eq "pick-through-pause run exit" "$PICK_RC" "0"
PICK_SUMMARY=$(grep 'spectate: summary' "$OUT/pause-manual.trace" | tail -1)
echo "  summary: $PICK_SUMMARY"
assert_eq "picks accepted in the pick run" "$(field "$PICK_SUMMARY" manual)" "1"
assert_eq "picks refused in the pick run" "$(field "$PICK_SUMMARY" rejected)" "0"
assert_eq "frames with no target in the pick run" "$(field "$PICK_SUMMARY" notarget)" "0"
PICK_OPEN=$(grep -c 'spectate: pause-at' "$OUT/pause-manual.trace" 2>/dev/null || true)
PICK_RESUMED=$(grep -c 'spectate: resume-after' "$OUT/pause-manual.trace" 2>/dev/null || true)
assert_eq "pause windows opened in the pick run" "${PICK_OPEN:-0}" "1"
assert_eq "pause windows resumed in the pick run" "${PICK_RESUMED:-0}" "1"
sed -n '/spectate: pause-at/,/spectate: resume-after/p' "$OUT/pause-manual.trace" > "$OUT/pick-window.txt"
PICK_LINES=$(grep -c 'spectate: tick=' "$OUT/pick-window.txt" 2>/dev/null || true)
echo "  frozen window: $(grep 'spectate: tick=' "$OUT/pick-window.txt" | tr '\n' '|' | cut -c1-360)"
# The trace prints a spectate line only when something changed, so a window with no
# lines is not a failure -- it is the strongest possible reading of "nothing took the
# pick back". What must not happen is a line inside the window that disagrees.
if [ "${PICK_LINES:-0}" -gt 0 ]; then
  PICK_WRONG=$(grep 'spectate: tick=' "$OUT/pick-window.txt" | grep -v 'target=6 ' | wc -l)
  PICK_HANDOVERS=$(grep 'spectate: tick=' "$OUT/pick-window.txt" | grep -c 'tr=1' || true)
  assert_eq "spectate frames inside the pause that name a different target" "${PICK_WRONG:-<none>}" "0"
  assert_eq "handovers opened while the pick was held through the pause" "${PICK_HANDOVERS:-<none>}" "0"
else
  echo "  NOTE the pick was never re-published during the pause, which is the quiet form of holding it"
fi
# The trace prints a line only when the relay's state changes, so the first line
# AFTER the resume is not "the frame just after the resume": it is the next change,
# and that change is legitimately a death-driven handover once the picked player is
# gone. A relay that dropped the pick on resume would show the replacement WITHOUT a
# death behind it, so that is what is asserted: the pick was the target when the
# freeze began, and any later change names a death.
PICK_READING=$(awk '
  function val(line, name,   m) {
    if (line == "") return "<none>"
    if (match(line, name "=[^ ]+")) return substr(line, RSTART + length(name) + 1, RLENGTH - length(name) - 1)
    return "<none>"
  }
  /spectate: tick=/ {
    if (!paused) before = $0
    else if (after == "" && $0 !~ /target=6 /) after = $0
  }
  /spectate: pause-at/ { paused = 1 }
  END { printf "before_target=%s before_src=%s after_target=%s after_deaths=%s\n", \
      val(before, "target"), val(before, "src"), val(after, "target"), val(after, "deaths") }' \
  "$OUT/pause-manual.trace")
echo "  $PICK_READING"
assert_eq "the target when the freeze began" "$(field "$PICK_READING" before_target)" "6"
assert_eq "the source when the freeze began" "$(field "$PICK_READING" before_src)" "manual"
if [ "$(field "$PICK_READING" after_target)" = "<none>" ]; then
  echo "  OK   the pick was still the target at the end of the run"
else
  assert_ge "deaths behind the first target change after the resume" "$(field "$PICK_READING" after_deaths)" "1"
fi

fi
# ===========================================================================
if section_wanted 10; then
echo "=== 10/11 a real seek: counted once, and no frame after it is mid-handover ==="
# The relay's seek rule was only ever proved by the self-test and by a rewind in a
# replay. The product's own transport is driven here through the camera script, which
# is the same jump path the UI uses (PlaybackRequestKind::JumpTicks), so the reading
# is about the product and not about a simulated tick stream.
# What is asserted after the seek is the state the viewer sees -- resync counted, and
# the next frame on the new target rather than a glide between two moments that never
# coexisted. The clearing line itself has no mutant of its own: dropping it was
# measured and is invisible here (the next frame's target change ends the transition
# anyway), so the case m7 pins the count instead. See T66-CLOSEOUT.md, "the mutants".
cat > "$OUT/seek-script.txt" <<'SEEKEOF'
tick 3400
seek 2500
frames 24
quit
SEEKEOF
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/seek.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --playback-speed "$SPEED" --camera-script "$(cygpath -m "$PWD/$OUT/seek-script.txt")" \
  > "$OUT/seek.txt" 2>&1
SEEK_RC=$?
assert_eq "seek run exit" "$SEEK_RC" "0"
# assert_contains takes its haystack as text, not as a path: a file name handed to it
# is compared as a literal string, which is a green-looking assertion that can only
# fail. Cut the two lines out of the trace first.
SEEK_ACCEPT=$(grep -a 'cmd=seek' "$OUT/seek.trace" | head -1)
assert_contains "the product accepted the seek" "$SEEK_ACCEPT" "cmd=seek result=ok detail=tick=2500"
SEEK_REQUEST=$(grep -a 'playback: request' "$OUT/seek.trace" | grep 'kind=jump-ticks' | head -1)
assert_contains "the seek went through the jump path" "$SEEK_REQUEST" "source=camera-script requestedTick=2500 "
# Split the trace at the seek: the last spectate line before it and the first one
# after it are the two states the clause distinguishes.
SEEK_READING=$(awk '
  function val(line, name,   m) {
    if (line == "") return "<none>"
    if (match(line, name "=[0-9]+")) return substr(line, RSTART + length(name) + 1, RLENGTH - length(name) - 1)
    return "<none>"
  }
  /spectate: tick=/ {
    if (seen) { if (after == "") after = $0 }
    else if (!before_seek) before = $0
  }
  /cmd=seek result=ok/ { seen = 1; if (before != "") before_seek = 1 }
  END {
    printf "before_tick=%s before_resync=%s after_tick=%s after_resync=%s\n", \
      val(before, "tick"), val(before, "resync"), val(after, "tick"), val(after, "resync")
  }' "$OUT/seek.trace")
echo "  $SEEK_READING"
# The pause is gone from this fixture on purpose: the handover window is measured on
# the wall clock (section 8), so any pause long enough to matter also lets the
# handover finish, and "was one in flight" then depends on how loaded the host was.
# What is asserted is what a viewer would notice: the seek is counted once, and the
# first frame the relay publishes afterwards is at the new tick instead of gliding
# from the old one.
assert_eq "seeks the relay reported before the seek" "$(field "$SEEK_READING" before_resync)" "0"
assert_ge "seeks the relay reported after the seek" "$(field "$SEEK_READING" after_resync)" "1"
assert_lt "the tick of the first line published after the seek" "$(field "$SEEK_READING" after_tick)" "$(field "$SEEK_READING" before_tick)"
SEEK_SUMMARY=$(grep 'spectate: summary' "$OUT/seek.trace" | tail -1)
echo "  summary: $SEEK_SUMMARY"
assert_eq "seeks counted in the whole seek run" "$(field "$SEEK_SUMMARY" resync)" "1"

fi
# ===========================================================================
if section_wanted 11; then
echo "=== 11/11 the speed: a 16x replay is playback, not a seek on every frame ==="
# T66 note 1: at 16x the relay reported two seeks in a run that never seeked. The
# cause was a fixed 200-tick gap; the bound is now derived from the live speed. The
# reading has to be taken at 16x, because at 8x the old rule looked correct.
TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$OUT/fast16.trace")" timeout -k 5 280 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
  --playback-speed 16 --capture-tick "$CAPTURE_TICK" \
  --capture-frame "$(cygpath -m "$PWD/$OUT/fast16.bmp")" > "$OUT/fast16.txt" 2>&1
FAST16_RC=$?
assert_eq "16x run exit" "$FAST16_RC" "0"
FAST16_SUMMARY=$(grep 'spectate: summary' "$OUT/fast16.trace" | tail -1)
echo "  summary: $FAST16_SUMMARY"
assert_eq "seeks reported during a 16x replay" "$(field "$FAST16_SUMMARY" resync)" "0"
assert_eq "deaths seen at 16x" "$(field "$FAST16_SUMMARY" deaths)" "3"
assert_eq "handovers seen at 16x" "$(field "$FAST16_SUMMARY" handovers)" "3"
assert_eq "frames with no target at 16x" "$(field "$FAST16_SUMMARY" notarget)" "0"
# There is no frame counter in the relay's summary, and the trace prints a line only
# on change, so the reading that the loop actually ran is the size of what it wrote.
assert_ge "16x trace bytes" "$(stat -c %s "$OUT/fast16.trace" 2>/dev/null || echo 0)" "1000000"

fi
# ===========================================================================
echo "=========================================================================="
if [ "$fail" -eq 0 ]; then
  echo "SPECTATE-CHECK=PASS"
else
  echo "SPECTATE-CHECK=FAIL"
fi
exit "$fail"
