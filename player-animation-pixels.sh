#!/usr/bin/env bash
# player-animation-pixels.sh -- T61 section F: the animation drive reaches the
# PIXELS the user sees.
#
# WHY THIS GATE EXISTS, SEPARATELY FROM player-animation-check.sh
# --------------------------------------------------------------------
# Sections A-E of player-animation-check.sh assert on counters and trace lines.
# A counter can be green while the picture is a flat black silhouette -- that is
# exactly how the black player body survived T60 with every counter healthy.
# The claim here is the picture itself, so the evidence has to be pixels:
#
#   F1  the product binary is fresh (a stale binary would measure old code)
#   F2  the entity pass's own COVERAGE is measured, not assumed: with
#       --world-entity-coverage the entity pass writes exactly (255,0,255) and
#       no other pass can, so the mask is what the pass drew
#   F3  over that mask the product frame is a shaded body, not a flat fill
#       (distinct colours, mean luminance, pure-black share)
#   F4  the animation drive moves those pixels: with and without
#       --player-animation at the SAME tick, camera and settings, a measured
#       share of the masked pixels must differ. Identical frames mean the drive
#       reaches nothing the user sees, whatever the counters say.
#
# The mask is deliberately NOT a hardcoded screen box: a box keeps passing when
# the entity pass stops drawing there.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it builds and runs captures):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t61-anim \
#     bash -c 'cd /d/TF2_Native_Worktrees/t61-anim && bash player-animation-pixels.sh'
#
# Usage: bash player-animation-pixels.sh
# Exit:  0 = PASS, 1 = FAIL, 2 = a required input is missing (FATAL).
set -uo pipefail
cd "$(dirname "$0")"

OUT=${T61_PIXEL_OUT:-evidence/t61-animation/pixels}
PRODUCT=${T61_PRODUCT:-native/build-nmake/tf2_demo_native.exe}
PY=${T61_PYTHON:-C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe}
TF=${T61_TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${T61_PIXEL_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}
# The camera was chosen for this tick: it frames a player body large enough to
# see pixels on. Changing the tick without re-choosing the camera makes F3/F4
# measure the sky.
TICK=${T61_PIXEL_TICK:-64161}
SPEED=${T61_PIXEL_SPEED:-8}
CAMERA=(--debug-camera 5363.8 -1102.1 5.9 4.1 0.0)

fail=0
say_fail() { echo "  FAIL $*"; fail=1; }

for path in "$PRODUCT" "$TF" "$DEMO" "$PY"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
[ -x "$PRODUCT" ] || { echo "FATAL: not executable: $PRODUCT"; exit 2; }

mkdir -p "$OUT"

# ===========================================================================
echo "=== F1: the binary under test is newer than the sources it compiles ==="
stale=""
for src in native/src/native_renderer.cpp native/src/main.cpp native/src/entity_model.cpp \
           native/src/animation_clock.cpp native/src/animation_binding.cpp; do
  [ -e "$src" ] || continue
  if [ "$PRODUCT" -ot "$src" ]; then stale="$src"; fi
done
if [ -z "$stale" ]; then
  echo "  OK   binary is newer than every source that feeds the pixel path"
else
  say_fail "STALE-BINARY=1 $PRODUCT is older than $stale; a stale binary would measure old code"
fi

# ===========================================================================
echo
echo "=== F2/F3/F4: capture the coverage mask, the animated frame and the off frame ==="
# run_frame <tag> <flag...>  -- every argument except the tag is identical
# between the animated and the off run; the flag list is the ONLY difference.
run_frame() {
  local tag="$1"; shift
  local bmp="$OUT/$tag.bmp" trace="$OUT/$tag.trace"
  rm -f "$bmp" "$trace"
  TF2_NATIVE_TRACE="$trace" timeout 1200 "$PRODUCT" \
    --tf-root "$TF" --hidden-window --audio-disabled \
    --settings-file "$OUT/settings.json" --ui-settings-file "$OUT/ui.json" \
    --demo "$DEMO" --capture-tick "$TICK" --playback-speed "$SPEED" \
    --capture-frame "$bmp" "${CAMERA[@]}" "$@" > "$OUT/$tag.log" 2>&1
  local rc=$?
  # The A-B identity has to come from lines that exist in BOTH runs. The animsum
  # line does not: it is published only when --player-animation is on, so using it
  # to prove "the same moment" made the check pass on the on run and read <none>
  # on the off run -- a reading wired to the wrong mechanism. These two lines are
  # emitted by the capture path regardless of the animation flag:
  #   capture: armed requested=64161 frame_tick=64161 frames=...
  #   scene: anim ... tick=74481 ...
  local view frameTick sceneTick
  view=$(grep -a -o 'gpu: world debug view=[a-z-]*' "$trace" 2>/dev/null | tail -1 | sed 's/.*view=//')
  frameTick=$(grep -a -o 'capture: armed requested=[0-9]* frame_tick=[0-9]*' "$trace" 2>/dev/null | tail -1 | sed 's/.*frame_tick=//')
  sceneTick=$(grep -a 'scene: anim ' "$trace" 2>/dev/null | tail -1 | grep -o 'tick=[0-9]*' | tail -1 | sed 's/tick=//')
  # Keep the small facts, drop the 50-70 MB trace: the trace is evidence of a
  # run, not a deliverable, and one per case would fill the disk.
  {
    echo "tag=$tag rc=$rc view=$view frameTick=$frameTick sceneTick=$sceneTick"
    grep -a 'animsum:' "$trace" 2>/dev/null | tail -1
    grep -a 'gpu: world debug view=' "$trace" 2>/dev/null | tail -1
    grep -a 'capture: armed' "$trace" 2>/dev/null | tail -1
    grep -a 'scene: anim ' "$trace" 2>/dev/null | tail -1
  } > "$OUT/$tag.summary"
  rm -f "$trace"
  printf '%s %s %s %s\n' "${rc:-1}" "${view:-<none>}" "${frameTick:-<none>}" "${sceneTick:-<none>}"
}

read -r cov_rc cov_view cov_ft cov_st < <(run_frame coverage --world-entity-coverage)
echo "  coverage rc=$cov_rc view=$cov_view frameTick=$cov_ft sceneTick=$cov_st"
read -r on_rc on_view on_ft on_st < <(run_frame animated --player-animation)
echo "  animated rc=$on_rc view=$on_view frameTick=$on_ft sceneTick=$on_st"
read -r off_rc off_view off_ft off_st < <(run_frame off)
echo "  off     rc=$off_rc view=$off_view frameTick=$off_ft sceneTick=$off_st"
# Determinism: the same capture, run twice, must be byte-identical. A frame that
# cannot be reproduced is not a controlled A-B -- the on/off comparison below
# would then be one run's noise against another's.
read -r rep_rc rep_view rep_ft rep_st < <(run_frame animated2 --player-animation)
echo "  repeat  rc=$rep_rc view=$rep_view frameTick=$rep_ft sceneTick=$rep_st"

if [ "$cov_rc" != "0" ]; then say_fail "coverage capture exited $cov_rc (a missing frame is not a pass)"; fi
if [ "$on_rc" != "0" ];  then say_fail "animated capture exited $on_rc (a missing frame is not a pass)"; fi
if [ "$off_rc" != "0" ]; then say_fail "off capture exited $off_rc (a missing frame is not a pass)"; fi
if [ "$rep_rc" != "0" ]; then say_fail "repeat capture exited $rep_rc (a missing frame is not a pass)"; fi
if [ -n "$rep_ft" ] && [ "$rep_ft" = "$TICK" ] && [ -n "$on_st" ] && [ "$rep_st" = "$on_st" ]; then
  echo "  OK   the repeat capture landed the same playback and scene tick ($rep_ft/$rep_st)"
else
  say_fail "repeat capture landed tick '${rep_ft:-<none>}'/scene '${rep_st:-<none>}', expected $TICK/$on_st"
fi
if [ -s "$OUT/animated2.bmp" ] && cmp -s "$OUT/animated.bmp" "$OUT/animated2.bmp"; then
  echo "  OK   a repeat capture of the same moment is byte-identical ($(stat -c %s "$OUT/animated2.bmp") bytes)"
else
  say_fail "the repeat capture differs from the first: the frame is not reproducible, so the on/off pair is not a controlled A-B"
fi

[ "$cov_view" = "entity-coverage" ] && \
  echo "  OK   coverage frame really is the entity-coverage view" || \
  say_fail "coverage frame view='${cov_view:-<none>}', expected entity-coverage (the mask would belong to another pass)"
[ "$on_view" = "textured" ] && \
  echo "  OK   animated frame is the product view" || \
  say_fail "animated frame view='${on_view:-<none>}', expected the product view 'textured'"
[ "$off_view" = "textured" ] && \
  echo "  OK   off frame is the product view" || \
  say_fail "off frame view='${off_view:-<none>}', expected the product view 'textured'"

# The A-B is only an A-B if both frames describe the same moment. Two
# independent readings prove it, and both must be present -- an empty reading is a
# failure, not a match, because two missing values compare equal.
if [ -n "$on_ft" ] && [ "$on_ft" = "$off_ft" ] && [ "$on_ft" = "$TICK" ]; then
  echo "  OK   both runs captured the requested playback tick $TICK (frame_tick=$on_ft)"
else
  say_fail "frame_tick differs or is missing: animated='${on_ft:-<none>}' off='${off_ft:-<none>}' requested=$TICK"
fi
if [ -n "$on_st" ] && [ "$on_st" = "$off_st" ]; then
  echo "  OK   both runs resolved the same scene tick $on_st"
else
  say_fail "scene tick differs or is missing: animated='${on_st:-<none>}' off='${off_st:-<none>}': the A-B compares different moments"
fi
# The drive must actually have run in the animated capture: animsum is published
# only by the drive, so its absence means --player-animation reached nothing and
# the on/off pair below would be comparing two undriven frames.
if grep -q 'animsum: tick=' "$OUT/animated.summary" 2>/dev/null; then
  echo "  OK   the animated capture published an animsum line (the drive ran)"
else
  say_fail "the animated capture published no animsum line: the drive did not run, so the on/off pair proves nothing"
fi

for bmp in coverage animated off; do
  if [ -s "$OUT/$bmp.bmp" ]; then
    echo "  OK   $bmp.bmp exists ($(stat -c %s "$OUT/$bmp.bmp") bytes)"
  else
    say_fail "$bmp.bmp is missing or empty"
  fi
done

# ===========================================================================
echo
echo "=== F3/F4: the pixel assertions themselves ==="
PIXEL_LOG="$OUT/pixel-check.txt"
"$PY" t61-pixels.py "$OUT/coverage.bmp" "$OUT/animated.bmp" --off "$OUT/off.bmp" > "$PIXEL_LOG" 2>&1
PIXEL_RC=$?
sed 's/^/  /' "$PIXEL_LOG"
if [ "$PIXEL_RC" != "0" ]; then
  say_fail "t61-pixels.py exited $PIXEL_RC (see $PIXEL_LOG)"
fi
# The tool's own verdict must be present: an empty log would otherwise read as
# "no FAIL lines", which is the vacuous-pass shape this gate exists to avoid.
if grep -q '^PIXEL-CHECK=PASS$' "$PIXEL_LOG"; then
  echo "  OK   t61-pixels.py printed PIXEL-CHECK=PASS"
else
  say_fail "t61-pixels.py did not print PIXEL-CHECK=PASS (a missing verdict is not a pass)"
fi
bad=$(grep -c '^FAIL ' "$PIXEL_LOG" 2>/dev/null || true)
if [ "${bad:-0}" = "0" ]; then
  echo "  OK   no FAIL lines from the pixel tool"
else
  say_fail "$bad FAIL line(s) from the pixel tool"
fi

echo
if [ "$fail" = "0" ]; then
  echo "PASS player-animation-pixels: the entity pass's measured mask is a shaded body (distinct colours, luminance, black share) and the animation drive moves a measured share of those pixels between the on and off frames of the same scene tick."
else
  echo "FAIL player-animation-pixels: see the FAIL lines above."
fi
exit "$fail"
