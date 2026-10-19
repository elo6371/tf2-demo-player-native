#!/usr/bin/env bash
# world-geometry-hole-check.sh -- T26: is an empty pixel a hole or the sky?
#
# WHAT IT ASSERTS
#   1. The picture did not move. `textured` and `ref` are byte-identical to the
#      frames the same commit produces without the T26 instrumentation. The
#      instrument adds a buffer and a trace; it must not add a surface.
#   2. The missing population reproduces the account: 38,328 pixels, every one
#      covered by a BSP sky face, none without one.
#   3. The enclosed components are sky-backed too: the 2,313 pixels that looked
#      like closed holes are the map's sky seen through brush seams, not absent
#      geometry.
#   4. The point oracle and the rasterizer agree. Representative points of every
#      component >= 200 px are projected by the product itself, the nearest
#      world triangle and the nearest sky face are named, and the face-id frame
#      must decode to the same sky face the projection named.
#   5. The winding repair is refused by measurement. Flipping the brush/side-1
#      cell would fill back-facing pixels at this POV, and the first-hit oracle
#      says it would hide two thirds of the eye's first hits. The numbers are
#      pinned so a later "fix" has to argue with them.
#
# WHAT IT DOES NOT CLAIM
#   `drawn` is not `correct`. A pixel with a triangle is not a pixel with the
#   right material, and the 28% dark population T23 measured is drawn geometry
#   with a dark lightmap, not a hole. T26 changes no geometry rule; it measures
#   which pixels have no geometry and what the map says about them.
#
# Usage: bash world-geometry-hole-check.sh [--mutation] [--reuse]
#   --mutation  run the forward gate, then prove each of five mutations turns it
#               red, restoring every source byte-for-byte.
#   --reuse     keep the frames already captured under evidence/ (the capture
#               script resumes). Without it the matrix is captured again.
# Exit:  0 = every assertion held (or, with --mutation, every mutation went red
#            and every source came back identical);
#        1 = an assertion failed, or a mutation did not turn the gate red;
#        2 = a required tool or input is missing.
set -uo pipefail
cd "$(dirname "$0")"

MODE="${1:-}"
OUT=evidence/t26-world-holes
CAP="$OUT/captures"
ANA="$OUT/analysis"
SCRATCH=".scratch/t26"
PYTHON="${PYTHON:-python}"
PRODUCT=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/world_geometry_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
TICK=2000
mkdir -p "$SCRATCH" "$ANA"

# ---- pins ----------------------------------------------------------------
# Measured on 36d68b0 with the T26 instrumentation built from the same commit.
# A pin that moves without a source change means the instrument changed the
# picture; a pin that moves with one must be explained in the delivery report,
# never copied from an older tree.
#
# T23 published 38,328 missing / 36,015 open / 2,313 enclosed / 44 components
# from 9591499, and this tree reproduces all four exactly -- once every frame in
# the matrix belongs to the SAME tick. The first run of this matrix did not:
# five frames were captured at tick 2001/2002 because the arm fires on the first
# draw at or after the requested tick, and the account read 38,143 missing /
# 2,310 enclosed. That 185-pixel difference was the camera, not the code, and it
# is why capture-matrix frames carry their tick and this gate refuses a frame
# from the wrong one.
PIN_MISSING=38328
PIN_SKY_MISSING=38328
PIN_UNEXPLAINED=0
PIN_ENCLOSED=2313
PIN_ENCLOSED_MIN_COMPONENT=200
PIN_ENCLOSED_COMPONENTS=3
PIN_SKY_FACES=731
PIN_SKY_TRIANGLES=1842
PIN_WORLD_TRIANGLES=99312
PIN_BACKFACE=167030
PIN_TEXTURED_SHA=5e24802a8ad29c54daba59f66e42498fd6ec7ede2a4f8782d00d33eb76b9fcba
PIN_REF_SHA=f87f396a60dc72082f232bcd71547066c02c433e35ccda1576c3b21333ef9608

fail=0
assert_in() { # assert_in <file> <fixed string> <label>
  if grep -Fq -- "$2" "$1"; then
    echo "  PASS $3"
  else
    echo "  FAIL $3: '$2' is not in $1"
    fail=1
  fi
}
assert_eq() { # assert_eq <actual> <expected> <label>
  if [ "$1" = "$2" ]; then
    echo "  PASS $3 ($1)"
  else
    echo "  FAIL $3: $1 != $2"
    fail=1
  fi
}
field() { # field <text> <key> -> value after key=
  sed -n "s/.*[[:space:]]$2=\\([^[:space:]]*\\).*/\\1/p" <<<"$1" | head -1
}
# Which tick a captured frame belongs to; see world-geometry-hole-capture.sh.
# The gate re-runs this rather than trusting the capture script, because a
# mutation's focused frame is compared against numbers measured at TICK.
frame_tick() { # frame_tick <trace>
  local trace="$1" tick
  tick=$(grep -a "capture: armed " "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed -n 's/.*frame_tick=\([-0-9]*\).*/\1/p')
  if [ -z "$tick" ]; then
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' \
      | sed -n 's/.*tick=\([0-9]*\).*/\1/p' | awk -v want="$TICK" '$1>=want{print; exit}')
  fi
  printf '%s' "$tick"
}
# A capture whose frame is not at TICK is a different camera, and comparing it
# against a tick-TICK account measures the camera, not the code. Retry rather
# than accept: the arm fires on the first draw at or after the tick, so a run
# that skips a tick captures late.
run_capture_trace() { # run_capture_trace <bmp> <trace> [extra flags...]
  local bmp="$1" trace="$2"; shift 2
  local attempt
  for attempt in 1 2 3 4; do
    rm -f "$trace" "$bmp"
    TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 180 "$PRODUCT" \
      --tf-root "$TF" --demo "$DEMO" --capture-tick "$TICK" \
      --capture-frame "$(cygpath -m "$PWD/$bmp")" \
      --audio-disabled --hidden-window --world-water off "$@" > "$SCRATCH/run.log" 2>&1
    if [ $? -eq 0 ] && [ -s "$bmp" ] && [ "$(frame_tick "$trace")" = "$TICK" ]; then
      return 0
    fi
    echo "  retry: the capture landed on tick $(frame_tick "$trace"), not $TICK"
  done
  return 1
}

echo "== 0. source and tool identity =="
HEAD_SHA=$(git rev-parse --short=12 HEAD)
echo "  HEAD=$HEAD_SHA branch=$(git branch --show-current)"
if [ -n "$(git status --porcelain native/ 2>/dev/null)" ]; then
  echo "  FAIL native/ has uncommitted changes; a gate must describe a committed tree"
  git status --short native/ | sed 's/^/       /'
  fail=1
else
  echo "  PASS native/ is clean"
fi
for tool in "$PYTHON" bash; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  FAIL $tool is missing"; exit 2; }
done

echo "== 1. build =="
bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/build.txt" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL build"
  tail -20 "$SCRATCH/build.txt" | sed 's/^/       /'
  exit 1
fi
echo "  PASS build"
{
  echo "HEAD $HEAD_SHA"
  echo "tf2_demo_native.exe $(sha256sum "$PRODUCT" | cut -d' ' -f1)"
  echo "world_geometry_probe.exe $(sha256sum "$PROBE" | cut -d' ' -f1)"
  echo "native/src/bsp_map.cpp $(sha256sum native/src/bsp_map.cpp | cut -d' ' -f1)"
  echo "native/src/main.cpp $(sha256sum native/src/main.cpp | cut -d' ' -f1)"
  echo "native/src/native_renderer.cpp $(sha256sum native/src/native_renderer.cpp | cut -d' ' -f1)"
} | tee "$ANA/identity.txt"

echo "== 2. capture matrix =="
bash world-geometry-hole-capture.sh "$CAP" > "$SCRATCH/capture.txt" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL capture"
  tail -20 "$SCRATCH/capture.txt" | sed 's/^/       /'
  exit 1
fi
tail -1 "$SCRATCH/capture.txt" | sed 's/^/  /'
for frame in "$CAP"/*.bmp; do
  printf '%s  %s\n' "$(sha256sum "$frame" | cut -d' ' -f1)" "$(basename "$frame")"
done | sort -k2 > "$ANA/hashes.txt"
TEXTURED_SHA=$(sha256sum "$CAP/textured.bmp" | cut -d' ' -f1)
REF_SHA=$(sha256sum "$CAP/ref.bmp" | cut -d' ' -f1)

echo "== 3. the product picture did not move =="
assert_eq "$TEXTURED_SHA" "$PIN_TEXTURED_SHA" "stored textured frame identity"
assert_eq "$REF_SHA" "$PIN_REF_SHA" "stored ref frame identity"
# The stored frames were written by an earlier build of this worktree. Re-capture
# with the binary THIS run built: the instrument adds a trace line and a vertex
# colour the product path never fills, and the only way to say that neither
# moved a surface is a byte comparison of the same view from the new binary.
if run_capture_trace "$SCRATCH/verify-textured.bmp" "$SCRATCH/verify-textured.trace"; then
  if cmp -s "$SCRATCH/verify-textured.bmp" "$CAP/textured.bmp"; then
    echo "  PASS the binary this run built reproduces the pinned textured frame byte for byte"
  else
    echo "  FAIL the binary this run built draws a different textured frame"
    echo "       fresh $(sha256sum "$SCRATCH/verify-textured.bmp" | cut -d' ' -f1)"
    fail=1
  fi
else
  echo "  FAIL the verification capture did not land on tick $TICK"
  fail=1
fi

echo "== 4. probe: the sky as geometry =="
"$PROBE" --tf-root "$TF" --map maps/pl_odyssey.bsp > "$ANA/probe.txt" 2>&1
PROBE_RC=$?
if [ "$PROBE_RC" -ne 0 ]; then
  echo "  FAIL probe rc=$PROBE_RC"
  fail=1
fi
assert_in "$ANA/probe.txt" "geometry sky faces=$PIN_SKY_FACES triangles=$PIN_SKY_TRIANGLES bufferTriangles=$PIN_WORLD_TRIANGLES" "sky face census"
assert_in "$ANA/probe.txt" "geometry faces total=18052 rendered=17321 sky=731 skippedNoDraw=0 skippedInvalid=0" "face account unchanged"

echo "== 5. first-hit oracle: the winding candidate is refused =="
"$PROBE" --tf-root "$TF" --map maps/pl_odyssey.bsp \
  --ray-down 96 --ray-eye "-2423.6,5566.0,-1280.0" 24 > "$ANA/probe-oracle.txt" 2>&1
ORACLE_RC=$?
if [ "$ORACLE_RC" -ne 0 ]; then
  echo "  FAIL oracle rc=$ORACLE_RC"
  fail=1
fi
SHIPPED_EYE=$(sed -n 's/.*rule-table mask=07 .* eye=\([0-9.]*\).*/\1/p' "$ANA/probe-oracle.txt" | head -1)
FLIP_EYE=$(sed -n 's/.*rule-table mask=05 .* eye=\([0-9.]*\).*/\1/p' "$ANA/probe-oracle.txt" | head -1)
TOP_EYE=$(sed -n 's/.*rule-table mask=13 .* eye=\([0-9.]*\).*/\1/p' "$ANA/probe-oracle.txt" | head -1)
echo "  shipped mask07 eye=${SHIPPED_EYE:-none} flip mask05 eye=${FLIP_EYE:-none} top mask13 eye=${TOP_EYE:-none}"
if [ -z "$SHIPPED_EYE" ] || [ -z "$FLIP_EYE" ] || [ -z "$TOP_EYE" ]; then
  echo "  FAIL the rule table is missing"; fail=1
else
  "$PYTHON" - "$SHIPPED_EYE" "$FLIP_EYE" "$TOP_EYE" <<'PYEOF'
import sys
shipped, flip, top = (float(value) for value in sys.argv[1:4])
if shipped > flip + 0.2 and shipped > top + 0.2:
    print("  PASS the shipped rule keeps %.3f of the eye's first hits; the two cells a flip would win read %.3f and %.3f" % (shipped, flip, top))
else:
    print("  FAIL the shipped rule no longer dominates the flip candidates: %.3f vs %.3f / %.3f" % (shipped, flip, top))
    sys.exit(1)
PYEOF
  [ $? -ne 0 ] && fail=1
fi

echo "== 6. the empty-pixel account =="
"$PYTHON" world-geometry-hole-account.py --dir "$CAP" --out "$ANA" > "$ANA/hole-account.txt" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL accounting"
  cat "$ANA/hole-account.txt" | sed 's/^/       /'
  exit 1
fi
TOTALS=$(grep -F "HOLE totals " "$ANA/hole-account.txt")
SPLIT=$(grep -F "HOLE split " "$ANA/hole-account.txt")
echo "  $TOTALS"
echo "  $SPLIT"
assert_eq "$(field "$TOTALS" missing)" "$PIN_MISSING" "missing pixels"
assert_eq "$(field "$TOTALS" sky)" "$PIN_SKY_MISSING" "missing pixels covered by a sky face"
assert_eq "$(field "$TOTALS" unexplained)" "$PIN_UNEXPLAINED" "missing pixels with no sky face"
assert_eq "$(field "$SPLIT" enclosed)" "$PIN_ENCLOSED" "enclosed missing pixels"
assert_eq "$(field "$SPLIT" enclosedUnexplained)" "0" "enclosed pixels with no sky face"
# The mask's reach, printed before its verdict is quoted. On a sealed map the
# sky hull surrounds the play space, so the mask covers the frame; that is a
# property of the map and it is also the reason coverage alone cannot tell a
# brush seam from the open sky above the map. Both cases are sky-backed, so the
# number is asserted as a FINDING (`degenerate=1`) and the two are separated by
# the shape test below and by the bounding faces in section 8.
MASK_REACH=$(grep -F "HOLE maskReach " "$ANA/hole-account.txt")
echo "  $MASK_REACH"
assert_eq "$(field "$MASK_REACH" degenerate)" "1" "the sky hull is closed from this camera (the mask cannot discriminate)"
# The depth control cannot answer the behind/in-front question on this build:
# the debug depth view is 8-bit and saturates for everything past a few units.
# It is reported, not relied on -- `saturated=1` is the measured reason.
DEPTH_CONTROL=$(grep -F "HOLE depthControl " "$ANA/hole-account.txt")
echo "  $DEPTH_CONTROL"
assert_eq "$(field "$DEPTH_CONTROL" measured)" "1" "the depth control ran"
assert_eq "$(field "$DEPTH_CONTROL" saturated)" "1" "the depth view is saturated, so it is not used as evidence"

# The enclosed population, per component. Two things are asserted for each
# component of consequence: it is sky-backed, and it is needle-shaped. A missing
# FACE would be a compact blob; a seam between two surfaces is a sliver with a
# low fill ratio, and the distinction is what keeps this account from calling a
# crack a lost face.
enclosed_components=0
while IFS= read -r line; do
  area=$(field "$line" area)
  unexplained=$(field "$line" unexplained)
  edge=$(field "$line" edge)
  fill=$(field "$line" fill)
  bbox=$(sed -n 's/.*bbox=(x=\([0-9]*\)\.\.\([0-9]*\),y=\([0-9]*\)\.\.\([0-9]*\)).*/\1 \2 \3 \4/p' <<<"$line")
  [ "${area:-0}" -ge "$PIN_ENCLOSED_MIN_COMPONENT" ] || continue
  if [ "${edge:-1}" = "1" ]; then continue; fi
  enclosed_components=$((enclosed_components + 1))
  if [ "${unexplained:-1}" != "0" ]; then
    echo "  FAIL component ${line#HOLE component }"
    fail=1
  fi
  read -r x0 x1 y0 y1 <<<"$bbox"
  width=$((x1 - x0 + 1)); height=$((y1 - y0 + 1))
  thin=$((width < height ? width : height)); long=$((width < height ? height : width))
  if [ "$thin" -gt 32 ] || [ "$long" -lt 40 ]; then
    echo "  FAIL component ${line#HOLE component } is not a sliver (${width}x${height})"
    fail=1
  fi
  if [ -n "$fill" ] && "$PYTHON" -c "import sys; sys.exit(0 if float(sys.argv[1]) < 0.70 else 1)" "$fill"; then
    :
  else
    echo "  FAIL component ${line#HOLE component } fills its box (fill=$fill): a solid blob is a missing face, not a seam"
    fail=1
  fi
done < <(grep -F "HOLE component " "$ANA/hole-account.txt")
assert_eq "$enclosed_components" "$PIN_ENCLOSED_COMPONENTS" "enclosed components >= 200 px, each a sliver"
[ "$fail" = 0 ] && echo "  PASS every enclosed component is a thin sky-backed sliver"

echo "== 6b. the T23 account still reads the same =="
# The point of re-running T23's own tool on T26's frames is that the two tasks
# agree about the same pixels through two different programs. If T26's overlay
# rule had drifted from T23's, the `missing` totals would already differ here.
# --out keeps the tool's mask inside T26's own analysis directory: the tool
# defaults to evidence/t23-world-geometry/analysis, and writing there would edit
# another task's evidence from this one's gate (it did, and it was reverted).
"$PYTHON" world-geometry-accounting.py --dir "$CAP" --out "$ANA" \
  > "$ANA/t23-account.txt" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL the T23 account refused the frame"
  tail -5 "$ANA/t23-account.txt" | sed 's/^/       /'
  fail=1
else
  T23=$(grep -F "ACCOUNTING totals " "$ANA/t23-account.txt")
  echo "  $T23"
  assert_eq "$(field "$T23" missing)" "$PIN_MISSING" "T23 and T26 agree on the missing population"
  assert_eq "$(field "$T23" backface)" "$PIN_BACKFACE" "back-face population (T11 winding unchanged)"
  assert_eq "$(field "$T23" visibility)" "0" "visibility population (T17 clip unchanged)"
fi

echo "== 7. the point oracle and the rasterizer =="
POINTS=$(grep -F "HOLE points=" "$ANA/hole-account.txt" | sed 's/^HOLE points=//')
if [ -z "$POINTS" ]; then
  echo "  FAIL the account named no representative points"; fail=1
else
  PT_TRACE="$SCRATCH/points.trace"
  if ! run_capture_trace "$SCRATCH/points.bmp" "$PT_TRACE" --world-hole-trace "$POINTS"; then
    echo "  FAIL the point trace run failed, or captured a tick other than $TICK"
    tail -10 "$SCRATCH/run.log" | sed 's/^/       /'
    fail=1
  else
    grep -a "gpu: world hole-trace" "$PT_TRACE" > "$ANA/point-trace.txt" || true
    SUMMARY=$(grep -a "gpu: world hole-trace-summary" "$PT_TRACE" | tail -1)
    echo "  ${SUMMARY:-no summary}"
    EMPTY_SKY=$(field "$SUMMARY" emptyWithSky)
    EMPTY_NONE=$(field "$SUMMARY" emptyWithoutSky)
    POINT_COUNT=$(grep -c "gpu: world hole-trace point=" "$ANA/point-trace.txt")
    assert_eq "$EMPTY_NONE" "0" "points with no sky face"
    if [ "${EMPTY_SKY:-0}" -ge 6 ] && [ "${EMPTY_SKY:-0}" -le "$POINT_COUNT" ]; then
      echo "  PASS $EMPTY_SKY of $POINT_COUNT traced points are empty with a sky face behind them"
    else
      echo "  FAIL emptyWithSky=$EMPTY_SKY of $POINT_COUNT points"
      fail=1
    fi
    # No --depth here on purpose: the 8-bit depth view saturates on this camera
    # (see section 6), so the tool's far-plane test is the depth statement it
    # makes, and the sky distance is checked by the oracle's own geometry.
    "$PYTHON" hole-point-verify.py --trace "$ANA/point-trace.txt" \
      --ids "$CAP/sky-ids.bmp" > "$ANA/point-verify.txt" 2>&1
    if [ $? -ne 0 ]; then
      echo "  FAIL the rasterizer disagreed with the point oracle"
      grep -F "MISMATCH" "$ANA/point-verify.txt" | head -5 | sed 's/^/       /'
      fail=1
    else
      echo "  PASS $(tail -1 "$ANA/point-verify.txt")"
    fi
    if cmp -s "$SCRATCH/points.bmp" "$CAP/textured.bmp"; then
      echo "  PASS the traced frame is the pinned textured frame (the trace changed no pixel)"
    else
      echo "  FAIL the traced frame differs from the textured frame"
      fail=1
    fi
  fi
fi

echo "== 8. what the empty pixels sit between =="
# The shape test says the enclosed population is slivers; it cannot say between
# what. Two instruments answer that from the map's own tables:
#   - the world face-id frame names the face on the other side of each sliver
#     (four directions, first drawn pixel within 64 px), and
#   - `world_geometry_probe --face-info` turns an index into a material, a plane
#     and a displacement flag.
# Together they turn "there is a crack" into "this crack runs between face A and
# face B, and B is a tessellated displacement cell".
SEAM_CULLED=$(grep -F "HOLE seamCulled " "$ANA/hole-account.txt" | sed -n 's/.*directions=\([0-9]*\).*/\1/p')
echo "  seam directions that only the cull-off frame could name: ${SEAM_CULLED:-0}"
# The sliver itself is missing even with the cull off -- it is in the missing
# mask, which is taken from the all-off frame. So a culled neighbour explains
# the SHAPE of the boundary, not the emptiness. Both facts are asserted because
# together they are the difference between "the cull hides a face" (T11's
# question) and "the surfaces do not meet" (this one).
if [ "${SEAM_CULLED:-0}" -ge 1 ]; then
  echo "  PASS at least one sliver is bounded by a surface the product culls"
else
  echo "  FAIL no sliver had a culled neighbour: world-ids-nocull.bmp is not being read"
  fail=1
fi
SEAM_LINE=$(grep -F "HOLE seamFaces " "$ANA/hole-account.txt" || true)
SEAM_IDS=$(sed -n 's/.*[[:space:]]ids=//p' <<<"$SEAM_LINE")
SKY_FACE_IDS=$(grep -o "sky=face=[0-9]*" "$ANA/point-trace.txt" 2>/dev/null \
  | sed 's/.*face=//' | sort -un | tr '\n' ',' | sed 's/,$//')
echo "  seam faces: ${SEAM_IDS:-none}"
echo "  sky faces at the traced points: ${SKY_FACE_IDS:-none}"
if [ -z "$SEAM_IDS" ]; then
  echo "  FAIL no face bracketed an enclosed component (is world-ids.bmp missing?)"
  fail=1
else
  FACE_LIST="$SEAM_IDS"
  [ -n "$SKY_FACE_IDS" ] && FACE_LIST="$FACE_LIST,$SKY_FACE_IDS"
  "$PROBE" --tf-root "$TF" --map maps/pl_odyssey.bsp --face-info "$FACE_LIST" \
    > "$ANA/face-info.txt" 2>&1
  if [ $? -ne 0 ]; then
    echo "  FAIL the probe refused --face-info"
    tail -5 "$ANA/face-info.txt" | sed 's/^/       /'
    fail=1
  else
    REQUESTED=$(tr ',' '\n' <<<"$FACE_LIST" | grep -c .)
    RESOLVED=$(grep -c "geometry face-info index=" "$ANA/face-info.txt")
    ABSENT=$(grep -c "state=absent" "$ANA/face-info.txt")
    DISPLACED=$(grep "geometry face-info index=" "$ANA/face-info.txt" | grep -c "displacement=1" || true)
    SKY_KIND=$(grep "geometry face-info index=" "$ANA/face-info.txt" | grep -c "kind=sky" || true)
    assert_eq "$RESOLVED" "$REQUESTED" "every named face exists in the map's face table"
    assert_eq "$ABSENT" "0" "no face index was invented by a decode error"
    echo "  $DISPLACED of them are displacement faces, $SKY_KIND are sky faces"
    if [ "${DISPLACED:-0}" -ge 1 ]; then
      echo "  PASS at least one side of a sliver is a tessellated displacement cell"
    else
      echo "  FAIL no displacement face bounds a sliver: the seam account lost its subject"
      fail=1
    fi
    if [ "${SKY_KIND:-0}" -ge 1 ]; then
      echo "  PASS at least one traced empty pixel is backed by a sky face of the map"
    else
      echo "  FAIL no sky face was named at the traced points"
      fail=1
    fi
    grep "geometry face-info index=" "$ANA/face-info.txt" | head -12 | sed 's/^/       /'
  fi
fi

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then
    echo "PASS: every empty pixel in the reference frame is covered by the map's own sky brush; there is no missing geometry to fix, and the one winding repair the oracle can see is a measured regression."
    exit 0
  fi
  echo "FAIL: $fail assertion groups did not hold."
  exit 1
fi

# ---------------------------------------------------------------------------
# mutation mode
# ---------------------------------------------------------------------------
# Each mutation removes one rule the forward verdict stands on and the gate must
# go red for it. "Red" is not eyeballed: the focused run re-executes the SAME
# equality assertions the forward run made -- the pinned missing/sky/unexplained/
# enclosed numbers for the mask mutations, the per-point face decode for the
# face-id mutation -- and the mutation is caught only if one of them disagrees.
echo "== mutation mode =="
mfail=0
SRC_BSP=native/src/bsp_map.cpp
SRC_REN=native/src/native_renderer.cpp
SRC_MAIN=native/src/main.cpp
TOOL_ACCOUNT=world-geometry-hole-account.py
SAVE_BSP="$SCRATCH/mutation.bsp_map.cpp"
SAVE_REN="$SCRATCH/mutation.native_renderer.cpp"
SAVE_MAIN="$SCRATCH/mutation.main.cpp"
SAVE_ACCOUNT="$SCRATCH/mutation.world-geometry-hole-account.py"
cp "$SRC_BSP" "$SAVE_BSP"
cp "$SRC_REN" "$SAVE_REN"
cp "$SRC_MAIN" "$SAVE_MAIN"
cp "$TOOL_ACCOUNT" "$SAVE_ACCOUNT"
RESTORED=0
MUTATION_ACTIVE=0
restore_sources() {
  cp "$SAVE_BSP" "$SRC_BSP"
  cp "$SAVE_REN" "$SRC_REN"
  cp "$SAVE_MAIN" "$SRC_MAIN"
  cp "$SAVE_ACCOUNT" "$TOOL_ACCOUNT"
  MUTATION_ACTIVE=0
  if cmp -s "$SAVE_BSP" "$SRC_BSP" && cmp -s "$SAVE_REN" "$SRC_REN" \
      && cmp -s "$SAVE_MAIN" "$SRC_MAIN" && cmp -s "$SAVE_ACCOUNT" "$TOOL_ACCOUNT"; then
    echo "  RESTORE=OK (four files byte-identical)"
  else
    echo "  RESTORE=FAIL"; mfail=1
  fi
}
# A mutation left in the tree after an interrupt would be worse than a red gate.
# The binary is rebuilt at the end, so an interrupt can leave a mutant product
# behind; the restore of the source is what stops it from being committed.
trap 'if [ "$MUTATION_ACTIVE" = 1 ]; then echo "  aborted mid-mutation: restoring sources" >&2; restore_sources; fi' EXIT

build_mutated() { # build_mutated <label>
  if ! bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/build-$1.txt" 2>&1; then
    echo "  FAIL $1 does not build"
    tail -6 "$SCRATCH/build-$1.txt" | sed 's/^/       /'
    mfail=1
    return 1
  fi
  return 0
}

# The frames the focused accounting needs that the mutation cannot change: the
# world pass is untouched by every mutation below except M5, and M5 mutates the
# account tool, not the picture.
stock_frames() { # stock_frames <dir>
  local dir="$1"
  rm -rf "$dir"; mkdir -p "$dir"
  local frame
  for frame in flat-base flat-novis flat-nocull flat-novis-nocull none textured; do
    cp "$CAP/$frame.bmp" "$dir/" || { echo "  FAIL $frame is missing from $CAP"; mfail=1; return 1; }
  done
}

capture_sky_mask() { # capture_sky_mask <dir> [extra flags...]
  local dir="$1"; shift
  run_capture_trace "$dir/sky-faces.bmp" "$SCRATCH/focused.trace" \
    --world-geometry sky --no-world-cull --world-flat "$@"
}

# Returns 0 when at least one pinned number moved -- i.e. when the forward gate
# would have failed. Prints which ones, so a mutation that only half-breaks the
# account is still visible in the log.
judge_account() { # judge_account <label> <dir>
  local label="$1" dir="$2" report="$SCRATCH/account-$label.txt"
  if ! "$PYTHON" world-geometry-hole-account.py --dir "$dir" --out "$SCRATCH/mask-$label" \
      > "$report" 2>&1; then
    echo "  PASS $label: the account refuses the frame -- $(head -1 "$report")"
    return 0
  fi
  local totals split broke=""
  totals=$(grep -F "HOLE totals " "$report")
  split=$(grep -F "HOLE split " "$report")
  [ "$(field "$totals" missing)" = "$PIN_MISSING" ] \
    || broke="$broke missing=$(field "$totals" missing)"
  [ "$(field "$totals" sky)" = "$PIN_SKY_MISSING" ] \
    || broke="$broke sky=$(field "$totals" sky)"
  [ "$(field "$totals" unexplained)" = "$PIN_UNEXPLAINED" ] \
    || broke="$broke unexplained=$(field "$totals" unexplained)"
  [ "$(field "$split" enclosed)" = "$PIN_ENCLOSED" ] \
    || broke="$broke enclosed=$(field "$split" enclosed)"
  [ "$(field "$split" enclosedUnexplained)" = "0" ] \
    || broke="$broke enclosedUnexplained=$(field "$split" enclosedUnexplained)"
  if [ -n "$broke" ]; then
    echo "  PASS $label: the gate goes red:$broke"
    return 0
  fi
  echo "  FAIL $label: every pinned number survived -- the mutation is not covered"
  mfail=1
  return 1
}

# M1: the parser still counts the sky faces and no longer emits their polygons.
# The census line the probe prints stays at 731; the mask can only be empty.
echo "-- M1: the parser keeps the sky count and drops the polygons --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_BSP" <<'PATCH_M1'
        triangle.faceIndex = faceIndex;
        map.skyTriangles.push_back(std::move(triangle));
%%
        triangle.faceIndex = faceIndex;
        (void)triangle;  // T26 mutation M1: the census stays, the polygon is gone.
PATCH_M1
if [ $? -ne 0 ]; then
  echo "  FAIL M1 could not patch the parser"; mfail=1
elif build_mutated M1 && stock_frames "$SCRATCH/m1" && capture_sky_mask "$SCRATCH/m1"; then
  judge_account M1 "$SCRATCH/m1"
fi
restore_sources

# M2: the sky buffer is filled from the world triangles. The mask is then the
# world's own coverage, so it covers every drawn pixel and none of the empty
# ones -- the exact answer the task had to rule out before it could blame
# geometry.
echo "-- M2: the sky pass uploads the world triangles --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_REN" <<'PATCH_M2'
  const std::vector<BspTriangle>& source = worldSkyFaceOnly_ ? map.skyTriangles : map.triangles;
%%
  const std::vector<BspTriangle>& source = map.triangles;  // T26 mutation M2
PATCH_M2
if [ $? -ne 0 ]; then
  echo "  FAIL M2 could not patch the uploader"; mfail=1
elif build_mutated M2 && stock_frames "$SCRATCH/m2" && capture_sky_mask "$SCRATCH/m2"; then
  judge_account M2 "$SCRATCH/m2"
fi
restore_sources

# M3: the face-id view returns a constant. The point oracle's answer is
# unchanged (it is CPU), so this isolates the second instrument: an id frame
# that cannot name a face must be caught by the decode, not by the accounting.
echo "-- M3: the face-id view returns a constant --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_REN" <<'PATCH_M3'
    if (envFeatures.x > 5.5) return float4(colour.rgb, 1.0);
%%
    if (envFeatures.x > 5.5) return float4(0.25, 0.25, 0.25, 1.0);  // T26 mutation M3
PATCH_M3
if [ $? -ne 0 ]; then
  echo "  FAIL M3 could not patch the shader"; mfail=1
elif build_mutated M3; then
  mkdir -p "$SCRATCH/m3"
  if ! run_capture_trace "$SCRATCH/m3/sky-ids.bmp" "$SCRATCH/focused.trace" \
      --world-geometry sky --no-world-cull --world-face-id; then
    echo "  FAIL M3 capture"; mfail=1
  elif "$PYTHON" hole-point-verify.py --trace "$ANA/point-trace.txt" \
      --ids "$SCRATCH/m3/sky-ids.bmp" > "$SCRATCH/verify-M3.txt" 2>&1; then
    echo "  FAIL M3: the decoded ids still agreed with the point oracle"
    mfail=1
  else
    echo "  PASS M3: $(tail -1 "$SCRATCH/verify-M3.txt")"
  fi
fi
restore_sources

# M4: the sky buffer is drawn through its own bounds instead of the world's.
# Every sky pixel is still a sky pixel, but at the wrong scale and offset. The
# coverage account cannot see this -- the hull covers every direction, so a
# mask that is merely "somewhere near" still covers the frame, and that limit is
# stated in the delivery report. What can see it is the second instrument: the
# rasterizer's face id at a traced pixel has to be the face the CPU oracle
# projected with the frame's own matrix, and a moved buffer puts a different
# face (or none) there.
echo "-- M4: the sky mask gets its own transform --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_REN" <<'PATCH_M4'
  for (const auto& triangle : map.triangles) {
    for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
      minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
%%
  for (const auto& triangle : source) {  // T26 mutation M4: the mask's own bounds
    for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
      minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
PATCH_M4
if [ $? -ne 0 ]; then
  echo "  FAIL M4 could not patch the bounds"; mfail=1
elif build_mutated M4; then
  mkdir -p "$SCRATCH/m4"
  if ! run_capture_trace "$SCRATCH/m4/sky-ids.bmp" "$SCRATCH/focused.trace" \
      --world-geometry sky --no-world-cull --world-face-id; then
    echo "  FAIL M4 capture"; mfail=1
  elif "$PYTHON" hole-point-verify.py --trace "$ANA/point-trace.txt" \
      --ids "$SCRATCH/m4/sky-ids.bmp" > "$SCRATCH/verify-M4.txt" 2>&1; then
    echo "  FAIL M4: the ids still matched after the buffer moved"
    mfail=1
  else
    echo "  PASS M4: $(tail -1 "$SCRATCH/verify-M4.txt")"
  fi
fi
restore_sources

# M5: the accounting drops the overlay exclusion -- the one definition this task
# shares with T23. The frames are the forward frames, unmutated; only the rule
# that decides what "missing" means changes, and the pinned total must move.
echo "-- M5: the account counts the overlay as missing --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$TOOL_ACCOUNT" <<'PATCH_M5'
    missing = bytearray(1 if (not present_all_off[index]) and not overlay[index] else 0
                        for index in range(total))
%%
    missing = bytearray(1 if not present_all_off[index] else 0
                        for index in range(total))  # T26 mutation M5
PATCH_M5
if [ $? -ne 0 ]; then
  echo "  FAIL M5 could not patch the account"; mfail=1
else
  judge_account M5 "$CAP"
fi
restore_sources

# The last mutation's build is a mutant binary. Rebuilding from the restored
# sources and re-capturing the pinned frame is the only statement that matters:
# byte-identical sources have to produce a byte-identical picture, or the gate
# has been judging a product that is not the one the tree describes.
echo "-- after restore: the pinned frame comes back --"
if build_mutated RESTORE; then
  RESTORED_CAPTURE="$SCRATCH/restored-textured.bmp"
  rm -f "$RESTORED_CAPTURE"
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$SCRATCH/restored.trace")" timeout 180 "$PRODUCT" \
    --tf-root "$TF" --demo "$DEMO" --capture-tick "$TICK" \
    --capture-frame "$(cygpath -m "$PWD/$RESTORED_CAPTURE")" \
    --audio-disabled --hidden-window --world-water off > "$SCRATCH/restored.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "  FAIL the restored build could not capture"; mfail=1
  elif cmp -s "$RESTORED_CAPTURE" "$CAP/textured.bmp"; then
    echo "  PASS the restored build reproduces the pinned textured frame byte for byte"
  else
    echo "  FAIL the restored build draws a different frame than the pin"
    echo "       restored $(sha256sum "$RESTORED_CAPTURE" | cut -d' ' -f1)"
    echo "       pinned   $(sha256sum "$CAP/textured.bmp" | cut -d' ' -f1)"
    mfail=1
  fi
fi

echo "== mutation verdict =="
if [ "$mfail" = 0 ]; then
  echo "PASS: all five mutations turned the gate red and every source came back byte-identical."
  exit 0
fi
echo "FAIL: a mutation was not caught, or a source was not restored. The tree may be dirty -- check 'git status native/' before continuing."
exit 1
