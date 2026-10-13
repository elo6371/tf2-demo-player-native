#!/usr/bin/env bash
# T2 matrix, run 2 -- reduced after the first run taught us what this demo can
# and cannot separate.
#
# WHAT RUN 1 ESTABLISHED (evidence/manual-acceptance/matrix.csv, 4 cells)
#   * `--viewmodel` changes the picture: no-ViewModel vs f40/left = 57.986% of
#     the frame differing (max_delta=153). The composite is being drawn.
#   * FOV and hand separate: f40/left vs f90/left = 10.3826% differing.
#   * tick 2000 vs tick 4000 are BYTE-IDENTICAL (differing=0) -- the frozen
#     world plus the same ViewModel pose at both playback positions.
#   * weapon-body on vs off are BYTE-IDENTICAL, because this demo names no
#     ViewModel entity at these positions: both readings say
#     `weaponFallback=noViewModelEntity weaponVisible=0 weaponDrawCalls=0`.
#
# SO WHAT THIS RUN DOES
#   It stops paying 80 seconds per cell to re-derive those two negatives and
#   spends the time on the axes that do separate: every FOV x hand pair at
#   tick 2000, plus one cell at tick 6000 to confirm the tick finding holds
#   there too (4000 was the one that was byte-identical; 6000 is a third
#   position, so "identical" is not an artefact of one pair).
#
# The readings recorded per cell are the ones the plan names: model path,
# attachment status, FOV, hand, arm/weapon draw counts, weapon visibility,
# BMP sha256, and the ViewModel/world region shares against the twin frame.
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
EXE=native/build-nmake/tf2_demo_native.exe
MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
OUT=evidence/manual-acceptance
TRACE_DIR="$OUT/traces"
mkdir -p "$TRACE_DIR"

# Regions from the ViewModel-material gate, so both reports name the same pixels.
declare -A WORLD_REGION=( [left]="0,0,420,681" [right]="844,0,420,681" [top]="0,0,1264,250" )
VM_REGION="420,250,424,431"

csv_field() { sed -n 's/.* '"$1"'=\([^ ]*\).*/\1/p'; }

cell() {
  local fov="$1" hand="$2" tick="$3"
  local name="r2-f${fov}-${hand}-t${tick}"
  local bmp="$OUT/$name.bmp"
  local trace="$TRACE_DIR/$name.trace"
  local csv="$TRACE_DIR/$name.csv"
  local ini="$TRACE_DIR/$name-settings.ini"
  TF2_NATIVE_TRACE="$ROOT/$trace" timeout 300 "$ROOT/$EXE" \
    --tf-root "$TF" --audio-device 6 --demo "$DEMO" \
    --settings-file "$ini" \
    --viewmodel "$MODEL" --viewmodel-render command \
    --viewmodel-fov "$fov" --viewmodel-hand "$hand" --viewmodel-weapon-body on \
    --capture-tick "$tick" --capture-frame "$bmp" --metrics-file "$csv" \
    > "$TRACE_DIR/$name.log" 2>&1
  local rc=$?
  local size; size=$(stat -c%s "$bmp" 2>/dev/null || echo 0)
  if [ "$rc" -ne 0 ] || [ "$size" -eq 0 ]; then
    echo "$name rc=$rc size=$size FAILED"
    return 1
  fi
  local reading order body
  reading=$(grep -a "viewModelPath=" "$trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  order=$(grep -a "gpu: viewmodel pass order" "$trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  body=$(grep -a "weaponBodyReading" "$trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  local armDraw weaponDraw weaponVisible attach fovRead handRead path
  path=$(echo "$reading"      | csv_field viewModelPath)
  armDraw=$(echo "$body"      | csv_field armDrawCalls)
  weaponDraw=$(echo "$body"   | csv_field weaponDrawCalls)
  weaponVisible=$(echo "$body"| csv_field weaponVisible)
  attach=$(echo "$reading"    | sed -n 's/.* attachmentStatus=\([^ ]*\).*/\1/p')
  fovRead=$(echo "$reading"   | csv_field fov)
  handRead=$(echo "$reading"  | csv_field hand)
  local sum; sum=$(sha256sum "$bmp" | cut -d' ' -f1)
  echo "$name,$fov,$hand,$tick,$sum,$size,$path,$attach,$fovRead,$handRead,$armDraw,$weaponDraw,$weaponVisible" >> "$OUT/matrix2.csv"
  echo "$name rc=$rc size=$size hand=$handRead fov=$fovRead arm=$armDraw weapon=$weaponDraw visible=$weaponVisible sha=${sum:0:16}"
}

echo "cell,fov,hand,tick,sha256,size,model,attachments,fov,hand,armDrawCalls,weaponDrawCalls,weaponVisible" > "$OUT/matrix2.csv"
for fov in 40 90 120; do
  for hand in left right; do
    cell "$fov" "$hand" 2000 || true
  done
done
# The tick axis at one hand/FOV, to test whether 6000 separates where 4000 did not.
for tick in 4000 6000; do
  cell 90 left "$tick" || true
done
echo "=== done: $(grep -c . "$OUT/matrix2.csv") lines (incl header) ==="
