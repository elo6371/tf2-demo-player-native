#!/usr/bin/env bash
# fog-check.sh -- T101: the map's env_fog_controller, read back as numbers, as a
#                picture, and as a refusal that can be caught.
#
# WHY THIS FILE EXISTS
#   T101 (feat(render): draw the map's env_fog_controller on the world brush pass)
#   added three things and no gate: the parser reads the controller out of BSP
#   lump 0 (native/src/bsp_map.cpp:357-410), main.cpp assembles a WorldFogState
#   from it (:7085-7120) and prints exactly one line about it (:7143-7165), and the
#   world pixel shader blends toward fogcolor behind feature bit 32
#   (native/src/native_renderer_shaders.cpp:374). Before this file, grep for
#   --world-fog across the 99 *.sh gates returned 0 hits: nothing in the repository
#   could have caught a wrong fog.
#
#   The pass is OFF by default on purpose (main.cpp:2330-2345: "a default that
#   changes pixels is a default that has to be argued for"), so a gate is the only
#   place the feature can be observed at all.
#
# WHAT IT ASSERTS (one map, one pinned camera, four captures)
#   A0  inputs: product, map, camera record and demo exist; their hashes are
#       recorded. A stale or wrong demo/BSP makes the rest of the gate meaningless,
#       so the hashes are printed before anything is claimed.
#   A1  --world-fog: exactly one fog line; enabled=1, source=bsp,
#       verticesFogged>=1, passes=world-brush-only, featureMaskFogBit=32, and the
#       numbers are the MAP's own -- start=1000.00 end=16000.00 maxdensity=1.000
#       color=74,83,102, read independently out of cp_snakewater_final1.bsp's entity
#       lump (plan.md section 2). A1 also asserts the same trace says
#       geometry=all, so "the map was not filtered" is a reading, not an assumption.
#   A2  no --world-fog: enabled=0, verticesFogged=0, source=off, reason is the
#       default-off sentence verbatim, and start/end/color STILL echo the map's
#       numbers (the numbers are carried; only the switch is off).
#   A3  the pixels: the A1 frame and the A2 frame are not the same picture, with a
#       diff-pixel floor (0.05% -- argument at FOG_MIN_DIFF_PCT below).
#   A4  --fog-start/--fog-end overrides: source=cli, both numbers echoed exactly,
#       maxdensity and color UNCHANGED (a partial override is a partial override),
#       and the frame differs from both A1 and A2.
#   A5  the negative control: --world-fog together with --world-geometry terrain.
#       The drawn set is not the map, so the pass must refuse: enabled=0,
#       verticesFogged=0, source=off, reason is the filtered sentence verbatim, and
#       the same trace carries geometry=terrain (otherwise that sentence could have
#       come from anywhere).
#   A6  --selftest: every parse/judge function is fed synthetic traces and synthetic
#       bitmaps -- positives that must come out clean and mutants that must go red on
#       a NAMED assertion. No product, no lock, no GPU.
#
# WHAT IT DOES NOT ASSERT (a green line may not borrow these claims)
#   * the two "no controller" reason branches (fogenable 0 / no controller at all)
#     are NOT reachable on this map: all four authorised maps carry one controller
#     with fogenable 1 (measured by decompressing lump 0, plan.md section 2). They
#     are exercised by A6's synthetic text only.
#   * entity models, the third-person weapon and the sky box are not fogged
#     (passes=world-brush-only states it); no assertion here describes them.
#   * fogblend (bilateral fog) is not implemented. cp_snakewater_final1 sets
#     fogblend 0, so reason reads "-" here; koth_bagel_rc11/rc12 set fogblend 1 and
#     would carry the blend sentence. Those maps are not run by default.
#   * nothing here says the fog looks right. It says the pass is on, the numbers are
#     the map's, the pixels moved, and the refusal sentences are the ones the code
#     was written to say.
#
# MUTATIONS: designed, NOT implemented in this draft (plan.md section 4, M1-M3).
#   --mutation exits 2 on purpose: a flag that cannot be honoured must not look like
#   one that was.
#
# NOT A verify-all STEP YET (2026-11-10). verify-all.sh labels its steps "N/30" and
#   its pinned counts were read off a 30-step chain; a 31st step renumbers every
#   label and leaves the chain unproven until it is run end to end again, which was
#   explicitly out of scope for the session that bought this file. So the gate is
#   run by hand and listed as open work, rather than wired in and left unrun.
#
# FIRST REAL RUN (2026-11-10, product 36d8a15e, cp_snakewater_final1, tick 2000):
#   FOG-CHECK=PASS (59 assertions, 5 judges). fog-on enabled=1 source=bsp
#   verticesFogged=726327 featureMaskFogBit=32; fog-off enabled=0 source=off
#   verticesFogged=0; --fog-start 16 --fog-end 128 gave source=cli with both
#   numbers echoed; --world-geometry terrain refused with the filtered sentence.
#   A3 measured 299,705 differing pixels of 860,784 (34.8%), 700x the floor.
#
# Usage: bash fog-check.sh [--selftest] [--keep]
#          --selftest  judge the judgement against synthetic input; no product
#          --keep      keep the scratch dir (settings copies); traces/logs/CSV stay
#                      in $OUT either way
# Exit:  0 = every assertion held; 1 = an assertion failed; 2 = an input is
#        missing (product not built, demo/BSP/lock/tool absent, bad usage).
set -uo pipefail
cd "$(dirname "$0")"

PYTHON_DEFAULT=C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe
LOCK_DEFAULT=/d/TF2_Native_Worktrees/_shared/hostlock.sh
OUT_DEFAULT=evidence/t101-fog
if [[ -v PYTHON ]]; then :; else PYTHON=$PYTHON_DEFAULT; fi
if [[ -v HOSTLOCK ]]; then LOCK=$HOSTLOCK; else LOCK=$LOCK_DEFAULT; fi
if [[ -v FOG_OUT ]]; then OUT=$FOG_OUT; else OUT=$OUT_DEFAULT; fi
if [[ -v FOG_TIMEOUT ]]; then :; else FOG_TIMEOUT=240; fi
if [[ -v LOCK_OWNER ]]; then :; else LOCK_OWNER=t101-fog; fi
if [[ -v HOLD_LOCK ]]; then :; else HOLD_LOCK=0; fi
[[ -x $PYTHON ]] || PYTHON=python
PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP=cp_snakewater_final1
BSP=maps/$MAP.bsp
MATRIX=evidence/t62-map-matrix/$MAP/camera.txt
SPEED=8
# T101 pins, read off cp_snakewater_final1.bsp's entity lump by hand (plan.md 2):
#   {"fogstart" "1000" "fogend" "16000" "fogmaxdensity" "1"
#    "fogcolor" "74 83 102" "fogenable" "1" "fogblend" "0"}
# bsp_map.cpp:396-406 divides the colour by 255 and main.cpp:7146 multiplies it
# back, so the trace must print the bytes the map wrote. If the parser loses the
# controller these read as the struct defaults (bsp_map.h:97-115: 0.00 / 0.00 /
# 1.000 / 255,255,255) and A1 goes red on five separate labels, not one.
PIN_START=1000.00
PIN_END=16000.00
PIN_MAXDENSITY=1.000
PIN_COLOR=74,83,102
# A4's override values: the pass starts at 16 map units and is solid by 128, i.e.
# near the camera and far inside the map's own 1000..16000 range. Chosen so the
# override cannot be a no-op even if the map is later edited.
OVERRIDE_START=16
OVERRIDE_END=128
# A3's floor. Capture is deterministic here (same tick, same settings, same GPU path
# produce a byte-identical frame -- hud-live-check.sh section 4 asserts exactly that
# for two runs), so the noise floor is 0 and ANY differing pixel is already a
# difference. The floor exists only to reject the degenerate reading "fog touched
# three pixels": 0.05% of a 1264x681 frame (860,784 px) is 430 px, an order of
# magnitude below any plausible correct fog, which must re-colour every world-brush
# pixel beyond fogstart. 0.05 is PROVISIONAL: the first window run records the
# measured percentage, and if that measurement sits within 10x of the floor the pin
# is re-derived from the reading, upward. A measurement BELOW the floor is a finding
# about the pass, not a reason to lower the floor.
FOG_MIN_DIFF_PCT=0.05
# D-62: a judge that returns early must not ride on the other judges' green. Five
# judges run and emit 50 assertions; the floors below catch a skipped judge (each
# judge carries at least 3 of them) without a knife-edge exact count that would turn
# every documentation edit into a re-pin.
MIN_ASSERTIONS=48
MIN_JUDGES=5

SELFTEST=0
KEEP=0
for arg in "$@"; do
  case $arg in
    --selftest) SELFTEST=1 ;;
    --keep) KEEP=1 ;;
    --mutation)
      echo "--mutation is designed but NOT implemented in this draft; see plan.md section 4" >&2
      exit 2 ;;
    --help)
      sed -n '2,68p' "$0"; exit 0 ;;
    *)
      echo "unknown argument: $arg" >&2
      echo "usage: bash fog-check.sh [--selftest] [--keep]" >&2
      exit 2 ;;
  esac
done

assertions=0
failures=0
judges=0
ok()  { assertions=$((assertions + 1)); echo "  OK   $*"; }
bad() { assertions=$((assertions + 1)); failures=$((failures + 1)); echo "  FAIL $*"; }
assert_eq() { # label actual expected
  local label=$1 actual=$2 expected=$3
  if [ "$actual" = "$expected" ]; then ok "$label = $actual"; else bad "$label = $actual (want $expected)"; fi
}
assert_int_ge() { # label actual floor
  local label=$1 actual=$2 floor=$3
  case $actual in
    ''|*[!0-9]*) bad "$label = $actual (want an integer >= $floor)"; return ;;
  esac
  if [ "$actual" -ge "$floor" ]; then ok "$label = $actual"; else bad "$label = $actual (want >= $floor)"; fi
}
assert_int_eq() { # label actual expected, integer semantics, catches non-numeric
  local label=$1 actual=$2 expected=$3
  case $actual in
    ''|*[!0-9]*) bad "$label = $actual (want $expected)"; return ;;
  esac
  if [ "$actual" -eq "$expected" ]; then ok "$label = $actual"; else bad "$label = $actual (want $expected)"; fi
}

# value of <key=...> on the first line containing <marker>, or "-" when the line or
# the key is absent. The marker is matched as a substring, the key must start a
# whitespace-separated token -- so "start=" cannot be satisfied by "fogstart=".
line_field() { # file marker key
  awk -v pat="$2" -v key="$3" '
    index($0, pat) > 0 {
      for (i = 1; i <= NF; i++) {
        if (index($i, key) == 1) { split($i, KV, "="); v = KV[2]; f = 1 }
      }
      if (f) exit
    }
    END { if (f) print v; else print "-" }
  ' "$1" 2>/dev/null
}
fog_field() { line_field "$1" "gpu: world fog " "$2"; }
fog_lines() { grep -a -c "gpu: world fog " "$1" 2>/dev/null || true; }
# The reason is free text and the LAST field on the line, so it cannot be read with
# whitespace tokenisation: it is everything after the last " reason=". An empty
# reason is legitimately printed as "-" by main.cpp:7152; a missing line yields "".
fog_reason() {
  local line
  line=$(grep -a "gpu: world fog " "$1" 2>/dev/null | head -1)
  [ -n "$line" ] || return 0
  printf '%s\n' "$line" | sed 's/.* reason=//'
}
sha256_file() {
  sha256sum "$1" 2>/dev/null | awk '{ print $1 }'
}

SCRATCH=$(mktemp -d) || { echo "FATAL: mktemp -d failed" >&2; exit 2; }
cleanup() { if [ "$KEEP" -eq 0 ]; then rm -rf "$SCRATCH"; fi; }
trap cleanup EXIT
SETTINGS_FILE=$SCRATCH/settings.ini
UI_FILE=$SCRATCH/ui.ini

# Diff two 24bpp uncompressed BMPs: prints "<differing pixels> <total pixels>", or
# "ERR ..." on anything it cannot read. A gate that silently treated an unreadable
# bitmap as "identical" would pass A3 on a truncated file, so every failure here is
# loud and A3 checks the shape of the answer before using it.
diff_pixels() { # a.bmp b.bmp
  "$PYTHON" - "$1" "$2" 2>&1 <<'PYEOF'
import struct, sys
def load(p):
    d = open(p, 'rb').read()
    if len(d) < 54 or d[:2] != b'BM':
        raise SystemExit('ERR not-a-bmp ' + p)
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    bpp = struct.unpack('<H', d[28:30])[0]
    comp = struct.unpack('<I', d[30:34])[0]
    if bpp != 24 or comp != 0:
        raise SystemExit('ERR bmp bpp=%d comp=%d (%s)' % (bpp, comp, p))
    if w <= 0 or h == 0:
        raise SystemExit('ERR bmp dims %dx%d (%s)' % (w, h, p))
    stride = ((w * 3 + 3) // 4) * 4
    if off + stride * abs(h) > len(d):
        raise SystemExit('ERR bmp truncated (%s)' % p)
    return d, off, w, abs(h), stride
a, ao, aw, ah, asr = load(sys.argv[1])
b, bo, bw, bh, bsr = load(sys.argv[2])
if (aw, ah) != (bw, bh):
    raise SystemExit('ERR dims %dx%d vs %dx%d' % (aw, ah, bw, bh))
diff = 0
for y in range(ah):
    ra = a[ao + y * asr: ao + (y + 1) * asr]
    rb = b[bo + y * asr: bo + (y + 1) * asr]
    for x in range(aw):
        if ra[x * 3:x * 3 + 3] != rb[x * 3:x * 3 + 3]:
            diff += 1
print('%d %d' % (diff, aw * ah))
PYEOF
}

# ---------------------------------------------------------------- A1 -- fog on
judge_a1() { # trace rc bmp
  judges=$((judges + 1))
  local t=$1 rc=$2 bmp=$3
  assert_int_eq "A1-exit" "$rc" 0
  assert_int_eq "A1-line-count" "$(fog_lines "$t")" 1
  assert_eq "A1-geometry" "$(line_field "$t" "gpu: world geometry=" "geometry=")" "all"
  assert_eq "A1-enabled" "$(fog_field "$t" "enabled=")" "1"
  assert_eq "A1-source" "$(fog_field "$t" "source=")" "bsp"
  assert_eq "A1-passes" "$(fog_field "$t" "passes=")" "world-brush-only"
  assert_eq "A1-featurebit" "$(fog_field "$t" "featureMaskFogBit=")" "32"
  assert_int_ge "A1-vertices" "$(fog_field "$t" "verticesFogged=")" 1
  assert_eq "A1-start" "$(fog_field "$t" "start=")" "$PIN_START"
  assert_eq "A1-end" "$(fog_field "$t" "end=")" "$PIN_END"
  assert_eq "A1-maxdensity" "$(fog_field "$t" "maxdensity=")" "$PIN_MAXDENSITY"
  assert_eq "A1-color" "$(fog_field "$t" "color=")" "$PIN_COLOR"
  assert_eq "A1-reason" "$(fog_reason "$t")" "-"
  if [ -s "$bmp" ]; then ok "A1-bmp = $(wc -c < "$bmp" | tr -d ' ') bytes"; else bad "A1-bmp = missing ($bmp)"; fi
}

# --------------------------------------------------------------- A2 -- fog off
judge_a2() { # trace rc bmp
  judges=$((judges + 1))
  local t=$1 rc=$2 bmp=$3
  assert_int_eq "A2-exit" "$rc" 0
  assert_int_eq "A2-line-count" "$(fog_lines "$t")" 1
  assert_eq "A2-geometry" "$(line_field "$t" "gpu: world geometry=" "geometry=")" "all"
  assert_eq "A2-enabled" "$(fog_field "$t" "enabled=")" "0"
  assert_eq "A2-vertices" "$(fog_field "$t" "verticesFogged=")" "0"
  assert_eq "A2-source" "$(fog_field "$t" "source=")" "off"
  assert_eq "A2-passes" "$(fog_field "$t" "passes=")" "world-brush-only"
  assert_eq "A2-reason" "$(fog_reason "$t")" "default off; --world-fog turns it on"
  # The numbers are carried by the state even when the switch is off: this is what
  # distinguishes "the map has no fog" from "the pass is off".
  assert_eq "A2-start" "$(fog_field "$t" "start=")" "$PIN_START"
  assert_eq "A2-end" "$(fog_field "$t" "end=")" "$PIN_END"
  assert_eq "A2-color" "$(fog_field "$t" "color=")" "$PIN_COLOR"
  if [ -s "$bmp" ]; then ok "A2-bmp = $(wc -c < "$bmp" | tr -d ' ') bytes"; else bad "A2-bmp = missing ($bmp)"; fi
}

# ------------------------------------------------------------- A3 -- the pixels
judge_a3() { # bmp_fog_on bmp_fog_off
  judges=$((judges + 1))
  local a=$1 b=$2 answer diff total pct verdict
  if [ ! -s "$a" ] || [ ! -s "$b" ]; then
    bad "A3-bmps = missing ($a / $b)"
    return
  fi
  ok "A3-bmps = $(basename "$a") / $(basename "$b")"
  answer=$(diff_pixels "$a" "$b")
  set -- $answer
  diff=$1
  total=$2
  case $diff in
    ''|*[!0-9]*) bad "A3-diff-nonzero = unreadable ($answer)"; return ;;
  esac
  if [ "$diff" -ne 0 ]; then ok "A3-diff-nonzero = $diff pixel(s)"; else bad "A3-diff-nonzero = 0 (the fog changed nothing)"; fi
  pct=$(awk -v d="$diff" -v t="$total" 'BEGIN { if (t > 0) printf "%.3f", 100.0 * d / t; else print "0.000" }')
  verdict=$(awk -v d="$diff" -v t="$total" -v f="$FOG_MIN_DIFF_PCT" 'BEGIN { print (t > 0 && d * 100.0 / t >= f) ? "pass" : "fail" }')
  if [ "$verdict" = pass ]; then
    ok "A3-diff-floor = $pct% ($diff/$total px, floor $FOG_MIN_DIFF_PCT%)"
  else
    bad "A3-diff-floor = $pct% ($diff/$total px, floor $FOG_MIN_DIFF_PCT%)"
  fi
}

# ------------------------------------------------- A4 -- the CLI numbers win
judge_a4() { # trace rc bmp_cli bmp_on bmp_off
  judges=$((judges + 1))
  local t=$1 rc=$2 bmp=$3 bmp_on=$4 bmp_off=$5 answer diff
  assert_int_eq "A4-exit" "$rc" 0
  assert_int_eq "A4-line-count" "$(fog_lines "$t")" 1
  assert_eq "A4-enabled" "$(fog_field "$t" "enabled=")" "1"
  assert_eq "A4-source" "$(fog_field "$t" "source=")" "cli"
  assert_eq "A4-passes" "$(fog_field "$t" "passes=")" "world-brush-only"
  assert_eq "A4-start" "$(fog_field "$t" "start=")" "$OVERRIDE_START.00"
  assert_eq "A4-end" "$(fog_field "$t" "end=")" "$OVERRIDE_END.00"
  # A partial override must stay partial: maxdensity and colour are the map's.
  assert_eq "A4-maxdensity" "$(fog_field "$t" "maxdensity=")" "$PIN_MAXDENSITY"
  assert_eq "A4-color" "$(fog_field "$t" "color=")" "$PIN_COLOR"
  assert_eq "A4-reason" "$(fog_reason "$t")" "-"
  answer=$(diff_pixels "$bmp" "$bmp_on")
  set -- $answer
  diff=$1
  case $diff in
    ''|*[!0-9]*) bad "A4-diff-vs-a1 = unreadable ($answer)" ;;
    *) if [ "$diff" -ne 0 ]; then ok "A4-diff-vs-a1 = $diff pixel(s) differ from the map's own fog"; else bad "A4-diff-vs-a1 = 0 (the override changed nothing)"; fi ;;
  esac
  answer=$(diff_pixels "$bmp" "$bmp_off")
  set -- $answer
  diff=$1
  case $diff in
    ''|*[!0-9]*) bad "A4-diff-vs-a2 = unreadable ($answer)" ;;
    *) if [ "$diff" -ne 0 ]; then ok "A4-diff-vs-a2 = $diff pixel(s) differ from fog off"; else bad "A4-diff-vs-a2 = 0 (fog on and fog off are the same picture)"; fi ;;
  esac
  if [ -s "$bmp" ]; then ok "A4-bmp = $(wc -c < "$bmp" | tr -d ' ') bytes"; else bad "A4-bmp = missing ($bmp)"; fi
}

# ------------------------------------------- A5 -- filtered geometry refuses
judge_a5() { # trace rc
  judges=$((judges + 1))
  local t=$1 rc=$2
  assert_int_eq "A5-exit" "$rc" 0
  assert_int_eq "A5-line-count" "$(fog_lines "$t")" 1
  assert_eq "A5-enabled" "$(fog_field "$t" "enabled=")" "0"
  assert_eq "A5-vertices" "$(fog_field "$t" "verticesFogged=")" "0"
  assert_eq "A5-source" "$(fog_field "$t" "source=")" "off"
  assert_eq "A5-reason" "$(fog_reason "$t")" "world geometry is filtered, so the fogged set is not the map"
  assert_eq "A5-geometry" "$(line_field "$t" "gpu: world geometry=" "geometry=")" "terrain"
  assert_eq "A5-start" "$(fog_field "$t" "start=")" "$PIN_START"
}

# ------------------------------------------------------------ A6 -- selftest
ST_EXPECTED=28
st_pass=0
st_red=0
st_expect_eq() { # label actual expected
  local label=$1 actual=$2 expected=$3
  if [ "$actual" = "$expected" ]; then
    echo "PASS  $label (= $actual)"; st_pass=$((st_pass + 1))
  else
    echo "RED   $label  -- got [$actual], want [$expected]"; st_red=$((st_red + 1))
  fi
}
st_judge_clean() { # label want_ok judgefn args...
  local label=$1 want=$2 out n_ok n_fail
  shift 2
  out=$("$@" 2>&1)
  n_ok=$(printf '%s\n' "$out" | grep -c '^  OK' || true)
  n_fail=$(printf '%s\n' "$out" | grep -c '^  FAIL' || true)
  if [ "$n_fail" -eq 0 ] && [ "$n_ok" -eq "$want" ]; then
    echo "PASS  $label (clean, $n_ok assertions)"; st_pass=$((st_pass + 1))
  else
    echo "RED   $label  -- want $want clean assertions, got ok=$n_ok fail=$n_fail"
    printf '%s\n' "$out" | sed 's/^/        /'
    st_red=$((st_red + 1))
  fi
}
# A mutant must go red on the assertion it was aimed at. The count matters too: a
# mutant that trips five assertions says the assertions are not independent, and a
# mutant that trips none says they do not observe what they claim to.
st_judge_red_n() { # label want_red_label want_fail_count judgefn args...
  local label=$1 want=$2 nwant=$3 out n_fail got
  shift 3
  out=$("$@" 2>&1)
  n_fail=$(printf '%s\n' "$out" | grep -c '^  FAIL' || true)
  got=$(printf '%s\n' "$out" | grep -c "^  FAIL $want =" || true)
  if [ "$got" -ge 1 ] && [ "$n_fail" -eq "$nwant" ]; then
    echo "PASS  $label (red on $want, $n_fail assertion(s))"; st_pass=$((st_pass + 1))
  else
    echo "RED   $label  -- want $nwant red, one of them on '$want'; got fail=$n_fail named=$got"
    printf '%s\n' "$out" | sed 's/^/        /'
    st_red=$((st_red + 1))
  fi
}
st_judge_red() { # label want_red_label judgefn args...  (exactly one red)
  local label=$1 want=$2
  shift 2
  st_judge_red_n "$label" "$want" 1 "$@"
}
make_fixture_bmps() { # dir
  "$PYTHON" - "$1" <<'PYEOF'
import os, struct, sys
d = sys.argv[1]
w = h = 100
stride = ((w * 3 + 3) // 4) * 4
BASE = (40, 60, 80)
FOG = (74, 83, 102)
def write(name, n_diff):
    buf = bytearray(h * stride)
    for p in range(w * h):
        y, x = divmod(p, w)
        o = y * stride + x * 3
        buf[o], buf[o + 1], buf[o + 2] = BASE
    for p in range(n_diff):
        y, x = divmod(p, w)
        o = y * stride + x * 3
        buf[o], buf[o + 1], buf[o + 2] = FOG
    size = 54 + len(buf)
    hdr = b'BM' + struct.pack('<IHHI', size, 0, 0, 54)
    dib = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(buf), 2835, 2835, 0, 0)
    with open(os.path.join(d, name), 'wb') as f:
        f.write(hdr + dib + bytes(buf))
write('a.bmp', 0)        # reference frame
write('b_same.bmp', 0)   # identical -> A3-diff-nonzero must go red
write('c_one.bmp', 1)    # 0.01 percent -> below the 0.05 percent floor -> A3-diff-floor red
write('d_big.bmp', 100)  # 1.00 percent -> above the floor, clean
write('e_alt.bmp', 200)  # 2.00 percent, differs from both a.bmp and d_big.bmp
PYEOF
}
selftest() {
  local fix=$SCRATCH/selftest
  mkdir -p "$fix" || { echo "FATAL: cannot create $fix" >&2; exit 2; }
  make_fixture_bmps "$fix" || { echo "FATAL: bitmap fixture generation failed" >&2; exit 2; }
  cat > "$fix/a1.trace" <<'EOF'
17000.0 gpu: world geometry=all blend=on
17000.0 gpu: world fog enabled=1 start=1000.00 end=16000.00 maxdensity=1.000 color=74,83,102 boxScaleXZ=0.000488 boxScaleZ=0.001250 verticesFogged=3600 featureMaskFogBit=32 source=bsp passes=world-brush-only reason=-
EOF
  cat > "$fix/a2.trace" <<'EOF'
17000.0 gpu: world geometry=all blend=on
17000.0 gpu: world fog enabled=0 start=1000.00 end=16000.00 maxdensity=1.000 color=74,83,102 boxScaleXZ=0.000488 boxScaleZ=0.001250 verticesFogged=0 featureMaskFogBit=32 source=off passes=world-brush-only reason=default off; --world-fog turns it on
EOF
  cat > "$fix/a4.trace" <<'EOF'
17000.0 gpu: world geometry=all blend=on
17000.0 gpu: world fog enabled=1 start=16.00 end=128.00 maxdensity=1.000 color=74,83,102 boxScaleXZ=0.000488 boxScaleZ=0.001250 verticesFogged=3600 featureMaskFogBit=32 source=cli passes=world-brush-only reason=-
EOF
  cat > "$fix/a5.trace" <<'EOF'
17000.0 gpu: world geometry=terrain blend=on
17000.0 gpu: world fog enabled=0 start=1000.00 end=16000.00 maxdensity=1.000 color=74,83,102 boxScaleXZ=0.000488 boxScaleZ=0.001250 verticesFogged=0 featureMaskFogBit=32 source=off passes=world-brush-only reason=world geometry is filtered, so the fogged set is not the map
EOF
  cat > "$fix/geometry-only.trace" <<'EOF'
17000.0 gpu: world geometry=all blend=on
EOF

  echo "== fog-check --selftest: clean fixtures =="
  st_judge_clean "A1 clean" 14 judge_a1 "$fix/a1.trace" 0 "$fix/a.bmp"
  st_judge_clean "A2 clean" 12 judge_a2 "$fix/a2.trace" 0 "$fix/a.bmp"
  st_judge_clean "A3 clean" 3 judge_a3 "$fix/a.bmp" "$fix/d_big.bmp"
  st_judge_clean "A4 clean" 13 judge_a4 "$fix/a4.trace" 0 "$fix/e_alt.bmp" "$fix/a.bmp" "$fix/d_big.bmp"
  st_judge_clean "A5 clean" 8 judge_a5 "$fix/a5.trace" 0

  echo "== fog-check --selftest: parse helpers (a positive and a must-fail each) =="
  st_expect_eq "parse:line_field/positive" "$(fog_field "$fix/a1.trace" "enabled=")" "1"
  st_expect_eq "parse:line_field/negative-absent-key" "$(fog_field "$fix/a1.trace" "absent_key=")" "-"
  st_expect_eq "parse:fog_reason/positive" "$(fog_reason "$fix/a2.trace")" "default off; --world-fog turns it on"
  st_expect_eq "parse:fog_reason/negative-empty-is-dash" "$(fog_reason "$fix/a1.trace")" "-"
  st_expect_eq "parse:fog_reason/negative-no-line" "$(fog_reason "$fix/geometry-only.trace")" ""
  st_expect_eq "parse:fog_lines/positive" "$(fog_lines "$fix/a1.trace")" "1"
  st_expect_eq "parse:fog_lines/negative" "$(fog_lines "$fix/geometry-only.trace")" "0"

  echo "== fog-check --selftest: mutants (each must go red on ONE named assertion) =="
  sed 's/enabled=1/enabled=0/' "$fix/a1.trace" > "$fix/m01.trace"
  st_judge_red "A1 mutated: enabled flipped" "A1-enabled" judge_a1 "$fix/m01.trace" 0 "$fix/a.bmp"
  sed 's/source=bsp/source=cli/' "$fix/a1.trace" > "$fix/m02.trace"
  st_judge_red "A1 mutated: source is not the bsp" "A1-source" judge_a1 "$fix/m02.trace" 0 "$fix/a.bmp"
  sed 's/start=1000.00/start=0.00/' "$fix/a1.trace" > "$fix/m03.trace"
  st_judge_red "A1 mutated: start is the struct default" "A1-start" judge_a1 "$fix/m03.trace" 0 "$fix/a.bmp"
  sed 's/verticesFogged=3600/verticesFogged=0/' "$fix/a1.trace" > "$fix/m04.trace"
  st_judge_red "A1 mutated: nothing fogged" "A1-vertices" judge_a1 "$fix/m04.trace" 0 "$fix/a.bmp"
  sed 's/reason=-/reason=default off; --world-fog turns it on/' "$fix/a1.trace" > "$fix/m05.trace"
  st_judge_red "A1 mutated: reason says off while enabled=1" "A1-reason" judge_a1 "$fix/m05.trace" 0 "$fix/a.bmp"
  cat "$fix/a1.trace" "$fix/a1.trace" > "$fix/m06.trace"
  st_judge_red "A1 mutated: two fog lines" "A1-line-count" judge_a1 "$fix/m06.trace" 0 "$fix/a.bmp"
  sed 's/gpu: world geometry=all/gpu: world geometry=none/' "$fix/a1.trace" > "$fix/m07.trace"
  st_judge_red "A1 mutated: geometry was not all" "A1-geometry" judge_a1 "$fix/m07.trace" 0 "$fix/a.bmp"
  sed 's/enabled=0/enabled=1/' "$fix/a2.trace" > "$fix/m08.trace"
  st_judge_red "A2 mutated: off-run reports enabled" "A2-enabled" judge_a2 "$fix/m08.trace" 0 "$fix/a.bmp"
  sed 's/reason=default off; --world-fog turns it on/reason=-/' "$fix/a2.trace" > "$fix/m09.trace"
  st_judge_red "A2 mutated: off-run lost its reason" "A2-reason" judge_a2 "$fix/m09.trace" 0 "$fix/a.bmp"
  sed 's/source=cli/source=bsp/' "$fix/a4.trace" > "$fix/m10.trace"
  st_judge_red "A4 mutated: override reported as bsp" "A4-source" judge_a4 "$fix/m10.trace" 0 "$fix/e_alt.bmp" "$fix/a.bmp" "$fix/d_big.bmp"
  sed 's/start=16.00/start=1000.00/' "$fix/a4.trace" > "$fix/m11.trace"
  st_judge_red "A4 mutated: override did not land" "A4-start" judge_a4 "$fix/m11.trace" 0 "$fix/e_alt.bmp" "$fix/a.bmp" "$fix/d_big.bmp"
  sed 's/color=74,83,102/color=255,255,255/' "$fix/a4.trace" > "$fix/m12.trace"
  st_judge_red "A4 mutated: override invented a colour" "A4-color" judge_a4 "$fix/m12.trace" 0 "$fix/e_alt.bmp" "$fix/a.bmp" "$fix/d_big.bmp"
  sed 's/reason=world geometry is filtered, so the fogged set is not the map/reason=default off; --world-fog turns it on/' "$fix/a5.trace" > "$fix/m13.trace"
  st_judge_red "A5 mutated: filtered run blames the switch" "A5-reason" judge_a5 "$fix/m13.trace" 0
  sed 's/gpu: world geometry=terrain/gpu: world geometry=all/' "$fix/a5.trace" > "$fix/m14.trace"
  st_judge_red "A5 mutated: filtered run claims the whole map" "A5-geometry" judge_a5 "$fix/m14.trace" 0
  # Zero differing pixels falsifies BOTH A3 claims (something moved / enough moved);
  # the mutation is expected to say so twice rather than have one hide the other.
  st_judge_red_n "A3 mutated: identical frames" "A3-diff-nonzero" 2 judge_a3 "$fix/a.bmp" "$fix/b_same.bmp"
  st_judge_red "A3 mutated: fog touched one pixel" "A3-diff-floor" judge_a3 "$fix/a.bmp" "$fix/c_one.bmp"

  echo
  echo "FOG-GATE-SELFTEST: $st_pass pass, $st_red red (expected $ST_EXPECTED pass)"
  if [ "$st_red" -ne 0 ] || [ "$st_pass" -ne "$ST_EXPECTED" ]; then
    echo "FOG-GATE-SELFTEST=FAIL"
    exit 1
  fi
  echo "FOG-GATE-SELFTEST=PASS ($st_pass cases)"
  exit 0
}

if [ "$SELFTEST" -eq 1 ]; then
  selftest
fi

# ------------------------------------------------------------ the real run
fatal() { echo "FATAL: $*" >&2; exit 2; }

echo "== fog-check (T101) =="
echo "   product : $PRODUCT"
echo "   map     : $MAP ($BSP under $TF)"
echo "   camera  : $MATRIX"
echo "   out     : $OUT"
echo "   lock    : owner=$LOCK_OWNER timeout=$FOG_TIMEOUT HOLD_LOCK=$HOLD_LOCK"

[ -x "$PYTHON" ] || fatal "python not found: $PYTHON"
[ -f "$PRODUCT" ] || fatal "product not built: $PRODUCT (bash build-target.sh tf2_demo_native)"
[ -f "$TF/$BSP" ] || fatal "map not found: $TF/$BSP"
[ -f "$MATRIX" ] || fatal "camera record not found: $MATRIX"
[ -f "$LOCK" ] || fatal "host lock not found: $LOCK"
command -v sha256sum >/dev/null 2>&1 || fatal "sha256sum not on PATH"
command -v cygpath >/dev/null 2>&1 || fatal "cygpath not on PATH (this gate expects Git Bash)"

DEMO=$(sed -n 's/^demo=//p' "$MATRIX" | head -1)
[ -n "$DEMO" ] || fatal "no demo= line in $MATRIX"
[ -f "$DEMO" ] || fatal "demo not found: $DEMO"
TICK=$(sed -n 's/^camera_tick=//p' "$MATRIX" | head -1)
TICK_SOURCE=explicit
case $TICK in
  ''|*[!0-9]*)
    # D-74: no camera.txt under evidence/t62-map-matrix carries a camera_tick=
    # line -- measured 2026-11-10, 0 of 5 -- so requiring one made this gate
    # refuse every fixture in the repository (first real run: rc=2, FATAL: no
    # numeric camera_tick= line). world-geometry-budget-check.sh:200 meets the
    # same absence and falls back to its own default. Derive the tick from the
    # fixture's own source= line (source=product-orient-run-tick2000), say it was
    # derived, and refuse only when it is absent: a guessed tick would move the
    # camera to another frame and A3/A4 would compare two different moments.
    TICK=$(sed -n 's/^source=.*tick\([0-9][0-9]*\).*/\1/p' "$MATRIX" | head -1)
    TICK_SOURCE=derived
    ;;
esac
case $TICK in ''|*[!0-9]*) fatal "no numeric camera_tick= line in $MATRIX and no tick<N> in its source= line" ;; esac
CAM=$(sed -n 's/^debug_camera_args=//p' "$MATRIX" | head -1)
[ -n "$CAM" ] || fatal "no debug_camera_args= line in $MATRIX"

PRODUCT_SHA=$(sha256_file "$PRODUCT")
DEMO_SHA=$(sha256_file "$DEMO")
BSP_SHA=$(sha256_file "$TF/$BSP")
[ -n "$PRODUCT_SHA" ] || fatal "cannot hash $PRODUCT"
[ -n "$DEMO_SHA" ] || fatal "cannot hash $DEMO"
[ -n "$BSP_SHA" ] || fatal "cannot hash $TF/$BSP"

if [ "$HOLD_LOCK" -eq 1 ]; then
  lock_state=$(bash "$LOCK" status 2>/dev/null)
  case $lock_state in
    *"owner=$LOCK_OWNER"*) ok "A0-hold-lock = the lock is held by $LOCK_OWNER" ;;
    *) fatal "HOLD_LOCK=1 but the lock is not held by $LOCK_OWNER" ;;
  esac
fi

ok "A0-product = $PRODUCT sha256=$PRODUCT_SHA"
ok "A0-demo = $(basename "$DEMO") sha256=$DEMO_SHA"
ok "A0-bsp = $BSP sha256=$BSP_SHA"
ok "A0-camera = tick=$TICK ($TICK_SOURCE) speed=$SPEED args=[$CAM]"
ok "A0-lock = owner=$LOCK_OWNER timeout=$FOG_TIMEOUT s"

mkdir -p "$OUT" || fatal "cannot create $OUT"
printf 'tag\trc\texe_sha256\tdemo_sha256\tbsp_sha256\ttrace\ttrace_sha256\tbmp\tbmp_sha256\tlog\n' > "$OUT/fog-readings.tsv"

# The product wants two ini files it may write back. Each run gets its own copies in
# the scratch dir so a run can never edit the repository's own settings; the sources
# are the build output when present, otherwise empty files (product defaults). Which
# one was used is printed, because "empty settings" is an assumption this gate has
# not proven -- see plan.md section 3.
SETTINGS_SRC=empty
UI_SRC=empty
for src in native/build-nmake/settings.ini settings.ini; do
  if [ -s "$src" ]; then cp -f "$src" "$SETTINGS_FILE"; SETTINGS_SRC=$src; break; fi
done
for src in native/build-nmake/ui.ini ui.ini; do
  if [ -s "$src" ]; then cp -f "$src" "$UI_FILE"; UI_SRC=$src; break; fi
done
[ -s "$SETTINGS_FILE" ] || : > "$SETTINGS_FILE"
[ -s "$UI_FILE" ] || : > "$UI_FILE"
ok "A0-settings = settings from $SETTINGS_SRC, ui from $UI_SRC"

locked() { # command-string
  if [ "$HOLD_LOCK" -eq 1 ]; then
    bash -c "$1"
  else
    bash "$LOCK" run "$LOCK_OWNER" bash -c "$1"
  fi
}

LAST_TRACE=
LAST_BMP=
LAST_LOG=
LAST_RC=1
run_capture() { # tag extra-args...
  local tag=$1 attempt rc csv
  shift
  LAST_TRACE=$OUT/$tag.trace
  LAST_BMP=$OUT/$tag.bmp
  LAST_LOG=$OUT/$tag.log
  csv=$OUT/$tag.csv
  rm -f "$LAST_TRACE" "$LAST_BMP"
  for attempt in 1 2 3; do
    local cmd
    cmd=$(printf "%q " TF2_NATIVE_TRACE="$(cygpath -m "$LAST_TRACE")" timeout "$FOG_TIMEOUT" "$PRODUCT" \
      --tf-root "$TF" --audio-device 6 --audio-disabled --hidden-window \
      --settings-file "$(cygpath -m "$SETTINGS_FILE")" --ui-settings-file "$(cygpath -m "$UI_FILE")" \
      --demo "$DEMO" --playback-speed "$SPEED" --capture-tick "$TICK" \
      --capture-frame "$(cygpath -m "$LAST_BMP")" --metrics-file "$(cygpath -m "$csv")" $CAM "$@")
    locked "$cmd" >"$LAST_LOG" 2>&1
    rc=$?
    LAST_RC=$rc
    if [ "$rc" -eq 0 ] && [ -s "$LAST_TRACE" ] && [ -s "$LAST_BMP" ]; then
      echo "  ..   $tag attempt=$attempt rc=$rc fog-lines=$(fog_lines "$LAST_TRACE")"
      return 0
    fi
    echo "  ..   $tag attempt=$attempt rc=$rc (retrying; log $LAST_LOG)"
  done
  echo "  ..   $tag gave up after 3 attempts (log $LAST_LOG)"
  return 1
}

evidence_row() { # tag
  printf '%s\trc=%s\texe_sha256=%s\tdemo_sha256=%s\tbsp_sha256=%s\ttrace=%s\ttrace_sha256=%s\tbmp=%s\tbmp_sha256=%s\tlog=%s\n' \
    "$1" "$LAST_RC" "$PRODUCT_SHA" "$DEMO_SHA" "$BSP_SHA" "$LAST_TRACE" "$(sha256_file "$LAST_TRACE")" \
    "$LAST_BMP" "$(sha256_file "$LAST_BMP")" "$LAST_LOG" >> "$OUT/fog-readings.tsv"
}

echo
echo "-- A1: --world-fog  (the map's own fog)"
run_capture fog-on --world-fog
judge_a1 "$LAST_TRACE" "$LAST_RC" "$LAST_BMP"
evidence_row fog-on

echo
echo "-- A2: no --world-fog  (the pass is off by default)"
run_capture fog-off
judge_a2 "$LAST_TRACE" "$LAST_RC" "$LAST_BMP"
evidence_row fog-off

echo
echo "-- A3: the two frames"
judge_a3 "$OUT/fog-on.bmp" "$OUT/fog-off.bmp"

echo
echo "-- A4: --fog-start $OVERRIDE_START --fog-end $OVERRIDE_END"
run_capture fog-cli --world-fog --fog-start "$OVERRIDE_START" --fog-end "$OVERRIDE_END"
judge_a4 "$LAST_TRACE" "$LAST_RC" "$LAST_BMP" "$OUT/fog-on.bmp" "$OUT/fog-off.bmp"
evidence_row fog-cli

echo
echo "-- A5: --world-fog with --world-geometry terrain  (must refuse)"
run_capture fog-terrain --world-fog --world-geometry terrain
judge_a5 "$LAST_TRACE" "$LAST_RC"
evidence_row fog-terrain

echo
echo "== summary =="
if [ "$judges" -eq "$MIN_JUDGES" ]; then ok "FOG-judges = $judges"; else bad "FOG-judges = $judges (want $MIN_JUDGES)"; fi
assert_int_ge "FOG-assertions" "$assertions" "$MIN_ASSERTIONS"
for tag in fog-on fog-off fog-cli fog-terrain; do
  line=$(grep -a "gpu: world fog " "$OUT/$tag.trace" 2>/dev/null | head -1)
  printf 'READ %-11s %s\n' "$tag" "$(printf '%s\n' "$line" | sed 's/.*gpu: world fog //')"
done
echo
echo "EVIDENCE table: $OUT/fog-readings.tsv"
awk -F'\t' 'NR > 1 { printf "EVIDENCE %-11s rc=%s trace_sha256=%s bmp_sha256=%s\n", $1, $2, $7, $9 }' "$OUT/fog-readings.tsv"
echo
if [ "$failures" -ne 0 ]; then
  echo "FOG-CHECK=FAIL ($assertions assertions, $failures failed)"
  exit 1
fi
echo "FOG-CHECK=PASS ($assertions assertions)"
exit 0
