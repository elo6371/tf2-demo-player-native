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
# CACHE VALIDITY (T49)
# --------------------
# Until T49 the resume test was `capture_written=1` plus the right tick. That is
# not a provenance test, and the T49 release run proved it: an 18-frame matrix
# from a tree whose traces had no `scene: camera ... eye=... src=demo` line was
# declared good and reused, so the semantic gate could not name the pose behind
# the pixels and went red on cached evidence. A frame is reusable only when ALL
# of the following hold, and every one of them is recorded in
# `$TRACE_DIR/<name>.identity` at capture time:
#   version        the identity schema below (bumped when the fields change)
#   head           the source commit that was checked out
#   exe            sha256 of the product executable that drew the frame
#   demo, map      sha256 of the demo and the BSP the run was pointed at
#   tick           the armed frame tick
#   window         --hidden-window or not (a visible window is a different run)
#   flags          the exact capture switches, in order
#   captureScript  sha256 of this file, so a changed capture path invalidates
# PLUS two trace properties the semantic gate reads and cannot invent:
#   `capture: armed ... frame_tick=<tick>` and, before that line, a
#   `scene: camera ... eye=... src=...` line. A trace without the pose is stale
#   evidence and is recaptured, not reused.
#
# Usage: bash world-geometry-hole-capture.sh [--recapture] [output-dir]
#        bash world-geometry-hole-capture.sh --validate <captures-dir>
#   --recapture  ignore cached frames and capture the whole matrix again.
#   --validate   launch nothing: report per frame whether the cache in
#                <captures-dir> would be reused, and why not when it would not.
#                The gate's cache control uses it to prove the rule rejects a
#                trace that has lost its pose.
# Exit:  0 = every frame is captured (or validated); 1 = one could not be;
#        2 = a required input is missing.
set -uo pipefail
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
MAP=maps/pl_odyssey.bsp
TICK=2000
IDENTITY_VERSION=1

RECAPTURE=0
VALIDATE=0
OUT=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --recapture) RECAPTURE=1; shift ;;
    --validate)  VALIDATE=1; shift ;;
    -*)          echo "unknown switch: $1" >&2; exit 2 ;;
    *)           OUT="$1"; shift ;;
  esac
done
OUT="${OUT:-evidence/t26-world-holes/captures}"
TRACE_DIR="$OUT/traces"
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

# ---- cache identity ------------------------------------------------------
# Every input that can move a pixel, hashed once per run. The demo and the BSP
# are 21 MB and 131 MB, so they are hashed once and reused for all 18 frames
# rather than 18 times.
sha_of() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }
HEAD_SHA=$(git rev-parse HEAD 2>/dev/null || echo unknown)
EXE_SHA=$(sha_of "$PRODUCT")
DEMO_SHA=$(sha_of "$DEMO")
MAP_SHA=$(sha_of "$TF/$MAP")
CAPTURE_SCRIPT_SHA=$(sha_of "$0")
if [ -z "$EXE_SHA" ] || [ -z "$DEMO_SHA" ] || [ -z "$MAP_SHA" ] || [ -z "$CAPTURE_SCRIPT_SHA" ]; then
  echo "capture: an identity input is missing (product=$EXE_SHA demo=$DEMO_SHA map=$MAP_SHA script=$CAPTURE_SCRIPT_SHA)" >&2
  exit 2
fi
# The flags are recorded as one line and compared as one line, so a switch whose
# ORDER changes (which changes nothing) still matches and a switch that appears
# or disappears does not.
identity_line() { # identity_line <name> <window> <flags...>
  local name="$1" window="$2"; shift 2
  printf 'version=%s\nhead=%s\nexe=%s\ndemo=%s\nmap=%s\ntick=%s\nwindow=%s\nflags=%s\ncaptureScript=%s\n' \
    "$IDENTITY_VERSION" "$HEAD_SHA" "$EXE_SHA" "$DEMO_SHA" "$MAP_SHA" "$TICK" \
    "$window" "$*" "$CAPTURE_SCRIPT_SHA"
}

# ---- what makes a cached frame reusable -----------------------------------
# Every reason a frame is rejected is named, because "recaptured" without a
# reason is exactly how a stale trace survives a release run.
cache_reason() { # cache_reason <name> <bmp> <trace> <identity> <window> <flags...>
  local name="$1" bmp="$2" trace="$3" ident="$4" window="$5"; shift 5
  local armed_index last_camera reason=""
  if [ ! -s "$bmp" ]; then printf 'no-frame'; return 0; fi
  if [ ! -s "$trace" ]; then printf 'no-trace'; return 0; fi
  if ! grep -aq "capture_written=1" "$trace"; then printf 'trace-not-written'; return 0; fi
  armed_index=$(grep -an "capture: armed " "$trace" | tr -d '\r' | tail -1 | cut -d: -f1)
  if [ -z "$armed_index" ]; then printf 'trace-no-armed-line'; return 0; fi
  reason=$(sed -n "$armed_index p" "$trace" | sed -n 's/.*frame_tick=\([-0-9]*\).*/\1/p')
  if [ "$reason" != "$TICK" ]; then printf 'trace-tick=%s' "${reason:-none}"; return 0; fi
  # The pose/provenance line the semantic gate classifies with. It must exist
  # BEFORE the arming line, because that is the camera the frame was drawn with.
  last_camera=$(grep -an "scene: camera " "$trace" | tr -d '\r' | awk -F: -v limit="$armed_index" '$1 < limit' | tail -1 | cut -d: -f1)
  if [ -z "$last_camera" ]; then printf 'trace-no-camera-pose'; return 0; fi
  if ! sed -n "$last_camera p" "$trace" | grep -aq "eye="; then printf 'trace-camera-no-eye'; return 0; fi
  if ! sed -n "$last_camera p" "$trace" | grep -aq "src="; then printf 'trace-camera-no-src'; return 0; fi
  if [ ! -s "$ident" ]; then printf 'no-identity'; return 0; fi
  if ! diff -q <(identity_line "$name" "$window" "$@") "$ident" >/dev/null 2>&1; then
    printf 'identity-mismatch'
    return 0
  fi
  printf 'ok'
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
  local ident="$TRACE_DIR/$name.identity"
  local attempt tick rc reason window=--hidden-window
  [ "$name" = "ref" ] && window=
  reason=$(cache_reason "$name" "$bmp" "$trace" "$ident" "$window" "$@")

  # --validate launches nothing: it reports what the cache decision would be, so
  # the gate can assert the rule on a directory whose trace has been made stale.
  if [ "$VALIDATE" = 1 ]; then
    printf 'VALIDATE %s reason=%s\n' "$name" "$reason"
    [ "$reason" = "ok" ] || return 1
    return 0
  fi

  if [ "$reason" = "ok" ] && [ "$RECAPTURE" = 0 ]; then
    echo "  kept $name (identity and pose match, tick $TICK)"
    return 0
  fi
  if [ "$RECAPTURE" = 1 ]; then
    echo "  recapturing $name (--recapture)"
  elif [ "$reason" != "ok" ]; then
    echo "  recapturing $name (cache rejected: $reason)"
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
    # The frame is only accepted if the cache rule would accept it on the next
    # run; otherwise a capture that writes a frame without a pose is "successful"
    # every time and no later run can tell it from stale evidence.
    reason=""
    if [ "$rc" = 0 ]; then
      reason=$(cache_reason "$name" "$bmp" "$trace" "" "$window" "$@")
      # The identity is written after the trace exists, so a just-captured frame
      # is judged on its trace properties first and on identity second.
      [ "$reason" = "no-identity" ] && reason=ok
    else
      reason="rc=$rc"
    fi
    if [ "$reason" = "ok" ]; then
      identity_line "$name" "$window" "$@" > "$ident"
      echo "  captured $name (attempt $attempt, tick $tick)"
      return 0
    fi
    echo "  capture $name attempt $attempt: $reason, retrying"
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

if [ "$VALIDATE" = 1 ]; then
  if [ "$fail" -ne 0 ]; then
    echo "T26 cache validation: at least one frame would NOT be reused" >&2
    exit 1
  fi
  echo "T26 cache validation: all $(ls "$OUT"/*.bmp | wc -l) frames have matching identity and pose"
  exit 0
fi

if [ "$fail" -ne 0 ]; then
  echo "T26 capture matrix FAILED" >&2
  exit 1
fi
# The matrix is complete, but a frame whose own trace cannot name its pose is not
# evidence: say so here rather than let the semantic gate discover it later.
IDENTITY_FAIL=0
for name in $(cd "$OUT" && ls *.bmp | sed 's/\.bmp$//'); do
  window=--hidden-window
  [ "$name" = "ref" ] && window=
  [ -s "$TRACE_DIR/$name.identity" ] || { echo "  FAIL $name has no identity file" >&2; IDENTITY_FAIL=1; }
done
if [ "$IDENTITY_FAIL" -ne 0 ]; then
  echo "T26 capture matrix wrote frames without identity" >&2
  exit 1
fi
echo "T26 capture matrix complete: $(ls "$OUT"/*.bmp | wc -l) frames in $OUT, each with a pose-bearing trace and an identity file"
