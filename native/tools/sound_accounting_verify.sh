#!/usr/bin/env bash
# sound_accounting_verify.sh <demo_open_probe.exe> <demo.dem>
#
# WHY THIS EXISTS
#   P3b read `sound_failures=20923` on pl_upward_f12 as 20923 decode failures. 20921 of them were
#   not failures: they were messages dropped by the 4096-message payload cap, which still entered
#   the decode loop with an empty payload, so the loop's first bit read incremented
#   decodedSoundEventFailures once and broke. The drop and the decode break are now counted apart
#   (native/src/demo_header_sound.cpp): `sound_dropped` is the designed-drop total, split into
#   `sound_dropped_payload_too_large` / `sound_dropped_message_cap`, and `sound_failures` counts
#   only messages whose payload was captured. This script re-reads that split and asserts it.
#
#   This is an ACCOUNTING check. A green run does not say "no audio is lost": the 4096-message cap
#   still drops messages by design, and nothing here measures whether audio plays.
#
# Usage: bash native/tools/sound_accounting_verify.sh <probe.exe> <demo.dem>
# Exit:  0 = identities hold, 1 = a named identity failed, 2 = bad usage.
set -uo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: bash native/tools/sound_accounting_verify.sh <demo_open_probe.exe> <demo.dem>" >&2
  exit 2
fi

PROBE=$1
DEMO=$2
if [ ! -f "$PROBE" ]; then echo "FAIL: probe not found: $PROBE" >&2; exit 1; fi
if [ ! -f "$DEMO" ]; then echo "FAIL: demo not found: $DEMO" >&2; exit 1; fi

OUT="$("$PROBE" --scan "$DEMO" 2>&1)"
RC=$?
if [ "$RC" -ne 0 ]; then echo "FAIL: probe exited rc=$RC"; printf '%s\n' "$OUT" | tail -n 5; exit 1; fi

LINE="$(printf '%s\n' "$OUT" | grep -m1 'sound_messages=')"
if [ -z "$LINE" ]; then
  echo "FAIL: scan line carries no sound_messages= (stale probe binary? the split needs the counters added with task B)"
  exit 1
fi

get() { printf '%s' "$LINE" | grep -o "$1=[0-9]*" | head -n1 | cut -d= -f2; }
MESSAGES=$(get sound_messages)
DROPPED=$(get sound_dropped)
TOO_LARGE=$(get sound_dropped_payload_too_large)
MESSAGE_CAP=$(get sound_dropped_message_cap)
FAILURES=$(get sound_failures)
DECODED=$(get sound_decoded)
SOUNDS=$(get sounds)

for name in MESSAGES DROPPED TOO_LARGE MESSAGE_CAP FAILURES DECODED SOUNDS; do
  if [ -z "${!name}" ]; then echo "FAIL: missing counter for $name (stale probe binary?)"; exit 1; fi
done

echo "[sound-accounting] demo=$(basename "$DEMO") sounds=$SOUNDS messages=$MESSAGES dropped=$DROPPED payload_too_large=$TOO_LARGE message_cap=$MESSAGE_CAP failures=$FAILURES decoded=$DECODED"

fail=0
check() {
  if [ "$2" -eq "$3" ]; then echo "[sound-accounting] PASS $1 ($2 == $3)"; else echo "[sound-accounting] FAIL $1 ($2 != $3)"; fail=1; fi
}
le() {
  if [ "$2" -le "$3" ]; then echo "[sound-accounting] PASS $1 ($2 <= $3)"; else echo "[sound-accounting] FAIL $1 ($2 > $3)"; fail=1; fi
}

# I1 total == sum of reasons: recordDroppedSoundMessage() maintains them together, so they cannot drift.
check "I1 dropped == payload_too_large + message_cap" "$DROPPED" "$((TOO_LARGE + MESSAGE_CAP))"
# I2 a message can only be dropped once the 4096-entry budget is full, so captured == 4096 exactly.
if [ "$DROPPED" -gt 0 ]; then check "I2 messages - dropped == 4096" "$((MESSAGES - DROPPED))" "4096"; fi
# I3 real failures can only come from the captured messages: at most one per captured message.
le "I3 failures <= 4096" "$FAILURES" "4096"
# I4 the pre-fix conflation makes failures >= dropped (one manufactured failure per drop). With drops
#    present, failures < dropped is what a split reading looks like; equality is the old bug back.
if [ "$DROPPED" -gt 0 ]; then
  if [ "$FAILURES" -lt "$DROPPED" ]; then
    echo "[sound-accounting] PASS I4 failures < dropped ($FAILURES < $DROPPED)"
  else
    echo "[sound-accounting] FAIL I4 failures < dropped ($FAILURES >= $DROPPED): designed drops are being counted as decode failures again"
    fail=1
  fi
fi

if [ "$fail" -ne 0 ]; then echo "[sound-accounting] VERDICT: FAIL"; exit 1; fi
echo "[sound-accounting] VERDICT: PASS (accounting only; the 4096-message cap still drops messages by design)"
exit 0
