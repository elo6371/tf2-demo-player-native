#!/usr/bin/env bash
# Test the gate's OWN predicate, extracted from the gate, not a copy of it.
cd /d/TF2_Native_Worktrees/t54-logstf || exit 90
bash -n logstf-check.sh && echo "SYNTAX-OK"
echo "launch_sites=$(grep -cE '^  TF2_NATIVE_TRACE=.*timeout ' logstf-check.sh)"
fn=$(sed -n '/^safety_check()/,/^}/p' logstf-check.sh)
[ -n "$fn" ] || { echo "FATAL: no safety_check in the gate"; exit 3; }
eval "$fn"
safety_check --hidden-window --capture-frame x; echo "flagless_rc=$?  (want 98)"
safety_check --audio-disabled --capture-frame x; echo "nohidden_rc=$?  (want 99)"
safety_check --hidden-window --audio-disabled; echo "good_rc=$?      (want 0)"
