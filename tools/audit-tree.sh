#!/usr/bin/env bash
# audit-tree.sh -- lead-side guard: compare the live tree against the frozen baseline
# taken before any subagent started (D:/_shared/EXEC-2026-11-15/LEAD/).
#
# WHY: eight subagents are editing one worktree. The expensive failure mode is not a
# bad report, it is a quiet edit to a file someone else owns (main.cpp, CMake, fixtures,
# evidence/). This script names every file that moved and flags the forbidden ones.
#
# Usage: bash tools/audit-tree.sh [--write-report]
set -u
cd "$(dirname "$0")/.." || exit 2
BASE=/d/_shared/EXEC-2026-11-15/LEAD
OUT=$BASE/audit-$(date -u +%Y%m%dT%H%M%SZ).txt

FORBIDDEN=(
  "native/src/main_camera.cpp"
  "native/include/main_camera.h"
  "native/src/demo_header_entity.cpp"
  "native/include/demo_header.h"
  "CMakeLists.txt"
  "native/CMakeLists.txt"
)

{
  echo "== audit $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
  echo "HEAD: $(git rev-parse --short HEAD)  (baseline: $(cat $BASE/baseline-head.txt | cut -c1-7))"
  echo
  echo "-- native/src + include changes vs baseline --"
  find native/src native/include -name '*.cpp' -o -name '*.h' | sort | xargs sha256sum 2>/dev/null > "$OUT.src" || true
  if diff -q "$BASE/baseline-native-src.sha256" "$OUT.src" >/dev/null 2>&1; then
    echo "  (no source changes)"
  else
    diff "$BASE/baseline-native-src.sha256" "$OUT.src" | grep -E '^[<>]' | sed 's/^/  /'
  fi
  echo
  echo "-- git status --"
  git status --porcelain | sed 's/^/  /'
  echo
  echo "-- forbidden-path check --"
  local_hit=0
  for f in "${FORBIDDEN[@]}"; do
    if ! git diff --quiet -- "$f" 2>/dev/null; then
      echo "  VIOLATION: $f changed"
      local_hit=1
    fi
  done
  # main.cpp is owned by exactly one agent (n12-camera); any change is expected there,
  # so report it as INFO with the line count rather than a violation.
  if ! git diff --quiet -- native/src/main.cpp 2>/dev/null; then
    echo "  INFO: native/src/main.cpp changed ($(git diff --numstat -- native/src/main.cpp | awk '{print $1"+"$2}') lines) -- owner: n12-camera"
  fi
  for d in native/tests/fixtures evidence; do
    if [ -n "$(git status --porcelain -- "$d" 2>/dev/null)" ]; then
      echo "  VIOLATION: $d is dirty (lockrun.sh will make every gate exit 97)"
      local_hit=1
    fi
  done
  echo
  if [ "$local_hit" = "0" ]; then echo "VERDICT: OK (no forbidden writes)"; else echo "VERDICT: VIOLATION"; fi
} | tee "$OUT"

rm -f "$OUT.src"
if [ "${1:-}" = "--write-report" ]; then echo "report: $OUT"; fi
exit 0
