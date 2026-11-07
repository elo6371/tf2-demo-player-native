#!/usr/bin/env bash
# world-geometry-budget-check.sh -- T88: the world triangle budget, as a reading
#                                    and as a refusal that can be caught.
#
# WHY THIS FILE EXISTS
#   native/src/main.cpp asked the BSP parser for 200,000 world triangles. That
#   number was the parser's default, never measured against the maps the product
#   draws, and the parser's answer to it was a bare return: cp_snakewater_final1
#   needs 242,361 triangles (234,699 of them while the func_illusionary bug was
#   deleting 2,892 of its faces). At the old 200,000 the fixed build reports
#   requested=242,361 dropped=42,643 over 3,513 faces, and the surfaces were
#   dropped from the frame with no trace line saying so. A gate could not have caught it because there was nothing to read:
#   the loss had no number, no face and no name.
#
#   This gate reads the budget back from BOTH implementations that apply it:
#     * the product's trace -- "map: geometry budget" on the map-load path and
#       "gpu: world geometry budget" on the first drawn frame, and
#     * world_geometry_probe, which takes the capacity as an argument and so can
#       be asked what a map needs when nothing is refused.
#   The two must agree about the demand, and the demand must not move with the
#   capacity. That is the shape of the fix: the capacity decides what is KEPT,
#   the parser reports what it REFUSED, and the trace carries both.
#
# WHAT IT ASSERTS (per authorised map, one product capture each)
#   B1  the product wrote the budget line on the load path AND on the drawn
#       frame. A budget that is only in a startup log is not in a capture trace,
#       and a gate reads traces.
#   B2  capacity = 400000 (the constant in main.cpp), requested <= capacity,
#       dropped = 0, state = within. This is the assertion a capacity shrink
#       breaks: at 200,000 snakewater reads dropped = 42643 against a 42,361
#       shortfall (see B6 for why those are not equal).
#   B3  requested equals the pin. The demand is a property of the map, so a
#       parser change that stops reading faces moves this number even when
#       nothing is dropped.
#   B4  brush faces drawn and skipped equal the pin. This is the assertion a
#       func_illusionary whitelist rollback breaks: snakewater loses 2,892 drawn
#       faces and gains exactly that many skipped ones.
#   B5  world_geometry_probe at --triangle-limit 4000000 reads the same requested
#       as the product: two implementations, one answer for the map's demand.
#   B6  world_geometry_probe at 200000 and at 100000 reads the SAME requested
#       (the demand does not move with the capacity), with dropped >= demand - limit,
#       facesDropped > 0 and firstFace >= 0. The inequality, not an equality: a
#       displacement grid the budget refuses is replaced by a fallback fan that
#       also occupies slots, so the refused count is never smaller than the
#       shortfall and can be larger (snakewater at 200,000 reads 42,643 refused
#       against a 42,361 shortfall). This is what makes "dropped" evidence rather
#       than a zero somebody forgot to wire.
#
#   pl_odyssey is measured and printed as a reading, never asserted: it is not one
#   of the four authorised maps, and its demand is measured here rather than
#   assumed to fit.
#
# MUTATIONS (--mutation): each must turn a NAMED assertion red, then the source
#   comes back byte-identical and the reading comes back to the pin.
#   m1  kWorldTriangleCapacity 400000 -> 200000 in main.cpp        -> B2 must fail
#   m2  func_illusionary dropped from the whitelist in bsp_map.cpp -> B4 must fail
#
# Usage: bash world-geometry-budget-check.sh [--only=<map[,map]>] [--re-pin[=<why>]]
#                                           [--mutation] [--reuse]
# Exit: 0 = every assertion held; 1 = an assertion failed; 2 = an input is missing.
set -uo pipefail
cd "$(dirname "$0")"

PYTHON_DEFAULT=C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe
LOCK_DEFAULT=/d/TF2_Native_Worktrees/_shared/hostlock.sh
OUT_DEFAULT=evidence/t88-maps
if [[ -v PYTHON ]]; then :; else PYTHON=$PYTHON_DEFAULT; fi
if [[ -v HOSTLOCK ]]; then LOCK=$HOSTLOCK; else LOCK=$LOCK_DEFAULT; fi
if [[ -v BUDGET_OUT ]]; then OUT=$BUDGET_OUT; else OUT=$OUT_DEFAULT; fi
if [[ -v HOLD_LOCK ]]; then :; else HOLD_LOCK=0; fi
[[ -x $PYTHON ]] || PYTHON=python
PRODUCT=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/world_geometry_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MATRIX=evidence/t62-map-matrix
SCRATCH=.scratch/t88-budget
PINS=$OUT/budget-pins.tsv
CAPACITY=400000
TICK=2000
SPEED=8
# Hermetic run inputs. The product saves settings and UI state on exit no matter
# what the caller changed (main.cpp saveSettings(persistent) + saveToSettings,
# native_ui.cpp recentDemo MRU), so every launch below points both files at
# copies inside the evidence directory. Never at the real profile directory.
# The two files live in a throwaway directory, not in $OUT: a stale copy would let
# a later run start from state an earlier run produced, and the directory is
# removed on exit so nothing a run leaves behind can influence the next one.
ISO_DIR=$(mktemp -d) || { echo "cannot create a temporary directory for the run inputs" >&2; exit 2; }
SETTINGS_FILE=$ISO_DIR/settings.ini
UI_FILE=$ISO_DIR/ui.ini
PROBE_HUGE=4000000
SHRINK="200000 100000"
MAPS="cp_snakewater_final1 cp_sunshine koth_bagel_rc12 koth_bagel_rc11"
READING_MAPS="pl_odyssey"
LOCK_OWNER=${LOCK_OWNER:-t88-budget}

ONLY=""
REPIN=0
REPIN_WHY=""
MUTATION=0
PIXEL_ATTR=0
REUSE=0
usage() {
  echo "usage: $0 [--only=<map[,map]>] [--re-pin[=<why>]] [--mutation] [--pixel-attr] [--reuse]"
  exit 2
}
for arg in "$@"; do
  case $arg in
    --only=*) ONLY=$(echo "$arg" | sed 's/^[^=]*=//') ;;
    --re-pin) REPIN=1 ;;
    --re-pin=*) REPIN=1; REPIN_WHY=$(echo "$arg" | sed 's/^[^=]*=//') ;;
    --mutation) MUTATION=1 ;;
    --pixel-attr) PIXEL_ATTR=1 ;;
    --reuse) REUSE=1 ;;
    -h|--help) usage ;;
    *) echo "unknown argument: $arg" >&2; usage ;;
  esac
done

for tool in "$PRODUCT" "$PROBE"; do
  if [[ ! -x $tool ]]; then
    echo "missing tool: $tool (run bash build-target.sh tf2_demo_native world_geometry_probe)" >&2
    exit 2
  fi
done
PRODUCT_SHA=$(sha256sum "$PRODUCT" | cut -d" " -f1)
mkdir -p "$OUT" "$SCRATCH"

# HOLD_LOCK=1 means this process already owns the shared lock (a reserved window,
# which --mutation needs because it rebuilds). Prove it is ours before running
# anything unprotected.
HL_ROOT=$(dirname "$LOCK")
if [[ $HOLD_LOCK == 1 ]]; then
  held_owner=$(head -1 "$HL_ROOT/host.lock/owner" 2>/dev/null | tr -d "\r")
  if [[ ! -d $HL_ROOT/host.lock || $held_owner != "$LOCK_OWNER" ]]; then
    echo "HOLD_LOCK=1 but the lock is held by $held_owner, not $LOCK_OWNER; refusing" >&2
    exit 2
  fi
  echo "HOLD_LOCK=1: captures and builds run inside the held lock (owner=$LOCK_OWNER)"
fi
locked() {
  if [[ $HOLD_LOCK == 1 ]]; then bash -c "$1"; return $?; fi
  bash "$LOCK" run "$LOCK_OWNER" bash -c "$1"
}

failures=0
failure() { echo "  FAIL $*"; failures=$((failures + 1)); }
pass() { echo "  ok   $*"; }

field() { # field <key=value ...> <key>  -> the value, empty when absent
  local line=$1 key=$2 token
  for token in $line; do
    case $token in
      "$key"=*) echo $(echo "$token" | sed 's/^[^=]*=//'); return 0 ;;
    esac
  done
  echo ""
}

map_bsp_rel() {
  case $1 in
    cp_snakewater_final1|pl_odyssey|cp_sunshine) echo "maps/$1.bsp" ;;
    koth_bagel_rc12|koth_bagel_rc11) echo "download/maps/$1.bsp" ;;
    *) echo "" ;;
  esac
}

pin_line() { # pin_line <map> -> the pin row (empty when absent)
  [[ -s $PINS ]] || return 0
  awk -v m="$1" '$1 == m { print; exit }' "$PINS"
}

capture_budget() { # capture_budget <map> <tag> -> trace path on stdout
  local map=$1 tag=$2 dir=$SCRATCH/$1 cam_file=$MATRIX/$1/camera.txt
  mkdir -p "$dir"
  local demo cam tick trace bmp csv log
  demo=$(grep -a "^demo=" "$cam_file" | head -1 | cut -d= -f2-)
  cam=$(grep -a "^debug_camera_args=" "$cam_file" | head -1 | cut -d= -f2-)
  tick=$(grep -a "^camera_tick=" "$cam_file" | head -1 | cut -d= -f2-)
  [[ -n $tick ]] || tick=$TICK
  if [[ -z $demo ]]; then
    echo "  capture $map: no demo= line in $cam_file" >&2
    return 1
  fi
  trace=$dir/$tag.trace
  bmp=$dir/$tag.bmp
  csv=$dir/$tag.csv
  log=$dir/$tag.log
  if [[ $REUSE -eq 1 && -s $trace && -s "$dir/.build.$tag" ]]; then
    if [[ "$(cat "$dir/.build.$tag")" == "$PRODUCT_SHA" ]]; then
      echo "$trace"; return 0
    fi
  fi
  rm -f "$trace"
  local cmd rc
  cmd=$(printf "%q " TF2_NATIVE_TRACE="$(cygpath -m "$trace")" timeout 400 "$PRODUCT" \
    --tf-root "$TF" --audio-device 6 --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$SETTINGS_FILE")" \
    --ui-settings-file "$(cygpath -m "$UI_FILE")" \
    --demo "$demo" --playback-speed "$SPEED" --capture-tick "$tick" \
    --capture-frame "$(cygpath -m "$bmp")" --metrics-file "$(cygpath -m "$csv")" $cam)
  for attempt in 1 2; do
    locked "$cmd" > "$log" 2>&1
    rc=$?
    if [[ $rc -ne 0 || ! -s $bmp ]]; then
      echo "  capture $map/$tag failed rc=$rc" >&2
      tail -3 "$log" | grep -a . | sed 's/^/    /' >&2
      return 1
    fi
    if grep -aq "map: geometry budget " "$trace"; then
      printf "%s" "$PRODUCT_SHA" > "$dir/.build.$tag"
      echo "$trace"; return 0
    fi
    echo "  capture $map/$tag wrote no budget line (attempt $attempt)" >&2
  done
  return 1
}

probe_budget() { # probe_budget <bsp_rel> <limit> -> the geometry budget line
  "$PROBE" --tf-root "$TF" --map "$1" --triangle-limit "$2" 2>&1 | grep -a "^geometry budget " | head -1
}

judge_map() { # judge_map <map> -> 0 when every assertion held
  local map=$1 bsp_rel cam_file trace load_line frame_line brush_line
  local cap req dropped state faces_dropped first_face drawn skipped
  local huge_line huge_req huge_dropped limit line s_req s_drop s_faces s_first
  local pcap preq pdropped pdrawn pskipped pwfaces pbfaces
  echo ""
  echo "== $map =="
  bsp_rel=$(map_bsp_rel "$map")
  cam_file=$MATRIX/$map/camera.txt
  if [[ -z $bsp_rel || ! -s $cam_file ]]; then
    failure "$map: missing camera file or bsp mapping"; return 1
  fi
  if [[ ! -f $TF/$bsp_rel ]]; then
    failure "$map: missing BSP $TF/$bsp_rel"; return 1
  fi

  trace=$(capture_budget "$map" forward) || { failure "$map: no capture"; return 1; }
  # Trace lines carry a leading timestamp ("42610.9 map: geometry budget ..."),
  # so these patterns must not be anchored to the start of the line.
  load_line=$(grep -a "map: geometry budget " "$trace" | head -1)
  frame_line=$(grep -a "gpu: world geometry budget " "$trace" | head -1)
  brush_line=$(grep -a "gpu: world brush models " "$trace" | head -1)
  if [[ -z $load_line ]]; then
    failure "B1 $map: no map: geometry budget line in the capture trace"; return 1
  fi
  cap=$(field "$load_line" capacity); req=$(field "$load_line" requested)
  dropped=$(field "$load_line" dropped); state=$(field "$load_line" state)
  faces_dropped=$(field "$load_line" facesDropped); first_face=$(field "$load_line" firstFace)
  drawn=$(field "$brush_line" facesDrawn); skipped=$(field "$brush_line" facesSkipped)
  echo "  reading load: $load_line"
  echo "  reading brush: $brush_line"

  # B1: the capture trace must carry the budget on the drawn frame as well.
  if [[ -z $frame_line ]]; then
    failure "B1 $map: no gpu: world geometry budget line in the capture trace"
  else
    echo "  reading frame: $frame_line"
    [[ $(field "$frame_line" requested) == "$req" ]] || \
      failure "B1 $map: frame requested=$(field "$frame_line" requested), load requested=$req"
    [[ $(field "$frame_line" dropped) == "$dropped" ]] || \
      failure "B1 $map: frame dropped=$(field "$frame_line" dropped), load dropped=$dropped"
  fi

  # B2: the capacity pin, and no loss at all.
  [[ $cap == "$CAPACITY" ]] || failure "B2 $map: capacity=$cap, expected $CAPACITY"
  if [[ $dropped != "0" || $state != "within" ]]; then
    failure "B2 $map: dropped=$dropped state=$state firstFace=$first_face -- the map lost geometry to the budget"
  fi
  if [[ -n $req && -n $cap ]] && (( req > cap )); then
    failure "B2 $map: requested=$req > capacity=$cap"
  fi

  read -r pcap preq pdropped pdrawn pskipped pwfaces pbfaces <<< "$(pin_line "$map")"
  wfaces=$(field "$brush_line" worldFaces); bfaces=$(field "$brush_line" brushFaces)
  # B3: the demand, pinned. A parser change that stops reading faces moves it.
  if [[ -n $preq ]]; then
    [[ $req == "$preq" ]] || failure "B3 $map: requested=$req, pin=$preq"
    [[ $cap == "$pcap" ]] || failure "B3 $map: capacity=$cap, pin=$pcap"
  elif [[ $REPIN -eq 1 ]]; then
    echo "  re-pin: no earlier pin for $map, this run becomes the reference"
  else
    failure "B3 $map: no pin row (run once with --re-pin=<why>)"
  fi
  # B4: brush faces, pinned -- the surface a whitelist rollback moves.
  if [[ -n $pdrawn ]]; then
    [[ $drawn == "$pdrawn" ]] || failure "B4 $map: facesDrawn=$drawn, pin=$pdrawn"
    [[ $skipped == "$pskipped" ]] || failure "B4 $map: facesSkipped=$skipped, pin=$pskipped"
  elif [[ $REPIN -eq 1 ]]; then
    echo "  re-pin: no earlier brush-face pin for $map, this run becomes the reference"
  elif [[ -z $drawn ]]; then
    failure "B4 $map: no gpu: world brush models line in the capture trace"
  fi
  # B4b: the face partition, as an accounting identity rather than a pin. Every
  # brush face is either drawn or skipped (drawn + skipped == brushFaces), and a
  # whitelist change moves faces BETWEEN the world count and the brush count
  # without inventing or losing any (worldFaces + brushFaces is a property of the
  # map). A face that vanishes from both sides is exactly the silent loss this
  # gate exists for, so this one is checked even when a pin exists.
  if [[ -n $wfaces && -n $bfaces ]]; then
    part_gap=$((bfaces - drawn - skipped))
    echo "  reading $map: face partition world=$wfaces brush=$bfaces drawn=$drawn skipped=$skipped unattributed=$part_gap"
    if [[ -n $pwfaces && -n $pbfaces ]]; then
      [[ $((wfaces + bfaces)) -eq $((pwfaces + pbfaces)) ]] ||         failure "B4b $map: worldFaces($wfaces) + brushFaces($bfaces) = $((wfaces + bfaces)), pin=$((pwfaces + pbfaces)) -- faces entered or left the map, not just the whitelist"
      pin_gap=$((pbfaces - pdrawn - pskipped))
      [[ $part_gap -eq $pin_gap ]] ||         failure "B4c $map: $part_gap brush faces are accounted for by neither pass (pin $pin_gap) -- a face was dropped without being counted"
    fi
  else
    failure "B4b $map: no worldFaces/brushFaces on the gpu: world brush models line"
  fi

  # B5: the probe agrees about the demand.
  huge_line=$(probe_budget "$bsp_rel" "$PROBE_HUGE")
  if [[ -z $huge_line ]]; then
    failure "B5 $map: probe at $PROBE_HUGE printed no budget line"
  else
    huge_req=$(field "$huge_line" requested); huge_dropped=$(field "$huge_line" dropped)
    [[ $huge_req == "$req" ]] || failure "B5 $map: probe requested=$huge_req, product requested=$req"
    [[ $huge_dropped == "0" ]] || failure "B5 $map: probe at $PROBE_HUGE dropped=$huge_dropped"
  fi

  # B6: the demand does not move with the capacity, and a shrink is detected.
  for limit in $SHRINK; do
    line=$(probe_budget "$bsp_rel" "$limit")
    if [[ -z $line ]]; then failure "B6 $map: probe at $limit printed no budget line"; continue; fi
    s_req=$(field "$line" requested); s_drop=$(field "$line" dropped)
    s_faces=$(field "$line" facesDropped); s_first=$(field "$line" firstFace)
    echo "  reading shrink $limit: $line"
    [[ $s_req == "$req" ]] || \
      failure "B6 $map: at $limit requested=$s_req, demand is $req (the ask must not move with the capacity)"
    if [[ -z $s_faces ]]; then s_faces=0; fi
    if (( limit < req )); then
      # Not an equality: a refused displacement grid leaves a fallback fan that
      # occupies slots too, so the refused count is never below the shortfall.
      (( s_drop >= req - limit )) || failure "B6 $map: at $limit dropped=$s_drop, expected at least $((req - limit))"
      (( s_drop <= req )) || failure "B6 $map: at $limit dropped=$s_drop, more than the whole demand $req"
      (( s_faces > 0 )) || failure "B6 $map: at $limit facesDropped=$s_faces, expected > 0"
      if [[ -z $s_first ]] || (( s_first < 0 )); then
        failure "B6 $map: at $limit firstFace=$s_first, expected a face index"
      fi
    fi
  done
  return 0
}

# ---------------------------------------------------------------- mutations ---
# Each mutation edits one line of the source, rebuilds, and requires the gate to
# go red AT A NAMED ASSERTION. Then the source is restored byte-identically, the
# product is rebuilt, and the gate must come back green with the same pins.
source_backup() { mkdir -p "$SCRATCH/mut"; cp "$1" "$SCRATCH/mut/$(basename "$1").bak"; }
source_restore() {
  cp "$SCRATCH/mut/$(basename "$1").bak" "$1"
  if ! cmp -s "$1" "$SCRATCH/mut/$(basename "$1").bak"; then
    echo "  MUTATION-RESTORE-FAILED $1 is not byte-identical to its backup" >&2
    return 1
  fi
  return 0
}
# A window that dies mid-mutation must not leave the pre-fix behaviour in the
# source tree: the payload would then be a map that silently loses geometry
# again, and the only record of it would be this script's own backup. SIGKILL
# cannot be trapped; anything that can be, is. The compare makes it a no-op
# after a successful restore.
restore_all_backups() {
  local bak
  for bak in "$SCRATCH"/mut/*.bak; do
    [[ -e $bak ]] || continue
    case $(basename "$bak") in
      main.cpp.bak)
        if ! cmp -s "$bak" native/src/main.cpp; then
          cp "$bak" native/src/main.cpp
          echo "  EXIT-RESTORE main.cpp (a mutation was still applied at exit)" >&2
        fi ;;
      bsp_map.cpp.bak)
        if ! cmp -s "$bak" native/src/bsp_map.cpp; then
          cp "$bak" native/src/bsp_map.cpp
          echo "  EXIT-RESTORE bsp_map.cpp (a mutation was still applied at exit)" >&2
        fi ;;
    esac
  done
}
cleanup_run_inputs() { [[ -n ${ISO_DIR:-} ]] && rm -rf "$ISO_DIR"; return 0; }
trap 'restore_all_backups; cleanup_run_inputs' EXIT

edit_source() { # edit_source <file> <old> <new>  (exactly one occurrence)
  "$PYTHON" - "$1" "$2" "$3" <<'PYEDIT'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, "r", encoding="utf-8", newline="") as handle:
    text = handle.read()
count = text.count(old)
if count != 1:
    sys.stderr.write("PATTERN NOT FOUND (%d occurrences): %r\n" % (count, old))
    sys.exit(3)
with open(path, "w", encoding="utf-8", newline="") as handle:
    handle.write(text.replace(old, new, 1))
PYEDIT
}
build_product() {
  locked "bash build-target.sh tf2_demo_native world_geometry_probe" > "$SCRATCH/mut/build.log" 2>&1
}
run_gate() { # run_gate <extra args...> -> gate output on stdout, exit code kept
  # WRITE_PINS=0: a mutant run must judge against the committed pins, never
  # rewrite them from its own reading (with --only it would also truncate the
  # file to one row).
  env WRITE_PINS=0 bash "$0" "$@"
}
mutation() { # mutation <name> <file> <old> <new> <expected-failure-text> <only-map>
  local name=$1 file=$2 old=$3 new=$4 expect=$5 only=$6 log out rc sha_before sha_after
  echo ""
  echo "-- mutation $name: $file"
  sha_before=$(sha256sum "$file" | cut -d" " -f1)
  source_backup "$file"
  if ! edit_source "$file" "$old" "$new"; then
    failure "MUTATION $name: could not apply the mutation to $file"
    source_restore "$file"
    return 1
  fi
  out=$SCRATCH/mut/$name.log
  if ! build_product; then
    failure "MUTATION $name: build failed (see $SCRATCH/mut/build.log)"
    tail -5 "$SCRATCH/mut/build.log" | sed 's/^/    /'
    source_restore "$file"
    return 1
  fi
  run_gate --only="$only" > "$out" 2>&1
  rc=$?
  if [[ $rc -eq 0 ]]; then
    failure "MUTATION $name: gate stayed green with the mutation applied ($expect)"
  elif ! grep -aqF "$expect" "$out"; then
    failure "MUTATION $name: gate went red but not at the named assertion; wanted: $expect"
    grep -a "FAIL" "$out" | head -3 | sed 's/^/    /'
  else
    echo "  mutation $name: gate red at the named assertion -- $(grep -aF "$expect" "$out" | head -1)"
  fi
  source_restore "$file" || failure "MUTATION $name: restore did not come back byte-identical"
  sha_after=$(sha256sum "$file" | cut -d" " -f1)
  if [[ $sha_after != "$sha_before" ]]; then
    failure "MUTATION $name: $file sha256 $sha_before -> $sha_after after restore"
  else
    echo "  mutation $name: $file restored byte-identical (sha256 $sha_after)"
  fi
  if ! build_product; then
    failure "MUTATION $name: rebuild after restore failed"
    return 1
  fi
  run_gate --only="$only" > "$SCRATCH/mut/$name-restored.log" 2>&1
  rc=$?
  if [[ $rc -ne 0 ]]; then
    failure "MUTATION $name: gate did not come back green after restore (see $SCRATCH/mut/$name-restored.log)"
    grep -a "FAIL" "$SCRATCH/mut/$name-restored.log" | head -3 | sed 's/^/    /'
  else
    echo "  mutation $name: gate green again after restore"
  fi
  cp "$out" "$OUT/mutation-$name.log" 2>/dev/null || true
  cp "$SCRATCH/mut/$name-restored.log" "$OUT/mutation-$name-restored.log" 2>/dev/null || true
  return 0
}

# --------------------------------------------------- hermetic run inputs ---
# The product writes its own settings and UI state on every exit (main.cpp
# saveSettings(persistent) at 11613-11625 and saveToSettings, native_ui.cpp
# recentDemo0..7 MRU at 137-139), whether or not this gate changed anything.
# Both files therefore point at copies in $ISO_DIR, and H1 below fails the run if
# the real profile moved anyway: a gate that edits the operator's profile is not
# reproducible, because the next run would start from different input.
REALPROFILE="$(cygpath -u "$LOCALAPPDATA")/TF2 Demo Player"
real_state() {
  local p
  for p in settings.ini ui.ini; do
    if [[ -f $REALPROFILE/$p ]]; then stat -c "%n %s %Y" "$REALPROFILE/$p"; else echo "$REALPROFILE/$p absent"; fi
  done
}
mkdir -p "$OUT"
if [[ ! -s $SETTINGS_FILE ]]; then
  if [[ -f $REALPROFILE/settings.ini ]]; then
    cp "$REALPROFILE/settings.ini" "$SETTINGS_FILE"
    echo "seeded $SETTINGS_FILE from the real profile (read only)"
  else
    : > "$SETTINGS_FILE"
    echo "seeded $SETTINGS_FILE empty (no real profile to copy)"
  fi
fi
if [[ ! -s $UI_FILE ]]; then
  if [[ -f $REALPROFILE/ui.ini ]]; then
    cp "$REALPROFILE/ui.ini" "$UI_FILE"
    echo "seeded $UI_FILE from the real profile (read only)"
  else
    : > "$UI_FILE"
    echo "seeded $UI_FILE empty (no real profile to copy)"
  fi
fi
REAL_BEFORE=$(real_state)

if [[ $MUTATION -eq 1 ]]; then
  mkdir -p "$OUT"
  # The red lands on B2's capacity pin, not on its dropped reading: with the
  # capacity shrunk to 200000 the first assertion that fires is "capacity=200000,
  # expected 400000", and that IS the assertion whose job is to notice the shrink.
  # (An earlier draft expected "dropped=" and would have called a correct red the
  # wrong red -- see docs/GATE-DESIGN-TRAPS.md trap 12.)
  mutation capacity-shrink native/src/main.cpp \
    "constexpr std::size_t kWorldTriangleCapacity = 400000;" \
    "constexpr std::size_t kWorldTriangleCapacity = 200000;" \
    "B2 cp_snakewater_final1: capacity=200000" cp_snakewater_final1
  # The whitelist anchor spans two source lines, and native/src/*.cpp are CRLF:
  # the pattern must be $'...' with an explicit \r\n. A literal newline here
  # matches nothing (count=0), edit_source exits 3 and the mutation reports
  # "could not apply the mutation" -- a false red that looks like the gate broke.
  mutation illusionary-whitelist native/src/bsp_map.cpp \
    $'return name == "func_brush" || name == "func_wall" || name == "func_lod"\r\n        || name == "func_illusionary";' \
    'return name == "func_brush" || name == "func_wall" || name == "func_lod";' \
    "B4 cp_snakewater_final1: facesDrawn=" cp_snakewater_final1
  echo ""
  if [[ $failures -eq 0 ]]; then echo "WORLD-GEOMETRY-BUDGET-MUTATION=PASS"; exit 0; fi
  echo "WORLD-GEOMETRY-BUDGET-MUTATION=FAIL ($failures)"; exit 1
fi

# ------------------------------------------------------- pixel attribution ---
# The fix changes what the product draws, so pinned frames move. Re-pinning
# without saying WHICH faces caused the move is the thing this mode exists to
# prevent: it captures the same camera and tick twice -- once on the fixed build
# (--world-face-id plus textured) and once with BOTH defects put back (capacity
# 200000 and the whitelist without func_illusionary, i.e. the pre-fix behaviour)
# -- and hands the four frames to world-geometry-budget-pixels.py.
#
# The assertion is one-sided on purpose: every changed textured pixel must belong
# to a face id that did not exist in the earlier frame. --pixel-attr rebuilds
# twice, so it needs the held lock (HOLD_LOCK=1) like --mutation does.
capture_frame() { # capture_frame <map> <tag> [extra product args...] -> bmp path
  local map=$1 tag=$2 dir=$SCRATCH/pix/$1 cam_file=$MATRIX/$1/camera.txt
  shift 2
  mkdir -p "$dir"
  local demo cam tick bmp log cmd rc
  demo=$(grep -a "^demo=" "$cam_file" | head -1 | cut -d= -f2-)
  cam=$(grep -a "^debug_camera_args=" "$cam_file" | head -1 | cut -d= -f2-)
  tick=$(grep -a "^camera_tick=" "$cam_file" | head -1 | cut -d= -f2-)
  [[ -n $tick ]] || tick=$TICK
  if [[ -z $demo ]]; then
    echo "  capture $map: no demo= line in $cam_file" >&2
    return 1
  fi
  bmp=$dir/$tag.bmp
  log=$dir/$tag.log
  cmd=$(printf "%q " TF2_NATIVE_TRACE="$(cygpath -m "$dir/$tag.trace")" timeout 400 "$PRODUCT" \
    --tf-root "$TF" --audio-device 6 --audio-disabled --hidden-window \
    --settings-file "$(cygpath -m "$SETTINGS_FILE")" \
    --ui-settings-file "$(cygpath -m "$UI_FILE")" \
    --demo "$demo" --playback-speed "$SPEED" --capture-tick "$tick" \
    --capture-frame "$(cygpath -m "$bmp")" --metrics-file "$(cygpath -m "$dir/$tag.csv")" "$@" $cam)
  rm -f "$bmp"
  locked "$cmd" > "$log" 2>&1
  rc=$?
  if [[ $rc -ne 0 || ! -s $bmp ]]; then
    echo "  capture $map/$tag failed rc=$rc (see $log)" >&2
    tail -3 "$log" | grep -a . | sed "s/^/    /" >&2
    return 1
  fi
  echo "$bmp"
}

mutate_both() { # put the pre-fix behaviour back in the two places that changed
  source_backup native/src/main.cpp || return 1
  source_backup native/src/bsp_map.cpp || return 1
  edit_source native/src/main.cpp \
    "constexpr std::size_t kWorldTriangleCapacity = 400000;" \
    "constexpr std::size_t kWorldTriangleCapacity = 200000;" || return 1
  edit_source native/src/bsp_map.cpp \
    $'return name == "func_brush" || name == "func_wall" || name == "func_lod"\r\n        || name == "func_illusionary";' \
    'return name == "func_brush" || name == "func_wall" || name == "func_lod";' || return 1
  return 0
}

restore_both() {
  local ok=0
  source_restore native/src/main.cpp || ok=1
  source_restore native/src/bsp_map.cpp || ok=1
  return $ok
}

pixel_attr() {
  local maps=$1 map a_tex a_ids b_tex b_ids out rc sha_now
  mkdir -p "$SCRATCH/pix" "$OUT/pixels"
  FIXED_SHA=$PRODUCT_SHA
  echo ""
  echo "== pixel attribution: after (fixed build, sha $FIXED_SHA) =="
  for map in $maps; do
    a_tex=$(capture_frame "$map" after-textured) || { failure "PIXELS $map: after textured capture failed"; continue; }
    a_ids=$(capture_frame "$map" after-ids --world-face-id) || { failure "PIXELS $map: after id capture failed"; continue; }
    echo "  after  $map textured=$(basename "$a_tex") ids=$(basename "$a_ids")"
  done
  echo ""
  echo "== pixel attribution: putting the pre-fix behaviour back =="
  if ! mutate_both; then
    failure "PIXELS: could not apply the combined mutation"
    restore_both
    return 1
  fi
  if ! build_product; then
    failure "PIXELS: build of the pre-fix behaviour failed (see $SCRATCH/mut/build.log)"
    restore_both
    build_product
    return 1
  fi
  echo "  pre-fix build sha $(sha256sum "$PRODUCT" | cut -d" " -f1)"
  for map in $maps; do
    b_tex=$(capture_frame "$map" before-textured) || { failure "PIXELS $map: before textured capture failed"; continue; }
    b_ids=$(capture_frame "$map" before-ids --world-face-id) || { failure "PIXELS $map: before id capture failed"; continue; }
    echo "  before $map textured=$(basename "$b_tex") ids=$(basename "$b_ids")"
  done
  echo ""
  echo "== pixel attribution: restoring the fix =="
  if ! restore_both; then
    failure "PIXELS: the source did not come back byte-identical"
  fi
  if ! build_product; then
    failure "PIXELS: rebuild after restore failed"
    return 1
  fi
  sha_now=$(sha256sum "$PRODUCT" | cut -d" " -f1)
  if [[ $sha_now != $FIXED_SHA ]]; then
    failure "PIXELS: the rebuilt product is not the fixed build (sha $sha_now, wanted $FIXED_SHA)"
  else
    pass "PIXELS: the pre-fix rebuild was rolled back; product sha back to $FIXED_SHA"
  fi
  echo ""
  echo "== pixel attribution: what the restored faces drew =="
  for map in $maps; do
    a_tex=$SCRATCH/pix/$map/after-textured.bmp
    a_ids=$SCRATCH/pix/$map/after-ids.bmp
    b_tex=$SCRATCH/pix/$map/before-textured.bmp
    b_ids=$SCRATCH/pix/$map/before-ids.bmp
    out=$OUT/pixels/$map.txt
    if [[ ! -s $a_tex || ! -s $a_ids || ! -s $b_tex || ! -s $b_ids ]]; then
      failure "PIXELS $map: a frame pair is missing, nothing to attribute"
      continue
    fi
    echo ""
    echo "-- $map"
    "$PYTHON" world-geometry-budget-pixels.py \
      --before-textured "$b_tex" --after-textured "$a_tex" \
      --before-ids "$b_ids" --after-ids "$a_ids" --out "$out" > "$SCRATCH/pix/$map.report" 2>&1
    rc=$?
    sed "s/^/  /" "$SCRATCH/pix/$map.report"
    if [[ $rc -eq 0 ]]; then
      pass "PIXELS $map: the picture change is contained in newly drawn faces ($out)"
    else
      failure "PIXELS $map: the change is NOT fully explained by the restored faces ($out)"
    fi
    cp "$SCRATCH/pix/$map/after-ids.trace" "$OUT/pixels/$map-after-ids.trace" 2>/dev/null || true
    cp "$SCRATCH/pix/$map/before-ids.trace" "$OUT/pixels/$map-before-ids.trace" 2>/dev/null || true
    cp "$SCRATCH/pix/$map/after-textured.csv" "$OUT/pixels/$map-after-textured.csv" 2>/dev/null || true
    cp "$SCRATCH/pix/$map/before-textured.csv" "$OUT/pixels/$map-before-textured.csv" 2>/dev/null || true
  done
  return 0
}

# ------------------------------------------------------------------- driver ---
if [[ -n $ONLY ]]; then
  CHECK_MAPS=$(echo "$ONLY" | tr ',' ' ')
  echo "ONLY: judging $CHECK_MAPS"
else
  CHECK_MAPS=$MAPS
fi

# --pixel-attr: say which faces the restored geometry actually drew. Rebuilds
# twice (pre-fix behaviour, then the fix again), so it wants a held lock.
if [[ $PIXEL_ATTR -eq 1 ]]; then
  pixel_attr "$CHECK_MAPS"
  echo ""
  if [[ $failures -eq 0 ]]; then echo "WORLD-GEOMETRY-PIXELS=PASS"; exit 0; fi
  echo "WORLD-GEOMETRY-PIXELS=FAIL ($failures assertions)"; exit 1
fi

if [[ $REPIN -eq 0 && ! -s $PINS ]]; then
  echo "missing $PINS -- run once with --re-pin=<why> to record the pins" >&2
  exit 2
fi

: > "$SCRATCH/pin-rows.txt"
for MAP in $CHECK_MAPS; do
  judge_map "$MAP"
  trace=$SCRATCH/$MAP/forward.trace
  if [[ -s $trace ]]; then
    load_line=$(grep -a "map: geometry budget " "$trace" | head -1)
    brush_line=$(grep -a "gpu: world brush models " "$trace" | head -1)
    echo "$MAP $(field "$load_line" capacity) $(field "$load_line" requested) $(field "$load_line" dropped) $(field "$brush_line" facesDrawn) $(field "$brush_line" facesSkipped) $(field "$brush_line" worldFaces) $(field "$brush_line" brushFaces)" >> "$SCRATCH/pin-rows.txt"
    cp "$trace" "$OUT/trace-$MAP-forward.trace" 2>/dev/null || true
  fi
done

# Reading-only maps: measured here, asserted nowhere.
for MAP in $READING_MAPS; do
  bsp_rel=$(map_bsp_rel "$MAP")
  line=$(probe_budget "$bsp_rel" "$PROBE_HUGE")
  [[ -n $line ]] || { echo "  reading $MAP: probe printed no budget line"; continue; }
  req=$(field "$line" requested)
  echo "  reading $MAP (not asserted): probe demand=$req at $PROBE_HUGE"
  if (( req > CAPACITY )); then
    echo "  WARNING $MAP needs $req triangles against a capacity of $CAPACITY: the product truncates it."
    echo "          It is not one of the four authorised maps, so this stays a reading."
  fi
done

if [[ $REPIN -eq 1 && ${WRITE_PINS:-1} -eq 1 ]]; then
  {
    echo "# T88 world triangle budget pins. Written by"
    echo "#   bash world-geometry-budget-check.sh --re-pin=$REPIN_WHY"
    echo "# on $(date -Iseconds) product sha256 $PRODUCT_SHA"
    echo "# map capacity requested dropped facesDrawn facesSkipped worldFaces brushFaces"
    cat "$SCRATCH/pin-rows.txt"
  } > "$PINS"
  echo ""
  echo "re-pinned $PINS"
  sed 's/^/  /' "$PINS"
fi

# H1: the run must not have touched the operator's real profile.
REAL_AFTER=$(real_state)
if [[ "$REAL_BEFORE" == "$REAL_AFTER" ]]; then
  pass "H1 real profile untouched by this run: $(echo "$REAL_BEFORE" | tr '\n' ' ')"
else
  failure "H1 the run MOVED the real profile: before [$(echo "$REAL_BEFORE" | tr '\n' ' ')] after [$(echo "$REAL_AFTER" | tr '\n' ' ')]"
fi

echo ""
if [[ $failures -eq 0 ]]; then echo "WORLD-GEOMETRY-BUDGET=PASS"; exit 0; fi
echo "WORLD-GEOMETRY-BUDGET=FAIL ($failures assertions)"; exit 1
