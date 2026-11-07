#!/usr/bin/env bash
# T90 final window driver: before-evidence, then the gate in ALL THREE modes.
# One long lease, because the lock is the scarce resource. Each stage records
# its rc and the driver CONTINUES, so a red in one mode still yields the other
# two readings instead of wasting the window. Exit is 0 only if all three are 0.
# Every product launch carries --settings-file/--ui-settings-file copies, so the
# product's unconditional save-on-exit cannot touch the user's real files.
set -uo pipefail
cd /d/TF2_Native_Worktrees/t90-identity
L=.scratch/t90
mkdir -p "$L/inputs"
T79=/d/TF2_Native_Worktrees/t79-integration/native/build-nmake/tf2_demo_native.exe
LA_UX="$(cygpath -u "${LOCALAPPDATA-}" 2>/dev/null)"
if [ -n "${LA_UX-}" ] && [ -d "$LA_UX/TF2 Demo Player" ]; then
  cp "$LA_UX/TF2 Demo Player/settings.ini" "$L/inputs/settings.ini" 2>/dev/null || true
  cp "$LA_UX/TF2 Demo Player/ui.ini" "$L/inputs/ui.ini" 2>/dev/null || true
  sha256sum "$LA_UX/TF2 Demo Player/settings.ini" "$LA_UX/TF2 Demo Player/ui.ini" > "$L/real-settings-sha-before.txt" 2>&1 || true
  stat -c '%n %y' "$LA_UX/TF2 Demo Player/settings.ini" "$LA_UX/TF2 Demo Player/ui.ini" > "$L/real-settings-stat-before.txt" 2>&1 || true
fi
SETTINGS=(--settings-file "$L/inputs/settings.ini" --ui-settings-file "$L/inputs/ui.ini")

echo "=== final window start $(date -u +%FT%TZ) host=$(hostname)"
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status > "$L/hostlock-status-at-start-final.txt" 2>&1 || true
cat "$L/hostlock-status-at-start-final.txt"
git rev-parse HEAD > "$L/final-window-head.txt" 2>&1
git rev-parse --short=12 HEAD >> "$L/final-window-head.txt" 2>&1
git status --porcelain -uall > "$L/final-window-tree-status.txt" 2>&1
echo "tree_records_at_start=$(wc -l < "$L/final-window-tree-status.txt")"
sha256sum build-identity-check.sh > "$L/final-window-gate-sha256.txt" 2>&1

# ---- before: the frozen candidate's own answer ------------------------------
sha256sum "$T79" > "$L/before-exe.sha256" 2>&1
stat -c '%n size=%s mtime=%y' "$T79" > "$L/before-exe.stat" 2>&1
"$T79" --version "${SETTINGS[@]}" > "$L/before-version.txt" 2>&1
echo "before_version_rc=$?" >> "$L/before-version.txt"
git -C /d/TF2_Native_Worktrees/t79-integration rev-parse HEAD > "$L/before-t79-head.txt" 2>&1
git -C /d/TF2_Native_Worktrees/t79-integration log -1 --format='%H %ct %ci' >> "$L/before-t79-head.txt" 2>&1
echo "--- before --version (frozen t79 candidate) ---"
cat "$L/before-version.txt"
cat "$L/before-exe.sha256" "$L/before-exe.stat"

# ---- stage A: forward ------------------------------------------------------
echo "=== stage A: gate --forward $(date -u +%FT%TZ)"
bash build-identity-check.sh --forward "$L/forward-final" > "$L/gate-forward-final.log" 2>&1
RC_FWD=$?
echo "stage_A_forward_rc=$RC_FWD"
grep -E "^(BUILD-IDENTITY=|  FAIL|=== )" "$L/gate-forward-final.log" | head -80

# ---- stage B: mutation -----------------------------------------------------
echo "=== stage B: gate --mutation $(date -u +%FT%TZ)"
bash build-identity-check.sh --mutation "$L/mutation-final" > "$L/gate-mutation-final.log" 2>&1
RC_MUT=$?
echo "stage_B_mutation_rc=$RC_MUT"
grep -E "^(BUILD-IDENTITY|MUTATION|  FAIL|=== )" "$L/gate-mutation-final.log" | head -120

# ---- stage C: reproducible -------------------------------------------------
echo "=== stage C: gate --reproducible $(date -u +%FT%TZ)"
bash build-identity-check.sh --reproducible "$L/repro-final" > "$L/gate-repro-final.log" 2>&1
RC_REP=$?
echo "stage_C_reproducible_rc=$RC_REP"
grep -E "^(BUILD-REPRODUCIBLE=|  FAIL|  --- build|  --- HEAD|=== )" "$L/gate-repro-final.log" | head -80

# ---- after readings --------------------------------------------------------
EXE=native/build-nmake/tf2_demo_native.exe
sha256sum "$EXE" > "$L/after-exe.sha256" 2>&1
stat -c '%n size=%s mtime=%y' "$EXE" >> "$L/after-exe.sha256" 2>&1
"$EXE" --version "${SETTINGS[@]}" > "$L/after-version-final-tree.txt" 2>&1
echo "after_version_rc=$?" >> "$L/after-version-final-tree.txt"
echo "--- after --version (this tree) ---"
cat "$L/after-version-final-tree.txt"
git status --porcelain -uall > "$L/final-window-tree-status-at-end.txt" 2>&1
echo "tree_records_at_end=$(wc -l < "$L/final-window-tree-status-at-end.txt")"
{ echo "forward_rc=$RC_FWD"; echo "mutation_rc=$RC_MUT"; echo "reproducible_rc=$RC_REP"; } > "$L/final-rc.txt"
cat "$L/final-rc.txt"
if [ -n "${LA_UX-}" ] && [ -d "$LA_UX/TF2 Demo Player" ]; then
  sha256sum "$LA_UX/TF2 Demo Player/settings.ini" "$LA_UX/TF2 Demo Player/ui.ini" > "$L/real-settings-sha-after.txt" 2>&1 || true
  stat -c '%n %y' "$LA_UX/TF2 Demo Player/settings.ini" "$LA_UX/TF2 Demo Player/ui.ini" > "$L/real-settings-stat-after.txt" 2>&1 || true
  if cmp -s "$L/real-settings-sha-before.txt" "$L/real-settings-sha-after.txt"; then
    echo "real_settings_sha_unchanged=yes"
  else
    echo "real_settings_sha_unchanged=no (see real-settings-sha-*.txt)"
  fi
fi
echo "=== final window done $(date -u +%FT%TZ)"
if [ "$RC_FWD" -eq 0 ] && [ "$RC_MUT" -eq 0 ] && [ "$RC_REP" -eq 0 ]; then exit 0; fi
exit 1
