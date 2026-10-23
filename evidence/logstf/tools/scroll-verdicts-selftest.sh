#!/usr/bin/env bash
# scroll-verdicts-selftest.sh -- the negative controls, without a lock.
#
# WHY THIS EXISTS
#   The 2026-10-23 defect was that "the scroll offset is correct" held at 0 == 0.
#   The fix is a set of assertions that can tell "nothing was read", "the answer
#   was not a number" and "the page did not move" apart. Those assertions must be
#   provable in two seconds, not only after a twenty-minute locked run: a check
#   that can only be exercised by the expensive path is a check nobody re-runs,
#   and the next person to break the injection would learn about it from a user.
#
#   It extracts scroll_verdicts and _scroll_check FROM THE GATE (not a copy), so
#   this tool fails when the gate changes and the check does not follow.
#
# WHAT IT PROVES
#   Four recorded product reading lines -- one healthy, three from the mutation
#   suite -- produce exactly the verdict pattern each defect must produce. It
#   runs no browser and needs no lock: it is a test of the ASSERTIONS, not of the
#   product. A green run of this tool is NOT a green gate.
set -uo pipefail
cd /d/TF2_Native_Worktrees/t54-logstf || exit 90

GATE=logstf-check.sh
[ -f "$GATE" ] || { echo "FATAL: no gate to read"; exit 3; }

# Extract the helpers from the gate itself. sed range extraction is brittle to
# braces, so take from the named start to the next blank line after the closing
# brace of each function -- the gate keeps those functions on their own lines.
fn_block() { # fn_block <name>
  sed -n "/^$1() {/,/^}/p" "$GATE"
}
pvalue_line="$(sed -n '/^pvalue() {/,/^}/p' "$GATE")"
check_line="$(fn_block _scroll_check)"
verdicts_line="$(fn_block scroll_verdicts)"
[ -n "$check_line" ] || { echo "FATAL: _scroll_check not found in $GATE"; exit 3; }
[ -n "$verdicts_line" ] || { echo "FATAL: scroll_verdicts not found in $GATE"; exit 3; }
[ -n "$pvalue_line" ] || { echo "FATAL: pvalue not found in $GATE"; exit 3; }
eval "$pvalue_line"
eval "$check_line"
eval "$verdicts_line"

# One healthy line and three defective ones, all copied VERBATIM from the
# 2026-10-23 locked run (.scratch/t54-locked-20261023-131223). A control that
# uses invented input proves the assertions reject invented input.
read -r -d '' HEALTHY <<'EOF'
logstf: summary attached=1 script=scroll state=content host=alive panel=640 content=637 client=1264,681 dpiScale=1.00 url=https://logs.tf/1 navigations=1 accepted=1 refused=0 opens=1 closes=0 hostStarts=1 hostFailures=0 scrollCalls=1 scriptFailures=0 canBack=0 canForward=0 refusal=- refusalReason=- unavailable=- scrollOffset=480 scrollRaw=480.0000305175781 scrollPage=1729 scrollViewport=608 urlBeforeHistory=- urlAfterHistory=- historyMoved=0
EOF
# m6: the script call is counted and never issued -- the counters are green.
read -r -d '' NO_SCRIPT <<'EOF'
logstf: summary attached=1 script=scroll state=content host=alive panel=640 content=637 client=1264,681 dpiScale=1.00 url=https://logs.tf/1 navigations=1 accepted=1 refused=0 opens=1 closes=0 hostStarts=1 hostFailures=0 scrollCalls=1 scriptFailures=0 canBack=0 canForward=0 refusal=- refusalReason=- unavailable=- scrollOffset=0 scrollRaw=none scrollPage=1729 scrollViewport=608 urlBeforeHistory=- urlAfterHistory=- historyMoved=0
EOF
# m8: the browser JSON envelope was not decoded -- a quoted, unparsable answer.
read -r -d '' UNDECODED <<'EOF'
logstf: summary attached=1 script=scroll state=content host=alive panel=640 content=637 client=1264,681 dpiScale=1.00 url=https://logs.tf/1 navigations=1 accepted=1 refused=0 opens=1 closes=0 hostStarts=1 hostFailures=0 scrollCalls=1 scriptFailures=0 canBack=0 canForward=0 refusal=- refusalReason=- unavailable=- scrollOffset=0 scrollRaw="480.0000305175781" scrollPage=0 scrollViewport=608 urlBeforeHistory=- urlAfterHistory=- historyMoved=0
EOF
# m9: the script was truncated -- the page answered the JSON literal null.
read -r -d '' TRUNCATED <<'EOF'
logstf: summary attached=1 script=scroll state=content host=alive panel=640 content=637 client=1264,681 dpiScale=1.00 url=https://logs.tf/1 navigations=1 accepted=1 refused=0 opens=1 closes=0 hostStarts=1 hostFailures=0 scrollCalls=1 scriptFailures=0 canBack=0 canForward=0 refusal=- refusalReason=- unavailable=- scrollOffset=0 scrollRaw=null scrollPage=1729 scrollViewport=608 urlBeforeHistory=- urlAfterHistory=- historyMoved=0
EOF

# If the last locked run left its traces behind, use THEM instead of the pasted
# lines below. Pasted evidence drifts; the trace is what the product wrote.
from_trace() { # from_trace <file>
  [ -f "$1" ] || return 0
  grep -F "logstf: summary" "$1" 2>/dev/null | tail -1
}
LIVE_SCROLL="$(from_trace .scratch/logstf/scroll.trace)"
LIVE_M6="$(from_trace .scratch/logstf/m6.trace)"
LIVE_M8="$(from_trace .scratch/logstf/m8.trace)"
LIVE_M9="$(from_trace .scratch/logstf/m9.trace)"
rc=0
# _scroll_check assigns to `fail`; the gate defines it at file scope, so this
# tool must too. (set -u turns the omission into an abort on the FIRST case,
# which is the good direction, but it aborted before printing a verdict.)
fail=0

# expect <label> <line> <wanted first red | NONE>
expect() {
  local label="$1" line="$2" want="$3"
  local saved_fail=$fail
  fail=0
  scroll_verdicts "$line" > /dev/null 2>&1
  local got="$SCROLL_FIRST_BAD"
  fail=$saved_fail
  if [ "$want" = "NONE" ]; then
    if [ "$got" = "none" ]; then
      echo "  OK   $label: every scroll verdict held"
    else
      echo "  FAIL $label: expected no red, got $got"; rc=1
    fi
  elif [ "$got" = "$want" ]; then
    echo "  OK   $label: first red = $got"
  else
    echo "  FAIL $label: expected first red $want, got $got"; rc=1
  fi
}

if [ -n "$LIVE_SCROLL" ] && [ -n "$LIVE_M6" ] && [ -n "$LIVE_M8" ] && [ -n "$LIVE_M9" ]; then
  HEALTHY="$LIVE_SCROLL"; NO_SCRIPT="$LIVE_M6"; UNDECODED="$LIVE_M8"; TRUNCATED="$LIVE_M9"
  echo "source: the last run's traces (.scratch/logstf/*.trace)"
else
  echo "source: the pasted lines recorded from .scratch/t54-locked-20261023-131223"
  echo "        (no traces on disk: this is a replay of recorded text, not of files)"
fi

echo "=== scroll verdicts: the healthy line vs the three defect lines ==="
expect "healthy run"            "$HEALTHY"    NONE
expect "script never ran (m6)"  "$NO_SCRIPT"  scroll_offset_was_read
expect "JSON not decoded (m8)"  "$UNDECODED"  scroll_raw_is_number
expect "script truncated (m9)"  "$TRUNCATED"  scroll_raw_is_number

# The counters that make the defect invisible must be IDENTICAL in all four: if
# they differed, the old assertions could have caught it and this control would
# prove nothing about why they did not.
counters() { printf '%s\n' "$1" | grep -oE 'scrollCalls=[0-9]+ scriptFailures=[0-9]+'; }
base_c="$(counters "$HEALTHY")"
same=1
for l in "$NO_SCRIPT" "$UNDECODED" "$TRUNCATED"; do
  [ "$(counters "$l")" = "$base_c" ] || same=0
done
if [ "$same" -eq 1 ]; then
  echo "  OK   all four lines carry the SAME counters ($base_c): the defect was"
  echo "       invisible to scrollCalls and scriptFailures"
else
  echo "  FAIL the control lines do not share the healthy line counters;"
  echo "       this control does not show why the old assertions missed it"; rc=1
fi

if [ "$rc" -eq 0 ]; then
  echo "SCROLL-VERDICTS-SELFTEST=PASS (4 cases, extracted from $GATE)"
  exit 0
fi
echo "SCROLL-VERDICTS-SELFTEST=FAIL"
exit 1