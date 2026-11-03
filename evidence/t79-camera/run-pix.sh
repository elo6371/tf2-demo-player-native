#!/usr/bin/env bash
# R vs D vs D2 pixel evidence for the spectate camera.
#
# WHAT THE THREE RUNS ARE
#   R  the product's own spectate camera.
#   D  --debug-camera pinned to the SAME world eye R used, so the real frame and
#      a diagnostic frame should produce the same picture through two paths.
#   D2 --debug-camera at a far offset: the control proving D is not accidentally
#      still looking through R's camera.
#
# WHY D IS FED A PROBED POSE INSTEAD OF THE TRACE NUMBERS
#   The earlier version replayed D with the eye the trace printed. "scene: camera"
#   prints eye at %.1f and angles at %.2f, and on a real map those two decimals
#   are worth a visible pixel count: measured on koth_bagel_rc11 tick 900, the
#   rounded-eye replay differs from R by 6127 px while the exact-eye replay
#   differs by 120 px. A fixture that cannot separate a rounding artefact from a
#   638-pixel regression is not measuring the camera. So D now reads the followed
#   player's transform at full precision from entity_model_probe -- the same
#   EntityModelResolver::extractTransform the playback loop calls, so this is the
#   product's own reading, not a second implementation of it.
#
# WHY THE FRAME TICK IS DERIVED AND NOT ASSUMED
#   A captured frame is built from a snapshot, and the snapshot is taken at the
#   last tick the packet stream had reached minus a staleness backoff. Probing
#   --capture-tick directly reads a different instant and would feed D the pose of
#   a moment nobody rendered. The resolved tick is read from the trace and
#   cross-checked against a second, independent trace line.
#
# WHAT THIS SCRIPT DOES NOT DO
#   It does not touch the renderer and it does not work around T84. Diagnostic
#   frames deliberately keep the followed body (native/src/main.cpp:10036-10043),
#   so a tick where that body is inside the frame cannot serve as an R/D fixture:
#   the pictures are then legitimately different and the comparison has no
#   subject. Which ticks are usable is a fixture question recorded alongside the
#   readings; this script measures the tick it is given.
#
# Keep every run in a new caller-provided directory: existing evidence is immutable.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/entity_model_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
# No default: every run must name a fresh directory, because the ones already
# committed (camfix2 = snakewater, rc11-20261103 = rc11) are evidence.
OUT=""
TICK=2000
# B4.4 acceptance numbers, quoted from the handover so that the reason a run can
# fail lives in this file and not only in a document:
#   R vs D  >8 delta stays "a few hundred pixels, <= 0.1%" -- the cameras agree.
#   R vs D2 >8 delta is >= 90% -- the far camera really is a different picture.
MAX_PCT_RD="0.1"
MIN_PCT_RD2="90"
# How far the trace's %.1f / %.2f rounding may sit from the pose fed in.
EYE_TOLERANCE="0.06"
ANGLE_TOLERANCE="0.006"

usage() {
  echo "Usage: bash evidence/t79-camera/run-pix.sh [--demo PATH] [--tick N] [--out DIR]" >&2
}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --demo) [ "$#" -ge 2 ] || { usage; exit 2; }; DEMO="$2"; shift 2 ;;
    --tick) [ "$#" -ge 2 ] || { usage; exit 2; }; TICK="$2"; shift 2 ;;
    --out) [ "$#" -ge 2 ] || { usage; exit 2; }; OUT="$2"; shift 2 ;;
    *) usage; exit 2 ;;
  esac
done
case "$TICK" in ''|*[!0-9]*) echo "FAIL tick must be a nonnegative integer: $TICK"; exit 2 ;; esac
[ -n "$OUT" ] || { echo "FAIL --out is required: evidence directories are never reused"; usage; exit 2; }
for input in "$EXE" "$PROBE" "$TF" "$DEMO"; do
  [ -e "$input" ] || { echo "FAIL missing input: $input"; exit 1; }
done
if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT" 2>/dev/null)" ]; then
  echo "FAIL refusing to overwrite nonempty evidence directory: $OUT"
  exit 1
fi
mkdir -p "$OUT" || exit 1
fail=0
note_fail() { echo "FAIL $*"; fail=1; }

run() {
  local tag="$1"; shift
  TF2_NATIVE_TRACE="$OUT/$tag.trace" timeout -k 5 300 "$EXE" --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --capture-frame "$OUT/$tag.bmp" --capture-tick "$TICK" "$@" >"$OUT/$tag.stdout" 2>&1
  local rc=$?
  echo "$tag rc=$rc"
  [ "$rc" = "0" ] || note_fail "$tag exited rc=$rc"
}

echo "== 1. R: the product's own camera =="
run R
R_TRACE="$OUT/R.trace"
[ -s "$R_TRACE" ] || { echo "FAIL R wrote no trace"; echo "PIX-EVIDENCE=FAIL"; exit 1; }

PRINTED=$(grep -a "scene: camera" "$R_TRACE" | tail -1 | sed -n 's/.*eye=\([^ ]*\) pitch=\([^ ]*\) yaw=\([^ ]*\).*/\1 \2 \3/p' | tr ',' ' ')
echo "R printed pose: $PRINTED"
[ "$(printf '%s\n' "$PRINTED" | wc -w)" = "5" ] || { echo "FAIL no complete camera pose in R trace"; echo "PIX-EVIDENCE=FAIL"; exit 1; }
TARGET=$(grep -a "spectate: camera relay" "$R_TRACE" | tail -1 | sed -n 's/.*target=\([0-9][0-9]*\).*/\1/p')
[ -n "$TARGET" ] || { echo "FAIL R trace has no spectate relay target"; echo "PIX-EVIDENCE=FAIL"; exit 1; }
echo "R followed entity: $TARGET"

SERVER_TICK=$(grep -a "scene: clock tick=" "$R_TRACE" | tail -1 | sed -n 's/.*serverTick=\([0-9][0-9]*\).*/\1/p')
STALE=$(grep -a "scene: snapshot status=" "$R_TRACE" | tail -1 | sed -n 's/.*stale=\([0-9][0-9]*\).*/\1/p')
[ -n "$SERVER_TICK" ] || { echo "FAIL no serverTick in the R trace clock line"; echo "PIX-EVIDENCE=FAIL"; exit 1; }
[ -n "$STALE" ] || { echo "FAIL no stale in the R trace snapshot line"; echo "PIX-EVIDENCE=FAIL"; exit 1; }
SCENE_TICK=$((SERVER_TICK - STALE))
echo "R frame source: serverTick=$SERVER_TICK stale=$STALE resolvedSceneTick=$SCENE_TICK"
# The anim line prints the same resolved tick on an independent path; if the two
# disagree one of the readers is on a different instant, so stop instead of
# choosing one of them.
ANIM_TICK=$(grep -a "scene: anim " "$R_TRACE" | tail -1 | sed -n 's/.*tick=\([0-9][0-9]*\).*/\1/p')
if [ -n "$ANIM_TICK" ] && [ "$ANIM_TICK" != "$SCENE_TICK" ]; then
  note_fail "resolved scene tick disagrees: clock-minus-stale=$SCENE_TICK anim=$ANIM_TICK"
fi

echo "== 2. probe the followed player's transform at that scene tick =="
"$PROBE" --tf-root "$TF" --demo "$DEMO" --observer-focus-at "$SCENE_TICK" --entity "$TARGET" 2>&1 \
  | grep -a "observer entity=" | sed -n 's/.*selfOrigin=\([^ ]*\).*/\1/p' > "$OUT/probe-origin.txt"
"$PROBE" --tf-root "$TF" --demo "$DEMO" --props-at "$SCENE_TICK" --entity "$TARGET" 2>&1 \
  | grep -a "m_angEyeAngles" > "$OUT/probe-angles.txt"
ORIGIN=$(head -1 "$OUT/probe-origin.txt")
[ -n "$ORIGIN" ] || { echo "FAIL probe printed no origin for entity $TARGET at tick $SCENE_TICK"; echo "PIX-EVIDENCE=FAIL"; exit 1; }
# The snapshot carries both a local-player and a non-local-player copy of the eye
# angles. extractTransform prefers one of them, so if the two ever disagree the
# choice decides the replay: that is a failure here, not a coin flip the script
# makes quietly.
PITCHES=$(sed -n 's/.*m_angEyeAngles\[0\].*x=\([-0-9.]*\).*/\1/p' "$OUT/probe-angles.txt" | sort -u)
YAWS=$(sed -n 's/.*m_angEyeAngles\[1\].*x=\([-0-9.]*\).*/\1/p' "$OUT/probe-angles.txt" | sort -u)
[ "$(printf '%s\n' "$PITCHES" | grep -c .)" = "1" ] || { echo "FAIL eye pitch is not single-valued in the snapshot: $PITCHES"; echo "PIX-EVIDENCE=FAIL"; exit 1; }
[ "$(printf '%s\n' "$YAWS" | grep -c .)" = "1" ] || { echo "FAIL eye yaw is not single-valued in the snapshot: $YAWS"; echo "PIX-EVIDENCE=FAIL"; exit 1; }
OX=$(echo "$ORIGIN" | cut -d, -f1)
OY=$(echo "$ORIGIN" | cut -d, -f2)
OZ=$(echo "$ORIGIN" | cut -d, -f3)
EYEZ=$(awk -v z="$OZ" 'BEGIN{printf "%.6f", z + 68.0}')
# placeSpectatorView puts the relay eye at origin plus kSpectatorEyeHeight (68)
# with no back-offset (native/src/main.cpp:329/337/376-380). The probe reports the
# origin, so the height is added here rather than asked of a tool that does not
# model the camera.
EXACT="$OX $OY $EYEZ $PITCHES $YAWS"
echo "probe origin: $ORIGIN"
echo "exact pose:   $EXACT"
printf 'demo=%s\ntick=%s\ntarget=%s\nscene_tick=%s\nprinted_pose=%s\nexact_pose=%s\n' \
  "$DEMO" "$TICK" "$TARGET" "$SCENE_TICK" "$PRINTED" "$EXACT" > "$OUT/pose.txt"

echo "== 3. D: same world eye through the diagnostic camera =="
# shellcheck disable=SC2086
run D --debug-camera $EXACT
echo "== 4. D2: the far control camera =="
run D2 --debug-camera 0 0 1000 0 0

echo "== 5. the pose fed in is the pose D used =="
D_LINE=$(grep -a "scene: camera" "$OUT/D.trace" | tail -1)
D_EYE=$(printf '%s\n' "$D_LINE" | sed -n 's/.*eye=\([^ ]*\) pitch=\([^ ]*\) yaw=\([^ ]*\).*/\1 \2 \3/p' | tr ',' ' ')
D_SRC=$(printf '%s\n' "$D_LINE" | sed -n 's/.*src=\([a-z-]*\).*/\1/p')
echo "D used: $D_EYE src=$D_SRC"
[ "$D_SRC" = "debug" ] || note_fail "D was not drawn with the diagnostic camera (src=$D_SRC)"
POSECHECK=$(awk -v got="$D_EYE" -v want="$EXACT" -v te="$EYE_TOLERANCE" -v ta="$ANGLE_TOLERANCE" 'BEGIN{
  n = split(got, g, " "); split(want, w, " ");
  if (n < 5) { print "could not read five numbers from the D camera line"; exit }
  for (i = 1; i <= 5; i++) {
    d = g[i] - w[i]; if (d < 0) d = -d;
    tol = (i <= 3) ? te : ta;
    if (d > tol) { printf "component %d: fed %s but D drew %s (tolerance %s)\n", i, w[i], g[i], tol; exit }
  }
}')
if [ -n "$POSECHECK" ]; then
  note_fail "D did not draw the pose the probe produced -- $POSECHECK"
fi

echo "== 6. every run captured the same frame =="
reference_tick=""
for t in R D D2; do
  echo "== $t camera:"
  grep -a "scene: camera" "$OUT/$t.trace" | tail -1 || note_fail "$t wrote no camera line"
  echo "== $t capture:"
  capture=$(grep -a "capture: armed" "$OUT/$t.trace" | tail -1)
  echo "$capture"
  frame_tick=$(printf '%s\n' "$capture" | sed -n 's/.*frame_tick=\([0-9][0-9]*\).*/\1/p')
  if [ "$frame_tick" != "$TICK" ]; then
    note_fail "$t frame_tick=$frame_tick expected=$TICK"
  fi
  if [ -z "$reference_tick" ]; then
    reference_tick="$frame_tick"
  elif [ "$frame_tick" != "$reference_tick" ]; then
    note_fail "$t frame_tick=$frame_tick differs from R=$reference_tick"
  fi
done

echo "== 7. body bookkeeping (a reading, not a threshold) =="
# The followed body leaves R's submitted set and stays in D's -- the documented
# T84 behaviour. Printing both counts lets a reader see whether the extra body
# could have been in frame at all, instead of guessing why a tick moved.
grep -a "cameraFilter" "$OUT/R.trace" | tail -1 || echo "  R cameraFilter: none"
for t in R D; do
  echo "  $t $(grep -a "scene: setInstances" "$OUT/$t.trace" | tail -1 | sed -n 's/.*\(draws=[0-9]* .*\)/\1/p')"
done

echo "== 8. pixels =="
RD_PCT=""
RD2_PCT=""
RD_SHAPE=""
# -W ignore keeps Pillow's "getdata is deprecated" notice out of the readings file;
# the reader is pinned to the Pillow the gate already uses.
python -W ignore::DeprecationWarning evidence/t79-camera/pixel-stats.py "$OUT" | tee "$OUT/pixel-stats.txt"
RD_PCT=$(grep -a "R.bmp vs D.bmp " "$OUT/pixel-stats.txt" | sed -n "s/.*'pct_gt8': \([0-9.]*\).*/\1/p")
RD2_PCT=$(grep -a "R.bmp vs D2.bmp " "$OUT/pixel-stats.txt" | sed -n "s/.*'pct_gt8': \([0-9.]*\).*/\1/p")
RD_SHAPE=$(grep -a "R.bmp vs D.bmp " "$OUT/pixel-stats.txt" | sed -n "s/.*'largest_comp': \([0-9]*\).*'largest_comp_box': (\([^)]*\)).*/\1 px \2/p")
[ -n "$RD_SHAPE" ] || RD_SHAPE="unavailable"
echo "R vs D  >8 = $RD_PCT% (must be <= $MAX_PCT_RD) largest residual component = $RD_SHAPE"
echo "R vs D2 >8 = $RD2_PCT% (must be >= $MIN_PCT_RD2)"
if [ -z "$RD_PCT" ] || [ -z "$RD2_PCT" ]; then
  note_fail "pixel-stats did not report both R vs D and R vs D2"
else
  awk -v v="$RD_PCT" -v m="$MAX_PCT_RD" 'BEGIN{ if (v + 0 > m + 0) exit 1 }' \
    || note_fail "R vs D differs by $RD_PCT% which is above the $MAX_PCT_RD% B4.4 ceiling"
  awk -v v="$RD2_PCT" -v m="$MIN_PCT_RD2" 'BEGIN{ if (v + 0 < m + 0) exit 1 }' \
    || note_fail "R vs D2 differs by only $RD2_PCT% which is below the $MIN_PCT_RD2% floor"
fi

if [ "$fail" != "0" ]; then
  echo "PIX-EVIDENCE=FAIL"
  exit 1
fi
echo "PIX-EVIDENCE=PASS"
