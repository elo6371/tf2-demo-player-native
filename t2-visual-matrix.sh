#!/usr/bin/env bash
# t2-visual-matrix.sh -- the frame-level pairs of the T2 world visual acceptance.
#
# WHY THIS EXISTS
#   world-material-check.sh asserts the world pass's readings (the sky box's
#   vertices, the pvs clip's drawn/hidden pair, the lightmap identity) and the
#   pixel pair it takes is the cubemap's. world-material-effects-check.sh takes
#   the water pair. Neither compares the SKY switch's frames or the CLIP
#   switch's frames, and neither turns "the switch does something" into a
#   pixel-level reading for those two. This script takes the five launches
#   those comparisons need, all at tick 0 with --start-paused: the first frame
#   is the captured frame, so the traces' pass readings describe the frame on
#   disk, and the paused world makes the repeat run byte-exact. (The live-tick
#   water signal is exploration evidence in the round's report; it is not
#   asserted here because the launch can land a few ticks past the request and
#   the two sides of a pair must report the same measured tick.)
#
#   The five launches, same demo, same map, same switches otherwise:
#     on        : --world-sky --world-vis-clip --world-water on   (the reference)
#     sky-off   : --no-world-sky
#     vis-off   : --no-world-vis-clip
#     water-off : --world-water off
#     repeat    : a second run of `on` (determinism)
#
# WHAT IT ASSERTS, AND WHAT EACH BOUND IS
#   Readings (from the traces, tick-0 point; all measured on the current line
#   before the assertions were written):
#     sky on  : state=active vertices=36 triangles=12 faces=6 draws=1
#     sky off : state=unavailable vertices=0 draws=0, reason names the caller
#     vis on  : clipApplied=1 state=active input=200000 drawn=170244 hidden=29756
#     vis off : clipApplied=0 state=disabled drawn=200000 (the whole buffer)
#     water on: state=active faces=18 triangles=36 draws=1 visible=1
#     water off: state=unavailable draws=0, reason names the caller
#     drawn(on) < drawn(off); drawn + hidden = input on the on run.
#   Frames (byte-level, 1264x681x24; measured values in the comments):
#     on vs sky-off   differ >= 50 percent      (measured 77.67%)
#     on vs vis-off   differ >= 5 percent       (measured 13.44%; see NOTE)
#     on vs water-off differ >= 0.1 percent and
#                     max channel delta >= 8    (measured 0.2157%, delta 79)
#     on vs repeat    differ by exactly 0 (--max-delta 0)
#
#   NOTE on the clip pair: the pvs clip hides triangles the camera's cluster
#   cannot see, and a perfect clip would be pixel-NEUTRAL. Measured, it is not:
#   the clip switch changes 13.44% of the frame (bbox x=0..1218 y=123..539).
#   This script asserts only what was measured -- the switch changes pixels and
#   the frame -- and does not assert which of the two frames is "right". The
#   face-to-cluster mapping is this repo's first-leaf approximation, not
#   cross-verified against Source (T1 report section 8 item 5), and coincident
#   brush surfaces resolve differently once the extra triangles are drawn.
#
# OUTPUT
#   $OUT (default evidence/t2-visual-<date>): the extracted readings, the frame
#   hashes and comparisons, PNG conversions of the frames, and one line per
#   assertion. $SCRATCH holds the raw BMPs/traces/CSVs (they are not committed).
#   Verdict: T2-VISUAL-MATRIX=PASS / =FAIL.
#
# Usage: bash t2-visual-matrix.sh
# Exit:  0 = every assertion held; 1 = one did not or an input was missing.
set -uo pipefail
cd "$(dirname "$0")"

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
PRODUCT=native/build-nmake/tf2_demo_native.exe
PY="${T2V_PY:-C:/Users/Administrator/.workbuddy/binaries/python/versions/3.13.12/python.exe}"
SCRATCH="${T2V_SCRATCH:-.scratch/t2-visual/matrix}"
OUT="${T2V_OUT:-evidence/t2-visual-20261015}"
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
mkdir -p "$SCRATCH" "$OUT/frames" "$OUT/readings"

fail=0
okcount=0
for path in "$PRODUCT" "$TF" "$TF/maps/cp_snakewater_final1.bsp" "$DEMO" bmp-pixel-diff.py bmp-diff-bbox.py; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"; okcount=$((okcount + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_bound() { # assert_bound <label> <tool-output-line> <status-token>
  if printf '%s\n' "$2" | grep -q "status=$3"; then
    echo "  OK   $1"; okcount=$((okcount + 1))
  else
    echo "  FAIL $1: $2"; fail=1
  fi
}

# run_capture <label> <tick> <pause 0|1> [extra args...]
# Sets RC / REACHED. Writes bmp/csv/trace/log under $SCRATCH.
run_capture() {
  local label="$1" tick="$2" pause="$3"; shift 3
  local bmp="$SCRATCH/$label.$RUN_TAG.bmp" csv="$SCRATCH/$label.$RUN_TAG.csv"
  local trace="$SCRATCH/$label.$RUN_TAG.trace" log="$SCRATCH/$label.$RUN_TAG.log"
  local pauseArg=""
  [ "$pause" = "1" ] && pauseArg="--start-paused"
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 300 "$PRODUCT" \
    --tf-root "$TF" --audio-disabled --hidden-window --demo "$DEMO" \
    $pauseArg "$@" --capture-tick "$tick" --capture-frame "$bmp" \
    --metrics-file "$csv" > "$log" 2>&1
  RC=$?
  REACHED=$(tail -1 "$csv" 2>/dev/null | cut -d, -f4)
  BMP="$bmp"; TRACE="$trace"
}

rline() { # rline <trace> <"gpu: world xxx"> -> the first matching trace line
  grep -o "$2 .*" "$1" 2>/dev/null | head -1
}
rfield() { printf '%s\n' "$1" | grep -oE "$2=[0-9-]+" | head -1 | cut -d= -f2; }
rtext() { # rtext <line> <key> -> word value (for state=/reason style fields)
  printf '%s\n' "$1" | grep -oE "$2=[a-z-]+" | head -1 | cut -d= -f2
}

echo "=== 1/4 the deterministic point: tick 0, paused ==="
run_capture on-0       0 1 --world-sky --world-water on
assert_eq "on run rc" "$RC" "0"; assert_eq "on reached tick" "$REACHED" "0"
ON_TRACE="$TRACE"; ON_BMP="$BMP"
run_capture sky-off-0  0 1 --no-world-sky --world-water on
assert_eq "sky-off run rc" "$RC" "0"; assert_eq "sky-off reached tick" "$REACHED" "0"
SKYOFF_TRACE="$TRACE"; SKYOFF_BMP="$BMP"
run_capture vis-off-0  0 1 --world-sky --no-world-vis-clip --world-water on
assert_eq "vis-off run rc" "$RC" "0"; assert_eq "vis-off reached tick" "$REACHED" "0"
VISOFF_TRACE="$TRACE"; VISOFF_BMP="$BMP"
run_capture water-off-0 0 1 --world-sky --world-water off
assert_eq "water-off run rc" "$RC" "0"; assert_eq "water-off reached tick" "$REACHED" "0"
WATOFF_TRACE="$TRACE"; WATOFF_BMP="$BMP"
run_capture repeat-0   0 1 --world-sky --world-water on
assert_eq "repeat run rc" "$RC" "0"; assert_eq "repeat reached tick" "$REACHED" "0"
REPEAT_BMP="$BMP"

echo
echo "=== 2/4 the readings the frames were taken under (tick 0) ==="
SKY_ON=$(rline "$ON_TRACE" "gpu: world sky");       [ -n "$SKY_ON" ] || { echo "  FAIL no sky line"; fail=1; }
SKY_OFF=$(rline "$SKYOFF_TRACE" "gpu: world sky");  [ -n "$SKY_OFF" ] || { echo "  FAIL no sky-off line"; fail=1; }
VIS_ON=$(rline "$ON_TRACE" "gpu: world vis");       [ -n "$VIS_ON" ] || { echo "  FAIL no vis line"; fail=1; }
VIS_OFF=$(rline "$VISOFF_TRACE" "gpu: world vis");  [ -n "$VIS_OFF" ] || { echo "  FAIL no vis-off line"; fail=1; }
WAT_ON=$(rline "$ON_TRACE" "gpu: world water");     [ -n "$WAT_ON" ] || { echo "  FAIL no water line"; fail=1; }
WAT_OFF=$(rline "$WATOFF_TRACE" "gpu: world water");[ -n "$WAT_OFF" ] || { echo "  FAIL no water-off line"; fail=1; }
printf '%s\n' "$SKY_ON" "$SKY_OFF" "$VIS_ON" "$VIS_OFF" "$WAT_ON" "$WAT_OFF" > "$OUT/readings/switches-tick0.txt"
assert_eq "sky on state"   "$(rtext "$SKY_ON" state)"   "active"
assert_eq "sky on vertices" "$(rfield "$SKY_ON" vertices)" "36"
assert_eq "sky on faces"   "$(rfield "$SKY_ON" faces)"  "6"
assert_eq "sky on draws"   "$(rfield "$SKY_ON" draws)"  "1"
assert_eq "sky off state"  "$(rtext "$SKY_OFF" state)"  "unavailable"
assert_eq "sky off vertices" "$(rfield "$SKY_OFF" vertices)" "0"
assert_eq "sky off draws"  "$(rfield "$SKY_OFF" draws)" "0"
case "$SKY_OFF" in *"disabled by the caller"*) echo "  OK   sky off reason names the caller"; okcount=$((okcount + 1));; *) echo "  FAIL sky off reason: $SKY_OFF"; fail=1;; esac
assert_eq "vis on clip applied" "$(rfield "$VIS_ON" clipApplied)" "1"
assert_eq "vis on input"  "$(rfield "$VIS_ON" inputTriangles)" "200000"
assert_eq "vis on hidden" "$(rfield "$VIS_ON" hiddenTriangles)" "29756"
assert_eq "vis off state" "$(rtext "$VIS_OFF" state)" "disabled"
assert_eq "vis off applied" "$(rfield "$VIS_OFF" clipApplied)" "0"
VIS_ON_DRAWN=$(rfield "$VIS_ON" drawnTriangles)
VIS_OFF_DRAWN=$(rfield "$VIS_OFF" drawnTriangles)
assert_eq "vis off drawn = input" "$VIS_OFF_DRAWN" "$(rfield "$VIS_ON" inputTriangles)"
if [ "$VIS_ON_DRAWN" -lt "$VIS_OFF_DRAWN" ] 2>/dev/null; then
  echo "  OK   the clip changes the geometry ($VIS_ON_DRAWN < $VIS_OFF_DRAWN)"; okcount=$((okcount + 1))
else
  echo "  FAIL the clip left the drawn geometry unchanged ($VIS_ON_DRAWN vs $VIS_OFF_DRAWN)"; fail=1
fi
assert_eq "vis identity drawn+hidden=input" "$(( VIS_ON_DRAWN + $(rfield "$VIS_ON" hiddenTriangles) ))" "$(rfield "$VIS_ON" inputTriangles)"
assert_eq "water on state" "$(rtext "$WAT_ON" state)" "active"
assert_eq "water on faces" "$(rfield "$WAT_ON" faces)" "18"
assert_eq "water on triangles" "$(rfield "$WAT_ON" triangles)" "36"
assert_eq "water on draws"  "$(rfield "$WAT_ON" draws)" "1"
assert_eq "water off state" "$(rtext "$WAT_OFF" state)" "unavailable"
assert_eq "water off draws" "$(rfield "$WAT_OFF" draws)" "0"
case "$WAT_OFF" in *"disabled by the caller"*) echo "  OK   water off reason names the caller"; okcount=$((okcount + 1));; *) echo "  FAIL water off reason: $WAT_OFF"; fail=1;; esac

echo
echo "=== 3/4 the tick-0 frame pairs ==="
cmp_pair() { # cmp_pair <label> <a.bmp> <b.bmp> [bounds...]
  local label="$1" a="$2" b="$3"; shift 3
  local out
  out=$("$PY" bmp-pixel-diff.py "$a" "$b" "$@" 2>&1)
  echo "  $label: $out"
  assert_bound "$label" "$out" PASS
  printf '%s\n' "$label: $out" >> "$OUT/frames/comparisons.txt"
}
: > "$OUT/frames/comparisons.txt"
cmp_pair "on vs sky-off  " "$ON_BMP" "$SKYOFF_BMP" --min-diff-percent 50
cmp_pair "on vs vis-off  " "$ON_BMP" "$VISOFF_BMP" --min-diff-percent 5
cmp_pair "on vs water-off" "$ON_BMP" "$WATOFF_BMP" --min-diff-percent 0.1 --min-delta 8
cmp_pair "on vs repeat   " "$ON_BMP" "$REPEAT_BMP" --max-delta 0
echo "  --- difference regions:"
for pair in "sky-off:$ON_BMP:$SKYOFF_BMP" "vis-off:$ON_BMP:$VISOFF_BMP" "water-off:$ON_BMP:$WATOFF_BMP"; do
  name="${pair%%:*}"; rest="${pair#*:}"; a="${rest%%:*}"; b="${rest#*:}"
  bbox=$("$PY" bmp-diff-bbox.py "$a" "$b" 2>&1)
  echo "  $name: $bbox"
  printf '%s\n' "region $name: $bbox" >> "$OUT/frames/comparisons.txt"
done

echo
echo "=== 4/4 frame identity and PNG record ==="
sha_dump() { # sha_dump <name> <bmp>
  local sum bytes
  sum=$(sha256sum "$2" | cut -d' ' -f1); bytes=$(stat -c%s "$2")
  printf '%s %s %s\n' "$1" "$sum" "$bytes"
}
# The five frames are committed next to their PNG conversions, so every hash in
# sha256.txt names a file that is in the tree. The scratch copy is what the
# comparisons above read; the committed copy is proved identical here.
for entry in on:"$ON_BMP" sky-off:"$SKYOFF_BMP" vis-off:"$VISOFF_BMP" water-off:"$WATOFF_BMP" repeat:"$REPEAT_BMP"; do
  name="${entry%%:*}"; src="${entry#*:}"
  cp "$src" "$OUT/frames/$name.bmp" || { echo "  FAIL could not copy $name"; fail=1; }
  if cmp -s "$src" "$OUT/frames/$name.bmp"; then
    echo "  OK   $name.bmp committed copy is byte-identical to the run's frame"; okcount=$((okcount + 1))
  else
    echo "  FAIL $name.bmp committed copy differs from the run's frame"; fail=1
  fi
done
for name in on sky-off vis-off water-off repeat; do sha_dump "$name" "$OUT/frames/$name.bmp"; done > "$OUT/frames/sha256.txt"
cat "$OUT/frames/sha256.txt"
for name in on sky-off vis-off water-off repeat; do
  "$PY" bmp-to-png.py "$OUT/frames/$name.bmp" "$OUT/frames/$name.png"
done

echo
echo "assertions_ok=$okcount"
if [ "$fail" -eq 0 ]; then
  echo "T2-VISUAL-MATRIX=PASS"
else
  echo "T2-VISUAL-MATRIX=FAIL"
fi
exit "$fail"
