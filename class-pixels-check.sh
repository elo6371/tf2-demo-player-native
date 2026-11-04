#!/usr/bin/env bash
# class-pixels-check.sh -- T62: the animation drive reaches the pixels of MORE
# THAN ONE CLASS.
#
# WHY THIS EXISTS
# player-animation-pixels.sh (T61 section F) proves the chain model -> pick ->
# sample -> pixels for ONE body: the Demoman at tick 64161 of the Snakewater
# recording, because the hand-chosen camera happens to frame him and nothing
# else. evidence/t62-matrix/CHARACTER-MATRIX.md could therefore mark only that
# one class on the pixel row. This script repeats the same chain for every class
# the recording actually contains as a player, with the camera derived from the
# body's own origin so the framing is reproducible instead of hand-tuned.
#
# HOW THE CAMERA IS DERIVED (and why that is legitimate)
# The reference camera (--debug-camera 5363.8 -1102.1 5.9 4.1 0.0) frames the
# Demoman whose entity line reports origin=5613.8,-1102.1,-52.1 at the same
# tick. The eye is therefore (-250, 0, +58) from that body's origin, yaw 0 (the
# map's +X axis) and pitch 4.1. The identical offset is applied to each class
# origin, so every class is measured through the SAME lens geometry as the
# reference frame -- not through a per-class camera tuned until it looked good.
#
# TARGETS FILE
# ${OUT}/targets.txt holds one `class ent tick ox oy oz` line per class: the
# moving player body the recording offers for that class AT THAT TICK, and the
# origin the same tick's capture reported for it. It is an INPUT (where to point
# the camera and which frame to land on), not a result: the script fails if the
# id frame refuses to draw that body, so a stale target cannot silently pass.
# The tick is per line because a class's body can leave the recording before
# another's: the Demoman and the Scout are still drawn at 64161, the Medic is
# not, and forcing one tick would silently shrink the evidence to two classes.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it launches captures):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t62-class \
#     bash -c 'cd /d/TF2_Native_Worktrees/t79-integration && bash class-pixels-check.sh'
#
# Usage: bash class-pixels-check.sh
# Exit:  0 = PASS, 1 = FAIL, 2 = a required input is missing (FATAL).
set -uo pipefail
cd "$(dirname "$0")"

OUT=${T62_CLASS_OUT:-evidence/t62-matrix/class-pixels}
PRODUCT=${T62_CLASS_PRODUCT:-native/build-nmake/tf2_demo_native.exe}
PY=${T62_CLASS_PYTHON:-C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe}
TF=${T62_CLASS_TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${T62_CLASS_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}
SPEED=${T62_CLASS_SPEED:-8}
CAMERA_OFFSET_X=-250
CAMERA_OFFSET_Z=58
CAMERA_PITCH=4.1
CAMERA_YAW=0.0
MIN_BODY_PIXELS=200

fail=0
say_fail() { echo "  FAIL $*"; fail=1; }

for path in "$PRODUCT" "$TF" "$DEMO" "$PY" "$OUT/targets.txt"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
[ -x "$PRODUCT" ] || { echo "FATAL: not executable: $PRODUCT"; exit 2; }

# run_frame <dir> <tag> <eye...> <flag...>
# Every argument except the tag and the flag list is identical across the three
# passes, so the only difference between the animated and the off frame is the
# drive flag -- the same construction player-animation-pixels.sh uses.
run_frame() {
  local dir="$1" tag="$2" eyeX="$3" eyeY="$4" eyeZ="$5"; shift 5
  local bmp="$dir/$tag.bmp" trace="$dir/$tag.trace"
  rm -f "$bmp" "$trace"
  TF2_NATIVE_TRACE="$trace" timeout 1200 "$PRODUCT" \
    --tf-root "$TF" --hidden-window --audio-disabled \
    --settings-file "$OUT/settings.json" --ui-settings-file "$OUT/ui.json" \
    --demo "$DEMO" --capture-tick "$TICK" --playback-speed "$SPEED" \
    --capture-frame "$bmp" \
    --debug-camera "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" "$CAMERA_YAW" \
    "$@" > "$dir/$tag.log" 2>&1
  local rc=$?
  local view frameTick sceneTick
  view=$(grep -a -o 'gpu: world debug view=[a-z-]*' "$trace" 2>/dev/null | tail -1 | sed 's/.*view=//')
  frameTick=$(grep -a -o 'capture: armed requested=[0-9]* frame_tick=[0-9]*' "$trace" 2>/dev/null | tail -1 | sed 's/.*frame_tick=//')
  sceneTick=$(grep -a 'scene: anim ' "$trace" 2>/dev/null | tail -1 | grep -o 'tick=[0-9]*' | tail -1 | sed 's/tick=//')
  {
    echo "tag=$tag rc=$rc view=$view frameTick=$frameTick sceneTick=$sceneTick"
    grep -a 'animsum:' "$trace" 2>/dev/null | tail -1
    grep -a 'anim: pick ' "$trace" 2>/dev/null
    grep -a 'anim: ent=' "$trace" 2>/dev/null | sed -E 's/^[0-9.]+ //' \
      | sed -E 's/ (speed|rate|cycle|delta|advanced|origin)=[^ ]*//g' | sort -u
    grep -a 'entity: id=' "$trace" 2>/dev/null
    grep -a 'gpu: world debug view=' "$trace" 2>/dev/null | tail -1
    grep -a 'capture: armed' "$trace" 2>/dev/null | tail -1
    grep -a 'scene: anim ' "$trace" 2>/dev/null | tail -1
  } > "$dir/$tag.summary"
  rm -f "$trace"
  printf '%s %s %s %s\n' "${rc:-1}" "${view:-<none>}" "${frameTick:-<none>}" "${sceneTick:-<none>}"
}

# ===========================================================================
echo "=== T62 class pixel chain: one class at a time, framed from its own origin ==="
echo "  demo=$DEMO"
echo "  ticks=per-target (see targets.txt) speed=$SPEED camera-offset=(${CAMERA_OFFSET_X},0,${CAMERA_OFFSET_Z})"\
     " pitch=$CAMERA_PITCH yaw=$CAMERA_YAW"
echo

while read -r CLASS ENT TICK ORIGIN_X ORIGIN_Y ORIGIN_Z; do
  case "$CLASS" in ''|'#'*) continue;; esac
  model="models/player/${CLASS}.mdl"
  dir="$OUT/${CLASS}"
  mkdir -p "$dir"
  eyeX=$(awk -v x="$ORIGIN_X" -v d="$CAMERA_OFFSET_X" 'BEGIN { printf "%.1f", x + d }')
  eyeY="$ORIGIN_Y"
  eyeZ=$(awk -v z="$ORIGIN_Z" -v d="$CAMERA_OFFSET_Z" 'BEGIN { printf "%.1f", z + d }')
  echo "--- ${CLASS}: ent=$ENT tick=$TICK origin=${ORIGIN_X},${ORIGIN_Y},${ORIGIN_Z}"\
       " eye=${eyeX},${eyeY},${eyeZ} model=$model"

  read -r on_rc on_view on_ft on_st < <(run_frame "$dir" animated "$eyeX" "$eyeY" "$eyeZ" --player-animation)
  read -r off_rc off_view off_ft off_st < <(run_frame "$dir" off "$eyeX" "$eyeY" "$eyeZ")
  read -r id_rc id_view id_ft id_st < <(run_frame "$dir" identity "$eyeX" "$eyeY" "$eyeZ" --player-animation --world-entity-id)
  echo "  captures animated=$on_rc/${on_view}/${on_ft}/${on_st} off=$off_rc/${off_view}/${off_ft}/${off_st}"\
       " identity=$id_rc/${id_view}/${id_ft}/${id_st}"

  [ "$on_rc" = "0" ] || say_fail "${CLASS}: animated capture exited $on_rc"
  [ "$off_rc" = "0" ] || say_fail "${CLASS}: off capture exited $off_rc"
  [ "$id_rc" = "0" ] || say_fail "${CLASS}: identity capture exited $id_rc"
  [ "$id_view" = "entity-id" ] || say_fail "${CLASS}: identity frame reports view=${id_view:-<none>}, not entity-id"
  [ "$id_ft" = "$TICK" ] || say_fail "${CLASS}: identity frame landed tick ${id_ft:-<none>}, not $TICK"
  [ -n "$on_ft" ] && [ "$on_ft" = "$off_ft" ] && [ "$on_ft" = "$TICK" ] \
    || say_fail "${CLASS}: animated/off frame ticks differ or miss $TICK (on=${on_ft:-<none>} off=${off_ft:-<none>})"
  [ -n "$on_st" ] && [ "$on_st" = "$off_st" ] && [ "$on_st" = "$id_st" ] \
    || say_fail "${CLASS}: the three passes resolved different scene ticks (on=${on_st:-<none>} off=${off_st:-<none>} id=${id_st:-<none>})"

  "$PY" t61-entityid.py "$dir/identity.bmp" "$dir/identity.summary" \
    "$dir/animated.bmp" "$dir/off.bmp" > "$dir/entity-id.txt" 2>&1
  ck_rc=$?
  echo "  entity-id rc=$ck_rc"
  if [ "$ck_rc" != "0" ]; then
    say_fail "${CLASS}: the T61 F5 chain did not pass on this frame:"
    sed 's/^/    /' "$dir/entity-id.txt" | tail -20
  fi
  subject=$(grep -a '^SUBJECT ' "$dir/entity-id.txt" | tail -1)
  if printf '%s' "$subject" | grep -q "model=$model "; then
    echo "  OK   the judged subject IS a $CLASS body: $subject"
  else
    say_fail "${CLASS}: the largest body on screen is not this class, so the frame does not judge it: ${subject:-<no SUBJECT line>}"
  fi
  # Match the body by ENTITY, not only by class: the Demoman and the Medic both
  # have a second body on screen at tick 62000, and a class-only match would
  # quote the other instance's pixels as if they were this target's.
  body=$(grep -a -E "^BODY id=[0-9]+ ent=$ENT .*model=$model " "$dir/entity-id.txt" | tail -1)
  if [ -z "$body" ]; then
    echo "  NOTE no BODY line names ent=$ENT of ${CLASS}; falling back to a class match"
    body=$(grep -a -E "^BODY id=[0-9]+ .*model=$model " "$dir/entity-id.txt" | tail -1)
  fi
  if [ -n "$body" ]; then
    px=$(printf '%s' "$body" | grep -o 'pixels=[0-9]*' | head -1 | sed 's/pixels=//')
    share=$(printf '%s' "$body" | grep -o 'share=[0-9.]*%' | head -1 | sed 's/share=//')
    echo "  BODY $body"
    if [ "${px:-0}" -ge "$MIN_BODY_PIXELS" ]; then
      echo "  OK   $CLASS body has $px pixels on screen (>= $MIN_BODY_PIXELS) and moved $share"
    else
      say_fail "${CLASS}: body mask ${px:-0} < $MIN_BODY_PIXELS pixels; too small to be evidence"
    fi
  else
    say_fail "${CLASS}: no BODY line names $model in the id frame"
  fi
  echo
done < "$OUT/targets.txt"

echo "==========================================================================="
if [ "$fail" = "0" ]; then
  echo "CLASS-PIXELS=PASS"
else
  echo "CLASS-PIXELS=FAIL (see the FAIL lines above)"
fi
exit "$fail"
