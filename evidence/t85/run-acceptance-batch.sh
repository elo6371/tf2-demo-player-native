#!/usr/bin/env bash
# T85 P0 acceptance batch -- ONE hostlock grant, five steps, self-terminating.
#
# Launch (from the worktree root):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t85-budget \
#        bash evidence/t85/run-acceptance-batch.sh
#
# It does NOT call hostlock itself: a nested acquire deadlocks (the wrapper is
# already holding the lock while this script runs).
#
# WHY ONE SCRIPT INSTEAD OF FIVE TICKETS: the queue is strict FIFO, so five
# separate tickets would wait five times. One ticket = one wait.
#
# SAFETY RULES (lead m01142)
#  * trap on EXIT/INT/TERM kills THIS batch's own process tree: a BFS over
#    ps -eo pid=,ppid= starting at ${} down to its children. No name-based
#    pkill -- another agent's tf2_demo_native.exe is never touched.
#    The hostlock itself is released by the wrapper's own EXIT trap once our
#    tree is gone. The driver deliberately does NOT release it: releasing while
#    a product is still alive would leave that capture unprotected.
#  * a step that goes RED does not abort the batch: rc and red lines are
#    recorded and the next step runs, so one lock window returns full
#    information. The verdict is one BATCH=PASS/FAIL at the end plus a table.
#  * total budget T85_BATCH_BUDGET (default 3600 s). Every step is wrapped in
#    timeout with whatever budget is left; with < 120 s left the remaining
#    steps are SKIPPED-BUDGET and the table says which did not run.
#  * the tail prints hostlock status and asserts product_processes=0.
#
# STEPS
#   1 outline-check.sh (full, no --mutation)   acceptance item 2
#   2 third-person-weapon-check.sh             acceptance item 3
#   3 player-body-appearance-check.sh          acceptance item 4
#   4 run-budget-priority-mutation.sh          behavioural mutation m14 (red/green)
#   5 run-determinism.sh                       acceptance item 5 (two processes)
#
# TRACKED-FILE HYGIENE (lead m00789): the product rewrites recentDemo0..7 inside
# whatever file it is handed as --ui-settings-file, so the two pixel gates are
# pointed at COPIES under evidence/t85/inputs/ instead of the tracked baselines
# under evidence/t62-matrix/. Outputs go under evidence/t85/acceptance/.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
SELF=evidence/t85/run-acceptance-batch.sh
OUT=evidence/t85/acceptance
mkdir -p "$OUT"
LOG=$OUT/batch.log
: > "$LOG"
exec 3>&1
exec >> "$LOG" 2>&1
START=$SECONDS
TOTAL_BUDGET=${T85_BATCH_BUDGET:-3600}
EXE=native/build-nmake/tf2_demo_native.exe
STEPS_RUN=""
STEP_TABLE=$OUT/step-table.txt
: > "$STEP_TABLE"
rc_all=0

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  local -a frontier next kids
  local pid ppid f
  frontier=(${})
  kids=()
  while [ "${#frontier[@]}" -gt 0 ]; do
    next=()
    while read -r pid ppid; do
      [ -n "${pid:-}" ] || continue
      for f in "${frontier[@]}"; do
        if [ "${ppid}" = "${f}" ]; then kids+=("${pid}"); next+=("${pid}"); fi
      done
    done < <(ps -eo pid=,ppid= 2>/dev/null)
    frontier=("${next[@]}")
  done
  if [ "${#kids[@]}" -gt 0 ]; then
    echo "CLEANUP: killing ${#kids[@]} descendant process(es) of ${} : ${kids[*]}"
    kill -TERM "${kids[@]}" 2>/dev/null
    sleep 2
    local alive=()
    for pid in "${kids[@]}"; do kill -0 "${pid}" 2>/dev/null && alive+=("${pid}"); done
    [ "${#alive[@]}" -gt 0 ] && { echo "CLEANUP: SIGKILL ${alive[*]}"; kill -KILL "${alive[@]}" 2>/dev/null; }
  fi
  echo "CLEANUP done at ${SECONDS}s (status=${status})"
  exit "${status}"
}
trap cleanup EXIT
trap 'echo "INTERRUPTED by signal at ${SECONDS}s"; exit 130' INT TERM

remaining() { echo $(( TOTAL_BUDGET - (SECONDS - START) )); }

echo "==========================================================================="
echo "T85 P0 acceptance batch  ${START}s  $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "driver      = ${SELF} sha256=$(sha256sum "$SELF" | cut -d' ' -f1)"
echo "pwd         = $(pwd)"
echo "git         = branch $(git rev-parse --abbrev-ref HEAD) head $(git rev-parse HEAD)"
echo "exe         = $(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1) mtime $(date -r "$EXE" +%Y-%m-%dT%H:%M:%S)"
echo "gates       = outline $(sha256sum outline-check.sh | cut -d' ' -f1)"
echo "              third-person $(sha256sum third-person-weapon-check.sh | cut -d' ' -f1)"
echo "              body $(sha256sum player-body-appearance-check.sh | cut -d' ' -f1)"
echo "budget      = ${TOTAL_BUDGET}s (T85_BATCH_BUDGET)"
echo "lock        = this process runs INSIDE a hostlock grant owned by t85-budget"
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>/dev/null | sed 's/^/  lock-before: /'
echo "==========================================================================="

step() { # step <name> <log> <reader> <command...>
  local name="$1" log="$2" reader="$3"; shift 3
  local started=$SECONDS rc rem t
  # Selective leases (lead m01584 split the run in two): T85_BATCH_SKIP is a
  # space-separated step list. A skipped step is RECORDED as SKIP, never silently
  # dropped, so the step table still shows every acceptance item.
  case " ${T85_BATCH_SKIP:-} " in
    *" ${name} "*)
      echo
      echo "=== STEP ${name} SKIPPED-BY-REQUEST (T85_BATCH_SKIP) ==="
      printf '%-12s %-6s %-6s %s\n' "${name}" "SKIP" "0" "skipped by T85_BATCH_SKIP" >> "$STEP_TABLE"
      STEPS_RUN="${STEPS_RUN}${name}:SKIP "
      return 0;;
  esac
  rem=$(remaining)
  if [ "$rem" -lt 120 ]; then
    echo
    echo "=== STEP ${name} SKIPPED-BUDGET (remaining ${rem}s < 120s) ==="
    printf '%-12s %-6s %-6s %s\n' "${name}" "SKIP" "0" "budget exhausted before this step" >> "$STEP_TABLE"
    STEPS_RUN="${STEPS_RUN}${name}:SKIP "
    return 0
  fi
  t=$(( rem < 1500 ? rem : 1500 ))
  echo
  echo "=== STEP ${name} (budget ${t}s, remaining ${rem}s) ==="
  echo "  argv: ${*}"
  timeout --kill-after=30 "$t" "$@" > "$log" 2>&1
  rc=$?
  local secs=$(( SECONDS - started ))
  local token fails
  token=$(grep -a -E "^(OUTLINE-CHECK|THIRD-PERSON-WEAPON-CHECK|BODY-APPEARANCE|BUDGET-PRIORITY-MUTATION)=" "$log" | tail -1)
  fails=$(grep -a -cE "^[[:space:]]+FAIL[[:space:]]" "$log")
  echo "  rc=${rc} wall_seconds=${secs} FAIL-lines=${fails} token=${token:-<none>} log=$log"
  if [ "$rc" = "124" ]; then echo "  NOTE: killed by the batch budget (timeout ${t}s)"; fi
  grep -a -E "^[[:space:]]+FAIL[[:space:]]" "$log" | head -20 | sed 's/^/  red: /'
  "${reader}" "$log"
  printf '%-12s %-6s %-6s %s\n' "${name}" "${rc}" "${secs}" "${token:-<none>}" >> "$STEP_TABLE"
  STEPS_RUN="${STEPS_RUN}${name}:${rc} "
  case "$rc" in 0) : ;; *) rc_all=1 ;; esac
  return 0
}

read_outline() { # ${1} = gate log
  local ok f
  ok=$(grep -a -cE "^[[:space:]]+OK[[:space:]]" "$1")
  f=$(grep -a -cE "^[[:space:]]+FAIL[[:space:]]" "$1")
  echo "  readings: assertions OK=${ok} FAIL=${f} total=$(( ok + f ))"
  grep -a -E "^(OUTLINE-CHECK)=" "$1" | tail -1 | sed 's/^/  verdict: /'
  grep -a -E "^[[:space:]]*(budget: prepare|budget: meshTable|budget: instances|budget: draw|attach: meshSource)" "$1" | tail -6 | sed 's/^/  budget: /'
}

read_third() {
  echo "  readings: $(grep -a -E "^cases=" "$1" | tail -1)"
  echo "  readings: OK=$(grep -a -cE "^[[:space:]]+OK[[:space:]]" "$1") FAIL=$(grep -a -cE "^[[:space:]]+FAIL[[:space:]]" "$1")"
}

read_body() {
  echo "  readings: judged-lines=$(grep -a -cE "^[[:space:]]+(PAIR|OK|FAIL)[[:space:]]" "$1")"
  grep -a -E "^[[:space:]]*PAIR " "$1" | head -10 | sed 's/^/  pair: /'
  echo "  readings: want_pairs line: $(grep -a -E "requested pairs|one team per class" "$1" | head -2 | tr '\n' ' ')"
}

read_mutation() {
  grep -a -E "^(source sha256|build rc|red run rc|green run rc|restore:|named FAIL lines|MUTATION-|BUDGET-PRIORITY-MUTATION=)" "$1" | sed 's/^/  mut: /'
}

read_determinism() {
  grep -a -E "^(FRAME:|TRACE:)" "$1" | sed 's/^/  det: /'
}

step outline-check      "$OUT/outline-check.log"      read_outline \
  env OUTLINE_OUT=evidence/t85/acceptance/outline bash outline-check.sh
step third-person       "$OUT/third-person.log"       read_third \
  env T83_WEAPON_BASELINE=evidence/t85/inputs/t62-class-pixels \
      T83_WEAPON_OUT=evidence/t85/acceptance/third-person \
      bash third-person-weapon-check.sh
step body-appearance    "$OUT/player-body.log"        read_body \
  env T62_BODY_INPUT=evidence/t85/inputs/t62-body-appearance \
      T62_BODY_OUT=evidence/t85/acceptance/player-body \
      bash player-body-appearance-check.sh
step determinism        "$OUT/determinism.log"        read_determinism \
  bash evidence/t85/run-determinism.sh
step m14-mutation       "$OUT/priority-mutation.log"  read_mutation \
  bash evidence/t85/run-budget-priority-mutation.sh

echo
echo "==========================================================================="
echo "STEP TABLE (name rc wall_seconds token)"
cat "$STEP_TABLE"
echo "steps run: ${STEPS_RUN}"
echo "outline-check wall_seconds alone: $(awk '$1=="outline-check"{print ${3}}' "$STEP_TABLE")"
echo
echo "lock after batch:"
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>/dev/null | sed 's/^/  /'
PP=$(bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>/dev/null | sed -n 's/^product_processes=//p' | head -1)
if [ "${PP:-unknown}" = "0" ]; then
  echo "  OK   no product process survived this batch (product_processes=0)"
  echo "BATCH-PRODUCT-PROCESSES=PASS"
else
  echo "  FAIL product_processes=${PP:-unknown} -- a tf2_demo_native.exe outlived the batch"
  echo "BATCH-PRODUCT-PROCESSES=FAIL"
  rc_all=1
fi
echo "exe sha256 after the batch = $(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)"
echo "git status tracked dirt:"
git status --porcelain | grep -v '^??' | sed 's/^/  /' || true
if [ "$rc_all" = "0" ]; then echo "BATCH=PASS"; else echo "BATCH=FAIL"; fi
echo "==========================================================================="
echo "--- tail of this driver log (full copy: $LOG) ---"
tail -n 160 "$LOG" >&3 2>/dev/null || true
exit "$rc_all"
