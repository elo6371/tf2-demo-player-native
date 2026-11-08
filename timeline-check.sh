#!/usr/bin/env bash
# timeline-check.sh -- the playback clock is an instrument: seeking lands on the
# tick asked for, the same tick is the same picture at any speed, pause freezes
# and resume does not repay wall time, and an out-of-range seek clamps instead of
# guessing.
#
# WHY THIS EXISTS
#   T85 measured those four behaviours once by hand (D:/_shared/t85-timeline/,
#   31 runs) and left them as a report. A report is not a gate: nothing in
#   verify-all.sh or verify-release.sh would have gone red if any of the four had
#   regressed. This script turns the four readings into assertions that a future
#   change has to keep true, and it is one of the few places in the chain that asks
#   "does the running product's clock land where it says it lands".
#
# WHAT IS PINNED, AND HOW
#   1. --capture-tick N must yield a frame drawn at tick N, not at the first
#      drawn tick at or after N. The product holds the cursor at N for the draw
#      (native/src/main.cpp:9005-9019, T27) and reports the tick it actually drew
#      in the same trace line that arms the capture (native/src/main.cpp:11271).
#      The assertion reads that line, so it fails if either half regresses.
#      Without the hold the run overshoots at 32x (a drawn frame spans several
#      ticks), which is exactly the historical T26 defect.
#   2. The frame drawn at tick 600 is byte-identical at 1x, 8x and 32x. This is
#      the strongest clause: it says the picture is a function of the tick, not
#      of how many frames it took to get there or of wall time. The frame is
#      compared payload-byte-for-payload-byte (BMP header offset 10), the same
#      comparison frame-capture-check.sh uses.
#      NOTE: this clause measures DETERMINISM, not speed mapping. A product that
#      played 8x at 4x would still draw tick 600 identically; T85 proved the
#      frame is a pure function of the pinned tick. What catches the mapping is
#      the "the run really used the requested speed" assertion below, which reads
#      the speed the ledger reports (native/src/main.cpp:1179). The mutation
#      suite carries both cases for that reason.
#   3. pause -> tick frozen (exact, not "wobbled by one"); resume -> the cursor
#      moves on and never by more wall time than has passed since the resume. The
#      sampling is a CONDITION, not a frame number: the draw cadence is wall clock
#      (~8.3 ms target) while a demo tick is ~15 ms at 1x, so the state read on the
#      first frame after the resume can legitimately still show the pause tick. The
#      script therefore asks for one state per frame (20 samples) and the gate
#      waits for the first sample that moved; no sample moving inside the cap is a
#      red, printed with the sample count. The upper bound is also wall-clock
#      based (delta <= elapsed * tickRate + 2 ticks), not a fixed 2: one loaded
#      frame can cover several ticks, and the clause that matters is that the
#      paused seconds are not repaid (that would jump by tens or hundreds).
#   4. seek -1 lands on 0 and seek 999999 lands on the demo's own last tick, read
#      back from the same request line the product writes for every transport
#      request (native/src/playback_state.cpp:15-24) and the state line
#      (native/src/main.cpp:1179). Nothing is clamped against a hard-coded end.
#
# --capture-tick IS NOT A SEEK (read this before reading section 1)
#   --capture-tick does not move the transport. The run plays forward from wherever
#   it is, and once the cursor has passed N the product holds the cursor back at N
#   for exactly the frame it draws (native/src/main.cpp:9005-9019, the T27 comment
#   says it outright: "the frame drawn AT tick N, not the first frame at or after
#   N"). The trace line that arms the capture reports the tick actually drawn
#   (main.cpp:11271). A real seek is a different path: JumpTicks through the
#   transport request (main.cpp:1156-1165; applied in
#   native/src/playback_state.cpp:13-31). Section 1 therefore asserts the pin, not
#   seek landing, and must never be cited as "seek was tested" -- the phrase reads
#   like a seek because it names a tick. Section 4 is where the transport's own
#   seek clamping is read. This distinction is carried as its own debt entry
#   (D-27, proposed to the debt list) so the misreading has a place to be found.

# WHAT THIS GATE DOES NOT COVER (deliberate)
#   * The animation phase reset after a BACKWARD seek is not asserted. That is a
#     deliberate T61 trade-off, not a defect: a rewound demo clears the per-player
#     clocks so they cannot integrate across the seek (native/src/main.cpp:9339-9346;
#     debt entry D-26). T85 measured its user-visible cost (same tick, XOR=1,952,690,
#     reproducible); a gate that reddened on it would be asking for the opposite of
#     the recorded decision.
#   * Reverse playback and Stop are not driven: the product has no scripted
#     producer for either (Stop: PlaybackRequestKind::Stop has no producer outside
#     the probe; reverse only through the keyboard/UI), and this gate does not
#     inject input. T85 recorded both as unmeasured, not as passing.
#   * Frame content correctness at the tick (materials, lighting, animation pose)
#     belongs to the other gates; this one asserts when the frame was taken and
#     that the same tick gives the same bytes, never what the frame should show.
#
# DETERMINISM OF THE MEASUREMENT
#   Every launch is isolated: --settings-file/--ui-settings-file point at a fresh
#   mktemp copy, so the product's exit-time settings write (main.cpp:11613-11629,
#   ui.ini at native_ui.cpp:137-139) lands in a directory that is deleted on the
#   way out and never in \%APPDATA\% (D-22). A run that wrote the user's real
#   configuration would make the next run's starting state depend on the last one.
#   In this sandbox a product launch must also disable audio and hide the window;
#   both flags are mandatory here.
#
# Usage: bash timeline-check.sh
#   bash timeline-check.sh --dry-run   print the launch plan only; starts nothing
#   TIMELINE_SECTIONS=1,2,3,4   run only the named sections (mutations use this)
#   TIMELINE_OUT=...            output directory (default evidence/t85-timeline/gate)
#   TIMELINE_EXE=...            product executable (timeline-check-selftest.sh substitutes a mock)
#   TIMELINE_TF=...             tf root (a mock only needs the path to exist)
#   TIMELINE_DEMO=...           demo (a mock only needs the path to exist)
# Exit: 0 = every assertion held; 1 = one did not; 2 = missing input.
#
# Run it under the host lock, e.g.
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t85-timeline bash timeline-check.sh

set -uo pipefail
cd "$(dirname "$0")"

EXE="${TIMELINE_EXE:-native/build-nmake/tf2_demo_native.exe}"
TF="${TIMELINE_TF:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}"
# cp_snakewater_final1 is installed on this machine, so the captures are of a real
# world (the koth_bagel_rc13 map is not installed; its demo draws the fallback
# quad, which is fine for "different scene" checks but useless for timing).
SNAKE="${TIMELINE_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}"
OUT="${TIMELINE_OUT:-evidence/t85-timeline/gate}"
SECTIONS="${TIMELINE_SECTIONS:-1,2,3,4}"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
AUDIO_FLAGS=(--audio-disabled --hidden-window)

mkdir -p "$OUT"

# Dry run: print the launch plan and stop. This exists so the gate can be
# reviewed and wired into a chain before the exe exists, and so a CI preflight can
# check the flags without spending a GPU. It starts no product, writes no frame
# and prints no PASS.
if [ "${1:-}" = "--dry-run" ]; then
  echo "--- timeline-check dry run: 8 product launches, nothing started ---"
  for T in 12000 20001 24001; do
    echo "  seek-$T   capture timeout=180s  --playback-speed 32 --capture-tick $T"
  done
  for S in 1 8 32; do
    echo "  speed-$S  capture timeout=180s  --playback-speed $S --player-animation --capture-tick 600 --camera-script scripts/timeline-speed.script"
  done
  echo "  pause-resume script timeout=120s  --playback-speed 1 --camera-script scripts/timeline-pause.script"
  echo "  clamp     script timeout=120s  --start-paused --camera-script scripts/timeline-clamp.script"
  echo "  every launch: --tf-root $TF --audio-disabled --hidden-window --settings-file <mktemp>/<name>/settings.ini --ui-settings-file <mktemp>/<name>/ui.ini (deleted on exit)"
  echo "  trace: TF2_NATIVE_TRACE=<out>/<name>/trace.txt"
  echo "TIMELINE-CHECK=DRY runs=8"
  exit 0
fi

# Settings isolation (D-22): one throwaway directory for the whole run, one trap.
# The trap is the only one in this script; adding a second trap later would
# silently replace this cleanup.
TMPROOT=$(mktemp -d)
cleanup_tmproot() { [ -n "${TMPROOT:-}" ] && rm -rf "$TMPROOT"; }
trap cleanup_tmproot EXIT INT TERM

for path in "$EXE" "$TF" "$SNAKE"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; exit 2; }

section_on() { case ",$SECTIONS," in *",$1,"*) return 0 ;; esac; return 1; }

fail=0
failures=0
count=0
ok() { count=$((count + 1)); echo "  OK   $1"; }
bad() { count=$((count + 1)); failures=$((failures + 1)); fail=1; echo "  FAIL $1"; }
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then ok "$1 = $3"; else bad "$1 = ${2:-<none>}, expected $3"; fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ "${2:--1}" -ge "$3" ] 2>/dev/null; then ok "$1"; else bad "$1 (actual ${2:-<none>}, need >= $3)"; fi
}
assert_le() { # assert_le <label> <actual> <maximum>
  if [ "${2:--1}" -le "$3" ] 2>/dev/null; then ok "$1"; else bad "$1 (actual ${2:-<none>}, need <= $3)"; fi
}

# Field readers. The trace formats are the ones the product prints; a changed
# format must redden the assertion rather than silently read an empty string,
# which is why every reader falls back to the literal <missing>.
field() { # field <line> <name>  -> value of name=... in line
  local line="$1" name="$2" rest
  rest="${line#*"$name"=}"
  if [ "$rest" = "$line" ]; then printf '<missing>'; return; fi
  printf '%s' "${rest%% *}"
}
last_line_with() { # last_line_with <file> <needle>
  grep "$2" "$1" 2>/dev/null | tail -1
}

sha256_of() { "$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$1"; }

# One isolated product launch. <name> <timeout-seconds> <capture|script> <extra args...>
#   capture: adds --capture-frame/--metrics-file inside the run directory
#   script : the camera script is responsible for terminating the run
run_product() {
  local name="$1" limit="$2" mode="$3"; shift 3
  local dir="$OUT/$name"
  mkdir -p "$dir"
  rm -f "$dir/frame.bmp" "$dir/metrics.csv" "$dir/trace.txt" "$dir/stdout.txt"
  local capture_args=()
  if [ "$mode" = "capture" ]; then
    capture_args=(--capture-frame "$dir/frame.bmp" --metrics-file "$dir/metrics.csv")
  fi
  # Settings live in the throwaway tree, not in evidence/: the product writes them
  # unconditionally on exit and they are not evidence (D-22). One directory per
  # launch so a reused case name cannot inherit the previous run's recent-demo MRU.
  mkdir -p "$TMPROOT/$name"
  TF2_NATIVE_TRACE="$dir/trace.txt" timeout "$limit" "$EXE" \
    --tf-root "$TF" "${AUDIO_FLAGS[@]}" \
    --settings-file "$TMPROOT/$name/settings.ini" \
    --ui-settings-file "$TMPROOT/$name/ui.ini" \
    "$@" "${capture_args[@]}" > "$dir/stdout.txt" 2>&1
  printf '%s' "$?"
}

# BMP payload comparison: the file header's own offset says where the pixels
# begin, so a header change cannot smuggle bytes into the comparison.
bmp_payload_diff() { # bmp_payload_diff <a.bmp> <b.bmp> -> "diff <n> <total>" | length-mismatch | reason
  "$PY" - "$1" "$2" <<'PYEOF'
import struct, sys
def payload(path):
    data = open(path, 'rb').read()
    if len(data) < 54 or data[0:2] != b'BM':
        raise SystemExit('not-a-bmp:' + path)
    off, = struct.unpack_from('<I', data, 10)
    return data[off:]
try:
    a = payload(sys.argv[1]); b = payload(sys.argv[2])
except OSError as error:
    raise SystemExit('read-error:%s' % error)
if len(a) != len(b):
    print('length-mismatch %d %d' % (len(a), len(b))); raise SystemExit(0)
n = len(a)
x = int.from_bytes(a, 'little') ^ int.from_bytes(b, 'little')
print('diff %d %d' % (n - x.to_bytes(n, 'little').count(0), n))
PYEOF
}

assert_zero_diff() { # assert_zero_diff <label> <readout>
  local label="$1" readout="$2" how one two
  read -r how one two <<< "$readout"
  case "$how" in
    diff)
      if [ "$one" = "0" ]; then ok "$label: payload bytes identical (0/$two)"; else bad "$label: $one/$two payload bytes differ"; fi ;;
    length-mismatch)
      bad "$label: payload sizes differ ($one vs $two)" ;;
    *)
      bad "$label: no comparison was possible ($readout)" ;;
  esac
}

echo "=== timeline-check: playback clock instrument ==="
echo "exe     = $EXE"
echo "sha256  = $(sha256_of "$EXE")"
echo "demo    = $SNAKE"
echo "demo sha256 = $(sha256_of "$SNAKE")"
echo "git     = $(git rev-parse HEAD 2>/dev/null || echo '<no-git>') $(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
echo "sections= $SECTIONS"
echo "out     = $OUT"
{
  echo "exe=$EXE"
  echo "exe_sha256=$(sha256_of "$EXE")"
  echo "demo=$SNAKE"
  echo "demo_sha256=$(sha256_of "$SNAKE")"
  echo "git_head=$(git rev-parse HEAD 2>/dev/null || echo '<no-git>')"
  echo "git_branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
  echo "sections=$SECTIONS"
} > "$OUT/identity.txt"

if section_on 1; then
  echo
  # This is the PIN, not a seek: the run plays forward from tick 0 and the product
  # holds the cursor at N for the drawn frame (main.cpp:9005-9019). See the
  # "--capture-tick IS NOT A SEEK" note at the top of this file before citing it.
  echo "=== 1/4 --capture-tick lands on the requested tick, exactly ==="
  for T in 12000 20001 24001; do
    name="seek-$T"
    rc=$(run_product "$name" 180 capture --demo "$SNAKE" --playback-speed 32 --capture-tick "$T")
    assert_eq "seek $T: product exit code" "$rc" "0"
    line=$(last_line_with "$OUT/$name/trace.txt" 'capture: armed')
    req=$(field "$line" requested)
    ft=$(field "$line" frame_tick)
    assert_eq "seek $T: capture armed for the requested tick" "$req" "$T"
    assert_eq "seek $T: captured frame drawn at the requested tick" "$ft" "$T"
  done
fi

if section_on 2; then
  echo
  echo "=== 2/4 the same tick is the same picture at 1x, 8x and 32x ==="
  for S in 1 8 32; do
    name="speed-$S"
    rc=$(run_product "$name" 180 capture --demo "$SNAKE" --playback-speed "$S" \
      --player-animation --capture-tick 600 --camera-script scripts/timeline-speed.script)
    assert_eq "speed ${S}x: product exit code" "$rc" "0"
    line=$(last_line_with "$OUT/$name/trace.txt" 'capture: armed')
    ft=$(field "$line" frame_tick)
    assert_eq "speed ${S}x: capture lands on tick 600" "$ft" "600"
    state=$(last_line_with "$OUT/$name/trace.txt" 'playback: state')
    sp=$(field "$state" speed)
    assert_eq "speed ${S}x: the run really used the requested speed" "$sp" "$(printf '%.6f' "$S")"
  done
  for pair in "1 8" "1 32" "8 32"; do
    set -- $pair
    readout=$(bmp_payload_diff "$OUT/speed-$1/frame.bmp" "$OUT/speed-$2/frame.bmp" 2>&1)
    assert_zero_diff "same tick 600 at ${1}x vs ${2}x" "$readout"
  done
fi

if section_on 3; then
  echo
  echo "=== 3/4 pause freezes the cursor; resume advances without repaying wall time ==="
  name="pause-resume"
  rc=$(run_product "$name" 120 script --demo "$SNAKE" --playback-speed 1 --camera-script scripts/timeline-pause.script)
  assert_eq "pause/resume: product exit code" "$rc" "0"
  line=$(last_line_with "$OUT/$name/trace.txt" 'expect what=same-tick')
  marked=$(field "$line" marked)
  actual=$(field "$line" actual)
  match=$(field "$line" match)
  assert_eq "pause freezes the cursor: expect-same-tick match" "$match" "1"
  assert_eq "pause freeze read the marked tick" "$actual" "$marked"
  trace="$OUT/$name/trace.txt"
  resume_line=$(grep -n 'cmd=resume' "$trace" 2>/dev/null | tail -1 | cut -d: -f1)
  if [ -z "${resume_line:-}" ]; then
    bad "resume advances the cursor past the pause tick (no resume step in the trace)"
  else
    resume_ts=$(sed -n "${resume_line}p" "$trace" | awk '{print $1}')
    # Poll the ledger, do not guess a frame: the product prints one state line per
    # playback-state step (one per frame), and the draw cadence is wall clock while
    # a tick is rate-locked. The script emits 20 samples; the first one whose
    # cursor moved settles the question, and none moving is the red.
    states=()
    while IFS= read -r sample; do
      states+=("$sample")
    done < <(awk -v first="$resume_line" 'NR > first && /playback: state/' "$trace")
    advanced=""
    advanced_index=0
    index=0
    for sample in "${states[@]}"; do
      index=$((index + 1))
      sample_cursor=$(field "$sample" cursor)
      if [ "${sample_cursor:-<missing>}" != "<missing>" ] && [ "$sample_cursor" -gt "$marked" ] 2>/dev/null; then
        advanced="$sample"
        advanced_index=$index
        break
      fi
    done
    if [ -z "$advanced" ]; then
      bad "resume advances the cursor past the pause tick (no state sample moved past $marked in ${#states[@]} sample(s); cap 20)"
    else
      cursor=$(field "$advanced" cursor)
      delta=$((cursor - marked))
      state_ts=$(printf '%s' "$advanced" | awk '{print $1}')
      rate=$(field "$advanced" rate)
      case "$rate" in ''|'<missing>') rate=66.666669 ;; esac
      # traceNowMs() stamps the ledger in MILLISECONDS (native/src/main_trace.cpp:40,
      # "%.1f %s"), so convert to seconds before multiplying by ticks/second. Treating
      # the ms delta as seconds would inflate the bound ~1000x and let a replay through.
      allowed=$(awk -v a="$resume_ts" -v b="$state_ts" -v r="$rate" 'BEGIN { n = int(((b - a) / 1000.0) * r) + 2; if (n < 2) n = 2; print n }')
      assert_ge "resume advances the cursor past the pause tick (delta=$delta after sample $advanced_index/${#states[@]})" "$delta" 1
      assert_le "resume does not replay paused wall time (delta=$delta, wall time allowed $allowed ticks)" "$delta" "$allowed"
    fi
  fi
fi

if section_on 4; then
  echo
  echo "=== 4/4 out-of-range seeks clamp to the demo's own ends ==="
  name="clamp"
  rc=$(run_product "$name" 120 script --demo "$SNAKE" --start-paused --camera-script scripts/timeline-clamp.script)
  assert_eq "clamp: product exit code" "$rc" "0"
  line=$(grep 'playback: request' "$OUT/$name/trace.txt" 2>/dev/null | grep 'requestedTick=-1 ' | tail -1)
  applied=$(field "$line" appliedTick)
  assert_eq "seek -1 clamps to tick 0" "$applied" "0"
  line=$(grep 'playback: request' "$OUT/$name/trace.txt" 2>/dev/null | grep 'requestedTick=999999' | tail -1)
  applied=$(field "$line" appliedTick)
  state=$(last_line_with "$OUT/$name/trace.txt" 'playback: state')
  end=$(field "$state" end)
  assert_eq "seek 999999 clamps to the demo's last tick" "$applied" "$end"
fi

echo
if [ "$count" -lt 4 ]; then
  bad "only $count assertions ran; a timeline gate with fewer than 4 cannot pass"
fi
if [ "$fail" -eq 0 ]; then
  echo "TIMELINE-CHECK=PASS assertions=$count"
  exit 0
fi
echo "TIMELINE-CHECK=FAIL assertions=$count failures=$failures"
exit 1
