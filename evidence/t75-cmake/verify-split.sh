#!/usr/bin/env bash
# =============================================================================
# T75 Phase 1 equivalence verifier -- entry point.
#
#   bash .scratch/t75-cmake/verify-split.sh
#
# Configures the pre-split build script and the split build script into two
# separate build trees and proves the generated build systems are identical.
# It never builds: no cmake --build, no nmake, no link, no product executable.
#
# The checks themselves live in verify_split.py next to this file; this wrapper
# only supplies the MSVC environment that build-cmake.sh exports (PATH, INCLUDE,
# LIB) and hands over. Exit code 0 means zero differences.
# =============================================================================
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

# Same environment block build-cmake.sh uses. It exports the compiler
# environment only; it does not build anything.
# shellcheck disable=SC1091
source "$ROOT/env-msvc.sh"

exec python "$HERE/verify_split.py" "$@"
