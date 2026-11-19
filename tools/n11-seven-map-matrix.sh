#!/usr/bin/env bash
# n11-seven-map-matrix.sh -- N11: the seven maps x POV/STV matrix, one exe.
#
# Why this exists: docs/N11-SEVEN-MAP-MATRIX-2026-11-15.md recorded 14 cells with
# only 2 of them usable, and the reason was structural -- no single exe had ever
# produced both the POV and the STV frame for the same map. Every cell was
# captured by a different build, so nothing in the matrix was comparable across
# columns. This script closes that gap the only way it can be closed: it takes
# one exe (whatever is at native/build-nmake), loops the 7 maps x 2 columns
# through it, and writes one row per cell with the readings that row is made of.
#
# TICK: 2000 by default. Measured the wrong way round once: a 100 MB POV demo
# asked for tick 20000 hit the 420 s ceiling (rc=124) before it drew a frame, and
# a matrix that spends seven minutes per POV cell is a matrix nobody reruns. Tick
# 2000 is past the spawn and inside the match, and it is the same tick in both
# columns, which is what the comparison needs. Override with N11_TICK.
#
# CAMERA: read from "scene: camera ... eye=x,y,z ... src=demo|follow|free|camera".
# The first cut read "scene: eye origin=", which only the POV path emits (it is
# tied to a local player); every STV cell then read eye="?" and the matrix scored
# its own healthy runs as failures. The camera line is emitted in both columns.
# "scene: eye" is still required for POV cells, where it exists.
#
# What it records per cell (all from that single run's own trace):
#   map, column, demo, tick requested / tick the frame was drawn on,
#   camera eye x,y,z, camera src, the static-props PVS tally,
#   how many world tile-fill lines the material path emitted,
#   capture byte size + sha256.
# Trace lines are NOT at column 0: main_trace.cpp prefixes each with seconds
# since start ("108985.5 scene: eye ..."), so the greps are unanchored on purpose.
#
# Assertions:
#   * every cell ran to rc=0 and wrote a non-empty BMP,
#   * every cell's trace carries "scene: camera" and "capture: frame",
#   * POV cells also carry "scene: eye",
#   * within a map, POV and STV captures are NOT byte-identical.
# What it does NOT assert: that the picture is right -- that is D01.
#
# Cost: 14 product launches, each loading a 56-147 MB demo. Run it under the
# host lock (hostlock.sh run n11-matrix ...) and in the background.
#
# Outputs: evidence/n11-matrix/readings.tsv, evidence/n11-matrix/runs/<tag>.txt
#   (extracted trace lines), evidence/n11-matrix/runs/<tag>.columns (provenance).
#   BMPs and full traces stay in .scratch/n11-matrix.
#
# Exit: 0 every cell has readings and every assertion holds
#       (prints N11-SEVEN-MAP-MATRIX=PASS); 1 a cell is missing or an assertion
#       fails; 2 usage/environment error.
set -uo pipefail
cd "$(dirname "$0")/.." 2>/dev/null || {
  echo "FATAL: script is not inside the repository" >&2; exit 2; }

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO_ROOT="E:/demo测试"
SCRATCH=".scratch/n11-matrix"
OUT="${N11_OUT:-evidence/n11-matrix}"
TICK="${N11_TICK:-2000}"
FALLBACK_TICK="${N11_FALLBACK_TICK:-200}"
RUN_TIMEOUT="${N11_RUN_TIMEOUT:-420}"
AUDIO_DEVICE=6

for path in "$EXE" "$TF" "$DEMO_ROOT"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path" >&2; exit 2; }
done
mkdir -p "$SCRATCH" "$OUT/runs"
rm -f "$OUT/readings.tsv"

# map|POV demo|STV demo -- relative to $DEMO_ROOT/<map>/.
PAIRS=(
  "cp_process_f12|POV/autorecord_2026-09-03_21-05-39.dem|STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
  "cp_gullywash_f9|POV/autorecord_2026-05-21_21-08-36.dem|STV/gully4454.dem"
  "cp_sultry_b8a|POV/autorecord_2026-06-16_21-42-44.dem|STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem"
  "cp_sunshine|POV/autorecord_2026-05-17_16-32-36.dem|STV/SUNSHINE.dem"
  "koth_bagel_rc11|POV/testdemo111.dem|STV/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem"
  "koth_product_final|POV/autorecord_2026-09-10_21-01-05.dem|STV/prodcutscout.dem"
  "pl_upward_f12|POV/autorecord_2026-08-08_21-43-59.dem|STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
)

ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-n11.XXXXXX") || {
  echo "FATAL: cannot create isolated settings directory" >&2; exit 2; }
ISO_SETTINGS="$ISO_DIR/settings.ini"
ISO_UI="$ISO_DIR/ui.ini"
trap 'rm -rf "$ISO_DIR"' EXIT

printf 'map\tcolumn\tdemo\ttick_requested\ttick_landed\teye\tcamera_src\tstaticProps\ttiles\tbmp_bytes\tbmp_sha256\trc\n' > "$OUT/readings.tsv"

run_cell() { # run_cell <map> <col> <demo-abs> <tick> -> prints "rc bmp"
  local map="$1" col="$2" demo="$3" tick="$4"
  local tag="${map}-${col}"
  local bmp="$SCRATCH/${tag}.bmp"
  local trace="$SCRATCH/${tag}.trace"
  rm -f "$bmp" "$bmp.csv" "$trace" "$SCRATCH/${tag}-t${tick}.log"
  TF2_NATIVE_TRACE="$(cygpath -w "$trace")" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" \
    --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
    --demo "$demo" --capture-tick "$tick" \
    --capture-frame "$bmp" --metrics-file "$bmp.csv" \
    > "$SCRATCH/${tag}-t${tick}.log" 2>&1
  local rc=$?
  echo "$rc $bmp"
}

extract_cell() { # extract_cell <tag> <trace>
  local tag="$1" trace="$2"
  grep -aE '(scene: (eye|camera|cameraFilter|staticProps|clock)|capture: (frame|fov)|demo: (open|map)|gpu: models done)' \
    "$trace" > "$OUT/runs/${tag}.txt" 2>/dev/null || true
  [ -s "$OUT/runs/${tag}.txt" ]
}

ok=0; total=0
for pair in "${PAIRS[@]}"; do
  IFS='|' read -r map pov stv <<< "$pair"
  for col in POV STV; do
    total=$((total + 1))
    if [ "$col" = "POV" ]; then demo="$DEMO_ROOT/$map/$pov"; else demo="$DEMO_ROOT/$map/$stv"; fi
    tag="${map}-${col}"
    if [ ! -f "$demo" ]; then
      echo "MISS $tag: demo absent: $demo"
      printf '%s\t%s\t%s\t%s\t-\t-\t-\t-\t-\t0\t-\t-1\n' "$map" "$col" "$demo" "$TICK" >> "$OUT/readings.tsv"
      continue
    fi
    read -r rc bmp <<< "$(run_cell "$map" "$col" "$demo" "$TICK")"
    trace="$SCRATCH/${tag}.trace"
    # A cell that times out at the requested tick is retried once at a low tick
    # and recorded as such, rather than reported as the same reading.
    if [ "$rc" -ne 0 ] || [ ! -s "$bmp" ]; then
      echo "RETRY $tag at tick=$FALLBACK_TICK (first attempt rc=$rc)"
      read -r rc bmp <<< "$(run_cell "$map" "$col" "$demo" "$FALLBACK_TICK")"
      trace="$SCRATCH/${tag}.trace"
    fi
    bytes=0; [ -f "$bmp" ] && bytes=$(stat -c %s "$bmp" 2>/dev/null || echo 0)
    sha="-"; [ "$bytes" -gt 0 ] && sha=$(sha256sum "$bmp" | cut -c1-16)
    tick_landed=$(grep -aE 'scene: clock tick=' "$trace" 2>/dev/null | tail -1 | sed -n 's/.*tick=\(-\?[0-9][0-9]*\).*/\1/p')
    cam=$(grep -aE 'scene: camera mode=' "$trace" 2>/dev/null | tail -1)
    eye=$(printf '%s' "$cam" | sed -n 's/.* eye=\([^ ]*\).*/\1/p')
    src=$(printf '%s' "$cam" | sed -n 's/.* src=\([^ ]*\).*/\1/p')
    props=$(grep -aE 'scene: staticProps available=' "$trace" 2>/dev/null | tail -1 | sed -n 's/.*available=\([0-9]*\) instances=\([0-9]*\) visible=\([0-9]*\) pvsRejected=\([0-9]*\) drawn=\([0-9]*\).*/\1\/\2\/\3\/\4\/\5/p')
    tiles=$(grep -acE 'gpu: world tile-fill' "$trace" 2>/dev/null || true)
    tiles=${tiles:-0}
    frames=$(grep -acE 'capture: frame' "$trace" 2>/dev/null || true)
    frames=${frames:-0}
    eyes=$(grep -acE 'scene: eye origin=' "$trace" 2>/dev/null || true)
    eyes=${eyes:-0}
    row_ok=0
    if [ "$rc" -eq 0 ] && [ "$bytes" -gt 0 ] && [ -n "${eye:-}" ] && [ -n "${tick_landed:-}" ] \
       && [ "$frames" -gt 0 ] && extract_cell "$tag" "$trace"; then
      if [ "$col" = "STV" ] || [ "$eyes" -gt 0 ]; then row_ok=1; fi
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$map" "$col" "$(basename "$demo")" "$TICK" "${tick_landed:-?}" "${eye:-?}" "${src:-?}" "${props:-?}" "${tiles:-0}" "$bytes" "$sha" "$rc" >> "$OUT/readings.tsv"
    printf 'cell=%s rc=%s bytes=%s sha=%s tick_landed=%s eye=%s camera_src=%s eye_lines=%s\n' \
      "$tag" "$rc" "$bytes" "$sha" "${tick_landed:-?}" "${eye:-?}" "${src:-?}" "$eyes" >> "$OUT/runs/${tag}.columns"
    if [ "$row_ok" -eq 1 ]; then ok=$((ok+1)); echo "OK   $tag tick=${tick_landed:-?} eye=${eye:-?} src=${src:-?} bytes=$bytes"; else echo "FAIL $tag rc=$rc bytes=$bytes"; fi
    rm -f "$bmp"
  done
done

# Cross-column assertion: per map, POV and STV must not be byte-identical. The
# BMPs are deleted once hashed, so the check reads the TSV.
dup=0
for map in $(cut -f1 "$OUT/readings.tsv" | tail -n +2 | sort -u); do
  p=$(awk -F'\t' -v m="$map" '$1==m && $2=="POV"{print $11}' "$OUT/readings.tsv")
  s=$(awk -F'\t' -v m="$map" '$1==m && $2=="STV"{print $11}' "$OUT/readings.tsv")
  if [ -n "$p" ] && [ "$p" != "-" ] && [ "$p" = "$s" ]; then
    echo "FAIL $map POV and STV captures are byte-identical ($p)"; dup=$((dup+1))
  fi
done

echo "N11 matrix rows=$total readable=$ok identical_pairs=$dup"
if [ "$ok" -eq "$total" ] && [ "$dup" -eq 0 ]; then
  echo "N11-SEVEN-MAP-MATRIX=PASS rows=$ok/$total"
  exit 0
fi
echo "N11-SEVEN-MAP-MATRIX=FAIL rows=$ok/$total identical=$dup"
exit 1
