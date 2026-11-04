#!/usr/bin/env bash
# map-material-matrix.sh -- T62 map material matrix spot-check (2026-11-03)
#
# What this is: material-coverage-check.sh reads cp_snakewater_final1 at tick 0.
# This script points the same instruments at the five maps the T62 matrix names
# and records, per map:
#   * the coverage probe's plan tally (material_coverage_probe --map <bsp>),
#   * the product's material-fetch, atlas, atlas-plan and per-material tile
#     table (gpu: world tile), at tick 0 and at tick 2000,
#   * the tick each capture actually landed on (capture: armed frame_tick),
#   * a pixel diff between the two captures, both taken from a FIXED camera so
#     the two frames differ by the tick they were drawn on, not by a moved
#     observer.
#
# What this asserts (2026-11-04: this is now a gate, not just a recorder). Every
# row must satisfy the structural invariants the world-material pipeline promises,
# and one of them is a pin nothing was tuned to satisfy:
#   * the demo's own header names the map the row claims (map= and server_map=),
#   * the fetch was not capped and the atlas bound with bytes > 0,
#   * the atlas plan refused nothing malformed or duplicated,
#   * the tile table has one row per planned material and every row's batch is in
#     [0, plan.batches),
#   * the capture landed on the requested tick and drew at least one frame,
#   * the two captures are not byte-identical, so the pair is a real observation.
# What is still NOT asserted: how MANY pixels the two ticks differ by. That is a
# rendering reading, not an invariant, and the matrix records it rather than
# bounding it. The probe prints aggregates only -- it has no per-material tile
# table -- so the tile tables here come from the product's trace and README.md
# says so instead of inventing one.
#
# Every probe/product launch goes through the shared host lock
# (hostlock.sh run t62-map bash -c '...'), one process at a time. Traces (~10-50 MB)
# and captures stay in .scratch/t62-map-matrix; only grep-extracted readings,
# tile tables and the pixel diff are written under evidence/.
#
# Outputs (all under evidence/t62-map-matrix, never evidence/material-coverage):
#   readings.tsv              one row per map x tick (long format)
#   <map>/probe-plan.txt      probe stdout, verbatim
#   <map>/camera-probe.txt    demo_open_probe output, verbatim
#   <map>/camera.txt          the fixed camera and where its pose came from
#   <map>/trace-tick<N>.txt   grep-extracted trace lines (traces stay in scratch)
#   <map>/tile-tick<N>.raw    gpu: world tile rows, verbatim
#   <map>/tile-tick<N>.tsv    the same rows, tab-separated
#   <map>/diff.txt            pixel diff, tick 0 vs tick 2000
#   <map>/run-tick<N>.log     product stdout/stderr
#
# Exit: 0 every reading present AND every invariant asserted (prints
#       MAP-MATERIAL-MATRIX=PASS); 1 a reading is missing, a run failed, or an
#       invariant does not hold; 2 usage or environment error. A map whose BSP is
#       absent is recorded as UNSUPPORTED and is counted, not failed.
set -uo pipefail
cd "$(dirname "$0")" || exit 2
ROOT=$PWD

PROBE=$ROOT/native/build-nmake/material_coverage_probe.exe
PRODUCT=$ROOT/native/build-nmake/tf2_demo_native.exe
DEMO_PROBE=$ROOT/native/build-nmake/demo_open_probe.exe
TF=${TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
AUDIO_DEVICE=${AUDIO_DEVICE:-6}
LOCK=${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}
OUT=$ROOT/evidence/t62-map-matrix
SCRATCH=$ROOT/.scratch/t62-map-matrix
CAPTURE_TICKS=(0 2000)
CAPTURE_TIMEOUT=${CAPTURE_TIMEOUT:-400}
# A pixel counts as changed when any RGB channel moves by MORE than this many
# levels. It defines the reported metric; it is not a pass threshold and
# nothing below compares the result to a bound.
CHANNEL_DELTA=8

ONLY=""
for arg in "$@"; do
  case "$arg" in
    --only=*) ONLY=${arg#*=} ;;
    -h|--help) sed -n '2,46p' "$0"; exit 0 ;;
    *) echo "usage: $0 [--only=<map[,map]>]" >&2; exit 2 ;;
  esac
done

MAPS=(cp_snakewater_final1 pl_odyssey cp_sunshine koth_bagel_rc12 koth_bagel_rc11)

# --only must name maps of the matrix: an unknown name would otherwise select
# nothing, leave a stale readings.tsv in place and still exit 0.
if [ -n "$ONLY" ]; then
  for want in ${ONLY//,/ }; do
    known=0
    for have in "${MAPS[@]}"; do [ "$want" = "$have" ] && known=1; done
    if [ "$known" -eq 0 ]; then
      echo "unknown map in --only: $want (known: ${MAPS[*]})" >&2
      exit 2
    fi
  done
fi

map_bsp() {
  case "$1" in
    cp_snakewater_final1|pl_odyssey|cp_sunshine) echo "$TF/maps/$1.bsp" ;;
    koth_bagel_rc12|koth_bagel_rc11) echo "$TF/download/maps/$1.bsp" ;;
    *) echo "" ;;
  esac
}
# Demo per map: its header names the same map and it has more than 2000 ticks
# (each was checked with demo_open_probe --scan). The two SourceTV recordings
# live in D:/TF2_Demo_Player/testdata/demos and carry no POV camera samples,
# which is why those two maps resolve their camera with a throwaway orient run
# instead of from the demo's own camera track.
map_demo() {
  case "$1" in
    cp_snakewater_final1) echo "D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem" ;;
    koth_bagel_rc11) echo "D:/TF2_Demo_Player/testdata/demos/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem" ;;
    pl_odyssey) echo "$TF/demos/autorecord_2026-05-26_23-14-06.dem" ;;
    cp_sunshine) echo "$TF/demos/autorecord_2026-05-14_23-09-16.dem" ;;
    koth_bagel_rc12) echo "$TF/demos/autorecord_2026-05-14_21-28-01.dem" ;;
    *) echo "" ;;
  esac
}
map_demo_source() {
  case "$1" in
    koth_bagel_rc11|cp_snakewater_final1) echo "SourceTV recording, D:/TF2_Demo_Player/testdata/demos, no POV camera samples" ;;
    *) echo "POV autorecord, $TF/demos" ;;
  esac
}

pick_python() {
  local c
  for c in python python3 py; do
    if command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys' >/dev/null 2>&1; then
      echo "$c"; return 0
    fi
  done
  return 1
}

mkdir -p "$OUT" "$SCRATCH" || exit 2
# --only re-runs part of the matrix (used to redo one map after a fix). The rows
# already on disk for the maps this invocation does not touch are merged back,
# so readings.tsv always describes the whole matrix, not just the last run.
PREV=$SCRATCH/readings.prev.tsv
rm -f "$PREV"
if [ -n "$ONLY" ] && [ -s "$OUT/readings.tsv" ]; then cp "$OUT/readings.tsv" "$PREV"; fi
: > "$OUT/readings.tsv"
: > "$OUT/failures.txt"
fail=0
failure() { printf '%s\n' "$*" >> "$OUT/failures.txt"; fail=1; }
na() { if [ -n "${1:-}" ]; then printf '%s' "$1"; else printf 'NA'; fi; }

COLS=(map tick status bsp bsp_bytes demo camera_source camera_eye
  probe_rc probe_distinct probe_tiled probe_vmtResolved probe_vmtMissing
  probe_vmtNoBase probe_vtfResolved probe_vtfMissing probe_batches probe_refused
  probe_beyondOldCap probe_tile_table
  product_rc fetch_distinct fetch_tiled fetch_capped fetch_vmtMissing
  fetch_vmtNoBase fetch_vtfMissing fetch_patchDeclared fetch_patchResolved
  fetch_transformRefused plan_batches plan_tiled plan_refused plan_malformed
  plan_duplicate plan_budget atlas_state atlas_bytes tile_rows
  capture_requested capture_frame_tick frames_rendered trace_bytes frame_sha256
  diff_pixels_ch8 diff_percent_ch8 diff_max_delta note)
row() { local IFS=$'\t'; printf '%s\n' "$*" >> "$OUT/readings.tsv"; }
row "${COLS[@]}"

PYTHON=$(pick_python) || true
if [ -z "${PYTHON:-}" ]; then
  echo "no usable python on PATH (needed for the BMP diff)" >&2
  exit 2
fi

# probe reading value: the probe writes one JSON object per line
kv() { # kv <file> <reading>
  grep -ao "\"reading\":\"$2\",\"value\":\"[^\"]*\"" "$1" 2>/dev/null | head -1 |
    sed 's/.*"value":"\(.*\)"/\1/'
}
# trace field value: gpu: world material-fetch distinct=148 ... -> rval line key
rval() { printf '%s\n' "$1" | tr ' ' '\n' | awk -F= -v k="$2" '$1==k{print $2; exit}'; }
# Reading lines from a trace summary: the summary's own headers start with '#',
# and the real trace lines keep their elapsed-seconds prefix, so neither an
# anchored '^gpu:' match nor a bare grep count can be used directly.
readings_only() { grep -a -- "$2" "$1" 2>/dev/null | grep -av '^#' | tr -d '\r'; }

locked() { bash "$LOCK" run t62-map bash -c "$1"; }
# ---- the invariants this gate asserts ----------------------------------------
# assert_tick <map> <tick> <fetch line> <plan line> <atlas line> <armed line> <tile_rows>
# Every bound here is a promise the pipeline makes, not a value tuned to pass.
assert_tick() {
  local map=$1 tick=$2 fetch=$3 plan=$4 atlas=$5 armed=$6 rows=$7 v tiled refused
  v=$(rval "$fetch" capped)
  [ "${v:-NA}" = "0" ] || failure "$map tick $tick: fetch capped=${v:-NA}, must be 0 (a capped fetch means material fell past the budget silently)"
  v=$(rval "$fetch" distinct); tiled=$(rval "$fetch" tiled)
  if [ -n "$v" ] && [ -n "$tiled" ]; then
    [ "$v" -ge "$tiled" ] || failure "$map tick $tick: fetch distinct=$v < tiled=$tiled"
  else
    failure "$map tick $tick: fetch line has no distinct/tiled reading"
  fi
  v=$(rval "$plan" malformed)
  [ "${v:-NA}" = "0" ] || failure "$map tick $tick: atlas plan malformed=${v:-NA}, must be 0"
  v=$(rval "$plan" duplicate)
  [ "${v:-NA}" = "0" ] || failure "$map tick $tick: atlas plan duplicate=${v:-NA}, must be 0"
  tiled=$(rval "$plan" tiled); refused=$(rval "$plan" refused)
  if [ -n "$tiled" ]; then
    [ "$rows" -eq "$((tiled + ${refused:-0}))" ] ||
      failure "$map tick $tick: tile table has $rows rows, plan tiled=$tiled + refused=${refused:-0}"
  else
    failure "$map tick $tick: atlas plan line has no tiled reading"
  fi
  v=$(rval "$plan" batches)
  if [ -n "$v" ]; then
    [ "$v" -gt 0 ] || failure "$map tick $tick: atlas plan batches=$v, must be >= 1"
  else
    failure "$map tick $tick: atlas plan line has no batches reading"
  fi
  v=$(rval "$atlas" state)
  [ "$v" = "bound" ] || failure "$map tick $tick: atlas state=${v:-NA}, must be bound"
  v=$(rval "$atlas" bytes)
  if [ -n "$v" ]; then
    [ "$v" -gt 0 ] || failure "$map tick $tick: atlas bytes=$v, must be > 0"
  else
    failure "$map tick $tick: atlas line has no bytes reading"
  fi
  v=$(rval "$armed" requested); tiled=$(rval "$armed" frame_tick)
  [ -n "$v" ] && [ "$v" = "$tiled" ] ||
    failure "$map tick $tick: capture landed on frame_tick=${tiled:-NA}, not the requested ${v:-NA}"
  v=$(rval "$armed" frames)
  if [ -n "$v" ]; then
    # Tick 0 is captured from --start-paused: no frame has been drawn when the
    # capture arms, so frames=0 is the expected reading there.
    if [ "$v" -eq 0 ] && [ "$tick" -ne 0 ]; then
      failure "$map tick $tick: frames=$v, must be >= 1 for a tick > 0"
    fi
  else
    failure "$map tick $tick: capture: armed line has no frames reading"
  fi
}

# assert_tile_table <map> <tick> <raw tile file> <plan line>
# One row per material, and every row belongs to a batch the plan declared.
assert_tile_table() {
  local map=$1 tick=$2 raw=$3 plan=$4 rows uniq batches bad
  rows=$(grep -ac 'material=' "$raw" 2>/dev/null || true); rows=${rows:-0}
  uniq=$(sed -n 's/.*material=\([^ ]*\) .*/\1/p' "$raw" 2>/dev/null | sort -u | wc -l)
  [ "$rows" -eq "$uniq" ] || failure "$map tick $tick: tile table repeats a material ($rows rows, $uniq unique)"
  batches=$(rval "$plan" batches)
  if [ -n "$batches" ] && [ "$rows" -gt 0 ]; then
    bad=$(sed -n 's/.*batch=\(-\{0,1\}[0-9]*\) .*/\1/p' "$raw" 2>/dev/null | awk -v b="$batches" '$1<0 || $1>=b' | wc -l)
    bad=${bad:-0}
    [ "$bad" -eq 0 ] || failure "$map tick $tick: $bad tile rows have a batch outside [0,$batches)"
  fi
}


# product_cmd <trace_win> <bmp_win> <metrics_win> <demo> <tick> [extra args...]
# Returns one %q-quoted string so hostlock runs it through bash -c exactly like
# the required lock invocation. Tick 0 is forced paused: a paused demo holds at
# tick 0, and --capture-tick 2000 from a paused start never arrives
# (material-coverage-check.sh measured rc=124 after 400 s).
product_cmd() {
  local trace=$1 bmp=$2 metrics=$3 demo=$4 tick=$5; shift 5
  local -a args=( "TF2_NATIVE_TRACE=$trace" timeout "$CAPTURE_TIMEOUT" "$PRODUCT"
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window
    --demo "$demo" )
  [ "$tick" = "0" ] && args+=( --start-paused )
  args+=( --capture-tick "$tick" --capture-frame "$bmp" --metrics-file "$metrics" )
  args+=( "$@" )
  printf '%q ' "${args[@]}"
}

extract_trace() { # extract_trace <map> <tick> <trace> <outfile>
  local map=$1 tick=$2 trace=$3 out=$4
  local bytes=0 lines=0 key line count
  if [ -f "$trace" ]; then bytes=$(wc -c < "$trace"); lines=$(wc -l < "$trace"); fi
  {
    printf '# map=%s tick=%s trace=%s bytes=%s lines=%s\n' "$map" "$tick" "$trace" "$bytes" "$lines"
    printf '# not the trace itself: the gpu: world lines are emitted once per run\n'
    printf '# (main.cpp traceWorldDraw emits on the first world draw). The rows below\n'
    printf '# are every line that run printed, not a sample.\n'
    for key in 'gpu: world material-fetch ' 'gpu: world atlas ' 'gpu: world atlas-plan ' \
               'gpu: world tile ' 'gpu: world isolation ' 'gpu: world vis '; do
      count=$(grep -ac "$key" "$trace" 2>/dev/null || true)
      printf '# %s count=%s\n' "$key" "${count:-0}"
      line=$(grep -a "$key" "$trace" 2>/dev/null | head -1 | tr -d '\r')
      [ -n "$line" ] && printf '%s\n' "$line"
    done
    printf '# capture: armed (last; frame_tick is the tick the frame was drawn on)\n'
    line=$(grep -a 'capture: armed ' "$trace" 2>/dev/null | tail -1 | tr -d '\r')
    [ -n "$line" ] && printf '%s\n' "$line"
    printf '# camera: capture (last)\n'
    line=$(grep -a 'camera: capture ' "$trace" 2>/dev/null | tail -1 | tr -d '\r')
    [ -n "$line" ] && printf '%s\n' "$line"
    printf '# scene: camera (last)\n'
    line=$(grep -a 'scene: camera ' "$trace" 2>/dev/null | tail -1 | tr -d '\r')
    [ -n "$line" ] && printf '%s\n' "$line"
  } > "$out"
}

tile_table() { # tile_table <trace> <raw> <tsv>
  grep -ao 'gpu: world tile material=.*' "$1" 2>/dev/null | tr -d '\r' > "$2"
  printf 'material\ttris\tvisible\tbatch\ttile\tsize\tblend\tmodulate\treason\n' > "$3"
  sed -n 's/.*material=\([^ ]*\) tris=\([0-9]*\) visible=\([0-9]*\) batch=\(-\{0,1\}[0-9]*\) tile=\(-\{0,1\}[0-9]*\) size=\([0-9]*x[0-9]*\) blend=\([0-9]*\) modulate=\([0-9]*\) reason=\(.*\)$/\1\t\2\t\3\t\4\t\5\t\6\t\7\t\8\t\9/p' "$2" >> "$3"
}

# BMP diff: compares RGB only (alpha ignored), reports how many pixels have any
# channel delta above CHANNEL_DELTA, the percentage, and the maximum delta.
diff_frames() { # diff_frames <bmp_a> <bmp_b> <tolerance> <out>
  "$PYTHON" - "$1" "$2" "$3" <<'PYEOF' > "$4"
import sys, hashlib
a, b, tol = sys.argv[1], sys.argv[2], int(sys.argv[3])

def load(path):
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise SystemExit("not-a-bmp " + path)
    off = int.from_bytes(data[10:14], "little")
    w = int.from_bytes(data[18:22], "little", signed=True)
    h = int.from_bytes(data[22:26], "little", signed=True)
    bpp = int.from_bytes(data[28:30], "little")
    comp = int.from_bytes(data[30:34], "little")
    if bpp not in (24, 32) or comp != 0:
        raise SystemExit("unsupported-bmp bpp=%d comp=%d" % (bpp, comp))
    return data, off, abs(w), abs(h), bpp, h > 0

def rgb(data, off, w, h, bpp, bottom_up, x, y):
    row = h - 1 - y if bottom_up else y
    stride = ((w * bpp + 31) // 32) * 4
    i = off + row * stride + x * (bpp // 8)
    return data[i], data[i + 1], data[i + 2]

da, oa, wa, ha, bpa, bua = load(a)
db, ob, wb, hb, bpb, bub = load(b)
if (wa, ha) != (wb, hb):
    raise SystemExit("size-mismatch %dx%d vs %dx%d" % (wa, ha, wb, hb))
total = wa * ha
any_delta = gt8 = maxd = 0
for y in range(ha):
    for x in range(wa):
        ca = rgb(da, oa, wa, ha, bpa, bua, x, y)
        cb = rgb(db, ob, wb, hb, bpb, bub, x, y)
        m = max(abs(ca[0] - cb[0]), abs(ca[1] - cb[1]), abs(ca[2] - cb[2]))
        if m:
            any_delta += 1
        if m > tol:
            gt8 += 1
        if m > maxd:
            maxd = m
print("bmp_a=" + a)
print("bmp_b=" + b)
print("width=%d" % wa)
print("height=%d" % ha)
print("bpp_a=%d" % bpa)
print("bpp_b=%d" % bpb)
print("channel_delta_tolerance=%d" % tol)
print("total_pixels=%d" % total)
print("pixels_any_delta=%d" % any_delta)
print("pixels_channel_gt8=%d" % gt8)
print("percent_channel_gt8=%.4f" % (100.0 * gt8 / total if total else 0.0))
print("percent_channel_any=%.4f" % (100.0 * any_delta / total if total else 0.0))
print("max_channel_delta=%d" % maxd)
print("sha256_a=" + hashlib.sha256(da).hexdigest())
print("sha256_b=" + hashlib.sha256(db).hexdigest())
print("identical_files=%d" % (1 if da == db else 0))
PYEOF
}

ran=0
for MAP in "${MAPS[@]}"; do
  if [ -n "$ONLY" ]; then
    case ",$ONLY," in *",$MAP,"*) ;; *) continue ;; esac
  fi
  ran=$((ran + 1))
  DIR=$OUT/$MAP
  mkdir -p "$DIR"
  BSP=$(map_bsp "$MAP")
  DEMO=$(map_demo "$MAP")
  DEMO_SRC=$(map_demo_source "$MAP")
  BSP_BYTES=NA
  [ -f "$BSP" ] && BSP_BYTES=$(wc -c < "$BSP")

  # Unsupported is a recorded outcome, not a hidden skip: the row says so and
  # every reading column stays NA (never 0, which would read as "measured").
  if [ ! -f "$BSP" ]; then
    printf 'map=%s status=UNSUPPORTED reason=bsp-not-found bsp=%s\n' "$MAP" "$BSP" > "$DIR/status.txt"
    U=("$MAP" - UNSUPPORTED "$BSP" NA "$DEMO" NA NA)
    for _ in $(seq 1 39); do U+=(NA); done
    U+=(bsp-not-found)
    row "${U[@]}"
    echo "SKIP $MAP: BSP not found at $BSP"
    continue
  fi

  echo "== $MAP =="
  # ---- 1. probe: plan tally (aggregate only, no per-material table) --------
  P_OUT=$DIR/probe-plan.txt
  P_CMD=$(printf '%q ' "$PROBE" --map "$(cygpath -m "$BSP")" --tf-root "$TF")
  locked "$P_CMD" > "$P_OUT" 2>&1
  P_RC=$?
  [ "$P_RC" -ne 0 ] && failure "$MAP probe rc=$P_RC (see probe-plan.txt)"
  P_DISTINCT=$(kv "$P_OUT" map.distinctMaterials)
  P_TILED=$(kv "$P_OUT" plan.tiled)
  P_VMT_RES=$(kv "$P_OUT" map.vmtResolved)
  P_VMT_MISS=$(kv "$P_OUT" map.vmtMissing)
  P_VMT_NOBASE=$(kv "$P_OUT" map.vmtNoBase)
  P_VTF_RES=$(kv "$P_OUT" map.vtfResolved)
  P_VTF_MISS=$(kv "$P_OUT" map.vtfMissing)
  P_BATCHES=$(kv "$P_OUT" plan.batches)
  P_REFUSED=$(kv "$P_OUT" plan.refused)
  P_BEYOND=$(kv "$P_OUT" plan.beyondOldCap)
  # The probe has no tile detail. Say that with a reading rather than a guess.
  P_TILE_ROWS=$(grep -ac 'gpu: world tile ' "$P_OUT" 2>/dev/null || true)
  P_TILE=NOT-PROVIDED
  [ "${P_TILE_ROWS:-0}" -gt 0 ] && P_TILE=present
  [ -z "$P_DISTINCT" ] && failure "$MAP probe emitted no map.distinctMaterials"

  # ---- 2. fixed camera -----------------------------------------------------
  # POV demos: demo_open_probe reads the camera sample at tick 2000. SourceTV
  # demos have no such sample (origin 0,0,0), so one orient run at tick 2000
  # reads the pose the product itself drew with (scene: camera).
  C_PROBE=$DIR/camera-probe.txt
  CP_CMD=$(printf '%q ' "$DEMO_PROBE" --scan --camera-tick 2000 "$DEMO")
  locked "$CP_CMD" > "$C_PROBE" 2>&1
  # The demo must be a recording of the map this row claims: a shared or renamed
  # recording would otherwise let a row say "cp_sunshine" while the frames came
  # from somewhere else. demo_open_probe reads both the header map and the server
  # map, so this needs no new instrument.
  DEMO_MAP_NAMED=$(grep -ao ' map=[^ ]*' "$C_PROBE" 2>/dev/null | head -1 | sed 's/^ map=//')
  SERVER_MAP_NAMED=$(grep -ao 'server_map=[^ ]*' "$C_PROBE" 2>/dev/null | head -1 | sed 's/^server_map=//')
  [ "$DEMO_MAP_NAMED" = "$MAP" ] ||
    failure "$MAP: the demo header names map=${DEMO_MAP_NAMED:-NA}, not $MAP ($DEMO)"
  [ "$SERVER_MAP_NAMED" = "$MAP" ] ||
    failure "$MAP: the demo's server_map=${SERVER_MAP_NAMED:-NA}, not $MAP ($DEMO)"
  printf 'demo_map=%s\nserver_map=%s\n' "${DEMO_MAP_NAMED:-NA}" "${SERVER_MAP_NAMED:-NA}" >> "$DIR/camera.txt"

  C_SRC=NA; C_EYE=NA; C_PITCH=NA; C_YAW=NA
  ORIGIN=$(grep -ao 'camera_origin=[^ ]*' "$C_PROBE" 2>/dev/null | head -1 | sed 's/^camera_origin=//')
  C_PITCH=$(grep -ao 'camera_pitch=[^ ]*' "$C_PROBE" 2>/dev/null | head -1 | sed 's/^camera_pitch=//')
  C_YAW=$(grep -ao 'camera_yaw=[^ ]*' "$C_PROBE" 2>/dev/null | head -1 | sed 's/^camera_yaw=//')
  if [ -n "$ORIGIN" ] && [ "$ORIGIN" != "0,0,0" ]; then
    C_SRC=demo-camera-tick2000
    C_EYE=$(printf '%s' "$ORIGIN" | tr ',' ' ')
  else
    O_TRACE=$SCRATCH/$MAP.orient.trace
    O_BMP=$SCRATCH/$MAP.orient.bmp
    O_METRICS=$SCRATCH/$MAP.orient.csv
    O_CMD=$(product_cmd "$(cygpath -m "$O_TRACE")" "$(cygpath -m "$O_BMP")" "$(cygpath -m "$O_METRICS")" "$DEMO" 2000)
    locked "$O_CMD" > "$SCRATCH/$MAP.orient.log" 2>&1
    O_RC=$?
    O_LINE=$(grep -a 'scene: camera ' "$O_TRACE" 2>/dev/null | tail -1 | tr -d '\r')
    O_EYE=$(printf '%s\n' "$O_LINE" | sed -n 's/.* eye=\([^ ]*\) pitch=\([^ ]*\) yaw=\([^ ]*\) src=.*/\1/p')
    O_PITCH=$(printf '%s\n' "$O_LINE" | sed -n 's/.* eye=\([^ ]*\) pitch=\([^ ]*\) yaw=\([^ ]*\) src=.*/\2/p')
    O_YAW=$(printf '%s\n' "$O_LINE" | sed -n 's/.* eye=\([^ ]*\) pitch=\([^ ]*\) yaw=\([^ ]*\) src=.*/\3/p')
    if [ -n "$O_EYE" ] && [ "$O_EYE" != "0.0,0.0,0.0" ]; then
      C_SRC=product-orient-run-tick2000
      C_EYE=$(printf '%s' "$O_EYE" | tr ',' ' ')
      C_PITCH=$O_PITCH
      C_YAW=$O_YAW
    else
      failure "$MAP camera unresolved (orient rc=${O_RC:-?}, no scene: camera pose)"
    fi
  fi
  {
    printf 'map=%s\n' "$MAP"
    printf 'source=%s\n' "$C_SRC"
    printf 'demo=%s\n' "$DEMO"
    printf 'demo_source=%s\n' "$DEMO_SRC"
    printf 'traces=%s\n' "$SCRATCH/$MAP.*.trace"
  } > "$DIR/camera.txt"
  [ -n "$C_EYE" ] && [ "$C_EYE" != "NA" ] && printf 'debug_camera_args=--debug-camera %s %s %s\n' "$C_EYE" "$C_PITCH" "$C_YAW" >> "$DIR/camera.txt"
  [ -n "${O_LINE:-}" ] && printf 'orient_scene_camera_line=%s\n' "$O_LINE" >> "$DIR/camera.txt"
  POSE_LINE=$(grep -a 'camera_origin=' "$C_PROBE" 2>/dev/null | tail -1)
  [ -n "$POSE_LINE" ] && printf 'demo_probe_camera_line=%s\n' "$POSE_LINE" >> "$DIR/camera.txt"

  # ---- 3. two captures, same demo, same fixed camera -----------------------
  TICK_STATUS=ok
  RC_0=NA; RC_2000=NA
  for TICK in "${CAPTURE_TICKS[@]}"; do
    TRACE=$SCRATCH/$MAP.tick$TICK.trace
    BMP=$SCRATCH/$MAP.tick$TICK.bmp
    METRICS=$SCRATCH/$MAP.tick$TICK.csv
    LOG=$DIR/run-tick$TICK.log
    : > "$TRACE"
    if [ "$C_EYE" = "NA" ]; then
      failure "$MAP tick $TICK not run: no fixed camera"
      TICK_STATUS=camera-unresolved
      continue
    fi
    CMD=$(product_cmd "$(cygpath -m "$TRACE")" "$(cygpath -m "$BMP")" "$(cygpath -m "$METRICS")" "$DEMO" "$TICK" --debug-camera $C_EYE $C_PITCH $C_YAW)
    locked "$CMD" > "$LOG" 2>&1
    rc=$?
    if [ "$TICK" = "0" ]; then RC_0=$rc; else RC_2000=$rc; fi
    [ "$rc" -ne 0 ] && { failure "$MAP tick $TICK product rc=$rc (see run-tick$TICK.log)"; TICK_STATUS=product-failed; }
    [ -s "$BMP" ] || { failure "$MAP tick $TICK wrote no capture frame ($BMP)"; TICK_STATUS=no-frame; }
    extract_trace "$MAP" "$TICK" "$TRACE" "$DIR/trace-tick$TICK.txt"
    tile_table "$TRACE" "$DIR/tile-tick$TICK.raw" "$DIR/tile-tick$TICK.tsv"
    ARMED=$(readings_only "$DIR/trace-tick$TICK.txt" 'capture: armed ' | tail -1)
    [ -z "$ARMED" ] && { failure "$MAP tick $TICK has no capture: armed line"; TICK_STATUS=no-capture-line; }
    FETCH=$(readings_only "$DIR/trace-tick$TICK.txt" 'gpu: world material-fetch ' | head -1)
    [ -z "$FETCH" ] && { failure "$MAP tick $TICK has no gpu: world material-fetch line"; TICK_STATUS=no-fetch-line; }
  done

  # ---- 4. pixel diff between the two captures -------------------------------
  BMP_0=$SCRATCH/$MAP.tick0.bmp
  BMP_2000=$SCRATCH/$MAP.tick2000.bmp
  DIFF=$DIR/diff.txt
  D_PIX=NA; D_PCT=NA; D_MAX=NA; D_SHA0=NA; D_SHA2000=NA; D_IDENT=NA
  if [ -s "$BMP_0" ] && [ -s "$BMP_2000" ]; then
    if diff_frames "$BMP_0" "$BMP_2000" "$CHANNEL_DELTA" "$DIFF"; then
      D_PIX=$(sed -n 's/^pixels_channel_gt8=//p' "$DIFF")
      D_PCT=$(sed -n 's/^percent_channel_gt8=//p' "$DIFF")
      D_MAX=$(sed -n 's/^max_channel_delta=//p' "$DIFF")
      D_SHA0=$(sed -n 's/^sha256_a=//p' "$DIFF")
      D_SHA2000=$(sed -n 's/^sha256_b=//p' "$DIFF")
      D_IDENT=$(sed -n 's/^identical_files=//p' "$DIFF")
      [ "$D_IDENT" = "1" ] && failure "$MAP tick 0 and tick 2000 captured byte-identical frames"
    else
      failure "$MAP pixel diff failed (see diff.txt)"
      TICK_STATUS=diff-failed
    fi
  else
    failure "$MAP pixel diff skipped: a capture frame is missing"
  fi

  # ---- 5. one row per tick -------------------------------------------------
  for TICK in "${CAPTURE_TICKS[@]}"; do
    TF_FILE=$DIR/trace-tick$TICK.txt
    FETCH=$(readings_only "$TF_FILE" 'gpu: world material-fetch ' | head -1)
    PLAN=$(readings_only "$TF_FILE" 'gpu: world atlas-plan ' | head -1)
    ATLAS=$(readings_only "$TF_FILE" 'gpu: world atlas ' | head -1)
    ARMED=$(readings_only "$TF_FILE" 'capture: armed ' | tail -1)
    TRACE_BYTES=$(sed -n 's/^# map=[^ ]* tick=[^ ]* trace=.* bytes=\([0-9]*\) lines=.*/\1/p' "$TF_FILE" | head -1)
    TILE_ROWS=$(grep -ac 'material=' "$DIR/tile-tick$TICK.raw" 2>/dev/null || true)
    assert_tick "$MAP" "$TICK" "$FETCH" "$PLAN" "$ATLAS" "$ARMED" "$TILE_ROWS"
    assert_tile_table "$MAP" "$TICK" "$DIR/tile-tick$TICK.raw" "$PLAN"

    if [ "$TICK" = "0" ]; then
      PRC=$RC_0
      SHA=$D_SHA0
    else
      PRC=$RC_2000
      SHA=$D_SHA2000
    fi
    NOTE=""
    [ -n "${D_PCT:-}" ] && [ "$D_PCT" != "NA" ] && NOTE="diff-is-per-map"
    row "$MAP" "$TICK" "$TICK_STATUS" "$BSP" "$BSP_BYTES" "$DEMO" "$C_SRC" "${C_EYE:-NA}" \
      "${P_RC:-NA}" "${P_DISTINCT:-NA}" "${P_TILED:-NA}" "${P_VMT_RES:-NA}" "${P_VMT_MISS:-NA}" \
      "${P_VMT_NOBASE:-NA}" "${P_VTF_RES:-NA}" "${P_VTF_MISS:-NA}" "${P_BATCHES:-NA}" \
      "${P_REFUSED:-NA}" "${P_BEYOND:-NA}" "$P_TILE" \
      "$PRC" "$(na "$(rval "$FETCH" distinct)")" "$(na "$(rval "$FETCH" tiled)")" \
      "$(na "$(rval "$FETCH" capped)")" "$(na "$(rval "$FETCH" vmtMissing)")" \
      "$(na "$(rval "$FETCH" vmtNoBase)")" "$(na "$(rval "$FETCH" vtfMissing)")" \
      "$(na "$(rval "$FETCH" patchDeclared)")" "$(na "$(rval "$FETCH" patchResolved)")" \
      "$(na "$(rval "$FETCH" transformRefused)")" \
      "$(na "$(rval "$PLAN" batches)")" "$(na "$(rval "$PLAN" tiled)")" \
      "$(na "$(rval "$PLAN" refused)")" "$(na "$(rval "$PLAN" malformed)")" \
      "$(na "$(rval "$PLAN" duplicate)")" "$(na "$(rval "$PLAN" budget)")" \
      "$(na "$(rval "$ATLAS" state)")" "$(na "$(rval "$ATLAS" bytes)")" \
      "$(na "${TILE_ROWS:-0}")" "$(na "$(rval "$ARMED" requested)")" \
      "$(na "$(rval "$ARMED" frame_tick)")" "$(na "$(rval "$ARMED" frames)")" \
      "$(na "${TRACE_BYTES:-}")" "${SHA:-NA}" "$D_PIX" "$D_PCT" "$D_MAX" "$NOTE"
  done
  printf 'map=%s status=%s probe_rc=%s product_rc_tick0=%s product_rc_tick2000=%s tile_rows_0=%s tile_rows_2000=%s\n' \
    "$MAP" "$TICK_STATUS" "${P_RC:-NA}" "$RC_0" "$RC_2000" \
    "$(grep -ac 'material=' "$DIR/tile-tick0.raw" 2>/dev/null || true)" \
    "$(grep -ac 'material=' "$DIR/tile-tick2000.raw" 2>/dev/null || true)" > "$DIR/status.txt"
done

if [ "$ran" -eq 0 ]; then
  echo "no map selected -- nothing to do" >&2
  exit 2
fi

if [ -s "$PREV" ]; then
  HDR=$(head -1 "$OUT/readings.tsv")
  { printf '%s\n' "$HDR"
    { tail -n +2 "$PREV" | awk -F'\t' -v run=",$ONLY," 'index(run, "," $1 ",") == 0'
      tail -n +2 "$OUT/readings.tsv"; } | LC_ALL=C sort -t$'\t' -k1,1 -k2,2n
  } > "$OUT/readings.tsv.merged"
  mv "$OUT/readings.tsv.merged" "$OUT/readings.tsv"
  rm -f "$PREV"
fi

echo
# ---- 7. the same invariants, asserted over EVERY row in readings.tsv ---------
# A --only run merges rows written by earlier runs, so the merged matrix has to
# be held to the same bounds as the rows this run wrote; otherwise a green run
# would hide a stale row that violates an invariant.
MATRIX_OUT=$OUT/matrix-check.txt
awk -F'\t' '
  NR==1{for(i=1;i<=NF;i++) col[$i]=i; next}
  {
    m=$1; t=$2; st=$3
    if (st=="UNSUPPORTED") { unsup++; seen[$1]=1; next }
    rows++; seen[$1]=1
    bad=""
    if (st!="ok") bad=bad " status=" st
    if ($(col["probe_rc"])!="0") bad=bad " probe_rc=" $(col["probe_rc"])
    if ($(col["product_rc"])!="0") bad=bad " product_rc=" $(col["product_rc"])
    if ($(col["fetch_capped"])!="0") bad=bad " fetch_capped=" $(col["fetch_capped"])
    if ($(col["plan_malformed"])!="0") bad=bad " plan_malformed=" $(col["plan_malformed"])
    if ($(col["plan_duplicate"])!="0") bad=bad " plan_duplicate=" $(col["plan_duplicate"])
    if ($(col["tile_rows"])+0 != $(col["plan_tiled"])+0 + $(col["plan_refused"])+0)
      bad=bad " tile_rows=" $(col["tile_rows"]) " vs plan_tiled+refused=" $(col["plan_tiled"]) "+" $(col["plan_refused"])
    if ($(col["atlas_state"])!="bound") bad=bad " atlas_state=" $(col["atlas_state"])
    if ($(col["atlas_bytes"])+0<=0) bad=bad " atlas_bytes=" $(col["atlas_bytes"])
    if ($(col["capture_requested"]) != $(col["capture_frame_tick"]))
      bad=bad " capture_frame_tick=" $(col["capture_frame_tick"]) " requested=" $(col["capture_requested"])
    if (t+0>0 && $(col["frames_rendered"])+0<=0) bad=bad " frames_rendered=" $(col["frames_rendered"])
    if ($(col["diff_pixels_ch8"])+0<=0) bad=bad " diff_pixels_ch8=" $(col["diff_pixels_ch8"])
    if (bad!="") { printf "FAIL %s tick %s:%s\n", m, t, bad; fails++ }
  }
  END {
    n=0; for (k in seen) n++
    printf "MATRIX maps=%d rows=%d unsupported=%d failed=%d\n", n, rows, unsup, fails
  }' "$OUT/readings.tsv" > "$MATRIX_OUT"
MATRIX_LINE=$(tail -1 "$MATRIX_OUT")
MATRIX_FAILED=$(printf '%s' "$MATRIX_LINE" | sed -n 's/.*failed=\([0-9]*\).*/\1/p')
if [ "${MATRIX_FAILED:-0}" -gt 0 ]; then
  grep '^FAIL ' "$MATRIX_OUT" | sed 's/^FAIL /matrix invariant: /' >> "$OUT/failures.txt"
  fail=1
fi

echo
echo "=== readings: $OUT/readings.tsv ==="
column -t -s $'\t' "$OUT/readings.tsv" 2>/dev/null | cut -c1-200 || sed -n '1,20p' "$OUT/readings.tsv"
echo
if [ "$fail" -ne 0 ]; then
  echo "FAIL: a reading is missing, a run failed, or an invariant does not hold -- see $OUT/failures.txt"
  sed -n '1,20p' "$OUT/failures.txt"
  exit 1
fi
echo "MAP-MATERIAL-MATRIX=PASS $(printf '%s' "$MATRIX_LINE" | sed 's/^MATRIX //')"
exit 0
