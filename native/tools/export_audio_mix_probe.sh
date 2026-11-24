#!/usr/bin/env bash
# export_audio_mix_probe.sh -- build native/tools/export_audio_mix_probe.cpp
# standalone with cl.exe, the same way native/tools/mf_export_probe.sh builds its
# probe. Self-contained on purpose: it compiles the product translation units the
# probe actually calls instead of linking a build tree some other build owns, so
# this probe builds from a bare worktree with only env-msvc.sh present.
#
# usage: native/tools/export_audio_mix_probe.sh [output-dir]
#   default output dir is <worktree>/.scratch/build-audio-mix (untracked scratch).
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

# This shell is a winuxcmd bash whose pwd does not support -W, so translate paths
# with cygpath when it exists. cl.exe needs Windows paths: a POSIX-looking absolute
# path such as /d/TF2_Native_Worktrees/... is read by the compiler as D:\d\... when
# D: happens to be the current drive.
to_win() {
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$1"
  else
    printf %s "$1"
  fi
}
ROOT_W="$(to_win "$ROOT")"
OUT="${1:-$ROOT/.scratch/build-audio-mix}"
mkdir -p "$OUT/obj" || exit 1
OUT_W="$(to_win "$OUT")"

# shellcheck source=/dev/null
source "$ROOT/env-msvc.sh" || { echo "build_rc=1 reason=env-msvc"; exit 1; }

SRC="$(to_win "$ROOT/native/tools/export_audio_mix_probe.cpp")"
EXE="$OUT_W/export_audio_mix_probe.exe"
INC="$(to_win "$ROOT/native/include")"
SRC_DIR="$(to_win "$ROOT/native/src")"

# The product TUs the probe calls: demo parsing (header/net/events/entity/strtab/
# camera/track/sound), the sound timeline, the VPK reader, the resource decoder,
# and the scheduler. No main.cpp: this tool is its own entry point.
PRODUCT_TUS=(
  audio_player.cpp
  audio_resource.cpp
  audio_timeline.cpp
  vpk_archive.cpp
  demo_header.cpp
  demo_header_core.cpp
  demo_header_net.cpp
  demo_header_events.cpp
  demo_header_entity.cpp
  demo_header_strtab.cpp
  demo_header_camera.cpp
  demo_header_track.cpp
  demo_header_sound.cpp
  demo_tick_audit.cpp
  wearable_instance.cpp
  entity_model.cpp
  item_schema.cpp
)
SOURCES=("$SRC")
for tu in "${PRODUCT_TUS[@]}"; do SOURCES+=("$SRC_DIR/$tu"); done

# /DNOMINMAX and /D_CRT_SECURE_NO_WARNINGS are what the product CMake build sets;
# without them <windows.h> min/max macros fight std::min and std::max in the
# product TUs. /utf-8 because two product sources carry non-ASCII comments and
# the default code page here is 936.
echo "build: cl.exe -> $EXE"
echo "build: sources=${#SOURCES[@]} (1 probe + ${#PRODUCT_TUS[@]} product TUs)"
cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /utf-8 \
  /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /D_USE_MATH_DEFINES \
  /I "$INC" \
  "${SOURCES[@]}" \
  /Fe:"$EXE" /Fo:"$OUT_W/obj/" \
  /link winmm.lib ole32.lib user32.lib msacm32.lib
BUILD_RC=$?
echo "build_rc=$BUILD_RC"
if [ "$BUILD_RC" -ne 0 ]; then
  echo "probe build failed" >&2
  exit 1
fi

ls -l "$EXE"
echo "== dependents =="
dumpbin.exe /nologo /dependents "$EXE" | sed -n "1,40p"
exit 0
