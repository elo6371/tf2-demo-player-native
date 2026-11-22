cd /d/TF2_Native_Worktrees/t105-gates || exit 1
for f in install-lifecycle-check.sh install-readiness-check.sh lighting-dark-account-check.sh lighting-semantics-check.sh material-coverage-check.sh mutate.sh quality-audit-check.sh slot-freshness-check.sh t21-capture.sh verify-audio.sh viewmodel-companion-check.sh viewmodel-presentation-check.sh viewmodel-range-check.sh viewmodel-robustness-check.sh visual-baseline-check.sh weapon-material-check.sh world-geometry-completeness-capture.sh world-geometry-hole-capture.sh world-material-face-check.sh world-material-semantics-check.sh world-near-clip-check.sh evidence/t85/run-weapon-oom.sh; do
  echo "### $f"
  n=$(grep -n "autorecord_2026-07-02_13-26-46" "$f" | head -1 | cut -d: -f1)
  lo=$((n-3)); [ "$lo" -lt 1 ] && lo=1
  sed -n "${lo},$((n))p" "$f" | sed "s/^/    /"
  # is the path guarded (existence check) or swallowed?
  awk -v n="$n" "NR>n && NR<=n+12" "$f" | grep -nE "FATAL|exit 1|\[ -f|SKIP|WARN" | head -3 | sed "s/^/    GUARD: /"
done