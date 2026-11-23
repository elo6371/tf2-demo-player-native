#!/usr/bin/env bash
# tools/gate-lint.sh -- thin launcher for tools/gate_lint.py (rules, scope and
# allow list are documented there).
#
# 2026-11-23: the lint used to be written in bash. On this host it printed no
# verdict, exited 0 and wrote ~116 lines of "unbound variable" / "command not
# found" -- gate text was being run as shell -- so every earlier GATE-LINT=PASS
# is unproven. The rules now live in Python, which only ever reads the gates as
# data, and this wrapper refuses to report success unless the linter printed
# exactly one verdict line that agrees with its exit code.
#
# Usage: bash tools/gate-lint.sh [--selftest | FILE...]
# Exit:  0 clean, 1 findings, 2 usage/input error or no trustworthy verdict.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2

PY=""
for candidate in "${GATE_LINT_PYTHON:-}" \
  "C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe" \
  "C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe" \
  python python3 py; do
  [ -n "$candidate" ] || continue
  if [ -x "$candidate" ] || command -v "$candidate" >/dev/null 2>&1; then PY="$candidate"; break; fi
done
if [ -z "$PY" ]; then
  echo "GATE-LINT=FAIL (no python found; set GATE_LINT_PYTHON)"
  exit 2
fi

out="$("$PY" tools/gate_lint.py "$@" 2>&1)"
rc=$?
printf '%s\n' "$out"

token="GATE-LINT="
[ "${1:-}" = "--selftest" ] && token="GATE-LINT-SELFTEST="
verdicts=$(printf '%s\n' "$out" | grep -c "^$token")
last=$(printf '%s\n' "$out" | grep "^$token" | tail -n 1)
if [ "$verdicts" -ne 1 ]; then
  echo "GATE-LINT=FAIL (expected one ${token} line from the linter, saw $verdicts; python rc=$rc)"
  exit 2
fi
case "$last" in
  *=PASS*) [ "$rc" -eq 0 ] && exit 0 ;;
  *=FAIL*) [ "$rc" -eq 1 ] && exit 1 ;;
esac
echo "GATE-LINT=FAIL (verdict and exit code disagree: '$last' rc=$rc)"
exit 2
