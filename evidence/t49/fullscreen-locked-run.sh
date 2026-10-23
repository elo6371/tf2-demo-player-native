#!/usr/bin/env bash
# fullscreen-locked-run.sh -- everything this gate needs, in ONE lock acquisition.
#
# Run it only through the host lock, never directly:
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t49-desk \
#     bash -c 'bash /d/TF2_Native_Test/evidence/t49/fullscreen-locked-run.sh'
#
# It builds first, then proves the binary is newer than the sources it came from,
# then runs the gate (three phases plus the instrument step), then checks that no
# product process survived. Builds, launches and screen takeovers are all mutations,
# which is why they share one acquisition: a second acquisition would mean a second
# window in which the tree could change between the build and the run.
set -uo pipefail
cd /d/TF2_Native_Test || exit 90

RUN="evidence/t49/run-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN"
exec > >(tee -a "$RUN/locked-run.log") 2>&1

BUILD_RC=1
FRESH=no
GATE_RC=1
MUT_RC=skip
LEFTOVERS=unknown
SAME=unknown
HEAD=$(git rev-parse --short HEAD)
BEFORE=$( { git status --porcelain; git diff; } | sha256sum | cut -d' ' -f1 )

echo "T49-RUN-START $(date '+%Y-%m-%d %H:%M:%S') head=$HEAD tree-before=$BEFORE"
echo "lock: $(cat /d/TF2_Native_Worktrees/_shared/host.lock 2>/dev/null | head -3 | tr '\n' ' ')"

# --- 1. build -------------------------------------------------------------
echo "=== 1/5 build tf2_demo_native ==="
bash build-target.sh tf2_demo_native > "$RUN/build.log" 2>&1
BUILD_RC=$?
echo "build rc=$BUILD_RC log=$RUN/build.log lines=$(wc -l < "$RUN/build.log")"
if [ "$BUILD_RC" -ne 0 ]; then
  echo "--- last 30 build lines ---"
  tail -30 "$RUN/build.log"
fi

# --- 2. the binary must be newer than the sources it was built from -------
echo "=== 2/5 freshness: the exe must be newer than every source it came from ==="
EXE=native/build-nmake/tf2_demo_native.exe
if [ ! -f "$EXE" ]; then
  echo "  FAIL $EXE does not exist"
else
  echo "  exe $(stat -c '%y' "$EXE" | cut -c1-19)"
  newer=$(find native/src native/include \( -name '*.cpp' -o -name '*.h' \) -newer "$EXE" 2>/dev/null | head -5)
  cmnew=$(find native -maxdepth 1 -name 'CMakeLists.txt' -newer "$EXE" 2>/dev/null | head -5)
  if [ -n "$newer$cmnew" ]; then
    echo "  FAIL sources newer than the binary:"
    echo "$newer" | sed 's/^/    /'
    echo "$cmnew" | sed 's/^/    /'
  else
    echo "  OK   no source under native/src, native/include or native/CMakeLists.txt is newer than the exe"
    FRESH=yes
  fi
fi

# --- 3. the gate ----------------------------------------------------------
echo "=== 3/5 gate: baseline, forward, control (+ instrument) ==="
# One invocation, both halves: instrument 14 s + baseline 32 + forward 32 + keyprobe ~26
# + control 32 = the phases, then M1/M2/M3/M5 re-run pieces of them (about 110 s more).
# The mutation half is not optional: without it nothing here shows the assertions can go
# red, and a gate whose assertions have never failed is a gate that only prints.
# The old 16 s phases made this ~70 s; the phases had to grow because the product needs
# ~19.5 s to show a window, and a guard that closes before that reports the same clean
# zeroes as a quiet product.
echo "    the screen is taken over for about 270 s from here; the guard window says so"
bash fullscreen-quiet-check.sh --instrument --mutation > "$RUN/gate.log" 2>&1
GATE_RC=$?
echo "gate rc=$GATE_RC log=$RUN/gate.log"
MUT_LINE=$(grep 'FULLSCREEN-QUIET-MUTATION=' "$RUN/gate.log" | tail -1)
case "$MUT_LINE" in
  *MUTATION=PASS*) MUT_RC=pass ;;
  "") MUT_RC=absent ;;
  *) MUT_RC=fail ;;
esac
echo "mutation half: ${MUT_LINE:-ABSENT}"
sed -n '/=== 0\/3/,$p' "$RUN/gate.log" | sed 's/^/    /'

# --- 4. no product process may survive ------------------------------------
echo "=== 4/5 leftover product processes ==="
LEFTOVERS=$(powershell -NoProfile -Command "if (Get-Process -Name 'tf2_demo_native' -ErrorAction SilentlyContinue) { 'yes' } else { 'no' }" | tr -d '\r')
echo "  product processes alive after the run: $LEFTOVERS"

# --- 5. the tree must be the one that was built ---------------------------
echo "=== 5/5 the tree did not change during the run ==="
AFTER=$( { git status --porcelain; git diff; } | sha256sum | cut -d' ' -f1 )
if [ "$BEFORE" = "$AFTER" ]; then SAME=yes; else SAME=no; fi
echo "  tree-before=$BEFORE tree-after=$AFTER unchanged=$SAME"

cp .scratch/fullscreen-quiet/*.txt .scratch/fullscreen-quiet/*.log "$RUN/" 2>/dev/null
cp .scratch/fullscreen-quiet/*.trace "$RUN/" 2>/dev/null
cp .scratch/fullscreen-quiet/*.bmp "$RUN/" 2>/dev/null

echo "T49-RUN-DONE build=$BUILD_RC fresh=$FRESH gate=$GATE_RC mutation=$MUT_RC leftovers=$LEFTOVERS tree-unchanged=$SAME run=$RUN"
if [ "$BUILD_RC" -eq 0 ] && [ "$FRESH" = "yes" ] && [ "$GATE_RC" -eq 0 ] && [ "$MUT_RC" = "pass" ] && [ "$LEFTOVERS" = "no" ] && [ "$SAME" = "yes" ]; then
  echo "T49-RUN=PASS"
  exit 0
fi
echo "T49-RUN=FAIL"
exit 1
