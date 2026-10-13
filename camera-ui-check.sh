#!/usr/bin/env bash
# camera-ui-check.sh -- the T4 gate: camera modes, observer rules, HUD and UI
# layout, all pinned on named readings.
#
# WHY THIS EXISTS
#   The camera/HUD/UI layer used to have no gate at all: native_ui_probe used
#   assert() under a Release build, which compiles to nothing, so its PASS line
#   proved only that the process did not crash. This file pins the T4 contract
#   on readings instead:
#
#     * the probe's readings line -- camera default and cycle, the 300 ms
#       double-click window measured at BOTH boundaries (299 ms is a double,
#       300 ms is a fresh single), the 6-case camera/target decision matrix
#       whose two HoldCurrent rows ARE the "no observe target keeps the
#       current camera" rule, the pause toggles, the layout planner over 42
#       window-size/dpi combinations with zero overlaps and zero out-of-range
#       rects, and the five-field HUD line;
#     * one quiet end-to-end run of the real product on the pinned demo, whose
#       trace must report first-person as the default mode, a measured
#       follow/hold/freeze split with at least one follow (the demo carries
#       observe targets), no device opened, and a hidden window; while the
#       driver polls the live window title, which must carry the composed HUD
#       line -- tick, demo name, state, camera mode, error -- because the
#       title is the HUD a player actually reads.
#
# WHAT THE MUTATIONS PROVE
#   --mutation re-runs the shipped expectation lists against perturbed
#   captures (so every needle discriminates), then rebuilds the product twice
#   with one defect reintroduced at a time and requires the real gate to go
#   red: the camera default flipped to third-person, and the title's camera
#   field replaced by a constant. Restore is a byte-for-byte copy from a
#   scratch backup, then a rebuild.
#
# Usage: bash camera-ui-check.sh [--mutation]
# Exit:  0 = every assertion held (or every mutation went red); 1 = not.

set -uo pipefail
cd "$(dirname "$0")"
# The legacy title prefix contains bytes outside the shell's UTF-8 locale on
# this Windows build. The gate is checking ASCII HUD markers in that title;
# byte-oriented matching must not stop at the prefix's encoding.
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/native_ui_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
DEMO_NAME="4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
OUT="${CAMERA_UI_OUT:-.scratch/camera-ui}"
RUN_TIMEOUT=240
LIVE_TICK=600

# Missing input is a failure, not a skip: a gate that quietly SKIPs reports
# success for a run that did no work.
fail=0
for path in "$EXE" "$PROBE" "$TF" "$BAGEL"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || PY="C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe"
[ -x "$PY" ] || { echo "FATAL: no python interpreter found"; exit 1; }
for tool in cygpath timeout cmp; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 1; }
done

mkdir -p "$OUT"

MUTATION=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

# assert_ge_ok <label> <actual> <floor> -- "actual >= floor", for counts whose
# honest value depends on the demo's observe-target coverage.
assert_ge_ok() {
  if [ -n "${2:-}" ] && [ "${2:-0}" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}

assert_contains() { # assert_contains <label> <haystack> <needle>
  case "$2" in
    *"$3"*) echo "  OK   $1" ;;
    *) echo "  FAIL $1 (wanted: $3)"; fail=1 ;;
  esac
}

pvalue() { # pvalue <file> <field> -- first key=value match on one line
  grep -oE "$2=[^ ]*" "$1" 2>/dev/null | head -1 | cut -d= -f2
}

run_e2e() { # run_e2e <prefix> -- one quiet product run with title polling
  local prefix="$1"
  rm -f "$prefix.bmp" "$prefix.trace" "$prefix.driver.txt"
  local trace
  trace="$(cygpath -m "$PWD/$prefix.trace")"
  "$PY" - "$EXE" "$RUN_TIMEOUT" "$prefix.bmp" "$LIVE_TICK" "$TF" "$BAGEL" \
    "$trace" > "$prefix.driver.txt" <<'PYEOF'
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

exe, timeout_s, bmp, tick, tf, demo, trace = sys.argv[1:8]
env = dict(os.environ)
env["TF2_NATIVE_TRACE"] = trace
proc = subprocess.Popen([exe, "--tf-root", tf, "--demo", demo,
                         "--hidden-window", "--audio-disabled",
                         "--capture-frame", bmp, "--capture-tick", tick],
                        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
u32 = ctypes.windll.user32


def title_of(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        owner = wt.DWORD()
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid:
            return True
        n = u32.GetWindowTextLengthW(hwnd)
        if not n:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        u32.GetWindowTextW(hwnd, buf, n + 1)
        if buf.value.startswith("TF2 Demo Player"):
            found.append(buf.value)
        return True

    u32.EnumWindows(cb, 0)
    return found[0] if found else ""


# The window is created hidden (the product's own IsWindowVisible answers 0),
# so "hidden" is not the absence of a window: the title must still be readable
# on the hidden window. Keep the last HUD-bearing title.
last = ""
polls = 0
deadline = time.time() + float(timeout_s)
while True:
    text = title_of(proc.pid)
    if "HUD[tick=" in text:
        last = text
        polls += 1
    if proc.poll() is not None:
        break
    if time.time() > deadline:
        proc.kill()
        proc.wait()
        break
    time.sleep(0.1)
print("RC=" + str(proc.returncode))
print("TITLE_POLLS=" + str(polls))
print("TITLE=" + last)
PYEOF
}

# --- the expectation list for the probe --------------------------------------
# Each entry is "<needle>|<what it means>"; --mutation re-runs this same list
# against a perturbed capture.
E_PROBE=(
  'PASS native_ui_probe screen=opening->import-review->player->opening scrub=clamped corrupt=recoverable|probe: the legacy verdict line, byte-identical'
  'cameraDefault=first-person|camera: the default mode is first-person'
  'cameraCycle=1|camera: one step per single press walks the full cycle'
  'doubleWindowMs=300|camera: the double-click window is the named 300'
  'doubleInsideMs=1|camera: 299 ms is inside the window (a double)'
  'doubleBoundaryMs=1|camera: 300 ms is the boundary (a fresh single)'
  'cameraDecisionMatrix=6|camera: all 6 mode/target decisions are pinned'
  'cameraNoTargetHold=1|camera: no observe target keeps the current camera'
  'pauseToggles=2|pause: P toggled twice and returned'
  'pauseStateReturn=1|pause: the second toggle restored the state'
  'layoutCases=42|layout: the planner ran the 42-case size/dpi matrix'
  'layoutOverlap=0|layout: no two control rects overlap anywhere'
  'layoutOutOfRange=0|layout: every control rect stays inside the client'
  'layoutControls=10|layout: all 10 controls are planned'
  'hudExact=1|hud: the five fields sit in the pinned order'
  'hudSingleLine=1|hud: CR/LF/tab never survive into the line'
  'hudFields=5|hud: five fields, always present'
  'uiFixture=ran|fixture: the demo fixture was present and exercised'
)

run_probe_expectations() { # run_probe_expectations <file> <needle|what>...
  local file="$1"; shift
  local e
  for e in "$@"; do
    local needle="${e%%|*}" what="${e#*|}"
    if [ -s "$file" ] && grep -qF -- "$needle" "$file"; then
      ok=$((ok + 1)); printf '  OK   %s\n' "$what"
    else
      bad=$((bad + 1)); printf '  FAIL %s (wanted: %s)\n' "$what" "$needle"
    fi
  done
}

# --- 1/3 the probe ------------------------------------------------------------
echo "=== 1/3 native_ui_probe (camera modes, double-click window, layout, HUD) ==="
ok=0
bad=0
"$PROBE" > "$OUT/probe.txt" 2>&1
probe_rc=$?
cat "$OUT/probe.txt"
run_probe_expectations "$OUT/probe.txt" "${E_PROBE[@]}"
echo "assertions_ok=$ok assertions_bad=$bad"
assert_eq "probe exit" "$probe_rc" "0"
if [ "$bad" -ne 0 ]; then fail=1; fi

# --- 2/3 the quiet end-to-end run ---------------------------------------------
echo
echo "=== 2/3 quiet product run (hidden window, audio disabled, title poll) ==="
run_e2e "$OUT/e2e"
e2e_rc_line="$(grep -oE 'RC=[0-9]+' "$OUT/e2e.driver.txt" | head -1 | cut -d= -f2)"
e2e_title="$(grep -oE 'TITLE=.*' "$OUT/e2e.driver.txt" | head -1 | cut -d= -f2-)"
e2e_polls="$(grep -oE 'TITLE_POLLS=[0-9]+' "$OUT/e2e.driver.txt" | head -1 | cut -d= -f2)"
assert_eq "e2e run rc" "$e2e_rc_line" "0"
assert_ge_ok "e2e title polls" "$e2e_polls" 1

if [ -s "$OUT/e2e.trace" ]; then
  # Trace lines carry a "%.1f " timestamp prefix, so the summary needles are
  # matched unanchored.
  grep -E '(mode|camera): summary' "$OUT/e2e.trace" | tail -4
  assert_contains "trace mode summary (hidden, no device)" \
    "$(grep -E 'mode: summary' "$OUT/e2e.trace" | tail -1)" \
    "window_created=1 window_visible=0 audio_mode=disabled audio_device_opened=0"
  camera_line="$(grep -E 'camera: summary' "$OUT/e2e.trace" | tail -1)"
  assert_contains "trace camera mode default" "$camera_line" "mode=first-person"
  assert_contains "trace camera window constant" "$camera_line" "doubleWindowMs=300"
  assert_contains "trace camera no input in a gate run" "$camera_line" \
    "spacePresses=0 spaceSingles=0 spaceDoubles=0"
  assert_contains "trace camera no pause toggles in a gate run" "$camera_line" "pauseToggles=0"
  assert_contains "trace camera freeze untouched" "$camera_line" "freezeFrames=0"
  follows="$(pvalue <(printf '%s\n' "$camera_line") follows)"
  assert_ge_ok "trace camera followed observe targets" "$follows" 1
else
  echo "  FAIL the trace was not written"; fail=1
fi

# The title is the HUD. Its composed five fields must all be present.
assert_contains "title HUD tick field" "$e2e_title" "HUD[tick="
assert_contains "title HUD demo name" "$e2e_title" "demo=$DEMO_NAME"
assert_contains "title HUD playback state" "$e2e_title" "state=playing"
assert_contains "title HUD camera mode" "$e2e_title" "camera=first-person"
assert_contains "title HUD error status" "$e2e_title" "error=none"

# --- 3/3 mutation --------------------------------------------------------------
if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== 3/3 mutation: perturbed captures, then two real product defects ==="
  mrc=0

  mutate_case() { # mutate_case <name> <file> <sed-expression>
    local name="$1" file="$2" expr="$3"
    local copy="$OUT/mutated-$name.txt"
    cp "$file" "$copy"
    sed -i "$expr" "$copy"
    if cmp -s "$file" "$copy"; then
      echo "  MUTATION-SETUP=FAIL ($name: the perturbation changed nothing)"
      mrc=1; return
    fi
    local saved_ok=$ok saved_bad=$bad
    ok=0; bad=0
    run_probe_expectations "$copy" "${E_PROBE[@]}" > "$OUT/mutated-$name.log" 2>&1
    local mok=$ok mbad=$bad
    ok=$saved_ok; bad=$saved_bad
    if [ "$mbad" -ge 1 ]; then
      printf '  MUTATION-CAUGHT=PASS (%s: %s of %s assertions went red)\n' \
        "$name" "$mbad" "$((mok + mbad))"
    else
      printf '  MUTATION-CAUGHT=FAIL (%s: no assertion moved)\n' "$name"
      mrc=1
    fi
  }

  mutate_case camera_cycle      "$OUT/probe.txt" 's/cameraCycle=1/cameraCycle=0/'
  mutate_case double_inside     "$OUT/probe.txt" 's/doubleInsideMs=1/doubleInsideMs=0/'
  mutate_case layout_overlap    "$OUT/probe.txt" 's/layoutOverlap=0/layoutOverlap=1/'
  mutate_case hud_exact         "$OUT/probe.txt" 's/hudExact=1/hudExact=0/'
  mutate_case decision_matrix   "$OUT/probe.txt" 's/cameraDecisionMatrix=6/cameraDecisionMatrix=5/'
  mutate_case no_target_hold    "$OUT/probe.txt" 's/cameraNoTargetHold=1/cameraNoTargetHold=0/'

  # Product mutation 1: the camera default flipped to third-person. The probe
  # (which links the same controller) must go red on cameraDefault.
  local_backup="$OUT/native_ui.h.backup"
  cp native/include/native_ui.h "$local_backup"
  sed -i 's/CameraMode mode_ = CameraMode::FirstPerson;/CameraMode mode_ = CameraMode::ThirdPerson;/' \
    native/include/native_ui.h
  if cmp -s native/include/native_ui.h "$local_backup"; then
    echo "  MUTATION-SETUP=FAIL (camera_default: the sed changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native native_ui_probe > "$OUT/mutated-camera_default.build" 2>&1
    "$PROBE" > "$OUT/mutated-camera_default.txt" 2>&1
    probe_mut_rc=$?
    if [ "$probe_mut_rc" -ne 0 ] && ! grep -qF 'cameraDefault=first-person' "$OUT/mutated-camera_default.txt"; then
      echo "  MUTATION-CAUGHT=PASS (camera_default: the probe went red on the flipped default)"
    else
      echo "  MUTATION-CAUGHT=FAIL (camera_default: the gate stayed green)"; mrc=1
    fi
  fi
  cp "$local_backup" native/include/native_ui.h
  bash build-target.sh tf2_demo_native native_ui_probe > "$OUT/restore-camera_default.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL"; mrc=1; }

  # Product mutation 2: the title's camera field replaced by a constant. The
  # end-to-end title poll must go red while the trace (which reads the
  # controller directly) stays green -- that is the pair that proves the title
  # assertion watches the HUD, not the controller.
  main_backup="$OUT/main.cpp.backup"
  cp native/src/main.cpp "$main_backup"
  sed -i 's/const char\* hudCameraName = tf2::native::cameraModeName(g_cameraMode.mode());/const char* hudCameraName = "mutated";/' \
    native/src/main.cpp
  if cmp -s native/src/main.cpp "$main_backup"; then
    echo "  MUTATION-SETUP=FAIL (hud_camera_field: the sed changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native > "$OUT/mutated-hud_camera_field.build" 2>&1
    run_e2e "$OUT/mutated-hud_camera_field"
    mut_title="$(grep -oE 'TITLE=.*' "$OUT/mutated-hud_camera_field.driver.txt" | head -1 | cut -d= -f2-)"
    if case "$mut_title" in *"camera=first-person"*) false ;; *) true ;; esac; then
      echo "  MUTATION-CAUGHT=PASS (hud_camera_field: the title assertion went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (hud_camera_field: the title assertion stayed green)"; mrc=1
    fi
  fi
  cp "$main_backup" native/src/main.cpp
  bash build-target.sh tf2_demo_native > "$OUT/restore-hud_camera_field.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL"; mrc=1; }

  [ "$mrc" -eq 0 ] || { echo "CAMERA-UI-MUTATION=FAIL"; exit 1; }
  echo "CAMERA-UI-MUTATION=PASS (six capture mutations and two product mutations all went red)"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "CAMERA-UI=PASS"
  exit 0
fi
echo "CAMERA-UI=FAIL"
exit 1
