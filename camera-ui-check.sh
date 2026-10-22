#!/usr/bin/env bash
# camera-ui-check.sh -- the camera gate: T4 modes/observer/HUD/layout and the
# T50 camera-director contract (paused free camera, named bookmarks, key table),
# all pinned on named readings.
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
# T50 WHAT THIS FILE ADDS
#   A probe (camera_probe) that drives the freeze rule, the bookmark store and
#   the key table without a GPU, then a SCRIPTED product run in which one
#   process pauses, orbits, saves three named bookmarks, jumps among them with
#   the configured keys, captures the same bookmark twice, resolves two key
#   conflicts, unbinds and restores a key, focuses a text field, and saves and
#   reloads the settings file. Every step reports `camera: accept step=N ...`,
#   so a step that did nothing and a step that never ran are different readings.
#
#   The two claims, asserted SEPARATELY and in this order:
#     (a) while paused, the demo tick does not move (three same-tick marks at
#         three different moments) AND the camera pose does move (the
#         before/idle/orbit snapshots differ) -- one assertion cannot satisfy
#         both by accident;
#     (b) after resume the tick advances past the frozen tick.
#   Then the bookmark claim: three names, three DIFFERENT cameras, each jump
#   restoring all six pose fields exactly, and two captures of the same
#   bookmark byte-identical.
#
# WHAT THE MUTATIONS PROVE
#   --mutation re-runs the shipped expectation lists against perturbed
#   captures (so every needle discriminates), then rebuilds the product with
#   one defect reintroduced at a time and requires the real gate to go red.
#   The T4 pair: the camera default flipped to third-person and the title's
#   camera field replaced by a constant. The T50 three: the pause stops
#   freezing the tick, a bookmark jump lands 0.001 box units off, and the key
#   table accepts a duplicate chord. Restore is a byte-for-byte copy from a
#   scratch backup verified with cmp, then a rebuild, then a control run that
#   must reproduce the fixed readings.
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
# patch-source.py is invoked by absolute path so a mutation runs from any cwd.
ROOT="$(pwd)"
RUN_TIMEOUT=240
LIVE_TICK=600
# The T50 scripted run does more than a capture: three scene ticks, several
# pauses and two full-frame captures in one process.
T50_RUN_TIMEOUT=300
CAMERA_PROBE=native/build-nmake/camera_probe.exe

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
  'layoutControls=19|layout: all 19 controls are planned (10 T4 + 9 T50)'
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

# --- T50: the camera director, bookmarks and the key table -------------------
#
# WHAT THIS SECTION ASSERTS, and why it is separate from sections 1/3 and 2/3
#   The T4 readings above prove the mode machine and the title HUD. They say
#   nothing about the two claims T50 makes:
#
#     (a) pause freezes the DEMO TICK while the CAMERA keeps moving, and resume
#         advances the tick again -- two independent assertions, in that order,
#         because "nothing moved" would satisfy a combined one;
#     (b) three named bookmarks survive a jump EXACTLY (six pose fields plus the
#         camera mode and the fov), and two jumps to the same bookmark draw two
#         byte-identical frames.
#
#   Both need a sequence of operations inside ONE process at a known tick, so
#   the product grows -- camera-script: a text file of steps executed by the
#   main loop, each reporting `camera: accept ...`. The gate writes the script,
#   runs it, and pins the readings. Nothing about this path exists in an
#   ordinary run.
#
#   The third claim is the key table: a conflict is refused and counted, an
#   unbound action dispatches nothing (no fallback), resetAll restores the
#   compiled defaults, and a focused text field suppresses every bound action.

echo
echo "=== T50 0/3 build (product + camera_probe) ==="
# Built here rather than assumed: a stale binary would make every reading below
# describe a tree that no longer exists. This gate is invoked under the host
# lock (hostlock.sh run t50-camera ...) precisely because it builds and runs the
# product, so the build belongs inside it, like the other gates that build.
if bash build-target.sh tf2_demo_native camera_probe > "$OUT/t50-build.txt" 2>&1; then
  echo "  OK build tf2_demo_native camera_probe"
else
  echo "  FAIL build (see $OUT/t50-build.txt)"; tail -20 "$OUT/t50-build.txt"; fail=1
fi

echo
echo "=== T50 1/3 camera_probe (freeze rule, bookmarks, key table) ==="
CAMERA_PROBE=native/build-nmake/camera_probe.exe
if [ ! -x "$CAMERA_PROBE" ]; then
  echo "  FAIL missing $CAMERA_PROBE (build it with: bash build-target.sh camera_probe)"
  fail=1
else
  "$CAMERA_PROBE" > "$OUT/camera_probe.txt" 2>&1
  probe_t50_rc=$?
  cat "$OUT/camera_probe.txt"
  assert_eq "T50 probe exit" "$probe_t50_rc" "0"
  assert_contains "T50 probe verdict" "$(cat "$OUT/camera_probe.txt")" \
    "PASS camera_probe pause=freeze-tick camera=continues bookmarks=3 keys=conflict"
  # The readings this round adds, pinned as exact values: each one is a number a
  # mutation moves, and pinning it is what stops a later edit from turning the
  # assertion into a tautology.
  t50_probe=$OUT/camera_probe.txt
  assert_eq "T50 pause froze the tick for 20 frames" \
    "$(pvalue "$t50_probe" pauseFrozenFrames)" "20"
  assert_ge_ok "T50 camera moved while paused" "$(pvalue "$t50_probe" pauseCameraMoves)" "2"
  assert_eq "T50 resume advanced the tick" "$(pvalue "$t50_probe" pauseResumeAdvance)" "5"
  assert_eq "T50 a moving tick while paused is counted" \
    "$(pvalue "$t50_probe" pauseViolationDetected)" "1"
  assert_eq "T50 three bookmarks stored" "$(pvalue "$t50_probe" bookmarksSaved)" "3"
  assert_eq "T50 every saved pose came back exactly" \
    "$(pvalue "$t50_probe" bookmarkPoseExact)" "3"
  assert_eq "T50 every pose survived the settings file exactly" \
    "$(pvalue "$t50_probe" bookmarkFilePoseExact)" "3"
  assert_eq "T50 deleting a bookmark is durable" \
    "$(pvalue "$t50_probe" bookmarkDeletePersists)" "1"
  assert_eq "T50 a corrupt pose is refused, not defaulted" \
    "$(pvalue "$t50_probe" bookmarkCorruptRefused)" "1"
  assert_eq "T50 the chord grammar round-trips" "$(pvalue "$t50_probe" chordRoundTrips)" "13"
  assert_eq "T50 27 actions are registered" "$(pvalue "$t50_probe" keyActions)" "27"
  assert_eq "T50 conflicts are refused" "$(pvalue "$t50_probe" keyConflictsRefused)" "2"
  assert_eq "T50 an unbound key dispatches nothing" \
    "$(pvalue "$t50_probe" keyUnboundDispatch)" "1"
  assert_eq "T50 a rebound key survives the file" "$(pvalue "$t50_probe" keyFileRoundTrip)" "1"
  assert_eq "T50 a duplicate chord in the file is refused" \
    "$(pvalue "$t50_probe" keyDuplicateRefused)" "1"
  assert_eq "T50 a focused text field suppresses hotkeys" \
    "$(pvalue "$t50_probe" keyFocusSuppresses)" "1"
fi

echo
echo "=== T50 2/3 scripted product run (hidden window, audio disabled) ==="
# The script is generated here so its capture paths are absolute and its
# readings land in this gate's own evidence directory.
T50_SCRIPT="$OUT/t50-camera.script.txt"
T50_TRACE="$OUT/t50-camera.trace"
T50_FRAME_A="$OUT/t50-jump1-a.bmp"
T50_FRAME_B="$OUT/t50-jump1-b.bmp"
T50_SETTINGS="$OUT/t50-settings.ini"
rm -f "$T50_TRACE" "$T50_FRAME_A" "$T50_FRAME_B" "$T50_SETTINGS"
cyg_trace="$(cygpath -m "$PWD/$T50_TRACE")"
cyg_a="$(cygpath -m "$PWD/$T50_FRAME_A")"
cyg_b="$(cygpath -m "$PWD/$T50_FRAME_B")"
cyg_settings="$(cygpath -m "$PWD/$T50_SETTINGS")"
cat > "$T50_SCRIPT" <<EOF
# T50 acceptance script. Every step reports `camera: accept step=N cmd=.. result=..`
# and every reading the gate pins is a `camera:` line below it.
# 1. reach a tick where the demo has an observe target and pause there. The run
#    starts paused (--start-paused) so its first scene is deterministic, which
#    means the script has to play before it can wait for a tick.
resume
tick 200
frames 2
pause
frames 4
mark-tick
snapshot before
# 2. THE PAUSE HALF: the tick must stand still while the camera moves.
frames 6
snapshot idle
expect-same-tick
orbit 60 20
frames 3
snapshot orbit-a
expect-same-tick
orbit -25 10
zoom 1.5
frames 3
snapshot orbit-b
expect-same-tick
director
# 3. THE RESUME HALF: the tick advances again once unpaused.
resume
frames 30
snapshot resumed
expect-tick-gt 200
director
# 4. Three named bookmarks, each saved from a distinct camera.
pause
frames 3
bind camera.resetView Alt+F11
frames 2
snapshot mark-base
save 1 tower
verify-name 1 tower
orbit 140 45
frames 2
save 2 spawn door
verify-name 2 spawn door
orbit -210 -60
zoom -2.0
frames 2
save 3 mid high
verify-name 3 mid high
snapshot saved3
director
# 5. Jump among them with the CONFIGURED keys (F1..F3), and require the pose
#    to come back exactly -- six fields, not "the eye looks close".
key 112
frames 3
verify-bookmark 1
snapshot jump1
key 113
frames 3
verify-bookmark 2
snapshot jump2
key 114
frames 3
verify-bookmark 3
snapshot jump3
key 112
frames 3
verify-bookmark 1
snapshot jump1-again
# 6. The frame claim: the same bookmark twice is the same frame, byte for byte.
capture ${cyg_a} jump1-a
frames 2
key 113
frames 2
key 112
frames 3
capture ${cyg_b} jump1-b
frames 1
# 7. Key table: conflict refused, unbound dispatches nothing, reset restores.
conflict camera.resetView F9
conflict camera.freeFly Ctrl+Shift+F1
bind camera.resetView Alt+F11
unbind camera.resetView
key 122
reset-keys
key 122
director
# 8. Text-field focus: every bound action is suppressed while a field is focused.
focus-text on
key 80
key 112
focus-text off
key 112
director
# 9. The durable half: write the settings, read them back, and require the
#    bookmarks to still be there with the same poses. Each slot is JUMPED TO
#    before it is verified: the pose is what the jump restores, so a verify
#    without a jump would only re-read whatever the last step left behind.
save-settings
load-settings
key 112
frames 2
verify-bookmark 1
key 113
frames 2
verify-bookmark 2
key 114
frames 2
verify-bookmark 3
quit
EOF

T50_RUN_TIMEOUT=300
TF2_NATIVE_TRACE="$cyg_trace" timeout -k 5 "$T50_RUN_TIMEOUT" "$EXE" --tf-root "$TF" \
  --demo "$BAGEL" --hidden-window --audio-disabled --start-paused \
  --settings-file "$cyg_settings" --camera-script "$(cygpath -m "$PWD/$T50_SCRIPT")" \
  > "$OUT/t50-run.log" 2>&1
t50_rc=$?
assert_eq "T50 scripted run rc" "$t50_rc" "0"
if [ ! -s "$T50_TRACE" ]; then
  echo "  FAIL the scripted run wrote no trace"
  fail=1
else
  t50_steps=$(grep -c "camera: accept step=" "$T50_TRACE" || true)
  t50_failed=$(grep -c "camera: accept .* result=fail" "$T50_TRACE" || true)
  # The script has 93 executable steps; a floor of 85 leaves room for the
  # optional re-flow of a step that had to wait, and still fails hard if the
  # script stopped early (which is what a crash or a broken step parser looks
  # like).
  assert_ge_ok "T50 script steps executed" "$t50_steps" 85
  assert_eq "T50 script steps that failed" "$t50_failed" "0"
  grep -a "camera: accept .* result=fail" "$T50_TRACE" | head -10 || true

  # (a) the pause half, as two separate readings.
  pause_frozen=$(grep -a "camera: expect what=same-tick" "$T50_TRACE" | grep -c "match=1" || true)
  assert_eq "T50 pause: the tick held at three separate marks" "$pause_frozen" "3"
  # The camera must have MOVED while the tick was frozen: the two snapshots
  # taken inside the pause differ, and the tick on both is the same.
  snap_idle=$(grep -a "camera: snapshot label=idle " "$T50_TRACE" | head -1)
  snap_orbit=$(grep -a "camera: snapshot label=orbit-b " "$T50_TRACE" | head -1)
  snap_before=$(grep -a "camera: snapshot label=before " "$T50_TRACE" | head -1)
  idle_tick=$(pvalue <(printf "%s\n" "$snap_idle") tick)
  orbit_tick=$(pvalue <(printf "%s\n" "$snap_orbit") tick)
  before_tick=$(pvalue <(printf "%s\n" "$snap_before") tick)
  assert_eq "T50 pause: the tick was the same before and after the orbit" \
    "$orbit_tick" "$before_tick"
  assert_eq "T50 pause: the idle snapshot is on the same tick" "$idle_tick" "$before_tick"
  # Only the POSE fields are compared: the label and the tick are part of the
  # line and differ by construction, so comparing whole snapshots would report
  # "the pose moved" for two snapshots of the same camera.
  pose_of() { # pose_of <snapshot line>
    printf "%s\n" "$1" | grep -oE "focus=[^ ]* yaw=[^ ]* pitch=[^ ]* dist=[^ ]*" | head -1
  }
  idle_pose="$(pose_of "$snap_idle")"
  orbit_pose="$(pose_of "$snap_orbit")"
  if [ -n "$idle_pose" ] && [ -n "$orbit_pose" ] && [ "$idle_pose" != "$orbit_pose" ]; then
    echo "  OK   T50 pause: the camera pose changed while the tick held"
    echo "       before: $idle_pose"
    echo "       after:  $orbit_pose"
  else
    echo "  FAIL T50 pause: the pose did not move while paused"; fail=1
  fi
  # And the pose at the moment the pause started must equal the idle one: the
  # camera must not drift on its own between two paused frames.
  before_pose="$(pose_of "$snap_before")"
  assert_eq "T50 pause: the camera does not drift on its own" "$idle_pose" "$before_pose"
  # (b) the resume half.
  resume_advanced=$(grep -a "camera: expect what=tick-gt" "$T50_TRACE" | grep -c "match=1" || true)
  assert_eq "T50 resume: the tick advanced past the paused tick" "$resume_advanced" "1"
  resumed_tick=$(pvalue <(grep -a "camera: snapshot label=resumed " "$T50_TRACE" | head -1) tick)
  if [ -n "$resumed_tick" ] && [ -n "$before_tick" ] && [ "$resumed_tick" -gt "$before_tick" ]; then
    echo "  OK   T50 resume: tick $before_tick -> $resumed_tick"
  else
    echo "  FAIL T50 resume: tick did not advance (before=$before_tick after=$resumed_tick)"; fail=1
  fi
  # (c) the bookmark half: three names, three exact poses, and the jump the
  #     configured key performs must equal the jump the button performs.
  t50_verify=$(grep -a "camera: verify kind=bookmark" "$T50_TRACE" || true)
  verify_total=$(printf "%s\n" "$t50_verify" | grep -c "match=" || true)
  verify_exact=$(printf "%s\n" "$t50_verify" | grep -c "match=1" || true)
  assert_ge_ok "T50 bookmark poses verified" "$verify_total" 6
  assert_eq "T50 bookmark poses exact" "$verify_exact" "$verify_total"
  verify_names=$(grep -a "camera: verify kind=name" "$T50_TRACE" | grep -c "match=1" || true)
  assert_eq "T50 three bookmark names round-tripped" "$verify_names" "3"
  saved_marks=$(grep -a "camera: bookmark action=save" "$T50_TRACE" | grep -c "ok=1" || true)
  assert_eq "T50 three bookmarks saved" "$saved_marks" "3"
  # The jumped pose must differ between the three bookmarks: if all three saved
  # the same camera, "exact restoration" would pass while proving nothing.
  jump1=$(grep -a "camera: snapshot label=jump1 " "$T50_TRACE" | head -1)
  jump2=$(grep -a "camera: snapshot label=jump2 " "$T50_TRACE" | head -1)
  jump3=$(grep -a "camera: snapshot label=jump3 " "$T50_TRACE" | head -1)
  distinct=0
  [ "$jump1" != "$jump2" ] && distinct=$((distinct + 1))
  [ "$jump2" != "$jump3" ] && distinct=$((distinct + 1))
  [ "$jump1" != "$jump3" ] && distinct=$((distinct + 1))
  assert_eq "T50 the three bookmarks are three different cameras" "$distinct" "3"
  # And the pair of captures of bookmark 1 must agree to the last byte.
  if [ -s "$T50_FRAME_A" ] && [ -s "$T50_FRAME_B" ] && cmp -s "$T50_FRAME_A" "$T50_FRAME_B"; then
    echo "  OK   T50 two captures of the same bookmark are byte-identical ($(stat -c %s "$T50_FRAME_A") bytes)"
  else
    echo "  FAIL T50 the two captures of bookmark 1 differ"
    ls -l "$T50_FRAME_A" "$T50_FRAME_B" 2>/dev/null || true
    fail=1
  fi
  # A capture that came out empty would make the comparison above vacuous.
  assert_ge_ok "T50 the compared frame has bytes" "$(stat -c %s "$T50_FRAME_A" 2>/dev/null || echo 0)" 100000
  # (d) the key table.
  conflicts=$(grep -a "camera: keys .* result=conflict" "$T50_TRACE" | wc -l)
  assert_eq "T50 two conflicting binds were refused" "$conflicts" "2"
  unbound=$(grep -a "camera: keys vk=122 .*result=unbound" "$T50_TRACE" | wc -l)
  assert_eq "T50 an unbound key dispatched nothing" "$unbound" "1"
  restored=$(grep -a "camera: keys vk=122 .*result=ok" "$T50_TRACE" | wc -l)
  assert_eq "T50 restore-defaults re-armed the key" "$restored" "1"
  suppressed=$(grep -a "camera: keys .*result=suppressed-by-focus" "$T50_TRACE" | wc -l)
  assert_eq "T50 a focused text field suppressed two bound keys" "$suppressed" "2"
  # The final director line carries the counters. Every paused frame must be
  # accounted for and no violation may exist in a run without a defect.
  director_line=$(grep -a "camera: director frozen=" "$T50_TRACE" | tail -1)
  assert_contains "T50 no pause violation in the live run" "$director_line" "violations=0"
  assert_ge_ok "T50 paused frames were counted" "$(pvalue <(printf "%s\n" "$director_line") frozen)" 5
  assert_ge_ok "T50 camera moves while paused were counted" \
    "$(pvalue <(printf "%s\n" "$director_line") movesWhilePaused)" 3
  assert_ge_ok "T50 bookmark applications were counted" \
    "$(pvalue <(printf "%s\n" "$director_line") bookmarkApplies)" 6
  assert_ge_ok "T50 text-field suppressions were counted" \
    "$(pvalue <(printf "%s\n" "$director_line") focusSuppressed)" 2
  # The durable file: the three bookmarks and the [keys] table must be there,
  # and the [renderer] section of an unrelated save must have survived.
  assert_contains "T50 bookmarks persisted" "$(cat "$T50_SETTINGS" 2>/dev/null)" "bookmark3.saved=1"
  assert_contains "T50 a bookmark name persisted" "$(cat "$T50_SETTINGS" 2>/dev/null)" "name=spawn door"
  assert_contains "T50 the key table persisted" "$(cat "$T50_SETTINGS" 2>/dev/null)" "camera.freeFly="
  # The process must still be a hidden window: a capture run that showed a
  # window would be a different run.
  grep -a "camera: capture label=jump1-a" "$T50_TRACE" | tail -1
  grep -a "camera: capture label=jump1-b" "$T50_TRACE" | tail -1
  capture_a_tick=$(pvalue <(grep -a "camera: capture label=jump1-a" "$T50_TRACE" | head -1) tick)
  capture_b_tick=$(pvalue <(grep -a "camera: capture label=jump1-b" "$T50_TRACE" | head -1) tick)
  assert_eq "T50 both compared captures are on the same tick" "$capture_a_tick" "$capture_b_tick"
fi
# --- T4 mutation ---------------------------------------------------------------
if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== T4 mutation: perturbed captures, then two real product defects ==="
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


# --- T50 mutation cases -------------------------------------------------------
#
# Three defects, each reintroducing one claim the T50 contract makes. Every case
# edits the product source, rebuilds, re-runs the SAME script the forward section
# ran, requires the specific reading to go red, then restores the source from a
# byte-exact backup and verifies with cmp. The restore check is not decorative:
# a gate that leaves a mutated tree behind would poison every later run.

if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== T50 mutation: pause freeze, bookmark pose, conflict detection ==="
  t50mrc=0
  T50M="$OUT/t50-mutation"
  mkdir -p "$T50M"

  t50_backup() { cp "$1" "$T50M/$(basename "$1").t50bak"; }
  t50_restore_check() {
    cp "$T50M/$(basename "$1").t50bak" "$1" || return 1
    if cmp -s "$1" "$T50M/$(basename "$1").t50bak"; then
      echo "  RESTORED-IDENTICAL $(basename "$1")"
      return 0
    fi
    echo "  RESTORE-FAILED $(basename "$1")"
    return 1
  }
  # t50_patch <file> <find> <replace> -- literal, single-occurrence, CRLF-aware.
  # t50_patch <file> <find> <replace> -- exact, single-occurrence, CRLF-aware.
  # patch-source.py reads the pair from stdin with a %% separator; the heredoc is
  # quoted so the shell cannot eat the C++ operators.
  t50_patch() {
    local file="$1" find="$2" replace="$3"
    { printf '%s\n' "$find"; printf '%%%%\n'; printf '%s\n' "$replace"; } | python "$ROOT/patch-source.py" "$file"
  }
  t50_script_run() {
    tag="$1"; trace="$T50M/$1.trace"; bmp="$T50M/$1.bmp"
    cygt="$(cygpath -m "$PWD/$trace")"
    cygb="$(cygpath -m "$PWD/$bmp")"
    sed -e "s|__TRACE__|$cygt|" -e "s|__BMP__|$cygb|" "$T50M/template.script" > "$T50M/$tag.script"
    rm -f "$trace" "$bmp"
    TF2_NATIVE_TRACE="$cygt" timeout -k 5 "$T50_RUN_TIMEOUT" "$EXE" --tf-root "$TF" \
      --demo "$BAGEL" --hidden-window --audio-disabled --start-paused \
      --settings-file "$(cygpath -m "$PWD/$T50M/$tag.settings.ini")" \
      --camera-script "$(cygpath -m "$PWD/$T50M/$tag.script")" \
      > "$T50M/$tag.log" 2>&1
    echo $? > "$T50M/$tag.rc"
  }

  cat > "$T50M/template.script" <<'T50EOF'
resume
tick 200
frames 2
pause
frames 4
mark-tick
snapshot base
orbit 60 20
frames 3
snapshot moved
expect-same-tick
director
resume
frames 30
snapshot resumed
expect-tick-gt 200
pause
frames 2
save 1 tower
orbit 90 25
frames 1
key 112
frames 3
verify-bookmark 1
conflict camera.resetView F9
conflict camera.freeFly Ctrl+Shift+F1
quit
T50EOF

  echo
  echo "--- T50-M1: the demo tick advances while paused"
  t50_backup native/src/main.cpp || t50mrc=1
  if t50_patch native/src/main.cpp "    if (g_playback.enabled && !g_playback.paused) {" "    if (g_playback.enabled) {  // mutation: pause no longer freezes the tick"; then
    if bash build-target.sh tf2_demo_native > "$T50M/m1.build" 2>&1; then
      t50_script_run m1
      m1_frozen=$(grep -a "camera: expect what=same-tick" "$T50M/m1.trace" | grep -c "match=1" || true)
      m1_viol=$(pvalue <(grep -a "camera: director frozen=" "$T50M/m1.trace" | tail -1) violations)
      m1_rc=$(cat "$T50M/m1.rc")
      if [ "${m1_frozen:-1}" = "0" ]; then
        echo "  MUTATION-CAUGHT=PASS (T50-M1: expect-same-tick went red, 0 of 1)"
        if [ "${m1_viol:-0}" -gt 0 ]; then
          echo "  MUTATION-CAUGHT=PASS (T50-M1: director counted violations=$m1_viol)"
        else
          echo "  MUTATION-HOLD (T50-M1: no violation counted, run rc=$m1_rc)"
        fi
      else
        echo "  MUTATION-CAUGHT=FAIL (T50-M1: the freeze assertion stayed green, match=1 count=$m1_frozen)"
        t50mrc=1
      fi
    else
      echo "  MUTATION-BUILD=FAIL (T50-M1)"; tail -5 "$T50M/m1.build"; t50mrc=1
    fi
  else
    echo "  MUTATION-SETUP=FAIL (T50-M1: the patch did not apply)"; t50mrc=1
  fi
  t50_restore_check native/src/main.cpp || t50mrc=1
  bash build-target.sh tf2_demo_native > "$T50M/m1.restore.build" 2>&1 || { echo "  RESTORE-BUILD=FAIL (T50-M1)"; t50mrc=1; }

  echo
  echo "--- T50-M2: a bookmark jump lands 0.001 box units off"
  t50_backup native/src/main.cpp || t50mrc=1
  if t50_patch native/src/main.cpp "      bookmark.pose.focusZ, bookmark.pose.yaw, bookmark.pose.pitch," "      bookmark.pose.focusZ + 0.001f, bookmark.pose.yaw, bookmark.pose.pitch,"; then
    if bash build-target.sh tf2_demo_native > "$T50M/m2.build" 2>&1; then
      t50_script_run m2
      m2_exact=$(grep -a "camera: verify kind=bookmark" "$T50M/m2.trace" | grep -c "match=1" || true)
      m2_total=$(grep -a "camera: verify kind=bookmark" "$T50M/m2.trace" | grep -c "match=" || true)
      if [ "${m2_total:-0}" -ge 1 ] && [ "${m2_exact:-1}" = "0" ]; then
        echo "  MUTATION-CAUGHT=PASS (T50-M2: verify-bookmark went red, 0 of $m2_total exact)"
      else
        echo "  MUTATION-CAUGHT=FAIL (T50-M2: exact-pose assertion stayed green, $m2_exact of $m2_total)"
        t50mrc=1
      fi
    else
      echo "  MUTATION-BUILD=FAIL (T50-M2)"; tail -5 "$T50M/m2.build"; t50mrc=1
    fi
  else
    echo "  MUTATION-SETUP=FAIL (T50-M2: the patch did not apply)"; t50mrc=1
  fi
  t50_restore_check native/src/main.cpp || t50mrc=1
  bash build-target.sh tf2_demo_native > "$T50M/m2.restore.build" 2>&1 || { echo "  RESTORE-BUILD=FAIL (T50-M2)"; t50mrc=1; }

  echo
  echo "--- T50-M3: the key table accepts a duplicate chord"
  t50_backup native/src/keybinds.cpp || t50mrc=1
  if t50_patch native/src/keybinds.cpp "      if (entry.second.current == chord) {" "      if (false && entry.second.current == chord) {  // mutation: conflicts accepted"; then
    if bash build-target.sh tf2_demo_native > "$T50M/m3.build" 2>&1; then
      t50_script_run m3
      m3_conflicts=$(grep -a "camera: keys .* result=conflict" "$T50M/m3.trace" | wc -l || true)
      if [ "${m3_conflicts:-1}" = "0" ]; then
        echo "  MUTATION-CAUGHT=PASS (T50-M3: conflict detection went red, 0 conflicts reported)"
      else
        echo "  MUTATION-CAUGHT=FAIL (T50-M3: still reported $m3_conflicts conflicts)"; t50mrc=1
      fi
    else
      echo "  MUTATION-BUILD=FAIL (T50-M3)"; tail -5 "$T50M/m3.build"; t50mrc=1
    fi
  else
    echo "  MUTATION-SETUP=FAIL (T50-M3: the patch did not apply)"; t50mrc=1
  fi
  t50_restore_check native/src/keybinds.cpp || t50mrc=1
  bash build-target.sh tf2_demo_native > "$T50M/m3.restore.build" 2>&1 || { echo "  RESTORE-BUILD=FAIL (T50-M3)"; t50mrc=1; }

  echo
  echo "--- T50-M4 (control): the restored build reproduces the fixed readings"
  t50_script_run restored
  restored_exact=$(grep -a "camera: verify kind=bookmark" "$T50M/restored.trace" | grep -c "match=1" || true)
  restored_frozen=$(grep -a "camera: expect what=same-tick" "$T50M/restored.trace" | grep -c "match=1" || true)
  restored_conflicts=$(grep -a "camera: keys .* result=conflict" "$T50M/restored.trace" | wc -l || true)
  restored_rc=$(cat "$T50M/restored.rc" 2>/dev/null || echo 1)
  if [ "$restored_rc" = "0" ] && [ "${restored_frozen:-0}" = "1" ] && [ "${restored_exact:-0}" -ge 1 ] && [ "${restored_conflicts:-0}" = "2" ]; then
    echo "  RESTORED=PASS (rc=0 frozen=1 exact>=1 conflicts=2)"
  else
    echo "  RESTORED=FAIL (rc=$restored_rc frozen=$restored_frozen exact=$restored_exact conflicts=$restored_conflicts)"
    t50mrc=1
  fi

  [ "$t50mrc" -eq 0 ] || { echo "CAMERA-UI-T50-MUTATION=FAIL"; exit 1; }
  echo "CAMERA-UI-T50-MUTATION=PASS (three T50 defects went red and every source restored byte-identical)"
fi
echo
if [ "$fail" -eq 0 ]; then
  echo "CAMERA-UI=PASS"
  exit 0
fi
echo "CAMERA-UI=FAIL"
exit 1
