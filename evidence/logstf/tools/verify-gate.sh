#!/usr/bin/env bash
# Test the gate's OWN safety predicate, extracted from the gate file itself.
#
# Extracting rather than copying matters: a self-test that re-implements the
# predicate tests the copy, not the thing that launches the product. The earlier
# predicate required " --hidden-window " immediately followed by
# " --audio-disabled ", which never matches when another argument sits between
# them -- it would have refused every correct argv. The gate's own self-test caught
# that; this tool re-catches it without a full run.
cd /d/TF2_Native_Worktrees/t54-logstf || exit 90

echo "SYNTAX=$(bash -n logstf-check.sh && echo OK || echo FAIL)"
# Count launch sites by the launch LINE's shape, not the bare command: the first
# version counted its own grep line and reported two. A whitespace class (not two
# literal spaces) keeps a reformat from silently turning the count into zero -- a
# check that passes by measuring nothing.
echo "launch_sites=$(grep -cE '^[[:space:]]+TF2_NATIVE_TRACE=.*timeout ' logstf-check.sh)  (want 1)"

fn=$(sed -n '/^safety_check()/,/^}/p' logstf-check.sh)
[ -n "$fn" ] || { echo "FATAL: no safety_check in the gate"; exit 3; }
eval "$fn"

# Three argv shapes, and this tool ASSERTS the codes instead of printing them:
#   only --hidden-window present   -> the audio flag is missing  -> 98
#   only --audio-disabled present  -> the window flag is missing -> 99
#   both present                   -> accepted                   -> 0
# The first test is "missing --audio-disabled", NOT "flagless": a truly flagless
# argv is rejected by whichever case runs first (99 here), and a report that calls
# the 98 case "flagless" states something the code cannot back. The code is a
# diagnosis aid, so pinning it here is fine -- but do not describe the test as
# something broader than it is.
rc=0
safety_check --hidden-window --capture-frame x; code=$?
[ "$code" -eq 98 ] || { echo "FAIL: missing_audio rc=$code (want 98)"; rc=3; }
safety_check --audio-disabled --capture-frame x; code=$?
[ "$code" -eq 99 ] || { echo "FAIL: missing_window rc=$code (want 99)"; rc=3; }
safety_check --hidden-window --audio-disabled; code=$?
[ "$code" -eq 0 ] || { echo "FAIL: good_argv rc=$code (want 0)"; rc=3; }
[ "$rc" -eq 0 ] && echo "PREDICATE=OK (missing audio -> 98, missing window -> 99, both -> 0)"
exit "$rc"
