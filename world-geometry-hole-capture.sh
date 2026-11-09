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
# CACHE VALIDITY (T49; key/provenance split by T45/D-45)
# ------------------------------------------------------
# Until T49 the resume test was `capture_written=1` plus the right tick. That is
# not a provenance test, and the T49 release run proved it: an 18-frame matrix
# from a tree whose traces had no `scene: camera ... eye=... src=demo` line was
# declared good and reused, so the semantic gate could not name the pose behind
# the pixels and went red on cached evidence.
#
# T49 added an identity file but put the commit in it, so EVERY commit -- even a
# docs-only one -- invalidated all 18 frames (RUN11: 0/18 cached), which made the
# matrix pay its full capture cost on every release run and taught nobody
# anything. D-45 separates the three jobs an identity has:
#
#   reuse key   cacheFormat, exe, shader, demo, map, assets, tick, window,
#               flags, captureScript, settings, captureMode, gpu. Every field
#               can move a pixel; a difference is a miss and the miss reason
#               names the field that differed.
#   provenance  the commit that was checked out and the host paths it ran
#               from. RECORDED, NEVER COMPARED: a docs-only commit changes this
#               block only and must not recapture a pixel.
#   artifact    frameSha256/frameBytes/traceSha256/traceBytes/frameWidth/
#               frameHeight/capturePose. Checked against the files on disk
#               BEFORE the key is trusted, so a truncated capture, a missing
#               frame or a hand-edited trace is a named miss, not a hit.
#
# The identity file is also the completion marker: it is written LAST, through a
# temp file and a rename, and it is removed before a recapture starts. The
# product writes into $OUT/.partial and the artifacts are moved into place only
# once they exist, so a run that dies halfway leaves no identity -- and a frame
# without an identity is `no-identity`, never a hit.
#
# Two inputs the key deliberately delegates, because they cannot be known
# without launching the product:
#   camera      recorded from the frame's own trace (capturePose) and verified
#               against it. A camera change can only come from demo, tick, flags
#               or the product, all of which are key fields.
#   resolution  the requested size lives in settings=; the produced size is
#               checked against the frame itself (frame-resolution).
#
# Usage: bash world-geometry-hole-capture.sh [--recapture] [--only=name,...] [output-dir]
#        bash world-geometry-hole-capture.sh --validate <captures-dir>
#        bash world-geometry-hole-capture.sh --self-test
#   --recapture  ignore cached frames and capture the whole matrix again.
#   --only=...   only these frames; a single-frame recapture for a byte compare.
#   --validate   launch nothing: report per frame whether the cache in
#                <captures-dir> would be reused, and why not when it would not.
#                The gate's cache control uses it to prove the rule rejects a
#                trace that has lost its pose, and refuses a frame whose
#                executable changed.
#   --self-test  launch nothing: prove the rule on a synthetic frame (D-45's
#                controls: provenance is ignored, each key field is fatal, a
#                missing or corrupt artifact is fatal, tick/camera are fatal).
# Exit:  0 = every frame is captured (or validated, or the self-test passed);
#        1 = one could not be; 2 = a required input is missing.
set -uo pipefail
cd "$(dirname "$0")"
# The isolated-settings helpers live in one file with their own test (iso-trap-selftest.sh).
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/verify-iso.sh"

# ---- inputs --------------------------------------------------------------
# Defaults are the shipped capture. Every input is overridable from the
# environment, because the cache rule has to be provable without rebuilding the
# product or touching the Steam install: the negative controls in --self-test
# (and the acceptance controls run by hand) change one input at a time through
# these seams.
PRODUCT=${CAPTURE_PRODUCT:-native/build-nmake/tf2_demo_native.exe}
TF=${CAPTURE_TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${CAPTURE_DEMO:-$TF/demos/autorecord_2026-07-02_13-26-46.dem}
MAP=${CAPTURE_MAP:-maps/pl_odyssey.bsp}
TICK=${CAPTURE_TICK:-2000}
# The material/model side of the asset identity. The game's materials and models
# live in the VPK archives: the numbered archives are ~100 MB each and cannot be
# hashed per run, but each *_dir.vpk index carries an entry table with a CRC32
# per file, so a material or model whose bytes changed changes this index too.
# The loose materials/ and models/ overlays are hashed when present.
ASSET_ROOT=${CAPTURE_ASSET_ROOT:-$TF}
# The renderer's shaders are embedded in the product as C++ string literals, so a
# shader change also moves exe=. They are hashed on their own so a shader-only
# change is NAMED in the miss reason instead of looking like a product change.
SHADER_FILES=${CAPTURE_SHADER_FILES:-native/src/native_renderer_shaders.cpp native/src/native_renderer_shaders.h}
# The cache rule's own version. 3 = key and provenance separated, artifact
# checksums added, staging directory. 2 (T49) put the commit in the key.
CACHE_FORMAT=3
CAPTURE_MODE=matrix

RECAPTURE=0
VALIDATE=0
SELF_TEST=0
ONLY=""
OUT=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --recapture) RECAPTURE=1; shift ;;
    --validate)  VALIDATE=1; shift ;;
    --self-test) SELF_TEST=1; shift ;;
    --only=*)    ONLY="${1#--only=}"; shift ;;
    --only)      ONLY="$2"; shift 2 ;;
    -*)          echo "unknown switch: $1" >&2; exit 2 ;;
    *)           OUT="$1"; shift ;;
  esac
done
OUT="${OUT:-evidence/t26-world-holes/captures}"
TRACE_DIR="$OUT/traces"
PARTIAL_DIR="$OUT/.partial"

# ---- small helpers -------------------------------------------------------
CR=$(printf '\r')
sha_of() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }
file_size() { wc -c < "$1" 2>/dev/null | tr -d ' '; }
win_path() { # win_path <posix path> -> the path the product receives
  local p="$1"
  case "$p" in
    /*|[A-Za-z]:*) : ;;
    *) p="$PWD/$p" ;;
  esac
  cygpath -m "$p"
}
bmp_size() { # bmp_size <bmp> -> "<width> <height>" from the BMP header
  od -An -tu4 -j 18 -N 8 "$1" 2>/dev/null | awk 'NR==1 { print $1, $2 }'
}
ident_field() { # ident_field <identity file> <field>
  sed -n "s/^$2=//p" "$1" 2>/dev/null | head -1
}
# cache_reason runs in a command substitution, so its diagnostics cannot come
# back through variables: the reason is stdout and the detail is this file.
DETAIL_FILE="${TMPDIR:-/tmp}/t26-cache-detail.$$"
: > "$DETAIL_FILE" 2>/dev/null || DETAIL_FILE=""
set_detail() { [ -n "$DETAIL_FILE" ] && printf '%s' "$1" > "$DETAIL_FILE"; return 0; }
get_detail() { [ -n "$DETAIL_FILE" ] && cat "$DETAIL_FILE" 2>/dev/null; return 0; }

# ---- cache identity ------------------------------------------------------
# Every input that can move a pixel, hashed once per run. The demo and the BSP
# are 21 MB and 131 MB, so they are hashed once and reused for all 18 frames
# rather than 18 times.
shader_identity() {
  local f
  {
    for f in $SHADER_FILES; do
      if [ -f "$f" ]; then
        echo "$f $(file_size "$f") $(sha_of "$f")"
      else
        echo "$f missing"
      fi
    done
  } | sha256sum | cut -d' ' -f1
}
asset_identity() { # asset_identity <root>
  local root="$1" f
  {
    for f in "$root"/*_dir.vpk; do
      [ -e "$f" ] || continue
      echo "vpk $(basename "$f") $(file_size "$f") $(sha_of "$f")"
    done
    if [ -d "$root/materials" ]; then
      find "$root/materials" -type f 2>/dev/null | LC_ALL=C sort | while IFS= read -r f; do
        echo "loose materials/${f#"$root"/materials/} $(file_size "$f") $(sha_of "$f")"
      done
    fi
    if [ -d "$root/models" ]; then
      find "$root/models" -type f 2>/dev/null | LC_ALL=C sort | while IFS= read -r f; do
        echo "loose models/${f#"$root"/models/} $(file_size "$f") $(sha_of "$f")"
      done
    fi
  } | sha256sum | cut -d' ' -f1
}
gpu_identity() { # gpu_identity -> "<adapter name>|<driver version>"
  local name="" driver=""
  if command -v powershell >/dev/null 2>&1; then
    name=$(powershell -NoProfile -Command "(Get-CimInstance Win32_VideoController | Select-Object -First 1 -ExpandProperty Name)" 2>/dev/null | tr -d "$CR" | sed -n '1p')
    driver=$(powershell -NoProfile -Command "(Get-CimInstance Win32_VideoController | Select-Object -First 1 -ExpandProperty DriverVersion)" 2>/dev/null | tr -d "$CR" | sed -n '1p')
  fi
  if [ -z "$name" ] && command -v wmic >/dev/null 2>&1; then
    name=$(wmic path win32_VideoController get name 2>/dev/null | tr -d "$CR" | sed -n '2p' | sed 's/^ *//;s/ *$//')
    driver=$(wmic path win32_VideoController get driverversion 2>/dev/null | tr -d "$CR" | sed -n '2p' | sed 's/^ *//;s/ *$//')
  fi
  printf '%s|%s' "${name:-unknown}" "${driver:-unknown}"
}
# The five provenance/artifact globals are filled by compute_inputs below; the
# self-test sets its own values and never calls it.
HEAD_SHA=""; EXE_SHA=""; DEMO_SHA=""; MAP_SHA=""; CAPTURE_SCRIPT_SHA=""; CAP_SETTINGS_SHA=""
SHADER_SHA=""; ASSET_SHA=""; GPU_SHA=""
compute_inputs() {
  HEAD_SHA=$(git rev-parse HEAD 2>/dev/null || echo unknown)
  EXE_SHA=$(sha_of "$PRODUCT")
  DEMO_SHA=$(sha_of "$DEMO")
  MAP_SHA=$(sha_of "$TF/$MAP")
  CAPTURE_SCRIPT_SHA=$(sha_of "$0")
  CAP_SETTINGS_SHA=$(sha_of "$(iso_fixture_settings)")
  SHADER_SHA=$(shader_identity)
  ASSET_SHA=$(asset_identity "$ASSET_ROOT")
  GPU_SHA=$(printf '%s' "${CAPTURE_GPU_IDENTITY:-$(gpu_identity)}" | sha256sum | cut -d' ' -f1)
  local missing="" shader_missing=0 f
  [ -n "$EXE_SHA" ] || missing="$missing product=$PRODUCT"
  [ -n "$DEMO_SHA" ] || missing="$missing demo=$DEMO"
  [ -n "$MAP_SHA" ] || missing="$missing map=$TF/$MAP"
  [ -n "$CAPTURE_SCRIPT_SHA" ] || missing="$missing script=$0"
  [ -n "$CAP_SETTINGS_SHA" ] || missing="$missing settings=$(iso_fixture_settings)"
  for f in $SHADER_FILES; do [ -f "$f" ] || shader_missing=1; done
  [ "$shader_missing" = 0 ] || missing="$missing shader=$SHADER_FILES"
  [ -n "$ASSET_SHA" ] || missing="$missing assets=$ASSET_ROOT"
  if [ -n "$missing" ]; then
    echo "capture: an identity input is missing:$missing" >&2
    exit 2
  fi
}

# The reuse key, one source of truth: key_line prints it, key_value answers the
# current value for the comparison, so the two cannot drift apart.
KEY_FIELDS="cacheFormat exe shader demo map assets tick window flags captureScript settings captureMode gpu"
key_value() { # key_value <field> <window> <flags...>
  local field="$1" window="$2"; shift 2
  case "$field" in
    cacheFormat)   printf '%s' "$CACHE_FORMAT" ;;
    exe)           printf '%s' "$EXE_SHA" ;;
    shader)        printf '%s' "$SHADER_SHA" ;;
    demo)          printf '%s' "$DEMO_SHA" ;;
    map)           printf '%s' "$MAP_SHA" ;;
    assets)        printf '%s' "$ASSET_SHA" ;;
    tick)          printf '%s' "$TICK" ;;
    window)        printf '%s' "$window" ;;
    flags)         printf '%s' "$*" ;;
    captureScript) printf '%s' "$CAPTURE_SCRIPT_SHA" ;;
    settings)      printf '%s' "$CAP_SETTINGS_SHA" ;;
    captureMode)   printf '%s' "$CAPTURE_MODE" ;;
    gpu)           printf '%s' "$GPU_SHA" ;;
    *)             printf '' ;;
  esac
}
key_line() { # key_line <window> <flags...>
  local window="$1"; shift
  local f
  for f in $KEY_FIELDS; do echo "$f=$(key_value "$f" "$window" "$@")"; done
}
provenance_line() {
  echo "provenanceHead=$HEAD_SHA"
  echo "provenanceTfRoot=$TF"
  echo "provenanceProduct=$PRODUCT"
  echo "provenanceCapturedAt=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
artifact_line() { # artifact_line <bmp> <trace>
  local bmp="$1" trace="$2" wh w h
  wh=$(bmp_size "$bmp")
  w=${wh%% *}
  h=${wh##* }
  echo "frameSha256=$(sha_of "$bmp")"
  echo "frameBytes=$(file_size "$bmp")"
  echo "traceSha256=$(sha_of "$trace")"
  echo "traceBytes=$(file_size "$trace")"
  echo "frameWidth=$w"
  echo "frameHeight=$h"
  echo "capturePose=$(trace_pose "$trace")"
}
write_identity() { # write_identity <out> <bmp> <trace> <window> <flags...>
  local out="$1" bmp="$2" trace="$3" window="$4"; shift 4
  {
    echo "# t26 frame cache identity v$CACHE_FORMAT -- generated, do not hand-edit"
    echo "# reuse key: a difference in any of these lines is a different picture"
    key_line "$window" "$@"
    echo "# provenance: recorded for the audit trail, never compared (a commit is not a pixel)"
    provenance_line
    echo "# artifact identity: the files this identity describes, checked before the key is trusted"
    artifact_line "$bmp" "$trace"
  } > "$out"
}

# Which tick a captured frame belongs to. The product records it at the moment
# the capture is armed. A trace written before that line existed falls back to
# the first draw at or after the requested tick, which is the rule the arming
# itself uses and therefore the same answer.
frame_tick() {
  local trace="$1" tick
  tick=$(grep -a "capture: armed " "$trace" 2>/dev/null | tr -d "$CR" | tail -1 | grep -o 'frame_tick=[-0-9]*' | cut -d= -f2)
  if [ -z "$tick" ]; then
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d "$CR" | grep -o 'tick=[0-9]*' | cut -d= -f2 | awk -v want="$TICK" '$1 >= want { print; exit }')
  fi
  printf '%s' "$tick"
}
# The pose the frame was drawn with: the last camera line BEFORE the arming line.
trace_pose() {
  local trace="$1" armed line
  armed=$(grep -an "capture: armed " "$trace" 2>/dev/null | tr -d "$CR" | tail -1 | cut -d: -f1)
  [ -n "$armed" ] || { printf ''; return 0; }
  line=$(grep -an "scene: camera " "$trace" 2>/dev/null | tr -d "$CR" | awk -F: -v limit="$armed" '$1 < limit' | tail -1)
  [ -n "$line" ] || { printf ''; return 0; }
  printf '%s' "$line" | grep -oE 'eye=[^ ]*|pitch=[^ ]*|yaw=[^ ]*|src=[^ ]*' | paste -sd' ' -
}

# ---- what makes a cached frame reusable -----------------------------------
# Every reason a frame is rejected is named, because "recaptured" without a
# reason is exactly how a stale trace survives a release run. The first twelve
# words are the T49 vocabulary the gate asserts on; the artifact words are new.
cache_reason() { # cache_reason <name> <bmp> <trace> <identity> <window> <flags...>
  local name="$1" bmp="$2" trace="$3" ident="$4" window="$5"; shift 5
  local armed_index last_camera reason="" field bad miss val got
  set_detail ""
  if [ ! -s "$bmp" ]; then printf 'no-frame'; return 0; fi
  if [ ! -s "$trace" ]; then printf 'no-trace'; return 0; fi
  if ! grep -aq "capture_written=1" "$trace"; then printf 'trace-not-written'; return 0; fi
  armed_index=$(grep -an "capture: armed " "$trace" | tr -d "$CR" | tail -1 | cut -d: -f1)
  if [ -z "$armed_index" ]; then printf 'trace-no-armed-line'; return 0; fi
  reason=$(sed -n "${armed_index}p" "$trace" | tr -d "$CR" | grep -o 'frame_tick=[-0-9]*' | cut -d= -f2)
  if [ "$reason" != "$TICK" ]; then printf 'trace-tick=%s' "${reason:-none}"; return 0; fi
  last_camera=$(grep -an "scene: camera " "$trace" | tr -d "$CR" | awk -F: -v limit="$armed_index" '$1 < limit' | tail -1 | cut -d: -f1)
  if [ -z "$last_camera" ]; then printf 'trace-no-camera-pose'; return 0; fi
  if ! sed -n "${last_camera}p" "$trace" | grep -aq "eye="; then printf 'trace-camera-no-eye'; return 0; fi
  if ! sed -n "${last_camera}p" "$trace" | grep -aq "src="; then printf 'trace-camera-no-src'; return 0; fi
  if [ ! -s "$ident" ]; then printf 'no-identity'; return 0; fi
  # The identity must describe this artifact before its key is worth comparing:
  # a checksum mismatch means the frame or the trace is not the one it names.
  # Presence, not value: window= is legitimately EMPTY for `ref` (the one frame
  # that runs without --hidden-window), so an empty field is only incomplete for
  # the artifact block, where an empty checksum or size is not a value at all.
  miss=""
  for field in $KEY_FIELDS; do
    grep -q "^$field=" "$ident" 2>/dev/null || miss="$miss $field"
  done
  for field in frameSha256 frameBytes traceSha256 traceBytes frameWidth frameHeight; do
    val=$(ident_field "$ident" "$field")
    [ -n "$val" ] || miss="$miss $field"
  done
  grep -q '^capturePose=' "$ident" 2>/dev/null || miss="$miss capturePose"
  if [ -n "$miss" ]; then set_detail "missing=$miss"; printf 'identity-incomplete'; return 0; fi
  got=$(sha_of "$bmp")
  if [ "$got" != "$(ident_field "$ident" frameSha256)" ]; then set_detail "frame=$got"; printf 'frame-sha'; return 0; fi
  got=$(sha_of "$trace")
  if [ "$got" != "$(ident_field "$ident" traceSha256)" ]; then set_detail "trace=$got"; printf 'trace-sha'; return 0; fi
  got=$(bmp_size "$bmp")
  if [ "${got%% *}" != "$(ident_field "$ident" frameWidth)" ] || [ "${got##* }" != "$(ident_field "$ident" frameHeight)" ]; then
    set_detail "frame=$got identity=$(ident_field "$ident" frameWidth)x$(ident_field "$ident" frameHeight)"
    printf 'frame-resolution'; return 0
  fi
  if [ "$(trace_pose "$trace")" != "$(ident_field "$ident" capturePose)" ]; then
    set_detail "trace=$(trace_pose "$trace")"
    printf 'capture-pose'; return 0
  fi
  # The reuse key. Each field is compared on its own so the miss names it.
  bad=""
  for field in $KEY_FIELDS; do
    val=$(ident_field "$ident" "$field")
    [ "$val" = "$(key_value "$field" "$window" "$@")" ] || bad="$bad $field"
  done
  if [ -n "$bad" ]; then set_detail "fields=$bad"; printf 'identity-mismatch'; return 0; fi
  # Hit. If only the provenance moved, say so: that is the D-45 case a docs-only
  # commit produces, and it must read as a hit in the log, not as a silent reuse.
  val=$(ident_field "$ident" provenanceHead)
  if [ -n "$val" ] && [ "$val" != "$HEAD_SHA" ]; then
    set_detail "provenance head=$(printf '%s' "$val" | cut -c1-12) (not part of the key)"
  fi
  printf 'ok'
}

only_has() { # only_has <name>
  local n
  [ -z "$ONLY" ] && return 0
  for n in ${ONLY//,/ }; do [ "$n" = "$1" ] && return 0; done
  return 1
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

# ---- capture -------------------------------------------------------------
HITS=()
MISSES=()
capture() { # capture <name> [extra flags...]
  local name="$1"; shift
  only_has "$name" || return 0
  local bmp="$OUT/$name.bmp"
  local trace="$TRACE_DIR/$name.trace"
  local ident="$TRACE_DIR/$name.identity"
  local stage_bmp="$PARTIAL_DIR/$name.bmp"
  local stage_trace="$PARTIAL_DIR/$name.trace"
  local stage_ident="$PARTIAL_DIR/$name.identity.tmp"
  local window=--hidden-window
  [ "$name" = "ref" ] && window=
  local attempt tick rc reason detail
  reason=$(cache_reason "$name" "$bmp" "$trace" "$ident" "$window" "$@"); detail=$(get_detail)

  # --validate launches nothing: it reports what the cache decision would be, so
  # the gate can assert the rule on a directory whose trace has been made stale.
  if [ "$VALIDATE" = 1 ]; then
    printf 'VALIDATE %s reason=%s\n' "$name" "$reason"
    [ -n "$detail" ] && printf '  detail %s %s\n' "$name" "$detail"
    [ "$reason" = "ok" ] || return 1
    return 0
  fi

  if [ "$reason" = "ok" ] && [ "$RECAPTURE" = 0 ]; then
    HITS+=("$name")
    printf '  kept %s (reuse key matches, tick %s)' "$name" "$TICK"
    [ -n "$detail" ] && printf '; %s' "$detail"
    printf '\n'
    return 0
  fi
  if [ "$RECAPTURE" = 1 ]; then
    printf '  recapturing %s (--recapture)\n' "$name"
  else
    printf '  recapturing %s (cache rejected: %s' "$name" "$reason"
    [ -n "$detail" ] && printf ', %s' "$detail"
    printf ')\n'
  fi
  MISSES+=("$name:$reason")
  mkdir -p "$PARTIAL_DIR"
  # Invalidate before drawing. Until the identity below is renamed into place a
  # reader must see "no cache", never a frame from a previous configuration.
  rm -f "$ident" "$bmp" "$trace" "$stage_bmp" "$stage_trace" "$stage_ident"
  if ! wait_for_idle_host 300; then
    echo "  note: $name starts while another product process is running; --hidden-window is used" >&2
  fi
  for attempt in 1 2 3 4 5 6 7 8; do
    rm -f "$stage_bmp" "$stage_trace"
    TF2_NATIVE_TRACE="$(win_path "$stage_trace")" timeout 120 "$PRODUCT" \
      --tf-root "$TF" --demo "$DEMO" \
      --capture-tick "$TICK" --capture-frame "$(win_path "$stage_bmp")" \
      --settings-file "$(win_path "$CAP_ISO_SETTINGS")" \
      --ui-settings-file "$(win_path "$CAP_ISO_UI")" \
      --audio-disabled $window "$@" \
      > "$OUT/$name.log" 2>&1
    rc=$?
    tick=$(frame_tick "$stage_trace")
    if [ "$rc" != 0 ] || [ ! -s "$stage_bmp" ] || [ ! -s "$stage_trace" ]; then
      echo "  capture $name attempt $attempt: rc=$rc, no frame or no trace, retrying"
      continue
    fi
    # Move the artifacts into place first; the identity is written last and is
    # the completion marker, so an interrupted capture leaves an unmarked frame
    # that the rule reports as no-identity instead of reusing half a result.
    mv -f "$stage_bmp" "$bmp" || { echo "  capture $name attempt $attempt: cannot move the frame"; continue; }
    mv -f "$stage_trace" "$trace" || { echo "  capture $name attempt $attempt: cannot move the trace"; continue; }
    reason=$(cache_reason "$name" "$bmp" "$trace" "" "$window" "$@"); detail=$(get_detail)
    if [ "$reason" != "no-identity" ]; then
      echo "  capture $name attempt $attempt: $reason, retrying"
      rm -f "$bmp" "$trace"
      continue
    fi
    write_identity "$stage_ident" "$bmp" "$trace" "$window" "$@"
    mv -f "$stage_ident" "$ident" || { echo "  capture $name attempt $attempt: cannot write the identity"; rm -f "$bmp" "$trace"; continue; }
    # The frame is only accepted if the cache rule would accept it on the next
    # run; otherwise a capture that writes a frame without a pose is
    # "successful" every time and no later run can tell it from stale evidence.
    reason=$(cache_reason "$name" "$bmp" "$trace" "$ident" "$window" "$@"); detail=$(get_detail)
    if [ "$reason" = "ok" ]; then
      printf '  captured %s (attempt %s, tick %s)\n' "$name" "$attempt" "${tick:-?}"
      return 0
    fi
    echo "  capture $name attempt $attempt: the rule rejects the identity just written ($reason), retrying"
    rm -f "$ident" "$bmp" "$trace"
  done
  echo "  FAILED to capture $name at tick $TICK" >&2
  return 1
}

# ---- self test -----------------------------------------------------------
# Synthetic frame, real rule: no product launch, no hostlock, no build. This is
# how the D-45 acceptance list is proved on a machine without a built product.
make_tiny_bmp() { # make_tiny_bmp <path> -- a 2x2 24-bit BMP (header only)
  local p="$1"
  printf 'BM\x3e\x00\x00\x00\x00\x00\x00\x00\x36\x00\x00\x00\x28\x00\x00\x00\x02\x00\x00\x00\x02\x00\x00\x00\x01\x00\x18\x00\x00\x00\x00\x00\x08\x00\x00\x00\x13\x0b\x00\x00\x13\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$p"
  printf '\x00\x00\x00\x00\x00\x00\x00\x00' >> "$p"
}
self_test() {
  local dir case n pass=0 fail=0 reason detail
  dir=$(mktemp -d "${TMPDIR:-/tmp}/t26-selftest.XXXXXX") || { echo "self-test: cannot create a temp dir" >&2; return 2; }
  mkdir -p "$dir/base/traces"
  make_tiny_bmp "$dir/base/frame.bmp"
  {
    echo '0.0 args: self-test'
    echo '1.0 scene: camera mode=first-person target=1 eye=1.0,2.0,3.0 pitch=0.00 yaw=0.00 src=demo'
    echo '2.0 capture: armed requested=2000 frame_tick=2000 frames=1'
    echo '3.0 capture: frame width=2 height=2 dpi=96'
    echo '4.0 capture_written=1'
  } > "$dir/base/traces/frame.trace"
  # Fixed identities: the self-test must not depend on the product existing.
  TICK=2000
  EXE_SHA=1111111111111111111111111111111111111111111111111111111111111111
  SHADER_SHA=2222222222222222222222222222222222222222222222222222222222222222
  DEMO_SHA=3333333333333333333333333333333333333333333333333333333333333333
  MAP_SHA=4444444444444444444444444444444444444444444444444444444444444444
  ASSET_SHA=5555555555555555555555555555555555555555555555555555555555555555
  GPU_SHA=6666666666666666666666666666666666666666666666666666666666666666
  CAPTURE_SCRIPT_SHA=7777777777777777777777777777777777777777777777777777777777777777
  CAP_SETTINGS_SHA=8888888888888888888888888888888888888888888888888888888888888888
  HEAD_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
  write_identity "$dir/base/traces/frame.identity" "$dir/base/frame.bmp" "$dir/base/traces/frame.trace" --hidden-window --world-water off
  check() { # check <expected> <what>
    reason=$(cache_reason frame "$fcase/frame.bmp" "$fcase/traces/frame.trace" "$fcase/traces/frame.identity" --hidden-window --world-water off)
    detail=$(get_detail)
    if [ "$reason" = "$1" ]; then
      echo "  PASS $2 (reason=$reason)"
      pass=$((pass + 1))
    else
      echo "  FAIL $2 (expected $1, got $reason${detail:+, $detail})"
      fail=$((fail + 1))
    fi
  }
  prep() { # prep -- a fresh copy of the baseline for one control
    rm -rf "$dir/case"; mkdir -p "$dir/case/traces"
    cp "$dir/base/frame.bmp" "$dir/case/frame.bmp"
    cp "$dir/base/traces/frame.trace" "$dir/case/traces/frame.trace"
    cp "$dir/base/traces/frame.identity" "$dir/case/traces/frame.identity"
    fcase="$dir/case"
  }

  fcase="$dir/base"; check ok 'a complete frame with the current key is a hit'
  # (a) docs-only commit: only the provenance moved.
  prep; sed -i 's/^provenanceHead=.*/provenanceHead=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/' "$fcase/traces/frame.identity"
  check ok 'a different provenance commit is still a hit (a commit is not a picture)'
  # (b) product and shader content.
  prep; sed -i 's/^exe=.*/exe=9999999999999999999999999999999999999999999999999999999999999999/' "$fcase/traces/frame.identity"
  check identity-mismatch 'a different product executable is refused'
  prep; EXE_SHA=0000000000000000000000000000000000000000000000000000000000000000; check identity-mismatch 'a rebuilt product (new exe hash) is refused'; EXE_SHA=1111111111111111111111111111111111111111111111111111111111111111
  prep; SHADER_SHA=0000000000000000000000000000000000000000000000000000000000000000; check identity-mismatch 'a changed shader is refused'; SHADER_SHA=2222222222222222222222222222222222222222222222222222222222222222
  # (c) demo, map, material/model assets.
  prep; DEMO_SHA=0000000000000000000000000000000000000000000000000000000000000000; check identity-mismatch 'a different demo is refused'; DEMO_SHA=3333333333333333333333333333333333333333333333333333333333333333
  prep; MAP_SHA=0000000000000000000000000000000000000000000000000000000000000000; check identity-mismatch 'a different BSP is refused'; MAP_SHA=4444444444444444444444444444444444444444444444444444444444444444
  prep; ASSET_SHA=0000000000000000000000000000000000000000000000000000000000000000; check identity-mismatch 'a changed material/model asset set is refused'; ASSET_SHA=5555555555555555555555555555555555555555555555555555555555555555
  prep; GPU_SHA=0000000000000000000000000000000000000000000000000000000000000000; check identity-mismatch 'a different GPU/driver is refused'; GPU_SHA=6666666666666666666666666666666666666666666666666666666666666666
  prep; sed -i 's/^cacheFormat=.*/cacheFormat=2/' "$fcase/traces/frame.identity"
  check identity-mismatch 'a cache written by an older rule format is refused'
  # (d) tick, camera, CLI.
  prep; TICK=2001; check trace-tick=2000 'a different tick (seek) is refused'; TICK=2000
  prep; sed -i '/^[0-9.]* scene: camera /d' "$fcase/traces/frame.trace"
  check trace-no-camera-pose 'a trace with no camera line is refused'
  prep; sed -i 's/^capturePose=.*/capturePose=eye=9,9,9 pitch=0 yaw=0 src=demo/' "$fcase/traces/frame.identity"
  check capture-pose 'a pose that does not match the trace is refused'
  prep; sed -i 's/^window=.*/window=/' "$fcase/traces/frame.identity"
  check identity-mismatch 'a different window mode is refused'
  prep; sed -i 's/^flags=.*/flags=--world-water on/' "$fcase/traces/frame.identity"
  check identity-mismatch 'a different capture CLI is refused'
  # (e) missing or corrupt artifacts: a miss, never a hit.
  prep; echo x >> "$fcase/frame.bmp"; check frame-sha 'a frame whose bytes changed is refused'
  prep; echo x >> "$fcase/traces/frame.trace"; check trace-sha 'a trace whose bytes changed is refused'
  prep; rm -f "$fcase/frame.bmp"; check no-frame 'a missing frame is refused'
  prep; rm -f "$fcase/traces/frame.trace"; check no-trace 'a missing trace is refused'
  prep; rm -f "$fcase/traces/frame.identity"; check no-identity 'a missing identity (interrupted capture) is refused'
  prep; sed -i '/^frameSha256=/d' "$fcase/traces/frame.identity"; check identity-incomplete 'an identity without an artifact checksum is refused'
  prep; sed -i 's/^frameWidth=.*/frameWidth=999/' "$fcase/traces/frame.identity"; check frame-resolution 'a frame whose size does not match the identity is refused'

  # The exit code of --validate must follow the rule, not just the reasons.
  local vrc=0
  prep; rm -f "$fcase/frame.bmp"
  ( VALIDATE=1; OUT="$fcase"; TRACE_DIR="$fcase/traces"; RECAPTURE=0; ONLY=""
    reason=$(cache_reason frame "$fcase/frame.bmp" "$fcase/traces/frame.trace" "$fcase/traces/frame.identity" --hidden-window --world-water off)
    [ "$reason" = ok ] && exit 0 || exit 1 ) || vrc=1
  if [ "$vrc" = 1 ]; then echo "  PASS a corrupted cache makes --validate exit non-zero"; pass=$((pass + 1));
  else echo "  FAIL a corrupted cache still made --validate exit zero"; fail=$((fail + 1)); fi

  rm -rf "$dir"
  echo "T26 cache self-test: $pass controls passed, $fail failed (rule format $CACHE_FORMAT)"
  [ "$fail" = 0 ]
}

# ---- main ----------------------------------------------------------------
if [ "$SELF_TEST" = 1 ]; then
  self_test
  exit $?
fi
compute_inputs

# ---- isolated settings (D-38) -------------------------------------------
# Every frame this script writes is drawn with an explicit renderer
# configuration: not the profile in %LOCALAPPDATA%, and not an empty file.
# An empty [renderer] section means Performance (settings_store.cpp:56-58),
# and Performance switches the sky pass off, which is 113,159 pixels of clear
# colour against the frames this matrix pins. The directory is created here and
# left alone if the caller passed one in; the gate passes CAPTURE_ISO_DIR so its
# own run_capture_trace frames and this matrix share one configuration.
CAP_ISO_DIR="${CAPTURE_ISO_DIR:-.scratch/capture-iso}"
case "$CAP_ISO_DIR" in
  /*) : ;;
  *)  CAP_ISO_DIR="$PWD/$CAP_ISO_DIR" ;;
esac
CAP_ISO_SETTINGS="$CAP_ISO_DIR/settings.ini"
CAP_ISO_UI="$CAP_ISO_DIR/ui.ini"
if [ "$VALIDATE" = 0 ]; then
  # --validate only reads traces and identities; it must not create state.
  mkdir -p "$OUT" "$TRACE_DIR"
  # The product rewrites both files when it exits (settings.ini gains the
  # runtime state, ui.ini is created), so a second run in the same directory
  # would either fail the fixture check or -- worse -- render under whatever UI
  # state the previous run left behind. Every run starts from the fixture: the
  # seed helper compares instead of overwriting (verify-iso.sh:87-104), so the
  # reset has to happen here, and it is printed, because silently running with
  # a configuration the identity does not name is the failure this guards.
  fixture=$(iso_fixture_settings)
  if [ -f "$CAP_ISO_SETTINGS" ] && ! cmp -s "$fixture" "$CAP_ISO_SETTINGS"; then
    echo "  restoring $CAP_ISO_SETTINGS from the gate fixture (a previous run rewrote it)"
  fi
  if [ -f "$CAP_ISO_UI" ]; then
    echo "  dropping $CAP_ISO_UI (a previous run wrote UI state; a fresh directory has none)"
    rm -f "$CAP_ISO_UI"
  fi
  rm -f "$CAP_ISO_SETTINGS"
  iso_seed_gate_settings "$CAP_ISO_DIR" || exit 2
  # Fail closed if the copy the product will read is not the fixture the
  # identity records (settings= below is the fixture's hash).
  iso_assert_gate_settings "$CAP_ISO_DIR" || exit 2
fi

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
  echo "T26 cache validation: all $(ls "$OUT"/*.bmp 2>/dev/null | wc -l) frames have a matching reuse key, artifact checksums and pose"
  exit 0
fi

if [ "$fail" -ne 0 ]; then
  echo "T26 capture matrix FAILED" >&2
  exit 1
fi
# The matrix is complete, but a frame whose own identity cannot be checked is
# not evidence: say so here rather than let the semantic gate discover it later.
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
if [ -n "$ONLY" ]; then
  echo "T26 cache this run: ${#MISSES[@]} recaptured, ${#HITS[@]} reused (--only=$ONLY)"
else
  echo "T26 cache this run: ${#MISSES[@]} recaptured, ${#HITS[@]} reused from cache"
fi
rm -f "$DETAIL_FILE"
echo "T26 capture matrix complete: $(ls "$OUT"/*.bmp | wc -l) frames in $OUT, each with a pose-bearing trace and an identity file"
