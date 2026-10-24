#!/usr/bin/env bash
# player-model-mutations.sh -- prove the T60 A/B/C readings can go red.
#
# SOUL.md rule 1: a criterion that has never been seen to fail is not evidence.
# player-model-check.sh asserts A (LOD0 not truncated), B (paint per material
# slot and skin family) and C (separate _animations.mdl source). This script
# reintroduces each defect, rebuilds, and requires the gate to go red with a
# NAMED reason, then restores the file BYTE-FOR-BYTE (cmp -s) and requires the
# gate to go green again. A fourth, separate category mutates the GATE's own
# configuration: it proves the assertions are load-bearing rather than a run of
# statements that cannot fail.
#
# RULES THIS SCRIPT KEEPS (problems/INDEX.md)
#   * every FAIL line inside a mutation run is counted, not just the expected
#     one: the count must rise above the baseline's 0 (rule 2);
#   * the patch must actually appear where it is supposed to (rule 6), and the
#     restore is verified with cmp -s (rule 5);
#   * the tree is restored from an EXIT trap even if the run is interrupted, so
#     a killed run cannot leave a mutant binary behind (rule 5);
#   * the gate's own binary-freshness check (STALE-BINARY) runs inside every
#     case, so a mutation that did not rebuild is caught by the gate itself.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it builds and runs captures):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t60-model \
#     bash -c 'cd /d/TF2_Native_Worktrees/t60-model && bash player-model-mutations.sh'
#
# Usage: bash player-model-mutations.sh [case ...]   (default: a b c meta)
# Exit:  0 = every case went red for its named reason, restored byte-for-byte,
#            and the baseline came back green.
set -uo pipefail
cd "$(dirname "$0")"

OUT=evidence/t57-player-model/runtime/t60/mutations
# Keep every gate run's own outputs under T60's directory so T57's runtime
# evidence in the parent directory is not rewritten by this suite.
export PLAYER_MODEL_OUT=evidence/t57-player-model/runtime/t60
BACKUP=.scratch/t60-mutations
mkdir -p "$OUT" "$BACKUP"
GATE=player-model-check.sh

TOUCHED=()   # "file|backup" pairs. The backup name is not derivable from the
             # file name (cases are tagged a/b/c/meta), and a trap that guesses
             # it restores nothing -- exactly the failure this trap prevents.
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
  bash build-target.sh entity_model_probe entity_overlay_probe model_robustness_probe tf2_demo_native
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
  echo "FAIL player-model-mutations: baseline is not green; mutations would not mean anything."
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
  if ! rebuild; then echo "  FAIL rebuild after mutation"; fail_all=1; return; fi
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
  # cp -p restores the original mtime, which is older than the mutant object and
  # older than the header stamp: NMAKE would link the mutant object again and the
  # "restored" gate would be reading the mutant binary. Touch the file so the
  # next build really recompiles it (rule 5: no tree left holding a mutant).
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

# A: reintroduce a fixed 12000-vertex cap inside the LOD0 builder. This is the
# exact defect T57 measured (all nine classes stopped at lod0Built=12000); the
# fix derives the budget from the model's own render-index count instead.
case_a() {
  run_case a native/src/model_loader.cpp \
"    const std::size_t rangeStart = metadata.bindPoseVertices.size();
    for (std::uint32_t i = 0; i < descriptor.renderIndexCount; ++i) {
" \
"    const std::size_t rangeStart = metadata.bindPoseVertices.size();
    for (std::uint32_t i = 0; i < descriptor.renderIndexCount; ++i) {
      if (metadata.bindPoseVertices.size() >= 12000) { ++skipped; continue; }
" \
    "A summary lod0Truncated = 9, expected 0" \
    "A summary lod0LostTotal =" \
    "A Scout lod0Built = 12000, expected 23442"
}

# B: drop the skin family from the lookup -- every family resolves slot 0's
# texture, which is the "all nine classes wear _red" defect (and heavy/spy wear
# the head texture on the body). The shared resolver is what both the product
# and the audit call, so mutating it moves the published reading.
case_b() {
  run_case b native/include/material_slot_resolution.h \
"  const auto textureSlot = skinFamilies.textureSlotFor(skinFamily, materialSlot);" \
"  const auto textureSlot = skinFamilies.textureSlotFor(0, materialSlot);" \
    "B Scout redBluDistinctSlots = 0, expected >= 1" \
    "B Heavy redBluDistinctSlots = 0, expected >= 1"
}

# C: keep the first companion instead of the best one -- the first include is
# <class>_user_animations.mdl (1 sequence), so separateAnimSource falls back to 0
# and the model keeps its own two-sequence stub, exactly T57's reading.
case_c() {
  run_case c native/src/animation_binding.cpp \
"    if (bestCompanion.empty() || nameBound > bestNameBound) {" \
"    if (bestCompanion.empty()) {" \
    "C scout separateAnimSource = 0, expected 1" \
    "C scout animSeqs = 2, expected >= 200"
}

# meta: a GATE-CONFIG mutation, separate category from the product-source cases.
# It does not touch product code; it changes what the gate expects, proving the
# assertion reads the published field and that a wrong expectation turns the
# gate red (rule 2's meta-mutation: the FAIL count rises from the gate itself).
case_meta() {
  local tag=meta log="$OUT/meta.log"
  echo
  echo "=== case meta: mutate the gate's own expectation (config, not product) ==="
  backup_file "$GATE" "$tag" || { echo "  FAIL could not back up $GATE"; fail_all=1; return; }
  local backup="$BK"
  if ! patch_in "$GATE" \
'assert_eq "A summary lod0Truncated" "$(field "$SUMMARY" lod0Truncated)" "0"' \
'assert_eq "A summary lod0Truncated" "$(field "$SUMMARY" lod0Truncated)" "1"'; then
    echo "  FAIL gate-config patch anchor not found"; fail_all=1; return
  fi
  local rc fails
  rc=$(run_gate "$log" --skip-negative-controls)
  fails=$(count_fails "$log")
  echo "  gate rc=$rc FAIL-count=$fails (baseline $BASELINE_FAILS)"
  if [ "$rc" != "0" ] && [ "$fails" -gt "$BASELINE_FAILS" ] \
     && grep -qF "A summary lod0Truncated = 0, expected 1" "$log"; then
    echo "  OK   gate-config mutation turned the gate red for its own reason"
  else
    echo "  FAIL gate-config mutation did not go red as expected"
    sed 's/^/       | /' "$log" | grep -E 'FAIL' | head -10
    fail_all=1
  fi
  cp -p "$backup" "$GATE"
  if cmp -s "$backup" "$GATE"; then
    echo "  OK   gate restored byte-identical (cmp -s)"
  else
    echo "  FAIL gate restore differs from the backup"; fail_all=1
  fi
  local back_rc back_fails
  back_rc=$(run_gate "$OUT/meta-restored.log" --skip-negative-controls)
  back_fails=$(count_fails "$OUT/meta-restored.log")
  if [ "$back_rc" = "0" ] && [ "$back_fails" = "0" ]; then
    echo "  OK   gate green again after restore (FAIL-count=0)"
  else
    echo "  FAIL gate not green after restore: rc=$back_rc FAIL-count=$back_fails"
    fail_all=1
  fi
}

CASES="${*:-a b c meta}"
for c in $CASES; do
  case "$c" in
    a) case_a ;;
    b) case_b ;;
    c) case_c ;;
    meta) case_meta ;;
    *) echo "unknown case: $c"; fail_all=1 ;;
  esac
done

echo
if [ "$fail_all" = "0" ]; then
  echo "PASS player-model-mutations: A/B/C each went red for its named reason; meta mutation proved the gate itself can fail; every file restored byte-identically; baseline green again."
else
  echo "FAIL player-model-mutations: see the FAIL lines above."
fi
exit "$fail_all"