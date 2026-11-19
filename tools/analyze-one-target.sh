#!/usr/bin/env bash
# L4 (2026-11-19): MSVC /analyze on ONE target's translation units, using the
# exact flags the configured build uses, minus /WX (analysis findings are read,
# not turned into a build failure) and minus /Brepro.
#
# Usage: tools/analyze-one-target.sh <target> [name-filter-regex]
# Env:   TF2_ROOT      repo root (default: this script's parent)
#        ANALYZE_OUT   output dir   (default: /d/_shared/n23-analyze)
#
# Each TU prints "TU=<name> rc=<n> warnings=<n> secs=<n>"; the target ends with
# "SUMMARY target=<t> tus=<n> warnings=<n> out=<dir>". The XML logs MSVC writes
# (<out>/<tu>.xml) are the machine-readable form; <out>/<tu>.log keeps stdout.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT="${TF2_ROOT:-$(cd "$HERE/.." && pwd)}"
T="${1:?target}"
FILTER="${2:-}"
BUILD="$ROOT/native/build-nmake"
FLAGS="$BUILD/CMakeFiles/$T.dir/flags.make"
BASE_OUT="${ANALYZE_OUT:-/d/_shared/n23-analyze}"
OUT="$BASE_OUT/$T"
OUTW="$(printf '%s' "$OUT" | sed -e 's#^/d/#D:/#')"
rm -rf "$OUT"; mkdir -p "$OUT/objs"
[ -f "$FLAGS" ] || { echo "NO-FLAGS $FLAGS"; exit 2; }
. "$ROOT/env-msvc.sh" || { echo "NO-TOOLCHAIN"; exit 2; }
DEFS=$(sed -n 's/^CXX_DEFINES = //p' "$FLAGS")
# MSVC writes string-valued defines as -DNAME=\"value\" for the build tool to
# re-parse; passing that text through bash hands cl the backslashes literally and
# the macro expands to a bad escape (install_lifecycle.cpp:750 C2017). MSYS
# re-escapes a real quote on the Windows command line, so drop the backslash.
DEFS=$(printf '%s' "$DEFS" | sed -e 's/\\"/"/g')
INCS=$(sed -n 's/^CXX_INCLUDES = //p' "$FLAGS")
CFLAGS=$(sed -n 's/^CXX_FLAGS = //p' "$FLAGS" | sed -e 's#/WX##g' -e 's#/Brepro##g')
# -I with Windows backslash paths: convert to forward slashes so bash does not eat them
INCS=$(printf '%s' "$INCS" | sed -e 's#\\\\#/#g')
echo "TARGET=$T"
echo "FLAGS=$CFLAGS"
TOTAL=0; TUS=0
for obj in "$BUILD/CMakeFiles/$T.dir"/*/*.obj; do
  [ -f "$obj" ] || continue
  base=$(basename "$obj" .obj); base=${base%.cpp}
  [ -n "$FILTER" ] && ! printf '%s' "$base" | grep -qE "$FILTER" && continue
  src=$(find "$ROOT/native/src" "$ROOT/native/tools" -maxdepth 1 -name "$base.cpp" 2>/dev/null | head -1)
  [ -n "$src" ] || { echo "TU=$base SKIP=no-source"; continue; }
  start=$(date +%s)
  cl /nologo $DEFS $INCS $CFLAGS /analyze /analyze:external- /analyze:log "$OUTW/$base.xml" /c "$src" /Fo"$OUTW/objs/$base.obj" > "$OUT/$base.log" 2>&1
  rc=$?
  secs=$(( $(date +%s) - start ))
  warns=$(grep -cE 'warning C[0-9]+' "$OUT/$base.log" || true)
  TUS=$((TUS+1)); TOTAL=$((TOTAL+warns))
  echo "TU=$base rc=$rc warnings=$warns secs=$secs"
done
echo "SUMMARY target=$T tus=$TUS warnings=$TOTAL out=$OUT"
