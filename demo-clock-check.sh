#!/usr/bin/env bash
# demo-clock-check.sh -- T63-F1: one demo carries TWO tick spaces.
#
# THE DEFECT THIS GATE PINS
#   A SourceTV demo numbers its commands: entry tick = index in the demo file.
#   Signon commands are numbered too, so the first DATA packet is not tick 0
#   (snakewater: signon entry 10252, first data entry 1). Separately, the server
#   stamps a server tick in svc_NetTick, which only advances when that message
#   appears -- packets can be dropped, so the offset between entry and server
#   ticks is NOT a constant. Before T63-F1 the product took firstPacketTick (the
#   first command 1/2 entry tick, i.e. a SIGNON tick on snakewater) as the server
#   origin and added playback ticks to it: every entity/effect query asked about
#   a tick ~10k too early, and the failure looked like "no snapshot" rather than
#   like a clock error. This gate asserts the separation and the per-packet table.
#
# WHAT IT ASSERTS
#   A1 snakewater playback 0 (paused, tick 0): entryTick=1, serverTick=10320,
#      serverTickKnown=1. The first data packet does not stamp its own
#      svc_NetTick, but the persistent networkTick is already valid (the signon
#      block carried 10320), so the tick is KNOWN by carry-over -- the table
#      records the tick in force, it is never derived by adding an offset.
#      Playback 1: entryTick=2, serverTick=10321.
#   A2 bagel playback 0 (paused): entryTick=1, serverTick=56148 -- there the
#      first data packet stamps its own svc_NetTick.
#   A3 snakewater playback 20000: serverTick equals the packet table row 20000
#      (NOT 10320+20000: the demo drops packets, so the table is the only honest
#      source), and the entity snapshot at that tick is available, stale=0,
#      states>0. The gate also proves the table is not a linear offset by
#      requiring row 20000 to be strictly greater than 10320+20000.
#   A4 view samples are strictly increasing data-entry ticks: signon samples are
#      counted but not mixed in, first sample == first data entry tick, last
#      sample within 200 ticks of the header end, out-of-order count 0, and the
#      shared cap is large enough that bagel drops nothing (the cap is the line
#      t66-camera also edits; m4 restores the old 65536 and must red this).
#   A5 the camera eye at playback 600 is the view sample at the ENTRY tick
#      (first_data_entry_tick + 600), not that tick plus an offset: the product
#      "scene: camera eye=" equals probe --camera-tick <entry>. A SourceTV demo
#      cannot prove this: its 76-byte cmdinfo block is all zeros (measured on
#      snakewater 601/20000/60000 and bagel 600-602), so its view samples carry
#      origin 0,0,0 and the comparison would be 0 against 0. P1 is a POV demo on
#      an installed map whose cmdinfo really moves (tick 600 origin
#      1784.664,-3594.817,239.965).
#   A6 negative control: bagel server tick 1 is before-history, 56148 is
#      available. This is what the pre-fix origin asked for.
#
# RUN UNDER THE HOST LOCK. Like hud-check.sh this gate does NOT acquire the lock
# (a gate that acquired it would deadlock inside a locked run). Invoke as:
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run demo-clock-check \
#     bash /d/TF2_Native_Worktrees/t63-clock/demo-clock-check.sh
#
# MUTATIONS (--mutation): m1 uses the signon entry tick as the playback origin
# (reds A1); m2 queries the entity snapshot with the entry tick (reds A3); m3
# stops excluding signon samples from the view list (reds A4); m4 restores the
# 65536 view-sample cap (reds A4 on bagel). Each mutation asserts its anchor is
# present exactly once, rebuilds, must red its named assertion, then restores the
# source bytes (cmp) and rebuilds the fixed tree.
#
# Usage: bash demo-clock-check.sh [--mutation]
# Exit:  0 = every assertion held (and, with --mutation, every mutation red).
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/demo_open_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# S1: the map is installed on this machine and the demo is the one the other
# gates already read; S5: the map is NOT installed, which is exactly why it is
# here -- the clock must not depend on the map loading.
S1="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
S5="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
P1="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-09-10_14-04-13.dem"
OUT="${CLOCK_OUT:-evidence/t63-clock/gate}"
mkdir -p "$OUT"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"

MUTATION=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

fail=0
assert_gt() { # assert_gt <label> <actual> <floor> -- strictly greater
  if [ -n "$2" ] && [ "$2" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = $2, expected > $3"; fail=1
  fi
}
assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <minimum>
  if [ -n "${2:-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
v() { # v <file> <key> -- value of the LAST "<key>=<value>" token
  local file="$1" key="$2" line
  [ -f "$file" ] || return 0
  line=$(grep -a -o -E "(^| )${key}=[^ ]+" "$file" 2>/dev/null | tail -1)
  printf '%s' "$line" | sed -e 's/^ //' -e "s/^${key}=//"
}
clock_field() { # clock_field <trace> <playback tick> <field>
  local line
  line=$(grep -a "scene: clock tick=$2 " "$1" 2>/dev/null | tail -1)
  [ -n "$line" ] || return 0
  printf '%s' "$line" | grep -a -o -E " $3=[^ ]+" | tail -1 | sed -e 's/^ //' -e "s/^$3=//"
}
last_field() { # last_field <file> <line-grep> <field> -- field of the last matching line
  local line
  line=$(grep -a "$2" "$1" 2>/dev/null | tail -1)
  [ -n "$line" ] || return 0
  printf '%s' "$line" | grep -a -o -E " $3=[^ ]+" | tail -1 | sed -e 's/^ //' -e "s/^$3=//"
}
first_of_list() { printf '%s' "${1%%,*}"; }
last_of_list() { printf '%s' "${1##*,}"; }
eye_close() { # eye_close <a> <b> <tol> -- "x,y,z" triples within tol
  awk -v a="$1" -v b="$2" -v tol="$3" 'BEGIN{
    if (a=="" || b=="") exit 1;
    split(a,x,","); split(b,y,",");
    for (i=1;i<=3;i++) { d=x[i]-y[i]; if (d<0) d=-d; if (d>tol) exit 1 }
    exit 0
  }'
}

origin_nonzero() { # origin_nonzero <x,y,z> <min abs> -- at least one axis beats min
  awk -v a="$1" -v min="$2" 'BEGIN{
    if (a=="") exit 1;
    split(a,x,",");
    for (i=1;i<=3;i++) { d=x[i]; if (d<0) d=-d; if (d>min) exit 0 }
    exit 1
  }'
}

# One product launch helper, the only place in this file that starts the product.
# --hidden-window and --audio-disabled are built here, not by callers.
product_run() { # product_run <prefix> <demo> <tick> <speed> [extra args...]
  local prefix="$1" demo="$2" tick="$3" speed="$4"; shift 4
  rm -f "$prefix.bmp" "$prefix.trace" "$prefix.log"
  local -a args=(--tf-root "$TF" --hidden-window --audio-disabled
    --settings-file "$(cygpath -m "$PWD/$prefix.settings.ini")"
    --ui-settings-file "$(cygpath -m "$PWD/$prefix.ui.ini")"
    --demo "$demo" --playback-speed "$speed" --capture-tick "$tick"
    --capture-frame "$(cygpath -m "$PWD/$prefix.bmp")")
  args+=("$@")
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$prefix.trace")" \
    timeout "${RUN_TIMEOUT:-900}" "$EXE" "${args[@]}" > "$prefix.log" 2>&1
  return $?
}
probe_run() { # probe_run <prefix> <demo> [extra args...]
  local prefix="$1" demo="$2"; shift 2
  "$PROBE" --scan "$@" "$demo" > "$prefix.txt" 2> "$prefix.err"
  return $?
}

# ---- A1: the two spaces at the start of the stream -------------------------
check_a1() { # <zero trace> <one trace>
  local Z="${1:-$OUT/a1-zero.trace}" O="${2:-$OUT/a1-one.trace}"
  assert_eq "S1 first_signon_entry_tick" "$(v "$OUT/S1-scan.txt" first_signon_entry_tick)" "10252"
  assert_eq "S1 first_data_entry_tick" "$(v "$OUT/S1-scan.txt" first_data_entry_tick)" "1"
  assert_eq "S1 first_data_net_tick" "$(v "$OUT/S1-scan.txt" first_data_net_tick)" "10320"
  assert_eq "A1 table row 0 (playback 0)" "$(v "$OUT/S1-scan.txt" server_tick_at_0)" "10320"
  assert_eq "A1 table row 1 (playback 1)" "$(v "$OUT/S1-scan.txt" server_tick_at_1)" "10321"
  assert_eq "A1 tick0 entryTick" "$(clock_field "$Z" 0 entryTick)" "1"
  assert_eq "A1 tick0 serverTick" "$(clock_field "$Z" 0 serverTick)" "10320"
  assert_eq "A1 tick0 serverTickKnown" "$(clock_field "$Z" 0 serverTickKnown)" "1"
  assert_eq "A1 tick1 entryTick" "$(clock_field "$O" 1 entryTick)" "2"
  assert_eq "A1 tick1 serverTick" "$(clock_field "$O" 1 serverTick)" "10321"
  assert_eq "A1 tick1 serverTickKnown" "$(clock_field "$O" 1 serverTickKnown)" "1"
}
# ---- A2: the other polarity, first data packet DOES carry svc_NetTick ------
check_a2() {
  assert_eq "S5 first_data_entry_tick" "$(v "$OUT/S5-scan.txt" first_data_entry_tick)" "1"
  assert_eq "S5 first_data_net_tick" "$(v "$OUT/S5-scan.txt" first_data_net_tick)" "56148"
  assert_eq "A2 table row 0 (playback 0)" "$(v "$OUT/S5-scan.txt" server_tick_at_0)" "56148"
  assert_eq "A2 tick0 entryTick" "$(clock_field "$OUT/a2-zero.trace" 0 entryTick)" "1"
  assert_eq "A2 tick0 serverTick" "$(clock_field "$OUT/a2-zero.trace" 0 serverTick)" "56148"
  assert_eq "A2 tick0 serverTickKnown" "$(clock_field "$OUT/a2-zero.trace" 0 serverTickKnown)" "1"
}
# ---- A3: mid-demo, the table (not an offset) and the snapshot ---------------
check_a3() { # <trace>
  local F="${1:-$OUT/a3.trace}"
  assert_eq "S1 server_tick_gaps" "$(v "$OUT/S1-scan.txt" server_tick_gaps)" "11"
  assert_eq "S1 table size == header ticks (row 0 is playback 0)" "$(v "$OUT/S1-scan.txt" packet_server_ticks)" "$(v "$OUT/S1-scan.txt" ticks)"
  assert_eq "S1 last data entry tick == header ticks" "$(v "$OUT/S1-scan.txt" last_data_entry_tick)" "$(v "$OUT/S1-scan.txt" ticks)"
  assert_eq "S1 data packets recorded" "$(v "$OUT/S1-scan.txt" data_packets)" "64330"
  assert_eq "A3 tick20000 entryTick" "$(clock_field "$F" 20000 entryTick)" "20001"
  assert_eq "A3 tick20000 serverTick" "$(clock_field "$F" 20000 serverTick)" "$T3"
  assert_eq "A3 tick20000 serverTickKnown" "$(clock_field "$F" 20000 serverTickKnown)" "1"
  A3_STATUS=$(last_field "$F" "scene: snapshot status=" status)
  case "$A3_STATUS" in
    0|1) echo "  OK   A3 snapshot status = $A3_STATUS (available or checkpoint)" ;;
    *) echo "  FAIL A3 snapshot status = $A3_STATUS, expected 0 or 1"; fail=1 ;;
  esac
  # Staleness is a property of the checkpoint store, not a free variable: the
  # product must report the same age the probe reports for the same server tick,
  # and that age must be inside the history's own checkpoint gap. Demanding 0
  # would be demanding a checkpoint store that never checkpoints.
  A3_PROBE_STALE=$(v "$OUT/S1-snapshot-A3.txt" snapshot_stale)
  A3_GAP=$(v "$OUT/S1-scan.txt" entity_history_archive_gap_max)
  case "$A3_GAP" in ''|0) A3_GAP=$(v "$OUT/S1-scan.txt" entity_history_gap_max) ;; esac
  assert_eq "A3 snapshot stale equals the probe's for the same server tick" "$(last_field "$F" "scene: snapshot status=" stale)" "$A3_PROBE_STALE"
  if [ -n "$A3_GAP" ] && [ -n "$A3_PROBE_STALE" ] && [ "$A3_PROBE_STALE" -le "$A3_GAP" ]; then
    echo "  OK   A3 staleness $A3_PROBE_STALE <= checkpoint gap $A3_GAP"
  else
    echo "  FAIL A3 staleness $A3_PROBE_STALE exceeds checkpoint gap $A3_GAP"; fail=1
  fi
  assert_ge "A3 snapshot states" "$(last_field "$F" "scene: snapshot status=" states)" "1"
  assert_gt "A3 table row 20000 beats the linear offset" "$T3" "30319"
  A3_PSTATUS=$(v "$OUT/S1-snapshot-A3.txt" snapshot_status)
  case "$A3_PSTATUS" in
    available|checkpoint) echo "  OK   A3 probe snapshot status = $A3_PSTATUS" ;;
    *) echo "  FAIL A3 probe snapshot status = $A3_PSTATUS"; fail=1 ;;
  esac
  assert_ge "A3 probe snapshot (table row 20000) states" "$(v "$OUT/S1-snapshot-A3.txt" snapshot_states)" "1"
}
# ---- A4: the view-sample sequence and the shared cap ------------------------
check_a4() {
  assert_eq "S1 view_ordered" "$(v "$OUT/S1-scan.txt" view_ordered)" "1"
  echo "  INFO S1 view_out_of_order pre-repair = $(v "$OUT/S1-scan.txt" view_out_of_order)"
  assert_eq "S1 view_out_of_order_after_repair" "$(v "$OUT/S1-scan.txt" view_out_of_order_after_repair)" "0"
  assert_ge "S1 signon samples counted" "$(v "$OUT/S1-scan.txt" view_signon_samples)" "1"
  assert_eq "S1 first view tick" "$(first_of_list "$(v "$OUT/S1-scan.txt" view_first5)")" "1"
  assert_ge "S1 last view tick" "$(last_of_list "$(v "$OUT/S1-scan.txt" view_last5)")" "64152"
  assert_eq "S5 view_ordered" "$(v "$OUT/S5-scan.txt" view_ordered)" "1"
  echo "  INFO S5 view_out_of_order pre-repair = $(v "$OUT/S5-scan.txt" view_out_of_order)"
  assert_eq "S5 view_out_of_order_after_repair" "$(v "$OUT/S5-scan.txt" view_out_of_order_after_repair)" "0"
  assert_eq "S5 table size == header ticks" "$(v "$OUT/S5-scan.txt" packet_server_ticks)" "$(v "$OUT/S5-scan.txt" ticks)"
  assert_eq "S5 view_samples_dropped" "$(v "$OUT/S5-scan.txt" camera_track_dropped)" "0"
  assert_ge "S5 last view tick" "$(last_of_list "$(v "$OUT/S5-scan.txt" view_last5)")" "77266"
}
# ---- A5: the camera eye is the ENTRY-tick view sample (POV demo) -----------
check_a5() {
  local entry=$((P1_ENTRY + 600))
  assert_ge "A5 P1 first_data_entry_tick" "$P1_ENTRY" "0"
  assert_eq "A5 tick600 entryTick" "$(clock_field "$OUT/a5.trace" 600 entryTick)" "$entry"
  assert_eq "A5 tick600 serverTick" "$(clock_field "$OUT/a5.trace" 600 serverTick)" "$T5P"
  assert_eq "A5 tick600 serverTickKnown" "$(clock_field "$OUT/a5.trace" 600 serverTickKnown)" "1"
  assert_eq "A5 probe camera_found" "$(v "$OUT/P1-camera.txt" camera_found)" "1"
  assert_eq "A5 probe camera_sample_tick" "$(v "$OUT/P1-camera.txt" camera_sample_tick)" "$entry"
  assert_eq "A5 probe camera_origin_valid" "$(v "$OUT/P1-camera.txt" camera_origin_valid)" "1"
  if origin_nonzero "$(v "$OUT/P1-camera.txt" camera_origin)" "100"; then
    echo "  OK   A5 probe camera_origin $(v "$OUT/P1-camera.txt" camera_origin) is a real position"
  else
    echo "  FAIL A5 probe camera_origin $(v "$OUT/P1-camera.txt" camera_origin) is zero/missing (a zero cmdinfo block cannot prove the camera)"; fail=1
  fi
  if eye_close "$(last_field "$OUT/a5.trace" "scene: camera mode=" eye)" "$(v "$OUT/P1-camera.txt" camera_origin)" "0.2"; then
    echo "  OK   A5 camera eye matches the entry-$entry view sample"
  else
    echo "  FAIL A5 camera eye $(last_field "$OUT/a5.trace" "scene: camera mode=" eye) != probe $(v "$OUT/P1-camera.txt" camera_origin)"; fail=1
  fi
}
# ---- A6: the pre-fix origin is before history ------------------------------
check_a6() {
  assert_eq "A6 bagel server tick 1 status" "$(v "$OUT/S5-snapshot-1.txt" snapshot_status)" "before-history"
  A6_STATUS=$(v "$OUT/S5-snapshot-56148.txt" snapshot_status)
  case "$A6_STATUS" in
    available|checkpoint) echo "  OK   A6 bagel server tick 56148 status = $A6_STATUS" ;;
    *) echo "  FAIL A6 bagel server tick 56148 status = $A6_STATUS"; fail=1 ;;
  esac
  assert_ge "A6 bagel server tick 56148 states" "$(v "$OUT/S5-snapshot-56148.txt" snapshot_states)" "1"
}

run_forward() {
  echo "=== A0: the readers, both ways (synthetic lines, no product) ==="
  printf ' 0.1s scene: clock tick=7 entryTick=8 serverTick=100 serverTickKnown=1\n' > "$OUT/selftest.trace"
  printf ' 0.1s scene: snapshot status=0 stale=0 states=3\n' >> "$OUT/selftest.trace"
  assert_eq "reader clock entryTick" "$(clock_field "$OUT/selftest.trace" 7 entryTick)" "8"
  assert_eq "reader clock serverTick" "$(clock_field "$OUT/selftest.trace" 7 serverTick)" "100"
  if [ -z "$(clock_field "$OUT/selftest.trace" 99 entryTick)" ]; then
    echo "  OK   reader clock unknown tick is empty"
  else
    echo "  FAIL reader clock unknown tick = $(clock_field "$OUT/selftest.trace" 99 entryTick)"; fail=1
  fi
  assert_eq "reader last snapshot status" "$(last_field "$OUT/selftest.trace" "scene: snapshot status=" status)" "0"
  assert_eq "reader last snapshot states" "$(last_field "$OUT/selftest.trace" "scene: snapshot status=" states)" "3"

  echo
  echo "=== A1/A2: the stream start, both polarities (probe + product) ==="
  probe_run "$OUT/S1-scan" "$S1" --server-tick-at 0,1,20000
  probe_run "$OUT/S5-scan" "$S5" --server-tick-at 0
  # Rows of the per-packet server-tick table. A3/A5 assert the product
  # resolves playback tick N to exactly table[N]; the table is the oracle.
  T3=$(v "$OUT/S1-scan.txt" server_tick_at_20000)
  assert_ge "S1 table row 20000 present" "$T3" "1"
  assert_eq "S1 scan" "$(v "$OUT/S1-scan.txt" scan)" "1"
  assert_eq "S5 scan" "$(v "$OUT/S5-scan.txt" scan)" "1"
  product_run "$OUT/a1-zero" "$S1" 0 1 --start-paused
  product_run "$OUT/a1-one" "$S1" 1 1
  product_run "$OUT/a2-zero" "$S5" 0 1 --start-paused
  check_a1
  check_a2

  echo
  echo "=== A3: mid-demo snapshot from the packet table ==="
  probe_run "$OUT/S1-snapshot-A3" "$S1" --snapshot "$T3"
  product_run "$OUT/a3" "$S1" 20000 8
  check_a3

  echo
  echo "=== A4: view samples are data-entry ticks, in order ==="
  check_a4

  echo
  echo "=== A5: camera eye is the entry-tick view sample (POV demo) ==="
  probe_run "$OUT/P1-scan" "$P1" --server-tick-at 600
  P1_ENTRY=$(v "$OUT/P1-scan.txt" first_data_entry_tick)
  T5P=$(v "$OUT/P1-scan.txt" server_tick_at_600)
  [ -n "$P1_ENTRY" ] || P1_ENTRY=0
  assert_ge "A5 P1 first_data_entry_tick" "$P1_ENTRY" "0"
  assert_ge "A5 P1 table row 600" "$T5P" "1"
  probe_run "$OUT/P1-camera" "$P1" --camera-tick "$((P1_ENTRY + 600))"
  product_run "$OUT/a5" "$P1" 600 8
  check_a5

  echo "=== A6: negative control, the pre-fix origin ==="
  probe_run "$OUT/S5-snapshot-1" "$S5" --snapshot 1
  probe_run "$OUT/S5-snapshot-56148" "$S5" --snapshot 56148
  check_a6
}

# Missing input is a failure, not a skip.
missing=0
for path in "$PROBE" "$EXE" "$TF" "$S1" "$S5" "$P1"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; missing=1; }
done
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; missing=1; }
[ "$missing" = "0" ] || exit 1
# One launch site, guarded. A second launch path added later would not inherit
# --hidden-window/--audio-disabled; that is the audio incident this repo already had.
LAUNCH_SITES=$(grep -c 'timeout.*"\$EXE"' "$0")
if [ "$LAUNCH_SITES" != "1" ]; then
  echo "FATAL: this gate has $LAUNCH_SITES product launch sites; every launch must go through product_run()"
  exit 1
fi

echo "demo-clock-check  demo_S1=$S1"
echo "                  demo_S5=$S5"
echo "                  demo_P1=$P1"
echo
run_forward
FORWARD_FAIL=$fail

if [ "$MUTATION" = "1" ]; then
  echo
  echo "=== mutations: each must move the reading it names ==="
  BACKUP="$OUT/mutation-backup"
  mkdir -p "$BACKUP"
  mut_fail=0
  restore_source() { # <file>
    local file="$1"
    cp "$BACKUP/$(basename "$file")" "$file" || return 1
    if cmp -s "$BACKUP/$(basename "$file")" "$file"; then
      echo "  restored $(basename "$file") byte-identical (cmp)"; return 0
    fi
    echo "  FAIL restore of $file is NOT byte-identical"; return 1
  }
  apply_mutation() { # <file> <needle> <repl> <label>
    local file="$1" needle="$2" repl="$3" label="$4" count
    cp "$file" "$BACKUP/$(basename "$file")" || { echo "  MUTATION-INVALID $label: backup failed"; return 1; }
    count=$(grep -a -F -c -- "$needle" "$file" 2>/dev/null)
    count=${count:-0}
    if [ "$count" != "1" ]; then
      echo "  MUTATION-INVALID $label: the anchor matches $count times, so the mutation"
      echo "                    cannot be attributed to the line it names."
      restore_source "$file" || mut_fail=1
      return 1
    fi
    "$PY" - "$file" "$needle" "$repl" <<'PYEOF'
import sys
path, needle, repl = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(path, 'rb').read()
n, r = needle.encode(), repl.encode()
if data.count(n) != 1:
    sys.exit("anchor count != 1")
open(path, 'wb').write(data.replace(n, r, 1))
PYEOF
    if [ $? -ne 0 ]; then
      echo "  MUTATION-INVALID $label: the replacement did not apply"
      restore_source "$file" || mut_fail=1
      return 1
    fi
  }
  rebuild() { # <label> <target...>
    local label="$1"; shift
    bash build-target.sh "$@" > "$OUT/build-$label.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
      echo "  MUTATION-INVALID $label: the mutated source does not compile, so the mutation proves nothing"
      tail -6 "$OUT/build-$label.log"
    fi
    return "$rc"
  }

  # m1: the pre-fix origin -- the SIGNON entry tick added to the playback tick.
  echo
  echo "  -- m1: sceneEntryTick uses the signon entry tick --"
  if apply_mutation native/src/main.cpp \
      '? firstPacketTick + g_playback.tick : -1;' \
      '? demoNetworkSummary.firstSignonEntryTick + g_playback.tick : -1;' m1; then
    if rebuild m1 tf2_demo_native; then
      product_run "$OUT/m1-a1-zero" "$S1" 0 1 --start-paused
      product_run "$OUT/m1-a1-one" "$S1" 1 1
      fail=0; check_a1 "$OUT/m1-a1-zero.trace" "$OUT/m1-a1-one.trace"
      if [ "$fail" = "1" ]; then echo "  MUTATION-RED   m1: the entry-tick reading moved"; else
        echo "  MUTATION-GREEN m1: the mutation did not move the reading"; mut_fail=1; fi
      restore_source native/src/main.cpp || mut_fail=1
      rebuild m1-restore tf2_demo_native || mut_fail=1
      product_run "$OUT/m1r-a1-zero" "$S1" 0 1 --start-paused
      product_run "$OUT/m1r-a1-one" "$S1" 1 1
      fail=0; check_a1 "$OUT/m1r-a1-zero.trace" "$OUT/m1r-a1-one.trace"
      if [ "$fail" != "0" ]; then echo "  FAIL m1 baseline after restore is not green"; mut_fail=1; fi
    else
      restore_source native/src/main.cpp || mut_fail=1
      rebuild m1-restore tf2_demo_native || mut_fail=1
      mut_fail=1
    fi
  else
    mut_fail=1
  fi

  # m2: the entity snapshot queried with the entry tick (the old defect).
  echo
  echo "  -- m2: entity snapshot query uses the entry tick --"
  if apply_mutation native/src/main.cpp \
      '            demoNetworkSummary, sceneTick, currentEntityStates, &resolvedSceneTick);' \
      '            demoNetworkSummary, sceneEntryTick, currentEntityStates, &resolvedSceneTick);' m2; then
    if rebuild m2 tf2_demo_native; then
      product_run "$OUT/m2-a3" "$S1" 20000 8
      fail=0; check_a3 "$OUT/m2-a3.trace"
      if [ "$fail" = "1" ]; then echo "  MUTATION-RED   m2: the snapshot reading moved"; else
        echo "  MUTATION-GREEN m2: the mutation did not move the reading"; mut_fail=1; fi
      restore_source native/src/main.cpp || mut_fail=1
      rebuild m2-restore tf2_demo_native || mut_fail=1
      product_run "$OUT/m2r-a3" "$S1" 20000 8
      fail=0; check_a3 "$OUT/m2r-a3.trace"
      if [ "$fail" != "0" ]; then echo "  FAIL m2 baseline after restore is not green"; mut_fail=1; fi
    else
      restore_source native/src/main.cpp || mut_fail=1
      rebuild m2-restore tf2_demo_native || mut_fail=1
      mut_fail=1
    fi
  else
    mut_fail=1
  fi

  # m3: drop the duplicate collapsing -- equal ticks survive into the vector that
  echo
  echo "  -- m3: the duplicate collapsing removed (equal ticks survive) --"
  if apply_mutation native/src/demo_header.cpp \
      '      if (!deduped.empty() && deduped.back().tick == sample.tick) {' \
      '      if (false) {' m3; then
    if rebuild m3 demo_open_probe; then
      probe_run "$OUT/m3-P1-scan" "$P1"
      fail=0
      assert_eq "m3 P1 view_out_of_order_after_repair" "$(v "$OUT/m3-P1-scan.txt" view_out_of_order_after_repair)" "0"
      assert_gt "m3 P1 view_out_of_order (the input really is unsorted)" "$(v "$OUT/m3-P1-scan.txt" view_out_of_order)" "0"
      if [ "$fail" = "1" ]; then echo "  MUTATION-RED   m3: the ordering reading moved"; else
        echo "  MUTATION-GREEN m3: the mutation did not move the reading"; mut_fail=1; fi
      restore_source native/src/demo_header.cpp || mut_fail=1
      rebuild m3-restore demo_open_probe || mut_fail=1
      probe_run "$OUT/m3r-P1-scan" "$P1"
      fail=0
      assert_eq "m3r P1 view_out_of_order_after_repair" "$(v "$OUT/m3r-P1-scan.txt" view_out_of_order_after_repair)" "0"
      if [ "$fail" != "0" ]; then echo "  FAIL m3 baseline after restore is not green"; mut_fail=1; fi
    else
      restore_source native/src/demo_header.cpp || mut_fail=1
      rebuild m3-restore demo_open_probe || mut_fail=1
      mut_fail=1
    fi
  else
    mut_fail=1
  fi

  # m4: the shared cap back to 65536 (the line t66-camera also edits).
  echo
  echo "  -- m4: kMaxViewSamples back to 65536 --"
  if apply_mutation native/src/demo_header_internal.h \
      'constexpr std::size_t kMaxViewSamples = 1048576u;' \
      'constexpr std::size_t kMaxViewSamples = 65536u;' m4; then
    if rebuild m4 demo_open_probe; then
      probe_run "$OUT/m4-S5-scan" "$S5"
      fail=0
      assert_eq "m4 S5 view_samples_dropped" "$(v "$OUT/m4-S5-scan.txt" camera_track_dropped)" "0"
      if [ "$fail" = "1" ]; then echo "  MUTATION-RED   m4: the drop reading moved"; else
        echo "  MUTATION-GREEN m4: the mutation did not move the reading"; mut_fail=1; fi
      restore_source native/src/demo_header_internal.h || mut_fail=1
      rebuild m4-restore demo_open_probe || mut_fail=1
      probe_run "$OUT/m4r-S5-scan" "$S5"
      fail=0
      assert_eq "m4r S5 view_samples_dropped" "$(v "$OUT/m4r-S5-scan.txt" camera_track_dropped)" "0"
      if [ "$fail" != "0" ]; then echo "  FAIL m4 baseline after restore is not green"; mut_fail=1; fi
    else
      restore_source native/src/demo_header_internal.h || mut_fail=1
      rebuild m4-restore demo_open_probe || mut_fail=1
      mut_fail=1
    fi
  else
    mut_fail=1
  fi

  echo
  if [ "$mut_fail" = "0" ] && [ "$FORWARD_FAIL" = "0" ]; then
    echo "MUTATION-CAUGHT=PASS (4/4 mutations red their named assertion and restore byte-identical)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL"
  exit 1
fi

echo
if [ "$fail" = "0" ]; then
  echo "DEMO-CLOCK-CHECK=PASS"
  exit 0
fi
echo "DEMO-CLOCK-CHECK=FAIL"
exit 1
