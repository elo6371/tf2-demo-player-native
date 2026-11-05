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
# DETERMINISM: THE IDENTITY FRAME IS CAPTURED TWICE AND MUST BE BYTE-IDENTICAL
# The reading this gate publishes is a PIXEL COUNT, and a count is only a
# reading if the frame that was counted is the frame the gate asked for. Every
# target therefore captures the identity frame TWICE, in two separate processes
# with identical arguments, and the script fails unless the two BMPs hash the
# same -- the construction T61 uses for its animated/animated2 pair.
#
# The point is not tidiness. On this tree the same request reproduced a
# byte-identical frame from independent processes over nine hours: the Scout at
# tick 64161 (ent=15, eye 2492.7,-1327.1,-54.0, pitch 4.1, yaw 0.0) hashed
# 7317b7a28afedbb129d9e564ed4c0f7aa1bb3e6497cd80a98471f73205f0e423 six times
# across four driver generations. What could NOT be reproduced across processes
# was every CUMULATIVE counter in the same trace -- boneMismatch, cycle,
# poseChanges: the replay advances by wall-clock time (src/main.cpp:454,
# kTargetFrameSeconds = 1/120) so each process draws a different number of
# frames before it reaches the capture tick (5607..7807 observed on this demo,
# at SPEED=8), and a per-frame counter scales with that count (boneMismatch =
# 4.860469 * frames - 89.862, R^2 = 0.999731 over ten runs). A gate that
# published a pixel count without asserting the bytes would be one relink away
# from reading frame pacing as a rendering difference.
#
# If this assertion ever fires, the captured frame is no longer a function of
# (demo, tick, camera, flags) and NO pixel count from that run may be quoted
# until the cause is named.
#
# PROVENANCE
# The sha256 of all four frames and of the product executable are appended as
# the LAST lines of that target's <tag>.summary and of entity-id.txt, so a
# published count can always be traced to the exact bytes and the exact
# executable behind it. They are appended AFTER the fixed block and carry none
# of the markers t61-entityid.py parses ('entity: id=', 'anim: pick model=',
# 'anim: ent='), so every existing reader keeps working unchanged.
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

# sha_of <path> -- the whole reason this gate can claim a count is a reading.
# Prints the file's sha256, or <none> when the file is absent, so a missing
# capture shows up in the provenance lines instead of hashing an empty string.
sha_of() {
  local f="$1"
  [ -f "$f" ] || { echo "<none>"; return 0; }
  sha256sum "$f" 2>/dev/null | cut -d' ' -f1
}

for path in "$PRODUCT" "$TF" "$DEMO" "$PY" "$OUT/targets.txt"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
[ -x "$PRODUCT" ] || { echo "FATAL: not executable: $PRODUCT"; exit 2; }

# The executable is part of the reading, not scenery: the pixel chain has been
# reproduced across relinks on this tree, so a count is only comparable when the
# binary behind it is named. Every target's provenance block carries this value.
PRODUCT_SHA=$(sha_of "$PRODUCT")

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
  # Provenance, APPENDED after the fixed block (never inside it): the summary
  # above is a stable format that other readers parse, and these lines carry
  # none of the markers t61-entityid.py looks for, so nothing downstream moves.
  {
    echo "frame: bmp=$tag.bmp sha256=$(sha_of "$bmp")"
    echo "frame: product sha256=$PRODUCT_SHA"
    echo "frame: request demo=$DEMO tick=$TICK speed=$SPEED camera=$eyeX,$eyeY,$eyeZ,$CAMERA_PITCH,$CAMERA_YAW flags=${*:-<none>}"
  } >> "$dir/$tag.summary"
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
  # The SAME request a second time, in a second process: this pair is what turns
  # the pixel count below from "a frame we got" into "the frame the request
  # names". Identical arguments, identical lock, different process.
  read -r id2_rc id2_view id2_ft id2_st < <(run_frame "$dir" identity-2 "$eyeX" "$eyeY" "$eyeZ" --player-animation --world-entity-id)
  echo "  captures animated=$on_rc/${on_view}/${on_ft}/${on_st} off=$off_rc/${off_view}/${off_ft}/${off_st}"\
       " identity=$id_rc/${id_view}/${id_ft}/${id_st}"\
       " identity-2=$id2_rc/${id2_view}/${id2_ft}/${id2_st}"

  [ "$on_rc" = "0" ] || say_fail "${CLASS}: animated capture exited $on_rc"
  [ "$off_rc" = "0" ] || say_fail "${CLASS}: off capture exited $off_rc"
  [ "$id_rc" = "0" ] || say_fail "${CLASS}: identity capture exited $id_rc"
  [ "$id_view" = "entity-id" ] || say_fail "${CLASS}: identity frame reports view=${id_view:-<none>}, not entity-id"
  [ "$id_ft" = "$TICK" ] || say_fail "${CLASS}: identity frame landed tick ${id_ft:-<none>}, not $TICK"
  [ -n "$on_ft" ] && [ "$on_ft" = "$off_ft" ] && [ "$on_ft" = "$TICK" ] \
    || say_fail "${CLASS}: animated/off frame ticks differ or miss $TICK (on=${on_ft:-<none>} off=${off_ft:-<none>})"
  [ "$id2_rc" = "0" ] || say_fail "${CLASS}: the second identity capture exited $id2_rc"
  [ "$id2_view" = "entity-id" ] || say_fail "${CLASS}: the second identity frame reports view=${id2_view:-<none>}, not entity-id"
  [ "$id2_ft" = "$TICK" ] || say_fail "${CLASS}: the second identity frame landed tick ${id2_ft:-<none>}, not $TICK"
  [ -n "$on_st" ] && [ "$on_st" = "$off_st" ] && [ "$on_st" = "$id_st" ] \
    || say_fail "${CLASS}: the three passes resolved different scene ticks (on=${on_st:-<none>} off=${off_st:-<none>} id=${id_st:-<none>})"

  # --- DETERMINISM: two processes, one request, one answer -----------------
  # This is the assertion that makes every number after it quotable. Without
  # it the gate publishes a count from "a frame we happened to get"; with it the
  # count is a property of (demo, tick, camera, flags) and of nothing else. The
  # wall-clock replay is why this is not free: src/main.cpp:454 advances the
  # playback by wall-clock time, so the number of frames rendered before the
  # capture tick differs per process -- harmless for the frame (it is pinned to
  # the tick) and fatal for every cumulative counter in the same trace.
  ID_SHA=$(sha_of "$dir/identity.bmp")
  ID2_SHA=$(sha_of "$dir/identity-2.bmp")
  ON_SHA=$(sha_of "$dir/animated.bmp")
  OFF_SHA=$(sha_of "$dir/off.bmp")
  echo "  sha256 identity=$ID_SHA"
  echo "  sha256 identity-2=$ID2_SHA"
  echo "  sha256 animated=$ON_SHA"
  echo "  sha256 off=$OFF_SHA"
  echo "  sha256 product=$PRODUCT_SHA"
  if [ "$ID_SHA" != "<none>" ] && [ "$ID_SHA" = "$ID2_SHA" ]; then
    echo "  OK   the identity capture twice is byte-identical ($ID_SHA)"
  else
    # A differing pair is a failure either way, but the two causes are different
    # findings and the failure text has to say which one happened. The summary
    # keeps this frame's entity census ("entity: id=" lines), so the two plants
    # can be compared directly. Measured on 2026-11-05 (freeze4 step
    # class-snakewater): the pair differed by 474 px and the census differed by
    # ONE body -- a medic that had an origin in one run and not in the other --
    # which renumbered every body after it (demo id 95 vs 94). That is not a
    # pixel difference, it is a different scene, and a reader who is told "the
    # picture moved" would go looking in the renderer for it.
    census_diff=$(diff <(grep -a 'entity: id=' "$dir/identity.summary" | cut -d' ' -f2-) \
                       <(grep -a 'entity: id=' "$dir/identity-2.summary" | cut -d' ' -f2-) | head -6)
    if [ -n "$census_diff" ]; then
      say_fail "${CLASS}: the two identity captures do not show the same SCENE (identity=$ID_SHA identity-2=$ID2_SHA); the entity census differs, so this is not a rendering difference:"
      printf '%s\n' "$census_diff" | sed 's/^/    /'
    else
      say_fail "${CLASS}: the same identity capture twice is not byte-identical, so a cross-run pixel count cannot be a reading (identity=$ID_SHA identity-2=$ID2_SHA); the entity census is identical, so the difference is in the rendering of one scene"
    fi
  fi

  "$PY" t61-entityid.py "$dir/identity.bmp" "$dir/identity.summary" \
    "$dir/animated.bmp" "$dir/off.bmp" > "$dir/entity-id.txt" 2>&1
  ck_rc=$?
  echo "  entity-id rc=$ck_rc"
  if [ "$ck_rc" != "0" ]; then
    say_fail "${CLASS}: the T61 F5 chain did not pass on this frame:"
    sed 's/^/    /' "$dir/entity-id.txt" | tail -20
  fi
  # Provenance is APPENDED to the report, never interleaved with it: the python
  # owns entity-id.txt above and the shell only adds lines at the end. They all
  # start with "frame: ", which no reader of this file matches (the gate itself
  # greps ^SUBJECT / ^BODY, t61-entityid.py never re-reads the file), so the
  # appended block cannot change a verdict.
  {
    echo "frame: class=$CLASS ent=$ENT tick=$TICK"
    echo "frame: bmp animated sha256=$ON_SHA"
    echo "frame: bmp identity sha256=$ID_SHA"
    echo "frame: bmp identity-2 sha256=$ID2_SHA"
    echo "frame: bmp off sha256=$OFF_SHA"
    echo "frame: identity capture twice byte-identical=$([ -n "$ID_SHA" ] && [ "$ID_SHA" = "$ID2_SHA" ] && echo yes || echo no)"
    echo "frame: product sha256=$PRODUCT_SHA"
    echo "frame: request demo=$DEMO"
    echo "frame: request tick=$TICK speed=$SPEED"
    echo "frame: request camera=$eyeX,$eyeY,$eyeZ,$CAMERA_PITCH,$CAMERA_YAW"
    echo "frame: request flags=--player-animation --world-entity-id"
  } >> "$dir/entity-id.txt"
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
