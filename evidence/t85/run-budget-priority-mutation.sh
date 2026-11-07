#!/usr/bin/env bash
# T85 P0: BEHAVIOURAL MUTATION -- the prepare pass reverted to first-come-first-served.
#
# WHAT IT PROVES. The P0 fix is not "a bigger constant": it is that the 64 prepare
# slots are spent by PRIORITY (player body > held weapon > overlay > prop) instead of
# by the order the entities happened to arrive. This script reverts exactly that one
# decision -- the sort body in the prepare pass -- rebuilds, and runs the gate's own
# named-model section (outline-check.sh --world-only, the same assertions the gate
# ships) before and after the restore:
#
#   BEFORE trace (frozen t79 candidate, evidence/t85/upward-pyro/before.trace):
#     64 world models + 9 overlay class models + 23 attach uploads = 96 = the table's
#     capacity, and exactly the last three attach uploads were refused with
#     0x8007000E: c_sapper_xmas, c_shogun_warhorn, c_flamethrower.
#   With the priority sort: 43 of the 64 prepare slots are weapons, the attach needs
#     only 13 uploads, the table never fills, nothing is refused.
#   With the sort reverted: the old admission order comes back, the attach needs the
#     old 23 uploads, and the named assertions must go RED.
#
# WHY NOT mutate.sh. mutate.sh:59 refuses to run while native/ has uncommitted
# changes, and this worktree carries the fix itself, so its case m14 can only run
# after the commit. This script is the same patch expressed as a one-shot runner so
# the red/green pair can be recorded now; m14 in mutate.sh points at the same gate
# section for the long term.
#
# Usage (hostlock wraps it): bash evidence/t85/run-budget-priority-mutation.sh
set -uo pipefail
cd "$(dirname "$0")/../.."
SRC=native/src/main.cpp
BACKUP=.scratch/t85-mut-priority-main.cpp.bak
OUT=evidence/t85/mut-priority
EXE=native/build-nmake/tf2_demo_native.exe
FIND='    const auto leftRank = static_cast<std::size_t>(prepareCandidates[left].priority);'
REPL='    (void)leftRank; (void)rightRank; return left < right;  /* mutation m14: first-come-first-served */'
mkdir -p "$OUT" .scratch

echo "== T85 P0 behavioural mutation: prepare priority sort -> first-come-first-served =="
echo "git commit   = $(git rev-parse HEAD)"
echo "git branch   = $(git rev-parse --abbrev-ref HEAD)"
echo "source       = $SRC"
echo "source sha256(before) = $(sha256sum "$SRC" | cut -d' ' -f1)"
echo "gate sha256  = $(sha256sum outline-check.sh | cut -d' ' -f1)"

cp "$SRC" "$BACKUP" || { echo "FATAL: cannot back up $SRC"; exit 2; }
# The patch: drop the two priority lines the sort body is made of and return the
# demo's own order. patch_in's contract is kept -- the literal must occur EXACTLY
# once, otherwise this is MUTATION-INVALID, not "the reading did not move".
python - "$SRC" <<'PY' || exit 3
import io, sys
path = sys.argv[1]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
find = '''          const auto leftRank = static_cast<std::size_t>(prepareCandidates[left].priority);
          const auto rightRank = static_cast<std::size_t>(prepareCandidates[right].priority);
          if (leftRank != rightRank) return leftRank < rightRank;
          return left < right;  // same priority: the demo's own order decides'''
repl = '''          /* mutation m14: the priority order is gone -- first-come-first-served */
          (void)prepareCandidates;
          return left < right;'''
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
if data.count(find) != 1:
    sys.exit('MUTATION-INVALID: the sort body was found %d times' % data.count(find))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
print('patched: priority sort replaced (1 occurrence)')
PY
echo "source sha256(mutated) = $(sha256sum "$SRC" | cut -d' ' -f1)"
grep -n 'mutation m14' "$SRC" | head -3

echo
echo "-- rebuild (mutated) --"
bash build-target.sh tf2_demo_native > "$OUT/build-mutated.log" 2>&1
BUILD_RC=$?
echo "build rc=$BUILD_RC  exe sha256=$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)  log=$OUT/build-mutated.log"
[ "$BUILD_RC" = "0" ] || { echo "MUTATION-BUILD-FAIL"; cp "$BACKUP" "$SRC"; exit 4; }

echo
echo "-- the gate's own named-model section, on the mutated build (must be RED) --"
OUTLINE_OUT="$OUT/red-out" bash outline-check.sh --world-only > "$OUT/red.log" 2>&1
RED_RC=$?
echo "red run rc=$RED_RC (expect non-zero)"
grep -a -E 'named world models|world layer|attach: the |world rc=' "$OUT/red.log" | head -40
echo "FAIL lines:"
grep -a 'FAIL' "$OUT/red.log" | head -20

echo
echo "-- restore (byte-exact, from the backup; NOT git checkout -- this tree is uncommitted work) --"
cp "$BACKUP" "$SRC"
echo "source sha256(restored) = $(sha256sum "$SRC" | cut -d' ' -f1)"
if cmp -s "$BACKUP" "$SRC"; then echo "restore: byte-identical (cmp)"; else echo "restore: MISMATCH"; exit 5; fi
echo "-- rebuild (restored) --"
bash build-target.sh tf2_demo_native > "$OUT/build-restored.log" 2>&1
RBUILD_RC=$?
echo "build rc=$RBUILD_RC  exe sha256=$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)"
[ "$RBUILD_RC" = "0" ] || { echo "RESTORE-BUILD-FAIL"; exit 6; }

echo
echo "-- the same section on the restored build (must be GREEN) --"
OUTLINE_OUT="$OUT/green-out" bash outline-check.sh --world-only > "$OUT/green.log" 2>&1
GREEN_RC=$?
echo "green run rc=$GREEN_RC (expect 0)"
grep -a -E 'named world models|world layer|world rc=' "$OUT/green.log" | head -40

echo
RED_NAMED=$(grep -a -c 'FAIL named world models' "$OUT/red.log" || true)
GREEN_NAMED=$(grep -a -c 'FAIL named world models' "$OUT/green.log" || true)
echo "named FAIL lines: mutated=$RED_NAMED restored=$GREEN_NAMED"
if [ "$RED_RC" != "0" ] && [ "$RED_NAMED" -ge 1 ] && [ "$GREEN_RC" = "0" ] && [ "$GREEN_NAMED" = "0" ]; then
  echo "BUDGET-PRIORITY-MUTATION=PASS (the named assertions red only while the priority sort is gone)"
  exit 0
fi
echo "BUDGET-PRIORITY-MUTATION=FAIL"
exit 1
