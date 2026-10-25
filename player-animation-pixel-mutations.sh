#!/usr/bin/env bash
# player-animation-pixel-mutations.sh -- prove the T61 PIXEL gate can go red.
#
# player-animation-pixels.sh asserts on the picture: the entity pass's measured
# mask is a shaded body (F3), and the animation drive moves those pixels between
# the on and off frames of the same scene tick (F4). This script reintroduces
# two real defects and requires the gate to go red for a NAMED pixel reason:
#
#   m6  the entity vertex shader stops writing the neutral blend/feature
#       values, so the shared world pixel shader reads whatever the unwritten
#       TEXCOORD4 slot happens to hold (1.0 on this driver) and the body is
#       replaced by the unbound blend atlas: the ORIGINAL black-silhouette
#       defect, reintroduced. Must red F3 (body distinct colours / black share).
#   m7  the drive still computes the cycle but the sampling request is no
#       longer rewritten (self-assignment), so --player-animation changes
#       nothing the user sees. Must red F4 (the on/off frames become identical).
#
# WHAT WOULD MAKE THIS SCRIPT DISHONEST, AND WHAT IT DOES INSTEAD
#   * a mutation that does not compile proves nothing -> the rebuild is checked
#     and the gate's own F1 freshness guard runs inside every case;
#   * a mutation caught by a MISSING trace line or a vanished counter proves
#     nothing about pixels -> both cases keep animsum and the debug view intact
#     and the named reasons required below are PIXEL assertions;
#   * a tree left holding a mutant proves nothing -> restore is cmp -s verified,
#     the file is touched, and the suite re-runs the gate at the end to prove it
#     is green again.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it builds and runs captures).
# Usage: bash player-animation-pixel-mutations.sh [case ...]  (default: m6 m7)
# Exit:  0 = every case went red for its named pixel reason and the tree came
#            back green; 1 = otherwise.
set -uo pipefail
cd "$(dirname "$0")"

GATE=player-animation-pixels.sh
OUT=evidence/t61-animation/pixel-mutations
export T61_PIXEL_OUT=evidence/t61-animation/pixels/mutations
BACKUP=.scratch/t61-pixel-mutations
mkdir -p "$OUT" "$BACKUP"

TOUCHED=()
restore_all() {
  local rc=$? entry f b
  for entry in "${TOUCHED[@]:-}"; do
    [ -n "$entry" ] || continue
    f="${entry%%|*}"; b="${entry#*|}"
    [ -f "$b" ] || continue
    cmp -s "$b" "$f" && continue
    cp -p "$b" "$f" 2>/dev/null || true
    cmp -s "$b" "$f" || echo "TRAP-RESTORE-MISMATCH $f" >&2
    touch "$f" 2>/dev/null || true
  done
  return $rc
}
trap restore_all EXIT INT TERM

patch_in() { # patch_in <file> <find> <replace>
  python - "$1" "$2" "$3" <<'PY'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
if find not in data:
    sys.exit('PATTERN NOT FOUND: ' + find[:70].replace('\r', '\\r').replace('\n', '\\n'))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PY
}

BK=""
backup_file() { BK="$BACKUP/$2.orig"; cp -p "$1" "$BK" || return 1; TOUCHED+=("$1|$BK"); }
rebuild() { bash build-target.sh tf2_demo_native; }
count_fails() { local n; n=$(grep -c '^  FAIL ' "$1" 2>/dev/null || true); printf '%s' "${n:-0}"; }
run_gate() { local log="$1"; bash "$GATE" > "$log" 2>&1; echo $?; }

fail_all=0
echo "=== baseline: the unmutated tree must be green before any case ==="
BASE_RC=$(run_gate "$OUT/baseline.log")
BASE_FAILS=$(count_fails "$OUT/baseline.log")
if [ "$BASE_RC" = "0" ] && [ "$BASE_FAILS" = "0" ]; then
  echo "  OK   baseline rc=0 FAIL-count=0 (log $OUT/baseline.log)"
  grep -E '^  OK ' "$OUT/baseline.log" | head -20 | sed 's/^/       /'
else
  echo "  FAIL baseline rc=$BASE_RC FAIL-count=$BASE_FAILS (log $OUT/baseline.log)"
  grep -E 'FAIL|FATAL' "$OUT/baseline.log" | head -20 | sed 's/^/       /'
  echo "FAIL player-animation-pixel-mutations: baseline is not green; mutations would not mean anything."
  exit 1
fi

run_case() { # run_case <tag> <file> <find> <replace> <named pixel reason>...
  local tag="$1" file="$2" find="$3" replace="$4"; shift 4
  if [ "$#" -eq 0 ]; then
    echo "  FAIL case $tag lists no named assertion: a case with an empty expectation list checks nothing, and a for-loop over an empty list prints a PASS it did not earn"
    fail_all=1; return
  fi
  local log="$OUT/$tag.log"
  echo
  echo "=== case $tag: mutate $file ==="
  backup_file "$file" "$tag" || { echo "  FAIL could not back up $file"; fail_all=1; return; }
  local backup="$BK"
  if ! patch_in "$file" "$find" "$replace"; then
    echo "  FAIL patch anchor not found in $file"; fail_all=1; return
  fi
  echo "  patched (backup $backup)"
  # A mutant that does not compile would make the gate fail on a stale binary,
  # which proves nothing about pixels. Demand a real compile.
  if ! rebuild; then
    echo "  FAIL rebuild after mutation (a non-compiling mutant is INVALID, not caught)"
    cp -p "$backup" "$file" 2>/dev/null || true; touch "$file" 2>/dev/null || true
    bash build-target.sh tf2_demo_native >/dev/null 2>&1 || true
    fail_all=1; return
  fi
  local rc fails
  rc=$(run_gate "$log")
  fails=$(count_fails "$log")
  echo "  gate rc=$rc FAIL-count=$fails (baseline $BASE_FAILS)"
  if [ "$rc" != "0" ] && [ "$fails" -gt "$BASE_FAILS" ]; then
    echo "  OK   gate went red and the FAIL count rose"
  else
    echo "  FAIL gate did not go red: rc=$rc FAIL-count=$fails"; fail_all=1
  fi
  local pattern
  for pattern in "$@"; do
    if grep -qF "$pattern" "$log"; then
      echo "  OK   named PIXEL reason present: $pattern"
    else
      echo "  FAIL named PIXEL reason missing: $pattern"
      grep -E 'FAIL' "$log" | head -10 | sed 's/^/       | /'
      fail_all=1
    fi
  done
  cp -p "$backup" "$file"
  if cmp -s "$backup" "$file"; then
    echo "  OK   restored byte-identical (cmp -s)"
  else
    echo "  FAIL restore differs from the backup"; fail_all=1
  fi
  touch "$file"
  if ! rebuild; then echo "  FAIL rebuild after restore"; fail_all=1; return; fi
}

if [ "$#" -eq 0 ]; then set -- m6 m7; fi
for want in "$@"; do
  case "$want" in
    m6)
      # The first run measured this case: m6 leaves 537 distinct colours and 84%
      # of the body pixels black, so the distinct-colours assertion (whose text is
      # "flat silhouette") stays green; the assertion that names the defect is the
      # black-share line, whose own text is "(silhouette again)". The first run's
      # log is kept as m6.log.first-expectation, so the correction is auditable.
      run_case m6 native/src/native_renderer.cpp \
'  output.blend = -1.0;
  output.blendModulate = 0.0;
  output.feature = float4(0.0, 0.0, 0.0, 0.0);
  output.tangent = float4(1.0, 0.0, 0.0, 0.0);
' \
'  // m6: the neutral writes are gone on purpose.
' \
        'body pure-black share' 'silhouette again' ;;
    m7)
      run_case m7 native/src/main.cpp \
'                request.sequence = chosen;
                request.hasCycle = true;
                request.cycle = step.cycle;
                request.hasPlaybackRate = false;
' \
'                request.sequence = request.sequence;
                request.hasCycle = request.hasCycle;
                request.cycle = request.cycle;
                request.hasPlaybackRate = false;
' \
        'animation-off moved' ;;
    *)
      echo "unknown case: $want"; fail_all=1 ;;
  esac
done

echo
echo "=== final: the tree must be green again after every restore ==="
FIN_RC=$(run_gate "$OUT/final.log")
FIN_FAILS=$(count_fails "$OUT/final.log")
if [ "$FIN_RC" = "0" ] && [ "$FIN_FAILS" = "0" ]; then
  echo "  OK   final baseline green (FAIL-count=0)"
else
  echo "  FAIL final baseline rc=$FIN_RC FAIL-count=$FIN_FAILS"; fail_all=1
fi

echo
if [ "$fail_all" = "0" ]; then
  echo "MUTATION-SUITE=PASS: every pixel case reddened a NAMED pixel assertion, the tree was restored byte-for-byte, and the final baseline is green."
else
  echo "MUTATION-SUITE=FAIL"
fi
exit "$fail_all"
