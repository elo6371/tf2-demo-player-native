#!/usr/bin/env bash
# tools/asan-build.sh -- build the ASan subset (core + the model/material probes).
#
# WHY THIS FILE EXISTS
#   The sanitizer readings in docs/ASAN-MODEL-MATERIAL-2026-11-19.md have to be
#   reproducible from the repo, not from a scratch directory that only exists on
#   this machine. The build itself still wants a separate tree (objects built with
#   /fsanitize=address are not interchangeable with the normal ones), so this
#   script owns the configure step and the target list, and the tree location is
#   an override (ASAN_BUILD_DIR).
#
# USAGE
#   tools/asan-build.sh                # configure if needed, build the subset
#   ASAN_BUILD_DIR=/d/x tools/asan-build.sh
#   ASAN_TARGETS="tf2_core demo_open_probe" tools/asan-build.sh
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO" || exit 1
. ./tools/tf2-toolchain.sh || { echo "NO-TOOLCHAIN" >&2; exit 1; }

B="${ASAN_BUILD_DIR:-/d/_shared/n23-asan-build}"
TARGETS="${ASAN_TARGETS:-tf2_core model_robustness_probe material_chain_probe demo_open_probe}"

if [ ! -f "$B/CMakeCache.txt" ]; then
  echo "CONFIGURE $B (fresh)"
  cmake -S native -B "$B" -G "NMake Makefiles" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl \
    -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi" \
    -DCMAKE_EXE_LINKER_FLAGS="/DEBUG" 2>&1 | tail -8
  rc=${PIPESTATUS[0]}
  echo "CONFIGURE-RC=$rc"
  [ "$rc" -eq 0 ] || exit "$rc"
fi

cmake --build "$B" --target $TARGETS 2>&1 | grep -E "error| Built target |Linking CXX executable" | tail -30
echo "BUILD-RC=${PIPESTATUS[0]}"
ls -l "$B"/*.exe | awk '{print $5, $9}'
