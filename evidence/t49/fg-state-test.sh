#!/usr/bin/env bash
# Unit test for the gate's decision functions, without running a product.
#
# fg_state_at is the function that decides whether the guard was the foreground at the
# instant the product created its window. A decision function that cannot be shown to
# return "background" is worth nothing, so the timeline is tested directly: every case
# below is a shape this desktop has actually produced.
set -uo pipefail
cd /d/TF2_Native_Test || exit 90

# Load the gate's definitions up to the point where it starts doing things.
HEAD=$(grep -n '^echo "product=' fullscreen-quiet-check.sh | head -1 | cut -d: -f1)
if [ -z "$HEAD" ]; then echo "TEST-FAIL could not find the execution boundary in the gate"; exit 1; fi
source <(sed -n "1,$((HEAD - 1))p" fullscreen-quiet-check.sh)

W=/d/TF2_Native_Test/.scratch/t49/fgtest
mkdir -p "$W"
mk() { # mk <file> <startEpochMs> <timeline>
  printf 'guard.startEpochMs=%s\nguard.fgTransitions=%s\n' "$2" "$3" > "$W/$1.guard.txt"
}

pass=0; bad=0
chk() { # chk <label> <expected> <actual>
  if [ "$2" = "$3" ]; then echo "  OK   $1 -> $3"; pass=$((pass + 1));
  else echo "  FAIL $1: got '$3' want '$2'"; bad=$((bad + 1)); fi
}

# The shape the Lead's correction named: held at 1 s, taken at 5 s, retaken at 6.1 s.
mk a 100000 '+1000,-5000,+6100'
chk "before the first acquisition" background "$(fg_state_at "$W/a.guard.txt" 100500)"
chk "while held" foreground "$(fg_state_at "$W/a.guard.txt" 103000)"
chk "while lost" background "$(fg_state_at "$W/a.guard.txt" 105500)"
chk "after the retake, at the window instant" foreground "$(fg_state_at "$W/a.guard.txt" 119700)"
# The event exactly ON a transition is taken as the new state: the sample that observed it
# is the sample that recorded it.
chk "exactly at a loss" background "$(fg_state_at "$W/a.guard.txt" 105000)"
chk "exactly at an acquisition" foreground "$(fg_state_at "$W/a.guard.txt" 101000)"

# A guard that never took the foreground is not "foreground": the empty timeline is a
# reading, and it must not be read as a pass.
mk b 100000 ''
chk "never foreground (empty timeline)" ABSENT "$(fg_state_at "$W/b.guard.txt" 120000)"

# A guard that took it and never lost it.
mk c 100000 '+1200'
chk "held from 1.2 s to the window instant" foreground "$(fg_state_at "$W/c.guard.txt" 119500)"

# Missing clock: the instant cannot be placed, so the answer is not a state.
printf 'guard.fgTransitions=+1200\n' > "$W/d.guard.txt"
chk "no start clock" ABSENT "$(fg_state_at "$W/d.guard.txt" 119500)"

# A malformed entry must not silently become "foreground".
mk e 100000 '+1000,garbage,-5000'
chk "malformed entry skipped, still background after the loss" background "$(fg_state_at "$W/e.guard.txt" 106000)"

echo "FGTEST pass=$pass fail=$bad"
[ "$bad" -eq 0 ]
