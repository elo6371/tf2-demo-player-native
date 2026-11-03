#!/usr/bin/env bash
# player-animation-mutations.sh -- prove the T61 A-E readings can go red.
#
# External playbook rule 1 (see docs/READ-THE-PLAYBOOK-FIRST.md): a criterion that
# has never been seen to fail is not evidence.
# player-animation-check.sh asserts A (sequence table and bind skeleton), B
# (four distinct quarter-cycle poses inside the bound sphere), C (continuity),
# D (the clock rate), E (speed advances the cycle, no speed freezes it). This
# script reintroduces each defect, rebuilds, and requires the gate to go red
# with a NAMED reason, then restores the file BYTE-FOR-BYTE (cmp -s) and
# requires the gate to go green again.
#
# RULES THIS SCRIPT KEEPS (problems/INDEX.md)
#   * every FAIL line inside a mutation run is counted, not just the expected
#     one: the count must rise above the baseline's 0 (rule 2);
#   * the patch must actually appear where it is supposed to (rule 6), and the
#     restore is verified with cmp -s (rule 5);
#   * the tree is restored from an EXIT trap even if the run is interrupted, so
#     a killed run cannot leave a mutant binary behind (rule 5);
#   * the gate's own binary-freshness check runs inside every case, so a
#     mutation that did not rebuild is caught by the gate itself.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it builds and runs captures):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t61-anim \
#     bash -c 'cd /d/TF2_Native_Worktrees/t61-anim && bash player-animation-mutations.sh'
#
# Usage: bash player-animation-mutations.sh [case ...]   (default: m1 m2 m3 m4 m5)
# Exit:  0 = every case went red for its named reason, restored byte-for-byte,
#            and the baseline came back green.
set -uo pipefail
cd "$(dirname "$0")"

OUT=evidence/t61-animation/mutations
# Keep every gate run's own outputs under the mutation directory so the
# baseline runtime evidence is not rewritten by this suite.
export PLAYER_ANIMATION_OUT=evidence/t61-animation/runtime/mutations
BACKUP=.scratch/t61-mutations
mkdir -p "$OUT" "$BACKUP"
GATE=player-animation-check.sh

TOUCHED=()   # "file|backup" pairs, because the backup name is not derivable
             # from the file name (cases are tagged m1..m5).
restore_all() {
  local rc=$? entry f b
  for entry in "${TOUCHED[@]:-}"; do
    [ -n "$entry" ] || continue
    f="${entry%%|*}"; b="${entry#*|}"
    [ -f "$b" ] || continue
    if cmp -s "$b" "$f"; then
      # Already byte-identical: leave it alone. Touching a file that needs no
      # restore would make it newer than the binary the last rebuild produced,
      # which the gate's freshness guard would then correctly report as stale.
      continue
    fi
    cp -p "$b" "$f" 2>/dev/null || true
    cmp -s "$b" "$f" || echo "TRAP-RESTORE-MISMATCH $f" >&2
    # Touch the restored file: build-target.sh rebuilds a .cpp only when it is
    # newer than its object, and a header older than the stamp is not noticed
    # at all, so an untouched restore would keep the mutant object linked.
    touch "$f" 2>/dev/null || true
  done
  return $rc
}
trap restore_all EXIT INT TERM

# Applies one textual replacement. Sources are CRLF, so the patterns are written
# with LF and converted before matching. Fails loudly when the anchor is absent.
patch_in() { # patch_in <file> <find> <replace>
  python - "$1" "$2" "$3" <<'PY'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
if find not in data:
    sys.exit('PATTERN NOT FOUND: ' + find[:70].replace('\r', '\\r').replace('\n', '\\n'))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PY
}

BK=""   # set by backup_file (a command substitution would lose TOUCHED)
backup_file() { # backup_file <file> <tag>
  BK="$BACKUP/$2.orig"
  cp -p "$1" "$BK" || return 1
  TOUCHED+=("$1|$BK")
}

rebuild() {
  bash build-target.sh model_pose_probe tf2_demo_native
}

count_fails() { # count_fails <log>
  local n
  n=$(grep -c '^  FAIL ' "$1" 2>/dev/null || true)
  printf '%s' "${n:-0}"
}

run_gate() { # run_gate <log> [extra args...]
  local log="$1"; shift
  bash "$GATE" "$@" > "$log" 2>&1
  echo $?
}

fail_all=0
BASELINE_FAILS=-1

echo "=== baseline: the unmutated tree must be green before any case ==="
BASE_LOG="$OUT/baseline.log"
BASE_RC=$(run_gate "$BASE_LOG")
BASELINE_FAILS=$(count_fails "$BASE_LOG")
if [ "$BASE_RC" = "0" ] && [ "$BASELINE_FAILS" = "0" ]; then
  echo "  OK   baseline rc=0 FAIL-count=0 (log $BASE_LOG)"
else
  echo "  FAIL baseline rc=$BASE_RC FAIL-count=$BASELINE_FAILS (log $BASE_LOG)"
  sed 's/^/       | /' "$BASE_LOG" | grep -E 'FAIL|FATAL' | head -20
  fail_all=1
fi
if [ "$fail_all" = "1" ]; then
  echo "FAIL player-animation-mutations: baseline is not green; mutations would not mean anything."
  exit 1
fi

# ---------------------------------------------------------------------------
# run_case <tag> <file> <find> <replace> <expected FAIL pattern>...
# ---------------------------------------------------------------------------
run_case() {
  local tag="$1" file="$2" find="$3" replace="$4"; shift 4
  local log="$OUT/$tag.log"
  echo
  echo "=== case $tag: mutate $file ==="
  backup_file "$file" "$tag" || { echo "  FAIL could not back up $file"; fail_all=1; return; }
  local backup="$BK"
  if ! patch_in "$file" "$find" "$replace"; then
    echo "  FAIL patch anchor not found in $file"; fail_all=1; return
  fi
  if ! grep -qF "$(printf '%s' "$replace" | tail -1)" "$file"; then
    echo "  FAIL the patch is not present in $file after the edit (rule 6)"; fail_all=1; return
  fi
  echo "  patched (backup $backup)"
  if ! rebuild; then
    echo "  FAIL rebuild after mutation"
    # Leave no mutant behind on this path either: the trap only runs at exit,
    # and the next case must start from the real source.
    cp -p "$backup" "$file" 2>/dev/null || true
    touch "$file" 2>/dev/null || true
    bash build-target.sh model_pose_probe tf2_demo_native >/dev/null 2>&1 || true
    fail_all=1; return
  fi
  local rc fails
  rc=$(run_gate "$log" --skip-negative-controls)
  fails=$(count_fails "$log")
  echo "  gate rc=$rc FAIL-count=$fails (baseline $BASELINE_FAILS)"
  if [ "$rc" != "0" ] && [ "$fails" -gt "$BASELINE_FAILS" ]; then
    echo "  OK   gate went red and the FAIL count rose"
  else
    echo "  FAIL gate did not go red: rc=$rc FAIL-count=$fails"
    fail_all=1
  fi
  local pattern
  for pattern in "$@"; do
    if grep -qF "$pattern" "$log"; then
      echo "  OK   named reason present: $pattern"
    else
      echo "  FAIL named reason missing: $pattern"
      sed 's/^/       | /' "$log" | grep -E 'FAIL' | head -10
      fail_all=1
    fi
  done
  # restore byte-for-byte and prove it
  cp -p "$backup" "$file"
  if cmp -s "$backup" "$file"; then
    echo "  OK   restored byte-identical (cmp -s)"
  else
    echo "  FAIL restore differs from the backup"; fail_all=1
  fi
  # cp -p restores the original mtime, which is older than the mutant object:
  # NMAKE would link the mutant object again. Touch the file so the next build
  # really recompiles it (rule 5: no tree left holding a mutant).
  touch "$file"
  if ! rebuild; then echo "  FAIL rebuild after restore"; fail_all=1; return; fi
  local back_rc back_fails
  back_rc=$(run_gate "$OUT/$tag-restored.log" --skip-negative-controls)
  back_fails=$(count_fails "$OUT/$tag-restored.log")
  if [ "$back_rc" = "0" ] && [ "$back_fails" = "0" ]; then
    echo "  OK   baseline green again after restore (FAIL-count=0)"
  else
    echo "  FAIL baseline not green after restore: rc=$back_rc FAIL-count=$back_fails"
    sed 's/^/       | /' "$OUT/$tag-restored.log" | grep -E 'FAIL' | head -10
    fail_all=1
  fi
}

# m1: the clock never advances. This is the "animation is sampled but frozen"
# defect: the four quarter-cycle poses collapse into one (B) and a moving
# second advances the cycle by zero instead of one (D).
case_m1() {
  run_case m1 native/src/animation_clock.cpp \
"  const double delta = static_cast<double>(step.rate) * dt;" \
"  const double delta = 0.0;  // T61 m1: deltaCycle forced to 0" \
    "B scout distinct pose checksums" \
    "D movingOneSecond"
}

# m2: the skinning path returns identity bone matrices, i.e. every pose is the
# bind pose. B's four checksums become one.
case_m2() {
  run_case m2 native/src/animation_binding.cpp \
"    result.skinMatrices[i] = toColumnMajor(skin);" \
"    result.skinMatrices[i] = identity;  // T61 m2: identity bone matrices" \
    "B scout distinct pose checksums"
}

# m3: the rate ignores speed and always returns the maximum. D's moving second
# is then 3.0 cycles instead of the expected 1.0.
case_m3() {
  run_case m3 native/src/animation_clock.cpp \
"  const float raw = speed / cfg.strideUnits;
  return std::clamp(raw, cfg.minMovingRate, cfg.maxMovingRate);" \
"  return cfg.maxMovingRate;  // T61 m3: rate ignores speed" \
    "D movingOneSecond"
}

# m4: the cycle jumps half a cycle per tick regardless of dt. C's per-step
# delta explodes past boundDiag/4 (and B sees only two distinct poses).
case_m4() {
  run_case m4 native/src/animation_clock.cpp \
"  const double delta = static_cast<double>(step.rate) * dt;" \
"  const double delta = 0.5;  // T61 m4: cycle jumps 0.5 regardless of dt" \
    "C scout maxStepDelta" \
    "B scout distinct pose checksums"
}

# m5: one skin-matrix element is NaN. Every sampled pose is then non-finite, so
# the probe's animating-sequence scan rejects each candidate and must say WHY:
# reason=non-finite-pose, not the misleading no-animating-sequence.
case_m5() {
  run_case m5 native/src/animation_binding.cpp \
"  result.skinBones = written;" \
"  result.skinBones = written;
  result.skinMatrices[0][0] = std::nanf(\"\");  // T61 m5: NaN bone" \
    "reason=non-finite-pose"
}

CASES="${*:-m1 m2 m3 m4 m5}"
for c in $CASES; do
  case "$c" in
    m1) case_m1 ;;
    m2) case_m2 ;;
    m3) case_m3 ;;
    m4) case_m4 ;;
    m5) case_m5 ;;
    *) echo "unknown case: $c"; fail_all=1 ;;
  esac
done

echo
if [ "$fail_all" = "0" ]; then
  echo "PASS player-animation-mutations: m1-m5 each went red for its named reason; every file restored byte-identically; baseline green again."
else
  echo "FAIL player-animation-mutations: see the FAIL lines above."
fi
exit "$fail_all"
