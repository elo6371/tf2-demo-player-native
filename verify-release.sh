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
#   launch-hygiene         120   VERIFY_RELEASE_TIMEOUT_LAUNCH_HYGIENE
#   debt-claim             120   VERIFY_RELEASE_TIMEOUT_DEBT_CLAIM
#   verify-all           14400   VERIFY_RELEASE_TIMEOUT_VERIFY_ALL   (round 2 measured ~150 min)
#   rebuild               3600   VERIFY_RELEASE_TIMEOUT_REBUILD
#   outline               5400   VERIFY_RELEASE_TIMEOUT_OUTLINE
#   third-person-weapon   3600   VERIFY_RELEASE_TIMEOUT_THIRD_PERSON_WEAPON
#   body-appearance       3600   VERIFY_RELEASE_TIMEOUT_BODY_APPEARANCE
#   class-pixels          3600   VERIFY_RELEASE_TIMEOUT_CLASS_PIXELS
#   build-identity        2400   VERIFY_RELEASE_TIMEOUT_BUILD_IDENTITY
#   world-geometry-budget 3600   VERIFY_RELEASE_TIMEOUT_WORLD_GEOMETRY_BUDGET
#   install-cli-contract  2400   VERIFY_RELEASE_TIMEOUT_INSTALL_CLI_CONTRACT
#   playback-error-paths  1800   VERIFY_RELEASE_TIMEOUT_PLAYBACK_ERROR_PATHS
#   timeline              1800   VERIFY_RELEASE_TIMEOUT_TIMELINE
#   hud-live              2400   VERIFY_RELEASE_TIMEOUT_HUD_LIVE      (forward + three
#                               mutation rebuilds; the readout is drawn, so this one
#                               is a pixel gate with a build per negative control)
#   pin-provenance         600   VERIFY_RELEASE_TIMEOUT_PIN_PROVENANCE  (static: hashes files, starts no process)
#
# Step 2 is debt-claim-check.sh (task-20): it is static (no build, no product,
# no lock) and fails when a row of docs/T85-DEBT-LIST.md claims CAN-RED without a
# resolvable gate anchor. Step 3 is verify-all. Step 4 (release-rebuild.sh) exists
# because the mutation suites inside verify-all restore sources with a new mtime:
# without it the capture steps read a binary the tree no longer vouches for.
#
# Every one of the fifteen steps has a script in this tree. A step whose script
# is missing still records STUB-ABSENT=FAIL and fails the chain: a chain that
# silently omits a step is the defect this file is meant to end.
#
# MACHINE ISOLATION (debt D-22)
# The chain must prove it did not rewrite the tester's real configuration. Before
# the first step and after the last one this script snapshots
# %LOCALAPPDATA%/TF2 Demo Player/settings.ini and ui.ini -- existence, size,
# mtime, sha256 -- to <utc>-machine-before.txt / <utc>-machine-after.txt and
# compares them. By default ANY change is MACHINE-MUTATED=FAIL and the chain is
# FAIL (both files are written by the product itself: settings.ini at exit,
# ui.ini when a demo is opened). VERIFY_RELEASE_ALLOW_MACHINE_MUTATION=1 turns
# the failure into a recorded warning (MACHINE-MUTATION-ALLOWED=1) and is meant
# only for diagnosing D-22, never for signing a release.
#
# ARTEFACTS: evidence/release/<utc>-<step>.log, <utc>-summary.txt,
#            <utc>-machine-before.txt, <utc>-machine-after.txt
# VERDICT:   VERIFY-RELEASE=PASS|FAIL steps_ok=N steps_total=15  (last stdout line)
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

# ORDER, NOT PREFERENCE: pin-provenance is LAST because it asserts the bytes of
# evidence/t62-matrix/class-pixels against their provenance.json. The steps whose
# defaults write into that pin directory (class-pixels, third-person-weapon,
# body-appearance -- this chain redirects them to its own OUT, but the order must
# not depend on that) all run before it, and nothing runs after it, so its pass
# describes the tree at the end of the chain, not a state a later step can disturb.
STEPS=(launch-hygiene debt-claim verify-all rebuild outline third-person-weapon body-appearance class-pixels build-identity world-geometry-budget install-cli-contract playback-error-paths timeline hud-live pin-provenance)
TOTAL=${#STEPS[@]}

step_file() {
  case "$1" in
    launch-hygiene)        echo gate-launch-hygiene-check.sh ;;
    debt-claim)            echo debt-claim-check.sh ;;
    verify-all)            echo verify-all.sh ;;
    rebuild)               echo release-rebuild.sh ;;
    outline)               echo outline-check.sh ;;
    third-person-weapon)   echo third-person-weapon-check.sh ;;
    body-appearance)       echo player-body-appearance-check.sh ;;
    class-pixels)          echo class-pixels-check.sh ;;
    build-identity)        echo build-identity-check.sh ;;
    world-geometry-budget) echo world-geometry-budget-check.sh ;;
    install-cli-contract)  echo install-cli-contract-check.sh ;;
    playback-error-paths)  echo playback-error-paths-check.sh ;;
    timeline)              echo timeline-check.sh ;;
    hud-live)              echo hud-live-check.sh ;;
    pin-provenance)        echo pin-provenance-check.sh ;;
    *)                     echo "" ;;
  esac
}

step_timeout() {
  case "$1" in
    launch-hygiene)        echo "${VERIFY_RELEASE_TIMEOUT_LAUNCH_HYGIENE:-120}" ;;
    debt-claim)            echo "${VERIFY_RELEASE_TIMEOUT_DEBT_CLAIM:-120}" ;;
    verify-all)            echo "${VERIFY_RELEASE_TIMEOUT_VERIFY_ALL:-14400}" ;;
    rebuild)               echo "${VERIFY_RELEASE_TIMEOUT_REBUILD:-3600}" ;;
    outline)               echo "${VERIFY_RELEASE_TIMEOUT_OUTLINE:-5400}" ;;
    # 20 captures (5 per class x 4 classes), each replaying to tick 64161 at
    # speed 8: round 2 spent 1200 s and finished ONE class, so 15 minutes was a
    # bound on the wall clock, not on the gate. Measured rate ~2.2 min/capture.
    third-person-weapon)   echo "${VERIFY_RELEASE_TIMEOUT_THIRD_PERSON_WEAPON:-5400}" ;;
    # Same shape as third-person (two captures per body, one body per team).
    body-appearance)       echo "${VERIFY_RELEASE_TIMEOUT_BODY_APPEARANCE:-5400}" ;;
    # Four frames per class: the preflight run took ~26 min for four classes.
    class-pixels)          echo "${VERIFY_RELEASE_TIMEOUT_CLASS_PIXELS:-4200}" ;;
    build-identity)        echo "${VERIFY_RELEASE_TIMEOUT_BUILD_IDENTITY:-2400}" ;;
    world-geometry-budget) echo "${VERIFY_RELEASE_TIMEOUT_WORLD_GEOMETRY_BUDGET:-3600}" ;;
    install-cli-contract)  echo "${VERIFY_RELEASE_TIMEOUT_INSTALL_CLI_CONTRACT:-2400}" ;;
    playback-error-paths)  echo "${VERIFY_RELEASE_TIMEOUT_PLAYBACK_ERROR_PATHS:-1800}" ;;
    timeline)              echo "${VERIFY_RELEASE_TIMEOUT_TIMELINE:-1800}" ;;
    # Forward plus three mutation rebuilds; each mutation recompiles main_hud and
    # relaunches the product, and the forward run captures five times.
    hud-live)              echo "${VERIFY_RELEASE_TIMEOUT_HUD_LIVE:-2400}" ;;
    # Static: it reads four provenance.json files and hashes the pinned BMPs; no
    # build, no product, no lock. 600 s bounds a pathological filesystem, not work.
    pin-provenance)        echo "${VERIFY_RELEASE_TIMEOUT_PIN_PROVENANCE:-600}" ;;
    *)                     echo 1200 ;;
  esac
}

# The one line each gate prints when it is green. A zero exit code with no such
# line is NOT a pass: a gate that returns 0 without a verdict measured nothing
# we can cite (recorded as NO-VERDICT below).
step_verdict() {
  case "$1" in
    launch-hygiene)        echo GATE-LAUNCH-HYGIENE ;;
    debt-claim)            echo DEBT-CLAIM ;;
    verify-all)            echo VERIFY ;;
    rebuild)               echo REBUILD ;;
    outline)               echo OUTLINE-CHECK ;;
    third-person-weapon)   echo THIRD-PERSON-WEAPON ;;
    body-appearance)       echo BODY-APPEARANCE ;;
    class-pixels)          echo CLASS-PIXELS ;;
    build-identity)        echo BUILD-IDENTITY ;;
    world-geometry-budget) echo WORLD-GEOMETRY-BUDGET ;;
    install-cli-contract)  echo INSTALL-CLI-CONTRACT ;;
    playback-error-paths)  echo PLAYBACK-ERROR-PATHS ;;
    timeline)              echo TIMELINE-CHECK ;;
    hud-live)              echo HUD-LIVE-CHECK ;;
    pin-provenance)        echo PIN-PROVENANCE ;;
    *)                     echo "" ;;
  esac
}

upper() { printf "%s" "$1" | tr "a-z" "A-Z"; }

# ---------------------------------------------------------------- machine guard
# Debt D-22: gates launch the product without --settings-file / --ui-settings-file,
# and the product writes its real config on the way out (settings.ini at exit,
# ui.ini when a demo is opened). A "green" chain that silently rewrites the
# tester's own configuration is not evidence of anything, so the chain measures
# the two files before and after and fails on any change.
ALLOW_MACHINE_MUTATION=${VERIFY_RELEASE_ALLOW_MACHINE_MUTATION:-0}

machine_dir() {
  local base=${LOCALAPPDATA:-}
  if [ -z "$base" ] && command -v cmd >/dev/null 2>&1; then
    base=$(cmd //c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r')
  fi
  [ -n "$base" ] || return 1
  if command -v cygpath >/dev/null 2>&1; then
    base=$(cygpath -u "$base" 2>/dev/null) || return 1
  else
    base=${base//\\//}
  fi
  [ -n "$base" ] || return 1
  printf "%s/TF2 Demo Player" "$base"
}

machine_snapshot() {
  local out=$1 dir f
  {
    echo "# machine settings snapshot (verify-release.sh)"
    echo "# taken_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    if ! dir=$(machine_dir); then
      echo "MACHINE-SNAPSHOT=UNAVAILABLE reason=localappdata-unset"
      return 2
    fi
    echo "dir=$dir"
    for f in settings.ini ui.ini; do
      if [ -f "$dir/$f" ]; then
        printf "file=%s exists=1 bytes=%s mtime=%s sha256=%s\n" "$f" \
          "$(stat -c %s "$dir/$f" 2>/dev/null || echo '?')" \
          "$(stat -c %Y "$dir/$f" 2>/dev/null || echo '?')" \
          "$(sha256sum "$dir/$f" 2>/dev/null | cut -d' ' -f1)"
      else
        printf "file=%s exists=0\n" "$f"
      fi
    done
  } > "$out"
  grep -q "MACHINE-SNAPSHOT=UNAVAILABLE" "$out" && return 2
  return 0
}

snapshot_line() {
  grep "^file=$1 " "$2" 2>/dev/null || echo "file=$1 missing-from-snapshot"
}

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
  echo
  echo "machine guard (debt D-22): snapshot $OUT/$STAMP-machine-before.txt before step 1 and"
  echo "  $OUT/$STAMP-machine-after.txt after the last step (settings.ini + ui.ini under"
  echo "  %LOCALAPPDATA%/TF2 Demo Player: exists, bytes, mtime, sha256). Any change is"
  echo "  MACHINE-MUTATED=FAIL unless VERIFY_RELEASE_ALLOW_MACHINE_MUTATION=1, which records"
  echo "  MACHINE-MUTATION-ALLOWED=1 and still cannot sign a release."
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

MACHINE_BEFORE="$OUT/$STAMP-machine-before.txt"
MACHINE_AFTER="$OUT/$STAMP-machine-after.txt"
machine_snapshot "$MACHINE_BEFORE"
machine_rc=$?
rec "machine_before=$MACHINE_BEFORE"
rec "machine_after=$MACHINE_AFTER"
if [ "$machine_rc" -eq 2 ]; then
  rec "MACHINE-SNAPSHOT=UNAVAILABLE before-step-1 note=the chain then cannot prove it left the tester's configuration alone"
fi

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
    stub_owner=""
    case "$step" in
      build-identity)        stub_owner="task-14 / t90-identity; evidence: docs/T85-DEBT-LIST.md D-20, D:/_shared/T85-STATUS.md 3.4" ;;
      world-geometry-budget) stub_owner="task-13; the gate is being delivered there" ;;
    esac
    if [ -n "$stub_owner" ]; then
      status=STUB-ABSENT
      rc=127
      {
        echo "STUB-ABSENT=FAIL step=$step"
        echo "the gate $script does not exist in this tree ($stub_owner)."
        echo "this step is not skipped: until the gate lands the chain cannot be PASS."
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
    # A gate that self-acquires the host lock would deadlock behind the lock this
    # chain already holds. Such gates accept HOLD_LOCK=1 + LOCK_OWNER to run inline
    # instead; everything else ignores both.
    step_env=()
    case "$step" in
      # world-geometry-budget writes its traces next to its committed evidence
      # (evidence/t88-maps/trace-*.trace) unless BUDGET_OUT is set; the chain must
      # not rewrite tracked evidence, so each run gets its own directory.
      world-geometry-budget) step_env=(env HOLD_LOCK=1 LOCK_OWNER="$OWNER" BUDGET_OUT="$OUT/$STAMP-world-geometry") ;;
      # playback writes captures into evidence/t93-playback by default; same rule.
      playback-error-paths) step_env=(env PLAYBACK_OUT="$OUT/$STAMP-playback") ;;
      # outline-check globs "$OUT"/*.trace to build its lease-wide readings. Its
      # default OUT is evidence/outline, which survives from run to run, so a
      # second chain would compare its captures against the first chain's traces.
      # Every chain run gets its own directory (t85's merge-window runbook).
      outline) step_env=(env OUTLINE_OUT="$OUT/$STAMP-outline") ;;
      # The three pixel gates write their captures into whatever OUT names, and
      # class-pixels' default OUT IS the pinned baseline directory that
      # third-person compares against (class-pixels-check.sh:98, :188 rm -f).
      # In one chain that made third-person compare the product with the frame
      # the SAME chain had just captured through it -- a self-comparison that
      # cannot fail. Each step now gets its own directory, and the pin stays the
      # committed one (t91 merge window, PIXEL-DELTA.md).
      third-person-weapon) step_env=(env T83_WEAPON_OUT="$OUT/$STAMP-third-person") ;;
      body-appearance) step_env=(env T62_BODY_OUT="$OUT/$STAMP-body-appearance") ;;
      class-pixels) step_env=(env T62_CLASS_OUT="$OUT/$STAMP-class-pixels") ;;
      # the readout gate writes its frames and readings into evidence/t97-hud by
      # default; a chain run must not rewrite that checked-in directory.
      hud-live) step_env=(env HUD_LIVE_CHECK_OUT="$OUT/$STAMP-hud-live") ;;
    esac
    if command -v timeout >/dev/null 2>&1; then
      "${step_env[@]}" timeout --kill-after=30s "${timeout_s}s" bash "$ROOT/$script" >> "$log" 2>&1
      rc=$?
    else
      "${step_env[@]}" bash "$ROOT/$script" >> "$log" 2>&1
      rc=$?
      echo "  NOTE no timeout(1) on PATH: $step ran without a wall-clock bound" | tee -a "$log"
    fi
    dur=$(( $(date +%s) - start ))
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
      status=TIMEOUT
    elif [ "$rc" -eq 0 ] && grep -qE "^[[:space:]]*${verdict}=PASS($| )" "$log"; then
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

# ---------------------------------------------------------------- machine guard verdict
# The two files are written by the product itself, not by this script: settings.ini
# at exit (bookmarks, keys, render/tfRoot) and ui.ini when a demo is opened
# (recentDemo0..7). A change here means some gate in the chain launched the product
# without pointing --settings-file / --ui-settings-file somewhere disposable (D-22).
machine_failed=0
machine_reason=""
machine_changed=""
if [ "$machine_rc" -eq 2 ]; then
  machine_failed=1
  machine_reason="snapshot-unavailable-before"
else
  machine_snapshot "$MACHINE_AFTER"
  machine_rc_after=$?
  if [ "$machine_rc_after" -eq 2 ]; then
    machine_failed=1
    machine_reason="snapshot-unavailable-after"
  else
    for f in settings.ini ui.ini; do
      b=$(snapshot_line "$f" "$MACHINE_BEFORE")
      a=$(snapshot_line "$f" "$MACHINE_AFTER")
      if [ "$b" != "$a" ]; then machine_changed="$machine_changed $f"; fi
    done
    machine_changed=${machine_changed# }
    if [ -n "$machine_changed" ]; then
      machine_failed=1
      machine_reason="files=${machine_changed// /,}"
    fi
  fi
fi

if [ "$machine_failed" -eq 1 ]; then
  if [ "$ALLOW_MACHINE_MUTATION" = "1" ]; then
    echo "MACHINE-MUTATION-ALLOWED=1 $machine_reason"
    rec "MACHINE-MUTATION-ALLOWED=1 $machine_reason"
    rec "note: the chain rewrote the tester's real configuration; it passed only because"
    rec "note: VERIFY_RELEASE_ALLOW_MACHINE_MUTATION=1 was set (debt D-22). Not a release run."
  else
    overall=1
    echo "MACHINE-MUTATED=FAIL $machine_reason"
    rec "MACHINE-MUTATED=FAIL $machine_reason"
    for f in settings.ini ui.ini; do
      case " $machine_changed " in
        *" $f "*)
          rec "  $f before: $(snapshot_line "$f" "$MACHINE_BEFORE")"
          rec "  $f after:  $(snapshot_line "$f" "$MACHINE_AFTER")"
          ;;
      esac
    done
  fi
else
  echo "MACHINE-UNCHANGED=PASS files=settings.ini,ui.ini"
  rec "MACHINE-UNCHANGED=PASS files=settings.ini,ui.ini"
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
