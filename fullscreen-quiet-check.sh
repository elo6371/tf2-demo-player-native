#!/usr/bin/env bash
# fullscreen-quiet-check.sh -- proves that the capture path does not take the
# desktop away from a fullscreen application, and that the proof is sensitive.
#
# WHY THIS IS A GATE AND NOT A PROMISE
# ------------------------------------
# quiet-capture-check.sh already proves the product window is hidden, using the
# program's own IsWindowVisible() reading. That answers "is the product window
# hidden". It does NOT answer the question an operator actually asks: "if I am in
# a fullscreen game on this machine, does running your chain take me out of it".
# Those are different questions. A window can be created hidden and still take the
# foreground for a frame while it initialises; a process can steal activation
# without ever showing a window of its own.
#
# SO THIS FILE MEASURES THE DESKTOP, NOT THE PRODUCT
# --------------------------------------------------
# desktop-guard.ps1 puts a borderless topmost fullscreen window on the primary
# monitor -- a stand-in for whatever the operator has up -- and records, for the
# whole time it is up: how many times a DIFFERENT process held the foreground,
# how many times the guard was deactivated, whether the display mode changed, the
# worst gap between two 20 ms samples, and (for a named process) how many 200 ms
# samples found a VISIBLE top-level window of that process on screen.
#
# THREE PHASES, BECAUSE ONE PHASE PROVES NOTHING
# ---------------------------------------------
#   baseline  the guard alone, nothing else running. Whatever it records here is
#             the desktop's own noise and must not be charged to the product.
#   forward   the guard plus a capture run with --hidden-window --audio-disabled.
#             The claim: no visible product window, no theft by name, no mode change.
#   control   the guard plus the same run WITH a shown window. This is the phase
#             that proves the instrument can see the product at all.
#
# WHY THE CONTROL PHASE DOES NOT ASSERT A STOLEN FOREGROUND
# --------------------------------------------------------
# An earlier version did, and every recorded run of it failed on that one line. The
# recorded runs turn out to have a dull explanation: their control phase never put a
# product window on screen at all (no control trace, no control frame), so the guard
# was watching a desktop the product was not on -- and "not caught" reads exactly
# like "quiet".
# It is NOT a Windows rule. A window shown by a separate process CAN take the
# foreground from this guard on this machine: measured with desktop-guard-target.ps1,
# SetForegroundWindow returned True, the guard recorded that process by name, and its
# deactivation counter moved by one. --instrument re-measures exactly that on every
# run, so the claim stays falsifiable instead of becoming folklore.
# The assertion is still left out of the product verdict, for a different reason:
# whether the PRODUCT's window activates is a product decision (a capture path is
# supposed to create its window without activating it), so a control that depends on
# it can fail for something that is not a defect. The product's appearance among the
# thieves is therefore PRINTED as a reading, and what is asserted instead is the pair
# that must hold for the instrument to be alive at all:
#   * the guard SAW a visible top-level window of the product (watchVisibleSamples>0);
#   * a foreground change was ATTRIBUTED TO THE PRODUCT BY NAME (handOffAttributed=1),
# and, in --instrument, a window from another process really took the foreground and
# was really charged to that process's name.
#
# A MISSING READING IS NOT A ZERO
# -------------------------------
# Every read here distinguishes "the field is absent" from "the field is 0". Reading
# an absent field as zero turned "the trace file was never written" into "the
# product says its window IS visible" -- an infrastructure failure dressed as a
# product defect. Everything that depends on a reading now fails loudly when the
# reading is missing, and says which file was missing it.
#
# WHAT THIS DOES NOT PROVE
# ------------------------
# The guard is a WinForms borderless window, not a Direct3D exclusive-mode swap
# chain. It proves the desktop, the display mode and the foreground are not taken.
# Whether an exclusive-fullscreen game keeps its flip queue is NOT VERIFIED here.
#
# USAGE
#   bash fullscreen-quiet-check.sh                 # all three phases
#   bash fullscreen-quiet-check.sh --instrument    # + prove the counter can fire
#   bash fullscreen-quiet-check.sh --mutation      # one targeted break per case
#   bash fullscreen-quiet-check.sh --phase forward # one phase, for iteration
set -o pipefail

MUTATION=0
INSTRUMENT=0
PHASE_ONLY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --mutation) MUTATION=1 ;;
    --instrument) INSTRUMENT=1 ;;
    --phase) shift; PHASE_ONLY="${1:-}" ;;
    *) echo "usage: $0 [--mutation] [--instrument] [--phase baseline|forward|control]" >&2; exit 2 ;;
  esac
  shift
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT" || exit 1
OUT="$ROOT/.scratch/fullscreen-quiet"
mkdir -p "$OUT"
EXE="$ROOT/native/build-nmake/tf2_demo_native.exe"
GUARD="$ROOT/desktop-guard.ps1"
MODE_PROBE="$ROOT/.scratch/t49/mode-probe.ps1"
# The product's real process name, DERIVED from the executable that is launched
# rather than typed a second time. (tf_win64, which kept appearing in guard
# readings, is an unrelated application on this desktop: it shows up in the
# BASELINE phase, where no product runs at all.)
PRODUCT_NAME="$(basename "$EXE" .exe)"
# The same inputs quiet-capture-check.sh uses, because a different demo would make
# the two gates measure different things.
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
PINNED_TICK=400
RUN_TIMEOUT=180
# 32 s, not 16. The product opens its archives for ~18 s before it creates and shows
# its window (vpk: archives opened=4 at 18540.6 ms, mode: window_created at 19506.5 ms
# of the control run), so a 16 s guard closes ~4 s BEFORE the product has a window at
# all. Its counters then read zero, which is indistinguishable from a product that
# never interfered -- the first full run of this gate reported exactly that. The
# coverage assertions below make this a checked precondition instead of a magic
# number: if startup ever slows past the margin, the gate fails instead of going green.
GUARD_SECONDS=${GUARD_SECONDS:-32}

fail=0
ok_count=0
# Set to 1 by the instrument phase when the foreground counter is shown to fire AND to
# charge the change to the instrument window by name. It is a property of the RUN, not of
# a phase: whoever proves attribution, the control phase does not have to prove it again.
ATTRIBUTION_PROVEN=0
# natural | fallback | none -- where this run's proof that the counter can fire and name
# a process came from. Printed in the summary so a reader never has to guess.
INSTRUMENT_SOURCE=none
assert_eq() { # assert_eq <label> <got> <want>
  if [ "$2" = "$3" ]; then
    echo "  OK   $1 = $2"; ok_count=$((ok_count + 1))
  else
    echo "  FAIL $1 = $2 (want $3)"; fail=$((fail + 1))
  fi
}
assert_le() { # assert_le <label> <got> <limit>
  if [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"; ok_count=$((ok_count + 1))
  else
    echo "  FAIL $1 = $2 (want <= $3)"; fail=$((fail + 1))
  fi
}
assert_gt() { # assert_gt <label> <got> <floor>
  if [ "$2" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"; ok_count=$((ok_count + 1))
  else
    echo "  FAIL $1 = $2 (want > $3)"; fail=$((fail + 1))
  fi
}

# One reader, two outcomes that are never confused: a value, or the word ABSENT.
# ABSENT means "the file or the field is not there"; it is never compared, never
# defaults to 0, and every caller that gets it fails the gate.
need_field() { # need_field <file> <key> -> value or ABSENT
  local v
  [ -f "$1" ] || { echo "ABSENT"; return; }
  v=$(grep -oE "$2=[^ ]*" "$1" 2>/dev/null | head -1 | cut -d= -f2)
  if [ -z "$v" ]; then echo "ABSENT"; else echo "$v"; fi
}
# For the one field whose EMPTY value is a real reading: guard.stolenBy is empty
# when nobody stole the foreground, which is the good outcome and must not be
# confused with a missing line.
has_key() { grep -qE "^$2=" "$1" 2>/dev/null; }

assert_field() { # assert_field <label> <file> <key> <want>
  local v
  v=$(need_field "$2" "$3")
  if [ "$v" = "ABSENT" ]; then
    echo "  FAIL $1 = ABSENT ($3 is not in $(basename "$2"); a missing reading is not a zero)"
    fail=$((fail + 1))
  elif [ "$v" = "$4" ]; then
    echo "  OK   $1 = $v"; ok_count=$((ok_count + 1))
  else
    echo "  FAIL $1 = $v (want $4)"; fail=$((fail + 1))
  fi
}
assert_field_gt() { # assert_field_gt <label> <file> <key> <floor>
  local v
  v=$(need_field "$2" "$3")
  if [ "$v" = "ABSENT" ]; then
    echo "  FAIL $1 = ABSENT ($3 is not in $(basename "$2"))"
    fail=$((fail + 1))
  elif [ "$v" -gt "$4" ] 2>/dev/null; then
    echo "  OK   $1 = $v (> $4)"; ok_count=$((ok_count + 1))
  else
    echo "  FAIL $1 = $v (want > $4)"; fail=$((fail + 1))
  fi
}

# The guard closes itself on its own timer, so a phase can never hang on it.
# Runs in the FOREGROUND of a subshell that the caller backgrounds. A PID obtained
# through command substitution is not a child of this shell and "wait" on it returns
# at once -- which reads the guard's file before it is written and reports every
# reading as missing. That is exactly what an earlier version of this gate did.
start_guard() { # start_guard <prefix> <seconds> [extra guard args...]
  local prefix="$1" secs="$2"; shift 2
  rm -f "$prefix.guard.txt"
  powershell -NoProfile -ExecutionPolicy Bypass -File "$GUARD" \
    -Seconds "$secs" -OutFile "$(cygpath -m "$prefix.guard.txt")" "$@" > "$prefix.guard.log" 2>&1
}

# One product run. All phases launch it identically, or the phases are not
# comparable and the comparison means nothing.
run_product() { # run_product <prefix> [extra args...]
  local prefix="$1"; shift
  # A capture run may never touch the audio device. Enforced here rather than
  # remembered at every call site, because the one call site that forgot it made
  # noise the operator could hear from another room.
  case " $* " in
    *" --audio-disabled "*) : ;;
    *) echo "  REFUSED: run_product called without --audio-disabled ($prefix)" >&2; printf '99\n'; return 99 ;;
  esac
  rm -f "$prefix.bmp" "$prefix.csv" "$prefix.trace" "$prefix.log"
  # The trace path is built from $prefix, which is ABSOLUTE. Prefixing $PWD to it
  # again produced /d/...//d/.../.trace, the product could not open the file, and
  # the whole trace was never written -- then the empty read was reported as "the
  # product says its window IS visible".
  local trace
  trace="$(cygpath -m "$prefix.trace")"
  # NOT --start-paused. A paused scheduler holds the cursor at tick 0, so
  # --capture-tick N is a tick this run will never arrive at and the phase dies on
  # its timeout.
  TF2_NATIVE_TRACE="$trace" timeout "$RUN_TIMEOUT" "$EXE" \
    --tf-root "$TF" --capture-tick "$PINNED_TICK" --demo "$BAGEL" \
    --capture-frame "$prefix.bmp" --metrics-file "$prefix.csv" "$@" > "$prefix.log" 2>&1
  echo "$?"
}

# The product's own IsWindowVisible answer lives in the TRACE ($TF2_NATIVE_TRACE),
# not on stdout: "mode: window_created=... window_visible=... " is a traceFmt line.
# The summary line is the last one, and it is the reading taken at the end of the
# run, so it is the one quoted. If the trace is not there at all this says so
# instead of returning an empty string that compares unequal to "0".
win_visible() { # win_visible <prefix> -> reading, or ABSENT
  local t="$1.trace" v
  [ -s "$t" ] || { echo "ABSENT"; return; }
  v=$(grep -oE "mode: summary .*window_visible=[0-9]+" "$t" 2>/dev/null | grep -oE "window_visible=[0-9]+" | tail -1 | cut -d= -f2)
  [ -n "$v" ] || v=$(grep -oE "window_visible=[0-9]+" "$t" 2>/dev/null | tail -1 | cut -d= -f2)
  if [ -z "$v" ]; then echo "ABSENT"; else echo "$v"; fi
}

win_created_ms() { # win_created_ms <prefix> -> ms from product start to window creation
  local t="$1.trace" v
  [ -s "$t" ] || { echo "ABSENT"; return; }
  v=$(grep -m1 "mode: window_created=" "$t" 2>/dev/null | awk '{print int($1)}')
  if [ -z "$v" ]; then echo "ABSENT"; else echo "$v"; fi
}

# The guard's observation interval must CONTAIN the moments this gate talks about. A
# guard that stopped before the product had a window reports the same clean zeroes as a
# product that never interfered -- that is what the 16 s version did, and nothing in its
# output said so. Covered, not assumed: start needs 2 s of lead, stop needs 2 s of tail
# and must be after the product's process is gone.
cover_window() { # cover_window <label> <guard-file> <prefix> <product-start-ms> <product-end-ms>
  local g_start g_stop win_at abs_at
  g_start=$(need_field "$2" "guard.startEpochMs")
  g_stop=$(need_field "$2" "guard.stopEpochMs")
  win_at=$(win_created_ms "$3")
  case "$g_start$g_stop$win_at" in
    *ABSENT*)
      echo "  FAIL $1: coverage cannot be shown -- guard.startEpochMs/stopEpochMs or the trace's window_created is missing"
      fail=$((fail + 1))
      return ;;
  esac
  abs_at=$(( $4 + win_at ))
  # The window instant is computed from two clocks: the shell's epoch at launch and the
  # product's own millisecond counter inside the trace. Between them sit process creation
  # and CRT startup -- tens of milliseconds warm, and no promise at all cold. So the offset
  # is PRINTED on every run, and bounded at 2000 ms for a stated reason: the quantity being
  # bounded is "fork + CRT init", a warm number in the tens of ms, and a wrong clock basis
  # shows up as a negative offset or one in the seconds (a different clock, not a slow
  # fork). 2 s is the line between "this machine was slow" and "these are not the same
  # clock"; it is checked on every run and prints its own number when it trips, rather
  # than being a number somebody picked.
  local trace_last wall skew
  trace_last=$(awk 'NF && $1 ~ /^[0-9]/ {v = int($1)} END {print v}' "$3.trace" 2>/dev/null)
  wall=$(( $5 - $4 ))
  if [ -n "${trace_last:-}" ]; then
    skew=$(( wall - trace_last ))
    echo "  $1: clock product_wall_ms=$wall trace_last_ms=$trace_last launch_skew_ms=$skew"
    if [ "$skew" -ge 0 ] 2>/dev/null && [ "$skew" -le 2000 ] 2>/dev/null; then
      echo "  OK   $1: the launch skew is small and positive ($skew ms), so the window instant is on the same clock"
      ok_count=$((ok_count + 1))
    else
      echo "  FAIL $1: launch skew $skew ms is outside [0, 2000] -- the two clocks are not telling the same story"
      fail=$((fail + 1))
    fi
  else
    echo "  FAIL $1: the trace has no timestamped lines, so the window instant cannot be placed on the clock"
    fail=$((fail + 1))
  fi
  assert_le "$1: the guard was watching 2 s before the product's window" "$(( g_start + 2000 ))" "$abs_at"
  assert_le "$1: the guard was still watching 2 s after the product's window" "$(( abs_at + 2000 ))" "$g_stop"
  assert_le "$1: the guard outlived the product's process" "$5" "$g_stop"
}

# "captured a frame" has to mean the frame is there. rc=0 says the process exited zero;
# it says nothing about a file, and the first version of this gate printed "the run still
# captured a frame = 0 (want 0)", an assertion no product behaviour could ever fail.
assert_artifacts() { # assert_artifacts <label> <prefix>
  local bytes rows
  bytes=$(stat -c %s "$2.bmp" 2>/dev/null || echo 0)
  rows=$(wc -l < "$2.csv" 2>/dev/null || echo 0)
  assert_gt "$1: the run wrote a capture frame (bytes)" "${bytes:-0}" "1000"
  assert_gt "$1: the run wrote metric rows" "${rows:-0}" "0"
}

# A person can stop the guard with ESC. That is a right, and it is recorded as its own
# outcome: not a pass (nothing was measured), not a failure (the product did nothing
# wrong). The operator's action must never be hidden inside a verdict.
guard_aborted() { # guard_aborted <guard-file> -- exits 3 when the operator stopped the run
  local a at
  a=$(need_field "$1" "guard.aborted")
  case "$a" in
    1)
      at=$(need_field "$1" "guard.abortedAtMs")
      echo
      echo "  ABORTED-BY-USER the operator pressed ESC \$at ms after the guard took the screen"
      echo "FULLSCREEN-QUIET=ABORTED (a person ended this run; neither a pass nor a failure)"
      exit 3 ;;
    ABSENT)
      echo "  FAIL $(basename "$1"): the guard file has no guard.aborted reading, so an operator abort could not be told apart from a completed run"
      fail=$((fail + 1)) ;;
  esac
}

# Before any trace-derived assertion, say WHY a missing trace is missing. A trace
# that was never opened and a product that never created a window are different
# failures and must not share a message.
trace_written() { # trace_written <prefix> -> 0 when the trace exists and got as far as a window
  local t="$1.trace"
  if [ ! -s "$t" ]; then
    echo "  FAIL $(basename "$1"): the trace was never written ($t missing or empty) -- every reading taken from it is unavailable, this is a harness failure and not a product verdict"
    fail=$((fail + 1))
    return 1
  fi
  if ! grep -q "mode: window_created=" "$t"; then
    echo "  FAIL $(basename "$1"): the trace has no 'mode: window_created=' line, so the run never got to a window"
    fail=$((fail + 1))
    return 1
  fi
  echo "  OK   $(basename "$1"): the trace was written and reports a window"
  ok_count=$((ok_count + 1))
  return 0
}

# Did the PRODUCT take the desktop, or did something else on this machine happen to
# grab a frame while the phase was running? The count cannot tell those apart: this
# desktop has unrelated applications that take the foreground on their own schedule,
# and a single blip during a 16 s phase would fail a count-based rule for a reason
# that has nothing to do with the program. The name can.
product_is_thief() { # product_is_thief <guard-file> -> 1 listed, 0 not listed, ABSENT unreadable
  # Two readings, each with one job. guard.stolenByCount says whether the guard recorded
  # anything at all -- it is a number, so "no list" and "empty list" stop looking alike.
  # The name list says WHO. An empty list is legal only when the count is 0; a count of
  # 1 with no names is an inconsistent file and is reported as unreadable, not as zero.
  local count list
  count=$(need_field "$1" "guard.stolenByCount")
  if [ "$count" = "ABSENT" ]; then echo "ABSENT"; return; fi
  if [ "$count" = "0" ]; then echo 0; return; fi
  list=$(need_field "$1" "guard.stolenBy")
  if [ "$list" = "ABSENT" ]; then echo "ABSENT"; return; fi
  case ",$list," in
    *",$PRODUCT_NAME,"*) echo 1 ;;
    *) echo 0 ;;
  esac
}

# The foreground state at an instant, from the guard's own timeline. This is the
# question a count cannot answer: "did the guard hold the foreground WHEN the product
# created its window", not "was it ever the foreground at some point in the phase".
# A guard that took the foreground at 1 s, lost it at 5 s and was in the background at
# 19.5 s reports everForeground=1 and would pass a naive reading of the same evidence.
fg_state_at() { # fg_state_at <guard-file> <absolute epoch ms> -> foreground|background|ABSENT
  local f="$1" at="$2" start tr rel state=0 e sign ms
  start=$(need_field "$f" "guard.startEpochMs")
  tr=$(need_field "$f" "guard.fgTransitions")
  if [ "$start" = "ABSENT" ] || [ "$tr" = "ABSENT" ]; then echo "ABSENT"; return; fi
  rel=$((at - start))
  IFS=',' read -r -a _ev <<< "$tr"
  for e in "${_ev[@]}"; do
    [ -z "$e" ] && continue
    sign="${e:0:1}"
    ms="${e:1}"
    case "$ms" in ''|*[!0-9]*) continue ;; esac
    [ "$ms" -gt "$rel" ] && break
    if [ "$sign" = "+" ]; then state=1; else state=0; fi
  done
  if [ "$state" -eq 1 ]; then echo foreground; else echo background; fi
}

# The display must be still before anything is measured across it. An earlier run
# caught modeBefore=1280x1024 against modeAfter=2560x1440 -- not the product's
# doing, the session was changing mode while the guard came up -- and a mode
# assertion taken across that window is worthless either way.
mode_stable() { # two EnumDisplaySettings samples one second apart must agree
  local a b
  [ -f "$MODE_PROBE" ] || { echo "  mode probe missing: $MODE_PROBE"; return 1; }
  a=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$MODE_PROBE" 2>/dev/null | tr -d '\r')
  sleep 1
  b=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$MODE_PROBE" 2>/dev/null | tr -d '\r')
  echo "  display first=${a:-unknown} second=${b:-unknown}"
  [ -n "$a" ] && [ "$a" = "$b" ]
}

want_phase() { [ -z "$PHASE_ONLY" ] || [ "$PHASE_ONLY" = "$1" ]; }

echo "product=$PRODUCT_NAME exe=$EXE"
echo "=== 0/3 the display must be still before anything is measured across it ==="
if mode_stable; then
  echo "  OK   the display mode was stable across a one second window"
  ok_count=$((ok_count + 1))
else
  echo "  REFUSED the display mode moved while this gate was starting; nothing measured here would mean anything"
  echo "FULLSCREEN-QUIET=REFUSED (unstable display)"; exit 3
fi

# The guard's foreground counter is the instrument this gate is built on, and a
# counter that never fires looks exactly like a quiet desktop. This step makes a
# window from a process the guard did NOT start take the foreground, and requires the
# guard to charge it to that process by name. Ten seconds of screen, once per run.
if [ "$INSTRUMENT" -eq 1 ]; then
  echo "=== 0b/4 instrument: a window from another process must take the foreground ==="
  TARGET_EXE="$OUT/instrument-target.exe"
  if [ ! -f "$TARGET_EXE" ]; then
    # A copy of the shell under a name of its own, so the process name the guard
    # reports cannot be confused with the guard's or the product's.
    cp "$(cygpath -u "$(powershell -NoProfile -Command 'Write-Output $PSHOME' | tr -d '\r')")/powershell.exe" "$TARGET_EXE"
  fi
  rm -f "$OUT/instrument.guard.txt" "$OUT/instrument.out"
  # The GUARD drives the hand-off, not the target. SetForegroundWindow is refused unless
  # the caller is already the foreground process, and a separate instrument process never
  # is; the earlier version had the target ask for itself and the outcome depended on who
  # owned the desktop at that instant -- measured True at 09:47 and False at 10:32 with the
  # foreground in another application's hands. The guard owns the foreground, so the guard
  # does the handing, and the phase no longer depends on the desktop being quiet.
  start_guard "$OUT/instrument" 14 -Watch "instrument-target" -HandOffMs 1500 -Phase "instrument" &
  gi=$!
  sleep 3
  T49_OUT="$(cygpath -m "$OUT/instrument.out")" "$TARGET_EXE" -NoProfile -ExecutionPolicy Bypass \
    -File "$ROOT/desktop-guard-target.ps1" >/dev/null 2>&1 &
  ti=$!
  wait "$gi" 2>/dev/null
  guard_aborted "$OUT/instrument.guard.txt"
  kill "$ti" 2>/dev/null
  i_set=$(sed -n 's/.*setForegroundWindow=\([A-Za-z]*\).*/\1/p' "$OUT/instrument.out" 2>/dev/null)
  i_by=$(need_field "$OUT/instrument.guard.txt" "guard.stolenBy")
  i_count=$(need_field "$OUT/instrument.guard.txt" "guard.stolenByCount")
  i_found=$(need_field "$OUT/instrument.guard.txt" "guard.watchWindowFound")
  i_ever=$(need_field "$OUT/instrument.guard.txt" "guard.everForeground")
  i_ok=$(need_field "$OUT/instrument.guard.txt" "guard.handOffOk")
  i_att=$(need_field "$OUT/instrument.guard.txt" "guard.handOffAttributed")
  i_tries=$(need_field "$OUT/instrument.guard.txt" "guard.handOffAttempts")
  i_self=$(need_field "$OUT/instrument.guard.txt" "guard.handOffSelfOk")
  i_attached=$(need_field "$OUT/instrument.guard.txt" "guard.attachedThreadsOutstanding")
  # The target's own attempt is a READING here: it is expected to be refused, because the
  # guard is holding the foreground on purpose. What the phase proves is the guard's half.
  echo "  instrument target.setForegroundWindow=${i_set:-ABSENT} (reading) guard.everForeground=$i_ever handOffSelfOk=$i_self handOffAttempts=$i_tries handOffOk=$i_ok handOffAttributed=$i_att stolenByCount=$i_count stolenBy=$i_by"
  # The instrument must not leave a thread attachment behind. AttachThreadInput is not
  # undone by the operating system: a guard that leaves one open shares its input queue
  # with another thread, stops receiving some messages, and can start delivering
  # keystrokes to a window nobody aimed at. That is the instrument changing the thing it
  # measures, and it is invisible in every other reading here.
  assert_eq "the guard left no input-queue attachment behind" "$i_attached" "0"
  # The ESC stop is the operator's only way out of a screen the guard is holding. If the
  # hotkey failed to register, that is a defect of the harness and has to be reported --
  # not discovered by a person pressing ESC and nothing happening.
  assert_field "the guard registered the ESC stop hotkey" "$OUT/instrument.guard.txt" "guard.hotkeyRegistered" "1"
  assert_eq "the guard held the foreground at least once" "$i_ever" "1"
  assert_eq "the guard found the instrument window by its process name" "$i_found" "1"
  # WHAT COUNTS AS EVIDENCE, and why it changed.
  #
  # The first version of this phase arranged for the instrument window to take the
  # foreground and required that to happen. That end depends on rules the harness does not
  # control (SetForegroundWindow is refused unless the caller already owns the foreground;
  # measured on this machine: 8 attempts, all refused, and another run where the call was
  # not even needed). Making the harness fight those rules led somewhere worse: a synthetic
  # ALT key press, i.e. injecting input into the operator's desktop to prove that nothing
  # injected input into the operator's desktop.
  #
  # So the primary evidence is no longer arranged: if the desktop itself changed the
  # foreground while the guard was watching, the guard must have recorded it AND resolved
  # it to a real process name. That is the same mechanism the product-facing verdict rests
  # on, exercised by events the machine produced rather than events this file produced.
  if [ "${i_count:-0}" -ge 1 ] 2>/dev/null; then
    echo "  OK   the foreground counter fired $i_count time(s) and named: $i_by"
    ok_count=$((ok_count + 1))
    # A name is only evidence if it is a name. The guard falls back to "pid-<n>" when the
    # process disappears between the read and the lookup; such an entry proves the counter
    # fired but not that attribution worked.
    case ",$i_by," in
      *,pid-*)
        echo "  FAIL one of the named processes could not be resolved to a name (list: $i_by)"
        echo "       a pid is not an attribution: the point of this phase is the name"
        fail=$((fail + 1)) ;;
      *)
        echo "  OK   every named thief is a resolved process name, not a pid fallback"
        ok_count=$((ok_count + 1))
        INSTRUMENT_SOURCE=natural ;;
    esac
    case "$i_by" in
      *instrument-target*)
        echo "  OK   the designated window is among them as well (the stronger arranged form)"
        ok_count=$((ok_count + 1)) ;;
      *)
        echo "  READING the designated window is not among them; this run's evidence is the"
        echo "          natural form (a change the machine made, which the guard named)" ;;
    esac
  else
    # A quiet desktop gives the counter nothing to name. The arranged hand-off is the
    # fallback, and it is a weaker source -- a change this harness caused, not one the
    # machine produced -- so it is labelled rather than passed off as the same thing.
    echo "  reading: stolenByCount=$i_count -- the desktop stole nothing during the phase"
    if [ "$i_ok" = "1" ] && [ "$i_att" = "1" ]; then
      echo "  INSTRUMENT-UNCONFIRMED the only attribution shown was arranged by this harness"
      echo "       (the guard handed the foreground to the instrument window in \$i_tries attempt(s));"
      echo "       a quiet desktop gives no natural foreground change to observe."
      INSTRUMENT_SOURCE=fallback
    else
      echo "  FAIL the desktop was quiet AND the arranged hand-off did not attribute either"
      echo "       handOffOk=$i_ok handOffAttributed=$i_att attempts=$i_tries selfForegroundRetaken=$i_self"
      echo "       this machine refuses to let a shell-started process take the foreground;"
      echo "       the expectation is labelled UNCONFIRMED, not met"
      fail=$((fail + 1))
    fi
  fi
  # The attribution chain is proven for this run, by whichever source above.
  ATTRIBUTION_PROVEN=1
  # A failed instrument ends the run here. Running three more phases with a broken
  # counter costs two minutes of the operator's screen and produces readings that only
  # look like evidence.
  if [ "$fail" -ne 0 ]; then
    echo "FULLSCREEN-QUIET=FAIL (instrument) -- the foreground counter could not be shown to fire and attribute by name, so no phase below would mean anything"
    exit 1
  fi
fi

if want_phase baseline; then
  echo "=== 1/3 baseline: the guard alone, nothing else running ==="
  start_guard "$OUT/baseline" "$GUARD_SECONDS" -Watch "$PRODUCT_NAME" -Phase "baseline" &
  gb=$!
  wait "$gb" 2>/dev/null
  guard_aborted "$OUT/baseline.guard.txt"
  b_deact=$(need_field "$OUT/baseline.guard.txt" "guard.deactivations")
  echo "  baseline deactivations=$b_deact stolenSamples=$(need_field "$OUT/baseline.guard.txt" "guard.foregroundStolenSamples")"
  echo "  baseline watchVisibleSamples=$(need_field "$OUT/baseline.guard.txt" "guard.watchVisibleSamples") modeChangedFields=$(need_field "$OUT/baseline.guard.txt" "guard.modeChangedFields")"
  echo "  mode before=$(need_field "$OUT/baseline.guard.txt" "guard.modeBefore") after=$(need_field "$OUT/baseline.guard.txt" "guard.modeAfter")"
  assert_field "baseline: the guard ran for the whole window" "$OUT/baseline.guard.txt" "guard.seconds" "$GUARD_SECONDS"
  assert_field "baseline changed no display field" "$OUT/baseline.guard.txt" "guard.modeChangedFields" "0"
  # The baseline is a READING, not a verdict: this desktop has other applications
  # that take the foreground on their own schedule and failing the gate for them
  # would be failing it for the machine, not the program. What must be true here is
  # that the product was not running in it -- and that is also the evidence that the
  # name in stolenBy (tf_win64 on this machine) belongs to somebody else.
  echo "  baseline stolenByCount=$(need_field "$OUT/baseline.guard.txt" "guard.stolenByCount") stolenBy=$(need_field "$OUT/baseline.guard.txt" "guard.stolenBy")"
  assert_eq "the baseline did not contain the product" "$(product_is_thief "$OUT/baseline.guard.txt")" "0"
  assert_field "the baseline never showed a product window" "$OUT/baseline.guard.txt" "guard.watchVisibleSamples" "0"
fi

if want_phase forward; then
  echo "=== 2/3 forward: the same run with --hidden-window --audio-disabled ==="
  start_guard "$OUT/forward" "$GUARD_SECONDS" -Watch "$PRODUCT_NAME" -Phase "forward" &
  gf=$!
  sleep 1
  f_start=$(date +%s%3N)
  fwd_rc=$(run_product "$OUT/forward" --hidden-window --audio-disabled)
  f_end=$(date +%s%3N)
  wait "$gf" 2>/dev/null
  guard_aborted "$OUT/forward.guard.txt"
  f_deact=$(need_field "$OUT/forward.guard.txt" "guard.deactivations")
  echo "  forward rc=$fwd_rc deactivations=$f_deact stolenSamples=$(need_field "$OUT/forward.guard.txt" "guard.foregroundStolenSamples") modeChangedFields=$(need_field "$OUT/forward.guard.txt" "guard.modeChangedFields")"
  echo "  forward watchVisibleSamples=$(need_field "$OUT/forward.guard.txt" "guard.watchVisibleSamples") window_visible=$(win_visible "$OUT/forward")"
  echo "  stolenByCount=$(need_field "$OUT/forward.guard.txt" "guard.stolenByCount") stolenBy=$(need_field "$OUT/forward.guard.txt" "guard.stolenBy")"
  assert_field "forward: the guard ran for the whole window" "$OUT/forward.guard.txt" "guard.seconds" "$GUARD_SECONDS"
  assert_field "forward: the guard registered the ESC stop hotkey" "$OUT/forward.guard.txt" "guard.hotkeyRegistered" "1"
  assert_eq "the hidden run exited cleanly" "$fwd_rc" "0"
  assert_artifacts "hidden run" "$OUT/forward"
  if trace_written "$OUT/forward"; then
    assert_eq "the product says its own window is not visible" "$(win_visible "$OUT/forward")" "0"
  fi
  cover_window "forward" "$OUT/forward.guard.txt" "$OUT/forward" "$f_start" "$f_end"
  # WHEN the guard held the foreground, not merely WHETHER it ever did.
  #
  # everyForeground=1 is satisfied by a guard that was activated at 1 s, lost the
  # desktop at 5 s and was in the background when the product created its window at
  # 19.5 s. The phase's claim is "the product did not take the foreground from a
  # fullscreen window that was in the foreground", and that claim is only about the
  # instant of the event. So two readings are required:
  #   1. the guard got the foreground at least 2 s BEFORE the product's window existed
  #      (it cannot have been activated by the event it is supposed to witness);
  #   2. it still held it AT that instant, evaluated from the timeline.
  f_startup_ms=$(need_field "$OUT/forward.guard.txt" "guard.startupForegroundMs")
  f_start_ms=$(need_field "$OUT/forward.guard.txt" "guard.startEpochMs")
  f_win_ms=$(win_created_ms "$OUT/forward")
  f_tl=$(need_field "$OUT/forward.guard.txt" "guard.fgTransitions")
  echo "  forward foreground timeline: ${f_tl:-ABSENT}"
  if [ "$f_startup_ms" = "ABSENT" ] || [ "$f_start_ms" = "ABSENT" ] || [ "$f_win_ms" = "ABSENT" ]; then
    echo "  FAIL the instant-coverage comparison is missing a reading (startupForegroundMs=$f_startup_ms startEpochMs=$f_start_ms window_created=$f_win_ms)"
    fail=$((fail + 1))
  else
    abs_window=$((f_start + f_win_ms))
    g_fg_abs=$((f_start_ms + f_startup_ms))
    win_rel=$((abs_window - f_start_ms))
    echo "  clock guard_foreground_at=+${f_startup_ms}ms window_at=+${win_rel}ms gap=$((abs_window - g_fg_abs))ms"
    assert_le "the guard held the foreground at least 2 s before the product created its window" "$((g_fg_abs + 2000))" "$abs_window"
    fg_state=$(fg_state_at "$OUT/forward.guard.txt" "$abs_window")
    assert_eq "the guard was the foreground at the instant the product created its window" "$fg_state" "foreground"
    if [ "$fg_state" != "foreground" ]; then
      echo "       timeline says the guard was not in the foreground at +${win_rel}ms, so 'the product did not take the foreground' would be a statement about a background window"
    fi
  fi
  # The claim that matters, in the two forms this machine can measure: the product's
  # process never held the foreground, and no visible window of it was ever on
  # screen during the phase.
  assert_eq "the hidden run never took the foreground as the product" "$(product_is_thief "$OUT/forward.guard.txt")" "0"
  assert_field "no product window was visible during the hidden run" "$OUT/forward.guard.txt" "guard.watchVisibleSamples" "0"
  assert_field "the hidden run changed no display field" "$OUT/forward.guard.txt" "guard.modeChangedFields" "0"
  # Ambient noise is the baseline's business. The forward phase may not add to it.
  #
  # But Deactivate can only fire if the guard was ever activated. If either phase's
  # guard never held the foreground, the comparison is vacuous -- a window that was never
  # active cannot be deactivated -- and a vacuous comparison printed as OK is the defect
  # this whole task is about. So the comparison is made only when both sides can carry it,
  # and says why when it cannot.
  f_ever=$(need_field "$OUT/forward.guard.txt" "guard.everForeground")
  b_ever=$(need_field "$OUT/baseline.guard.txt" "guard.everForeground")
  if [ "${b_deact:-}" = "" ]; then
    echo "  NOT COMPARED the baseline phase did not run, so there is no deactivation reference for this phase"
  elif [ "$f_ever" = "0" ] || [ "$b_ever" = "0" ]; then
    echo "  NOT COMPARED the guard never held the foreground in one of these phases (forward everForeground=$f_ever, baseline=$b_ever),"
    echo "               so a Deactivate event could not have fired and deactivations=$f_deact carries no information"
  else
    assert_le "the hidden run did not deactivate the guard more than the baseline" "$f_deact" "$b_deact"
  fi
fi

if want_phase keyprobe; then
  echo "=== 2b/4 key dispatch: H must reach the HUD, F9 must still reach the camera ==="
  # Why this phase exists: when T51's HUD dispatch and T50's camera bindings were merged,
  # the dispatch was "any action the table returns -> handle it and return", which swallowed
  # every camera key: F9 printed "hud: keys dispatch action=camera.freeFly" and did nothing.
  # The reading that looked like dispatch was dispatch to nobody. So the product is asked
  # to do both things at once, and BOTH answers are required.
  #
  # No guard, no visible window: this phase is about the product's own message handler, not
  # about the desktop, so nothing is taken from the operator. The keys are POSTED to the
  # product's hidden window (see evidence/t49/key-dispatch-probe.ps1 for why posted messages
  # and not synthesized input).
  rm -f "$OUT/keyprobe.probe.txt" "$OUT/keyprobe.rc" "$OUT/keyprobe.probe.log"
  run_product "$OUT/keyprobe" --hidden-window --audio-disabled > "$OUT/keyprobe.rc" &
  kpid=$!
  powershell -NoProfile -ExecutionPolicy Bypass -File "$ROOT/evidence/t49/key-dispatch-probe.ps1" \
    -ProcessName "$PRODUCT_NAME" -Out "$(cygpath -m "$OUT/keyprobe.probe.txt")" > "$OUT/keyprobe.probe.log" 2>&1 &
  kppid=$!
  wait "$kpid"; k_wait_rc=$?
  wait "$kppid" 2>/dev/null
  k_rc=$(tr -d '\r' < "$OUT/keyprobe.rc" 2>/dev/null | tail -1)
  k_found=$(need_field "$OUT/keyprobe.probe.txt" "keyprobe.found")
  k_h=$(need_field "$OUT/keyprobe.probe.txt" "keyprobe.hSent")
  k_f9=$(need_field "$OUT/keyprobe.probe.txt" "keyprobe.f9Sent")
  echo "  keyprobe rc=$k_rc probe_wait_rc=$k_wait_rc found=$k_found hSent=$k_h f9Sent=$k_f9"
  assert_eq "the key probe found the hidden product window" "$k_found" "1"
  assert_eq "the key probe posted the H key" "$k_h" "1"
  assert_eq "the key probe posted the F9 key" "$k_f9" "1"
  if trace_written "$OUT/keyprobe"; then
    k_dispatch=$(grep -c 'hud: keys dispatch' "$OUT/keyprobe.trace" 2>/dev/null || echo 0)
    k_toggle=$(grep -c 'hud: keys dispatch action=hud.toggle' "$OUT/keyprobe.trace" 2>/dev/null || echo 0)
    k_stolen=$(grep 'hud: keys dispatch' "$OUT/keyprobe.trace" 2>/dev/null | grep -vc 'action=hud\.' || true)
    k_enabled=$(grep -oE 'hud: settings enabled=[01]' "$OUT/keyprobe.trace" 2>/dev/null | tail -1 | cut -d= -f2)
    k_table=$(grep 'keys: table ' "$OUT/keyprobe.trace" 2>/dev/null | tail -1)
    k_last=$(printf '%s' "$k_table" | grep -oE 'last=[A-Za-z0-9._-]+' | tail -1)
    echo "  keyprobe dispatch_lines=$k_dispatch hud.toggle=$k_toggle non-hud_dispatch_lines=$k_stolen hud_enabled_after=${k_enabled:-ABSENT}"
    echo "  keyprobe keys_table: ${k_table:-ABSENT}"
    # 1. H reached the HUD and the state changed.
    assert_gt "the H key dispatched hud.toggle" "${k_toggle:-0}" "0"
    assert_eq "pressing H turned the HUD on" "${k_enabled:-ABSENT}" "1"
    # 2. Nothing that is not hud.* was dispatched as a hud action -- the merge defect in
    #    one line: before the fix F9 produced "hud: keys dispatch action=camera.freeFly".
    assert_eq "no camera action was dispatched as a HUD action" "${k_stolen:-0}" "0"
    # 3. F9 still reached the camera table. Its own dispatch prints a different line; the
    #    table summary says which action fired last, and the probe posts F9 last.
    assert_eq "F9 still reached the camera bindings" "${k_last:-ABSENT}" "last=camera.freeFly"
  fi
fi

if want_phase control; then
  echo "=== 3/3 control: the same run with a SHOWN window, which the guard must see ==="
  start_guard "$OUT/control" "$GUARD_SECONDS" -Watch "$PRODUCT_NAME" -Phase "control" &
  gc=$!
  sleep 1
  # --audio-disabled on the CONTROL phase too. It was omitted once, and the product
  # opened the default playback device and played the demo's audio at system volume
  # for the whole phase; the operator heard it from another room. The control
  # differs from the forward phase in exactly one dimension (is the window shown), so
  # an audio flag difference silently turns one run into a public-address test.
  c_start=$(date +%s%3N)
  ctl_rc=$(run_product "$OUT/control" --audio-disabled)
  c_end=$(date +%s%3N)
  wait "$gc" 2>/dev/null
  guard_aborted "$OUT/control.guard.txt"
  c_deact=$(need_field "$OUT/control.guard.txt" "guard.deactivations")
  echo "  control rc=$ctl_rc watchVisibleSamples=$(need_field "$OUT/control.guard.txt" "guard.watchVisibleSamples") watchWindowFound=$(need_field "$OUT/control.guard.txt" "guard.watchWindowFound") handOffSent=$(need_field "$OUT/control.guard.txt" "guard.handOffSent") handOffAttributed=$(need_field "$OUT/control.guard.txt" "guard.handOffAttributed") deactivations=$c_deact"
  echo "  control window_visible=$(win_visible "$OUT/control") stolenByCount=$(need_field "$OUT/control.guard.txt" "guard.stolenByCount") stolenBy=$(need_field "$OUT/control.guard.txt" "guard.stolenBy")"
  assert_field "control: the guard ran for the whole window" "$OUT/control.guard.txt" "guard.seconds" "$GUARD_SECONDS"
  assert_eq "the shown run exited cleanly" "$ctl_rc" "0"
  assert_artifacts "shown run" "$OUT/control"
  if trace_written "$OUT/control"; then
    assert_eq "the product says its own window IS visible" "$(win_visible "$OUT/control")" "1"
  fi
  cover_window "control" "$OUT/control.guard.txt" "$OUT/control" "$c_start" "$c_end"
  # Instrument sensitivity. Without these the forward reading would be "quiet" on a
  # machine where the measurement never worked, and the two readings are identical.
  assert_field_gt "the guard saw a visible product window" "$OUT/control.guard.txt" "guard.watchVisibleSamples" "0"
  # The name is derived from the exe that was launched. Finding the window BY THAT
  # NAME is the only evidence that the derivation is right: a visible-window count
  # alone would also be satisfied by watching the wrong process.
  assert_field "the guard found a window of the derived product name" "$OUT/control.guard.txt" "guard.watchWindowFound" "1"
  # The product's window is not expected to ask for the foreground (a capture path
  # creates its window without activating it), so the guard offers it: take the
  # foreground, then hand it to the product window, and see whether the guard charges
  # that change to the product by name.
  assert_field "the guard offered the foreground to the product window" "$OUT/control.guard.txt" "guard.handOffSent" "1"
  # Attribution has to be proven ONCE per run, by whichever phase can prove it. The
  # instrument phase proves it directly, on a quiet desktop, by a process the guard did
  # not start. Only when that phase did not run does this reading have to carry the
  # proof -- and it is a weaker one: Windows refuses a hand-off from a process that is
  # not foreground, and this machine stole the guard's foreground 1327 times in 42 s
  # while it waited for the window. Requiring it unconditionally would fail the gate for
  # the desktop's behaviour, which is exactly what the baseline exists to keep out of the
  # verdict.
  # Now that the guard drives the hand-off (and forcibly takes the foreground before
  # giving it away), this reading is reachable even on a desktop that keeps stealing it,
  # so it is required again rather than downgraded to a print.
  echo "  control hand-off attempts=$(need_field "$OUT/control.guard.txt" "guard.handOffAttempts") ok=$(need_field "$OUT/control.guard.txt" "guard.handOffOk") attributed=$(need_field "$OUT/control.guard.txt" "guard.handOffAttributed")"
  assert_field "the guard handed the foreground to the product window" "$OUT/control.guard.txt" "guard.handOffOk" "1"
  assert_field "the foreground change was attributed to the product by name" "$OUT/control.guard.txt" "guard.handOffAttributed" "1"
  # A READING, not an assertion: whether the product's own window activates is the
  # product's decision. It is printed because on the day it is 1, the question "can a
  # shown product window take this desktop" gets an answer from the product itself,
  # and on the day it is 0 the next reader knows this control did not put a window
  # that activates on screen.
  echo "  control: the product appeared among the thieves: $(product_is_thief "$OUT/control.guard.txt")"
fi

# No product process may survive the run: a leftover one would keep holding the
# window it created and make the next phase's reading not the one it claims to be.
leftover=$(powershell -NoProfile -Command "if (Get-Process -Name '$PRODUCT_NAME' -ErrorAction SilentlyContinue) { 'yes' } else { 'no' }" | tr -d '\r')
echo "  product processes left after the run: $leftover"
assert_eq "no product process survived the run" "$leftover" "no"

echo
echo "fullscreen-quiet: assertions_ok=$ok_count failures=$fail attribution_proven=$ATTRIBUTION_PROVEN instrument_source=$INSTRUMENT_SOURCE guard_seconds=$GUARD_SECONDS"
if [ "$fail" -eq 0 ]; then echo "FULLSCREEN-QUIET=PASS"; else echo "FULLSCREEN-QUIET=FAIL"; fi
[ -n "$PHASE_ONLY" ] && exit "$fail"
[ "$fail" -eq 0 ] || exit 1

if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== mutation: the readings that could make this gate vacuous ==="
  mfail=0

  # M1: run the forward phase with a SHOWN window. If a shown window does not raise
  # the visible-window reading, the forward phase's zero means nothing.
  # Classes of mutation, kept apart on purpose. A gate-config mutation changes an
  # argument; a harness-source mutation edits the instrument and must be restored byte
  # for byte; a product-source mutation would edit the program under test. Adding them
  # into one count would let "N mutations all red" mean less than it sounds.
  echo "-- M1 [gate-config]: the forward phase with the window shown"
  start_guard "$OUT/m1" "$GUARD_SECONDS" -Watch "$PRODUCT_NAME" -Phase "m1-shown" &
  gm=$!
  sleep 1
  m1_rc=$(run_product "$OUT/m1" --audio-disabled)
  wait "$gm" 2>/dev/null
  guard_aborted "$OUT/m1.guard.txt"
  assert_artifacts "m1 shown run" "$OUT/m1"
  m1_watch=$(need_field "$OUT/m1.guard.txt" "guard.watchVisibleSamples")
  echo "   m1 watchVisibleSamples=$m1_watch rc=$m1_rc"
  if [ "${m1_watch:-0}" -gt 0 ] 2>/dev/null; then
    echo "  OK   M1 caught: a shown window is seen, so the forward phase's zero is a real zero"
  else
    echo "  FAIL M1 survived: a shown window left watchVisibleSamples at $m1_watch"; mfail=$((mfail + 1))
  fi

  # M2: make the guard blind to who holds the foreground. The control phase's by-name
  # attribution must then go red, which is what proves that assertion is load bearing.
  echo "-- M2 [harness-source]: blind the guard's foreground counter"
  cp "$GUARD" "$OUT/guard.orig.ps1"
  sed -i 's/if (\$fgPid -ne \$script:myPid) {/if (\$false) {/' "$GUARD"
  if cmp -s "$GUARD" "$OUT/guard.orig.ps1"; then
    echo "  FAIL M2 could not be applied: the line it needs is not there"
    mfail=$((mfail + 1))
  else
    start_guard "$OUT/m2" "$GUARD_SECONDS" -Watch "$PRODUCT_NAME" -Phase "m2-blind" &
    gb2=$!
    sleep 1
    m2_rc=$(run_product "$OUT/m2" --audio-disabled)
    wait "$gb2" 2>/dev/null
    guard_aborted "$OUT/m2.guard.txt"
    cp "$OUT/guard.orig.ps1" "$GUARD"
    if cmp -s "$GUARD" "$OUT/guard.orig.ps1"; then
      echo "  OK   the guard was restored byte for byte"
    else
      echo "  FAIL the guard was NOT restored"; mfail=$((mfail + 1))
    fi
    m2_count=$(need_field "$OUT/m2.guard.txt" "guard.stolenByCount")
    m2_attr=$(need_field "$OUT/m2.guard.txt" "guard.handOffAttributed")
    echo "   m2 stolenByCount=$m2_count handOffAttributed=$m2_attr rc=$m2_rc"
    assert_eq "M2 the blind guard reports zero thieves" "${m2_count:-ABSENT}" "0"
    if [ "${m2_attr:-1}" = "0" ]; then
      echo "  OK   M2 detected: with a blind guard the by-name attribution fails, so the control phase cannot pass"
    else
      echo "  FAIL M2 survived: the control phase still looked satisfied"; mfail=$((mfail + 1))
    fi
  fi

  # M3: the same phase with the guard length this gate used to have. The product's
  # window appears after ~19.5 s, so a 16 s guard has to report a clean zero -- which is
  # exactly the failure the gate shipped with. Reproduced on demand, so the 32 s is a
  # measured requirement and not a number somebody liked.
  # M3 changes no file at all: it passes -Seconds 16 where the gate passes 32. There is
  # nothing to restore, and the log says so rather than leaving a reader to guess.
  echo "-- M3 [gate-config, no file touched]: the old 16 s guard, which cannot outlive the product's startup"
  start_guard "$OUT/m3" 16 -Watch "$PRODUCT_NAME" -Phase "m3-short" &
  gm3=$!
  sleep 1
  m3_rc=$(run_product "$OUT/m3" --audio-disabled)
  wait "$gm3" 2>/dev/null
  guard_aborted "$OUT/m3.guard.txt"
  assert_artifacts "m3 short run" "$OUT/m3"
  m3_watch=$(need_field "$OUT/m3.guard.txt" "guard.watchVisibleSamples")
  m3_win=$(win_created_ms "$OUT/m3")
  echo "   m3 guard=16s window_created=${m3_win}ms watchVisibleSamples=$m3_watch rc=$m3_rc"
  if [ "${m3_watch:-1}" = "0" ] && [ "${m3_win:-ABSENT}" != "ABSENT" ] && [ "${m3_win:-0}" -gt 16000 ] 2>/dev/null; then
    echo "  OK   M3 caught: the window appears at ${m3_win}ms, after a 16 s guard has stopped, and the reading is a clean zero"
  else
    echo "  FAIL M3 did not reproduce the old blindness (watch=$m3_watch window_created=$m3_win)"
    mfail=$((mfail + 1))
  fi

  # The counts are printed by class so the line cannot be read as "three product
  # mutations": this gate never edits the product, M2 edits the instrument and restores
  # it, M1 and M3 only change arguments.
  if [ "$mfail" -eq 0 ]; then
    echo "FULLSCREEN-QUIET-MUTATION=PASS product_source=0 harness_source=1(restored byte-for-byte, cmp) gate_config=2(files untouched)"
  else
    echo "FULLSCREEN-QUIET-MUTATION=FAIL product_source=0 harness_source=1 gate_config=2"
    exit 1
  fi
fi
