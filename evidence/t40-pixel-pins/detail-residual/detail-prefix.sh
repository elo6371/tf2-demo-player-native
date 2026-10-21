#!/usr/bin/env bash
# T45 hold C: attribute T40's 2,812-px detail residual with T29's own near-plane
# mutation.
#
# The residual is 27 non-displaced WOOD/WALL007A faces where the detail layer's
# effect toggles between the pre-T29 frame (kept: sha 21cb8aa2...) and the
# current frame (sha 2ffa7d2b...) at pixels whose baseline byte did not move.
# The two builds' traces are identical line-for-line except 'gpu: world clip',
# so the test is: put the box-unit 0.05 near plane back, capture A and F at tick
# 2000, and compare against both kept frames. If the mutation reproduces the
# pre-fix F (and its A), the near plane is the cause; if it does not, the cause
# is a tree-level difference outside the projection and this script says so.
#
# The mutation is T29's own m1 patch. It is restored byte-identically (cmp +
# sha printed) and the restored build must reproduce the current pinned A/F.
# Run only under the shared host lock.
set -uo pipefail
source "$(dirname "$0")/lib.sh" || exit 2
OUT=.scratch/t45/detail
mkdir -p "$OUT"
SRC=native/src/native_renderer.cpp
SAVE="$OUT/native_renderer.cpp.save"
PREFIX_F="D:/TF2_Native_Worktrees/t40-pixel-pins/.scratch/t40/frames/material-old/F.bmp"
PREFIX_A="D:/TF2_Native_Worktrees/t40-pixel-pins/.scratch/t40/frames/material-old/A.bmp"
PIN_A=6bb4d9a712e82ef4cf58929651cea1070d11c1ac082bdb25a6e6d7097e2f974b
PIN_F=2ffa7d2bfeb9e6e9de160315f460c3384b8a0d124cfb060b6bc905752f38f8b5
OLD_A=f87f396a60dc72082f232bcd71547066c02c433e35ccda1576c3b21333ef9608
OLD_F=21cb8aa28df87b6c869e12a12be240ab79c0d5d5370bf97b073652890280647f

echo "=== T45 detail-prefix $(date '+%Y-%m-%dT%H:%M:%S') ==="
echo "HEAD=$(git rev-parse HEAD) exe=$(sha "$PRODUCT" 2>/dev/null || echo absent)"
echo "kept pre-fix frames: A=$OLD_A F=$OLD_F"
echo "pre-fix frames present: $( [ -s "$PREFIX_A" ] && echo A=yes || echo A=no ) $( [ -s "$PREFIX_F" ] && echo F=yes || echo F=no )"

echo "== current-tree references =="
capture "$OUT" A_ref 2000 || exit 1
capture "$OUT" F_ref 2000 --world-detail on --world-detail-texture materials/brick/brickwall001.vtf || exit 1
echo "A_ref $(sha "$OUT/A_ref.bmp") pin=$PIN_A"
echo "F_ref $(sha "$OUT/F_ref.bmp") pin=$PIN_F"
if [ "$(sha "$OUT/A_ref.bmp")" != "$PIN_A" ] || [ "$(sha "$OUT/F_ref.bmp")" != "$PIN_F" ]; then
  echo "STOP: the current tree does not reproduce T40's pinned frames; the residual classification would not be comparable."
  exit 3
fi

echo "== mutation: the near plane is the box-unit constant 0.05 again (T29 m1) =="
cp "$SRC" "$SAVE"
echo "source_before=$(sha "$SRC")"
python - "$SRC" <<'PYEOF'
import sys
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
old = """    const float worldNearPlane = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
      ? std::max(worldHorizontalScale_ * kWorldNearMapUnits, 1.0e-6f) : 0.05f;"""
new = """    const float worldNearPlane = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
      ? 0.05f : 0.05f;"""
assert old in src, "M1 patch site not found"
open(path, 'w', encoding='utf-8', newline='').write(src.replace(old, new))
PYEOF
if [ $? -ne 0 ]; then echo "MUTATION-PATCH=FAIL"; cp "$SAVE" "$SRC"; exit 1; fi
echo "source_mutated=$(sha "$SRC")"
bash build-target.sh tf2_demo_native > "$OUT/mutation.build" 2>&1
if [ $? -ne 0 ]; then echo "MUTATION-BUILD=FAIL (see $OUT/mutation.build)"; cp "$SAVE" "$SRC"; exit 1; fi
echo "exe_mutated=$(sha "$PRODUCT")"
capture "$OUT" A_mut 2000 || { echo "MUTATION-CAPTURE=FAIL A_mut"; cp "$SAVE" "$SRC"; exit 1; }
capture "$OUT" F_mut 2000 --world-detail on --world-detail-texture materials/brick/brickwall001.vtf || { echo "MUTATION-CAPTURE=FAIL F_mut"; cp "$SAVE" "$SRC"; exit 1; }
CLIP=$(grep -a "gpu: world clip " "$OUT/A_mut.trace" | head -1 | tr -d '\r')
echo "MUTATED-CLIP $CLIP"

echo "== restore =="
cp "$SAVE" "$SRC"
if cmp -s "$SAVE" "$SRC"; then echo "RESTORE=OK source byte-identical"; else echo "RESTORE=FAIL"; exit 1; fi
echo "source_after=$(sha "$SRC")"
bash build-target.sh tf2_demo_native > "$OUT/restore.build" 2>&1
if [ $? -ne 0 ]; then echo "RESTORE-BUILD=FAIL"; exit 1; fi
echo "exe_restored=$(sha "$PRODUCT")"
capture "$OUT" A_res 2000 || exit 1
capture "$OUT" F_res 2000 --world-detail on --world-detail-texture materials/brick/brickwall001.vtf || exit 1

REP="$OUT/readings.txt"
{
  echo "=== T45 detail-prefix readings $(date '+%Y-%m-%dT%H:%M:%S') ==="
  echo "HEAD=$(git rev-parse HEAD)"
  echo "source_before=$(sha "$SAVE") source_after=$(sha "$SRC")"
  echo "frames:"
  for f in A_ref F_ref A_mut F_mut A_res F_res; do echo "  $f $(sha "$OUT/$f.bmp")"; done
  echo "restored A reproduces pin: $( [ "$(sha "$OUT/A_res.bmp")" = "$PIN_A" ] && echo YES || echo NO )"
  echo "restored F reproduces pin: $( [ "$(sha "$OUT/F_res.bmp")" = "$PIN_F" ] && echo YES || echo NO )"
  echo "-- A_mut vs kept pre-fix A --"
  echo "A_mut_vs_old $(run_diff "$OUT/A_mut.bmp" "$PREFIX_A")"
  echo "A_mut_sha $(sha "$OUT/A_mut.bmp") old_pin $OLD_A"
  echo "-- F_mut vs kept pre-fix F --"
  echo "F_mut_vs_old $(run_diff "$OUT/F_mut.bmp" "$PREFIX_F")"
  echo "F_mut_vs_currentF $(run_diff "$OUT/F_mut.bmp" "$OUT/F_ref.bmp")"
  echo "F_mut_vs_old_sign $(run_sign "$PREFIX_F" "$OUT/F_mut.bmp" | tr '\n' ' ')"
  echo "-- mutation must move the frame away from the current pin --"
  echo "A_mut_vs_A_ref $(run_diff "$OUT/A_ref.bmp" "$OUT/A_mut.bmp")"
  echo "F_mut_vs_F_ref $(run_diff "$OUT/F_ref.bmp" "$OUT/F_mut.bmp")"
} | tee "$REP"
echo "=== DONE $(date '+%Y-%m-%dT%H:%M:%S') ==="
