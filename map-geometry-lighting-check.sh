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
#
# WHAT IT DOES NOT CLAIM
#   The algebra holds on one pose per map: it is not a claim about every pose.
#   It also does not claim a pixel is meant to be sky -- only that the frame that
#   draws everything it can leaves it empty, and that the product own oracle
#   agrees there is nothing along that ray. A low product-attributable population
#   says the dark pixels this frame has are the map own lightmap data, not that
#   every lightmap decode on every map is correct. Neither axis is a pixel pin:
#   the frame hashes are recorded in captures.txt so a later run can see the
#   picture moved, but nothing here asserts a frame hash against the repository.
#   The one byte-level assertion is internal to a run: the oracle capture, which
#   repeats flat-base with --world-hole-trace added, must reproduce it exactly.
#
# Usage: bash map-geometry-lighting-check.sh [--only=<map[,map]>] [--reuse]
#   --only    judge the named maps only (others are left alone).
#   --reuse   keep the frames already captured under .scratch/t62-map-axes.
# Exit: 0 = every assertion held; 1 = an assertion failed; 2 = an input is
#       missing (a camera.txt, a demo, a BSP, a tool).
set -uo pipefail
cd "$(dirname "$0")"

PYTHON=${PYTHON:-python}
TICK=2000
SPEED=8
LATTICE_SAMPLES=${LATTICE_SAMPLES:-6}
MAX_ATTRIBUTABLE_FRAME_PCT=2
PRODUCT=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/world_geometry_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
LOCK=${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}
MATRIX=evidence/t62-map-matrix
OUT=${MAP_AXIS_OUT:-evidence/t62-map-axes}
SCRATCH=.scratch/t62-map-axes
MAPS=(cp_snakewater_final1 pl_odyssey cp_sunshine koth_bagel_rc12 koth_bagel_rc11)
FRAMES=(textured none sky-faces flat-base flat-novis flat-nocull flat-novis-nocull ids ids-nocull lmoff)

ONLY=""
REUSE=0
for arg in "$@"; do
  case $arg in
    --only=*) ONLY=${arg#*=} ;;
    --reuse) REUSE=1 ;;
    -h|--help) echo "usage: $0 [--only=<map[,map]>] [--reuse]"; exit 0 ;;
    *) echo "usage: $0 [--only=<map[,map]>] [--reuse]" >&2; exit 2 ;;
  esac
done

for tool in "$PRODUCT" "$PROBE"; do
  if [[ ! -x $tool ]]; then echo "missing tool: $tool (run bash build-target.sh tf2_demo_native world_geometry_probe)" >&2; exit 2; fi
done

locked() { bash "$LOCK" run t62-mapaxes bash -c "$1"; }

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

capture() { # capture <name> [extra world flags...]
  local name=$1; shift
  local trace=$CAP/$name.trace bmp=$CAP/$name.bmp csv=$CAP/$name.csv log=$CAP/$name.log
  if [[ $REUSE -eq 1 && -s $bmp ]]; then echo "  reused $name"; return 0; fi
  local -a args=( "$PRODUCT" --tf-root "$TF" --audio-device 6 --audio-disabled
    --hidden-window --demo "$DEMO" --playback-speed "$SPEED" --capture-tick "$TICK"
    --capture-frame "$(cygpath -m "$bmp")" --metrics-file "$(cygpath -m "$csv")"
    --debug-camera "$CX" "$CY" "$CZ" "$CP" "$CYAW" "$@" )
  local cmd
  cmd=$(printf "%q " TF2_NATIVE_TRACE="$(cygpath -m "$trace")" timeout 400 "${args[@]}")
  locked "$cmd" > "$log" 2>&1
  local rc=$?
  if [[ $rc -ne 0 || ! -s $bmp ]]; then
    echo "  FAIL capture $name rc=$rc"
    tail -3 "$log" | grep -a . | tail -3 | while read -r line; do echo "    $line"; done
    return 1
  fi
  if ! grep -aq "capture: armed" "$trace"; then echo "  FAIL capture $name wrote no armed line"; return 1; fi
  echo "  captured $name"
}

mkdir -p "$OUT" "$SCRATCH" || exit 2
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
  read -r CX CY CZ CP CYAW <<<"$CAM"
  if [[ -z ${CYAW:-} ]]; then failure "$MAP: the camera line is not five numbers: $CAM"; continue; fi
  echo "  camera: eye=($CX, $CY, $CZ) pitch=$CP yaw=$CYAW, tick $TICK, speed $SPEED"

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

  # G1/G2/G3: the BSP own account.
  geo_before=$failures
  "$PROBE" --tf-root "$TF" --map "$BSP_REL" > "$DIR/geometry.txt" 2>&1
  probe_rc=$?
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

  $PYTHON world-empty-lattice.py --dir "$CAP" --samples "$LATTICE_SAMPLES" > "$DIR/lattice.txt" 2>&1
  lattice_rc=$?
  lframe=$(grep -a "LATTICE frame=" "$DIR/lattice.txt" | head -1)
  lmono=$(grep -a "LATTICE monotone " "$DIR/lattice.txt" | head -1)
  lrestrict=$(grep -a "LATTICE restrict " "$DIR/lattice.txt" | head -1)
  llight=$(grep -a "LATTICE light " "$DIR/lattice.txt" | head -1)
  if [[ $lattice_rc -ne 0 ]]; then
    failure "$MAP: the empty-population algebra failed: $(grep -a "LATTICE violated" "$DIR/lattice.txt" | head -1 | cut -c1-140)"
  fi
  empty_alloff=$(field "$lframe" empty_alloff)
  drawn_base=$(field "$lframe" drawn_base)
  assert_positive "empty pixels left with both restrictors off (G4)" "${empty_alloff:-}" || true
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
  assert_positive "pixels whose albedo is dark (G7 is not vacuous)" "$(field "$llight" albedo_dark)" || true

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
  if [[ $failures -eq $geo_before ]]; then geometry_ok=$((geometry_ok + 1)); fi

  # L1/L2/L3: the dark population attribution.
  light_before=$failures
  # A ledger left by an earlier run must never stand in for this one: when the
  # analysis crashed, the stale account.json made the checks below read the old
  # numbers and the failure showed up only as a bare rc. Wipe the account first,
  # and print the head of the crash log whenever the analysis fails.
  rm -rf "$CAP/account"
  "$PYTHON" dark-account.py --frames "$CAP" --out "$CAP/account" --bsp "$TF/$BSP_REL" "--eye=$EYESET" --json "$CAP/account/account.json" > "$DIR/dark-account.txt" 2>&1
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
    "$PYTHON" dark-account.py --frames "$CAP" --out "$CAP/account" --bsp "$TF/$BSP_REL" "--eye=$EYESET" --json "$CAP/account/account.json" > "$DIR/dark-account-retry.txt" 2>&1
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
    "$PYTHON" map-axis-stats.py "$CAP/account/account.json" > "$DIR/dark-stats.txt" 2>&1
    stats_rc=$?
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
  if [[ $failures -eq $light_before ]]; then lighting_ok=$((lighting_ok + 1)); fi

  if [[ $failures -eq $f_before ]]; then maps_judged=$((maps_judged + 1)); fi
done

echo ""
if [[ $failures -ne 0 ]]; then
  echo "MAP-GEOMETRY-LIGHTING=FAIL maps=$maps_judged geometry=$geometry_ok lighting=$lighting_ok failed=$failures"
  exit 1
fi
echo "MAP-GEOMETRY-LIGHTING=PASS maps=$maps_judged geometry=$geometry_ok lighting=$lighting_ok failed=0"
exit 0
