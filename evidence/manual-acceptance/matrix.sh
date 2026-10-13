#!/usr/bin/env bash
# T2 manual-acceptance matrix: FOV x hand x weapon-body x tick.
#
# WHY A SCRIPT AND NOT 36 BY HAND
#   Each cell is one product launch, and a launch writes a trace that is
#   regenerated from scratch. A hand-typed command that silently loses a flag
#   (this round's own pilot lost `--viewmodel` twice) produces a run with no
#   readings at all, and a matrix with an empty cell looks like a measurement
#   of nothing. Every cell goes through one function so the flag list exists
#   exactly once.
#
# WHY --settings-file
#   The product writes the user's real settings file on exit. Without an
#   isolated one, a run that overrides the FOV leaves `settingsFov` behind and
#   the NEXT run inherits it -- observed here as `settingsFov=40.0` on a run
#   whose command line said 90. Each cell gets its own settings file under the
#   trace directory, so a cell describes the build, not the previous cell.
#
# Evidence written per cell: BMP (frozen witness), trace (readings), and one
# CSV line appended to matrix.csv (sha256 + region shares + readings).
set -uo pipefail
# The script lives in <root>/evidence/manual-acceptance/, so the root is two
# levels up. One level up is `evidence/`, and a relative EXE path from there is
# rc=127 on every cell -- which is what the first version of this script did.
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
echo "root=$ROOT"

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
EXE=native/build-nmake/tf2_demo_native.exe
MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
OUT=evidence/manual-acceptance
TRACE_DIR="$OUT/traces"
mkdir -p "$TRACE_DIR"

# Regions, taken from the ViewModel-material gate so the two reports speak about
# the same pixels. Left and right thirds and the top band are OUTSIDE the
# ViewModel; the ViewModel composite lives in the middle-right of the frame.
# 1264x681, so the middle third is x=420..844.
declare -A WORLD_REGION=(
  [left]="0,0,420,681"
  [right]="844,0,420,681"
  [top]="0,0,1264,250"
)
# The ViewModel's own region: the arm/weapon composite, right of centre.
VM_REGION="420,250,424,431"

sha256_of() { sha256sum "$1" | cut -d' ' -f1; }

cell() {
  local fov="$1" hand="$2" body="$3" tick="$4"
  local name="f${fov}-${hand}-b${body}-t${tick}"
  local bmp="$OUT/$name.bmp"
  local trace="$TRACE_DIR/$name.trace"
  local csv="$TRACE_DIR/$name.csv"
  local ini="$TRACE_DIR/$name-settings.ini"
  TF2_NATIVE_TRACE="$ROOT/$trace" \
  timeout 300 "$ROOT/$EXE" \
    --tf-root "$TF" --audio-device 6 --demo "$DEMO" \
    --settings-file "$ini" \
    --viewmodel "$MODEL" --viewmodel-render command \
    --viewmodel-fov "$fov" --viewmodel-hand "$hand" --viewmodel-weapon-body "$body" \
    --capture-tick "$tick" --capture-frame "$bmp" --metrics-file "$csv" \
    > "$TRACE_DIR/$name.log" 2>&1
  local rc=$?
  local size; size=$(stat -c%s "$bmp" 2>/dev/null || echo 0)
  if [ "$rc" -ne 0 ] || [ "$size" -eq 0 ]; then
    echo "$name rc=$rc size=$size FAILED"
    return 1
  fi
  local reading order
  reading=$(grep -a "viewModelPath=" "$trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  order=$(grep -a "gpu: viewmodel pass order" "$trace" | tr -d '\r' | tail -1 | sed 's/^[0-9]\{1,\}\(\.[0-9]*\)\? //')
  local drawn handRead fovRead attach
  drawn=$(echo "$reading"  | sed -n 's/.* drawn=\([0-9]*\).*/\1/p')
  handRead=$(echo "$reading" | sed -n 's/.* hand=\([a-z]*\).*/\1/p')
  fovRead=$(echo "$reading"  | sed -n 's/.* fov=\([0-9.]*\).*/\1/p')
  attach=$(echo "$reading"  | sed -n 's/.* attachmentStatus=\([^ ]*\).*/\1/p')
  # Regions are diffed against the same cell's own weapon-body-off twin, so the
  # share isolates the body draw; the frame is a frozen world, so the world
  # region shares must stay at 0.0000 for every pair.
  local offBmp="$OUT/f${fov}-${hand}-boff-t${tick}.bmp"
  local vmShare="n/a" worldShare="n/a"
  if [ -f "$offBmp" ]; then
    local diff
    diff=$(python bmp-pixel-diff.py --min-delta 1 "$bmp" "$offBmp" --region "$VM_REGION" 2>/dev/null | grep -oE "share=[0-9.]+" | tail -1 | cut -d= -f2)
    vmShare="${diff:-0.0000}"
    local worldMax="0.0000"
    local r
    for r in left right top; do
      local d
      d=$(python bmp-pixel-diff.py --min-delta 1 "$bmp" "$offBmp" --region "${WORLD_REGION[$r]}" 2>/dev/null | grep -oE "share=[0-9.]+" | tail -1 | cut -d= -f2)
      [ -n "$d" ] && awk -v a="$d" -v b="$worldMax" 'BEGIN{exit !(a>b)}' && worldMax="$d"
    done
    worldShare="$worldMax"
  fi
  local sum
  sum=$(sha256_of "$bmp")
  echo "$name,$fov,$hand,$body,$tick,$sum,$size,$drawn,$handRead,$fovRead,$attach,$vmShare,$worldShare" >> "$OUT/matrix.csv"
  echo "$name rc=$rc size=$size drawn=$drawn hand=$handRead fov=$fovRead vm_share=$vmShare world_share=$worldShare sha=${sum:0:16}"
}

echo "cell,fov,hand,body,tick,sha256,size,drawn,hand,fov,attachments,vm_share,world_share" > "$OUT/matrix.csv"
for fov in 40 90 120; do
  for hand in left right; do
    for tick in 2000 4000 6000; do
      # off first, then on, so every on-cell finds its twin on disk
      cell "$fov" "$hand" off "$tick" || true
      cell "$fov" "$hand" on  "$tick" || true
    done
  done
done
echo "=== matrix complete: $(grep -c . "$OUT/matrix.csv") lines (incl header) ==="
grep -c FAILED "$OUT/matrix.csv" 2>/dev/null || true
