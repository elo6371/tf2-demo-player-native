#!/usr/bin/env bash
# build-target.sh <target> [target...]  -- build specific CMake targets.
set -uo pipefail
cd "$(dirname "$0")"
source ./env-msvc.sh
cmake -S native -B native/build-nmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl > /dev/null 2>&1
for t in "$@"; do
  echo "=== build $t ==="
  cmake --build native/build-nmake --target "$t" 2>&1 | grep -E "error C|warning C|error MSB|fatal error" | head -20
  echo "rc=${PIPESTATUS[0]}"
done
