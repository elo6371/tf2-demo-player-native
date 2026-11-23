#!/usr/bin/env bash
# I3c gate (2026-11-24): the result list is a real control -- it is filled from the
# scan, it places itself without moving anything else, and a click in it is a seek.
# (docs/UI-SHELL-DESIGN-2026-11-22.md 9.1)
#
# What it pins, and why each one needs the product rather than the model:
#   1. the planner: 24 sizes x scales, the list inside the client and off every rect
#      above it, plus the exact row text -- a listbox that draws a row wrapped or
#      covered is not a list the user can click accurately;
#   2. the control is FILLED: LB_GETCOUNT agrees with the model's rows. The probe only
#      proves the model; this proves the string reached the Win32 control;
#   3. the mouse path: --ui-list-click selects a row in the control and posts the
#      notification a double-click sends, so LB_GETCURSEL, the parent's LBN_DBLCLK and
#      the queued seek are all exercised. The frame drawn must be the seek tick;
#   4. the mouse and keyboard paths land on the SAME tick for the same index -- two
#      entry points that disagree would be a bug the model tests cannot see;
#   5. an index past the end is ignored (no selection to notify, no seek), not clamped.
set -uo pipefail

cd "$(dirname "$0")" || exit 1
export LC_ALL=C
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/native_ui_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT=".scratch/i3c"

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
nfield() { # <line> <field>
  printf '%s\n' "$1" | grep -oE "(^| )$2=-?[0-9]+" | tail -1 | sed 's/^ //' | cut -d= -f2
}
count_of() { # <file> <literal>
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

echo "=== 1/5 probe: the list planner and the row text ==="
"$PROBE" > "$OUT/probe.txt" 2>&1
assert_eq "probe rc" "$?" "0"
assert_eq "probe failures" "$(count_of "$OUT/probe.txt" 'FAIL ')" "0"
pline="$(grep -a 'resultsListCases=' "$OUT/probe.txt" | head -1)"
assert_eq "planner cases (6 sizes x 4 scales)" "$(nfield "$pline" resultsListCases)" "24"
assert_eq "planner failures" "$(nfield "$pline" resultsListBad)" "0"
assert_eq "row label" "$(nfield "$pline" resultsListLabel)" "1"
assert_eq "the HUD plan's control count is untouched" \
  "$(nfield "$(grep -a 'layoutControls=' "$OUT/probe.txt" | head -1)" layoutControls)" "19"

# One product run: analysis scan on, one captured frame so it exits by itself, a
# private trace/settings trio so runs cannot share state.
run_product() { # <label> <extra args...>
  local label="$1"
  shift
  TF2_NATIVE_TRACE="$(win "$OUT/$label.trace")" timeout 600 "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --analysis-scan \
    --settings-file "$(win "$OUT/$label.ini")" \
    --ui-settings-file "$(win "$OUT/$label-ui.ini")" \
    --capture-tick 100 --capture-frame "$(win "$OUT/$label.bmp")" "$@" \
    > "$OUT/$label.log" 2>&1
  local rc=$?
  tr -d '\r' < "$OUT/$label.trace" > "$OUT/$label.trace.lf"
  return $rc
}

echo "=== 2/5 the control is filled from the scan ==="
run_product control
assert_eq "control rc" "$?" "0"
# The LAST fill line, not the first: the control is created before the demo is
# scanned, so the first fill is legitimately empty and the scan's rows arrive later.
# Reading the first line would pin the empty pass and pass for the wrong reason.
fill_line="$(grep -a 'analysis: results list items=' "$OUT/control.trace.lf" | tail -1)"
fill_items="$(nfield "$fill_line" items)"
fill_count="$(nfield "$fill_line" count)"
assert_gt "rows handed to the control" "$fill_items" "0"
assert_eq "LB_GETCOUNT agrees with the model" "$fill_count" "$fill_items"
rows_total="$(nfield "$(grep -a -m1 'analysis: results rows=' "$OUT/control.trace.lf")" rows)"
assert_eq "the control holds every row the scan produced" "$fill_items" "$rows_total"
assert_eq "no click line without a click" \
  "$(count_of "$OUT/control.trace.lf" 'analysis: results list click ')" "0"
assert_eq "no seek line without a click" \
  "$(count_of "$OUT/control.trace.lf" 'analysis: results click ')" "0"
row0="$(grep -a -m1 'result: row index=0 ' "$OUT/control.trace.lf")"
row0_start="$(nfield "$row0" start)"
row0_seek="$(nfield "$row0" seek)"
assert_gt "row 0 start tick" "$row0_start" "0"

echo "=== 3/5 --ui-list-click 0 exercises the control and lands the seek ==="
run_product mouse --ui-list-click 0
assert_eq "mouse rc" "$?" "0"
assert_eq "the list reported the double-click" \
  "$(count_of "$OUT/mouse.trace.lf" 'analysis: results list click index=0 source=mouse')" "1"
assert_eq "mouse seek lines" "$(count_of "$OUT/mouse.trace.lf" 'analysis: results click ')" "1"
mouse_line="$(grep -a -m1 'analysis: results click ' "$OUT/mouse.trace.lf")"
assert_eq "mouse click index" "$(nfield "$mouse_line" index)" "0"
assert_eq "mouse click seek" "$(nfield "$mouse_line" seek)" "$row0_seek"
assert_eq "mouse click seeked" "$(nfield "$mouse_line" seeked)" "1"
assert_eq "mouse click playing" "$(nfield "$mouse_line" playing)" "1"
assert_eq "mouse click reports the seek tick" "$(nfield "$mouse_line" tick)" "$row0_seek"
assert_eq "the mouse path filled the list too" \
  "$(nfield "$(grep -a 'analysis: results list items=' "$OUT/mouse.trace.lf" | tail -1)" count)" "$fill_items"
assert_gt "the mouse seek tick was drawn" \
  "$(count_of "$OUT/mouse.trace.lf" "draw: begin tick=$row0_seek ")" "0"

echo "=== 4/5 mouse and keyboard land on the same row ==="
run_product key --results-select 0
assert_eq "key rc" "$?" "0"
key_line="$(grep -a -m1 'analysis: results click ' "$OUT/key.trace.lf")"
assert_eq "key click seek equals mouse seek" "$(nfield "$key_line" seek)" "$(nfield "$mouse_line" seek)"
assert_eq "key click start equals mouse start" "$(nfield "$key_line" start)" "$(nfield "$mouse_line" start)"
assert_eq "the keyboard path is not the mouse path" \
  "$(count_of "$OUT/key.trace.lf" 'analysis: results list click ')" "0"

echo "=== 5/5 an index past the end is ignored, not clamped ==="
run_product past --ui-list-click 99999
assert_eq "past-the-end rc" "$?" "0"
assert_eq "no double-click for a row that does not exist" \
  "$(count_of "$OUT/past.trace.lf" 'analysis: results list click ')" "0"
assert_eq "no seek for a row that does not exist" \
  "$(count_of "$OUT/past.trace.lf" 'analysis: results click ')" "0"
assert_eq "it did not fall back to row 0" \
  "$(count_of "$OUT/past.trace.lf" "draw: begin tick=$row0_seek ")" "0"

echo "=== verdict ==="
if [ "$fail" -eq 0 ]; then
  echo "RESULTS-LIST=PASS $ok OK / 0 FAIL"
  exit 0
fi
echo "RESULTS-LIST=FAIL $ok OK / $fail FAIL"
exit 1
