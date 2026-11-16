#!/usr/bin/env bash
# t2-visual-matrix.sh -- the frame-level pairs of the T2 world visual acceptance.
#
# WHY THIS EXISTS
#   world-material-check.sh asserts the world pass's readings (the sky box's
#   vertices, the pvs clip's drawn/hidden pair, the lightmap identity) and the
#   pixel pair it takes is the cubemap's. world-material-effects-check.sh takes
#   the water pair. Neither compares the SKY switch's frames or the CLIP
#   switch's frames, and neither turns "the switch does something" into a
#   pixel-level reading for those two. This script takes the nine launches
#   those comparisons need, all at tick 0 with --start-paused: the first frame
#   is the captured frame, so the traces' pass readings describe the frame on
#   disk, and the paused world makes the repeat run byte-exact. (The live-tick
#   water signal is exploration evidence in the round's report; it is not
#   asserted here because the launch can land a few ticks past the request and
#   the two sides of a pair must report the same measured tick.)
#
#   The five launches at the orbit camera, same demo, same map, same switches
#   otherwise (they carry the readings, the clip pair and the determinism pair):
#     on        : --world-sky --world-vis-clip --world-water on   (the reference)
#     sky-off   : --no-world-sky
#     vis-off   : --no-world-vis-clip
#     water-off : --world-water off
#     repeat    : a second run of `on` (determinism)
#
#   The four camera launches, added 2026-11-01: cp_snakewater_final1 is a POV
#   recording with no observer sample, so the tick-0 trace says `src=held`
#   (holds counted every tick of the run) and the frame is drawn from the orbit
#   camera wherever it happens to sit. Measured on that camera the sky switch
#   changed 30.17% of the frame and the water switch 0.0020% -- one below the
#   floor, and neither a fixture anybody chose. Each of the two pairs below now
#   states its camera with --debug-camera, and the trace's `scene: camera` line
#   is asserted to carry that eye and pitch, so the frame is attributed to the
#   camera the readings were taken under rather than to whatever the orbit did:
#     sky-on / sky-off     at (542 -98 2000 -85 0)   -- looking up, almost all sky
#     water-on / water-off at (-600 3800 150 70 0)   -- a water strip on the right
#
#   That attribution assertion is not decoration. On 2026-11-01 the switch was
#   inert: the demo view was set in the scene tick and then cleared before the
#   draw, so two cameras 1500 units apart produced byte-identical frames while
#   the trace still printed src=debug. T84 re-states the debug view immediately
#   before the draw and makes the trace print the eye the frame is drawn with;
#   the two camera frames differing by 99.99% is what proves it.
#
# WHAT IT ASSERTS, AND WHAT EACH BOUND IS
#   Readings (from the traces, tick-0 point):
#     sky on  : state=active vertices=36 triangles=12 faces=6 draws=1
#     sky off : state=unavailable vertices=0 draws=0, reason names the caller
#     vis on  : clipApplied=1 state=active, input=242109 drawn=216114 hidden=25995
#     vis off : clipApplied=0 state=disabled drawn = input (the whole buffer)
#     water on: state=active faces=18 triangles=36 draws=1 visible=1
#     water off: state=unavailable draws=0, reason names the caller
#     the camera launches' last `scene: camera` line carries the pinned pose
#     (eye=, pitch=) and src=debug -- the frame is the camera it names.
#
#   THE CLIP IS JUDGED BY DERIVED BOUNDS, NOT BY PINNED COUNTS (2026-11-16)
#   This block used to pin vis input=200000 (drawn 184961, hidden 15039) and to
#   require that turning the clip off change >= 5% of the frame. All three bounds
#   described a tree that no longer exists:
#     * input was the parser's triangle capacity, a number no map had been
#       measured against. T88 raised it to 400,000 after measuring the maps
#       (evidence/t88-maps/demand.txt: cp_snakewater_final1 asks for 242361), so
#       the buffer now holds 242,109 triangles and 200000 is simply a count of the
#       previous buffer.
#     * hidden is a property of the CAMERA (which clusters the frame's own eye can
#       see), not of the map: 25995 here, while world-material-check.sh pins 15039
#       for its own camera. One gate's number is not the other's.
#     * the >= 5% pair was written while the clip queried a camera other than the
#       one the frame was drawn with, so the difference was the clip hiding
#       geometry the frame could see. T80/T84 moved the query onto the frame's own
#       eye and the difference went to zero -- which is what a correct pvs clip
#       does (see NOTE).
#   Swapping 200000 for 242109 would pin the current arithmetic and still say
#   nothing about whether the arithmetic is right. So the clip is judged by what
#   its own trace reports, and the frame pair by pixel neutrality:
#     map load : `map: geometry budget` reads state=within, dropped=0, and a
#                capacity that covers `requested`. A truncated walk is geometry
#                the frame never had, whatever the vis census says.
#     input    : equals the demand the same run measured, within 1%. `requested`
#                counts triangles a displacement grid registers and then resizes
#                away when the grid fails mid-build (252 of them here), so exact
#                equality is not a property of the product; a short buffer is.
#     census   : withoutCluster + clustered = input; drawn >= withoutCluster (a
#                triangle with no cluster is never hidden -- it cannot be tested);
#                hidden = clustered - (drawn - withoutCluster); drawn + hidden =
#                input; 0 < hidden < clustered (neither vacuous nor total).
#     geometry : vis-on drawn < vis-off drawn, and vis-off drawn = vis-on input.
#   Frames (byte-level, 1264x681x24; measured values in the comments):
#     sky-on vs sky-off     differ >= 50 percent  (measured 100.0000%, delta 235)
#     water-on vs water-off differ >= 0.1 percent and
#                           max channel delta >= 8 (measured 0.8216%, delta 83)
#     clip on vs clip off   differ by exactly 0 (--max-delta 0; measured 0.0000%)
#     on vs repeat          differ by exactly 0 (--max-delta 0)
#     water camera vs sky camera differ >= 50 percent (measured 99.9660%, delta
#     249): the camera is what moved, not the world.
#
#   NOTE on the clip pair: the pvs clip hides triangles none of whose clusters the
#   camera's cluster can see (world_material.cpp clipWorldTrianglesByVis keeps a
#   triangle when ANY of its clusters is visible), and the pair is taken at the
#   camera the clip itself queried -- so a correct clip is pixel-NEUTRAL, and any
#   difference is the clip hiding geometry the camera could see. Measured 0.0000%
#   (the two BMPs are byte-identical). The old >= 5% bound asserted that the
#   switch "changes the frame", which a correct clip has no reason to do;
#   `--mutation` keeps this from being an assertion that cannot fail (m2-invert
#   hides the visible set and the pair goes red).
#
# OUTPUT
#   $OUT (default evidence/t2-visual-<date>): the extracted readings, the frame
#   hashes and comparisons, PNG conversions of the frames, and one line per
#   assertion. The five orbit-camera frames are committed as BMP+PNG; the four
#   camera-pinned frames as PNG only (sha256-cameras.txt carries both hashes).
#   $SCRATCH holds the raw BMPs/traces/CSVs (they are not committed).
#   Verdict: T2-VISUAL-MATRIX=PASS / =FAIL.
#
# Usage: bash t2-visual-matrix.sh [--mutation]
#   --mutation: after a green run, inject three defects one at a time (the old
#   200,000 capacity, an inverted visibility test, and a clip that hides the
#   triangles with no cluster), rebuild with build-target.sh, re-run only the vis
#   captures, and require the intended assertion to go red. Sources are restored
#   by byte copy and the final rebuild must reproduce the forward exe. Takes the
#   serial host lock (it builds) and about ten minutes.
# Exit:  0 = every assertion held; 1 = one did not or an input was missing.
set -uo pipefail
cd "$(dirname "$0")"

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
PRODUCT=native/build-nmake/tf2_demo_native.exe
# The two pinned cameras (map units, pitch/yaw degrees). See the header: this
# demo has no observer sample, so without these the frame is drawn from the
# orbit camera and neither pair means what it says.
CAM_SKY=(--debug-camera 542 -98 2000 -85 0)
CAM_WATER=(--debug-camera -600 3800 150 70 0)
PY="${T2V_PY:-C:/Users/Administrator/.workbuddy/binaries/python/versions/3.13.12/python.exe}"
SCRATCH="${T2V_SCRATCH:-.scratch/t2-visual/matrix}"
OUT="${T2V_OUT:-evidence/t2-visual-20261015}"
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
# --- isolated settings (D-22) ----------------------------------------------
# The product persists settings.ini and ui.ini under %LOCALAPPDATA%/TF Demo
# Player, so a run that does not redirect them shares state with the desktop
# player and with every other gate on the host -- and verify-release's machine
# guard compares that pair at the end of the chain, where one stray write turns
# the whole chain red. Each run gets a throwaway directory instead, seeded with
# the tracked gate fixture: an empty settings file is NOT "the defaults"
# (verify-iso.sh: preset falls back to 0 = Performance, which draws no sky), so a
# gate pointed at nothing would measure a different renderer than the frames it
# pins.
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-iso.XXXXXX") || {
  echo "FATAL: cannot create isolated settings directory" >&2; exit 2; }
. "$(dirname "$0")/verify-iso.sh"
iso_chain_exit "$ISO_DIR"
ISO_SETTINGS="$ISO_DIR/settings.ini"
ISO_UI="$ISO_DIR/ui.ini"
iso_seed_gate_settings "$ISO_DIR" || {
  echo "FATAL: cannot seed the isolated gate settings" >&2; exit 2; }
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

MUTATION=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    *) echo "FATAL: unknown argument: $arg (usage: bash t2-visual-matrix.sh [--mutation])" >&2; exit 2 ;;
  esac
done

# judge_vis <map-line> <vis-on-line> <vis-off-line>
# Judges the pvs clip from what the run itself reports: no map triangle count is
# pinned, because the buffer's capacity and the camera's hidden set move with the
# tree (see the header note).
judge_vis() {
  local map="$1" on="$2" off="$3"
  local cap req dropped state
  local inp drawn hidden clustered no_cluster
  cap=$(rfield "$map" capacity);            req=$(rfield "$map" requested)
  dropped=$(rfield "$map" dropped);         state=$(rtext "$map" state)
  inp=$(rfield "$on" inputTriangles);       drawn=$(rfield "$on" drawnTriangles)
  hidden=$(rfield "$on" hiddenTriangles);   clustered=$(rfield "$on" clusteredTriangles)
  no_cluster=$(rfield "$on" withoutCluster)

  # A truncated walk is geometry the frame never had; no vis count can excuse it.
  assert_eq "map budget state" "$state" "within"
  assert_eq "map budget dropped" "$dropped" "0"
  if [ -n "$cap" ] && [ -n "$req" ]; then
    if [ "$cap" -ge "$req" ]; then
      echo "  OK   the capacity covers the map's demand ($cap >= $req)"; okcount=$((okcount + 1))
    else
      echo "  FAIL the capacity is below the map's demand ($cap < $req)"; fail=1
    fi
  else
    echo "  FAIL the map budget line has no capacity/requested: ${map:-<none>}"; fail=1
  fi

  case "$inp$drawn$hidden$clustered$no_cluster" in
    ''|*[!0-9]*) echo "  FAIL the vis line is missing a census field: ${on:-<none>}"; fail=1; return ;;
  esac

  # input is the buffer the map wanted. It trails 'requested' by the triangles a
  # half-built displacement grid registers and then resizes away (252 on this
  # map), so it is judged against the same run's demand with a 1% residual.
  if [ "$inp" -le "$req" ] && [ "$(( inp * 100 ))" -ge "$(( req * 99 ))" ]; then
    echo "  OK   the buffer holds the map's demand ($inp of $req requested)"; okcount=$((okcount + 1))
  else
    echo "  FAIL the buffer holds $inp of the $req triangles the map asked for"; fail=1
  fi

  assert_eq "vis census adds up" "$(( no_cluster + clustered ))" "$inp"
  # A triangle with no cluster is never hidden: it cannot be tested, so the clip
  # keeps it and counts it.
  if [ "$drawn" -ge "$no_cluster" ]; then
    echo "  OK   the clip keeps every triangle with no cluster ($no_cluster kept)"; okcount=$((okcount + 1))
  else
    echo "  FAIL the clip hid triangles with no cluster (drawn $drawn < withoutCluster $no_cluster)"; fail=1
  fi
  # What is hidden is the clustered total minus the clustered triangles drawn.
  assert_eq "vis hidden from the census" "$hidden" "$(( clustered - (drawn - no_cluster) ))"
  assert_eq "vis identity drawn+hidden=input" "$(( drawn + hidden ))" "$inp"
  # Neither absent nor total: zero would make the clip vacuous, all of them would
  # mean the census and the clip disagree.
  if [ "$hidden" -gt 0 ] && [ "$hidden" -lt "$clustered" ]; then
    echo "  OK   the clip hid some but not all clustered triangles ($hidden of $clustered)"; okcount=$((okcount + 1))
  else
    echo "  FAIL the clip hid $hidden of $clustered clustered triangles"; fail=1
  fi

  assert_eq "vis on clip applied" "$(rfield "$on" clipApplied)" "1"
  assert_eq "vis on state" "$(rtext "$on" state)" "active"
  assert_eq "vis off state" "$(rtext "$off" state)" "disabled"
  assert_eq "vis off applied" "$(rfield "$off" clipApplied)" "0"
  local off_drawn; off_drawn=$(rfield "$off" drawnTriangles)
  case "$off_drawn" in
    ''|*[!0-9]*) echo "  FAIL the vis-off line has no drawnTriangles: ${off:-<none>}"; fail=1; return ;;
  esac
  assert_eq "vis off drawn = input" "$off_drawn" "$inp"
  if [ "$drawn" -lt "$off_drawn" ]; then
    echo "  OK   the clip changes the geometry ($drawn < $off_drawn)"; okcount=$((okcount + 1))
  else
    echo "  FAIL the clip left the drawn geometry unchanged ($drawn vs $off_drawn)"; fail=1
  fi
}

cmp_pair() { # cmp_pair <label> <a.bmp> <b.bmp> [bounds...]
  local label="$1" a="$2" b="$3"; shift 3
  local out
  out=$("$PY" bmp-pixel-diff.py "$a" "$b" "$@" 2>&1)
  echo "  $label: $out"
  assert_bound "$label" "$out" PASS
  printf '%s\n' "$label: $out" >> "$OUT/frames/comparisons.txt"
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
    --settings-file "$ISO_SETTINGS" \
    --ui-settings-file "$ISO_UI" \
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

# ---- the mutation path: only the vis captures, judged by the shipped function --
# T2V_QUICK=1 runs the two launches a defect build needs and exits with their
# verdict, so an injected defect is judged by the assertions the gate ships rather
# than by a second copy of them.
if [ "${T2V_QUICK:-0}" = "1" ]; then
  echo "=== quick: the pvs clip only (the mutation judgment) ==="
  run_capture on-0      0 1 --world-sky --world-water on
  assert_eq "on run rc" "$RC" "0"; assert_eq "on reached tick" "$REACHED" "0"
  ON_TRACE="$TRACE"; ON_BMP="$BMP"
  run_capture vis-off-0 0 1 --world-sky --no-world-vis-clip --world-water on
  assert_eq "vis-off run rc" "$RC" "0"; assert_eq "vis-off reached tick" "$REACHED" "0"
  VISOFF_TRACE="$TRACE"; VISOFF_BMP="$BMP"
  MAP_ON=$(grep -o 'map: geometry budget .*' "$ON_TRACE" 2>/dev/null | tail -1)
  VIS_ON=$(rline "$ON_TRACE" "gpu: world vis")
  VIS_OFF=$(rline "$VISOFF_TRACE" "gpu: world vis")
  [ -n "$MAP_ON" ] || { echo "  FAIL no map: geometry budget line in the on run"; fail=1; }
  [ -n "$VIS_ON" ] || { echo "  FAIL no vis line in the on run"; fail=1; }
  [ -n "$VIS_OFF" ] || { echo "  FAIL no vis line in the vis-off run"; fail=1; }
  judge_vis "$MAP_ON" "$VIS_ON" "$VIS_OFF"
  cmp_pair "clip pixel neutrality" "$ON_BMP" "$VISOFF_BMP" --max-delta 0
  echo
  echo "assertions_ok=$okcount"
  [ "$fail" -eq 0 ] && echo "T2-VISUAL-MATRIX=PASS" || echo "T2-VISUAL-MATRIX=FAIL"
  exit "$fail"
fi

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
echo "=== 1b/4 the two camera-pinned pairs (same demo, same tick, stated camera) ==="
run_capture sky-on-0    0 1 "${CAM_SKY[@]}" --world-sky --world-water on
assert_eq "sky camera on run rc" "$RC" "0"; assert_eq "sky camera on reached tick" "$REACHED" "0"
SKYCAM_ON_TRACE="$TRACE"; SKYCAM_ON_BMP="$BMP"
run_capture sky-off-cam 0 1 "${CAM_SKY[@]}" --no-world-sky --world-water on
assert_eq "sky camera off run rc" "$RC" "0"; assert_eq "sky camera off reached tick" "$REACHED" "0"
SKYCAM_OFF_TRACE="$TRACE"; SKYCAM_OFF_BMP="$BMP"
run_capture water-on-0  0 1 "${CAM_WATER[@]}" --world-sky --world-water on
assert_eq "water camera on run rc" "$RC" "0"; assert_eq "water camera on reached tick" "$REACHED" "0"
WCAM_ON_TRACE="$TRACE"; WCAM_ON_BMP="$BMP"
run_capture water-off-c 0 1 "${CAM_WATER[@]}" --world-sky --world-water off
assert_eq "water camera off run rc" "$RC" "0"; assert_eq "water camera off reached tick" "$REACHED" "0"
WCAM_OFF_TRACE="$TRACE"; WCAM_OFF_BMP="$BMP"

echo
echo "=== 2/4 the readings the frames were taken under (tick 0) ==="
SKY_ON=$(rline "$ON_TRACE" "gpu: world sky");       [ -n "$SKY_ON" ] || { echo "  FAIL no sky line"; fail=1; }
SKY_OFF=$(rline "$SKYOFF_TRACE" "gpu: world sky");  [ -n "$SKY_OFF" ] || { echo "  FAIL no sky-off line"; fail=1; }
VIS_ON=$(rline "$ON_TRACE" "gpu: world vis");       [ -n "$VIS_ON" ] || { echo "  FAIL no vis line"; fail=1; }
VIS_OFF=$(rline "$VISOFF_TRACE" "gpu: world vis");  [ -n "$VIS_OFF" ] || { echo "  FAIL no vis-off line"; fail=1; }
WAT_ON=$(rline "$ON_TRACE" "gpu: world water");     [ -n "$WAT_ON" ] || { echo "  FAIL no water line"; fail=1; }
WAT_OFF=$(rline "$WATOFF_TRACE" "gpu: world water");[ -n "$WAT_OFF" ] || { echo "  FAIL no water-off line"; fail=1; }
MAP_ON=$(grep -o 'map: geometry budget .*' "$ON_TRACE" 2>/dev/null | tail -1)
[ -n "$MAP_ON" ] || { echo "  FAIL no map: geometry budget line in the on run"; fail=1; }
printf '%s\n' "$MAP_ON" "$SKY_ON" "$SKY_OFF" "$VIS_ON" "$VIS_OFF" "$WAT_ON" "$WAT_OFF" > "$OUT/readings/switches-tick0.txt"
assert_eq "sky on state"   "$(rtext "$SKY_ON" state)"   "active"
assert_eq "sky on vertices" "$(rfield "$SKY_ON" vertices)" "36"
assert_eq "sky on faces"   "$(rfield "$SKY_ON" faces)"  "6"
assert_eq "sky on draws"   "$(rfield "$SKY_ON" draws)"  "1"
assert_eq "sky off state"  "$(rtext "$SKY_OFF" state)"  "unavailable"
assert_eq "sky off vertices" "$(rfield "$SKY_OFF" vertices)" "0"
assert_eq "sky off draws"  "$(rfield "$SKY_OFF" draws)" "0"
case "$SKY_OFF" in *"disabled by the caller"*) echo "  OK   sky off reason names the caller"; okcount=$((okcount + 1));; *) echo "  FAIL sky off reason: $SKY_OFF"; fail=1;; esac
judge_vis "$MAP_ON" "$VIS_ON" "$VIS_OFF"
assert_eq "water on state" "$(rtext "$WAT_ON" state)" "active"
assert_eq "water on faces" "$(rfield "$WAT_ON" faces)" "18"
assert_eq "water on triangles" "$(rfield "$WAT_ON" triangles)" "36"
assert_eq "water on draws"  "$(rfield "$WAT_ON" draws)" "1"
assert_eq "water off state" "$(rtext "$WAT_OFF" state)" "unavailable"
assert_eq "water off draws" "$(rfield "$WAT_OFF" draws)" "0"
case "$WAT_OFF" in *"disabled by the caller"*) echo "  OK   water off reason names the caller"; okcount=$((okcount + 1));; *) echo "  FAIL water off reason: $WAT_OFF"; fail=1;; esac
# The frame is attributed to a camera: the trace's last `scene: camera` line must
# carry the pose this run asked for. T84 made the line print the eye the frame is
# drawn with; before that it printed the demo eye (zeroes here) next to src=debug.
camassert() { # camassert <label> <trace> <eye> <pitch>
  local line eye pitch
  line=$(grep -a "scene: camera mode=" "$2" 2>/dev/null | tail -1)
  eye=$(printf '%s\n' "$line" | grep -oE 'eye=[-0-9.,]+' | head -1 | cut -d= -f2)
  pitch=$(printf '%s\n' "$line" | grep -oE 'pitch=[-0-9.]+' | head -1 | cut -d= -f2)
  assert_eq "$1 camera eye" "$eye" "$3"
  case "$pitch" in "$4"*) echo "  OK   $1 camera pitch = $pitch"; okcount=$((okcount + 1));; *) echo "  FAIL $1 camera pitch = ${pitch:-<none>}, expected $4"; fail=1;; esac
  case "$line" in *"src=debug"*) echo "  OK   $1 names the debug camera as the source"; okcount=$((okcount + 1));; *) echo "  FAIL $1 camera source: ${line:-<none>}"; fail=1;; esac
}
camassert "sky camera on"  "$SKYCAM_ON_TRACE"  "542.0,-98.0,2000.0"  "-85.00"
camassert "water camera on" "$WCAM_ON_TRACE"   "-600.0,3800.0,150.0" "70.00"

echo
echo "=== 3/4 the tick-0 frame pairs ==="

: > "$OUT/frames/comparisons.txt"
cmp_pair "sky-on vs sky-off    " "$SKYCAM_ON_BMP" "$SKYCAM_OFF_BMP" --min-diff-percent 50
cmp_pair "water-on vs water-off" "$WCAM_ON_BMP" "$WCAM_OFF_BMP" --min-diff-percent 0.1 --min-delta 8
cmp_pair "clip pixel neutrality" "$ON_BMP" "$VISOFF_BMP" --max-delta 0
cmp_pair "on vs repeat         " "$ON_BMP" "$REPEAT_BMP" --max-delta 0
cmp_pair "water cam vs sky cam " "$WCAM_ON_BMP" "$SKYCAM_ON_BMP" --min-diff-percent 50
echo "  --- difference regions:"
for pair in "sky-off:$SKYCAM_ON_BMP:$SKYCAM_OFF_BMP" "vis-off:$ON_BMP:$VISOFF_BMP" "water-off:$WCAM_ON_BMP:$WCAM_OFF_BMP"; do
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
# The four camera frames are recorded as PNG only: they are 2,582,406-byte BMPs
# and this repository just spent a day removing 814 MB of redundant bitmaps. The
# PNG is the reviewable form; the BMP's hash is recorded next to it so the frame
# the comparisons above read is still named by a hash.
: > "$OUT/frames/sha256-cameras.txt"
camframe() { # camframe <name> <bmp>
  "$PY" bmp-to-png.py "$2" "$OUT/frames/$1.png" || { echo "  FAIL could not convert $1"; fail=1; return; }
  printf '%s bmp %s\n' "$1" "$(sha256sum "$2" | cut -d' ' -f1)" >> "$OUT/frames/sha256-cameras.txt"
  printf '%s png %s\n' "$1" "$(sha256sum "$OUT/frames/$1.png" | cut -d' ' -f1)" >> "$OUT/frames/sha256-cameras.txt"
  echo "  OK   $1.png recorded (bmp not committed: 2.5 MB each)"
  okcount=$((okcount + 1))
}
camframe sky-camera-on   "$SKYCAM_ON_BMP"
camframe sky-camera-off  "$SKYCAM_OFF_BMP"
camframe water-camera-on "$WCAM_ON_BMP"
camframe water-camera-off "$WCAM_OFF_BMP"
cat "$OUT/frames/sha256-cameras.txt"

# ---- injected defects: three, and each must turn the judgment red -----------
# A gate whose new bounds cannot fail is not evidence. Each case changes ONE line,
# rebuilds, runs the quick judgment only (T2V_QUICK=1: the two vis launches), and
# requires the intended assertion in the FAIL output. The forward run must be
# green first.
mutation_one() { # mutation_one <id> <file> <from> <to> <expect-substring>
  local id="$1" file="$2" from="$3" to="$4" expect="$5"
  local backup="$SCRATCH/mut-$id.bak"
  echo "-- $id  $file: '$from' -> '$to'"
  cp "$file" "$backup" || { echo "  FAIL $id could not back $file up"; fail=1; return 1; }
  sed -i "s|$from|$to|" "$file"
  if ! grep -qF -- "$to" "$file"; then
    cp "$backup" "$file"; echo "  FAIL $id was not applied (pattern not in $file)"; fail=1; return 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mut-$id-build.log" 2>&1; then
    cp "$backup" "$file"; echo "  FAIL $id did not build (mut-$id-build.log)"; fail=1; return 1
  fi
  local log="$SCRATCH/mut-$id-gate.log" grc
  T2V_QUICK=1 T2V_SCRATCH="$SCRATCH/mut-$id/scratch" T2V_OUT="$SCRATCH/mut-$id/out" \
    bash "$0" > "$log" 2>&1
  grc=$?
  cp "$backup" "$file"
  cmp -s "$backup" "$file" || { echo "  FAIL $id restore is not byte-identical"; fail=1; return 1; }
  local reds hits
  reds=$(grep -c '^  FAIL' "$log" 2>/dev/null)
  hits=$(grep '^  FAIL' "$log" 2>/dev/null | grep -cF -- "$expect")
  echo "   MUTATION $id rc=$grc red=${reds:-0} expected=${hits:-0} (log mut-$id-gate.log)"
  if [ "$grc" -eq 0 ]; then
    echo "  FAIL $id was NOT caught: the judgment stayed green"; fail=1; return 1
  fi
  if [ "${hits:-0}" -eq 0 ]; then
    echo "  FAIL $id went red, but not on \"$expect\" -- the red is for the wrong reason"; fail=1; return 1
  fi
  echo "  OK   $id was caught on \"$expect\""; okcount=$((okcount + 1))
}

section_mutation() {
  if [ "$MUTATION" -ne 1 ]; then
    echo "=== mutation (skipped: pass --mutation to build and inject three defects) ==="
    return 0
  fi
  echo "=== mutation: three injected defects must turn the judgment red ==="
  if [ "$fail" -ne 0 ]; then
    echo "  FAIL mutation refuses to judge over a red forward run"; fail=1; return 0
  fi
  local fwd; fwd=$(sha256sum "$PRODUCT" | awk '{print $1}')
  echo "   forward exe sha256=$fwd"
  mutation_one m1-capacity native/src/main.cpp \
    'constexpr std::size_t kWorldTriangleCapacity = 400000;' \
    'constexpr std::size_t kWorldTriangleCapacity = 200000;' \
    "the map asked for"
  mutation_one m2-invert native/src/world_material.cpp \
    'if (visible) {' \
    'if (!visible) {' \
    "clip pixel neutrality"
  mutation_one m3-clusterless native/src/world_material.cpp \
    '      ++reading.trianglesWithoutCluster;' \
    '      ++reading.trianglesWithoutCluster; if (true) { ++reading.hiddenTriangles; continue; }' \
    "hid triangles with no cluster"
  echo "   rebuilding the forward tree"
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/mut-restore-build.log" 2>&1; then
    echo "  FAIL the restore rebuild failed (mut-restore-build.log)"; fail=1; return 0
  fi
  local now; now=$(sha256sum "$PRODUCT" | awk '{print $1}')
  if [ "$now" = "$fwd" ]; then
    echo "  OK   the restored build reproduces the forward exe ($fwd)"; okcount=$((okcount + 1))
  else
    echo "  FAIL the restored build is $now, not $fwd -- the source did not come back"; fail=1
  fi
}
section_mutation

echo
echo "assertions_ok=$okcount"
if [ "$fail" -eq 0 ]; then
  echo "T2-VISUAL-MATRIX=PASS"
else
  echo "T2-VISUAL-MATRIX=FAIL"
fi
exit "$fail"
