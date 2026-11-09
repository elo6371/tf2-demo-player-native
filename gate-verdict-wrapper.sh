#!/usr/bin/env bash
# gate-verdict-wrapper.sh -- run one gate beside the chain and report what
# actually happened: its own exit code, how it ended, and its own verdict line.
#
# What this is. The run-9 bypass wrapper parsed the gate with
#   gate | sed ... | while read line; do ...; done
# so the parser's state lived in a pipeline subshell: the gate passed, the
# wrapper printed VERDICT=MISSING and exited 99, and the only surviving record
# of the pass was the full log (RUN9-REPORT section 6). A wrapper that invents
# its own status is worse than no wrapper -- it makes 'the gate crashed', 'the
# gate timed out', 'a human cancelled it' and 'the gate never ran' into one
# number that the driver cannot tell apart.
#
# What this does instead:
#   * runs the gate with its streams teed to a log and keeps the gate's own
#     exit code (PIPESTATUS[0]) -- it never substitutes one of its own;
#   * keeps the gate's last verdict token line verbatim (TOKEN=VALUE), read
#     from the lines this run appended, not from a reused log;
#   * classifies the ending from exit code plus verdict (passed, failed,
#     verdict-disagrees, verdict-missing, refused, timed-out, killed,
#     interrupted, not-executable, not-executed, crashed);
#   * prints one WRAPPER-* block and exits with the gate's code.
#
# Usage: bash gate-verdict-wrapper.sh [--log FILE] [--token TOKEN] [--label NAME] -- <command...>
#        bash gate-verdict-wrapper.sh --selftest
# Exit:  the gate's exit code, unchanged (never 99 by construction).
set -uo pipefail
SELF="${BASH_SOURCE[0]}"

LOG=""
TOKEN=""
LABEL=""
SELFTEST=0
while [ $# -gt 0 ]; do
  case "$1" in
    --log) LOG="${2:-}"; shift 2 2>/dev/null || shift ;;
    --log=*) LOG="${1#*=}"; shift ;;
    --token) TOKEN="${2:-}"; shift 2 2>/dev/null || shift ;;
    --token=*) TOKEN="${1#*=}"; shift ;;
    --label) LABEL="${2:-}"; shift 2 2>/dev/null || shift ;;
    --label=*) LABEL="${1#*=}"; shift ;;
    --selftest) SELFTEST=1; shift ;;
    --) shift; break ;;
    *) break ;;
  esac
done

classify_verdict() { # classify_verdict <verdict value> -> passed|failed|unknown
  case "$1" in
    *FAIL*|*RED*|*BROKEN*|*ERROR*|*REFUSED*) echo failed ;;
    *PASS*|*OK*|*GREEN*) echo passed ;;
    *) echo unknown ;;
  esac
}

if [ "$SELFTEST" -eq 1 ]; then
  tmp=$(mktemp -d "${TMPDIR:-/tmp}/gvw-selftest.XXXXXX") || exit 1
  st=0; ncase=0
  mkdir -p "$tmp/a-directory"   # exec of a directory is the portable rc=126 case
  case_run() { # case_run <label> <want-rc> <want-reason> <want-verdict-substr> -- <cmd...>
    local label="$1" want_rc="$2" want_reason="$3" want_verdict="$4" out got reason verdict
    shift 5
    ncase=$((ncase + 1))
    out=$(bash "$SELF" --log "$tmp/$label.log" --label "$label" -- "$@" 2>&1); got=$?
    reason=$(printf '%s\n' "$out" | sed -n 's/^WRAPPER-REASON=//p' | tail -1)
    verdict=$(printf '%s\n' "$out" | sed -n 's/^WRAPPER-VERDICT=//p' | tail -1)
    if [ "$got" = "$want_rc" ] && [ "$reason" = "$want_reason" ] \
       && printf '%s' "$verdict" | grep -qF -- "$want_verdict"; then
      echo "  OK   $label: exit=$got reason=$reason verdict=$verdict"
    else
      echo "  FAIL $label: exit=$got (want $want_rc) reason=$reason (want $want_reason) verdict=$verdict (want *$want_verdict*)"
      st=1
    fi
  }
  echo "=== wrapper self-test: every ending keeps its own name and code ==="
  case_run pass 0 passed 'VERIFY=PASS' -- bash -c 'echo "VERIFY=PASS"; exit 0'
  case_run failed 1 failed 'VERIFY=FAIL' -- bash -c 'echo "VERIFY=FAIL"; exit 1'
  case_run verdict-disagrees 0 verdict-disagrees 'VERIFY=FAIL' -- bash -c 'echo "VERIFY=FAIL"; exit 0'
  case_run verdict-missing 0 verdict-missing 'MISSING' -- bash -c 'echo "all quiet"; exit 0'
  case_run timed-out 124 timed-out 'STEP=start' -- bash -c 'echo "STEP=start"; exit 124'
  case_run killed 137 killed 'STEP=start' -- bash -c 'echo "STEP=start"; exit 137'
  case_run interrupted-sigterm 143 interrupted 'MISSING' -- bash -c 'kill -TERM $$'
  case_run interrupted-sigint 130 interrupted 'MISSING' -- bash -c 'kill -INT $$'
  case_run crashed 139 crashed 'MISSING' -- bash -c 'kill -SEGV $$'
  case_run refused 2 refused 'MISSING' -- bash -c 'echo "FATAL: busy"; exit 2'
  case_run not-executable 126 not-executable 'MISSING' -- "$tmp/a-directory"
  case_run not-executed 127 not-executed 'MISSING' -- "$tmp/definitely-not-a-gate.sh"
  # An outer timeout around the wrapper must not become "no verdict": either the
  # wrapper never printed its block (the outer rc=124 is then the record), or it
  # printed the ending it saw. A fabricated MISSING is the one thing that cannot
  # happen, because the block is only printed after the gate has returned.
  ncase=$((ncase + 1))
  out=$(timeout 5 bash "$SELF" --log "$tmp/outer-timeout.log" -- bash -c "echo STEP=start; sleep 60" 2>&1); got=$?
  if [ "$got" = "124" ] && ! printf "%s" "$out" | grep -q "WRAPPER-REASON=verdict-missing"; then
    echo "  OK   outer-timeout: outer exit=$got, no fabricated verdict-missing"
  else
    echo "  FAIL outer-timeout: exit=$got (want 124) or a fabricated verdict-missing was printed"; st=1
  fi
  if [ "$st" -eq 0 ]; then
    echo "WRAPPER-SELFTEST=PASS ($ncase/$ncase cases)"
    rm -rf "$tmp"; exit 0
  else
    echo "WRAPPER-SELFTEST=FAIL"; rm -rf "$tmp"; exit 1
  fi
fi

if [ $# -eq 0 ]; then
  echo "FATAL: no command given (usage: gate-verdict-wrapper.sh [--log FILE] [--token TOKEN] -- <command...>)" >&2
  echo "WRAPPER-EXIT=2"
  echo "WRAPPER-REASON=usage"
  echo "WRAPPER-VERDICT=MISSING"
  exit 2
fi

LABEL_SAFE=$(printf '%s' "${LABEL:-gate}" | tr -c 'A-Za-z0-9._-' '_')
if [ -z "$LOG" ]; then
  LOG="${TMPDIR:-/tmp}/gate-verdict-${LABEL_SAFE}-$(date +%Y%m%dT%H%M%S)-$$.log"
fi
mkdir -p "$(dirname "$LOG")" 2>/dev/null || true
start_line=0
if [ -f "$LOG" ]; then start_line=$(wc -l < "$LOG"); fi

t0=$(date +%s)
"$@" 2>&1 | tee -a "$LOG"
rc=${PIPESTATUS[0]}
t1=$(date +%s)
elapsed=$((t1 - t0))

# The verdict is the last token line this run appended. A reused --log must not
# hand back the previous run's verdict, and this wrapper's own WRAPPER- lines
# must never be mistaken for one.
verdict=""
if [ -n "$TOKEN" ]; then
  verdict=$(tail -n +"$((start_line + 1))" "$LOG" | grep -aE "^${TOKEN}=" | tail -1)
else
  verdict=$(tail -n +"$((start_line + 1))" "$LOG" | grep -aE '^[A-Z][A-Z0-9_-]*=' | grep -avE '^WRAPPER-' | tail -1)
fi
verdict_word=""
if [ -n "$verdict" ]; then verdict_word="${verdict#*=}"; fi

reason=""; signal_no=""; note=""
case "$rc" in
  0)
    if [ -z "$verdict" ]; then
      reason="verdict-missing"; note="the gate exited 0 but printed no TOKEN=VALUE line"
    else
      case "$(classify_verdict "$verdict_word")" in
        passed) reason="passed" ;;
        failed) reason="verdict-disagrees"; note="the gate printed a failing verdict but exited 0" ;;
        *)      reason="passed-unrecognised-verdict"; note="exit 0 with a verdict this wrapper does not classify" ;;
      esac
    fi ;;
  2)   reason="refused" ;;
  124) reason="timed-out" ;;
  126) reason="not-executable" ;;
  127) reason="not-executed" ;;
  130) reason="interrupted"; signal_no=2 ;;
  137) reason="killed" ;;
  143) reason="interrupted"; signal_no=15 ;;
  *)
    if [ "$rc" -ge 128 ]; then
      reason="crashed"; signal_no=$((rc - 128))
    else
      reason="failed"
    fi ;;
esac

echo "WRAPPER-LABEL=${LABEL:-}"
echo "WRAPPER-EXIT=$rc"
echo "WRAPPER-REASON=$reason"
echo "WRAPPER-VERDICT=${verdict:-MISSING}"
echo "WRAPPER-ELAPSED=${elapsed}s"
if [ -n "$signal_no" ]; then echo "WRAPPER-SIGNAL=$signal_no"; fi
if [ -n "$note" ]; then echo "WRAPPER-NOTE=$note"; fi
echo "WRAPPER-LOG=$LOG"
exit "$rc"
