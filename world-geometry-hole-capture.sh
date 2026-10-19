#!/usr/bin/env bash
# world-geometry-hole-capture.sh -- T26: the same-view frames that separate sky
# from a hole.
#
# WHY THIS FILE EXISTS
# --------------------
# T23's matrix answers "is there a world triangle along this pixel" and "which
# switch removed it". It cannot answer "was this pixel meant to be sky", because
# the sky box draws behind every pixel the world pass leaves empty: the finished
# frame is sky-coloured whether the map has a sky brush there or nothing at all.
#
# The extra frames here are the map's own answer:
#
#   sky-faces  --world-geometry sky --world-flat  the BSP sky-brush polygons in
#                                                 the world pass's flat colour,
#                                                 through the world transform --
#                                                 a coverage mask of the map's
#                                                 sky brushes.
#   sky-depth  --world-geometry sky --world-depth how far away that sky brush is
#   sky-ids    --world-geometry sky --world-face-id  which sky face it is, as
#                                                 the rasterizer's own answer (the
#                                                 CPU point trace is the other).
#
# The cull is off in all three: a sky brush is authored as the inside of a box,
# so half of its faces point away from the camera and the mask must not depend
# on a winding rule that does not apply to it. The world's own frames are the
# T23 matrix, unchanged, so the two runs are the same picture at the same tick.
#
# Usage: bash world-geometry-hole-capture.sh [output-dir]
# Exit:  0 = every capture wrote a frame; 1 = one did not.
set -uo pipefail
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
OUT="${1:-evidence/t26-world-holes/captures}"
TRACE_DIR="$OUT/traces"
TICK=2000
mkdir -p "$OUT" "$TRACE_DIR"

# Which tick a captured frame belongs to. The product records it at the moment
# the capture is armed. A trace written before that line existed falls back to
# the first draw at or after the requested tick, which is the rule the arming
# itself uses and therefore the same answer -- and it is how the five frames
# captured at 2001/2002 in the first run of this matrix were found.
frame_tick() {
  local trace="$1" tick
  tick=$(grep -a "capture: armed " "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed -n 's/.*frame_tick=\([-0-9]*\).*/\1/p')
  if [ -z "$tick" ]; then
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' \
      | sed -n 's/.*tick=\([0-9]*\).*/\1/p' | awk -v want="$TICK" '$1>=want{print; exit}')
  fi
  printf '%s' "$tick"
}

# The task plan serialises every product launch on this host. This worktree
# cannot enforce that for other tasks, so it waits for a quiet host: another
# task's product process may be mid-capture, and two renderers competing for the
# same GPU makes the frame a measurement of the contention rather than of the
# map. The wait is long (five minutes) because the other task's matrix is the
# thing being polite about; if it still has not gone quiet, the capture proceeds
# with --hidden-window and says so, and the frame hashes are what settle whether
# the pixels moved.
wait_for_idle_host() {
  local deadline=$((SECONDS + $1))
  local idle=0
  while [ "$SECONDS" -lt "$deadline" ]; do
    if ps -W 2>/dev/null | grep -qi "tf2_demo_native"; then
      idle=0
    else
      idle=$((idle + 1))
      if [ "$idle" -ge 1 ]; then return 0; fi
    fi
    sleep 5
  done
  return 1
}

capture() { # capture <name> [extra flags...]
  local name="$1"; shift
  local bmp="$OUT/$name.bmp"
  local trace="$TRACE_DIR/$name.trace"
  local attempt tick rc
  local window=--hidden-window
  [ "$name" = "ref" ] && window=
  if [ -s "$bmp" ] && [ -s "$trace" ] && grep -aq "capture_written=1" "$trace" \
      && [ "$(frame_tick "$trace")" = "$TICK" ]; then
    echo "  kept $name (already captured at tick $TICK)"
    return 0
  fi
  if ! wait_for_idle_host 300; then
    echo "  note: $name starts while another product process is running; --hidden-window is used" >&2
  fi
  for attempt in 1 2 3 4 5 6 7 8; do
    rm -f "$bmp" "$trace"
    TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 120 "$PRODUCT" \
      --tf-root "$TF" --demo "$DEMO" \
      --capture-tick "$TICK" --capture-frame "$(cygpath -m "$PWD/$bmp")" \
      --audio-disabled $window "$@" \
      > "$OUT/$name.log" 2>&1
    rc=$?
    tick=$(frame_tick "$trace")
    if [ "$rc" = 0 ] && [ -s "$bmp" ] && grep -aq "capture_written=1" "$trace" \
        && [ "$tick" = "$TICK" ]; then
      echo "  captured $name (attempt $attempt, tick $tick)"
      return 0
    fi
    echo "  capture $name attempt $attempt: rc=$rc tick=${tick:-none} capture=$([ -s "$bmp" ] && echo yes || echo no), retrying"
    rm -f "$bmp"
  done
  echo "  FAILED to capture $name at tick $TICK" >&2
  return 1
}

fail=0
# The frame the pinned gate judges, so the two matrices are comparable.
capture ref --viewmodel models/weapons/v_models/v_scattergun_scout.mdl \
  --viewmodel-render companion || fail=1
# The world-only accounting population, unchanged from T23.
WATER=(--world-water off)
capture textured "${WATER[@]}" || fail=1
capture none --world-geometry none "${WATER[@]}" || fail=1
capture terrain --world-geometry terrain "${WATER[@]}" || fail=1
capture lightmap-off --world-lightmap off "${WATER[@]}" || fail=1
for vis in on off; do
  for cull in on off; do
    suffix=""
    [ "$vis" = "off" ] && suffix="$suffix-novis"
    [ "$cull" = "off" ] && suffix="$suffix-nocull"
    [ -z "$suffix" ] && suffix="-base"
    flags=()
    [ "$vis" = "off" ] && flags+=(--no-world-vis-clip)
    [ "$cull" = "off" ] && flags+=(--no-world-cull)
    capture "flat$suffix" --world-flat "${WATER[@]}" "${flags[@]}" || fail=1
    capture "depth$suffix" --world-depth "${WATER[@]}" "${flags[@]}" || fail=1
  done
done
# The map's own sky, as a mask, a distance and a face id.
SKY=(--world-geometry sky --no-world-cull "${WATER[@]}")
capture sky-faces --world-flat "${SKY[@]}" || fail=1
capture sky-depth --world-depth "${SKY[@]}" || fail=1
capture sky-ids --world-face-id "${SKY[@]}" || fail=1
# The world's own face ids, for the seam trace: coverage says an empty pixel is
# sky-backed, and these frames say which two faces the empty pixel sits between.
capture world-ids --world-face-id "${WATER[@]}" || fail=1
# ... and the same with the cull off, because a sliver is often bounded on one
# side by a surface the product culls. Without this frame that neighbour has no
# id at all -- the cull-on frame leaves the skybox colour there -- and the trace
# would report "nothing" where the map has a face.
capture world-ids-nocull --world-face-id --no-world-cull "${WATER[@]}" || fail=1

if [ "$fail" -ne 0 ]; then
  echo "T26 capture matrix FAILED" >&2
  exit 1
fi
echo "T26 capture matrix complete: $(ls "$OUT"/*.bmp | wc -l) frames in $OUT"
