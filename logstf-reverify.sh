#!/usr/bin/env bash
# logstf-reverify.sh -- T54: one command that turns "the CRLF fix works" into a verdict.
#
# WHY FIVE SECTIONS AND NOT ONE
#   The 2026-10-23 run on the integration tree reported MUTATION_RC=1 with four
#   "needle not found" setups and one mutation that printed nothing at all. Each
#   of those was invisible in a different way, so each gets its own gate here.
#
#   0/5  the binary is the current source. The same run left the tree holding a
#        MUTANT build (m8's, because m9's setup failed and the restore step sits
#        after the failure path). A reading taken against that binary says the
#        scroll script never runs, which is true of the mutant, not the product.
#        THIS IS THE MOST IMPORTANT SECTION: without it, every later number can
#        be a number about a binary nobody chose.
#   1/5  the line ending of every file the mutation patches. It is the hidden
#        variable of this repository (core.autocrlf=true makes the integration
#        tree CRLF and a fresh worktree LF), and it is what made 5 of 9 patches
#        no-ops on one tree while the same gate was green on another.
#   2/5  forward. This half was already valid before the fix; keep it here so a
#        regression in the mutation path cannot be mistaken for a forward one.
#   3/5  mutation: every one of the nine must be CAUGHT and NAMED. A mutant that
#        is absent from the log counts as INVALID, because absence is exactly
#        how m3 failed silently.
#   4/5  the restore. It runs from a trap, so it also runs when section 2 or 3
#        exits early -- which is the only case where it matters.
#
# USAGE
#   bash logstf-reverify.sh          # run it under the host lock
#
# EXIT
#   0 only when forward is green AND 9/9 mutants were caught by name AND the
#   tree ends with a binary built from clean sources.

set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/logstf_viewer_probe.exe
SRC=native/src/logstf_viewer.cpp
GATE=logstf-check.sh
OUT=evidence/logstf/reverify
MUTANTS="m1_wrong_host m2_empty_prefetch m3_silent_runtime m4_resize_unclamped m5_pending_navigation m6_script_never_runs m7_history_flag_only m8_json_undecoded m9_script_truncated"

mkdir -p "$OUT"
step() { printf '\n=== %s ===\n' "$*"; }
die() { printf 'REVERIFY=FAIL reason=%s\n' "$*"; exit 1; }

build() {
  # One place that builds, so the preflight and the restore cannot drift apart.
  bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/$1.log" 2>&1
}

restore_binaries() {
  # Runs on every exit path, including the early ones. Section 4.
  local rc=$?
  printf '\n=== 4/5 the tree ends with a real binary ===\n'
  build final-rebuild || { printf '  FAIL the final rebuild failed; the tree holds whatever the last mutant built\n'; printf 'BINARY-FRESH=0\n'; return; }
  local dirty
  dirty=$(git status --porcelain -- native | wc -l | tr -d ' ')
  if [ "$dirty" != "0" ]; then
    printf '  FAIL %s paths under native/ are still modified; the mutation did not restore them\n' "$dirty"
    git status --porcelain -- native | sed 's/^/    /'
    printf 'BINARY-FRESH=0\n'
    return
  fi
  if [ ! "$EXE" -nt "$SRC" ]; then
    printf '  FAIL the rebuilt exe is not newer than the source\n'
    printf 'BINARY-FRESH=0\n'
    return
  fi
  printf '  OK   sources clean and the exe is newer than them\n'
  printf 'BINARY-FRESH=1 source=HEAD exe=%s\n' "$(stat -c %y "$EXE")"
}
trap restore_binaries EXIT

# ---- 0/5 ------------------------------------------------------------------
step "0/5 the binary is the current source"
[ -f "$EXE" ]   || die "missing $EXE"
[ -f "$PROBE" ] || die "missing $PROBE"
dirty=$(git status --porcelain -- native | wc -l | tr -d ' ')
[ "$dirty" = "0" ] || die "native/ has $dirty modified paths; a reading now would describe edited sources"
if [ ! "$EXE" -nt "$SRC" ]; then
  printf '  the exe is not newer than %s; rebuilding before measuring\n' "$SRC"
  build preflight-rebuild || die "the preflight rebuild failed"
  [ "$EXE" -nt "$SRC" ] || die "the rebuild did not produce a newer exe"
  printf '  rebuilt\n'
fi
printf '  OK   exe=%s\n       src=%s\n' "$(stat -c %y "$EXE")" "$(stat -c %y "$SRC")"

# ---- 1/5 ------------------------------------------------------------------
step "1/5 the line ending of every patched file"
for f in $(grep -oE 'native/src/[A-Za-z0-9_./-]+\.(cpp|h)' "$GATE" | sort -u); do
  [ -f "$f" ] || { printf '  FAIL %s is named by the gate but missing\n' "$f"; exit 1; }
  cr=$(tr -cd '\r' < "$f" | wc -c | tr -d ' ')
  printf '  CR=%-5s %s\n' "$cr" "$f"
done
printf '  (core.autocrlf=%s -- this is why the same needles can match on one tree and not another)\n' "$(git config core.autocrlf || echo unset)"

# ---- 2/5 ------------------------------------------------------------------
step "2/5 forward"
bash "$GATE" > "$OUT/forward.log" 2>&1
FRC=$?
printf '  gate exit=%s\n' "$FRC"
grep -E '^  (OK|FAIL)|^LOGSTF=|^FORWARD_RC=|^assertions' "$OUT/forward.log" | tail -5
grep -q '^LOGSTF=PASS' "$OUT/forward.log" || { printf 'REVERIFY=FAIL reason=forward-not-pass\n'; exit 1; }
[ "$FRC" = "0" ] || { printf 'REVERIFY=FAIL reason=forward-exit-%s\n' "$FRC"; exit 1; }

# ---- 3/5 ------------------------------------------------------------------
step "3/5 mutation: nine mutants, each CAUGHT and NAMED"
bash "$GATE" --mutation > "$OUT/mutation.log" 2>&1
MRC=$?
printf '  gate exit=%s\n' "$MRC"
bad=0
for m in $MUTANTS; do
  if grep -q "MUTATION-CAUGHT=PASS ($m" "$OUT/mutation.log"; then
    printf '  OK   %-22s caught\n' "$m"
  elif grep -q "MUTATION-SETUP=FAIL ($m" "$OUT/mutation.log"; then
    printf '  FAIL %-22s SETUP=FAIL (the patch changed nothing -- check the line ending)\n' "$m"
    bad=$((bad + 1))
  elif grep -q "MUTATION-CAUGHT=FAIL ($m" "$OUT/mutation.log"; then
    printf '  FAIL %-22s the gate stayed green\n' "$m"
    bad=$((bad + 1))
  else
    printf '  FAIL %-22s NO VERDICT AT ALL (silence is the m3 failure mode)\n' "$m"
    bad=$((bad + 1))
  fi
done
grep -q 'LOGSTF-MUTATION=PASS' "$OUT/mutation.log" || { printf '  note: the gate did not print LOGSTF-MUTATION=PASS\n'; }
if [ "$bad" != "0" ] || [ "$MRC" != "0" ]; then
  printf 'REVERIFY=FAIL reason=mutation bad=%s exit=%s\n' "$bad" "$MRC"
  exit 1
fi
printf 'REVERIFY=PASS forward=0 mutation=9/9\n'
