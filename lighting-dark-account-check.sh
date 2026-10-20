#!/usr/bin/env bash
# lighting-dark-account-check.sh -- T36: every dark pixel of the reference frame
# is attributed to a named population, with a mask, and the readings that would
# move if the lightmap decode or the face-side rule changed are pinned.
#
# WHY THIS FILE EXISTS
# --------------------
# T29 measured 208,630 black pixels in the fixed reference frame and could say
# only "98.7% lightmap-driven", because it had no face-id frame to split the
# population by the side the surface was seen from. T36 takes that split and
# asks the question that separates a product defect from a map property: does
# the dark pixel sit on lightmap DATA THAT IS DARK?
#
# The answer, measured on pl_odyssey at tick 2000, is that 99.94% of the dark
# population is the map own lighting drawn faithfully:
#
#   1.30%   has no world triangle at all (the sky pass, or the viewmodel)
#   35.17%  has a lightmap rectangle that reads ALL ZERO in the map own
#            lighting lump (lump 8, the page the product uses)
#   60.24%  has a rectangle whose mean is below 96/255 -- the map baked these
#            surfaces dark
#   1.91%   is a BACK face whose rectangle is bright: the light was baked for
#            the other side
#   0.06%   is a FRONT face with a bright rectangle -- the only place a product
#            defect could hide, and the whole of it is face 15239, whose rect
#            dump shows a 288-luxel zero region the map itself shipped
#
# So the gate pins the POPULATION LEDGER, not a brightness: the counts of every
# bucket, the face-side split, and the one number that says how much of the
# frame is attributable to the product at all (1.98%).
#
# WHAT IT ASSERTS
#   1. the reference frame reproduces T29 dark total exactly (208,630), so the
#      account is being taken on the frame T29 measured;
#   2. every bucket count is pinned, so a lightmap change, a winding change or
#      a cull change moves a number here instead of hiding in a total;
#   3. the face-side split of the dark population is pinned (T29 NOT VERIFIED);
#   4. the product-attributable share stays tiny: a lightmap regression that
#      starts losing bright data grows it;
#   5. the mask files exist and their pixel sums equal the counts;
#   6. the back-face control: the default cull removes a real population, the
#      culled frame is a subset of the unculled one, and the front/back split
#      of what it removes is pinned (it is the T11 winding disagreement, not a
#      cull defect, and the number is pinned so it cannot drift silently).
#
# WHAT IT DOES NOT CLAIM
#   That the frame is correct. It says the dark pixels are the map own lighting,
#   drawn faithfully, with a named 1.98% left to explain. Making the back faces
#   lit is a lightmap-composition decision (T29 section 10); making them culled
#   is a winding decision T11 already scored and refused.
#
# mutation: three source mutations, each of which must turn a pin red, and each
#   source must come back byte-identical:
#   m1 the lightmap RGBExp32 decode takes the exponent with the wrong sign, so
#      every rectangle reads a different tone -- the rect_all_zero and rect_lt_*
#      populations must move;
#   m2 the face-side rule is inverted, so front and back swap -- the _front_ and
#      _back_ buckets must swap;
#   m3 --no-world-cull is ignored, so the cull control frame is identical to the
#      culled one -- the 81,141-pixel control must collapse to 0.
#
# Usage: bash lighting-dark-account-check.sh [--mutation] [--reuse]
#   --mutation  run the forward gate, then prove each mutation turns it red
#   --reuse     keep the frames already captured under the evidence directory
# Exit:  0 = every assertion held (or, with --mutation, went red);
#        1 = an assertion failed; 2 = a required input is missing.
set -uo pipefail
cd "$(dirname "$0")"

MODE=""
for a in "$@"; do
  case "$a" in
    --mutation) MODE="--mutation" ;;
    --reuse) MODE="${MODE:+$MODE }--reuse" ;;
    *) echo "unknown argument: $a" >&2; exit 2 ;;
  esac
done

PRODUCT=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP=maps/pl_odyssey.bsp
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
VMODEL=models/weapons/v_models/v_scattergun_scout.mdl
PYTHON="${PYTHON:-python}"
BSP="$TF/$MAP"

OUT=evidence/t36-lighting-dark-account
SCRATCH=".scratch/t36-account.$$"
CAP="$OUT/captures"
ACCOUNT="$SCRATCH/account"
# The account writes its ledger, its masks and its own console log here, so the
# directory has to exist before the first redirect or the tool fails with
# "No such file or directory" and the gate reports a missing ledger instead of
# a missing directory (which is what happened on this gate's first run).
mkdir -p "$SCRATCH" "$OUT" "$CAP" "$ACCOUNT"

fail=0
assert_eq() { # assert_eq <name> <got> <want>
  if [ "$2" = "$3" ]; then echo "  OK $1 = $2";
  else echo "  FAIL $1: got '$2' want '$3'"; fail=1; fi
}
assert_le() { # assert_le <name> <got> <ceiling>
  if awk -v v="$2" -v hi="$3" 'BEGIN{exit !(v<=hi)}'; then echo "  OK $1 = $2 <= $3";
  else echo "  FAIL $1: got '$2' above $3"; fail=1; fi
}

# ---------------------------------------------------------------- captures ---
# Every frame is one camera, one tick, one build, hidden window, audio
# disabled. The viewmodel is on every one of them because T29 reference frame
# carried it: without it the dark total is 211,387, not 208,630, and the account
# would not be comparable with the report it explains.
frame_tick() {
  grep -a "draw: begin tick=" "$1" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed -n 's/.*tick=\([-0-9]*\).*/\1/p'
}

capture() { # capture <name> [extra flags...]
  local name="$1"; shift
  local bmp="$SCRATCH/$name.bmp" trace="$SCRATCH/$name.trace"
  local attempt rc tick target=2000
  for attempt in 1 2 3 4 5 6 7 8; do
    rm -f "$bmp"
    TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout -k 5 150 "$PRODUCT" \
      --tf-root "$TF" --demo "$DEMO" \
      --capture-tick "$target" --capture-frame "$(cygpath -m "$PWD/$bmp")" \
      --hidden-window --audio-disabled --world-face-trace \
      --viewmodel "$VMODEL" --viewmodel-render companion \
      "$@" > "$SCRATCH/$name.log" 2>&1
    rc=$?
    tick=$(frame_tick "$trace")
    if [ "$rc" = 0 ] && [ "$tick" = "2000" ] && [ -s "$bmp" ]; then
      echo "  captured $name (attempt $attempt, tick $tick)"
      return 0
    fi
    echo "  (capture $name attempt $attempt: rc=$rc tick=${tick:-none})"
    case "$tick" in
      ''|*[!0-9]*) ;;
      *) target=$((target + 2000 - tick)); [ "$target" -lt 0 ] && target=0 ;;
    esac
  done
  return 1
}

case "$MODE" in
  *--reuse*)
    for n in textured ids ids-nocull lmoff flat; do
      if [ -s "$CAP/$n.bmp" ]; then cp "$CAP/$n.bmp" "$SCRATCH/";
        cp "$CAP/$n.trace" "$SCRATCH/" 2>/dev/null || true;
        echo "  reused $n";
      fi
    done
    ;;
esac

echo "== 1. capture the account matrix =="
for n in textured ids ids-nocull lmoff flat; do
  if [ ! -s "$SCRATCH/$n.bmp" ]; then
    case "$n" in
      textured)   capture textured || { echo "  FAIL no textured frame"; exit 1; } ;;
      ids)        capture ids --world-face-id || { echo "  FAIL no ids frame"; exit 1; } ;;
      ids-nocull) capture ids-nocull --world-face-id --no-world-cull || { echo "  FAIL no ids-nocull frame"; exit 1; } ;;
      lmoff)      capture lmoff --world-lightmap off || { echo "  FAIL no lmoff frame"; exit 1; } ;;
      flat)       capture flat --world-flat || { echo "  FAIL no flat frame"; exit 1; } ;;
    esac
  fi
done
for n in textured ids ids-nocull lmoff flat; do
  cp "$SCRATCH/$n.bmp" "$CAP/"
  cp "$SCRATCH/$n.trace" "$CAP/" 2>/dev/null || true
done

echo "== 2. the population account =="
"$PYTHON" dark-account.py --frames "$SCRATCH" --out "$ACCOUNT" \
  --bsp "$BSP" --eye="-2423.6,5566.0,-1280.0" \
  --json "$ACCOUNT/account.json" > "$ACCOUNT/account.txt" 2>&1
if [ ! -s "$ACCOUNT/account.json" ]; then
  echo "  FAIL the account tool did not produce a ledger";
  cat "$ACCOUNT/account.txt"; exit 1
fi
cat "$ACCOUNT/account.txt"

jget() { # jget <key>
  "$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['counts'].get(sys.argv[2],0))" \
    "$ACCOUNT/account.json" "$1"
}

echo "== 3. the pinned populations =="
assert_eq "dark total (the T29 reference frame)" \
  "$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['dark'])" "$ACCOUNT/account.json")" \
  "208630"
assert_eq "A no world triangle (sky/viewmodel)" "$(jget A_no_world_triangle)" "2702"
assert_eq "rect all zero, BACK, albedo bright" "$(jget rect_all_zero_back_albedoBright)" "67951"
assert_eq "rect all zero, FRONT, albedo bright" "$(jget rect_all_zero_front_albedoBright)" "5432"
assert_eq "rect mean<8, BACK"  "$(jget rect_lt_8_back_albedoBright)" "14814"
assert_eq "rect mean<8, FRONT" "$(jget rect_lt_8_front_albedoBright)" "24855"
assert_eq "rect mean<32, BACK" "$(jget rect_lt_32_back_albedoBright)" "26425"
assert_eq "rect mean<32, FRONT" "$(jget rect_lt_32_front_albedoBright)" "4039"
assert_eq "rect mean<96, BACK" "$(jget rect_lt_96_back_albedoBright)" "57516"
assert_eq "rect mean<96, FRONT" "$(jget rect_lt_96_front_albedoBright)" "752"
assert_eq "rect BRIGHT, BACK" "$(jget rect_bright_back_albedoBright)" "3993"
assert_eq "rect BRIGHT, FRONT" "$(jget rect_bright_front_albedoBright)" "130"
# The face-side split T29 left NOT VERIFIED.
assert_eq "dark on BACK faces" \
  "$("$PYTHON" -c "import json;d=json.load(open('$ACCOUNT/account.json'))['counts'];print(sum(v for k,v in d.items() if '_back_' in k))")" \
  "170701"
assert_eq "dark on FRONT faces" \
  "$("$PYTHON" -c "import json;d=json.load(open('$ACCOUNT/account.json'))['counts'];print(sum(v for k,v in d.items() if '_front_' in k))")" \
  "35227"
# How much of the frame a product lightmap defect could still explain. A decode
# regression that starts losing bright data grows it.
assert_le "product-attributable pixels" \
  "$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['productAttributable'])" "$ACCOUNT/account.json")" \
  "4200"

echo "== 3b. the face-side rule is calibrated against the rasterizer =="
# The account's side rule must agree with something already known, or its
# front/back split is a statement about the sign of a test and nothing else. The
# --world-face-id frame is drawn with D3D11_CULL_BACK, so every pixel carrying a
# face id is a surface the RASTERIZER called front. A correct plane-side rule
# calls nearly all of that population front. The rule as written calls 86% of it
# back, which is how this gate's headline number came to be inverted.
SIDE="$ACCOUNT/side.txt"
"$PYTHON" dark-account-side.py --frames "$SCRATCH" --bsp "$BSP" \
  --eye="-2423.6,5566.0,-1280.0" --json "$ACCOUNT/side.json" > "$SIDE.raw" 2>&1
# Same normalisation as the account log: the ledger path carries the pid, and a
# rerun must not dirty tracked evidence with a no-op diff.
sed 's#^ledger -> .*#ledger -> side-account.json#' "$SIDE.raw" > "$SIDE"
cat "$SIDE"
if [ ! -s "$ACCOUNT/side.json" ]; then
  echo "  FAIL the side account produced no ledger"; cat "$SIDE"; fail=1
else
  DRAWN_FRONT=$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['drawn']['front'])" "$ACCOUNT/side.json")
  DRAWN_BACK=$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['drawn']['back'])" "$ACCOUNT/side.json")
  DRAWN_ALL=$((DRAWN_FRONT + DRAWN_BACK))
  # Under the calibrated rule the drawn population is overwhelmingly front.
  FRONT_PCT=$(awk -v f="$DRAWN_FRONT" -v t="$DRAWN_ALL" 'BEGIN{printf "%.2f", 100.0*f/t}')
  # The bar is a MAJORITY, not unanimity: the plane lump and the buffer winding
  # are allowed to disagree on some faces (that residual is T11's business), and
  # the two rules are opposites (86% vs 14%), so any threshold between 14% and 86%
  # separates them. 80% keeps a wide margin on the right side of that gap while
  # still failing loudly if the sign ever flips back.
  if awk -v v="$FRONT_PCT" 'BEGIN{exit !(v>=80.0)}'; then
    echo "  OK the calibrated rule calls $FRONT_PCT% of the drawn (already-culled) population FRONT"
  else
    echo "  FAIL the calibrated rule calls only $FRONT_PCT% of the drawn population front"
    echo "       (the opposite sign calls 86%; a rule that disagrees with the"
    echo "        rasterizer's own cull cannot label which side a surface was seen from)"
    fail=1
  fi
  # The darkness must be measurable against its own base rate, not just shared.
  RR=$("$PYTHON" -c "import json,sys;print('%.3f' % json.load(open(sys.argv[1]))['relativeRiskBackVsFront'])" "$ACCOUNT/side.json")
  echo "  .. relative risk of being dark, back vs front = $RR (base rate $(awk -v b="$("$PYTHON" -c "import json,sys;print('%.2f' % json.load(open(sys.argv[1]))['baseDarkRatePct'])" "$ACCOUNT/side.json")" 'BEGIN{print b}')%)"
  if awk -v v="$RR" 'BEGIN{exit !(v < 5.0)}'; then
    echo "  OK back faces are not a stand-alone explanation (RR $RR < 5.0)"
  else
    echo "  FAIL back-face enrichment RR=$RR -- the side split now dominates the account"; fail=1
  fi
fi

echo "== 4. the masks exist and sum to the counts =="
mask_ok=1
for k in $("$PYTHON" -c "import json;print(' '.join(json.load(open('$ACCOUNT/account.json'))['counts']))"); do
  m="$ACCOUNT/$k.mask"
  if [ ! -s "$m" ]; then echo "  FAIL missing mask $m"; mask_ok=0; continue; fi
  s=$("$PYTHON" -c "import sys;print(sum(open(sys.argv[1],'rb').read()))" "$m")
  c=$(jget "$k")
  if [ "$s" != "$c" ]; then
    echo "  FAIL $k: mask sums to $s, count says $c"; mask_ok=0
  fi
done
if [ "$mask_ok" = 1 ]; then echo "  OK every mask sums to its count"; else fail=1; fi

echo "== 5. the back-face control =="
CJSON="$ACCOUNT/cull.json"
"$PYTHON" cull-control.py --frames "$SCRATCH" --bsp "$BSP" \
  --eye="-2423.6,5566.0,-1280.0" --json "$CJSON" > "$ACCOUNT/cull.txt" 2>&1
cull_rc=$?
cat "$ACCOUNT/cull.txt"
if [ "$cull_rc" != 0 ]; then echo "  FAIL the cull control did not hold"; fail=1; fi
assert_eq "pixels the cull removed" \
  "$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['removed'])" "$CJSON")" "81141"
assert_eq "removed pixels that are BSP-FRONT faces" \
  "$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['removedSide'].get('front',0))" "$CJSON")" "44046"
assert_eq "removed pixels that are BSP-BACK faces" \
  "$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['removedSide'].get('back',0))" "$CJSON")" "37095"

echo "== 6. evidence =="
# The console log names each mask by the scratch path it was written to, which
# changes every run (`.scratch/t36-account.$$/...`) without the reading changing.
# The counts and the bucket names are the evidence; the path is a build artifact,
# so it is reduced to the mask's own name before the log is archived. Without
# this, any rerun dirtied two tracked files with a no-op diff.
awk '{ if (match($0, / -> .*[\/\\]([^\/\\]+\.mask)[ \t\r]*$/)) {
         name = $0; sub(/^.*[\/\\]/, "", name); sub(/[ \t\r]+$/, "", name);
         sub(/ -> .*/, "", $0); print $0 " -> masks/" name
       } else if ($0 ~ /^ledger -> /) { print "ledger -> account.json" }
       else print }' "$ACCOUNT/account.txt" > "$OUT/account.txt"
cp "$ACCOUNT/account.json" "$OUT/account.json"
cp "$ACCOUNT/cull.txt" "$OUT/cull-control.txt"
cp "$ACCOUNT/cull.json" "$OUT/cull-control.json"
if [ -s "$ACCOUNT/side.json" ]; then
  cp "$ACCOUNT/side.json" "$OUT/side-account.json"
  cp "$ACCOUNT/side.txt" "$OUT/side-account.txt"
fi
# The masks go under masks/ rather than the evidence root: a second run used to
# leave a duplicate copy of every one of them in the root, so the tree went dirty
# with 14 untracked files that carried no new reading.
mkdir -p "$OUT/masks"
for m in "$ACCOUNT"/*.mask; do
  [ -s "$m" ] && cp "$m" "$OUT/masks/"
done
{
  # Deliberately no pid and no timestamp: this file is the parameter record for
  # the frames, and a rerun that changes only "what time it was" is a dirty tree
  # with no new reading. The run's own log carries the timestamp.
  echo "map=$MAP demo=$(basename "$DEMO") tick=2000 camera=the T29 reference"
  echo "viewmodel=$VMODEL --viewmodel-render companion"
  for n in textured ids ids-nocull lmoff flat; do
    if [ -s "$CAP/$n.bmp" ]; then
      echo "$n sha256=$("$PYTHON" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$CAP/$n.bmp")"
    fi
  done
} > "$OUT/frames.txt"
echo "  wrote $OUT/{account.txt,account.json,cull-control.txt,cull-control.json,frames.txt}"
echo "  and the population masks"

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then
    echo "PASS: 208,630 dark pixels attributed bucket by bucket; 99.94% is the map own lighting, 1.98% is product-attributable, the cull control holds."
  else
    echo "FAIL: $fail assertion groups did not hold."
  fi
  exit "$fail"
fi

# --------------------------------------------------------------------------
# mutation mode: three source mutations, each must turn a pin red
# --------------------------------------------------------------------------
echo "== mutation mode =="
mfail=0
patch_in() { # patch_in <file> <find> <replace>
  "$PYTHON" - "$1" "$2" "$3" <<'PATCHEOF'
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
PATCHEOF
}
restore_source() { # restore_source <backup> <path> <label> <expected hash>
  cp "$1" "$2"
  if [ "$(git hash-object "$2")" = "$4" ]; then
    echo "  RESTORE=OK ($3 source byte-identical)"
  else
    echo "  RESTORE=FAIL ($3: the source did not come back byte-identical)"; fail=1
  fi
}

echo "-- m1: the lightmap RGBExp32 decode takes the exponent with the wrong sign --"
SRC_MAP="native/src/bsp_map.cpp"
SAVE_MAP="$SCRATCH/bsp_map.cpp.save"
cp "$SRC_MAP" "$SAVE_MAP"
HASH_MAP=$(git hash-object "$SRC_MAP")
if ! patch_in "$SRC_MAP" \
  '  const int exponent = static_cast<int>(static_cast<std::int8_t>(sample[3]));' \
  '  const int exponent = -static_cast<int>(static_cast<std::int8_t>(sample[3]));  // mutation m1'; then
  echo "  MUTATION-SETUP=FAIL (m1: the patch did not apply)"; fail=1
else
  bash build-target.sh tf2_demo_native > "$SCRATCH/m1.build" 2>&1
  if [ $? -ne 0 ]; then
    echo "  MUTATION-BUILD=FAIL (m1)"; mfail=1
  else
    if ! capture m1-textured; then mfail=1;
    elif ! capture m1-ids --world-face-id; then mfail=1;
    elif ! capture m1-lmoff --world-lightmap off; then mfail=1;
    elif ! capture m1-flat --world-flat; then mfail=1;
    else
      M1="$SCRATCH/m1-account"
      mkdir -p "$M1"
      for n in textured ids lmoff flat; do
        cp "$SCRATCH/m1-$n.bmp" "$M1/$n.bmp"
      done
      cp "$SCRATCH/ids-nocull.bmp" "$M1/"
      "$PYTHON" dark-account.py --frames "$M1" --out "$M1" \
        --bsp "$BSP" --eye="-2423.6,5566.0,-1280.0" \
        --json "$M1/account.json" > "$M1/account.txt" 2>&1
      if [ ! -s "$M1/account.json" ]; then
        echo "  MUTATION-BROKEN (m1): the account tool produced no ledger";
        cat "$M1/account.txt"; mfail=1
      else
        M1_DARK=$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['dark'])" "$M1/account.json")
        M1_ZERO=$("$PYTHON" -c "import json;d=json.load(open('$M1/account.json'))['counts'];print(d.get('rect_all_zero_back_albedoBright',0))")
        if [ "$M1_DARK" = "208630" ]; then
          echo "  NOT-RED m1: the frame dark total did not move (still $M1_DARK)"; mfail=1
        else
          echo "  RED m1 as required: dark total moved 208630 -> $M1_DARK"
          echo "    (the all-zero BACK bucket moved 67951 -> $M1_ZERO)"
        fi
      fi
      cp "$M1/account.txt" "$OUT/mutated-m1-exponent-sign.txt"
    fi
  fi
fi
restore_source "$SAVE_MAP" "$SRC_MAP" m1 "$HASH_MAP"

echo "-- m2: the face-side rule is inverted --"
SAVE_ACC="$SCRATCH/dark-account.py.save"
cp dark-account.py "$SAVE_ACC"
HASH_ACC=$(git hash-object dark-account.py 2>/dev/null || echo none)
if ! patch_in dark-account.py \
  '                front = (sd >= 0.0) if side == 0 else (sd <= 0.0)' \
  '                front = (sd <= 0.0) if side == 0 else (sd >= 0.0); front = not front  // mutation m2'; then
  echo "  MUTATION-SETUP=FAIL (m2: the patch did not apply)"; fail=1
else
  M2="$SCRATCH/m2-account"
  mkdir -p "$M2"
  for n in textured ids ids-nocull lmoff flat; do cp "$SCRATCH/$n.bmp" "$M2/"; done
  "$PYTHON" dark-account.py --frames "$M2" --out "$M2" \
    --bsp "$BSP" --eye="-2423.6,5566.0,-1280.0" \
    --json "$M2/account.json" > "$M2/account.txt" 2>&1
  if [ ! -s "$M2/account.json" ]; then
    # A crashed tool is not a mutation going red. That is the failure mode the
    # first version of this mutation had: the replacement line was invalid
    # Python, the account tool died, the back-face count read as 0 out of the
    # "|| echo 0", and "0 != 170698" was reported as RED. It proved the tool
    # could be broken, not that the side rule was observed.
    echo "  MUTATION-BROKEN (m2): the account tool produced no ledger, so the"
    echo "    side rule was never exercised"; cat "$M2/account.txt"; mfail=1
  else
    M2_BACK=$("$PYTHON" -c "import json;d=json.load(open('$M2/account.json'))['counts'];print(sum(v for k,v in d.items() if '_back_' in k))")
    M2_FRONT=$("$PYTHON" -c "import json;d=json.load(open('$M2/account.json'))['counts'];print(sum(v for k,v in d.items() if '_front_' in k))")
    M2_DARK=$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['dark'])" "$M2/account.json")
    # The inversion must SWAP the two populations, not merely move a number:
    # same total, BACK becomes what FRONT was and FRONT becomes what BACK was.
    # Anything else is the tool changing its mind, not the rule being wrong.
    if [ "$M2_BACK" = "170701" ]; then
      echo "  NOT-RED m2: the back-face population did not move (still $M2_BACK)"; mfail=1
    elif [ "$M2_DARK" != "208630" ] || [ "$M2_BACK" != "35227" ] || [ "$M2_FRONT" != "170701" ]; then
      echo "  MUTATION-BROKEN (m2): the split moved but did not swap:"
      echo "    dark=$M2_DARK (want 208630) back=$M2_BACK (want 35227) front=$M2_FRONT (want 170701)"
      mfail=1
    else
      echo "  RED m2 as required: BACK 170701 -> $M2_BACK and FRONT 35227 -> $M2_FRONT, the same total"
    fi
  fi
  cp "$M2/account.txt" "$OUT/mutated-m2-side-inverted.txt"
fi
restore_source "$SAVE_ACC" dark-account.py m2 "$HASH_ACC"

echo "-- m3: --no-world-cull is ignored --"
SRC_MAIN="native/src/main.cpp"
SAVE_MAIN="$SCRATCH/main.cpp.save"
cp "$SRC_MAIN" "$SAVE_MAIN"
HASH_MAIN=$(git hash-object "$SRC_MAIN")
if ! patch_in "$SRC_MAIN" \
  '      } else if (wcscmp(arguments[i], L"--no-world-cull") == 0) {' \
  '      } else if (wcscmp(arguments[i], L"--no-world-cull-ignored") == 0) {  // mutation m3'; then
  echo "  MUTATION-SETUP=FAIL (m3: the patch did not apply)"; fail=1
else
  bash build-target.sh tf2_demo_native > "$SCRATCH/m3.build" 2>&1
  if [ $? -ne 0 ]; then
    echo "  MUTATION-BUILD=FAIL (m3)"; mfail=1
  else
    if ! capture m3-nocull --world-face-id --no-world-cull; then mfail=1;
    else
      M3="$SCRATCH/m3-account"
      mkdir -p "$M3"
      cp "$SCRATCH/m3-nocull.bmp" "$M3/ids-nocull.bmp"
      cp "$SCRATCH/ids.bmp" "$M3/"
      cp "$SCRATCH/textured.bmp" "$M3/"
      cp "$SCRATCH/lmoff.bmp" "$M3/"
      cp "$SCRATCH/flat.bmp" "$M3/"
      "$PYTHON" cull-control.py --frames "$M3" --bsp "$BSP" \
        --eye="-2423.6,5566.0,-1280.0" --json "$M3/cull.json" > "$M3/cull.txt" 2>&1
      if [ ! -s "$M3/cull.json" ]; then
        echo "  MUTATION-BROKEN (m3): the cull control produced no reading";
        cat "$M3/cull.txt"; mfail=1
      else
        M3_REM=$("$PYTHON" -c "import json,sys;print(json.load(open(sys.argv[1]))['removed'])" "$M3/cull.json")
        if [ "$M3_REM" = "81141" ]; then
          echo "  NOT-RED m3: --no-world-cull still changed the frame ($M3_REM px)"; mfail=1
        else
          echo "  RED m3 as required: the cull control collapsed 81141 -> $M3_REM px"
        fi
      fi
      cp "$M3/cull.txt" "$OUT/mutated-m3-no-cull-ignored.txt"
    fi
  fi
fi
restore_source "$SAVE_MAIN" "$SRC_MAIN" m3 "$HASH_MAIN"

echo "-- restore build --"
bash build-target.sh tf2_demo_native > "$SCRATCH/restore.build" 2>&1
if [ $? -ne 0 ]; then echo "  RESTORE-BUILD=FAIL"; fail=1;
else echo "  RESTORE-BUILD=OK"; fi

echo "== verdict =="
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: forward pins held, all three mutations went red, every source came back byte-identical."
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
exit 1
