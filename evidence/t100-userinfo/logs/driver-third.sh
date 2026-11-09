#!/usr/bin/env bash
# Third and last locked driver for t100-userinfo: product build, gate baseline,
# then the mutation suite. The lock is taken by the hostlock run that wraps it.
set -u
cd /d/TF2_Native_Worktrees/t100-userinfo || exit 1
LOG=evidence/t100-userinfo/logs
mkdir -p "$LOG"
echo "start=$(date -Iseconds)"
echo "head=$(git rev-parse HEAD)"
echo "branch=$(git rev-parse --abbrev-ref HEAD)"
echo "host_owner=$(cat /d/TF2_Native_Worktrees/_shared/host.lock/owner 2>/dev/null)"
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status 2>&1 || true
sha256sum native/build-nmake/demo_open_probe.exe > "$LOG/exe-before-sha256.txt" 2>&1 || true
cat "$LOG/exe-before-sha256.txt"

echo "=== product build ==="
bash build-target.sh tf2_demo_native > "$LOG/build-product.log" 2>&1
echo "product_build_rc=$?"
sha256sum native/build-nmake/tf2_demo_native.exe > "$LOG/product-exe-sha256.txt" 2>&1 || true
cat "$LOG/product-exe-sha256.txt"
ls -l native/build-nmake/tf2_demo_native.exe 2>/dev/null || true

echo "=== gate baseline ==="
bash userinfo-check.sh > "$LOG/gate-baseline.txt" 2>&1
echo "gate_baseline_rc=$?"
echo "baseline_asserts=$(grep -c '^ok ' "$LOG/gate-baseline.txt") baseline_fails=$(grep -c '^FAIL ' "$LOG/gate-baseline.txt")"
tail -3 "$LOG/gate-baseline.txt"
sha256sum native/build-nmake/demo_open_probe.exe > "$LOG/exe-after-baseline-sha256.txt" 2>&1 || true

echo "=== gate mutation suite ==="
# The full baseline above is the baseline; do not pay for it twice.
bash userinfo-check.sh --mutation --skip-baseline > "$LOG/gate-mutation.txt" 2>&1
echo "gate_mutation_rc=$?"
grep -E '^(MUTATION-CAUGHT|BASELINE-GREEN|BASELINE-GREEN-FULL|RESTORED|mutation [a-z0-9]+ build rc|restore build rc|restored full gate rc|ASSERTIONS=|RESULT=)' "$LOG/gate-mutation.txt" || true
echo "mutation_asserts=$(grep -c '^ok ' "$LOG/gate-mutation.txt") mutation_fails=$(grep -c '^FAIL ' "$LOG/gate-mutation.txt")"
sha256sum native/build-nmake/demo_open_probe.exe > "$LOG/exe-after-mutation-sha256.txt" 2>&1 || true
cat "$LOG/exe-after-mutation-sha256.txt"
echo "end=$(date -Iseconds)"
