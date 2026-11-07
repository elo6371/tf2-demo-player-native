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
# The point is not tidiness. What could NOT be reproduced across processes was
# every CUMULATIVE counter in the trace -- boneMismatch, cycle, poseChanges: the
# replay advances by wall-clock time (src/main.cpp:454, kTargetFrameSeconds =
# 1/120) so each process draws a different number of frames before it reaches
# the capture tick (5607..7807 observed on this demo, at SPEED=8), and a
# per-frame counter scales with that count (boneMismatch = 4.860469 * frames -
# 89.862, R^2 = 0.999731 over ten runs). A gate that published a pixel count
# without asserting the bytes would be one relink away from reading frame pacing
# as a rendering difference.
#
# ANIMATION PHASE (learned the hard way on 2026-11-06)
# The FRAME is reproducible, but not as one value: for a MOVING body it has two
# legal branches, and which one a run lands on is decided by the animation clock,
# not by the renderer. native/src/main.cpp:9827-9836 builds speed from
# distance/elapsedTicks and src/animation_clock.cpp:110/123 multiplies it by dt;
# the two tick terms cancel, leaving
#     delta = |origin delta| / strideUnits        (strideUnits = 250,
#                                                  include/animation_clock.h:52-62)
# so cycle accumulates the LENGTH OF THE POLYLINE the origin was sampled along,
# and the polyline is cut into as many segments as the wall clock drew frames.
# Rendering one intermediate snapshot at tick 74436 before the capture tick 74481
# gives cycle 0.005392 (branch A, Scout identity 1161 px, sha 7317b7a2...);
# skipping straight from 73961 to 74481 gives 0.879967 (branch B, 1211 px, sha
# 00e30490...). The 0.125426 cycle difference IS the extra segment. Every frame
# in each branch is byte-identical across processes and across days; the two
# branches differ only in pose (474 px, all inside the body bbox, best integer
# translation explains none of it).
#
# maxDeltaCycle in the animsum line is the fingerprint of the branch (0.838676/
# 9.315151 for the branches above, an 11x gap), it is already in every summary,
# and it is not a cumulative counter, so this gate pins each row to ONE phase:
# identity picks the phase, every other capture in the row is retried until it
# lands there, and a pair that still disagrees about phase fails as "not a
# reading" rather than as "the renderer is non-deterministic". A frame from
# branch A may be quoted inside branch A; a pixel count of branch A may NOT be
# compared with one from branch B. SPEED=64 is not a legal capture speed: it
# steps several demo ticks per frame and always lands on branch B.
#
# If the "not byte-identical inside one phase" branch ever fires, the captured
# frame is no longer a function of (demo, tick, camera, flags) and NO pixel
# count from that run may be quoted until the cause is named.
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

# --- ANIMATION PHASE: the quantity that decides whether two frames are comparable
# A pixel count is only a reading when the SAME request in a second process lands
# on the same frame. It does not always: the playback advances on wall-clock time
# (native/src/main.cpp, clamp 0..0.25 s per step) and the animation clock advances
# by the chord length of however many steps that produced
# (delta = |dOrigin| / strideUnits, native/src/animation_clock.cpp), so one request
# can land on one of exactly two phases. Measured 2026-11-06 on this demo (scout,
# tick 64161, speed 32, 5 runs): phase A maxDeltaCycle=0.838676 / sha 7317b7a2 and
# phase B maxDeltaCycle=9.315151 / sha 00e30490, each stable inside itself across
# days and relinks; the 64x playback speed lands on B only, 4x-32x on A almost
# always. A pair split across the two phases differs by 474 px of POSE, not of
# geometry (all inside the body bbox, IoU 0.67, no integer translation fits), so
# calling it a non-deterministic renderer is a false red and a reader would go
# looking in the wrong file. The pair is therefore retried (bounded) until both
# captures sit in ONE phase, and only a same-phase mismatch is a failure.
PHASE_ATTEMPTS=${T62_CLASS_PHASE_ATTEMPTS:-4}
phase_of_summary() {
  local f="$1"
  [ -f "$f" ] || return 0
  grep -a 'animsum:' "$f" 2>/dev/null | tail -1 | grep -o 'maxDeltaCycle=[0-9.]*' | tail -1
}
phase_of() { phase_of_summary "$1/$2.summary"; }
want_phase() {
  case "${1:-}" in
    maxDeltaCycle=*) printf '%s' "$1" ;;
    *)               printf '%s' "-" ;;
  esac
}

# run_frame_until_phase <dir> <tag> <wantPhase|-> <eye...> <flags...>
# Same request, repeated in a NEW process until it lands on <wantPhase> or the
# budget runs out. Leaves the last attempt's files in <dir>; prints one line:
#   rc view frameTick sceneTick phase attempts
run_frame_until_phase() {
  local dir="$1" tag="$2" want="$3"; shift 3
  local attempt=1 out rc view ft st ph
  while : ; do
    out=$(run_frame "$dir" "$tag" "$@")
    read -r rc view ft st <<<"$out"
    ph=$(phase_of "$dir" "$tag")
    [ "$want" = "-" ] && break
    [ "$ph" = "$want" ] && break
    [ "$attempt" -ge "$PHASE_ATTEMPTS" ] && break
    attempt=$((attempt + 1))
    echo "  RETRY $tag: landed ${ph:-<no animsum line>}, wanted $want (attempt $attempt of $PHASE_ATTEMPTS)" >&2
  done
  printf '%s %s %s %s %s %s\n' "${rc:-1}" "${view:-<none>}" "${ft:-<none>}" "${st:-<none>}" "${ph:-<none>}" "$attempt"
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

  # The identity pair is the one that has to agree, so IT leads: its first
  # capture fixes the phase this row is read in and the second capture is retried
  # until it lands there. animated has no determinism claim of its own (it is a
  # single capture), but it is pinned to the same phase anyway so the row
  # describes one moment of the recording rather than two.
  read -r id_rc id_view id_ft id_st id_ph id_try < <(run_frame_until_phase "$dir" identity - "$eyeX" "$eyeY" "$eyeZ" --player-animation --world-entity-id)
  read -r on_rc on_view on_ft on_st on_ph on_try < <(run_frame_until_phase "$dir" animated "$(want_phase "$id_ph")" "$eyeX" "$eyeY" "$eyeZ" --player-animation)
  # off is deliberately NOT pinned to a phase, and it cannot be: it is the frame
  # with the animation drive flag ABSENT, so the replay never runs the animation
  # clock and never prints the 'animsum:' line the phase is read from. Asking for
  # a phase here is a request that can never be satisfied -- it burns all
  # PHASE_ATTEMPTS runs of an 8 minute capture and then reds the row for a
  # quantity the pass does not have. Measured 2026-11-06: 'grep -c animsum:' is 0
  # for every off.summary in both committed baselines (4 snakewater + 5 upward)
  # and 1 for every identity/animated one. off is still compared to the other
  # passes inside the row -- same request, same tick, same scene tick -- which is
  # what the flag-inert assertions need; it just has no phase to be pinned to.
  read -r off_rc off_view off_ft off_st off_ph off_try < <(run_frame_until_phase "$dir" off - "$eyeX" "$eyeY" "$eyeZ")

  # The SAME request a second time, in a second process: this pair is what turns
  # the pixel count below from "a frame we got" into "the frame the request
  # names". Identical arguments, identical lock, different process.
  read -r id2_rc id2_view id2_ft id2_st id2_ph id2_try < <(run_frame_until_phase "$dir" identity-2 "$(want_phase "$id_ph")" "$eyeX" "$eyeY" "$eyeZ" --player-animation --world-entity-id)
  echo "  captures animated=$on_rc/${on_view}/${on_ft}/${on_st} off=$off_rc/${off_view}/${off_ft}/${off_st}"\
       " identity=$id_rc/${id_view}/${id_ft}/${id_st}"\
       " identity-2=$id2_rc/${id2_view}/${id2_ft}/${id2_st}"
  echo "  phase identity=${id_ph:-<none>}(${id_try:-?}x) identity-2=${id2_ph:-<none>}(${id2_try:-?}x)"\
       " animated=${on_ph:-<none>}(${on_try:-?}x) off=n/a(no animation drive, see the capture block)"

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
    echo "  OK   the identity capture twice is byte-identical ($ID_SHA) in phase ${id_ph:-<none>}"
  elif [ -n "${id_ph:-}" ] && [ "$id_ph" != "-" ] && [ "$id_ph" != "${id2_ph:-}" ]; then
    # Two captures in two DIFFERENT phases are not a rendering difference: they
    # are two moments of the animation (see the ANIMATION PHASE block above --
    # 474 px of pose, all inside the body bbox, on the two branches of the same
    # request). Reporting this as non-determinism would send a reader into the
    # renderer for something that lives in the animation clock. It is still a
    # failure, because the count below cannot be quoted across runs, and the
    # retry budget is what should have prevented it.
    say_fail "${CLASS}: the two identity captures landed on different animation phases (identity=$id_ph/$ID_SHA identity-2=${id2_ph:-?}/$ID2_SHA) and the retry budget (${PHASE_ATTEMPTS}x) ran out, so a cross-run pixel count cannot be a reading; the pair differs by pose, not by rendering"
  else
    # Same phase, different bytes: this IS a rendering difference and the
    # failure text has to name the right one. The summary keeps this frame's
    # entity census ("entity: id=" lines), so the two lists can be diffed and the
    # difference distinguishes the two causes that look alike from outside.
    # Measured on 2026-11-05 (freeze4 step class-snakewater): the pair differed
    # by 474 px and the census differed by ONE body -- a medic with an origin in
    # one run and not in the other -- which renumbered every body after it (demo
    # id 95 vs 94). That is a different scene. The other cause is a renderer that
    # draws one scene two ways; the census then matches and only pixels differ.
    # The phase, not the census, is what made the earlier reading of this pair
    # ambiguous: on 2026-11-06 the same demo split on maxDeltaCycle while both
    # censuses carried 96 entity lines, so a census diff alone cannot be the
    # discriminator -- which is why the branch above runs first.
    census_diff=$(diff <(grep -a 'entity: id=' "$dir/identity.summary" | cut -d' ' -f2-) \
                       <(grep -a 'entity: id=' "$dir/identity-2.summary" | cut -d' ' -f2-) | head -6)
    if [ -n "$census_diff" ]; then
      say_fail "${CLASS}: the two identity captures do not show the same SCENE (identity=$ID_SHA identity-2=$ID2_SHA); the entity census differs, so this is not a rendering difference:"
      printf '%s\n' "$census_diff" | sed 's/^/    /'
    else
      say_fail "${CLASS}: the same identity capture twice is not byte-identical inside one phase (identity=$ID_SHA identity-2=$ID2_SHA, phase=${id_ph:-?}), so a cross-run pixel count cannot be a reading; the entity census is identical, so the same scene is being rendered two different ways"
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
    # The phase each frame was taken in, next to the bytes it hashed to: two
    # frames from one phase are one reading, and a later reader comparing this
    # row with another one can see whether the comparison is legal at all.
    # off has no phase of its own: without --player-animation the animation clock
    # never runs, so there is no 'animsum:' line to read one from (see the
    # capture block above). The field is written as "n/a" so a later reader does
    # not mistake an empty value for a phase that failed to be recorded.
    echo "frame: phase identity=${id_ph:-<none>} identity-2=${id2_ph:-<none>} animated=${on_ph:-<none>} off=n/a"
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
