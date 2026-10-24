#!/usr/bin/env bash
# run-mutations.sh -- T69 mutation suite for the PURE minimap logic.
#
# Each case patches ONE anchor in the product source, rebuilds, and requires the
# gate to fail on a NAMED assertion. Three outcomes are kept apart, because
# collapsing them is how a mutation suite lies:
#
#   MUTATION-CAUGHT   the mutated build compiled (rc=0) AND the named assertion
#                     turned red
#   MUTATION-MISSED   the mutated build compiled AND the gate stayed green
#   MUTATION-INVALID  the anchor was missing/ambiguous, or the build failed --
#                     a build error is NOT a caught mutation, because nothing ran
#
# After every case the original bytes are restored, cmp proves it, the baseline
# is rebuilt and the gate must return to PASS. Without that final baseline the
# suite would not show that the restores actually worked.
#
# Usage: bash evidence/t69-minimap/run-mutations.sh   (from the repo root)
# Exit:  0 = all cases caught and the baseline returned green; 1 = otherwise.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2

PROBE_SRC=native/src/minimap.cpp
HEADER_SRC=native/include/minimap.h
WORK=.scratch/mut
mkdir -p "$WORK/needles" "$WORK/backup" "$WORK/gates"

SUITE_RC=0
caught=0
total=4

# ---------------------------------------------------------------- anchors ---
# m1: the y flip is removed. The flip branch becomes the same sign as the
# non-flip branch, so world +y no longer maps to a smaller pixel y.
cat > "$WORK/needles/m1.needle" <<'NEEDLE'
projection.offsetY - worldY * projection.scale
NEEDLE
cat > "$WORK/needles/m1.repl" <<'REPL'
projection.offsetY + worldY * projection.scale
REPL

# m2: "first visible hit wins" instead of "nearest wins".
cat > "$WORK/needles/m2.needle" <<'NEEDLE'
if (bestIndex < 0 || closer || tieWithSmallerIndex) {
NEEDLE
cat > "$WORK/needles/m2.repl" <<'REPL'
if (bestIndex < 0) {
REPL

# m3: the panel starts expanded instead of collapsed.
cat > "$WORK/needles/m3.needle" <<'NEEDLE'
  bool collapsed_ = true;
NEEDLE
cat > "$WORK/needles/m3.repl" <<'REPL'
  bool collapsed_ = false;
REPL

# m4: the panel's hit test ignores its own zoom (draws zoomed, hit-tests unzoomed).
cat > "$WORK/needles/m4.needle" <<'NEEDLE'
  const int result = minimapHitTest(players, bounds_, projection(), px, py, radiusPx);
NEEDLE
cat > "$WORK/needles/m4.repl" <<'REPL'
  const int result = minimapHitTest(players, bounds_, makeMinimapProjection(bounds_, 1.0f, panX_, panY_), px, py, radiusPx);
REPL

restore_case() {
  local name="$1" target="$2" backup="$3"
  cp "$backup" "$target"
  if cmp -s "$backup" "$target"; then
    echo "RESTORE-OK name=$name"
  else
    echo "RESTORE-FAIL name=$name"
    SUITE_RC=1
  fi
  bash build-target.sh minimap_probe > "$WORK/$name.rebuild.log" 2>&1
  local rrc=$?
  bash minimap-check.sh > "$WORK/gates/$name.baseline.log" 2>&1
  local grc=$?
  if [ "$rrc" -eq 0 ] && [ "$grc" -eq 0 ] && grep -q '^MINIMAP-CHECK=PASS$' "$WORK/gates/$name.baseline.log"; then
    echo "BASELINE-RESTORED name=$name rebuild_rc=0 gate_rc=0"
  else
    echo "BASELINE-BROKEN name=$name rebuild_rc=$rrc gate_rc=$grc"
    SUITE_RC=1
  fi
}

run_case() {
  local name="$1" target="$2" fail_token="$3"
  local base backup prc brc grc
  base=$(basename "$target")
  backup="$WORK/backup/$name.$base"
  cp "$target" "$backup" || { echo "MUTATION-INVALID name=$name reason=backup-failed"; SUITE_RC=1; return; }
  python evidence/t69-minimap/mutation/patch_once.py "$target" "$WORK/needles/$name.needle" "$WORK/needles/$name.repl" > "$WORK/$name.patch.log" 2>&1
  prc=$?
  if [ "$prc" -ne 0 ]; then
    local why
    if [ "$prc" -eq 3 ]; then why=anchor-missing; else why=anchor-ambiguous; fi
    echo "MUTATION-INVALID name=$name reason=$why patch_rc=$prc"
    sed 's/^/  /' "$WORK/$name.patch.log"
    restore_case "$name" "$target" "$backup"
    return
  fi
  echo "PATCH-OK name=$name $(cat "$WORK/$name.patch.log")"
  bash build-target.sh minimap_probe > "$WORK/$name.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ]; then
    echo "MUTATION-INVALID name=$name reason=does-not-compile build_rc=$brc"
    tail -20 "$WORK/$name.build.log" | sed 's/^/  /'
    restore_case "$name" "$target" "$backup"
    return
  fi
  echo "MUTATED-BUILD-RC=0 name=$name"
  bash minimap-check.sh > "$WORK/gates/$name.log" 2>&1
  grc=$?
  if grep -qF "FAIL $fail_token" "$WORK/gates/$name.log"; then
    echo "MUTATION-CAUGHT name=$name assertion=\"$fail_token\" gate_rc=$grc"
    caught=$((caught + 1))
  else
    echo "MUTATION-MISSED name=$name assertion=\"$fail_token\" gate_rc=$grc (the gate stayed green)"
    SUITE_RC=1
  fi
  restore_case "$name" "$target" "$backup"
}

echo "=== T69 minimap mutation suite ==="
echo "=== pre-mutation baseline ==="
bash build-target.sh minimap_probe > "$WORK/baseline.build.log" 2>&1
brc=$?
bash minimap-check.sh > "$WORK/gates/baseline.log" 2>&1
grc=$?
if [ "$brc" -eq 0 ] && [ "$grc" -eq 0 ] && grep -q '^MINIMAP-CHECK=PASS$' "$WORK/gates/baseline.log"; then
  echo "BASELINE=PASS build_rc=0 gate_rc=0"
else
  echo "BASELINE=FAIL build_rc=$brc gate_rc=$grc"
  tail -20 "$WORK/gates/baseline.log"
  echo "MUTATION-SUITE=FAIL"
  exit 1
fi

run_case m1 "$PROBE_SRC" "yflip:"
run_case m2 "$PROBE_SRC" "hittest nearest:"
run_case m3 "$HEADER_SRC" "panel collapsed initial:"
run_case m4 "$PROBE_SRC" "zoom2 hittest:"

echo "mutations_caught=$caught/$total"
if [ "$caught" -eq "$total" ] && [ "$SUITE_RC" -eq 0 ]; then
  echo "MUTATION-SUITE=PASS"
  exit 0
fi
echo "MUTATION-SUITE=FAIL"
exit 1
