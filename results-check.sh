#!/usr/bin/env bash
# I3b gate (2026-11-24): the analysis screen's result list and the click that lands the
# workspace three seconds before an anomaly (docs/UI-SHELL-DESIGN-2026-11-22.md 9.1).
#
# What it pins:
#   1. the row projection, on a fabricated report the probe builds by hand: one list
#      sorted by start tick, index = the visible position, seek = start - 200 clamped
#      at 0, and teleport/sharp turn switch player while a stall does not;
#   2. the trace line a click needs, field by field -- and that a click past the list
#      and a click on an empty list are REFUSED, not clamped to the first row;
#   3. the product lists the rows its own scan measured (count > 0) and prints no
#      click line at all when no click was asked for;
#   4. --results-select 0 makes the product seek to that row's seek tick, start
#      playing, and DRAW that tick: the scene clock line and the draw line both carry
#      the seek tick, so this is the frame and not merely an accepted request;
#   5. an index nobody can click comes back refused, with the row count, and never
#      falls back to row 0.
set -uo pipefail

cd "$(dirname "$0")" || exit 1
export LC_ALL=C
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/native_ui_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT=".scratch/i3b"

fail=0
ok=0
mkdir -p "$OUT"
win() { cygpath -m "$1"; }

assert_eq() { # <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"; ok=$((ok + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=$((fail + 1))
  fi
}
assert_gt() { # <label> <actual> <floor>
  if [ "${2:-x}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"; ok=$((ok + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"; fail=$((fail + 1))
  fi
}
nfield() { # <line> <field> -- a number, matched on a word boundary
  printf '%s\n' "$1" | grep -oE "(^| )$2=-?[0-9]+" | tail -1 | sed 's/^ //' | cut -d= -f2
}
sfield() { # <line> <field> -- a token, matched on a word boundary
  printf '%s\n' "$1" | grep -oE "(^| )$2=[^ ]*" | tail -1 | sed 's/^ //' | cut -d= -f2
}
count_of() { # <file> <literal> -- a line count, never grep's exit status
  grep -a -c -F "$2" "$1" 2>/dev/null || true
}

for path in "$TF" "$DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

echo "=== 0/5 build (product + native_ui_probe) ==="
if bash build-target.sh tf2_demo_native native_ui_probe > "$OUT/build.log" 2>&1; then
  echo "  OK   build tf2_demo_native native_ui_probe"
else
  echo "  FAIL build"
  tail -25 "$OUT/build.log" | sed 's/^/       /'
  exit 1
fi

echo "=== 1/5 probe: the row projection and the click rule ==="
"$PROBE" > "$OUT/probe.txt" 2>&1
assert_eq "probe rc" "$?" "0"
assert_eq "probe failures" "$(count_of "$OUT/probe.txt" 'FAIL ')" "0"
pline="$(grep -a 'resultsRows=' "$OUT/probe.txt" | head -1)"
assert_eq "probe rows" "$(nfield "$pline" resultsRows)" "3"
assert_eq "probe seek" "$(nfield "$pline" resultsSeek)" "11800"
assert_eq "probe clamp" "$(nfield "$pline" resultsClamp)" "0"
assert_eq "probe switch count" "$(nfield "$pline" resultsSwitch)" "2"
assert_eq "probe format" "$(nfield "$pline" resultsFormat)" "1"
assert_eq "probe result line" \
  "$(count_of "$OUT/probe.txt" 'results: rows=3 seek=11800 clamp=0 switch=2 refused=0 empty=0')" "1"

# Each product run: analysis scan on (the list needs it), one captured frame so the
# run exits by itself, and a private trace/settings pair so runs cannot share state.
run_product() { # <label> <settings> <extra args...>
  local label="$1" settings="$2"
  shift 2
  TF2_NATIVE_TRACE="$(win "$OUT/$label.trace")" timeout 600 "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --analysis-scan \
    --settings-file "$(win "$settings.ini")" --ui-settings-file "$(win "$settings-ui.ini")" \
    --capture-tick 100 --capture-frame "$(win "$OUT/$label.bmp")" "$@" \
    > "$OUT/$label.log" 2>&1
  local rc=$?
  tr -d '\r' < "$OUT/$label.trace" > "$OUT/$label.trace.lf"
  return $rc
}

echo "=== 2/5 the product lists the rows its scan measured, and only a click is a click ==="
run_product control "$OUT/control"
assert_eq "control rc" "$?" "0"
control_rows_line="$(grep -a -m1 'analysis: results rows=' "$OUT/control.trace.lf")"
assert_gt "control rows" "$(nfield "$control_rows_line" rows)" "0"
assert_gt "control rows printed" "$(nfield "$control_rows_line" printed)" "0"
assert_eq "control click lines" "$(count_of "$OUT/control.trace.lf" 'analysis: results click ')" "0"
row0="$(grep -a -m1 'result: row index=0 ' "$OUT/control.trace.lf")"
row0_start="$(nfield "$row0" start)"
assert_gt "row 0 start tick" "$row0_start" "0"
row0_seek="$(( row0_start > 200 ? row0_start - 200 : 0 ))"
assert_eq "row 0 seek (start - 200, clamped)" "$(nfield "$row0" seek)" "$row0_seek"

echo "=== 3/5 --results-select 0 seeks there, plays, and draws that tick ==="
run_product click "$OUT/click" --results-select 0
assert_eq "click rc" "$?" "0"
assert_eq "click lines" "$(count_of "$OUT/click.trace.lf" 'analysis: results click ')" "1"
click_line="$(grep -a -m1 'analysis: results click ' "$OUT/click.trace.lf")"
assert_eq "click index" "$(nfield "$click_line" index)" "0"
assert_eq "click start" "$(nfield "$click_line" start)" "$row0_start"
assert_eq "click seek" "$(nfield "$click_line" seek)" "$row0_seek"
click_kind="$(sfield "$click_line" kind)"
case "$click_kind" in
  teleport|sharp-turn) click_switch_expected=1 ;;
  stall) click_switch_expected=0 ;;
  *) click_switch_expected=unknown ;;
esac
assert_eq "click switch follows the kind" "$(nfield "$click_line" switch)" "$click_switch_expected"
assert_eq "click seek accepted" "$(nfield "$click_line" seeked)" "1"
assert_eq "click playing accepted" "$(nfield "$click_line" playing)" "1"
assert_eq "click reports the seek tick" "$(nfield "$click_line" tick)" "$row0_seek"
assert_gt "the scene clock runs at the seek tick" \
  "$(count_of "$OUT/click.trace.lf" "scene: clock tick=$row0_seek ")" "0"
assert_gt "the click's tick is the tick that was drawn" \
  "$(count_of "$OUT/click.trace.lf" "draw: begin tick=$row0_seek ")" "0"
assert_gt "the captured frame is the clicked frame" "$(count_of "$OUT/click.log" "traceTick=$row0_seek")" "0"

echo "=== 4/5 a click past the list is refused, not clamped to row 0 ==="
run_product refused "$OUT/refused" --results-select 99999
assert_eq "refused rc" "$?" "0"
assert_eq "refused lines" "$(count_of "$OUT/refused.trace.lf" 'analysis: results click index=99999 refused rows=')" "1"
assert_eq "refused ran no seek" "$(count_of "$OUT/refused.trace.lf" 'analysis: results click index=99999 start=')" "0"
assert_eq "refused fell back to no row" "$(count_of "$OUT/refused.trace.lf" 'analysis: results click index=0 ')" "0"

echo "=== 5/5 verdict ==="
if [ "$fail" -eq 0 ]; then
  echo "RESULTS=PASS $ok OK / 0 FAIL"
  exit 0
fi
echo "RESULTS=FAIL $ok OK / $fail FAIL"
exit 1
