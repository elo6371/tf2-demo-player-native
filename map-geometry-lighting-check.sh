#!/usr/bin/env bash
# map-geometry-lighting-check.sh -- T62: the map matrix geometry and lighting
#                                  axes, for every map in the matrix.
#
# WHY THIS FILE EXISTS
#   MAP-MATRIX.md carries two axes that only pl_odyssey had a gate for. The
#   geometry axis was world-geometry-hole-check.sh and the lighting axis was
#   lighting-dark-account-check.sh; both hardcode maps/pl_odyssey.bsp and pin
#   populations that only that camera produces, so no other map could be
#   measured without copying the file. This gate asks the same two questions of
#   every map in the matrix, from a capture matrix it makes itself, and asserts
#   only invariants that do not depend on a map own numbers.
#
# WHAT IT ASSERTS (per map, at the tick-2000 camera the map matrix resolved)
#   geometry, read from the BSP by world_geometry_probe:
#     G1  the BSP parses and its face account is a partition, twice over:
#         rendered + sky + skippedNoDraw + skippedInvalid + skippedBrushModel
#         == total faces, and worldFaces + brushFaces == total faces.
#     G2  the map has sky brush polygons (a sky mask needs them).
#     G3  every lightmap sample an emitted vertex reads is written
#         (unwrittenTexels == 0): a black pixel has to be map data, not a
#         lightmap page with holes in it.
#   geometry, read from the captured frame, in two instruments.
#
#   The set algebra (world-empty-lattice.py) is what decides the axis:
#     G4  the frame has empty pixels nobody drew (flat-novis-nocull), so the
#         algebra below is not vacuous.
#     G5  the four flat captures nest. base (both restrictors on) is a subset of
#         each single-restrictor frame, and each of those is a subset of
#         flat-novis-nocull. An empty pixel can only get emptier as a restrictor
#         is removed; the reverse -- a pixel drawn with a restrictor on and not
#         drawn with it off -- is a violation no rasteriser produces.
#     G6  the removals are accounted for: union = cull_only + pvs_only - overlap
#         and union <= both-off empty, i.e. every pixel either restrictor alone
#         recovers is also recovered with both off.
#     G7  the albedo/lit relation is one-directional and holds: a pixel whose
#         albedo (--world-lightmap off) is dark must be dark in the lit frame.
#         The reverse does not hold -- bright albedo times dark light is a dark
#         lit pixel -- so only the direction that is an invariant is asserted.
#
#   The point oracle (--world-hole-trace) is the second instrument, and it is
#   what lets this gate say what an empty pixel IS rather than only that it is
#   classified. On sampled pixels from each class it reports the world triangle
#   along the ray (material, face, tri), the depth, and the captured depth:
#     an empty pixel with any=- has no world triangle along its ray at all (open
#     sky, drawn by the cubemap); an empty pixel with drawn=- any=face=  has a
#     triangle the cull or the PVS clip removed. The gate requires every class
#     that has samples to be explained by one of those two shapes.
#
#   WHY THE SKY-BRUSH SHARE IS NO LONGER A VERDICT
#     The first version judged a map by how much of its empty population the BSP
#     sky brushes covered, and only where that coverage was high enough
#     to be named sky. That measured the instrument, not the frame: the
#     product draws its sky as a six-face cubemap (trace: world sky-upload
#     faces=6), so on a map with a 3D skybox the BSP brushes cover 100% of the
#     empties on odyssey, 98.2% on snakewater, 62.9% on bagel rc11, 11.3% on
#     bagel rc12 and 0.4% on sunshine. The share is still printed as a reading;
#     it decides nothing. The 3824 pixels it used to call unexplained on
#     snakewater are, by the algebra and by the oracle, open sky: no triangle
#     along the ray, nothing removed by either restrictor. No map gets a claim
#     that a hole below a share is ruled out; a hole this gate can see is a
#     violation of G5-G7, which are map independent.
#   lighting, read from the same frames by dark-account.py:
#     L1  the frame has dark pixels (otherwise L2/L3 are vacuous).
#     L2  the dark buckets are a partition of the dark population (every dark
#         pixel landed in exactly one bucket) and none of them is ambiguous.
#     L3  the account product-attributable population (bright albedo x bright
#         lightmap rectangle) is at most MAX_ATTRIBUTABLE_FRAME_PCT of the
#         frame. It is a coarse proxy -- a whole lightmap rectangle is called
#         bright by its mean, so a rectangle that is mostly dark still counts --
#         and a decode regression that loses bright texels moves it by an order
#         of magnitude, not by tenths of a percent: odyssey 0.48%, snakewater
#         1.00% of the frame.
#   And, for every map, the frames agree on the pose and the tick: all ten
#   captures report the requested frame tick and the same scene camera eye.
#
# THE CAPTURES (one debug camera, one tick, eleven product runs per map)
#   textured            the product frame the maps are looked at
#   none                --world-geometry none            (clear colour only)
#   sky-faces           --world-geometry sky --world-flat --no-world-cull
#   flat-base           --world-flat
#   flat-novis          --world-flat --no-world-vis-clip
#   flat-nocull         --world-flat --no-world-cull
#   flat-novis-nocull   --world-flat --no-world-vis-clip --no-world-cull
#   ids                 --world-face-id
#   ids-nocull          --world-face-id --no-world-cull
#   lmoff               --world-lightmap off
#   oracle              --world-flat --world-hole-trace <the sampled points>
#   flat-base is copied to flat.bmp because dark-account.py names its frame
#   `flat`; it is the same capture, not a second run.
#   The oracle capture is the eleventh run and it repeats flat-base with the
#   point oracle switched on: G8 asserts the two BMPs are byte identical, so a
#   flag that changed the picture would fail before its reading was used.
#
# WHERE THE CAMERA COMES FROM
#   evidence/t62-map-matrix/<map>/camera.txt, written by map-material-matrix.sh.
#   That file already resolved the map own tick-2000 pose (from the demo camera
#   for the POV recordings, from a product orient run for the two SourceTV
#   recordings), and re-deriving it here would be a second camera definition to
#   drift. A map whose camera.txt is missing is an input error, not a pass.
#   camera2.txt in the same directory names pose 2: p2_tick=, p2_eye=, p2_yaw= and
#   p2_rule= say which kind it is -- a later tick of the same recording (the
#   default; tick= 12000, 8000 on sunshine) or the pose 1 eye turned 90 degrees at
#   the pose 1 tick (p2_rule=yaw+90-fallback). Both are key=value lines, so the
#   gate can read them; rejected_line= records which camera was NOT used and why.
#
# WHAT IT DOES NOT CLAIM
#   The algebra holds on two poses per map (T62 P1-1), which is still not every
#   pose: two cameras are two viewpoints, not a sweep, so a hole visible only
#   from a third place can still be missed. Pose 2 is either a camera the
#   recording passes through later (a different place and heading than pose 1 at
#   tick 2000) or, when the recording has no usable second position inside the
#   budget, the pose 1 eye turned 90 degrees (a second view frustum, not a second
#   moment). The faces behind pose 1's occluders are what a second viewpoint can
#   put in front of the camera instead. What the two poses gained stays a reading:
#   each pose records its own frames, lattice and dark account under its own
#   directory.
#   Coverage is counted per map, not per pose: empty>0 (G4), dark-albedo>0 (G7)
#   and cull-removed>0 (P1-2a) each have to hold SOMEWHERE on the map, and a pose
#   that has none of them is a viewpoint without coverage. Requiring them at every
#   pose would make the gate verdict depend on one map sky distribution rather
#   than on the product: pl_odyssey's pose 1 is the only one of eight sampled
#   cameras on that recording that leaves any pixel empty (1838 of 860784, 0.2%),
#   so a turned second camera there sees a real 47205-colour scene with empty=0.
#   The per-pose numbers are still printed and kept in lattice.txt and
#   dark-stats.txt, and a pose that measures NOTHING at all (one flat colour over
#   the whole frame) fails outright -- see below.
#   It also does not claim a pixel is meant to be sky -- only that the frame that
#   draws everything it can leaves it empty, and that the product own oracle
#   agrees there is nothing along that ray. A low product-attributable population
#   says the dark pixels this frame has are the map own lightmap data, not that
#   every lightmap decode on every map is correct.
#   The back-face cull control (P1-2a) is a reading with bounds, not an invariant:
#   it keeps the face-id pixels the BSP resolves at the eye and removes the ones
#   only the back faces still resolve. A few pixels can appear in the culled frame
#   only, where two draws swap a face id on a shared edge (cp_sunshine: 4 of
#   ~1.06M kept pixels, 3 ppm); the gate fails only when that ratio exceeds a
#   loose 0.1% (1000 ppm) sanity bound, not at the first non-zero. And
#   `drawn_alloff - union == drawn_base` is false, so it is not asserted: a face
#   the cull removes hands its pixels to whatever stands behind it, so the drawn
#   count does not drop by the union (cp_sunshine: drawn_base=444373,
#   drawn_alloff=472566, union=20254 -- a 7939-pixel gap). The gate prints the
#   two accounts side by side and asserts neither against the other.
#   A pose whose camera sits inside the world is not a viewpoint at all: every
#   pixel of its flat frame is one colour, so empty, dark-albedo and cull-removed
#   are all zero for want of anything to measure. The gate fails that pose by
#   name (every pixel of the flat frame is the same colour) instead of letting it
#   pad the run with zero readings; that is what caught pl_odyssey's tick-12000
#   camera, which sat in a wall and produced one flat colour over 860784 pixels.
#   A pose with DEPTH but no empty pixel is not that: it is a real second view
#   that simply did not exercise the sky axis, which the per-map coverage above
#   absorbs while still printing its empty=0 reading.
#
# WHAT IS PINNED (T62 P1-2a, P1-2b)
#   Two things, both per map and pose, both in the matrix rather than in .scratch:
#   frame-pins-<pose>.txt holds the sha256 of each of the ten world frames a pose
#   captures (the G8 oracle differential frame comes later and is not pinned), and
#   cull-reference-<pose>.txt holds cull-control.py's account of which BSP faces
#   the cull removed, on which side of their plane, and how many pixels.
#   A pin that stops matching means THE PICTURE MOVED -- the product draws
#   something else now -- not that this check is broken. Re-pin only when the
#   change is intended (a renderer or map fix, a rebuild that changes shading, a
#   deliberate camera change); the pin header then records head, time and the
#   reason passed to --re-pin. Never re-pin to make a run green.
#   The byte-level assertion inside a run stays: the oracle capture repeats
#   flat-base with --world-hole-trace added and must reproduce it exactly.
#
# Usage: bash map-geometry-lighting-check.sh [--only=<map[,map]>] [--reuse] [--re-pin[=<why>]]
#   --only    judge the named maps only (others are left alone).
#   --reuse   keep frames under .scratch/t62-map-axes, but only when the .build
#             stamp in that directory matches the product being run.
#   --re-pin  rewrite frame-pins-<pose>.txt and cull-reference-<pose>.txt from
#             this run. Say why: --re-pin="fixed the water shading".
# Exit: 0 = every assertion held; 1 = an assertion failed; 2 = an input is
#       missing (a camera.txt, a demo, a BSP, a tool).
set -uo pipefail
cd "$(dirname "$0")"

# The interpreter is fixed: the shell python3 here is an App Execution Alias
# stub (rc=49, no output) and a bare python is not the one the evidence was
# measured with. Override with PYTHON=... only when you know you mean it.
PYTHON=${PYTHON:-C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe}
[[ -x $PYTHON ]] || PYTHON=python
TICK_P1=2000
TICK_P2=12000  # the default second-pose tick; camera2.txt may name another later one
SPEED=8
LATTICE_SAMPLES=${LATTICE_SAMPLES:-6}
MAX_ATTRIBUTABLE_FRAME_PCT=2
PRODUCT=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/world_geometry_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
LOCK=${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}
MATRIX=evidence/t62-map-matrix
OUT=${MAP_AXIS_OUT:-evidence/t62-map-axes}
# Hermetic product inputs. The product writes settings.ini and ui.ini on every
# exit (main.cpp saveSettings(persistent) at 11613-11625, native_ui.cpp
# recentDemo0..7 MRU at 137-139), so a gate that lets it write the operator's
# real profile makes the next run start from different input -- and a run that
# edited tracked files elsewhere is a repository contamination. Both paths point
# at copies under the matrix directory.
RUN_INPUTS=$MATRIX/inputs
SETTINGS_FILE=$RUN_INPUTS/settings.ini
UI_FILE=$RUN_INPUTS/ui.ini
SCRATCH=.scratch/t62-map-axes
MAPS=(cp_snakewater_final1 pl_odyssey cp_sunshine koth_bagel_rc12 koth_bagel_rc11)
FRAMES=(textured none sky-faces flat-base flat-novis flat-nocull flat-novis-nocull ids ids-nocull lmoff)
# p1 keeps the paths the earlier runs used (evidence/<map>/...); p2 gets its own
# directory so its readings are separate instead of overwriting p1.
POSES=(p1 p2)
# --poses= narrows the loop (a sampling run, not the five-map verdict).
POSES_ARG=""

ONLY=""
REUSE=0
REPIN=0
REPIN_WHY=""
usage() {
  echo "usage: $0 [--only=<map[,map]>] [--poses=p1|p2|p1,p2] [--reuse] [--re-pin[=<why>]]"
  echo "  --only    judge the named maps only"
  echo "  --poses   judge only the named pose(s); a sampling run, so the per-map"
  echo "            coverage asserts are printed as readings instead"
  echo "  --reuse   keep frames under .scratch/t62-map-axes whose .build stamp matches the product"
  echo "  --re-pin  rewrite the frame pins and the cull reference from THIS run; say why"
}
for arg in "$@"; do
  case $arg in
    --only=*) ONLY=${arg#*=} ;;
    --poses=*) POSES_ARG=${arg#*=} ;;
    --reuse) REUSE=1 ;;
    --re-pin) REPIN=1 ;;
    --re-pin=*) REPIN=1; REPIN_WHY=${arg#*=} ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

if [[ -n $POSES_ARG ]]; then
  case $POSES_ARG in
    p1) POSES=(p1) ;;
    p2) POSES=(p2) ;;
    p1,p2|p2,p1) POSES=(p1 p2) ;;
    *) echo "--poses= takes p1, p2 or p1,p2 (got $POSES_ARG)" >&2; exit 2 ;;
  esac
  echo "POSES: judging ${POSES[*]} only -- a sampling run, so the per-map coverage asserts are readings, not verdicts"
fi

for tool in "$PRODUCT" "$PROBE"; do
  if [[ ! -x $tool ]]; then echo "missing tool: $tool (run bash build-target.sh tf2_demo_native world_geometry_probe)" >&2; exit 2; fi
done

PRODUCT_SHA=$(sha256sum "$PRODUCT" | cut -d" " -f1)

# HOLD_LOCK=1: this process is already inside `hostlock.sh run t62-mapaxes ...`
# and owns the shared lock for its whole lifetime (a reserved window). Nested
# acquisition would deadlock, so captures run directly -- but only after this
# check proves the lock really is ours; otherwise every capture would run
# unprotected, which is the one thing the lock exists to prevent.
HOLD_LOCK=${HOLD_LOCK:-0}
# hostlock.sh keeps host.lock, queue and reserved next to itself.
HL_ROOT=$(dirname "$LOCK")
if [[ $HOLD_LOCK == 1 ]]; then
  held_owner=$(head -1 "$HL_ROOT/host.lock/owner" 2>/dev/null | tr -d "\r")
  if [[ ! -d $HL_ROOT/host.lock || $held_owner != t62-mapaxes ]]; then
    echo "HOLD_LOCK=1 but the shared lock is not held by t62-mapaxes (owner=${held_owner:-none}); refusing to run captures unprotected" >&2
    exit 2
  fi
  echo "HOLD_LOCK=1: captures run inside the held host lock (owner=t62-mapaxes); no per-capture acquire"
fi

locked() {
  if [[ $HOLD_LOCK == 1 ]]; then
    touch "$HL_ROOT/host.lock/start" 2>/dev/null || true
    bash -c "$1"
    return $?
  fi
  bash "$LOCK" run t62-mapaxes bash -c "$1"
}

map_bsp_rel() {
  case $1 in
    cp_snakewater_final1|pl_odyssey|cp_sunshine) echo "maps/$1.bsp" ;;
    koth_bagel_rc12|koth_bagel_rc11) echo "download/maps/$1.bsp" ;;
    *) echo "" ;;
  esac
}

field() { # field <line> <key> -> the value of key=value in a space-separated line
  local tok
  for tok in $1; do
    case $tok in
      "$2"=*) printf "%s" "${tok#*=}"; return 0 ;;
    esac
  done
  return 1
}

failures=0
failure() { echo "  FAIL $*"; failures=$((failures + 1)); }

assert_eq() { # assert_eq <label> <got> <want>
  if [[ ${2:-NA} == "$3" ]]; then
    echo "  OK $1 = $2"
  else
    failure "$1 = ${2:-NA}, expected $3"
  fi
}

assert_positive() { # assert_positive <label> <value>
  case ${2:-} in
    ""|*[!0-9]*) failure "$1 = ${2:-NA}, expected a positive integer"; return 1 ;;
  esac
  if [[ $2 -gt 0 ]]; then echo "  OK $1 = $2"; else failure "$1 = $2, expected > 0"; return 1; fi
}

bmp_dims() { # bmp_dims <file> -> WxH, exit 1 if it is not a BMP header we can read
  "$PYTHON" - "$1" <<'PY'
import struct, sys
with open(sys.argv[1], 'rb') as handle:
    head = handle.read(30)
if len(head) < 30 or head[:2] != b'BM':
    sys.exit(1)
w, h = struct.unpack('<ii', head[18:26])
print('%dx%d' % (w, abs(h)))
PY
}

capture() { # capture <name> [extra world flags...]
  local name=$1; shift
  local trace=$CAP/$name.trace bmp=$CAP/$name.bmp csv=$CAP/$name.csv log=$CAP/$name.log
  # A frame on disk is only this build's frame: --reuse must never hand an old
  # build's picture to an assertion that is about this build.
  # The stamp is per frame, not per pose: a run killed midway leaves some frames
  # captured with the new binary and the rest from an older one, and one pose-wide
  # stamp would then reuse the stale ones under the new hash.
  if [[ $REUSE -eq 1 && -s $bmp && -s "$CAP/.build.$name" && "$(cat "$CAP/.build.$name")" == "$PRODUCT_SHA" ]]; then
    if [[ ! -s "$CAP/.size" || "$(bmp_dims "$bmp")" == "$(cat "$CAP/.size")" ]]; then
      echo "  reused $name"; return 0
    fi
  fi
  local -a args=( "$PRODUCT" --tf-root "$TF" --audio-device 6 --audio-disabled
    --hidden-window --settings-file "$(cygpath -m "$SETTINGS_FILE")"
    --ui-settings-file "$(cygpath -m "$UI_FILE")"
    --demo "$DEMO" --playback-speed "$SPEED" --capture-tick "$TICK"
    --capture-frame "$(cygpath -m "$bmp")" --metrics-file "$(cygpath -m "$csv")"
    --debug-camera "$CX" "$CY" "$CZ" "$CP" "$CYAW" "$@" )
  local cmd attempt rc dims want other
  cmd=$(printf "%q " TF2_NATIVE_TRACE="$(cygpath -m "$trace")" timeout 400 "${args[@]}")
  for attempt in 1 2; do
    locked "$cmd" > "$log" 2>&1
    rc=$?
    if [[ $rc -ne 0 || ! -s $bmp ]]; then
      echo "  FAIL capture $name rc=$rc"
      tail -3 "$log" | grep -a . | tail -3 | while read -r line; do echo "    $line"; done
      return 1
    fi
    if ! grep -aq "capture: armed" "$trace"; then echo "  FAIL capture $name wrote no armed line"; return 1; fi
    dims=$(bmp_dims "$bmp")
    if [[ -z $dims ]]; then echo "  FAIL capture $name wrote a bmp with no readable header"; return 1; fi
    printf "%s" "$cmd" > "$CAP/name.$name.cmd"
    want=""
    [[ -s "$CAP/.size" ]] && want=$(cat "$CAP/.size")
    if [[ -z $want ]]; then printf "%s" "$dims" > "$CAP/.size"; break; fi
    if [[ $dims == "$want" ]]; then break; fi
    # The window's client size is the frame size, and the hole account compares
    # the frames pixel for pixel, so one run has one picture size. One capture
    # came back 833x439 against the run's 1264x681 on 2026-11-05; a one-off size
    # is a launch race and comes back right on a second try, so the first try is
    # retried and the retry is logged. If the size is stable, the frame is real:
    # every earlier frame of the other size is recaptured, and only a frame that
    # still disagrees after that fails.
    if [[ $attempt -eq 1 ]]; then
      echo "  NOTE capture $name wrote $dims, the run's frames are $want -- retrying once"
      rm -f "$bmp"; continue
    fi
    echo "  NOTE capture $name wrote $dims twice, so the run's frame size is $dims and the frames of $want are recaptured"
    printf "%s" "$dims" > "$CAP/.size"
    for other in "$CAP"/name.*.cmd; do
      [[ -s $other ]] || continue
      local obmp=$CAP/${other##*/name.}; obmp=${obmp%.cmd}.bmp
      [[ -s $obmp ]] || continue
      [[ "$(bmp_dims "$obmp")" == "$dims" ]] && continue
      echo "  NOTE recapturing ${other##*/name.} (it was $(bmp_dims "$obmp"), the run's frames are $dims)"
      locked "$(cat "$other")" > "${other%.cmd}.log" 2>&1
      local orc=$?
      if [[ $orc -ne 0 || ! -s $obmp ]]; then echo "  FAIL recapture of ${other##*/name.} rc=$orc"; return 1; fi
      if [[ "$(bmp_dims "$obmp")" != "$dims" ]]; then
        echo "  FAIL ${other##*/name.} is $(bmp_dims "$obmp") after a recapture, the run's frames are $dims"
        return 1
      fi
    done
    break
  done
  printf "%s" "$PRODUCT_SHA" > "$CAP/.build.$name"
  echo "  captured $name"
}

mkdir -p "$OUT" "$SCRATCH" "$RUN_INPUTS" || exit 2
# Seed the input copies from the real profile once, read only. After that the
# run's input is whatever is in $RUN_INPUTS and nothing else.
for pair in "settings.ini:$SETTINGS_FILE" "ui.ini:$UI_FILE"; do
  name=${pair%%:*}; dest=${pair#*:}
  if [[ ! -s $dest ]]; then
    real="$LOCALAPPDATA/TF2 Demo Player/$name"
    if [[ -f $real ]]; then
      cp "$real" "$dest" && echo "seeded $dest from the real profile (read only)"
    else
      : > "$dest" && echo "seeded $dest empty (no real profile to copy)"
    fi
  fi
done
maps_judged=0
geometry_ok=0
lighting_ok=0

for MAP in "${MAPS[@]}"; do
  if [[ -n $ONLY ]]; then
    hit=0
    for want in ${ONLY//,/ }; do [[ $want == "$MAP" ]] && hit=1; done
    if [[ $hit -eq 0 ]]; then continue; fi
  fi
  echo ""
  echo "== $MAP =="
  DIR=$OUT/$MAP
  CAP=$SCRATCH/$MAP
  mkdir -p "$DIR" "$CAP"
  CAM_FILE=$MATRIX/$MAP/camera.txt
  if [[ ! -s $CAM_FILE ]]; then
    failure "$MAP: missing $CAM_FILE -- the map matrix has not resolved this map camera"
    continue
  fi
  DEMO=$(grep -a "^demo=" "$CAM_FILE" | head -1 | cut -d= -f2-)
  CAM=$(grep -a "^debug_camera_args=--debug-camera " "$CAM_FILE" | head -1 | cut -d" " -f2-)
  BSP_REL=$(map_bsp_rel "$MAP")
  if [[ -z $DEMO || ! -f $DEMO ]]; then
    failure "$MAP: the demo in $CAM_FILE is missing: ${DEMO:-none}"
    continue
  fi
  if [[ -z $BSP_REL || ! -f $TF/$BSP_REL ]]; then
    failure "$MAP: the BSP is missing: $TF/${BSP_REL:-none}"
    continue
  fi
  if [[ -z $CAM ]]; then
    failure "$MAP: $CAM_FILE has no debug_camera_args line"
    continue
  fi
  CAM2_FILE=$MATRIX/$MAP/camera2.txt
  if [[ ! -s $CAM2_FILE ]]; then
    failure "$MAP: missing $CAM2_FILE -- the second pose is part of this gate now"
    continue
  fi
  DEMO2=$(grep -a "^demo=" "$CAM2_FILE" | head -1 | cut -d= -f2-)
  CAM2=$(grep -a "^debug_camera_args=--debug-camera " "$CAM2_FILE" | head -1 | cut -d" " -f2-)
  TICK2=$(grep -a "^tick=" "$CAM2_FILE" | head -1 | cut -d= -f2-)
  if [[ -n $DEMO2 && $DEMO2 != "$DEMO" ]]; then
    failure "$MAP: camera2.txt uses demo $DEMO2 but camera.txt uses $DEMO (both poses are the same demo)"
    continue
  fi
  if [[ -z $CAM2 ]]; then
    failure "$MAP: $CAM2_FILE has no five-number debug_camera_args line"
    continue
  fi
  case ${TICK2:-} in
    ""|*[!0-9]*) failure "$MAP: $CAM2_FILE has no numeric tick= line"; continue ;;
  esac
  P2_RULE=$(grep -a "^p2_rule=" "$CAM2_FILE" | head -1 | cut -d= -f2-)
  if [[ -z $P2_RULE ]]; then
    failure "$MAP: $CAM2_FILE has no p2_rule= line, so the run cannot say what kind of second pose it is"
    continue
  fi
  if [[ $P2_RULE == yaw+90-fallback ]]; then
    P2_REJECTED=$(grep -a "^rejected_line=" "$CAM2_FILE" | head -1)
    if [[ -z $P2_REJECTED ]]; then
      failure "$MAP: $CAM2_FILE says p2_rule=yaw+90-fallback but has no rejected_line= recording the camera that was not used"
    else
      echo "  NOTE $MAP pose 2 is the yaw+90 fallback; not used: $(printf "%s" "$P2_REJECTED" | cut -c1-170)"
    fi
  fi
  if [[ $TICK2 -le $TICK_P1 && $CAM2 == "$CAM" ]]; then
    failure "$MAP: $CAM2_FILE is pose 1 itself (tick $TICK2 and the same camera)"
    continue
  fi
  # Pose 2 has to be a different camera, not a later moment: a later tick of the
  # recording is one way (the default), the pose 1 eye turned 90 degrees at the
  # same tick is the fallback (camera2.txt says which with p2_rule=). A turned
  # camera is a second view frustum without inventing a position, so the gate
  # asks for "not pose 1" and records which kind pose 2 is.
  if [[ $TICK2 -le $TICK_P1 ]]; then
    echo "  NOTE $MAP pose 2 keeps tick $TICK2 and turns the camera: a second view frustum, not a second moment"
  fi
  CAM_P1=$CAM

  # Coverage is counted per map (see WHAT IT DOES NOT CLAIM): the three vacuity
  # axes have to be exercised somewhere on the map, not at every pose.
  cov_empty=0
  cov_dark=0
  cov_cullrem=0

  map_before=$failures
  for POSE in "${POSES[@]}"; do
    if [[ $POSE == p1 ]]; then
      DIR=$OUT/$MAP; CAP=$SCRATCH/$MAP; TICK=$TICK_P1; CAM=$CAM_P1
    else
      DIR=$OUT/$MAP/p2; CAP=$SCRATCH/$MAP/p2; TICK=$TICK2; CAM=$CAM2
    fi
    mkdir -p "$DIR" "$CAP"
    read -r CX CY CZ CP CYAW <<<"$CAM"
    if [[ -z ${CYAW:-} ]]; then failure "$MAP/$POSE: the camera line is not five numbers: $CAM"; continue; fi
    if [[ $POSE == p1 ]]; then POSE_RULE=recording-tick; else POSE_RULE=$P2_RULE; fi
    echo "  == $MAP/$POSE == eye=($CX, $CY, $CZ) pitch=$CP yaw=$CYAW tick=$TICK speed=$SPEED rule=$POSE_RULE"
    f_before=$failures
    ok=1
    capture textured || ok=0
  capture none --world-geometry none || ok=0
  capture sky-faces --world-geometry sky --world-flat --no-world-cull || ok=0
  capture flat-base --world-flat || ok=0
  capture flat-novis --world-flat --no-world-vis-clip || ok=0
  capture flat-nocull --world-flat --no-world-cull || ok=0
  capture flat-novis-nocull --world-flat --no-world-vis-clip --no-world-cull || ok=0
  capture ids --world-face-id || ok=0
  capture ids-nocull --world-face-id --no-world-cull || ok=0
  capture lmoff --world-lightmap off || ok=0
  if [[ $ok -eq 0 ]]; then
    echo "  (a capture failed: this map is not judged)"
    continue
  fi
  cp "$CAP/flat-base.bmp" "$CAP/flat.bmp"

  # Every frame must be the requested tick at the same pose. The pose is read
  # from the trace, not from the command line, so a capture that fell back to
  # the demo camera is caught here rather than compared against itself later.
  EYESET=""
  poses=0
  for name in "${FRAMES[@]}"; do
    armed=$(grep -a "capture: armed" "$CAP/$name.trace" | tail -1)
    req=$(field "$armed" requested)
    got=$(field "$armed" frame_tick)
    if [[ $req != "$TICK" || $got != "$TICK" ]]; then
      failure "$MAP $name: capture requested=${req:-NA} frame_tick=${got:-NA}, expected $TICK"
      poses=1
    fi
    camera=$(grep -a "scene: camera " "$CAP/$name.trace" | tail -1)
    eye=$(field "$camera" eye)
    if [[ -z $EYESET ]]; then
      EYESET=$eye
    elif [[ $eye != "$EYESET" ]]; then
      failure "$MAP $name: camera eye=$eye, not $EYESET"
      poses=1
    fi
  done
  if [[ $poses -eq 0 ]]; then echo "  OK every frame armed at tick $TICK with eye=$EYESET"; fi

  {
    echo "map=$MAP"
    echo "bsp=$BSP_REL"
    echo "demo=$DEMO"
    echo "tick=$TICK"
    echo "speed=$SPEED"
    echo "camera=--debug-camera $CAM"
    echo "pose_eye=$EYESET"
    echo "product_sha256=$(sha256sum "$PRODUCT" | cut -d" " -f1)"
    echo "probe_sha256=$(sha256sum "$PROBE" | cut -d" " -f1)"
    echo "head=$(git rev-parse HEAD 2>/dev/null || echo NA)"
    for name in "${FRAMES[@]}"; do
      echo "frame $name sha256=$(sha256sum "$CAP/$name.bmp" | cut -d" " -f1) bytes=$(wc -c < "$CAP/$name.bmp")"
    done
    echo "frame_flat=copy of flat-base.bmp"
  } > "$DIR/captures.txt"

  # P1-2b: the frame pins. These hashes say what the picture is at this pose and
  # tick, so a diff means the product draws something else now -- a fact about the
  # product, not a broken check. The pin lives in the matrix, not in .scratch,
  # because it is evidence; captures.txt keeps recording hashes per run as before.
  PINS=$MATRIX/$MAP/frame-pins-$POSE.txt
  PINS_NOW=$CAP/frame-pins.txt
  {
    echo "# product_sha256=$PRODUCT_SHA"
    echo "map=$MAP"
    echo "pose=$POSE"
    echo "tick=$TICK"
    echo "speed=$SPEED"
    echo "camera=--debug-camera $CAM"
    echo "eye=$EYESET"
    echo "demo=$DEMO"
    for name in "${FRAMES[@]}"; do
      echo "frame $name sha256=$(sha256sum "$CAP/$name.bmp" | cut -d" " -f1) bytes=$(wc -c < "$CAP/$name.bmp")"
    done
  } > "$PINS_NOW"
  pin_header="pinned=$(date -u +%Y-%m-%dT%H:%M:%SZ) head=$(git rev-parse --short HEAD 2>/dev/null || echo NA) reason=${REPIN_WHY:-unspecified}"
  if [[ ! -s $PINS ]]; then
    if [[ $REPIN -eq 1 ]]; then
      { echo "$pin_header"; cat "$PINS_NOW"; } > "$PINS"
      echo "  RE-PIN wrote $PINS (reason: ${REPIN_WHY:-unspecified})"
    else
      failure "$MAP/$POSE: no pins at $PINS -- the first run needs --re-pin=<why> so the reference has a reason"
    fi
  elif [[ $REPIN -eq 1 ]]; then
    prev=$(grep -a "^pinned=" "$PINS" | head -3 | sed "s/^/# previous pin: /")
    { echo "$pin_header"; printf "%s\n" "$prev"; cat "$PINS_NOW"; } > "$PINS"
    echo "  RE-PIN moved the reference to this run (reason: ${REPIN_WHY:-unspecified})"
  else
    pin_diff=$(diff <(grep -a "^frame " "$PINS") <(grep -a "^frame " "$PINS_NOW") | grep -a "^[<>]" | head -6 | paste -sd" " -)
    if [[ -z $pin_diff ]]; then
      echo "  OK the frames are the pinned picture ($(grep -ac "^frame " "$PINS") frames)"
    else
      failure "$MAP/$POSE: THE PICTURE MOVED: a frame no longer matches $PINS. That says what the product draws changed, not that this check broke; if the change is intended, re-pin with --re-pin=<why> and record it in the run notes. $pin_diff"
    fi
  fi

  # G1/G2/G3: the BSP own account.
  geo_before=$failures
  "$PROBE" --tf-root "$TF" --map "$BSP_REL" > "$DIR/geometry.txt.new" 2>&1
  probe_rc=$?
  mv -f "$DIR/geometry.txt.new" "$DIR/geometry.txt"
  if [[ $probe_rc -ne 0 ]]; then failure "$MAP: the geometry probe exited $probe_rc"; fi
  status_line=$(grep -a "geometry status=" "$DIR/geometry.txt" | head -1)
  if [[ $(field "$status_line" status) == ok ]]; then
    echo "  OK the BSP parsed"
  else
    failure "$MAP: the BSP did not parse: ${status_line:-no status line}"
  fi
  faces=$(grep -a "geometry faces total=" "$DIR/geometry.txt" | head -1)
  total=$(field "$faces" total)
  rendered=$(field "$faces" rendered)
  skyfaces=$(field "$faces" sky)
  nodraw=$(field "$faces" skippedNoDraw)
  invalid=$(field "$faces" skippedInvalid)
  brushskipped=$(field "$faces" skippedBrushModel)
  worldfaces=$(field "$faces" worldFaces)
  brushfaces=$(field "$faces" brushFaces)
  if [[ -n $total && -n $rendered && -n $skyfaces && -n $nodraw && -n $invalid && -n $brushskipped ]]; then
    assert_eq "face partition rendered+sky+skippedNoDraw+skippedInvalid+skippedBrushModel" "$((rendered + skyfaces + nodraw + invalid + brushskipped))" "$total"
  else
    failure "$MAP: the face account is incomplete: ${faces:-no faces line}"
  fi
  if [[ -n $total && -n $worldfaces && -n $brushfaces ]]; then
    assert_eq "face partition worldFaces+brushFaces" "$((worldfaces + brushfaces))" "$total"
  else
    failure "$MAP: the world/brush face split is missing: ${faces:-no faces line}"
  fi
  skyline=$(grep -a "geometry sky faces=" "$DIR/geometry.txt" | head -1)
  assert_positive "sky brush polygons" "$(field "$skyline" triangles)" || true
  lmline=$(grep -a "geometry lightmap-uv " "$DIR/geometry.txt" | head -1)
  assert_eq "lightmap samples an emitted vertex reads but the page never wrote" "$(field "$lmline" unwrittenTexels)" 0

  # G4-G6: the frame own account of what it left empty, as set algebra over the
  # four flat captures. This, not the sky-brush share, is the geometry verdict:
  # it names every empty pixel either as open sky (no world triangle along the
  # ray, even with both restrictors off) or as a restrictor removal (a triangle
  # was there and the backface cull or the PVS clip took it away), and it does so
  # without needing a sky mask that only some maps can localise.
  $PYTHON world-geometry-hole-account.py --dir $CAP --out $CAP/analysis > $DIR/hole-account.txt 2>&1
  hole_rc=$?
  if [[ $hole_rc -ne 0 ]]; then failure "$MAP: the hole account exited $hole_rc"; fi
  hstatus=$(grep -a "HOLE status=" "$DIR/hole-account.txt" | head -1)
  if [[ $(field "$hstatus" status) != ok ]]; then
    failure "$MAP: the hole account did not start: ${hstatus:-no status line}"
  fi
  totals=$(grep -a "HOLE totals " "$DIR/hole-account.txt" | head -1)
  missing=$(field "$totals" missing)
  unexplained=$(field "$totals" unexplained)
  skycovered=$(field "$totals" sky)
  [[ -n $skycovered && $skycovered =~ ^[0-9]+$ ]] || skycovered=0
  if [[ -n $missing && $missing =~ ^[0-9]+$ ]]; then
    sky_share=$(awk -v s="$skycovered" -v m="$missing" 'BEGIN{printf "%.1f", (m > 0 ? 100.0 * s / m : 0)}')
    echo "  reading: empty=$missing skyMaskCovered=$skycovered ($sky_share%) outsideSkyMask=$unexplained (a reading; the verdict is G4-G8)"
  else
    failure "$MAP: the hole account gave no empty population"
  fi

  # Write to .new then rename: a run killed mid-way must not truncate the evidence an earlier run wrote.
  $PYTHON world-empty-lattice.py --dir "$CAP" --samples "$LATTICE_SAMPLES" > "$DIR/lattice.txt.new" 2>&1
  lattice_rc=$?
  mv -f "$DIR/lattice.txt.new" "$DIR/lattice.txt"
  lframe=$(grep -a "LATTICE frame=" "$DIR/lattice.txt" | head -1)
  lmono=$(grep -a "LATTICE monotone " "$DIR/lattice.txt" | head -1)
  lrestrict=$(grep -a "LATTICE restrict " "$DIR/lattice.txt" | head -1)
  llight=$(grep -a "LATTICE light " "$DIR/lattice.txt" | head -1)
  lcolours=$(grep -a "LATTICE colours flat-base " "$DIR/lattice.txt" | head -1)
  cflat=$(field "$lcolours" flat)
  cclear=$(field "$lcolours" clear)
  cother=$(field "$lcolours" other)
  if [[ $lattice_rc -ne 0 ]]; then
    failure "$MAP: the empty-population algebra failed: $(grep -a "LATTICE violated" "$DIR/lattice.txt" | head -1 | cut -c1-140)"
  fi
  # A pose that sees one flat surface measures nothing: every pixel of its flat
  # frame carries the same colour. That is what a camera sitting inside the world
  # produces, and it is a failure of the pose, not a pass with zero readings.
  echo "  reading: flat-base colours flat=${cflat:-NA} clear=${cclear:-NA} other=${cother:-NA} (one colour over the whole frame means the camera sees no depth)"
  if [[ -n $cflat && -n $cclear && -n $cother ]]; then
    if [[ $cclear -eq 0 && $cother -eq 0 ]]; then
      failure "$MAP/$POSE: every pixel of the flat frame is the same colour ($cflat of $((cflat + cclear + cother)) pixels), so this pose sees one flat surface and measures nothing -- camera2.txt needs a view with depth"
    fi
  else
    failure "$MAP/$POSE: the lattice gave no colour account: ${lcolours:-no LATTICE colours line}"
  fi
  empty_alloff=$(field "$lframe" empty_alloff)
  drawn_base=$(field "$lframe" drawn_base)
  # The empty population is an algebra -- the lattice script fails when the four
  # flat captures disagree -- so what is asserted per pose is the agreement, and
  # the count itself feeds the per-map coverage bound below.
  cov_empty=$(( cov_empty + ${empty_alloff:-0} ))
  assert_positive "drawn pixels with both restrictors on (G4)" "${drawn_base:-}" || true
  assert_eq "pixels drawn with both restrictors on and not with the vis clip off (G5)" "$(field "$lmono" base_not_in_novis)" 0
  assert_eq "pixels drawn with both restrictors on and not with the cull off (G5)" "$(field "$lmono" base_not_in_nocull)" 0
  assert_eq "pixels the vis-off capture draws outside the both-off capture (G5)" "$(field "$lmono" novis_not_in_all)" 0
  assert_eq "pixels the cull-off capture draws outside the both-off capture (G5)" "$(field "$lmono" nocull_not_in_all)" 0
  cull_only=$(field "$lrestrict" cull_only)
  pvs_only=$(field "$lrestrict" pvs_only)
  overlap=$(field "$lrestrict" overlap)
  runion=$(field "$lrestrict" union)
  rboth=$(field "$lrestrict" both)
  if [[ $cull_only =~ ^[0-9]+$ && $pvs_only =~ ^[0-9]+$ && $overlap =~ ^[0-9]+$ && $runion =~ ^[0-9]+$ && $rboth =~ ^[0-9]+$ ]]; then
    assert_eq "the restrictor union against cull_only+pvs_only-overlap (G6)" "$runion" "$((cull_only + pvs_only - overlap))"
    if [[ $runion -le $rboth ]]; then
      echo "  OK the restrictor union = $runion pixels, within the $rboth the two of them remove together (G6)"
    else
      failure "$MAP: the restrictor union = $runion, larger than the $rboth pixels both restrictors remove"
    fi
  else
    failure "$MAP: the restrictor account is incomplete: ${lrestrict:-no line}"
  fi
  assert_eq "drawn pixels whose albedo is dark but the frame is not (G7)" "$(field "$llight" albedo_not_lit)" 0
  # Whether a pose has any dark albedo pixel at all feeds the per-map coverage
  # bound; the attribution above is what this pose itself has to get right.
  cov_dark=$(( cov_dark + $(field "$llight" albedo_dark || echo 0) ))
  echo "  reading: this pose has $(field "$llight" albedo_dark || echo NA) dark-albedo pixels (coverage is counted per map)"

  # G8: the point oracle explains what the two classes ARE, rather than only that
  # the algebra classified them. It rides on the pose the base class was defined
  # against (both restrictors on, --world-flat only and nothing else), so an
  # open-sky ray must carry no world triangle at all and a removal ray must carry
  # exactly the triangle that the cull or the PVS clip refuses to draw. Riding on
  # flat-base also makes the frame pollutable in a way cmp can see: the oracle
  # capture has to stay byte-identical to the flat-base capture it repeats.
  # Only empty samples are worth a ray: a drawn sample would be asking the
  # oracle to confirm a pixel the frame already painted. The base column is
  # what the algebra classified, so it is the filter.
  ORACLE_POINTS=$(grep -a "^SAMPLE " "$DIR/lattice.txt" | awk '$4 == "base=empty" {print $2","$3}' | paste -sd';' -)
  if [[ -z $ORACLE_POINTS ]]; then
    # No empty pixel means no unpainted pixel: the sky brush covers
    # everything the world pass leaves. That is a stronger claim than an
    # explained hole, so it is asserted rather than failed -- but only when
    # the algebra really reports an empty population of zero, so a map
    # cannot pass G8 by having nothing to ask about.
    if [[ -n $empty_alloff && $empty_alloff == 0 ]]; then
      echo "  OK no pixel is left unpainted in the both-off capture (G8): the sky brush covers what the world pass leaves"
      echo "ORACLE skipped reason=no-empty-pixel empty_alloff=0" > "$DIR/oracle.txt"
    else
      failure "$MAP: no empty pixel was sampled although empty_alloff=$empty_alloff"
    fi
  else
    oracle_cap=1
    capture oracle --world-flat --world-hole-trace "$ORACLE_POINTS" || oracle_cap=0
    if [[ $oracle_cap -eq 0 ]]; then
      failure "$MAP: the oracle capture failed"
    else
      echo "frame oracle sha256=$(sha256sum "$CAP/oracle.bmp" | cut -d" " -f1) bytes=$(wc -c < "$CAP/oracle.bmp")" >> "$DIR/captures.txt"
      if cmp -s "$CAP/flat-base.bmp" "$CAP/oracle.bmp"; then
        echo "  OK the point oracle did not change the frame it rode on"
      else
        failure "$MAP: adding --world-hole-trace changed the captured frame"
      fi
      $PYTHON world-empty-oracle.py --lattice "$DIR/lattice.txt" --trace "$CAP/oracle.trace" > "$DIR/oracle.txt" 2>&1
      oracle_rc=$?
      if [[ $oracle_rc -ne 0 ]]; then
        failure "$MAP: the oracle did not explain every sampled pixel: $(grep -a "ORACLE failed\|ORACLE verdict=" "$DIR/oracle.txt" | head -2 | paste -sd" " -)"
      else
        echo "  OK $(grep -a "ORACLE points=" "$DIR/oracle.txt" | head -1)"
      fi
    fi
  fi
  # P1-2a: what the back-face cull is supposed to keep and remove. G5 says the
  # algebra explains the empty pixels; this says the cull itself removed a real
  # population from the SAME frame pair (ids vs ids-nocull at this pose and tick)
  # and that the culled frame is a subset of the unculled one. Whether a pose has
  # anything to cull at all is counted for the per-map coverage bound below: a
  # camera looking at one flat wall can legitimately have nothing to remove.
  CULL_REF=$MATRIX/$MAP/cull-reference-$POSE.txt
  CULL_NOW=$CAP/cull-reference.txt
  if [[ -z $EYESET ]]; then
    failure "$MAP/$POSE: no eye came out of the traces, so the cull control cannot run"
  else
    rm -f "$CAP/cull-control.json"
    "$PYTHON" cull-control.py --frames "$CAP" --bsp "$TF/$BSP_REL" "--eye=$EYESET" --json "$CAP/cull-control.json" > "$DIR/cull-control.txt.new" 2>&1
    cull_rc=$?
    mv -f "$DIR/cull-control.txt.new" "$DIR/cull-control.txt"
    if [[ ! -s $CAP/cull-control.json ]]; then
      failure "$MAP/$POSE: the cull control wrote no account (rc=$cull_rc): $(grep -a "CONTROL=FAIL" "$DIR/cull-control.txt" | head -1)"
    else
      "$PYTHON" - "$CAP/cull-control.json" > "$CULL_NOW" <<PY
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
side = d.get("removedSide") or {}
print("cull kept=%d" % d.get("kept", -1))
print("cull removed=%d" % d.get("removed", -1))
print("cull onlyInCull=%d" % d.get("onlyInCullFrame", -1))
print("cull removedFront=%d" % side.get("front", -1))
print("cull removedBack=%d" % side.get("back", -1))
print("cull removedAmbiguous=%d" % (d.get("removed", 0) - side.get("front", 0) - side.get("back", 0)))
print("cull distinctRemovedFaces=%d" % d.get("distinctRemovedFaces", -1))
for fid, n in d.get("topRemovedFaces") or []:
    print("cull face %d pixels=%d" % (fid, n))
PY
      ckept=$(field "$(grep -a "^cull kept=" "$CULL_NOW")" kept)
      cremoved=$(field "$(grep -a "^cull removed=" "$CULL_NOW")" removed)
      conly=$(field "$(grep -a "^cull onlyInCull=" "$CULL_NOW")" onlyInCull)
      cambig=$(field "$(grep -a "^cull removedAmbiguous=" "$CULL_NOW")" removedAmbiguous)
      cfaces=$(field "$(grep -a "^cull distinctRemovedFaces=" "$CULL_NOW")" distinctRemovedFaces)
      assert_positive "face-id pixels the cull kept" "${ckept:-}" || true
      # The control always has to keep pixels; removing some is the per-map
      # coverage bound below (a pose can legitimately have nothing to cull).
      cov_cullrem=$(( cov_cullrem + ${cremoved:-0} ))
      # A face id that appears only in the culled frame is not always zero: where
      # two faces are coincident (or meet at a shared edge) the id a pixel gets can
      # swap between the two captures, and that swap is a rasterization tie, not a
      # cull defect by itself. What would be a defect is a population that scales
      # with surfaces instead of with face edges, so it is bounded at 0.1% of the
      # pixels the cull kept -- a loose sanity bound, not a tolerance fitted to a
      # map, and the exact number is pinned in cull-reference-<pose>.txt either way.
      if [[ -n ${conly:-} && $conly -ne 0 ]]; then
        conly_ppm=$(( ckept > 0 ? conly * 1000000 / ckept : 1000000 ))
        if awk -v c="$conly" -v k="$ckept" "BEGIN{ exit !(k > 0 && c * 1000 <= k) }"; then
          echo "  NOTE $conly face-id pixels appear only in the culled frame ($conly_ppm ppm of kept): coincident-face id swaps; bounded at 0.1% of kept (1000 ppm)"
        else
          failure "$MAP/$POSE: $conly face-id pixels appear only in the culled frame ($conly_ppm ppm of kept), over the 0.1% sanity bound -- the culled frame is not a subset of the unculled one"
        fi
      else
        echo "  OK no face-id pixel appears only in the culled frame (strict subset)"
      fi
      assert_eq "removed pixels on a face whose side the control could not decide" "${cambig:-NA}" 0
      # The control has to be shown able to FAIL: given the same frame twice (a
      # cull that removes nothing) it must refuse the account. Without this the
      # green reading above could come from a control that never says no.
      NEG=$CAP/neg-control
      mkdir -p "$NEG"
      cp "$CAP/ids.bmp" "$NEG/ids.bmp"
      cp "$CAP/ids.bmp" "$NEG/ids-nocull.bmp"
      "$PYTHON" cull-control.py --frames "$NEG" --bsp "$TF/$BSP_REL" "--eye=$EYESET" --json "$NEG/cull.json" > "$DIR/cull-control-negative.txt.new" 2>&1
      neg_rc=$?
      mv -f "$DIR/cull-control-negative.txt.new" "$DIR/cull-control-negative.txt"
      if [[ $neg_rc -eq 1 ]] && grep -qa "CONTROL=FAIL the cull removed nothing" "$DIR/cull-control-negative.txt"; then
        echo "  OK the cull control rejects a cull that removes nothing (negative control)"
      else
        failure "$MAP/$POSE: the cull control did not reject a no-op cull (rc=$neg_rc)"
      fi
      if [[ -n $cull_only && -n $runion ]]; then
        # The lattice cull_only and the control removed count different things: the
        # lattice compares frame pixels under --world-flat, where a culled face can be
        # replaced by the face behind it and the pixel stays drawn; the control compares
        # face ids, so the replacement face shows up as a different id. A small ratio is
        # expected, and the two are recorded side by side rather than asserted equal.
        echo "  reading: lattice cull_only=$cull_only pvs_only=$pvs_only union=$runion drawn_base=$drawn_base vs control removed=$cremoved (face ids; the wider count is the pixels a culled face leaves to the face behind it)"
      else
        failure "$MAP/$POSE: no lattice numbers to read next to the cull control"
      fi
      echo "  reading: cull removed=$cremoved px from $cfaces faces (front=$(field "$(grep -a "^cull removedFront=" "$CULL_NOW")" removedFront) back=$(field "$(grep -a "^cull removedBack=" "$CULL_NOW")" removedBack)); frame cull_only=$cull_only pvs_only=$pvs_only union=$runion"
      if [[ ! -s $CULL_REF ]]; then
        if [[ $REPIN -eq 1 ]]; then
          { echo "reference=$(date -u +%Y-%m-%dT%H:%M:%SZ) head=$(git rev-parse --short HEAD 2>/dev/null || echo NA) reason=${REPIN_WHY:-unspecified}"; cat "$CULL_NOW"; } > "$CULL_REF"
          echo "  RE-PIN wrote $CULL_REF"
        else
          failure "$MAP/$POSE: no cull reference at $CULL_REF -- the first run needs --re-pin=<why>"
        fi
      elif [[ $REPIN -eq 1 ]]; then
        { echo "reference=$(date -u +%Y-%m-%dT%H:%M:%SZ) head=$(git rev-parse --short HEAD 2>/dev/null || echo NA) reason=${REPIN_WHY:-unspecified}"; cat "$CULL_NOW"; } > "$CULL_REF"
        echo "  RE-PIN wrote $CULL_REF"
      else
        cull_diff=$(diff <(grep -a "^cull " "$CULL_REF") <(grep -a "^cull " "$CULL_NOW") | grep -a "^[<>]" | head -8 | paste -sd" " -)
        if [[ -z $cull_diff ]]; then
          echo "  OK the cull kept and removed the same pixels as the pin in $CULL_REF"
        else
          failure "$MAP/$POSE: THE CULL REFERENCE MOVED: the numbers no longer match $CULL_REF. That says what the cull does changed; if it is intended, re-pin with --re-pin=<why>. $cull_diff"
        fi
      fi
    fi
  fi

  if [[ $failures -eq $geo_before && $POSE == p1 ]]; then geometry_ok=$((geometry_ok + 1)); fi

  # L1/L2/L3: the dark population attribution.
  light_before=$failures
  # A ledger left by an earlier run must never stand in for this one: when the
  # analysis crashed, the stale account.json made the checks below read the old
  # numbers and the failure showed up only as a bare rc. Wipe the account first,
  # and print the head of the crash log whenever the analysis fails.
  rm -rf "$CAP/account"
  "$PYTHON" dark-account.py --frames "$CAP" --out "$CAP/account" --bsp "$TF/$BSP_REL" "--eye=$EYESET" --json "$CAP/account/account.json" > "$DIR/dark-account.txt.new" 2>&1
  dark_rc=$?
  mv -f "$DIR/dark-account.txt.new" "$DIR/dark-account.txt"
  # --eye is passed with an equals sign on purpose: odyssey and sunshine have a
  # negative eye X, and argparse reads "--eye -1221.2,462.2,650.3" as a flag.
  dark_rc=$?
  if [[ $dark_rc -ne 0 ]]; then
    # One non-reproducible crash was seen on odyssey (a TypeError inside lum() on
    # a frame pair that then re-ran clean twice); the analysis only reads fixed
    # files, so a second attempt separates a transient from a real failure. Both
    # attempts stay in the log and in the evidence directory.
    echo "  RETRY $MAP: the dark account exited $dark_rc; re-running once on the same frames"
    sed -n '1,6p' "$DIR/dark-account.txt" | sed 's/^/    /'
    sleep 2
    rm -rf "$CAP/account"
    "$PYTHON" dark-account.py --frames "$CAP" --out "$CAP/account" --bsp "$TF/$BSP_REL" "--eye=$EYESET" --json "$CAP/account/account.json" > "$DIR/dark-account-retry.txt.new" 2>&1
    mv -f "$DIR/dark-account-retry.txt.new" "$DIR/dark-account-retry.txt"
    dark_rc=$?
    if [[ $dark_rc -ne 0 ]]; then
      sed -n '1,6p' "$DIR/dark-account-retry.txt" | sed 's/^/    /'
      failure "$MAP: the dark account exited $dark_rc on both attempts"
    else
      echo "  RETRY-OK $MAP: the second attempt wrote the ledger"
    fi
  fi
  if [[ ! -s $CAP/account/account.json ]]; then
    failure "$MAP: the dark account produced no ledger"
  else
    "$PYTHON" map-axis-stats.py "$CAP/account/account.json" > "$DIR/dark-stats.txt.new" 2>&1
    stats_rc=$?
    mv -f "$DIR/dark-stats.txt.new" "$DIR/dark-stats.txt"
    if [[ $stats_rc -ne 0 ]]; then failure "$MAP: map-axis-stats.py exited $stats_rc"; fi
    framepix=$(field "$(grep -a "DARK framePixels=" "$DIR/dark-stats.txt" | head -1)" framePixels)
    dark=$(field "$(grep -a "DARK dark=" "$DIR/dark-stats.txt" | head -1)" dark)
    back=$(field "$(grep -a "DARK back=" "$DIR/dark-stats.txt" | head -1)" back)
    front=$(field "$(grep -a "DARK front=" "$DIR/dark-stats.txt" | head -1)" front)
    countstotal=$(field "$(grep -a "DARK countsTotal=" "$DIR/dark-stats.txt" | head -1)" countsTotal)
    ambiguous=$(field "$(grep -a "DARK ambiguous=" "$DIR/dark-stats.txt" | head -1)" ambiguous)
    attributable=$(field "$(grep -a "DARK productAttributable=" "$DIR/dark-stats.txt" | head -1)" productAttributable)
    framepct=$(field "$(grep -a "DARK attributableFramePct=" "$DIR/dark-stats.txt" | head -1)" attributableFramePct)
    assert_positive "dark pixels the account started from" "${dark:-}" || true
    assert_eq "dark bucket partition (sum of every bucket)" "${countstotal:-NA}" "${dark:-none}"
    assert_eq "dark pixels the account could not place in a bucket" "${ambiguous:-NA}" 0
    if awk -v a="${attributable:-x}" -v f="${framepix:-0}" -v lim="$MAX_ATTRIBUTABLE_FRAME_PCT" "BEGIN{exit !(f > 0 && a * 100 <= lim * f)}"; then
      echo "  OK product-attributable = ${framepct:-NA}% of the frame (<= $MAX_ATTRIBUTABLE_FRAME_PCT%)"
    else
      failure "$MAP: product-attributable = ${framepct:-NA}% of the frame, over $MAX_ATTRIBUTABLE_FRAME_PCT%"
    fi
    echo "  reading: dark=$dark (back=$back front=$front) attributable=$attributable countsTotal=$countstotal frame=$framepix"
  fi
  if [[ $failures -eq $light_before && $POSE == p1 ]]; then lighting_ok=$((lighting_ok + 1)); fi

  done  # POSES

  # The three vacuity axes, judged once per map: at least one pose must leave an
  # empty pixel (G4), show a dark albedo pixel (G7), and have the back-face cull
  # take a face-id pixel away (P1-2a). A pose that has none of them is a
  # viewpoint without coverage, not a pass; the degenerate-pose check above keeps
  # such a pose from being a camera inside the world.
  echo "  reading: coverage across the poses of $MAP: empty=$cov_empty dark=$cov_dark cull-removed=$cov_cullrem"
  if [[ ${#POSES[@]} -ge 2 ]]; then
    assert_positive "$MAP: a pose that leaves an empty pixel (G4 coverage)" "$cov_empty" || true
    assert_positive "$MAP: a pose with a dark albedo pixel (G7 coverage)" "$cov_dark" || true
    assert_positive "$MAP: a pose whose cull removes a face-id pixel (P1-2a coverage)" "$cov_cullrem" || true
  else
    echo "  NOTE this run judged ${POSES[*]} only, so those three coverage numbers are readings; a sampling run does not judge the map"
  fi

  if [[ $failures -eq $map_before ]]; then maps_judged=$((maps_judged + 1)); fi
done

echo ""
if [[ $failures -ne 0 ]]; then
  echo "MAP-GEOMETRY-LIGHTING=FAIL maps=$maps_judged geometry=$geometry_ok lighting=$lighting_ok failed=$failures"
  exit 1
fi
echo "MAP-GEOMETRY-LIGHTING=PASS maps=$maps_judged geometry=$geometry_ok lighting=$lighting_ok failed=0"
exit 0
