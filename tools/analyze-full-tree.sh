#!/usr/bin/env bash
# L4 (2026-11-19): run tools/analyze-one-target.sh over every configured target
# and write one summary row per target.
#
# Usage: tools/analyze-full-tree.sh [--stamp <name>] [--only <target-regex>]
# Env:   TF2_ROOT (repo root), ANALYZE_OUT (default /d/_shared/n23-analyze-<stamp>)
#
# Output: <out>/<stamp>/summary.tsv with "target tus warnings rc" plus the raw
# per-TU logs under <out>/<stamp>/<target>/. Exit 0 when every target ran, even
# if it has findings; exit 1 when a target could not run.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT="${TF2_ROOT:-$(cd "$HERE/.." && pwd)}"
BUILD="$ROOT/native/build-nmake"
STAMP=2026-11-19
ONLY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --stamp) STAMP="${2:?}"; shift 2 ;;
    --only)  ONLY="${2:?}"; shift 2 ;;
    *) echo "usage: $0 [--stamp <name>] [--only <regex>]" >&2; exit 2 ;;
  esac
done
OUTBASE="${ANALYZE_OUT:-/d/_shared/n23-analyze-$STAMP}"
mkdir -p "$OUTBASE"
TSV="$OUTBASE/summary.tsv"
printf 'target\ttus\twarnings\trc\n' > "$TSV"
# Enumerate with a shell glob + basename, not "ls | sed": ls given an absolute
# MSYS path prints Windows-style backslash paths, so a sed on '/' matched nothing
# and every target was silently skipped (the driver reported tus=0 in 3 s).
TARGETS=""
for d in "$BUILD"/CMakeFiles/*.dir; do
  [ -d "$d" ] || continue
  TARGETS="$TARGETS $(basename "$d" .dir)"
done
[ -n "$TARGETS" ] || { echo "NO-TARGETS $BUILD"; exit 1; }
echo "TARGETS=$(echo $TARGETS | wc -w) BUILD=$BUILD"
FAILED=0
for t in $TARGETS; do
  [ -f "$BUILD/CMakeFiles/$t.dir/flags.make" ] || continue
  [ -n "$ONLY" ] && ! printf '%s' "$t" | grep -qE "$ONLY" && continue
  echo "=== $t"
  log="/d/_shared/analyze-$STAMP-$t.log"
  ANALYZE_OUT="$OUTBASE" bash "$HERE/analyze-one-target.sh" "$t" > "$log" 2>&1
  rc=$?
  line=$(grep -E '^SUMMARY ' "$log" | tail -1)
  tus=$(printf '%s' "$line" | sed -n 's/.* tus=\([0-9]*\).*/\1/p'); tus=${tus:-0}
  warns=$(printf '%s' "$line" | sed -n 's/.* warnings=\([0-9]*\).*/\1/p'); warns=${warns:-0}
  [ -n "$line" ] || rc=1   # -s tests a file named by the SUMMARY text; -n is the intended "did the runner print a summary" test
  [ "$rc" -ne 0 ] && FAILED=1
  printf '%s\t%s\t%s\t%s\n' "$t" "$tus" "$warns" "$rc" >> "$TSV"
  echo "TARGET=$t tus=$tus warnings=$warns rc=$rc"
done
echo "---"
awk -F'\t' 'NR>1 {t+=$2; w+=$3; if ($4!=0) bad++} END {printf "TOTAL tus=%d warnings=%d failed=%d\n", t, w, bad+0}' "$TSV"
echo "SUMMARY-FILE $TSV"
[ "$FAILED" -eq 0 ] && echo "ANALYZE-FULL-TREE=OK" || echo "ANALYZE-FULL-TREE=PARTIAL"
exit "$FAILED"
