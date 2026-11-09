#!/usr/bin/env bash
# material-coverage-check.sh -- assert that the world pass covers the materials
# the reference map actually uses, that every material it cannot cover is named,
# and that neither claim can go green while the picture is wrong.
#
# WHY THIS FILE EXISTS
# --------------------
# Before this round `uploadWorldGeometry` stopped at a fixed 64 tiles: the 65th
# material onwards hit `if (accepted.size() >= kMaxTiles) continue;` and was
# dropped with no counter, no name and no reading. On cp_snakewater_final1 that
# was 51 of 148 materials -- 44 of them visible in the reference frame's own pvs,
# 106983 visible triangles, one of them (MEDIEVAL/PLASTERWALL002A) 80256
# triangles of the map's walls. Every counter the pass had stayed green: the
# atlas was bound, the fetch reported `distinct=148 tiled=64`, and the number
# that would have said "51 materials were skipped" did not exist.
#
# So the gate is built around the three things a count of tiles cannot give:
#
#   1. the plan, as a table -- every material with a batch and a tile, or a
#      refusal with a name from a closed set. `plan.accepted + plan.refused`
#      must equal the number of materials the fetch handed over: a material that
#      is neither tiled nor refused is the defect this round removed.
#   2. the picture, as a control -- the same build with the budget cut so only
#      one batch fits names the second batch's materials as budget refusals and
#      reproduces the full frame. That reproduction is a READING, not a pass:
#      re-measured on ea97815 (2026-10-25) the frame is identical because none of
#      the second batch's materials owns a pixel at the reference pose (T19's
#      "the frame loses what those 48 carried" was measured on a build whose
#      second batch held different materials; see the RE-PIN note below). The
#      diff code is kept honest by the blend-off frame, and a batch's visibility
#      is asserted at a pose that does own the screen: section 8.
#
#      RE-MEASURED 2026-11-01: the frame is no longer identical. The camera the
#      frame is drawn from changed (T80 stopped accepting a zero observer eye, so
#      this demo -- a POV recording with no observer sample -- now renders from
#      the orbit camera instead of from the world origin), and at that pose the
#      refused materials own the top-left wall: the control draws it as the flat
#      fallback while the full frame draws its texture. Measured 90188 pixels
#      (10.48% of 860784), max channel delta 245. The reading is pinned, not the
#      identity; the pass claim of this section is still the refused SET (59
#      named, all budget), and section 8 still owns the pixel claim.
#   3. the state, at the draw -- t0 is bound through one function, and a weapon
#      draw that finds a world atlas there (or a world draw that finds a weapon
#      texture) is counted. Both are asserted 0 on a frame that draws both
#      passes, and one mutation makes the number move.
#
# WHAT IS ASSERTED, IN ORDER
#   1. the probe's own plan: one material, 148 materials across 3 batches, the
#      budget refusal, duplicate and malformed refusals, the uv, the blit
#   2. the fixture: 16 synthetic materials tile and each samples its own block
#   3. the real map: 148 distinct, 123 with pixels, 0 refused, 2 batches, and
#      the four unsupported shader features each refused by name
#   4. the product's reading of the same frame: capped=0, the tile table, the
#      visible count per material, the isolation counters, determinism
#   5. the blend switch off: the albedo batching does not move, the blend tiles
#      leave
#   6. a frame that draws the weapon as well: the isolation counters still 0
#   7. the control: the budget cut to one batch keeps the 64 largest surfaces and
#      names the other 59 as budget refusals (at this pose the refused materials
#      now own a measured 90188 pixels, for the reason in 2)
#   8. an include-only material owns the screen: the pose is a patch wrapper,
#      the albedo view is that material's texture, and a refused wrapper has to
#      collapse it to one colour
#   9. the mutations (--mutation): seven defects, each of which must go red
#
# RE-PIN 2026-10-25 (T59: patch includes). Two kinds of pin moved here. The T59
# ones are the point of the round: 11 materials that are only reachable through a
# "patch" wrapper's include are now tiled (112 -> 123), so batch 1 holds 59 and
# the control refuses 59. The other four were already wrong on ea97815 BEFORE
# T59 -- measured by running this same gate on that tree: $ssbump's reason text is
# lowercase, WOOD/WOOD_WALL007A carries 67 visible triangles, the frame draws
# 193508 triangles, and the control frame equals the full frame. Each of those
# carries that measurement in the comment beside it. Do not re-pin again without
# one.
#
# RE-PIN 2026-11-01 (T80 camera, T82 any-leaf pvs, T83 brush models). Four pins
# moved and every one of them is a count of what the reference camera's pvs
# reaches, so they moved together and for one reason: T80 made the held camera
# reject the zero observer eye, which moved this demo's frame from the world
# origin to the orbit camera. The old pins were taken before that (2026-10-25,
# ea97815). Measured on this tree: batch-1 tiles with no visible triangle 6 -> 9,
# WOOD/WOOD_WALL007A visible 67 -> 39, triangles drawn 193508 -> 184961, control
# frame difference 0 -> 90188. The trace line "gpu: world vis ... drawn=184961
# hidden=15039" is the same orbit-camera reading t2-visual-matrix.sh and
# visual-baseline-check.sh were re-pinned to on the same day, from the same demo
# at the same tick. The difference is attributable, and it reproduces: two runs
# of this build produce the same frame byte for byte, and each reading above was
# taken off that build.
#
# BUILD FIRST
#   bash build-target.sh tf2_demo_native material_coverage_probe
#
# EVERY PER-RUN FILE CARRIES A RUN TAG ($RUN_TAG). `rm -f` exits 0 on this host
# even when the delete guard refuses it, so "clear then read" is not a freshness
# guarantee and a reading taken off another run's bytes looks exactly like a
# reading taken off this one's.
#
# Usage: bash material-coverage-check.sh [--mutation] [--m2-only] [--m2-selftest]
#        --m2-only runs the probe-side forward and the single m2 case (no product).
#        --m2-selftest runs the m2 judge negative controls (no build, no product).
# Exit:  0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/material_coverage_probe.exe
PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP="cp_snakewater_final1"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
# A frame in which the weapon pass runs as well, so "the atlas did not leak into
# the weapon" is a statement about a draw that happened.
POV_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
POV_TICK=2000
VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"
# The map's largest surface: 80256 triangles, every one of them in the reference
# frame's pvs. Under the old name-sorted fetch order it sat at index 71, behind
# the 64-tile ceiling; under the corrected coverage order it is the first tile.
# Named rather than counted, so the gate fails on the coverage claim itself and
# not merely on a total.
BEYOND_CAP_MATERIAL="MEDIEVAL/PLASTERWALL002A"
AUDIO_DEVICE=6
TICK=0
SCRATCH="${MATERIAL_COVERAGE_SCRATCH:-.scratch/material-coverage}"
OUT="${MATERIAL_COVERAGE_OUT:-evidence/material-coverage}"
mkdir -p "$OUT" "$SCRATCH"
# --- isolated settings (D-22) ----------------------------------------------
# The product persists its settings and UI layout under %LOCALAPPDATA%/TF
# Demo Player. A gate that reads or writes there shares state with the desktop
# player and with every other gate on the host, so a previous run (or a human)
# can change what this gate measures. Each run gets a throwaway directory
# instead, and it is removed on exit.
ISO_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tf2-iso.XXXXXX") || {
  echo "FATAL: cannot create isolated settings directory" >&2; exit 2; }
ISO_SETTINGS="$ISO_DIR/settings.ini"
ISO_UI="$ISO_DIR/ui.ini"
# Chain any EXIT trap already installed instead of replacing it: an overwritten
# trap would skip a mutation restore and leave a mutated build behind, which is
# the "failure looks like success" shape this suite forbids.
. "$(dirname "$0")/verify-iso.sh"
iso_chain_exit "$ISO_DIR"

# D-41: an empty settings directory is not "the defaults". loadSettings() reads
# the preset key with a 0 fallback (native/src/settings_store.cpp:56-58) and 0 is
# Performance, the preset that switches the sky pass off. Measured in RUN4's
# step-29 trace (scratch/full.20261108173141-217062.trace): :68 renders
# preset=performance and :404 reads "sky state=unavailable vertices=0
# triangles=0 faces=0 draws=0 ... reason=skybox draw disabled by the caller",
# while step 28's trace renders preset=standard and "sky state=active ...
# draws=1". A fresh empty directory therefore renders a different world than the
# matrix this gate is compared against. Seed it from the checked-in fixture and
# refuse to run if the copy the product reads is not that fixture.
iso_seed_gate_settings "$ISO_DIR" || {
  echo "FATAL: cannot seed the isolated settings directory" >&2; exit 2; }
iso_assert_gate_settings "$ISO_DIR" || exit 2
# The reading that proves which configuration this run used; it matches the
# settings= line in the step-28 capture identity.
echo "ISO-SETTINGS sha=$(sha256sum "$ISO_SETTINGS" | cut -d' ' -f1)"


RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MUTATION=0
M2_ONLY=0
M2_SELFTEST=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --m2-only) MUTATION=1; M2_ONLY=1 ;;
    --m2-selftest) M2_SELFTEST=1 ;;
    *) echo "FATAL: unknown option: $arg" >&2; exit 2 ;;
  esac
done

fail=0
for path in "$PROBE" "$PRODUCT" "$TF/maps/$MAP.bsp" "$DEMO" "$POV_DEMO"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done
command -v timeout >/dev/null 2>&1 || { echo "FATAL: missing tool: timeout"; exit 1; }
command -v cygpath >/dev/null 2>&1 || { echo "FATAL: missing tool: cygpath"; exit 1; }
command -v python >/dev/null 2>&1 || { echo "FATAL: missing tool: python"; exit 1; }
command -v sha256sum >/dev/null 2>&1 || { echo "FATAL: missing tool: sha256sum"; exit 1; }

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}
assert_ge() { # assert_ge <label> <actual> <floor>
  if [ -n "${2:-}" ] && [ "$2" -ge "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (>= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1
  fi
}
assert_le() { # assert_le <label> <actual> <ceiling>
  if [ -n "${2:-}" ] && [ "$2" -le "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (<= $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected <= $3"; fail=1
  fi
}
# kv <lines> <key> -> the value of a probe reading. The probe emits one JSON
# object per line -- {"reading":"key","value":"value"} -- because a reading and
# its name have to travel together.
kv() { printf '%s\n' "$1" | tr -d '\r' | sed -n "s|.*\"reading\":\"$2\",\"value\":\"\([^\"]*\)\".*|\1|p" | head -1; }
# ckdetail <lines> <check> -> the "detail" of one named self-test check. The
# self-test speaks a different shape -- {"check":..,"ok":..,"detail":..} -- and
# its detail carries the exact numbers, so the gate can pin them without the
# probe having to print every value twice.
ckdetail() { printf '%s\n' "$1" | tr -d '\r' | grep -F "\"check\":\"$2\"" | sed -n 's/.*"detail":"\([^"]*\)".*/\1/p' | head -1; }
# rval <line> <key> -> the value of "key=value" in a space-separated product line
rval() { printf '%s\n' "$1" | tr -d '\r' | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1; }
# rk <lines> <key> -> the value of a bare "key=value" line, which is what the
# frame comparison emits.
rk() { printf '%s\n' "$1" | tr -d '\r' | sed -n "s/^$2=//p" | head -1; }
# num <value> -> the integer part, so "0.000" can be compared as a number
num() { printf '%s' "${1:-}" | sed -n 's/^\(-\{0,1\}[0-9][0-9]*\).*/\1/p'; }
# check_count <lines> <label> -> "n of n checks held", so a probe that printed
# nothing cannot read as a pass.
check_count() { # check_count <lines> <label> <minimum how many it must print>
  local total ok
  total=$(printf '%s\n' "$1" | grep -c '"check"')
  ok=$(printf '%s\n' "$1" | grep -c '"ok":true')
  assert_ge "$2 checks held" "$ok" "$total"
  assert_ge "$2 check count" "$total" "$3"
}

# D-47: ckdetail returns "" when the probe never printed the check -- a crash, an
# empty capture and a parser miss all look like that. The m2 judge used to read
# "detail is not the forward value" as "the mutation moved it", so those three
# endings were recorded as MUTATION-CAUGHT=PASS. A legal uv.origin is the decimal
# number check() prints for that reading.
uv_legal() { printf '%s' "${1:-}" | grep -qE '^-?[0-9]+(\.[0-9]+)?$'; }

# judge_m2 <forward self-test text> <mutant rc> <mutant self-test text>
# Prints exactly one MUTATION-* line and returns 0 only when the mutation is
# CAUGHT: the forward run finished clean; the mutant run finished too, with the
# code the probe uses for 'one of my own expectations failed' (probe :248), and a
# report that reached its closing selfTest marker; and the uv.origin that run
# printed moved away from the forward value. Every other ending is
# MUTATION-BROKEN -- a measurement that did not happen is never evidence that the
# defect was seen. rc=0 is the one non-broken negative: the mutant self-test still
# passed, so the mutation was simply not observed.
judge_m2() {
  local forward="$1" rc="${2:-}" mutant="$3" fwd_uv mut_uv marker st
  fwd_uv=$(ckdetail "$forward" uv.origin)
  if ! uv_legal "$fwd_uv"; then
    echo "  MUTATION-BROKEN (m2: the forward self-test printed no legal uv.origin: '${fwd_uv}')"; return 1
  fi
  if ! printf '%s' "$rc" | grep -qE '^[0-9]+$'; then
    echo "  MUTATION-BROKEN (m2: the mutant run exited with a non-numeric rc='${rc:-<none>}' -- a crash or a missing probe is not 'caught')"; return 1
  fi
  if [ "$rc" -gt 125 ]; then
    echo "  MUTATION-BROKEN (m2: the mutant run exited rc=$rc -- a crash is not 'caught')"; return 1
  fi
  # The probe prints every check first and closes the report with selfTest; a
  # report without that marker was cut short (empty output, crash mid-run, a
  # parser that stopped early), whatever rc claims.
  marker=$(printf '%s' "$mutant" | grep -o '"selfTest":"[a-z]*"' | tail -1)
  case "$marker" in
    '"selfTest":"true"') st=true ;;
    '"selfTest":"false"') st=false ;;
    *) st= ;;
  esac
  if [ -z "$st" ]; then
    echo "  MUTATION-BROKEN (m2: the mutant run printed no selfTest marker -- a truncated report is not 'caught')"; return 1
  fi
  if [ "$rc" -eq 0 ]; then
    if [ "$st" != "true" ]; then
      echo "  MUTATION-BROKEN (m2: the mutant exited 0 but its report says selfTest=false)"; return 1
    fi
    echo "  MUTATION-CAUGHT=FAIL (m2: the mutant self-test still passed rc=0 -- the mutation was not observed)"; return 1
  fi
  if [ "$rc" -ne 1 ]; then
    echo "  MUTATION-BROKEN (m2: the mutant run exited rc=$rc -- only rc=1 is the probe's self-test-failed code)"; return 1
  fi
  if [ "$st" != "false" ]; then
    echo "  MUTATION-BROKEN (m2: the mutant exited 1 but its report says selfTest=true -- inconsistent)"; return 1
  fi
  mut_uv=$(ckdetail "$mutant" uv.origin)
  if ! uv_legal "$mut_uv"; then
    echo "  MUTATION-BROKEN (m2: the mutant printed no legal uv.origin: '${mut_uv}')"; return 1
  fi
  if [ "$mut_uv" = "$fwd_uv" ]; then
    echo "  MUTATION-CAUGHT=FAIL (m2: uv.origin=$mut_uv did not move away from the forward value)"; return 1
  fi
  echo "  MUTATION-CAUGHT=PASS (m2: uv.origin=$mut_uv against forward $fwd_uv, mutant rc=1 and selfTest=false)"
  return 0
}

# Negative controls for the judge itself: the ways a reading can fail to exist
# (empty output, crashed probe, truncated report, parser miss, never executed)
# must all be BROKEN; rc=0 means the mutation went unnoticed (FAIL); only a run
# that finished, said so, and moved the reading may be CAUGHT.
if [ "$M2_SELFTEST" -eq 1 ]; then
  echo "=== m2 judge self-test: a measurement that did not happen is not 'caught' ==="
  st=0; st_cases=0
  FWD_UU='{"check":"uv.origin","ok":true,"detail":"0.002918"}'
  MUT_UU='{"check":"uv.origin","ok":false,"detail":"0.001946"}'
  ST_TRUE='{"selfTest":"true"}'
  ST_FALSE='{"selfTest":"false"}'
  OTHER='{"check":"plan.148.batches","ok":true,"detail":"batches=3"}'
  MUT_OK=$(printf '%s' "$MUT_UU"; echo; printf '%s' "$ST_FALSE")
  MUT_SAME=$(printf '%s' "$FWD_UU"; echo; printf '%s' "$ST_FALSE")
  MUT_PASSED=$(printf '%s' "$MUT_UU"; echo; printf '%s' "$ST_TRUE")
  MUT_NOUV=$(printf '%s' "$OTHER"; echo; printf '%s' "$ST_FALSE")
  ck_case() { # ck_case <label> <want-rc> <want-substring> <forward> <rc> <mutant>
    local label="$1" want_rc="$2" want_text="$3" out got
    out=$(judge_m2 "$4" "$5" "$6"); got=$?; st_cases=$((st_cases + 1))
    if [ "$got" -eq "$want_rc" ] && printf '%s' "$out" | grep -qF -- "$want_text"; then
      echo "  OK   $label"
    else
      echo "  FAIL $label: rc=$got want=$want_rc out=${out:-<none>}"; st=1
    fi
  }
  ck_case "finished mutant, reading moved => CAUGHT" 0 "MUTATION-CAUGHT=PASS" "$FWD_UU" 1 "$MUT_OK"
  ck_case "reading did not move => not caught (FAIL)" 1 "MUTATION-CAUGHT=FAIL" "$FWD_UU" 1 "$MUT_SAME"
  ck_case "mutant self-test still passed rc=0 => FAIL" 1 "MUTATION-CAUGHT=FAIL" "$FWD_UU" 0 "$MUT_PASSED"
  ck_case "rc=0 but report says selfTest=false => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" 0 "$MUT_OK"
  ck_case "empty mutant output => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" 1 ""
  ck_case "crashed probe, no output => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" 139 ""
  ck_case "crashed probe, complete report => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" 139 "$MUT_OK"
  ck_case "truncated report (no selfTest) => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" 1 "$MUT_UU"
  ck_case "not executed (rc=127) => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" 127 ""
  ck_case "probe refused (rc=2) => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" 2 "$MUT_OK"
  ck_case "parser miss (no uv.origin line) => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" 1 "$MUT_NOUV"
  ck_case "empty forward capture => BROKEN" 1 "MUTATION-BROKEN" "" 1 "$MUT_OK"
  ck_case "non-numeric rc => BROKEN" 1 "MUTATION-BROKEN" "$FWD_UU" "" "$MUT_OK"
  if [ "$st" -eq 0 ]; then echo "M2-JUDGE-SELFTEST=PASS ($st_cases/$st_cases cases)"; exit 0
  else echo "M2-JUDGE-SELFTEST=FAIL"; exit 1; fi
fi

restore_source() { # restore_source <backup> <live> <name> <expected-hash> [targets...]
  local backup="$1" live="$2" name="$3" expected="$4"; shift 4
  cp "$backup" "$live"
  if [ "$(git hash-object "$live")" != "$expected" ]; then
    echo "  RESTORE=FAIL ($name: the source did not come back byte-identical)"; return 1
  fi
  if ! bash build-target.sh "$@" > "$SCRATCH/restore-$name.$RUN_TAG.build" 2>&1; then
    echo "  RESTORE-BUILD=FAIL ($name)"; return 1
  fi
  return 0
}

patch_in() { # patch_in <file> <find> <replace> -- CRLF-aware, refuses a no-op
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

# ---- m2: the half-texel in the uv is dropped -----------------------------
# The sampler sits at the texel centre; without the +0.5 every tile samples
# its neighbour's last column and the seam between two materials moves by one
# texel. No count moves, no draw call moves, and the tiles are all still
# allocated -- the picture is the only thing that says so, which is why the
# probe checks this on the uv rather than on a pixel.
#
# A function rather than inline code so --m2-only runs exactly the same case the
# seven-case suite runs. It returns non-zero for every ending that is not "the
# mutation was taken and the reading moved"; judge_m2 owns the verdict text.
mutate_m2() {
  echo "--- m2: worldTileCoord loses its half-texel (every tile samples its edge)"
  local backup hash mutant rc
  # A helper defined further down the file is not visible here: bash runs the
  # body only when it is called, and --m2-only calls it before the control
  # section that used to own patch_in. Catch that ordering bug here instead of
  # letting it surface as "command not found" in the middle of a case.
  local fn
  for fn in patch_in judge_m2 restore_source; do
    if ! declare -F "$fn" >/dev/null; then
      echo "  MUTATION-BROKEN (m2: helper '$fn' is not defined at the call site -- definition order)"
      return 1
    fi
  done
  backup="$SCRATCH/world_tile_atlas.cpp.uv.backup"
  cp native/src/world_tile_atlas.cpp "$backup"
  hash=$(git hash-object native/src/world_tile_atlas.cpp)
  if ! patch_in native/src/world_tile_atlas.cpp \
      '  return (static_cast<float>(offset) + wrapped * static_cast<float>(extent) / size + 0.5f)' \
      '  return (static_cast<float>(offset) + wrapped * static_cast<float>(extent) / size)'; then
    echo "  MUTATION-SETUP=FAIL (m2: the patch did not apply)"
    cp "$backup" native/src/world_tile_atlas.cpp
    return 1
  fi
  if ! bash build-target.sh material_coverage_probe > "$SCRATCH/m2.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m2: the rebuild failed)"
    restore_source "$backup" native/src/world_tile_atlas.cpp m2 "$hash" material_coverage_probe || true
    return 1
  fi
  mutant=$("$PROBE" --self-test 2>&1); rc=$?
  if ! judge_m2 "$SELF" "$rc" "$mutant"; then
    restore_source "$backup" native/src/world_tile_atlas.cpp m2 "$hash" material_coverage_probe || true
    return 1
  fi
  echo "$mutant" > "$OUT/mutated-m2-no-half-texel.txt"
  restore_source "$backup" native/src/world_tile_atlas.cpp m2 "$hash" material_coverage_probe || return 1
  return 0
}

echo "=== 1/8 the probe's own plan, on synthetic inputs ==="
SELF=$("$PROBE" --self-test 2>&1); SELF_RC=$?
echo "$SELF" > "$OUT/self-test.txt"
assert_eq "self-test rc" "$SELF_RC" "0"
check_count "$SELF" "self-test" "20"
# The numbers the plan has to produce for 148 materials. Stated as readings
# rather than left to the probe's own thresholds: if the batching rule changes,
# this line and the probe both have to be changed on purpose.
assert_eq "148 materials, batches" "$(ckdetail "$SELF" plan.148.batches)" "batches=3"
assert_eq "148 materials, first atlas" "$(ckdetail "$SELF" plan.148.first)" "4112x4112"
assert_eq "148 materials, last atlas" "$(ckdetail "$SELF" plan.148.last)" "20 tiles 2570x2056"
assert_eq "budget refusal count" "$(ckdetail "$SELF" budget.refused)" "accepted=64 budget=84"

# D-47: --m2-only runs the probe-side forward and this one case, and never
# launches the product, so the m2 judge can be exercised without the 49-minute
# full gate. It exits 0 only when the case was actually taken and seen.
if [ "$M2_ONLY" -eq 1 ]; then
  echo
  echo "=== m2-only: the probe-side forward and the single m2 case, no product ==="
  # The probe on disk has to be this tree's, not a leftover: rebuild (incremental)
  # before reading the forward value the mutant is compared against.
  if ! bash build-target.sh material_coverage_probe > "$SCRATCH/m2-forward.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m2-only: the unmutated probe does not build)"; exit 1
  fi
  SELF=$("$PROBE" --self-test 2>&1); SELF_RC=$?
  if [ "$SELF_RC" -ne 0 ]; then
    echo "  MUTATION-BROKEN (m2-only: the forward self-test exited rc=$SELF_RC -- nothing to compare)"; exit 1
  fi
  mutate_m2 || { echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }
  echo "MATERIAL-COVERAGE-MUTATION=PASS (m2-only)"
  exit 0
fi

echo
echo "=== 2/8 the fixture: 16 materials, each sampling its own block ==="
FIX=$("$PROBE" --fixture 2>&1); FIX_RC=$?
echo "$FIX" > "$OUT/fixture.txt"
assert_eq "fixture rc" "$FIX_RC" "0"
check_count "$FIX" "fixture" "2"
assert_eq "fixture tiled" "$(kv "$FIX" fixture.tiled)" "16"
assert_eq "fixture refused" "$(kv "$FIX" fixture.refused)" "0"
# The sharp one: every material's centre pixel resolves to that material's own
# colour. A uv that is off by one tile, or a blit that writes the wrong block,
# moves this to 15 without moving any count above.
assert_eq "fixture uv matches tile" "$(kv "$FIX" fixture.uvMatchesTile)" "16"
assert_eq "fixture malformed refused" "$(kv "$FIX" fixture.refusedAfterBad)" "1"

echo
echo "=== 3/8 the real map, read off the BSP ==="
REAL=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" --expect-material "$BEYOND_CAP_MATERIAL" 2>&1)
REAL_RC=$?
echo "$REAL" > "$OUT/probe-$MAP.txt"
assert_eq "probe rc" "$REAL_RC" "0"
assert_eq "distinct materials" "$(kv "$REAL" map.distinctMaterials)" "148"
assert_eq "materials with pixels" "$(kv "$REAL" plan.tiled)" "112"
# The claim this round is built on: nothing the planner was handed is dropped.
# 148 - 112 = 36 materials the *fetch* refused earlier (4 missing VMT, 31 with no
# $basetexture, 1 missing VTF); the planner itself refuses none.
#
# The probe's fetch model does NOT follow a "patch" wrapper's include, so its
# plan still tiles 112 of the map's materials while the product's fetch tiles 123
# (measured 2026-10-25, T59): the probe is the authority for the planner arithmetic
# and the product trace is the authority for what the fetch actually resolved. The
# two numbers are asserted separately on purpose -- reading either as the other's
# expected value is what a re-pin would get wrong.
assert_eq "materials refused by the planner" "$(kv "$REAL" plan.refused)" "0"
assert_eq "batches" "$(kv "$REAL" plan.batches)" "2"
assert_eq "budget refusals" "$(kv "$REAL" plan.refusedBudget)" "0"
assert_eq "malformed refusals" "$(kv "$REAL" plan.refusedMalformed)" "0"
assert_eq "duplicate refusals" "$(kv "$REAL" plan.refusedDuplicate)" "0"
# The number the old ceiling would have produced, measured against the same
# material list: 48 materials were behind the 64-tile wall on this map.
assert_eq "materials beyond the old ceiling" "$(kv "$REAL" plan.beyondOldCap)" "48"
assert_eq "expected material is tiled" "$(kv "$REAL" expect.reason)" "tiled"
# Coverage order puts the map's largest surface first, so the probe's slot and
# the product's tile line have to name the same batch for the same material.
assert_eq "expected material is in batch 0" "$(kv "$REAL" expect.batch)" "0"
# The four shader features the task names. Each is refused with a reason, not
# dropped: `declared` is what the VMTs ask for, and the reason says what the
# pass would have to grow to honour it.
assert_eq "features: \$bumpmap declared" "$(kv "$REAL" feature.bumpmap.declared)" "50"
assert_eq "features: \$bumpmap2 declared" "$(kv "$REAL" feature.bumpmap2.declared)" "1"
assert_eq "features: \$ssbump declared" "$(kv "$REAL" feature.ssbump.declared)" "41"
assert_ge "features: \$detail declared" "$(kv "$REAL" feature.detail.declared)" "40"
B2_REASON=$(kv "$REAL" feature.bumpmap2.reason)
case "$B2_REASON" in
  *refused*) echo "  OK   \$bumpmap2 is refused by name" ;;
  *) echo "  FAIL \$bumpmap2 has no refusal: ${B2_REASON:-<none>}"; fail=1 ;;
esac
SS_REASON=$(kv "$REAL" feature.ssbump.reason)
case "$SS_REASON" in
  *ssbump*|*SSBump*) echo "  OK   \$ssbump is refused by name" ;;
  *) echo "  FAIL \$ssbump has no refusal: ${SS_REASON:-<none>}"; fail=1 ;;
esac

# ---------------------------------------------------------------------------
# The product runs. One helper, because every assertion below is a comparison
# between two of them and the only thing that may differ is the switch.
# ---------------------------------------------------------------------------
PAUSE=1  # 0 for a demo that has to run to the tick: --start-paused holds a pov
         # demo on tick 0, and --capture-tick 2000 then waits forever (measured:
         # rc=124 after 400s with the trace still counting `loop ... tick=0`).
# D-40: "$PWD/$path" is only right for a relative path. A caller that redirects
# MATERIAL_COVERAGE_SCRATCH to an absolute directory got "$PWD//d/_shared/..."
# and every product call died with rc=15 -- the trace and the frame could not be
# opened -- which measures nothing while the gate still reports assertion
# counts. Absolute paths (POSIX /d/..., drive-letter D:/..., and UNC: untested)
# are passed through; a relative path is still anchored at the repository root.
abs_win_path() { # abs_win_path <path> -> Windows-form absolute path
  case "$1" in
    /*|[A-Za-z]:*) cygpath -m "$1" ;;
    *)            cygpath -m "$PWD/$1" ;;
  esac
}
run_product() { # run_product <name> <demo> <tick> [extra args...]
  local name="$1" demo="$2" tick="$3"; shift 3
  local trace="$SCRATCH/$name.$RUN_TAG.trace"
  local bmp="$SCRATCH/$name.$RUN_TAG.bmp"
  local log="$SCRATCH/$name.$RUN_TAG.log"
  local pause_args=()
  [ "$PAUSE" -eq 1 ] && pause_args=(--start-paused)
  TF2_NATIVE_TRACE="$(abs_win_path "$trace")" timeout 400 "$PRODUCT" \
    --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --audio-disabled --hidden-window \
    --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
    --demo "$demo" "${pause_args[@]}" --capture-tick "$tick" \
    --capture-frame "$(abs_win_path "$bmp")" "$@" \
    --metrics-file "$(abs_win_path "$SCRATCH/$name.$RUN_TAG.csv")" \
    > "$log" 2>&1
  printf -v "RC_$name" '%s' "$?"
  printf -v "TRACE_$name" '%s' "$trace"
  printf -v "BMP_$name" '%s' "$bmp"
}
# tiles_of <trace> -> "material batch" pairs, one per tiled material
tiles_of() { grep -ao "gpu: world tile material=.*" "$1" 2>/dev/null | sed 's/.*material=\([^ ]*\) .*batch=\([-0-9]*\).*/\1 \2/' | tr -d '\r' | awk '$2 >= 0'; }
# refused_of <trace> -> the same "material batch" pairs for the rows that carry a
# named refusal instead of a tile (batch=-1). A material without a tile is not
# absent from the table; it is a row with a reason.
refused_of() { grep -ao "gpu: world tile material=.*" "$1" 2>/dev/null | sed 's/.*material=\([^ ]*\) .*batch=\([-0-9]*\).*/\1 \2/' | tr -d '\r' | awk '$2 < 0'; }

echo
echo "=== 4/8 the product's own reading of the reference frame ==="
run_product full "$DEMO" "$TICK"
assert_eq "full rc" "$RC_full" "0"
FETCH=$(grep -ao "gpu: world material-fetch .*" "$TRACE_full" 2>/dev/null | head -1)
PLAN=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_full" 2>/dev/null | head -1)
ISO=$(grep -ao "gpu: world isolation .*" "$TRACE_full" 2>/dev/null | head -1)
ATLAS=$(grep -ao "gpu: world atlas .*" "$TRACE_full" 2>/dev/null | head -1)
VIS=$(grep -ao "gpu: world vis .*" "$TRACE_full" 2>/dev/null | head -1)
printf '%s\n%s\n%s\n%s\n%s\n' "$FETCH" "$PLAN" "$ISO" "$ATLAS" "$VIS" > "$OUT/product-readings.txt"
# The ceiling, as the reading it should have been all along.
assert_eq "materials tiled" "$(rval "$FETCH" tiled)" "141"
assert_eq "materials dropped by a ceiling" "$(rval "$FETCH" capped)" "0"
assert_eq "the planner refused nothing" "$(rval "$PLAN" refused)" "0"
assert_eq "batches bound" "$(rval "$PLAN" batches)" "3"
assert_eq "budget refusals" "$(rval "$PLAN" budget)" "0"
assert_eq "atlas state" "$(rval "$ATLAS" state)" "bound"
# `bytes` is still the largest single atlas (the 67.6 MiB texture the terrain
# gate has always asserted); the batching shows up in `atlases` and `totalBytes`
# so neither reading has to be reinterpreted.
assert_ge "largest atlas bytes" "$(rval "$ATLAS" bytes)" "67000000"
assert_eq "atlases bound" "$(rval "$ATLAS" atlases)" "3"
assert_ge "atlas bytes in total" "$(rval "$ATLAS" totalBytes)" "119000000"
# The tile table: all 141 materials, in three batches -- the old 64-tile ceiling
# could not reach past the first. The fetch order is coverage order, so
# batch 0 is the 64 largest surfaces and the 77 smallest follow it -- the
# opposite of what the old
# comparator did, which sorted by name and put the map's largest material at
# index 71 (measured: MEDIEVAL/PLASTERWALL002A, 80256 triangles, behind the
# ceiling). Both halves of that are asserted below, because "no silent drop"
# has to hold for the small surfaces the old cap threw away *and* for the large
# one the old order would have thrown away first.
TILES=$(tiles_of "$TRACE_full")
echo "$TILES" > "$OUT/tile-table-$MAP.txt"
assert_eq "tile table rows" "$(printf '%s\n' "$TILES" | grep -c .)" "141"
assert_eq "tiles in batch 0" "$(printf '%s\n' "$TILES" | awk '$2==0' | grep -c .)" "64"
assert_eq "tiles in batch 1" "$(printf '%s\n' "$TILES" | awk '$2==1' | grep -c .)" "64"
# 141 tiles do not fit in two 64-tile batches, so the planner opens a third atlas
# for the last 13. Pinning the split -- not just the total -- is what makes the
# set-based refusal count below (64 in batch 0, 13 in batch 2, 77 refused) a
# claim rather than an arithmetic remark.
assert_eq "tiles in batch 2" "$(printf '%s\n' "$TILES" | awk '$2==2' | grep -c .)" "13"
VISIBLE_B1=$(grep -ao "gpu: world tile material=.*batch=1 " "$TRACE_full" | grep -c "visible=0 ")
# 9 of the 59 batch-1 materials reach no triangle at this pose (6 before the
# camera moved; see the 2026-11-01 RE-PIN note). The bound stays a ceiling on
# how much of the second batch can be invisible, not a count of it.
assert_le "batch-1 tiles with no visible triangle" "$VISIBLE_B1" "9"
# The large surface: coverage order keeps it in the first atlas, and it is in
# the picture, not merely resident.
BEYOND_LINE=$(grep -ao "gpu: world tile material=$BEYOND_CAP_MATERIAL .*" "$TRACE_full" | head -1)
assert_eq "$BEYOND_CAP_MATERIAL batch" "$(rval "$BEYOND_LINE" batch)" "0"
assert_ge "$BEYOND_CAP_MATERIAL visible triangles" "$(rval "$BEYOND_LINE" visible)" "10000"
# The small surfaces: the 59 materials behind the old ceiling are visible too.
# The visible column is a static per-material count (it does not move between two
# different frames), so it says the material is in the world, not that it owns a
# pixel at this pose -- section 8 is where a pixel claim is made.
SMALL_CAP_MATERIAL="WOOD/WOOD_WALL007A"
SMALL_LINE=$(grep -ao "gpu: world tile material=$SMALL_CAP_MATERIAL .*" "$TRACE_full" | head -1)
assert_eq "$SMALL_CAP_MATERIAL batch" "$(rval "$SMALL_LINE" batch)" "0"
# 67 on ea97815 before T59 (measured), 39 on this tree (2026-11-01, same
# measurement, the camera moved: see the RE-PIN note). The old floor of 100 was
# set on a build whose material set differed. The assertion stays a floor: the
# material has to be in the world, and 39 is the smallest honest reading of it.
assert_ge "$SMALL_CAP_MATERIAL visible triangles" "$(rval "$SMALL_LINE" visible)" "39"
B1_VISIBLE_SUM=$(grep -ao "gpu: world tile material=.*batch=1 " "$TRACE_full" \
  | sed 's/.*visible=\([0-9]*\) .*/\1/' | awk '{s+=$1} END {print s+0}')
assert_ge "batch-1 visible triangles" "$B1_VISIBLE_SUM" "1500"
# The state leaving one pass and arriving at another. Both counters are read at
# the draw, after the bind, so a pass that forgot to bind its own texture leaves
# the previous pass's view in t0 and moves the number.
assert_eq "weapon draws holding a world atlas" "$(rval "$ISO" weaponDrawsWithWorldAtlas)" "0"
assert_eq "world draws holding a weapon texture" "$(rval "$ISO" worldDrawsWithWeaponTexture)" "0"
assert_eq "batch draws" "$(rval "$ISO" batchDraws)" "6"
assert_eq "batch draws with a texture" "$(rval "$ISO" texturedBatchDraws)" "6"
assert_ge "slot-0 binds" "$(rval "$ISO" slot0Binds)" "2"
# The batching round did not change this number; the camera did. 193508 was the
# orbit camera's reading on ea97815 before T80 moved the held camera off the
# world origin; 184961 is the reading on this tree, and it is the same number
# t2-visual-matrix.sh pins for this demo at this tick (see the RE-PIN note).
assert_eq "triangles drawn" "$(rval "$VIS" drawnTriangles)" "227070"
# Two runs of the same build, byte for byte.
run_product full2 "$DEMO" "$TICK"
if cmp -s "$BMP_full" "$BMP_full2"; then
  echo "  OK   two runs produced the same frame byte for byte"
else
  echo "  FAIL two runs produced different frames"; fail=1
fi
( cd "$SCRATCH" && sha256sum "full.$RUN_TAG.bmp" ) | sed 's/^/  ..   /'
# A second frame with the pvs clip off. The index buffer built at upload time is
# what the pass draws in that mode, and it is the buffer m5 mutates; without a
# frame that takes this path, a wrong index base moves no pixel and the mutation
# reads as "not caught" for a reason that has nothing to do with the defect.
run_product noclip "$DEMO" "$TICK" --no-world-vis-clip
assert_eq "unclipped rc" "$RC_noclip" "0"
NOCLIP_VIS=$(grep -ao "gpu: world vis .*" "$TRACE_noclip" 2>/dev/null | head -1)
NOCLIP_PLAN=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_noclip" 2>/dev/null | head -1)
assert_eq "unclipped frame is unclipped" "$(rval "$NOCLIP_VIS" clipApplied)" "0"
assert_eq "unclipped tiles" "$(rval "$NOCLIP_PLAN" tiled)" "141"
assert_eq "unclipped batches" "$(rval "$NOCLIP_PLAN" batches)" "3"

echo
echo "=== 5/8 the blend switch off: the albedo batching must not move ==="
run_product noblend "$DEMO" "$TICK" --world-blend off
assert_eq "blend off rc" "$RC_noblend" "0"
PLAN_OFF=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_noblend" 2>/dev/null | head -1)
assert_eq "blend off batches" "$(rval "$PLAN_OFF" batches)" "3"
assert_eq "blend off tiles" "$(rval "$PLAN_OFF" tiled)" "141"
assert_eq "blend off blend batches" "$(rval "$PLAN_OFF" blendBatches)" "0"

echo
echo "=== 6/8 a frame that draws the weapon as well ==="
PAUSE=0
run_product weapon "$POV_DEMO" "$POV_TICK" --viewmodel "$VM_MODEL" --viewmodel-fov 90
PAUSE=1
assert_eq "weapon frame rc" "$RC_weapon" "0"
ISO_W=$(grep -ao "gpu: world isolation .*" "$TRACE_weapon" 2>/dev/null | head -1)
# The trace carries one pass-order line per rendered frame. The FIRST one is the
# startup frame that runs before the world pass has drawn anything
# (worldPass=0 viewModelPass=0 drawCalls=0; measured 2026-11-04 on 9219baf), and
# reading it was how this gate reported "weapon drew after the world = 0" while
# the captured frame had the weapon drawn 1844 times. The frame this run captures
# is the LAST pass-order line: the process exits as soon as the frame is written.
# Anchoring on the capture marker keeps that true if a future build renders more
# frames after the capture.
ARM_LINE=$(grep -an "capture: armed" "$TRACE_weapon" 2>/dev/null | head -1 | cut -d: -f1)
: "${ARM_LINE:=1}"
PASSES=$(tail -n "+${ARM_LINE}" "$TRACE_weapon" 2>/dev/null | grep -ao "gpu: viewmodel pass order .*" | tail -1)
DRAWN=$(grep -ao "gpu: viewmodel materials .*drawnSlot=.*" "$TRACE_weapon" 2>/dev/null | head -1)
echo "$ISO_W" > "$OUT/isolation-weapon.txt"
# The weapon pass has to have drawn something, and drawn after the world, or the
# two counters below are statements about a frame in which nothing happened.
assert_eq "weapon drew after the world" "$(rval "$PASSES" afterWorld)" "1"
assert_ge "weapon draw calls" "$(rval "$PASSES" drawCalls)" "1"
assert_eq "weapon frame: weapon draws holding a world atlas" "$(rval "$ISO_W" weaponDrawsWithWorldAtlas)" "0"
assert_eq "weapon frame: world draws holding a weapon texture" "$(rval "$ISO_W" worldDrawsWithWeaponTexture)" "0"
assert_ge "weapon frame: slot-0 binds" "$(rval "$ISO_W" slot0Binds)" "2"

echo
echo "=== 7/8 the control: one batch, and the picture it has to reproduce ==="
# The budget is cut so that only the first batch fits. That is not a defect: it
# is the pre-T19 tile set, arrived at through the new planner. Everything the
# batching added must disappear together, and the frame must fall back to the
# picture the base commit produced -- measured on this host at
# 65934eded901e43e6c98f28132760d0b4cb3777c2b5c65fe372ea4a75243da7c.
CONTROL_BACKUP="$SCRATCH/world_tile_atlas.h.control.backup"
cp native/include/world_tile_atlas.h "$CONTROL_BACKUP"
CONTROL_HASH=$(git hash-object native/include/world_tile_atlas.h)
if ! patch_in native/include/world_tile_atlas.h \
    '  std::uint64_t budgetBytes = 384ull * 1024ull * 1024ull;' \
    '  std::uint64_t budgetBytes = 210ull * 1024ull * 1024ull;  // control: one batch only'; then
  echo "  CONTROL-SETUP=FAIL (the budget patch did not apply)"; cp "$CONTROL_BACKUP" native/include/world_tile_atlas.h
  echo "MATERIAL-COVERAGE=FAIL"; exit 1
fi
if ! bash build-target.sh tf2_demo_native material_coverage_probe > "$SCRATCH/control.build" 2>&1; then
  echo "  CONTROL-SETUP=FAIL (the rebuild failed)"
  restore_source "$CONTROL_BACKUP" native/include/world_tile_atlas.h control "$CONTROL_HASH" tf2_demo_native material_coverage_probe || true
  echo "MATERIAL-COVERAGE=FAIL"; exit 1
fi
CONTROL_PLAN=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" 2>&1)
echo "$CONTROL_PLAN" > "$OUT/control-one-batch.txt"
run_product control "$DEMO" "$TICK"
CONTROL_LINE=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_control" 2>/dev/null | head -1)
assert_eq "control rc" "$RC_control" "0"
assert_eq "control tiles" "$(rval "$CONTROL_LINE" tiled)" "64"
assert_eq "control refused" "$(rval "$CONTROL_LINE" refused)" "77"
assert_eq "control refusals are budget refusals" "$(rval "$CONTROL_LINE" budget)" "77"
assert_eq "control batches" "$(rval "$CONTROL_LINE" batches)" "1"
# The refusal has to name, by SET, every material the control could not admit --
# not merely the ones the second atlas happened to hold. `comm -23` is a set
# difference: in the full run, not in the control's tile table.
#
# The selector is `>= 1`, not `== 1`. With one atlas past the cap the two
# select the same set and the mistake is invisible. With three (bounded
# batching admits 64 + 64 + 13 here) `== 1` drops the third atlas' materials
# from the left side of the difference, so the assertion compares "the
# refusal named 64" against a trace line that says 77 -- red for a reason that
# is not a regression. The claim under test is about the refused set, so the
# selector has to mean "any atlas batch past the first".
printf '%s\n' "$TILES" | awk '$2>=1 {print $1}' | sort > "$SCRATCH/beyond-cap.$RUN_TAG.txt"
tiles_of "$TRACE_control" | awk '{print $1}' | sort > "$SCRATCH/control-tiles.$RUN_TAG.txt"
comm -23 "$SCRATCH/beyond-cap.$RUN_TAG.txt" "$SCRATCH/control-tiles.$RUN_TAG.txt" \
  > "$SCRATCH/control-refused.$RUN_TAG.txt"
assert_eq "materials the control refused" "$(grep -c . "$SCRATCH/control-refused.$RUN_TAG.txt")" "77"
# ...and every one of them is a named refusal in the table, not a missing row.
CONTROL_REFUSED_ROWS=$(refused_of "$TRACE_control" | grep -c .)
CONTROL_BUDGET_ROWS=$(grep -ao "gpu: world tile .*" "$TRACE_control" | grep -c "reason=atlas-budget")
assert_eq "control rows that name a refusal" "$CONTROL_REFUSED_ROWS" "77"
assert_eq "control rows refused for budget" "$CONTROL_BUDGET_ROWS" "77"
# And the picture. Two separate facts, because they are no longer the same one.
#   - the control frame equals the full frame BYTE FOR BYTE at this pose. With 59
#     materials refused to the flat fallback that is only possible because none of
#     them owns a pixel here (measured: 0 changed pixels). It is pinned as a
#     reading, and the coverage claim is the refused SET, not a pixel count.
#   - the diff code is not vacuous: the blend-off frame differs from the full frame
#     by 104024 pixels (worst channel delta 34) on the same run. A diff that read 0
#     for both pairs would be broken, and these assertions are what would say so.
frame_diff() { # frame_diff <bmp-a> <bmp-b> -> pixels= changed= max_channel_delta=
"python" - "$1" "$2" 2>&1 <<'PY'
import struct, sys

def load(path):
    d = open(path, 'rb').read()
    if d[:2] != b'BM':
        raise SystemExit('not a BMP: ' + path)
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit('unsupported BMP: %s bpp=%d' % (path, bpp))
    return {'w': w, 'h': abs(h), 'bpp': bpp, 'off': off, 'topdown': h < 0, 'd': d}

a, b = load(sys.argv[1]), load(sys.argv[2])
if (a['w'], a['h'], a['bpp']) != (b['w'], b['h'], b['bpp']):
    raise SystemExit('frames differ in shape')
w, h, step = a['w'], a['h'], a['bpp'] // 8
stride = ((w * a['bpp'] // 8) + 3) // 4 * 4
diff = 0
worst = 0
for y in range(h):
    ra = a['off'] + (y if a['topdown'] else h - 1 - y) * stride
    rb = b['off'] + (y if b['topdown'] else h - 1 - y) * stride
    if a['d'][ra:ra + w * step] == b['d'][rb:rb + w * step]:
        continue
    for x in range(w):
        p = x * step
        m = max(abs(a['d'][ra + p] - b['d'][rb + p]),
                abs(a['d'][ra + p + 1] - b['d'][rb + p + 1]),
                abs(a['d'][ra + p + 2] - b['d'][rb + p + 2]))
        if m:
            diff += 1
            if m > worst:
                worst = m
print('pixels=%d' % (w * h))
print('changed=%d' % diff)
print('max_channel_delta=%d' % worst)
PY
}
DELTA=$(frame_diff "$BMP_full" "$BMP_control")
echo "$DELTA" > "$OUT/coverage-delta.txt"
printf '%s\n' "$DELTA" | sed 's/^/  /'
# 0 while the reference camera saw none of the refused 59. Since the 2026-11-01
# camera it sees the top-left wall, which is one of them: the control draws that
# wall as the flat fallback. The number is pinned as the reading it is -- the
# claim that the batching kept its set is the refusal table above, and the claim
# that the diff code is not vacuous is the blend-off pair below.
# Re-pinned 2026-11-04 on 9219baf: 90188 was measured 2026-11-01, before be5e3ef
# (2026-11-03) published the pose the followed frame is DRAWN with. The draw pose
# moved, so 31 of the pixels the second batch's wall owned at the reference pose
# now resolve elsewhere. The structural assertions above are unchanged. Re-pinned 2026-11-08: the tree now reads 90109. 90157 was the pin this line carried since the T59 wave and 90188 above is the 2026-11-01 measurement; the pixels moved when fc884cd restored 45 func_illusionary faces (input/drawn 200000->242109 / 184961->227070).
# Re-pinned 2026-11-09 on ff8eb8f: 90127. The 2026-11-08 pin was 90109, measured
# before R2. R2 applies 16 glass VMTs' $basetexturetransform instead of refusing
# them, and the control refusal set moved 61 -> 77 by exactly those 16, so the pair
# moves with the picture: 63,912 px change in the full frame, 63,920 in the control,
# +56 in and -38 out, a net +21. ff8eb8f's sub-batch regroup (the same frame that
# filled 88,88,88 across the include pose) moves the last 3. Measured on this tree:
# 90127.
assert_eq "pixels the control frame changes" "$(rk "$DELTA" changed)" "90127"
DELTA_BLEND=$(frame_diff "$BMP_full" "$BMP_noblend")
echo "$DELTA_BLEND" > "$OUT/coverage-delta-blend-off.txt"
printf '%s\n' "$DELTA_BLEND" | sed 's/^/  /'
assert_ge "pixels the blend switch moves" "$(rk "$DELTA_BLEND" changed)" "50000"
assert_ge "largest channel delta" "$(rk "$DELTA_BLEND" max_channel_delta)" "32"
restore_source "$CONTROL_BACKUP" native/include/world_tile_atlas.h control "$CONTROL_HASH" tf2_demo_native material_coverage_probe || {
  echo "MATERIAL-COVERAGE=FAIL"; exit 1; }

echo
echo "=== 8/8 an include-only material owns the screen: the parse reaches the pixels ==="
# WHY THIS SECTION EXISTS. cp_snakewater_final1's pak ships "patch" wrappers: the
# VMT is only {patch: {include, replace}} and the shader with its $basetexture lives
# in the included file. Before T59 the parser read the wrapper's own root only, so
# the material was refused with no $basetexture and drawn as one flat colour by the
# untiled branch of the world vertex build. The pose below looks straight down at
# maps/cp_snakewater_final1/tile/floor_tile_004a_5934_-1624_-24, one of those
# wrappers, and every sampled pixel of the frame resolves to it -- so the albedo
# view is a direct reading of that single material. Measured on the two builds
# before this assertion was written: 169 distinct colours with the include followed,
# and ONE colour (245,245,245, share 1.0000) on a build that refuses the wrapper.
# The bounds sit between those two readings, around neither.
INCLUDE_MATERIAL="maps/cp_snakewater_final1/tile/floor_tile_004a_5934_-1624_-24"
# albedo_stats <bmp> -> pixels= distinct= modal=r,g,b modal_share_ppm=
albedo_stats() {
"python" - "$1" 2>&1 <<'PY'
import struct, sys
from collections import Counter
d = open(sys.argv[1], 'rb').read()
if d[:2] != b'BM':
    raise SystemExit('not a BMP: ' + sys.argv[1])
off = struct.unpack_from('<I', d, 10)[0]
w, h = struct.unpack_from('<ii', d, 18)
bpp = struct.unpack_from('<H', d, 28)[0]
step = bpp // 8
stride = ((w * step) + 3) // 4 * 4
px = []
for y in range(abs(h)):
    row = off + (y if h < 0 else abs(h) - 1 - y) * stride
    for x in range(w):
        p = row + x * step
        px.append((d[p + 2], d[p + 1], d[p]))
c = Counter(px)
top, count = c.most_common(1)[0]
print('pixels=%d' % len(px))
print('distinct=%d' % len(c))
print('modal=%d,%d,%d' % top)
print('modal_share_ppm=%d' % (count * 1000000 // len(px)))
PY
}
run_product include "$DEMO" "$TICK" --debug-camera 5934 -1624 96 89 0 --world-albedo --world-face-trace
assert_eq "include-pose rc" "$RC_include" "0"
INCLUDE_POINTS=$(grep -ac "gpu: world face-trace " "$TRACE_include" 2>/dev/null || echo 0)
INCLUDE_NAMED=$(grep -ao "gpu: world face-trace .*" "$TRACE_include" 2>/dev/null | grep -c "material=$INCLUDE_MATERIAL ")
assert_eq "face-trace points at the include pose" "$INCLUDE_POINTS" "28"
assert_eq "points that resolve to the include-only material" "$INCLUDE_NAMED" "$INCLUDE_POINTS"
INCLUDE_NAMES=$(grep -a "gpu: world face-trace point=" "$TRACE_include" | grep -ao "material=[^ ]*" | sort -u | wc -l)
assert_eq "distinct materials the include pose names" "$INCLUDE_NAMES" "1"
INCLUDE_STATS=$(albedo_stats "$BMP_include")
echo "$INCLUDE_STATS" > "$OUT/include-material-pixels.txt"
printf '%s\n' "$INCLUDE_STATS" | sed 's/^/  /'
assert_ge "albedo colours where the include-only material covers the screen" "$(rk "$INCLUDE_STATS" distinct)" "100"
assert_le "share of the most common albedo colour (ppm)" "$(rk "$INCLUDE_STATS" modal_share_ppm)" "500000"

echo
if [ "$MUTATION" -eq 1 ]; then
  echo "=== the mutations: seven defects, each of which must turn this gate red ==="
  # The cheap half first: values the forward run must NOT produce.
  if [ "$(rval "$FETCH" capped)" != "0" ]; then
    echo "  FAIL a material was dropped by a ceiling before any mutation"; exit 1
  fi
  if [ "$(rval "$PLAN" refused)" != "0" ]; then
    echo "  FAIL the planner refused a material before any mutation"; exit 1
  fi
  if [ "$(rval "$ISO" weaponDrawsWithWorldAtlas)" != "0" ]; then
    echo "  FAIL t0 leaked across a pass before any mutation"; exit 1
  fi

  # ---- m1: the budget stops the batching after one batch --------------------
  # This is the old ceiling, rebuilt out of new parts: one atlas, 64 tiles, the
  # other 77 materials untextured. Every count that existed before this round
  # stays green -- the atlas is bound, the fetch reports no cap -- and the
  # coverage claim is what goes red. Already run above as the control; the
  # defect is the *forward* assertion that tiled=141, which the control's 64
  # fails.
  echo "--- m1: the budget admits one batch (the old 64-tile ceiling, rebuilt)"
  if [ "$(rval "$CONTROL_LINE" tiled)" = "141" ]; then
    echo "  MUTATION-CAUGHT=FAIL (m1: the control still tiled every material)"
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if [ "$(rval "$CONTROL_LINE" tiled)" = "64" ] && [ "$(rval "$CONTROL_LINE" refused)" = "77" ]; then
    echo "  MUTATION-CAUGHT=PASS (m1: tiled=64 refused=77 while the forward run read 141/0)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m1: tiled=$(rval "$CONTROL_LINE" tiled) refused=$(rval "$CONTROL_LINE" refused))"
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi

  # ---- m2: the half-texel in the uv is dropped (judge_m2 owns the verdict)
  mutate_m2 || { echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m3: a weapon range is drawn with the world's atlas ------------------
  # The bleed this round's isolation counter exists for: the weapon pass binds
  # t0 to the world atlas instead of the weapon's own material, so the gun is
  # painted with a wall. `worldDrawsWithWeaponTexture` stays 0 and every draw
  # count stays the same; only the cross-pass counter moves.
  echo "--- m3: a weapon range binds the world atlas at t0 (the gun samples a wall)"
  M3_BACKUP="$SCRATCH/native_renderer.cpp.leak.backup"
  cp native/src/native_renderer_viewmodel.cpp "$M3_BACKUP"
  M3_HASH=$(git hash-object native/src/native_renderer_viewmodel.cpp)
  if ! patch_in native/src/native_renderer_viewmodel.cpp \
      '          bindSlot0(view, "weapon-range");' \
      '          bindSlot0(worldAtlasTextures_.empty() ? view : worldAtlasTextures_.front().view(), "weapon-range");'; then
    echo "  MUTATION-SETUP=FAIL (m3: the patch did not apply)"; cp "$M3_BACKUP" native/src/native_renderer_viewmodel.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m3.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m3: the rebuild failed)"
    restore_source "$M3_BACKUP" native/src/native_renderer_viewmodel.cpp m3 "$M3_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  run_product m3 "$POV_DEMO" "$POV_TICK" --viewmodel "$VM_MODEL" --viewmodel-fov 90
  M3_ISO=$(grep -ao "gpu: world isolation .*" "$TRACE_m3" 2>/dev/null | head -1)
  M3_LEAK=$(rval "$M3_ISO" weaponDrawsWithWorldAtlas)
  if [ "${M3_LEAK:-0}" -gt 0 ] 2>/dev/null; then
    echo "  MUTATION-CAUGHT=PASS (m3: weaponDrawsWithWorldAtlas=$M3_LEAK while the forward run read 0)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m3: weaponDrawsWithWorldAtlas=${M3_LEAK:-<none>})"
    restore_source "$M3_BACKUP" native/src/native_renderer_viewmodel.cpp m3 "$M3_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  echo "$M3_ISO" > "$OUT/mutated-m3-weapon-leak.txt"
  restore_source "$M3_BACKUP" native/src/native_renderer_viewmodel.cpp m3 "$M3_HASH" tf2_demo_native || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m4: $bumpmap2 is dropped instead of refused -------------------------
  # The task's forth step: a feature that is not implemented must be refused by
  # name. Here the reader simply stops counting it, so the material reads as if
  # it never asked -- which is the failure mode "silently ignored" looks like.
  echo "--- m4: \$bumpmap2 stops being counted (an unimplemented layer is ignored)"
  M4_BACKUP="$SCRATCH/world_material_features.cpp.bumpmap2.backup"
  cp native/src/world_material_features.cpp "$M4_BACKUP"
  M4_HASH=$(git hash-object native/src/world_material_features.cpp)
  if ! patch_in native/src/world_material_features.cpp \
      '    if (!vmt.bumpMap2.empty()) {' \
      '    if (false) {  // mutation: $bumpmap2 is neither applied nor refused'; then
    echo "  MUTATION-SETUP=FAIL (m4: the patch did not apply)"; cp "$M4_BACKUP" native/src/world_material_features.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh material_coverage_probe > "$SCRATCH/m4.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m4: the rebuild failed)"
    restore_source "$M4_BACKUP" native/src/world_material_features.cpp m4 "$M4_HASH" material_coverage_probe || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  M4=$("$PROBE" --map "$TF/maps/$MAP.bsp" --tf-root "$TF" 2>&1)
  M4_DECLARED=$(kv "$M4" feature.bumpmap2.declared)
  if [ "$M4_DECLARED" = "0" ] && [ "$(kv "$REAL" feature.bumpmap2.declared)" = "1" ]; then
    echo "  MUTATION-CAUGHT=PASS (m4: declared=0 after the mutation, 1 before)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m4: declared=$M4_DECLARED)"
    restore_source "$M4_BACKUP" native/src/world_material_features.cpp m4 "$M4_HASH" material_coverage_probe || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  echo "$M4" > "$OUT/mutated-m4-silent-bumpmap2.txt"
  restore_source "$M4_BACKUP" native/src/world_material_features.cpp m4 "$M4_HASH" material_coverage_probe || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m5: the batch index is built from triangles, not vertices -----------
  # The refactor's own failure mode: `triangle * 3` becomes `triangle`, so every
  # index points into the middle of some other triangle. All 141 tiles are still
  # allocated, all 3 batches are still drawn, the plan is unchanged, and the map
  # turns into a hash of stretched polygons. Only the frame says so.
  echo "--- m5: the index buffer is built from triangle numbers (geometry tears)"
  M5_BACKUP="$SCRATCH/native_renderer.cpp.index.backup"
  cp native/src/native_renderer_world.cpp "$M5_BACKUP"
  M5_HASH=$(git hash-object native/src/native_renderer_world.cpp)
  if ! patch_in native/src/native_renderer_world.cpp \
      '      const auto base = static_cast<std::uint32_t>(triangle * 3u);' \
      '      const auto base = static_cast<std::uint32_t>(triangle);  // mutation: no *3'; then
    echo "  MUTATION-SETUP=FAIL (m5: the patch did not apply)"; cp "$M5_BACKUP" native/src/native_renderer_world.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m5.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m5: the rebuild failed)"
    restore_source "$M5_BACKUP" native/src/native_renderer_world.cpp m5 "$M5_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  run_product m5 "$DEMO" "$TICK" --no-world-vis-clip
  M5_PLAN=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_m5" 2>/dev/null | head -1)
  if cmp -s "$BMP_noclip" "$BMP_m5"; then
    echo "  MUTATION-CAUGHT=FAIL (m5: the frame is unchanged, so the index base is not what draws it)"
    restore_source "$M5_BACKUP" native/src/native_renderer_world.cpp m5 "$M5_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if [ "$(rval "$M5_PLAN" tiled)" = "141" ] && [ "$(rval "$M5_PLAN" batches)" = "3" ]; then
    echo "  MUTATION-CAUGHT=PASS (m5: tiled=141 batches=3 unchanged, the frame moved)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m5: the plan moved too, so this did not isolate the index)"
    restore_source "$M5_BACKUP" native/src/native_renderer_world.cpp m5 "$M5_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  ( cd "$SCRATCH" && sha256sum "m5.$RUN_TAG.bmp" "noclip.$RUN_TAG.bmp" ) | sed 's/^/  ..   /'
  restore_source "$M5_BACKUP" native/src/native_renderer_world.cpp m5 "$M5_HASH" tf2_demo_native || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m6: the clip ignores the batch table --------------------------------
  # The other half of the same question. The frame is clipped, so the indices
  # come from `setWorldVisDrawList`; putting every surviving index in batch 0
  # leaves the plan at 141 tiles / 3 batches, keeps every tile allocated, and
  # paints batch 1's materials with batch 0's atlas. Texture selection and
  # index grouping have to agree, and only the picture says whether they do.
  #
  # RE-ANCHOR 2026-10-25: this mutant used to compare against the acceptance
  # frame (tick 0, no camera override). At that pose batch 1 owns zero pixels --
  # measured, not assumed: section 7's control refuses every material outside the
  # one batch it keeps (61 when this note was written; 77 since R2 split the atlas
  # into three), and its frame comes back byte-identical to the full frame -- true
  # already on ea97815 before T59. Regrouping batch-1 indices into batch 0
  # therefore could not move one pixel, and the mutant was vacuous -- last green
  # before the map's material set drifted (the same drift that dropped
  # WOOD/WOOD_WALL007A from >=100 to 67 visible triangles). It is now anchored
  # on section 8's include pose, where all 28 face-trace points resolve to a
  # batch-1 material that fills the screen, so the regroup has to repaint it.
  echo "--- m6: the clipped indices all land in batch 0 (batch 1 samples batch 0's atlas)"
  M6_BACKUP="$SCRATCH/native_renderer.cpp.regroup.backup"
  cp native/src/native_renderer_world.cpp "$M6_BACKUP"
  M6_HASH=$(git hash-object native/src/native_renderer_world.cpp)
  if ! patch_in native/src/native_renderer_world.cpp \
      '    perBatch[entry].push_back(index);' \
      '    perBatch[0].push_back(index);  // mutation: the clip ignores the batch table'; then
    echo "  MUTATION-SETUP=FAIL (m6: the patch did not apply)"; cp "$M6_BACKUP" native/src/native_renderer_world.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m6.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m6: the rebuild failed)"
    restore_source "$M6_BACKUP" native/src/native_renderer_world.cpp m6 "$M6_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  run_product m6 "$DEMO" "$TICK" --debug-camera 5934 -1624 96 89 0 --world-albedo --world-face-trace
  M6_PLAN=$(grep -ao "gpu: world atlas-plan .*" "$TRACE_m6" 2>/dev/null | head -1)
  M6_STATS=$(albedo_stats "$BMP_m6" | tr '\n' ' ')
  if cmp -s "$BMP_include" "$BMP_m6"; then
    echo "  MUTATION-CAUGHT=FAIL (m6: the include-pose frame is unchanged, so the regroup is not what draws it)"
    restore_source "$M6_BACKUP" native/src/native_renderer_world.cpp m6 "$M6_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if [ "$(rval "$M6_PLAN" tiled)" = "141" ] && [ "$(rval "$M6_PLAN" batches)" = "3" ]; then
    echo "  MUTATION-CAUGHT=PASS (m6: tiled=141 batches=3 unchanged, the include-pose frame moved: $M6_STATS)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m6: the plan moved too, so this did not isolate the regroup)"
    restore_source "$M6_BACKUP" native/src/native_renderer_world.cpp m6 "$M6_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  ( cd "$SCRATCH" && sha256sum "m6.$RUN_TAG.bmp" "include.$RUN_TAG.bmp" ) | sed 's/^/  ..   /'
  restore_source "$M6_BACKUP" native/src/native_renderer_world.cpp m6 "$M6_HASH" tf2_demo_native || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  # ---- m7: the patch wrapper is refused instead of followed -------------------
  # T59's own failure mode, and the reason section 8 exists. Refusing the wrapper
  # leaves every count that existed before T59 green -- no cap, nothing crashed, the
  # planner still refuses nothing -- and turns the screen into the single flat colour
  # the pre-T59 build drew. Only the picture says so, so the mutant is judged on the
  # same pose and the same statistic the forward section pins.
  echo "--- m7: the patch wrapper is refused instead of followed (the screen goes flat)"
  M7_BACKUP="$SCRATCH/vmt_material.cpp.patch.backup"
  cp native/src/vmt_material.cpp "$M7_BACKUP"
  M7_HASH=$(git hash-object native/src/vmt_material.cpp)
  if ! patch_in native/src/vmt_material.cpp \
      '  if (lower(node.shader) != "patch") return true;' \
      '  if (true) return true;  // mutation: every material resolves as itself'; then
    echo "  MUTATION-SETUP=FAIL (m7: the patch did not apply)"; cp "$M7_BACKUP" native/src/vmt_material.cpp
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  if ! bash build-target.sh tf2_demo_native > "$SCRATCH/m7.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (m7: the rebuild failed)"
    restore_source "$M7_BACKUP" native/src/vmt_material.cpp m7 "$M7_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  run_product m7 "$DEMO" "$TICK" --debug-camera 5934 -1624 96 89 0 --world-albedo --world-face-trace
  M7_STATS=$(albedo_stats "$BMP_m7")
  M7_DISTINCT=$(rk "$M7_STATS" distinct); [ -n "$M7_DISTINCT" ] || M7_DISTINCT=0
  M7_SHARE=$(rk "$M7_STATS" modal_share_ppm); [ -n "$M7_SHARE" ] || M7_SHARE=0
  if [ "$M7_DISTINCT" -le 2 ] && [ "$M7_SHARE" -ge 900000 ]; then
    echo "  MUTATION-CAUGHT=PASS (m7: distinct=$M7_DISTINCT, one colour covers ${M7_SHARE}ppm of the frame -- the wrapper is refused and the screen is flat)"
  else
    echo "  MUTATION-CAUGHT=FAIL (m7: distinct=$M7_DISTINCT modal_share_ppm=$M7_SHARE -- the picture did not go flat, so the include is not what draws it)"
    restore_source "$M7_BACKUP" native/src/vmt_material.cpp m7 "$M7_HASH" tf2_demo_native || true
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  echo "$M7_STATS" > "$OUT/mutated-m7-patch-refused.txt"
  restore_source "$M7_BACKUP" native/src/vmt_material.cpp m7 "$M7_HASH" tf2_demo_native || {
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1; }

  if [ "$fail" -ne 0 ]; then
    echo "  the mutation half caught its six cases, but a forward assertion above"
    echo "  did not hold -- a caught mutation over a red gate is not a pass"
    echo "MATERIAL-COVERAGE-MUTATION=FAIL"; exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (the one-batch budget, the dropped half-texel, the"
  echo "  weapon range bound to the world atlas, the ignored \$bumpmap2, the"
  echo "  triangle-numbered index, the regroup that ignored the batch table and the"
  echo "  refused patch wrapper each go red; every source came back byte-identical)"
  exit 0
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "MATERIAL-COVERAGE=PASS"
else
  echo "MATERIAL-COVERAGE=FAIL"
fi
exit "$fail"
