#!/usr/bin/env bash
# verify-release.sh -- the full acceptance chain, serially, in one timed run.
#
# WHY THIS EXISTS
# docs/RELEASE-POLICY.md ran the "full chain" through verify-all.sh alone. That
# is 30 steps calling 29 of the 99 gate scripts in this tree. The four P0 gates
# named by the handoff -- outline, third-person weapon, body appearance, class
# pixels -- are NOT in verify-all.sh (grep -c returns 0 for all four), and the
# seven standalone mutation harnesses are not either. A green VERIFY=PASS was
# therefore being read as "the acceptance chain passed" while the four gates the
# project called P0 had not run at all (debt D-19). This script is the entry
# point that actually runs them, and it says so in one machine-readable line.
#
# It does NOT change verify-all.sh (its step numbering and pinned counts stand)
# and does NOT change any gate script. It only sequences them.
#
# USAGE (the chain only runs inside the host lock; this script refuses otherwise)
#
#   cd /d/TF2_Native_Worktrees/t86-phase0
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run MY-OWNER \
#     env VERIFY_RELEASE_OWNER=MY-OWNER bash verify-release.sh
#
#   VERIFY_RELEASE_DRY_RUN=1 bash verify-release.sh     # print the plan, run nothing
#
# TIMEOUTS (seconds, per step; a step that hits its timeout is recorded as
# <STEP>=TIMEOUT, the chain continues, and the run cannot be PASS)
#
#   verify-all            3600   VERIFY_RELEASE_TIMEOUT_VERIFY_ALL
#   outline               1800   VERIFY_RELEASE_TIMEOUT_OUTLINE
#   third-person-weapon   1200   VERIFY_RELEASE_TIMEOUT_THIRD_PERSON_WEAPON
#   body-appearance       1200   VERIFY_RELEASE_TIMEOUT_BODY_APPEARANCE
#   class-pixels          1200   VERIFY_RELEASE_TIMEOUT_CLASS_PIXELS
#   build-identity        1200   VERIFY_RELEASE_TIMEOUT_BUILD_IDENTITY
#
# build-identity-check.sh does not exist yet (task-14 / t90-identity owns it).
# Until it lands this step records STUB-ABSENT=FAIL and the chain is FAIL. It is
# not skipped: a chain that silently omits a step is the defect this file is
# meant to end.
#
# ARTEFACTS: evidence/release/<utc>-<step>.log and evidence/release/<utc>-summary.txt
# VERDICT:   VERIFY-RELEASE=PASS|FAIL steps_ok=N steps_total=6   (last stdout line)
#
# Exit: 0 PASS, 1 FAIL, 9 refused (not under the host lock / owner mismatch).
#
# Policy: docs/RELEASE-POLICY.md "The rule" item 2 (2026-11-07).
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
HOSTLOCK=${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}
OUT=${VERIFY_RELEASE_OUT:-"$ROOT/evidence/release"}
STAMP=${VERIFY_RELEASE_STAMP:-$(date -u +%Y%m%dT%H%M%SZ)}
OWNER=${VERIFY_RELEASE_OWNER:-}
DRY=${VERIFY_RELEASE_DRY_RUN:-0}

STEPS=(verify-all outline third-person-weapon body-appearance class-pixels build-identity)
TOTAL=${#STEPS[@]}

step_file() {
  case "$1" in
    verify-all)            echo verify-all.sh ;;
    outline)               echo outline-check.sh ;;
    third-person-weapon)   echo third-person-weapon-check.sh ;;
    body-appearance)       echo player-body-appearance-check.sh ;;
    class-pixels)          echo class-pixels-check.sh ;;
    build-identity)        echo build-identity-check.sh ;;
    *)                     echo "" ;;
  esac
}

step_timeout() {
  case "$1" in
    verify-all)            echo "${VERIFY_RELEASE_TIMEOUT_VERIFY_ALL:-3600}" ;;
    outline)               echo "${VERIFY_RELEASE_TIMEOUT_OUTLINE:-1800}" ;;
    third-person-weapon)   echo "${VERIFY_RELEASE_TIMEOUT_THIRD_PERSON_WEAPON:-1200}" ;;
    body-appearance)       echo "${VERIFY_RELEASE_TIMEOUT_BODY_APPEARANCE:-1200}" ;;
    class-pixels)          echo "${VERIFY_RELEASE_TIMEOUT_CLASS_PIXELS:-1200}" ;;
    build-identity)        echo "${VERIFY_RELEASE_TIMEOUT_BUILD_IDENTITY:-1200}" ;;
    *)                     echo 1200 ;;
  esac
}

# The one line each gate prints when it is green. A zero exit code with no such
# line is NOT a pass: a gate that returns 0 without a verdict measured nothing
# we can cite (recorded as NO-VERDICT below).
step_verdict() {
  case "$1" in
    verify-all)            echo VERIFY ;;
    outline)               echo OUTLINE-CHECK ;;
    third-person-weapon)   echo THIRD-PERSON-WEAPON ;;
    body-appearance)       echo BODY-APPEARANCE ;;
    class-pixels)          echo CLASS-PIXELS ;;
    build-identity)        echo BUILD-IDENTITY ;;
    *)                     echo "" ;;
  esac
}

upper() { printf "%s" "$1" | tr "a-z" "A-Z"; }

if [ "$DRY" = "1" ]; then
  echo "VERIFY-RELEASE dry run -- nothing is executed, no lock is taken, no log is written."
  echo "owner: ${OWNER:-<VERIFY_RELEASE_OWNER unset; the real run would refuse>}"
  echo "plan (each step runs under one host lock held by the caller):"
  i=0
  for step in "${STEPS[@]}"; do
    i=$((i + 1))
    script=$(step_file "$step")
    timeout_s=$(step_timeout "$step")
    printf "  [%d/%d] timeout %ss bash %s  > %s\n" "$i" "$TOTAL" "$timeout_s" "$script" "$OUT/$STAMP-$step.log"
  done
  echo "VERIFY-RELEASE=DRY-RUN steps_total=$TOTAL"
  exit 0
fi

# ---------------------------------------------------------------- preflight
# The lock check happens before any gate runs, inside the lock the caller took
# with "hostlock.sh run". Two conditions, both required: the lock is LOCKED, and
# its owner is this run owner. Anything else and the chain does not start --
# two build/capture chains on one host is the failure this project paid for.
if [ ! -f "$HOSTLOCK" ]; then
  echo "VERIFY-RELEASE=FAIL steps_ok=0 steps_total=$TOTAL reason=hostlock-script-missing path=$HOSTLOCK" >&2
  exit 9
fi
echo "hostlock status before the chain:"
before=$(bash "$HOSTLOCK" status 2>&1)
echo "$before"
held=$(printf "%s\n" "$before" | sed -n "s/^LOCKED owner=\([^ ]*\).*/\1/p" | head -1)
reason=""
if [ -z "$OWNER" ]; then
  reason="VERIFY_RELEASE_OWNER-unset"
elif [ -z "$held" ]; then
  reason="hostlock-not-held"
elif [ "$held" != "$OWNER" ]; then
  reason="hostlock-owner-mismatch(held=$held want=$OWNER)"
fi
if [ -n "$reason" ]; then
  echo >&2
  echo "verify-release: refusing to start -- $reason" >&2
  echo "  the acceptance chain builds, launches and captures frames, so it runs only" >&2
  echo "  inside a host lock the caller already holds, and only as that lock owner:" >&2
  echo "    bash $HOSTLOCK run <owner> env VERIFY_RELEASE_OWNER=<owner> bash $ROOT/verify-release.sh" >&2
  echo "VERIFY-RELEASE=FAIL steps_ok=0 steps_total=$TOTAL reason=$reason" >&2
  exit 9
fi

mkdir -p "$OUT" || { echo "VERIFY-RELEASE=FAIL steps_ok=0 steps_total=$TOTAL reason=output-dir-unwritable path=$OUT" >&2; exit 9; }
SUMMARY="$OUT/$STAMP-summary.txt"

exe="$ROOT/native/build-nmake/tf2_demo_native.exe"
exe_sha="absent"
if [ -f "$exe" ]; then
  exe_sha=$(sha256sum "$exe" 2>/dev/null | cut -d" " -f1)
fi
commit=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)
branch=$(git -C "$ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)

rec() { printf "%s\n" "$1" >> "$SUMMARY"; }

: > "$SUMMARY"
rec "verify-release summary"
rec "started_utc=$STAMP"
rec "host=$HOSTNAME"
rec "owner=$OWNER"
rec "worktree=$ROOT"
rec "commit=$commit"
rec "branch=$branch"
rec "exe=$exe"
rec "exe_sha256=$exe_sha"
rec "hostlock_before=$(printf "%s" "$before" | tr "\n" "|")"
rec "note: ok_lines counts output lines starting with two-spaces-OK or OK-space -- a lower"
rec "note: bound on assertions, not the per-gate pinned totals (verify-all.sh prints those itself)."
rec ""

steps_ok=0
overall=0
i=0
for step in "${STEPS[@]}"; do
  i=$((i + 1))
  script=$(step_file "$step")
  timeout_s=$(step_timeout "$step")
  verdict=$(step_verdict "$step")
  log="$OUT/$STAMP-$step.log"
  echo
  echo "[$i/$TOTAL] $step -- $script (timeout ${timeout_s}s)"
  echo "  log: $log"

  rc=0
  dur=0
  status=""
  if [ ! -f "$ROOT/$script" ]; then
    if [ "$step" = "build-identity" ]; then
      status=STUB-ABSENT
      rc=127
      {
        echo "STUB-ABSENT=FAIL step=build-identity"
        echo "the gate $script does not exist in this tree (task-14 / t90-identity owns it)."
        echo "this step is not skipped: until the gate lands the chain cannot be PASS."
        echo "evidence for the defect: docs/T85-DEBT-LIST.md D-20; D:/_shared/T85-STATUS.md 3.4"
      } > "$log"
    else
      status=MISSING
      rc=127
      {
        echo "MISSING=FAIL step=$step"
        echo "$script does not exist in this tree; a step that cannot run is not green."
      } > "$log"
    fi
  else
    {
      echo "verify-release step: $step ($script)"
      echo "cmd: timeout ${timeout_s}s bash $script"
      echo "owner: $OWNER"
      echo "commit: $commit"
      echo
    } > "$log"
    start=$(date +%s)
    if command -v timeout >/dev/null 2>&1; then
      timeout --kill-after=30s "${timeout_s}s" bash "$ROOT/$script" >> "$log" 2>&1
      rc=$?
    else
      bash "$ROOT/$script" >> "$log" 2>&1
      rc=$?
      echo "  NOTE no timeout(1) on PATH: $step ran without a wall-clock bound" | tee -a "$log"
    fi
    dur=$(( $(date +%s) - start ))
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
      status=TIMEOUT
    elif [ "$rc" -eq 0 ] && grep -q "^${verdict}=PASS$" "$log"; then
      status=PASS
    elif [ "$rc" -eq 0 ]; then
      status=NO-VERDICT
    else
      status=FAIL
    fi
  fi

  ok_lines=$(grep -cE "^(  OK|OK )" "$log" 2>/dev/null || true)
  key=$(grep -E "^[A-Z0-9][A-Z0-9-]*=(PASS|FAIL)" "$log" 2>/dev/null | tail -1)
  printf "  rc=%s dur=%ss status=%s ok_lines=%s key=%s\n" "$rc" "$dur" "$status" "$ok_lines" "${key:-none}"
  tail -3 "$log" 2>/dev/null | sed "s/^/  | /"
  rec "step=$step rc=$rc dur=${dur}s status=$status ok_lines=$ok_lines key=${key:-none} log=$log"
  if [ "$status" = "TIMEOUT" ]; then
    rec "$(upper "$step")=TIMEOUT timeout=${timeout_s}s"
    echo "  $(upper "$step")=TIMEOUT"
  fi
  if [ "$status" = "PASS" ]; then
    steps_ok=$((steps_ok + 1))
  else
    overall=1
  fi
done

# ---------------------------------------------------------------- teardown
echo
echo "hostlock status after the chain:"
after=$(bash "$HOSTLOCK" status 2>&1)
echo "$after"
procs=$(printf "%s\n" "$after" | sed -n "s/.*product_processes=\([^ ]*\).*/\1/p" | head -1)
rec "hostlock_after=$(printf "%s" "$after" | tr "\n" "|")"
if [ "$procs" = "0" ]; then
  rec "product_processes_after=0"
else
  overall=1
  rec "product_processes_after=${procs:-unknown} (must be 0: a player process outlived the chain)"
  echo "PRODUCT-PROCESSES=FAIL (${procs:-unknown}; must be 0)"
fi

if [ "$overall" -eq 0 ]; then
  verdict_line="VERIFY-RELEASE=PASS steps_ok=$steps_ok steps_total=$TOTAL"
else
  verdict_line="VERIFY-RELEASE=FAIL steps_ok=$steps_ok steps_total=$TOTAL"
fi
rec ""
rec "$verdict_line"
rec "summary=$SUMMARY"
echo
echo "$verdict_line"
echo "summary: $SUMMARY"
[ "$overall" -eq 0 ] && exit 0
exit 1
