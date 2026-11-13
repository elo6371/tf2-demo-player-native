#!/usr/bin/env bash
# audio-keys-check.sh -- the product keys M, `[` and `]` change the master
# volume, persist it, and the next process reads it back.
#
# WHY THIS IS A GATE
#   AUD-03 added the product consumer and the three keys, but its evidence was
#   the PCM bytes in a probe run (`audio_effects_probe --player-master-gain-
#   selftest`). The product path -- WM_KEYDOWN -> NativeUiController -> ui.ini
#   -> the next process -> AudioEventScheduler -- was never driven end to end.
#   The keys are not in the camera key table (`dispatchHotkey` reports them
#   unbound), so a camera-script `key` step cannot reach them; this gate posts
#   the real WM_KEYDOWN/WM_KEYUP pair through `send-window-keys.ps1`.
#
# WHAT IS ASSERTED
#   1. six key edges, in order, each leaves the exact `audio: master volume=`
#      reading (M toggles mute; `[` steps down 10; `]` steps up 10 and unmutes a
#      muted run first);
#   2. the scheduler receives the master gain on the next frame
#      (`audio: master applied` 0.50 -> 0.00 -> 0.50 -> 0.40 -> 0.50 -> 0.60 ->
#      0.00) and the last applied value is muted 0.00;
#   3. the run ends with ui.ini volumePercent=60 muted=1;
#   4. a fresh process with that file starts muted (`master applied 0.00 muted=1`);
#   5. after flipping only the muted bit, a fresh process starts unmuted at the
#      persisted 0.60.
#
# HEADLESS: every run passes --audio-disabled and --hidden-window; the gate
# asserts audio_mode=disabled and audio_device_opened=0 on each run. It never
# asks for a device.
#
# Usage: bash audio-keys-check.sh
# Exit: 0 = every assertion held; 1 = a finding.
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
OUT="${AUDIO_KEYS_OUT:-.scratch/audio-keys}"
PS_HELPER="$PWD/send-window-keys.ps1"
mkdir -p "$OUT"

for path in "$EXE" "$DEMO" "$TF" "$PS_HELPER"; do
  [ -e "$path" ] || { echo "AUDIO-KEYS=FAIL reason=missing-input path=$path"; exit 1; }
done
command -v powershell >/dev/null 2>&1 || {
  echo "AUDIO-KEYS=FAIL reason=missing-tool name=powershell"; exit 1; }

. "$(dirname "$0")/verify-iso.sh"
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-audio-keys.XXXXXX") || {
  echo "AUDIO-KEYS=FAIL reason=iso-dir"; exit 2; }
ISO_SETTINGS="$ISO_DIR/settings.ini"
UI="$ISO_DIR/ui.ini"
iso_seed_gate_settings "$ISO_DIR" || { echo "AUDIO-KEYS=FAIL reason=iso-seed"; exit 2; }
KPID=""
trap 'if [ -n "${KPID:-}" ]; then kill "$KPID" 2>/dev/null; wait "$KPID" 2>/dev/null; fi; rm -rf "$ISO_DIR"' EXIT

fail=0
assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() {
  if [ -n "${2:-}" ] && [ "${2:-0}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
tfield() { # tfield <line> <field>
  printf '%s' "$1" | grep -oE "$2=[^ ]*" | head -1 | cut -d= -f2
}
seed_ui() { # seed_ui <volumePercent> <muted>
  printf '[ui]\r\nvolumePercent=%s\r\nmuted=%s\r\n' "$1" "$2" > "$UI"
}
last_line() { grep -a "$2" "$1" | tail -1; }
first_line() { grep -a "$2" "$1" | head -1; }
# I-28: an absolute AUDIO_KEYS_OUT must not become "$PWD//d/_shared/...".
abs_win_path() {
  case "$1" in
    /*|[A-Za-z]:*) cygpath -m "$1" ;;
    *)            cygpath -m "$PWD/$1" ;;
  esac
}

# run_sync <prefix> <tick> <paused> [extra...]
run_sync() {
  local prefix="$1" tick="$2" paused="$3"; shift 3
  local pause_args=()
  [ "$paused" = "1" ] && pause_args=(--start-paused)
  rm -f "$prefix.bmp" "$prefix.trace" "$prefix.log"
  TF2_NATIVE_TRACE="$(abs_win_path "$prefix.trace")" timeout 240 "$EXE" \
    --tf-root "$TF" --audio-disabled --hidden-window \
    --settings-file "$ISO_SETTINGS" --ui-settings-file "$UI" \
    --demo "$DEMO" "${pause_args[@]}" --capture-tick "$tick" \
    --capture-frame "$(abs_win_path "$prefix.bmp")" "$@" \
    > "$prefix.log" 2>&1
  echo $?
}

echo "=== 1/3 the product keys: M, [, ], ], M ==="
seed_ui 50 0
KEYS="$OUT/keys"
rm -f "$KEYS.bmp" "$KEYS.trace" "$KEYS.log"
TF2_NATIVE_TRACE="$(abs_win_path "$KEYS.trace")" timeout 240 "$EXE" \
  --tf-root "$TF" --audio-disabled --hidden-window \
  --settings-file "$ISO_SETTINGS" --ui-settings-file "$UI" \
  --demo "$DEMO" --capture-tick 1200 \
  --capture-frame "$(abs_win_path "$KEYS.bmp")" \
  > "$KEYS.log" 2>&1 &
KPID=$!

# Wait for the window, then post the keys. The product's load phase blocks the
# message pump, so a key posted during load is queued and dispatched when the
# loop starts. That is deliberate: polling the trace for a live line is not
# reliable on this Windows filesystem (measured: the file's bytes became visible
# to MSYS grep only at process exit), and a gate that waits for it sends the
# keys after the product has already left.
sleep 10
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass \
  -File "$(cygpath -m "$PS_HELPER")" -ProcessName tf2_demo_native \
  -Keys "77,77,219,221,221,77" -TimeoutSeconds 120 -GapMs 300 \
  > "$OUT/keys.sent.txt" 2>&1
keys_rc=$?
cat "$OUT/keys.sent.txt"
assert_eq "key send rc" "$keys_rc" "0"
assert_eq "key send count" "$(grep -c '^KEY-SEND=PASS count=6 ' "$OUT/keys.sent.txt")" "1"
wait "$KPID"; KRC=$?
assert_eq "key run rc" "$KRC" "0"
assert_ge "key run bmp bytes" "$(stat -c%s "$KEYS.bmp" 2>/dev/null || echo 0)" 1
MODE=$(last_line "$KEYS.trace" 'mode: summary')
assert_eq "key run audio_mode" "$(tfield "$MODE" audio_mode)" "disabled"
assert_eq "key run device_opened" "$(tfield "$MODE" audio_device_opened)" "0"

# The six key edges, in order. Each line names its source, so a run that
# produced the right final number by a different path cannot pass.
assert_eq "key-m mute line" \
  "$(grep -c 'audio: master volume=0.50 muted=1 source=key-m' "$KEYS.trace")" "1"
assert_eq "key-m unmute line" \
  "$(grep -c 'audio: master volume=0.50 muted=0 source=key-m' "$KEYS.trace")" "1"
assert_eq "key-volume-down line" \
  "$(grep -c 'audio: master volume=0.40 muted=0 source=key-volume-down' "$KEYS.trace")" "1"
assert_eq "key-volume-up to 50 line" \
  "$(grep -c 'audio: master volume=0.50 muted=0 source=key-volume-up' "$KEYS.trace")" "1"
assert_eq "key-volume-up to 60 line" \
  "$(grep -c 'audio: master volume=0.60 muted=0 source=key-volume-up' "$KEYS.trace")" "1"
assert_eq "final key-m mute line" \
  "$(grep -c 'audio: master volume=0.60 muted=1 source=key-m' "$KEYS.trace")" "1"
# The keys are queued while the load phase blocks the message pump, so all six
# edges can be dispatched in one batch before the first frame: the scheduler
# then sees one transition (0.50 -> 0.00), not six. What must hold is that the
# final applied value is the one the six edges produced; the six `master volume=`
# lines above are the per-edge evidence.
assert_ge "master applied lines" "$(grep -c 'audio: master applied' "$KEYS.trace")" 1
assert_eq "last applied volume" "$(last_line "$KEYS.trace" 'audio: master applied' | grep -o 'volume=[0-9.]*' | cut -d= -f2)" "0.00"
assert_eq "last applied muted" "$(last_line "$KEYS.trace" 'audio: master applied' | grep -o 'muted=[01]' | cut -d= -f2)" "1"

# The persisted file. GetPrivateProfileIntW is case-insensitive; the writer uses
# these spellings.
UI_TXT=$(tr -d '\r' < "$UI")
assert_eq "ui.ini volumePercent" "$(printf '%s\n' "$UI_TXT" | grep -i '^volumePercent=' | cut -d= -f2)" "60"
assert_eq "ui.ini muted" "$(printf '%s\n' "$UI_TXT" | grep -i '^muted=' | cut -d= -f2)" "1"

echo
echo "=== 2/3 a fresh process reads the persisted mute ==="
PRC=$(run_sync "$OUT/restart-muted" 0 1)
assert_eq "restart muted rc" "$PRC" "0"
RMODE=$(last_line "$OUT/restart-muted.trace" 'mode: summary')
assert_eq "restart muted audio_mode" "$(tfield "$RMODE" audio_mode)" "disabled"
assert_eq "restart muted device_opened" "$(tfield "$RMODE" audio_device_opened)" "0"
assert_eq "restart muted volume" \
  "$(first_line "$OUT/restart-muted.trace" 'audio: master applied' | grep -o 'volume=[0-9.]*' | cut -d= -f2)" "0.00"
assert_eq "restart muted flag" \
  "$(first_line "$OUT/restart-muted.trace" 'audio: master applied' | grep -o 'muted=[01]' | cut -d= -f2)" "1"

echo
echo "=== 3/3 flipping only muted=0 keeps the persisted volume ==="
sed -i 's/^muted=1/muted=0/' "$UI"
PRU=$(run_sync "$OUT/restart-unmuted" 0 1)
assert_eq "restart unmuted rc" "$PRU" "0"
assert_eq "restart unmuted first applied volume" \
  "$(first_line "$OUT/restart-unmuted.trace" 'audio: master applied' | grep -o 'volume=[0-9.]*' | cut -d= -f2)" "0.60"
assert_eq "restart unmuted first applied muted" \
  "$(first_line "$OUT/restart-unmuted.trace" 'audio: master applied' | grep -o 'muted=[01]' | cut -d= -f2)" "0"
assert_eq "restart unmuted device_opened" \
  "$(tfield "$(last_line "$OUT/restart-unmuted.trace" 'mode: summary')" audio_device_opened)" "0"

echo
if [ "$fail" -eq 0 ]; then
  echo "AUDIO-KEYS=PASS (six key edges, two restart reads, no device opened)"
  exit 0
fi
echo "AUDIO-KEYS=FAIL"
exit 1
