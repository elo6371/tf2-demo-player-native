#!/usr/bin/env bash
# mf_export_probe.sh -- build and run native/tools/mf_export_probe.cpp.
#
# WHY NOT CMAKE
#   native/CMakeLists.txt is being edited by another instance in this batch, and
#   this probe is throwaway evidence, not a product target. So cl.exe is invoked
#   directly with the same environment native/env-msvc.sh sets up for cmake.
#
# ARTIFACTS
#   Default output directory: D:/_shared/EXEC-2026-11-15/mf-probe (outside the
#   repository, so a probe run never leaves binaries in the worktree).
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ROOT_W="$(cd "$HERE/../.." && pwd -W 2>/dev/null || echo "$ROOT")"

# shellcheck source=/dev/null
source "$ROOT/env-msvc.sh"

OUT="${1:-D:/_shared/EXEC-2026-11-15/mf-probe}"
mkdir -p "$OUT"

SRC="$ROOT_W/native/tools/mf_export_probe.cpp"
EXE="$OUT/mf_export_probe.exe"

echo "== cl.exe =="
cl.exe /nologo /std:c++17 /EHsc /O2 /W4 \
  "$SRC" \
  /Fe:"$EXE" \
  /Fo:"$OUT/" \
  /link mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib user32.lib
BUILD_RC=$?
echo "build_rc=$BUILD_RC"
if [ "$BUILD_RC" -ne 0 ]; then
  echo "probe build failed" >&2
  exit 1
fi

echo "== dependents (evidence for Q7: system libs only) =="
dumpbin.exe /nologo /dependents "$EXE" | sed -n '1,60p'

exit 0
