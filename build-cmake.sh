#!/usr/bin/env bash
# Reproducible CMake build for this machine.
#
# WHY THIS FILE EXISTS
#   The handoff checklist asks for:
#       cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
#   That generator cannot work here. Two separate blockers, both verified:
#     1. CMake's VS generator runs vcvars64.bat, which calls reg.exe. reg.exe is
#        on this sandbox's command blacklist, so compiler detection fails with
#        "No CMAKE_CXX_COMPILER could be found". Passing
#        -DCMAKE_GENERATOR_INSTANCE=<VS path> does not help (vswhere.exe itself
#        runs fine and returns C:\Program Files\Microsoft Visual Studio\2022\Community).
#     2. The NMake generator DOES work, but only after two non-obvious fixes:
#          a. INCLUDE/LIB must be set explicitly (vcvars64.bat is unusable), with
#             Windows-style paths -- MSYS /c/... paths are not accepted.
#          b. The Windows SDK bin directory must be on PATH, otherwise rc.exe and
#             mt.exe are missing and CMake reports CMAKE_MT-NOTFOUND and the
#             link step dies with "no such file or directory".
#
# USAGE
#   bash build-cmake.sh            # configure (if needed) + build Release
#   bash build-cmake.sh configure  # configure only
#   bash build-cmake.sh clean      # remove the build tree
#
# OUTPUT
#   native/build-nmake/*.exe   (Release)
set -uo pipefail
cd "$(dirname "$0")"

MSVC_VER="14.44.35207"
SDK_VER="10.0.26100.0"
MSVC_W="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/$MSVC_VER"
SDK_W="C:/Program Files (x86)/Windows Kits/10"
MSVC_ROOT="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/$MSVC_VER"
SDK_BIN="/c/Program Files (x86)/Windows Kits/10/bin/$SDK_VER/x64"

export PATH="$MSVC_ROOT/bin/Hostx64/x64:$SDK_BIN:$PATH"
export INCLUDE="$MSVC_W/include;$SDK_W/Include/$SDK_VER/ucrt;$SDK_W/Include/$SDK_VER/shared;$SDK_W/Include/$SDK_VER/um;$SDK_W/Include/$SDK_VER/winrt"
export LIB="$MSVC_W/lib/x64;$SDK_W/Lib/$SDK_VER/ucrt/x64;$SDK_W/Lib/$SDK_VER/um/x64"

BUILD=native/build-nmake
LOG=evidence/cmake-build.log
mkdir -p evidence

case "${1:-all}" in
  clean)
    rm -rf "$BUILD"; echo "removed $BUILD"; exit 0 ;;
  configure)
    cmake -S native -B "$BUILD" -G "NMake Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl 2>&1 | tee "$LOG"
    exit "${PIPESTATUS[0]}" ;;
esac

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  echo "=== configure ==="
  cmake -S native -B "$BUILD" -G "NMake Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl 2>&1 | tee "$LOG"
fi

echo "=== build (Release, parallel 2) ==="
cmake --build "$BUILD" --config Release --parallel 2 2>&1 | tee -a "$LOG"
rc=${PIPESTATUS[0]}

echo "=== summary ==="
echo "cmake_build_rc=$rc"
echo "errors=$(grep -c 'error C' "$LOG" || true)"
echo "warnings=$(grep -c 'warning C' "$LOG" || true)"
grep -E 'error C|warning C' "$LOG" | head -30
ls -la "$BUILD"/*.exe 2>/dev/null | wc -l | sed 's/^/exe_count=/'
exit "$rc"
