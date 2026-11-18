#!/usr/bin/env bash
# audio-keys-check.sh -- the product keys M, `[` and `]` drive the master
# volume and the map overview, persist the volume, and the next process reads
# it back.
#
# WHY THIS IS A GATE
#   AUD-03 added the product consumer and the three volume keys, but its
#   evidence was the PCM bytes in a probe run (`audio_effects_probe
#   --player-master-gain-selftest`). The product path -- WM_KEYDOWN ->
#   NativeUiController -> ui.ini -> the next process -> AudioEventScheduler --
#   was never driven end to end. The keys are not in the camera key table
#   (`dispatchHotkey` reports them unbound), so a camera-script `key` step
#   cannot reach them; this gate posts the real WM_KEYDOWN/WM_KEYUP pair
#   through `send-window-keys.ps1` (and synthesises the Ctrl state in the
#   shared input queue, because PostMessage does not update GetKeyState).
#
# WHAT IS ASSERTED
#   1. bare M toggles the map overview and NOTHING ELSE: the trace carries
#      `overview: toggle open=1 source=key` and no `audio: master` line, and
#      ui.ini keeps volumePercent=50 muted=0;
#   2. Ctrl+M toggles mute (the 2026-11-17 user decision moved mute off bare
#      M because hud.overview claims that key): the trace carries
#      `audio: master volume=0.50 muted=1 source=key-ctrl-m` and no overview
#      toggle;
#   3. `]` on a muted run unmutes first and steps up 10 (0.50 -> 0.60);
#   4. Ctrl+M again mutes the new volume (0.60 -> muted);
#   5. the scheduler receives the master gain (`audio: master applied`) and
#      the last applied value is muted 0.00;
#   6. the run ends with ui.ini volumePercent=60 muted=1;
#   7. a fresh process with that file starts muted (`master applied 0.00 muted=1`);
#   8. after flipping only the muted bit, a fresh process starts unmuted at the
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
  printf '%s' "$1" | tr -d '\r' | grep -oE "$2=[^ ]*" | head -1 | cut -d= -f2
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

# key_run <name> <keys> <modifiers|-> <hold-until-exit 0|1> -> echoes the rc
# The keys are posted while the load phase still blocks the message pump, so
# they are queued and dispatched together. A modifier is therefore held until
# the process exits (-HoldUntilExit): releasing it at post time would make the
# chord arrive as a bare key.
key_run() {
  local name="$1" keys="$2" mods="$3" hold="$4"
  local prefix="$OUT/$name"
  rm -f "$prefix.bmp" "$prefix.trace" "$prefix.log" "$prefix.sent.txt" "$prefix.sent.rc"
  TF2_NATIVE_TRACE="$(abs_win_path "$prefix.trace")" timeout 240 "$EXE" \
    --tf-root "$TF" --audio-disabled --hidden-window \
    --settings-file "$ISO_SETTINGS" --ui-settings-file "$UI" \
    --demo "$DEMO" --capture-tick 1200 \
    --capture-frame "$(abs_win_path "$prefix.bmp")" \
    > "$prefix.log" 2>&1 &
  KPID=$!
  sleep 10
  # The helper must post to the process THIS gate launched. The run-unique
  # --capture-frame path is in that process command line, so it cannot attach to
  # a foreign product when our run died early (that happened 2026-11-17: a run
  # crashed at 0.7 s and the helper posted to somebody else's process).
  local args=(-ProcessName tf2_demo_native -CommandLineMatch "$(abs_win_path "$prefix.bmp")" \
    -Keys "$keys" -TimeoutSeconds 120 -GapMs 300)
  [ -n "$mods" ] && [ "$mods" != "-" ] && args+=(-Modifiers "$mods")
  [ "$hold" = "1" ] && args+=(-HoldUntilExit)
  powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass \
    -File "$(cygpath -m "$PS_HELPER")" "${args[@]}" > "$prefix.sent.txt" 2>&1 &
  local hpid=$!
  wait "$KPID"; local rc=$?
  wait "$hpid"; local hrc=$?
  # helper output goes to stderr so callers capture only the product rc; the
  # helper rc is published in a file because a command substitution cannot also
  # carry this gate's own OK/FAIL lines.
  cat "$prefix.sent.txt" >&2
  if [ "$rc" -ne 0 ]; then
    echo "  product exited rc=$rc; last trace line: $(tr -d '\r' < "$prefix.trace" 2>/dev/null | tail -1)" >&2
  fi
  printf '%s' "$hrc" > "$prefix.sent.rc"
  echo "$rc"
}

echo "=== 1/4 bare M is the map overview, not the mute ==="
seed_ui 50 0
R=$(key_run key-bare-m 77 - 0)
assert_eq "bare M key send rc" "$(cat "$OUT/key-bare-m.sent.rc" 2>/dev/null)" 0
assert_eq "bare M run rc" "$R" 0
assert_ge "bare M wrote a bitmap" "$(stat -c%s "$OUT/key-bare-m.bmp" 2>/dev/null || echo 0)" 1
assert_eq "bare M overview toggled open" \
  "$(grep -c 'overview: toggle open=1 source=key' "$OUT/key-bare-m.trace")" "1"
assert_eq "bare M produced NO mute line" \
  "$(grep -c 'audio: master volume=' "$OUT/key-bare-m.trace")" "0"
UI_TXT=$(tr -d '\r' < "$UI")
assert_eq "bare M ui.ini volumePercent" "$(printf '%s\n' "$UI_TXT" | grep -i '^volumePercent=' | cut -d= -f2)" "50"
assert_eq "bare M ui.ini muted" "$(printf '%s\n' "$UI_TXT" | grep -i '^muted=' | cut -d= -f2)" "0"

echo
echo "=== 2/4 Ctrl+M mutes at 0.50 ==="
seed_ui 50 0
R=$(key_run key-ctrl-m 77 17 1)
assert_eq "Ctrl+M key send rc" "$(cat "$OUT/key-ctrl-m.sent.rc" 2>/dev/null)" 0
assert_eq "Ctrl+M run rc" "$R" 0
assert_eq "Ctrl+M mute line" \
  "$(grep -c 'audio: master volume=0.50 muted=1 source=key-ctrl-m' "$OUT/key-ctrl-m.trace")" "1"
assert_eq "Ctrl+M did NOT toggle the minimap" \
  "$(grep -c 'overview: toggle' "$OUT/key-ctrl-m.trace")" "0"
UI_TXT=$(tr -d '\r' < "$UI")
assert_eq "Ctrl+M ui.ini volumePercent" "$(printf '%s\n' "$UI_TXT" | grep -i '^volumePercent=' | cut -d= -f2)" "50"
assert_eq "Ctrl+M ui.ini muted" "$(printf '%s\n' "$UI_TXT" | grep -i '^muted=' | cut -d= -f2)" "1"

echo
echo "=== 3/4 ] on the muted run unmutes and steps to 0.60 ==="
R=$(key_run key-vol-up 221 - 0)
assert_eq "vol-up key send rc" "$(cat "$OUT/key-vol-up.sent.rc" 2>/dev/null)" 0
assert_eq "vol-up run rc" "$R" 0
assert_eq "vol-up unmute+step line" \
  "$(grep -c 'audio: master volume=0.60 muted=0 source=key-volume-up' "$OUT/key-vol-up.trace")" "1"
UI_TXT=$(tr -d '\r' < "$UI")
assert_eq "vol-up ui.ini volumePercent" "$(printf '%s\n' "$UI_TXT" | grep -i '^volumePercent=' | cut -d= -f2)" "60"
assert_eq "vol-up ui.ini muted" "$(printf '%s\n' "$UI_TXT" | grep -i '^muted=' | cut -d= -f2)" "0"

echo
echo "=== 4/4 Ctrl+M again mutes the persisted 0.60 ==="
R=$(key_run key-ctrl-m-final 77 17 1)
assert_eq "final Ctrl+M key send rc" "$(cat "$OUT/key-ctrl-m-final.sent.rc" 2>/dev/null)" 0
assert_eq "final Ctrl+M run rc" "$R" 0
assert_eq "final Ctrl+M mute line" \
  "$(grep -c 'audio: master volume=0.60 muted=1 source=key-ctrl-m' "$OUT/key-ctrl-m-final.trace")" "1"
assert_ge "master applied lines" "$(grep -c 'audio: master applied' "$OUT/key-ctrl-m-final.trace")" 1
assert_eq "last applied volume" "$(last_line "$OUT/key-ctrl-m-final.trace" 'audio: master applied' | grep -o 'volume=[0-9.]*' | cut -d= -f2)" "0.00"
assert_eq "last applied muted" "$(last_line "$OUT/key-ctrl-m-final.trace" 'audio: master applied' | grep -o 'muted=[01]' | cut -d= -f2)" "1"
MODE=$(last_line "$OUT/key-ctrl-m-final.trace" 'mode: summary')
assert_eq "final key run audio_mode" "$(tfield "$MODE" audio_mode)" "disabled"
assert_eq "final key run device_opened" "$(tfield "$MODE" audio_device_opened)" "0"

# The persisted file. GetPrivateProfileIntW is case-insensitive; the writer uses
# these spellings.
UI_TXT=$(tr -d '\r' < "$UI")
assert_eq "ui.ini volumePercent" "$(printf '%s\n' "$UI_TXT" | grep -i '^volumePercent=' | cut -d= -f2)" "60"
assert_eq "ui.ini muted" "$(printf '%s\n' "$UI_TXT" | grep -i '^muted=' | cut -d= -f2)" "1"

echo
echo "=== a fresh process reads the persisted mute ==="
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
echo "=== flipping only muted=0 keeps the persisted volume ==="
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
  echo "AUDIO-KEYS=PASS (bare M, Ctrl+M x2, one volume step, two restart reads, no device opened)"
  exit 0
fi
echo "AUDIO-KEYS=FAIL"
exit 1