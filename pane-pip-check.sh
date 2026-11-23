#!/usr/bin/env bash
# pane-pip-check.sh -- N16 (2026-11-23): the picture-in-picture gate.
#
# WHY THIS EXISTS
#   U4 built two panes that divide the client into disjoint halves, and the pane
#   contract in native/include/pane_boundary.h refuses anything else: R8 required
#   the rectangles to tile the client exactly once. Picture in picture is the
#   opposite arrangement -- pane 0 keeps the whole client and the second pane is
#   drawn over it -- so R8 had to learn a second layout instead of being relaxed.
#   This gate makes that difference a reading:
#
#     1. the predicate's own controls (native_ui_probe): the split causes and the
#        inset causes are each separately reachable, and the SAME rectangles are
#        clean as an inset and broken as a split. A rule that merely stopped
#        checking overlap would fail the last one.
#     2. one product run, split then inset in the same process: on the captured
#        frame of each layout the trace must print the rectangles the frame
#        actually bound, and every pane-contract mismatch must be zero.
#     3. the script step's refusals: an unknown layout name must fail the step,
#        so "the layout was set" cannot be a step that silently did nothing.
#
# Usage: bash pane-pip-check.sh
# Exit:  0 = every assertion held; 1 = not.
set -uo pipefail
# W1: the pixel/gate size. An explicit --window-size still wins in the product.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")"
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/native_ui_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# rc11, like the camera gate: its referenced map is installed, so the frames this
# gate captures are real geometry and not the empty-world fallback.
DEMO="D:/TF2_Demo_Player/testdata/demos/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem"
OUT=.scratch/pane-pip
RUN_TIMEOUT=300

fail=0
mkdir -p "$OUT"

win() { cygpath -m "$1"; }   # never prefix $PWD: an absolute /d/... path stays usable

assert_eq() { # <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

assert_num_le() { # <label> <actual> <bound> -- actual <= bound
  if [ -n "${2:-}" ] && [ "${2:-}" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}

assert_num_lt() { # <label> <actual> <bound> -- actual < bound
  if [ -n "${2:-}" ] && [ "${2:-}" -lt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (< $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected < $3"; fail=1
  fi
}

pvalue() { # <file> <field> -- first key=value on one line
  grep -oE "$2=[^[:space:]]*" "$1" 2>/dev/null | head -1 | cut -d= -f2
}

echo "=== 0/3 build (product + native_ui_probe) ==="
if bash build-target.sh tf2_demo_native native_ui_probe > "$OUT/build.txt" 2>&1; then
  echo "  OK build"
else
  echo "  FAIL build (see $OUT/build.txt)"; tail -30 "$OUT/build.txt"; exit 1
fi
for path in "$EXE" "$PROBE" "$TF" "$DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

echo "=== 1/3 predicate controls (native_ui_probe) ==="
"$PROBE" > "$OUT/probe.log" 2>&1
probe_rc=$?
assert_eq "probe rc" "$probe_rc" "0"
if grep -q "PASS native_ui_probe" "$OUT/probe.log"; then
  echo "  OK   probe reports PASS"
else
  echo "  FAIL probe did not report PASS"; fail=1
fi
# The counts the probe publishes: ten rules, sixteen negative controls. The
# negative count is the one this slice moved (13 -> 16: three inset controls),
# so a probe that stopped exercising the inset branch reads as a red gate.
assert_eq "pane boundary rules" "$(pvalue "$OUT/probe.log" paneBoundaryRules)" "10"
assert_eq "pane boundary negatives" "$(pvalue "$OUT/probe.log" paneBoundaryNegatives)" "16"

echo "=== 2/3 the product's two layouts ==="
# The layout line travels with the frame on disk, so each layout needs its own
# capture: --capture-frame is a CLI arm, while the camera script's capture step
# writes a frame whose readings this trace block does not annotate. Two runs, one
# capture each, is what makes the rectangles and the bitmap describe one frame.
CAPTURE_TICK=1200
: > "$OUT/settings.ini"
: > "$OUT/ui.ini"
TRACE_SHELL=""
FRAME_RC=0

run_frame() { # <name> <extra product args...>
  local name=$1; shift
  local trace_shell="$OUT/$name.trace"
  local trace; trace="$(win "$PWD/$trace_shell")"
  rm -f "$trace_shell"
  TF2_NATIVE_TRACE="$trace" timeout -k 5 "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
    --capture-tick "$CAPTURE_TICK" --capture-frame "$(win "$PWD/$OUT/cap-$name.bmp")" \
    --settings-file "$(win "$PWD/$OUT/settings.ini")" \
    --ui-settings-file "$(win "$PWD/$OUT/ui.ini")" \
    "$@" > "$OUT/$name.log" 2>&1
  FRAME_RC=$?
  # The product trace is CRT text mode; normalise to LF before any reading.
  if [ -s "$trace_shell" ]; then
    tr -d "\r" < "$trace_shell" > "$trace_shell.lf" && mv "$trace_shell.lf" "$trace_shell"
  fi
  TRACE_SHELL="$trace_shell"
}

field() { # <line> <name> -- name=value token
  printf "%s\n" "$1" | grep -oE "$2=[0-9,x]+" | head -1 | cut -d= -f2
}
rect() { # <line> <pane> -- "x y w h"; the trace prints x,y,WxH
  local raw; raw=$(field "$1" "$2")
  local rx ry wh; IFS=, read -r rx ry wh <<< "$raw"
  printf "%s %s %s %s" "$rx" "$ry" "${wh%x*}" "${wh#*x}"
}

split_line=""
pip_line=""
for layout in split pip; do
  if [ "$layout" = "pip" ]; then
    run_frame pip --pane-split --pane1-orbit 1.10 0.45 1.35 --pane-layout pip
  else
    run_frame split --pane-split --pane1-orbit 1.10 0.45 1.35
  fi
  assert_eq "$layout run rc" "$FRAME_RC" "0"
  if [ ! -s "$TRACE_SHELL" ]; then
    echo "  FAIL the $layout run wrote no trace"; fail=1; continue
  fi
  line=$(grep -a "pane: layout=$layout " "$TRACE_SHELL" | tail -1)
  if [ -z "$line" ]; then
    echo "  FAIL the $layout run captured no layout reading"; fail=1; continue
  fi
  if [ "$layout" = "pip" ]; then pip_line=$line; else split_line=$line; fi
done

if [ -n "$split_line" ]; then
  read -r s0x s0y s0w s0h <<< "$(rect "$split_line" p0)"
  read -r s1x s1y s1w s1h <<< "$(rect "$split_line" p1)"
  split_client=$(field "$split_line" client)
  s_cw=${split_client%x*}; s_ch=${split_client#*x}
  # Split: two disjoint halves that tile the client, exactly what U4 shipped.
  assert_eq "split pane 0 x" "$s0x" "0"
  assert_eq "split pane 0 width (half the client)" "$s0w" "$((s_cw / 2))"
  assert_eq "split pane 1 x (right half)" "$s1x" "$((s_cw / 2))"
  assert_eq "split halves tile the client" "$((s0w + s1w))" "$s_cw"
  assert_eq "split pane-contract mismatches" "$(field "$split_line" mismatches)" "0x0"
fi

if [ -n "$pip_line" ]; then
  read -r i0x i0y i0w i0h <<< "$(rect "$pip_line" p0)"
  read -r i1x i1y i1w i1h <<< "$(rect "$pip_line" p1)"
  pip_client=$(field "$pip_line" client)
  i_cw=${pip_client%x*}; i_ch=${pip_client#*x}
  # Pip: pane 0 owns the client, pane 1 is a smaller rectangle over it.
  assert_eq "pip pane 0 is the client (x,y)" "$i0x,$i0y" "0,0"
  assert_eq "pip pane 0 is the client (w x h)" "$i0w x $i0h" "$i_cw x $i_ch"
  assert_num_lt "pip pane width is smaller than the client" "$i1w" "$i_cw"
  assert_num_lt "pip pane height is smaller than the client" "$i1h" "$i_ch"
  assert_num_le "pip pane stays inside the client (x + w)" "$((i1x + i1w))" "$i_cw"
  assert_num_le "pip pane stays inside the client (y + h)" "$((i1y + i1h))" "$i_ch"
  assert_eq "pip pane is not pinned to the top-left corner" \
    "$((i1x > 0 && i1y > 0 ? 1 : 0))" "1"
  assert_eq "pip pane-contract mismatches" "$(field "$pip_line" mismatches)" "0x0"
  # The CLI arm is the other way in; the trace says which one set the layout.
  assert_eq "the CLI set the pip layout" \
    "$(grep -ac "paneLayout: layout=pip source=cli" "$TRACE_SHELL" || true)" "1"
fi

# The step's own report, so "the layout changed" is not inferred from pixels,
# and its refusal, so a name it does not know cannot pass as a layout.
REFUSE_SCRIPT="$OUT/refuse.script"
printf "pane-layout pip\npane-layout bogus\nquit\n" > "$REFUSE_SCRIPT"
TRACE_SHELL="$OUT/refuse.trace"
TRACE="$(win "$PWD/$TRACE_SHELL")"
rm -f "$TRACE_SHELL"
TF2_NATIVE_TRACE="$TRACE" timeout -k 5 "$RUN_TIMEOUT" "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --start-paused \
  --settings-file "$(win "$PWD/$OUT/settings.ini")" \
  --ui-settings-file "$(win "$PWD/$OUT/ui.ini")" \
  --camera-script "$(win "$PWD/$REFUSE_SCRIPT")" > "$OUT/refuse.log" 2>&1
refuse_rc=$?
if [ -s "$TRACE_SHELL" ]; then
  tr -d "\r" < "$TRACE_SHELL" > "$TRACE_SHELL.lf" && mv "$TRACE_SHELL.lf" "$TRACE_SHELL"
fi
# A script with a failing step fails the run; saying so is what keeps the
# refusal from being a line nobody reads.
assert_eq "refusal run rc (a failing step fails the run)" "$refuse_rc" "1"
assert_eq "script step set the pip layout" \
  "$(grep -ac "cmd=pane-layout result=ok detail=pip" "$TRACE_SHELL" || true)" "1"
assert_eq "script step refused an unknown layout" \
  "$(grep -ac "cmd=pane-layout result=fail detail=bad-argument" "$TRACE_SHELL" || true)" "1"

echo "=== 3/3 verdict ==="
if [ "$fail" -eq 0 ]; then
  echo "PANE-PIP=PASS"
  exit 0
fi
echo "PANE-PIP=FAIL"
exit 1
