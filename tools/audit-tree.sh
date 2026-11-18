#!/usr/bin/env bash
# audit-tree.sh -- lead-side guard: compare the live tree against the frozen baseline
# taken before any subagent started (D:/_shared/EXEC-2026-11-15/LEAD/).
#
# WHY: several subagents edit one worktree. The expensive failure modes are not a bad
# report; they are (a) a quiet edit to a file someone else owns (main.cpp, CMake,
# fixtures, evidence/) and (b) a whole-file rewrite that only changes line endings,
# which makes every later diff unreadable.
#
# Usage: bash tools/audit-tree.sh [--write-report]
set -u
cd "$(dirname "$0")/.." || exit 2
BASE=/d/_shared/EXEC-2026-11-15/LEAD
OUT=$BASE/audit-$(date -u +%Y%m%dT%H%M%SZ).txt

# Ownership as of 2026-11-15 20:40. A path listed here may NOT change without the
# lead reassigning it; a path in OWNED is expected to be changing and is reported
# as INFO with its real (ignore-CR) line counts.
# Files the lead does not own: CRLF here is a decision, not damage. Both are
# documents written by the product owner, not by an agent.
EOL_EXEMPT="docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md docs/PROJECT-HANDOFF-2026-11-14.md"

FORBIDDEN=(
  "native/src/main_camera.cpp"
  "native/include/main_camera.h"
  "CMakeLists.txt"
  "native/CMakeLists.txt"
)
OWNED=(
  "native/include/demo_header.h"
  "native/src/demo_header.cpp"
  "native/src/demo_header_entity.cpp"
  "native/src/demo_header_camera.cpp"
  "native/cmake/tf2-libraries.cmake"
  "native/tools/demo_open_probe.cpp"
  "native/src/main.cpp"
  "native/tools/mf_export_probe.cpp"
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
      echo "  VIOLATION: $f changed (not assigned to any agent)"
      local_hit=1
    fi
  done
  for f in "${OWNED[@]}"; do
    if ! git diff --quiet -- "$f" 2>/dev/null; then
      echo "  INFO: $f changed ($(git diff HEAD --ignore-cr-at-eol --numstat -- "$f" | awk '{print "+"$1" -"$2}') real lines, $(git diff HEAD --numstat -- "$f" | awk '{print $1"+ "$2"-"}') raw)"
    else
      [ -e "$f" ] || echo "  INFO: $f does not exist yet"
    fi
  done
  for d in native/tests/fixtures evidence; do
    if [ -n "$(git status --porcelain -- "$d" 2>/dev/null)" ]; then
      echo "  VIOLATION: $d is dirty (lockrun.sh will make every gate exit 97)"
      local_hit=1
    fi
  done
  echo
  echo "-- line-ending check (raw diff vs ignore-CR diff) --"
  # A file whose raw diff is far larger than its ignore-CR diff was rewritten with
  # CRLF. The repository stores LF; such a rewrite makes every historical line look
  # changed. Measured 2026-11-15 on six files written by an agent.
  eol_hit=0
  for f in $(git status --porcelain | awk '{print $2}' | grep -E '\.(c|cc|cpp|h|hpp|sh|md|txt|cmake|py)$' || true); do
    [ -f "$f" ] || continue
    case " $EOL_EXEMPT " in *" $f "*) continue ;; esac
    cr=$(tr -cd '\r' < "$f" | wc -c | tr -d ' ')
    headcr=$(git show "HEAD:$f" 2>/dev/null | tr -cd '\r' | wc -c | tr -d ' ')
    [ -z "$headcr" ] && headcr=0
    if [ "$cr" != "0" ] && [ "$headcr" != "$cr" ]; then
      raw=$(git diff --numstat -- "$f" | awk '{print $1+$2}')
      real=$(git diff --ignore-cr-at-eol --numstat -- "$f" | awk '{print $1+$2}')
      echo "  VIOLATION: $f has $cr CR (HEAD had $headcr); raw diff $raw lines vs real $real"
      eol_hit=1
    fi
  done
  [ "$eol_hit" = "0" ] && echo "  (every changed text file matches HEAD's line endings)"
  echo
  if [ "$local_hit" = "0" ] && [ "$eol_hit" = "0" ]; then
    echo "VERDICT: OK (no forbidden writes, line endings intact)"
  else
    echo "VERDICT: $([ "$eol_hit" = "0" ] || echo "CRLF-DAMAGE ") $([ "$local_hit" = "0" ] || echo "FORBIDDEN-WRITE")"
  fi
} | tee "$OUT"

rm -f "$OUT.src"
if [ "${1:-}" = "--write-report" ]; then echo "report: $OUT"; fi
exit 0
