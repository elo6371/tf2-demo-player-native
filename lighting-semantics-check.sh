#!/usr/bin/env bash
# lighting-semantics-check.sh -- T24: the six lighting/shader controls.
#
# WHY THIS FILE EXISTS
# --------------------
# T24's list mixes two kinds of thing and the product treated them alike:
#
#   * pass switches -- `dynamicLighting`, `specular`, `bump` are fields on
#     RenderSettings, multiplied into ONE constant buffer for the whole world;
#   * material keys -- `$bumpmap2`, `%detailtype`, `$ssbump` are parsed from
#     VMTs and refused because the pass has one sampler slot, one atlas uv and
#     no tangent basis.
#
# Before this gate, the only way to reach the three switches was
# `--quality standard`, which flips eight fields at once. A capture from that
# switch cannot attribute a pixel to one semantic, so each switch got its own
# on/off pair and the gate compares ONE field at a time:
#
#   A  baseline (all three off)
#   A2 baseline again                   -> determinism control, must be 0 px
#   B  --world-dynamic-light on
#   C  --world-specular on
#   D  --world-bump on + a named VTF    -> 0 px, and that is the finding
#   E  dynamic on AND bump on
#   F  --world-bump on with no texture  -> requested=1 effective=0, refused
#
# A->B is the dynamic-lighting semantic; A->C the specular one; A->D the bump
# one with no consumer; B->E the bump one with a consumer. D vs A reading 0 is
# not a failure: the bump map only perturbs `n`, and `n` is read by the
# dynamic-lighting, specular and env branches. With all three off, a different
# normal map cannot change a pixel. That dependency is the measurement.
#
# THE DIRECTION MATTERS
#   `bmp-delta-sign.py` counts brighter and darker separately, because the
#   semantic claim is directional. Measured: A->B is brighter=0 darker=1583615,
#   i.e. the "dynamic light" term (0.62+0.38*dot) can only subtract from the
#   baked light. A->C is darker=0 brighter=265448: the lobe only adds. Those two
#   lines are what stops either switch being promoted to a default on the
#   strength of "the picture changed".
#
# WHAT THIS GATE DOES NOT CLAIM
#   That any of the six is visually correct. It pins declared/parsed/requested/
#   effective and the measured frame delta. Per-material specular and per-
#   material bump remain refused by name; `%detailtype` has parse evidence from
#   the probe's fixture and no render evidence anywhere.
#
# mutation: four source mutations, each of which must turn a forward pin red,
# and each source must come back byte-identical:
#   m1 `$bumpmap2` reports applied instead of refused (the silent-replacement
#      defect T19 named),
#   m2 the `%detailtype` read path stops reading the key,
#   m3 the dynamic-light switch also flips specular (two semantics in one run),
#   m4 the bump switch reports effective with no texture bound.
#
# Usage: bash lighting-semantics-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, went red).
set -uo pipefail
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/lighting_semantics_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP=maps/pl_odyssey.bsp
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
BUMP_TEXTURE=materials/brick/brickwall001_normal.vtf
PYTHON="${PYTHON:-python}"

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MODE="${1:-}"
SCRATCH=".scratch/lighting-semantics.$RUN_TAG"
OUT="evidence/lighting-semantics"
mkdir -p "$SCRATCH" "$OUT"

fail=0
assert_eq() { # assert_eq <name> <got> <want>
  if [ "$2" = "$3" ]; then echo "  OK $1 = $2"; else echo "  FAIL $1: got '$2' want '$3'"; fail=1; fi
}
assert_contains() { # assert_contains <name> <text> <needle>
  case "$2" in
    *"$3"*) echo "  OK $1 contains '$3'" ;;
    *) echo "  FAIL $1: '$2' does not contain '$3'"; fail=1 ;;
  esac
}
assert_ge() { # assert_ge <name> <got> <floor>
  if awk -v v="$2" -v lo="$3" 'BEGIN{exit !(v>=lo)}'; then echo "  OK $1 = $2 >= $3";
  else echo "  FAIL $1: got '$2' below $3"; fail=1; fi
}

# A capture reads AND writes the settings file: the product saves the settings it
# ran with when it exits, and `--quality performance` therefore rewrites the
# preset for every later run -- against the machine's real %LOCALAPPDATA% ini
# that is a side effect on the user, and against a shared scratch ini it is a
# capture that inherits the previous capture's preset. Rewritten before every
# capture, so each one starts from the same file and none of them touch the
# machine's. --settings-file is pre-scanned in main.cpp, so it covers the write.
GATE_INI="$SCRATCH/settings.ini"
write_gate_ini() {
  {
    echo "[renderer]"
    echo "preset=1"
    echo "renderScalePercent=100"
    echo "viewModelFov=90"
    echo "vsync=0"
  } > "$GATE_INI"
}

capture() { # capture <name> [extra flags...]
  local name="$1"; shift
  local bmp="$SCRATCH/$name.bmp" trace="$SCRATCH/$name.trace" attempt rc tick
  for attempt in 1 2 3 4 5 6 7 8; do
    rm -f "$bmp"
    write_gate_ini
    TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 300 "$PRODUCT" --tf-root "$TF" \
      --demo "$DEMO" --capture-tick 2000 --capture-frame "$(cygpath -m "$PWD/$bmp")" \
      --audio-disabled --viewmodel models/weapons/v_models/v_scattergun_scout.mdl \
      --viewmodel-render companion --settings-file "$(cygpath -m "$PWD/$GATE_INI")" \
      "$@" > "$SCRATCH/$name.log" 2>&1
    rc=$?
    # `--capture-tick N` means "first draw with tick >= N", so it lands on 2001
    # about half the time. A pin measured at 2000 cannot be compared with 2001:
    # the camera moved. Retry until the draw reports exactly 2000.
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
      | sed -n 's/.*tick=\([-0-9]*\).*/\1/p')
    if [ "$rc" = 0 ] && [ "$tick" = "2000" ] && [ -s "$bmp" ]; then return 0; fi
    echo "  (capture $name attempt $attempt: rc=$rc tick=${tick:-none})"
  done
  return 1
}

kv() { # kv <probe text file> <reading key>
  grep -F "\"reading\":\"$2\"" "$1" 2>/dev/null | head -1 \
    | sed -n 's/.*"value":"\(.*\)"}.*/\1/p'
}
ckdetail() { # ckdetail <self-test text> <check name> -- the check's own detail
  grep -F "\"check\":\"$2\"" "$1" 2>/dev/null | head -1 \
    | sed -n 's/.*"detail":"\([^"]*\)".*/\1/p'
}
lightingline() { grep -a "gpu: world lighting " "$1" 2>/dev/null | head -1 | tr -d '\r'; }
lfield() { # lfield <line> <key>
  printf '%s' "$1" | grep -oE "(^| )$2=[^ ]+" | head -1 | cut -d= -f2-
}
controlline() { # controlline <trace> <control name>
  grep -aF "gpu: world lighting-control control=$2 " "$1" 2>/dev/null | head -1 | tr -d '\r'
}
cfield() { lfield "$1" "$2"; }

# ---------------------------------------------------------------------------
# 0. build the two targets from this tree
# ---------------------------------------------------------------------------
echo "== 0. build =="
bash build-target.sh tf2_demo_native lighting_semantics_probe > "$SCRATCH/build.txt" 2>&1
if [ $? -ne 0 ]; then echo "  FAIL the build did not finish (see $SCRATCH/build.txt)"; exit 1; fi
echo "  OK both targets built"

# ---------------------------------------------------------------------------
# 1. the fixture: every key, including the one the install cannot supply
# ---------------------------------------------------------------------------
echo "== 1. probe self-test (fixture) =="
SELF="$SCRATCH/self.txt"
"$PROBE" --self-test > "$SELF" 2>&1
assert_eq "self-test verdict" "$(kv "$SELF" selfTest)" "true"
assert_eq "self-test check count" "$(grep -c '"ok":true' "$SELF")" "30"
assert_eq "controls count" "$(ckdetail "$SELF" controls.count)" "6"
assert_eq "controls order" "$(ckdetail "$SELF" controls.names)" "the six rows are in the task's order"
assert_eq "bump counts" "$(ckdetail "$SELF" bump.counts)" "2/2 resolved 2"
assert_eq "bump2 refused" "$(ckdetail "$SELF" bump2.refused)" "refused"
assert_eq "ssbump counts" "$(ckdetail "$SELF" ssbump.counts)" "1 declared, 1 resolved"
# The parse evidence for `%detailtype`: the fixture's detail VMT really declares
# it, so a future failure here is the reader and not the install.
assert_eq "detail counts" "$(ckdetail "$SELF" detail.counts)" "2 declared, 1 resolved, 1 missing"
assert_contains "detail type read" "$(ckdetail "$SELF" detail.typeRead)" "1 distinct %detailtype value(s) (first=2)"
assert_eq "detail refused" "$(ckdetail "$SELF" detail.refused)" "refused"
assert_contains "requested-not-effective names the owner" \
  "$(ckdetail "$SELF" pass.bump.reason.names.owner)" "declares no \$bumpmap"
assert_contains "requested-not-effective dynamic" \
  "$(ckdetail "$SELF" pass.dynamic.requestedNotEffective)" "requested but no constant was uploaded"

# ---------------------------------------------------------------------------
# 2. the real map: one ledger, six rows
# ---------------------------------------------------------------------------
echo "== 2. probe on $MAP =="
PROBE_MAP="$SCRATCH/probe-map.txt"
"$PROBE" --map "$TF/$MAP" --tf-root "$TF" --list-bump > "$PROBE_MAP" 2>&1
assert_eq "map first material" "$(kv "$PROBE_MAP" map.firstMaterial)" "WOOD/WALL007A"
assert_eq "map distinct materials" "$(kv "$PROBE_MAP" map.distinctMaterials)" "108"
# The slot's owner: the first material of the map. It declares no $bumpmap,
# which is why `--world-bump on` alone cannot bind anything on this frame.
assert_eq "bump owner" "$(kv "$PROBE_MAP" map.bumpOwner)" "WOOD/WALL007A"
assert_eq "bump owner declares" "$(kv "$PROBE_MAP" map.bumpOwnerDeclared)" "0"
assert_eq "bump owner texture" "$(kv "$PROBE_MAP" map.bumpTexture)" "-"
# $phong: none of this map's world materials ask for a specular lobe, and the
# pass adds one anyway when the switch is on.
assert_eq "\$phong declared" "$(kv "$PROBE_MAP" lighting.phongDeclared)" "0"
assert_eq "dynamicLighting state" "$(kv "$PROBE_MAP" lighting.dynamicLighting.state)" "disabled"
assert_eq "specular state" "$(kv "$PROBE_MAP" lighting.specular.state)" "disabled"
assert_eq "bump state" "$(kv "$PROBE_MAP" lighting.bump.state)" "refused"
assert_eq "bump declared" "$(kv "$PROBE_MAP" lighting.bump.declared)" "44"
assert_eq "bump resolved" "$(kv "$PROBE_MAP" lighting.bump.resolved)" "44"
assert_eq "bump missing" "$(kv "$PROBE_MAP" lighting.bump.missing)" "0"
assert_eq "\$bumpmap2 state" "$(kv "$PROBE_MAP" 'lighting.$bumpmap2.state')" "refused"
assert_eq "\$bumpmap2 declared" "$(kv "$PROBE_MAP" 'lighting.$bumpmap2.declared')" "5"
assert_eq "\$bumpmap2 resolved" "$(kv "$PROBE_MAP" 'lighting.$bumpmap2.resolved')" "5"
assert_eq "%detailtype state" "$(kv "$PROBE_MAP" 'lighting.%detailtype.state')" "refused"
assert_eq "%detailtype declared" "$(kv "$PROBE_MAP" 'lighting.%detailtype.declared')" "15"
assert_eq "%detailtype resolved" "$(kv "$PROBE_MAP" 'lighting.%detailtype.resolved')" "0"
assert_eq "%detailtype parsed" "$(kv "$PROBE_MAP" 'lighting.%detailtype.parsed')" "0"
assert_eq "\$ssbump state" "$(kv "$PROBE_MAP" 'lighting.$ssbump.state')" "refused"
assert_eq "\$ssbump declared" "$(kv "$PROBE_MAP" 'lighting.$ssbump.declared')" "36"
assert_eq "\$ssbump resolved" "$(kv "$PROBE_MAP" 'lighting.$ssbump.resolved')" "36"
assert_eq "ledger summary" "$(kv "$PROBE_MAP" lighting.summary)" \
  "controls=6 applied=0 refused=4 disabled=2 notDeclared=0 phong=0(visible 0, exponents 0/0) bumpOwner=WOOD/WALL007A"

# The 9 `$bumpmap` VTFs that are in the map's own pakfile and nowhere else. The
# product's feature scan called them missing before T24 because the chain's
# pakfile reader held a reference to a block-local vector; this pins the
# attribution so a regression cannot come back as "9 missing" again.
assert_eq "bump paths absent from the chain" "$(kv "$PROBE_MAP" bump.chainMissing)" "9"
assert_eq "bump paths in the map pakfile only" "$(kv "$PROBE_MAP" bump.pakfileOnly)" "9"
assert_eq "bump paths absent everywhere" "$(kv "$PROBE_MAP" bump.absent)" "0"

RES_DETAIL="$SCRATCH/res-detail.txt"
"$PROBE" --tf-root "$TF" --resource materials/overlays/detail006.vmt > "$RES_DETAIL" 2>&1
assert_eq "detail VMT exists" "$(kv "$RES_DETAIL" resource.exists)" "false"
RES_BUMP="$SCRATCH/res-bump.txt"
"$PROBE" --tf-root "$TF" --resource "$BUMP_TEXTURE" > "$RES_BUMP" 2>&1
assert_eq "diagnostic bump texture exists" "$(kv "$RES_BUMP" resource.exists)" "true"

# ---------------------------------------------------------------------------
# 3. the product's reference frame and its six rows
# ---------------------------------------------------------------------------
echo "== 3. reference capture (all three switches off) =="
capture A || { echo "  FAIL the reference capture did not produce tick 2000"; fail=1; }
A_TRACE="$SCRATCH/A.trace"
ALINE=$(lightingline "$A_TRACE")
assert_eq "A switch state" \
  "$(lfield "$ALINE" dynamic)/$(lfield "$ALINE" specular)/$(lfield "$ALINE" bump)" "0/0/0"
assert_eq "A requested" \
  "$(lfield "$ALINE" requestedDynamic)/$(lfield "$ALINE" requestedSpecular)/$(lfield "$ALINE" requestedBump)" "0/0/0"
assert_eq "A bump owner" "$(lfield "$ALINE" bumpOwner)" "WOOD/WALL007A"
assert_eq "A bump texture" "$(lfield "$ALINE" bumpTexture)" "-"
assert_eq "A bump texture source" "$(lfield "$ALINE" bumpTextureSource)" "none"
assert_eq "A bump owner declared" "$(lfield "$ALINE" bumpOwnerDeclared)" "0"

SUMMARY=$(grep -a "gpu: world lighting-summary" "$A_TRACE" | head -1 | tr -d '\r')
assert_contains "A ledger summary" "$SUMMARY" \
  "controls=6 applied=0 refused=4 disabled=2 notDeclared=0 phong=0(visible 0, exponents 0/0)"

DYN=$(controlline "$A_TRACE" dynamicLighting)
assert_eq "A dynamic state" "$(cfield "$DYN" state)" "disabled"
assert_eq "A dynamic scope" "$(cfield "$DYN" materials)/$(cfield "$DYN" triangles)" "100/76231"
SPEC=$(controlline "$A_TRACE" specular)
assert_eq "A specular state" "$(cfield "$SPEC" state)" "disabled"
assert_contains "A specular names the missing \$phong" "$SPEC" "no visible material declares \$phong"
BUMP=$(controlline "$A_TRACE" bump)
assert_eq "A bump counts" \
  "$(cfield "$BUMP" declared)/$(cfield "$BUMP" visible)/$(cfield "$BUMP" resolved)/$(cfield "$BUMP" missing)" \
  "44/43/44/0"
assert_eq "A bump scope" "$(cfield "$BUMP" materials)/$(cfield "$BUMP" triangles)" "43/53334"
B2=$(controlline "$A_TRACE" '$bumpmap2')
assert_eq "A \$bumpmap2 counts" \
  "$(cfield "$B2" declared)/$(cfield "$B2" visible)/$(cfield "$B2" resolved)" "5/5/5"
assert_eq "A \$bumpmap2 scope" "$(cfield "$B2" materials)/$(cfield "$B2" triangles)" "5/13706"
DET=$(controlline "$A_TRACE" '%detailtype')
assert_eq "A %detailtype counts" \
  "$(cfield "$DET" declared)/$(cfield "$DET" visible)/$(cfield "$DET" resolved)/$(cfield "$DET" missing)/$(cfield "$DET" parsed)" \
  "15/14/0/15/0"
assert_contains "A %detailtype names the install" "$DET" "absent from this install"
SSB=$(controlline "$A_TRACE" '$ssbump')
assert_eq "A \$ssbump counts" \
  "$(cfield "$SSB" declared)/$(cfield "$SSB" visible)/$(cfield "$SSB" resolved)/$(cfield "$SSB" missing)" \
  "36/35/36/0"

# ---------------------------------------------------------------------------
# 4. one semantic at a time
# ---------------------------------------------------------------------------
echo "== 4. single-semantic captures =="
capture A2 || fail=1
capture B --world-dynamic-light on || fail=1
capture C --world-specular on || fail=1
capture D --world-bump on --world-bump-texture "$BUMP_TEXTURE" || fail=1
capture E --world-dynamic-light on --world-bump on --world-bump-texture "$BUMP_TEXTURE" || fail=1
# The refusal that only exists when requested != effective: the switch is on and
# no texture is bound, because the slot's owner declares no $bumpmap.
capture F --world-bump on || fail=1

BLINE=$(lightingline "$SCRATCH/B.trace")
assert_eq "B switches" \
  "$(lfield "$BLINE" dynamic)/$(lfield "$BLINE" specular)/$(lfield "$BLINE" bump)" "1/0/0"
assert_eq "B request" \
  "$(lfield "$BLINE" requestedDynamic)/$(lfield "$BLINE" requestedSpecular)/$(lfield "$BLINE" requestedBump)" "1/0/0"
CLINE=$(lightingline "$SCRATCH/C.trace")
assert_eq "C switches" \
  "$(lfield "$CLINE" dynamic)/$(lfield "$CLINE" specular)/$(lfield "$CLINE" bump)" "0/1/0"
DLINE=$(lightingline "$SCRATCH/D.trace")
assert_eq "D switches" \
  "$(lfield "$DLINE" dynamic)/$(lfield "$DLINE" specular)/$(lfield "$DLINE" bump)" "0/0/1"
assert_eq "D texture source" "$(lfield "$DLINE" bumpTextureSource)" "forced"
assert_eq "D texture" "$(lfield "$DLINE" bumpTexture)" "$BUMP_TEXTURE"
ELINE=$(lightingline "$SCRATCH/E.trace")
assert_eq "E switches" \
  "$(lfield "$ELINE" dynamic)/$(lfield "$ELINE" specular)/$(lfield "$ELINE" bump)" "1/0/1"
FLINE=$(lightingline "$SCRATCH/F.trace")
# Requested but not effective: the constant was NOT uploaded and the trace says
# so. This is the pin m4 breaks.
assert_eq "F requested" "$(lfield "$FLINE" requestedBump)" "1"
assert_eq "F effective" "$(lfield "$FLINE" bump)" "0"
assert_eq "F texture" "$(lfield "$FLINE" bumpTexture)" "-"
FBUMP=$(controlline "$SCRATCH/F.trace" bump)
assert_eq "F control state" "$(cfield "$FBUMP" state)" "refused"
assert_contains "F refusal names the owner" "$FBUMP" "declares no \$bumpmap"

# The preset gate T24 removed. --quality performance must still leave all three
# off (fromPreset zero-initialises), and an explicit switch must still win over
# the preset's name (that is what the removed `preset == Standard` conjunct and
# the removed normalize() duplicate used to prevent). Two captures pin both
# halves, so "the flag is the statement" cannot later regress into "the preset
# name is".
capture P1 --quality performance || fail=1
capture P2 --quality performance --world-specular on || fail=1
PLINE=$(lightingline "$SCRATCH/P1.trace")
assert_eq "performance preset leaves all three off" \
  "$(lfield "$PLINE" dynamic)/$(lfield "$PLINE" specular)/$(lfield "$PLINE" bump)" "0/0/0"
P2LINE=$(lightingline "$SCRATCH/P2.trace")
assert_eq "an explicit switch outranks the preset name" \
  "$(lfield "$P2LINE" dynamic)/$(lfield "$P2LINE" specular)/$(lfield "$P2LINE" bump)" "0/1/0"
assert_eq "P2 requested" "$(lfield "$P2LINE" requestedSpecular)" "1"

# The control rows in the switched frames must say what the switch line says.
cstate() { cfield "$(controlline "$1" "$2")" state; }
assert_eq "B dynamic row" "$(cstate "$SCRATCH/B.trace" dynamicLighting)" "applied"
assert_eq "C specular row" "$(cstate "$SCRATCH/C.trace" specular)" "applied"
assert_eq "D bump row" "$(cstate "$SCRATCH/D.trace" bump)" "applied"
assert_eq "F bump row" "$(cstate "$SCRATCH/F.trace" bump)" "refused"

# ---------------------------------------------------------------------------
# 5. the frame delta, and its direction
# ---------------------------------------------------------------------------
echo "== 5. measured deltas =="
DELTAS="$SCRATCH/deltas.txt"
: > "$DELTAS"
run_diff() { # run_diff <a> <b>
  local line
  line=$("$PYTHON" bmp-pixel-diff.py "$1" "$2" --max-diff-percent 100 2>&1 | head -1)
  echo "$line" | tee -a "$DELTAS"
}
DIFF_A2=$(run_diff "$SCRATCH/A.bmp" "$SCRATCH/A2.bmp")
DIFF_B=$(run_diff "$SCRATCH/A.bmp" "$SCRATCH/B.bmp")
DIFF_C=$(run_diff "$SCRATCH/A.bmp" "$SCRATCH/C.bmp")
DIFF_D=$(run_diff "$SCRATCH/A.bmp" "$SCRATCH/D.bmp")
DIFF_E=$(run_diff "$SCRATCH/B.bmp" "$SCRATCH/E.bmp")

dfield() { printf '%s' "$1" | grep -oE "$2=[-0-9.]+" | head -1 | cut -d= -f2; }
assert_eq "determinism (A vs A2) differing" "$(dfield "$DIFF_A2" differing)" "0"
assert_eq "determinism (A vs A2) max_delta" "$(dfield "$DIFF_A2" max_delta)" "0"
assert_eq "dynamic differing" "$(dfield "$DIFF_B" differing)" "1583615"
assert_eq "dynamic max_delta" "$(dfield "$DIFF_B" max_delta)" "83"
assert_eq "specular differing" "$(dfield "$DIFF_C" differing)" "265448"
assert_eq "specular max_delta" "$(dfield "$DIFF_C" max_delta)" "12"
# Bump alone: the normal is only read by the lighting terms, all off here.
assert_eq "bump alone differing" "$(dfield "$DIFF_D" differing)" "0"
assert_eq "bump alone max_delta" "$(dfield "$DIFF_D" max_delta)" "0"
assert_eq "bump under dynamic differing" "$(dfield "$DIFF_E" differing)" "749629"
assert_eq "bump under dynamic max_delta" "$(dfield "$DIFF_E" max_delta)" "13"

SIGN="$SCRATCH/sign.txt"
: > "$SIGN"
"$PYTHON" bmp-delta-sign.py "$SCRATCH/A.bmp" "$SCRATCH/B.bmp" | tee -a "$SIGN" > /dev/null
"$PYTHON" bmp-delta-sign.py "$SCRATCH/A.bmp" "$SCRATCH/C.bmp" | tee -a "$SIGN" > /dev/null
"$PYTHON" bmp-delta-sign.py "$SCRATCH/B.bmp" "$SCRATCH/E.bmp" | tee -a "$SIGN" > /dev/null
sfield() { sed -n "$1p" "$SIGN" | grep -oE "$2=[-0-9]+" | head -1 | cut -d= -f2; }
# The dynamic-light term is 0.62+0.38*NdotL: it can only subtract from the baked
# lightmap. A single brighter byte would refute the sentence the report makes.
assert_eq "dynamic brighter" "$(sfield 1 brighter)" "0"
assert_eq "dynamic darker" "$(sfield 1 darker)" "1583615"
assert_eq "dynamic max_down" "$(sfield 1 max_down)" "83"
# Specular is additive only.
assert_eq "specular darker" "$(sfield 2 darker)" "0"
assert_eq "specular brighter" "$(sfield 2 brighter)" "265448"
assert_eq "specular max_up" "$(sfield 2 max_up)" "12"
# Bump perturbs the normal both ways: the delta is not a scale.
assert_ge "bump brighter" "$(sfield 3 brighter)" "1"
assert_ge "bump darker" "$(sfield 3 darker)" "1"

# Evidence: the trace and the numbers, not 18 MB of BMPs. The frame hashes let
# a later run prove it captured the same pictures.
# The reference trace is 3.7 MB of per-frame chatter; the evidence is the world
# material/lighting lines it was captured for, so keep those and not the rest.
grep -aE "gpu: world (feature|lighting|material-fetch|atlas|cubemap)" "$A_TRACE" \
  | tr -d '\r' > "$OUT/trace-ref.txt"
if [ ! -s "$OUT/trace-ref.txt" ]; then
  echo "  FAIL the reference trace had no world lines"; fail=1
fi
cp "$PROBE_MAP" "$OUT/probe-map.txt"
cp "$SELF" "$OUT/probe-self-test.txt"
cp "$DELTAS" "$OUT/deltas.txt"
cp "$SIGN" "$OUT/sign.txt"
{
  echo "run=$RUN_TAG"
  echo "map=$MAP demo=$(basename "$DEMO") tick=2000"
  echo "bumpTexture=$BUMP_TEXTURE"
  for name in A A2 B C D E F P1 P2; do
    if [ -s "$SCRATCH/$name.bmp" ]; then
      echo "$name sha256=$("$PYTHON" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$SCRATCH/$name.bmp")"
    fi
  done
} > "$OUT/frames.txt"

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then echo "PASS: six controls measured; three pass switches compared one at a time; four keys refused by name."
  else echo "FAIL: $fail assertion groups did not hold."; fi
  exit "$fail"
fi

# ---------------------------------------------------------------------------
# 6. mutation mode: four source mutations, each must go red
# ---------------------------------------------------------------------------
echo "== mutation mode =="
mfail=0
patch_in() { # patch_in <file> <find> <replace> -- CRLF-aware
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

# m1: $bumpmap2 reports applied. This is the "silently replaced by layer 1"
# defect: the pass would draw layer 1's normal map and call the feature done.
echo "-- m1: \$bumpmap2 reports applied instead of refused --"
SRC_LIGHT="native/src/world_lighting.cpp"
SAVE_LIGHT="$SCRATCH/world_lighting.cpp.save"
cp "$SRC_LIGHT" "$SAVE_LIGHT"
HASH_LIGHT=$(git hash-object "$SRC_LIGHT")
if ! patch_in "$SRC_LIGHT" \
  '    control.state = control.declared > 0 ? WorldLightingState::Refused
                                       : WorldLightingState::NotDeclared;
    control.reason = features.bumpMap2.declared == 0' \
  '    control.state = WorldLightingState::Applied;  // mutation m1
    control.reason = features.bumpMap2.declared == 0'; then
  echo "  MUTATION-SETUP=FAIL (m1: the patch did not apply)"; fail=1
else
  bash build-target.sh lighting_semantics_probe > "$SCRATCH/m1.build" 2>&1
  M1="$SCRATCH/m1.txt"
  "$PROBE" --map "$TF/$MAP" --tf-root "$TF" > "$M1" 2>&1
  M1_STATE=$(kv "$M1" 'lighting.$bumpmap2.state')
  if [ "$M1_STATE" = "refused" ]; then
    echo "  NOT-RED m1: \$bumpmap2 still reads refused"; mfail=1
  else
    echo "  RED m1 as required: \$bumpmap2 state = $M1_STATE (was refused)"
  fi
  "$PROBE" --self-test > "$SCRATCH/m1-self.txt" 2>&1
  if grep -q '"check":"bump2.refused","ok":true' "$SCRATCH/m1-self.txt"; then
    echo "  NOT-RED m1 self-test: bump2.refused still passes"; mfail=1
  else
    echo "  RED m1 self-test as required: bump2.refused no longer passes"
  fi
  cp "$M1" "$OUT/mutated-m1-bumpmap2-applied.txt"
fi
restore_source "$SAVE_LIGHT" "$SRC_LIGHT" m1 "$HASH_LIGHT"

# m2: the %detailtype read path stops reading the key. The fixture is the only
# input that can catch it, because the install has no detail VMT at all.
echo "-- m2: the detail material's %detailtype is no longer read --"
SRC_FEAT="native/src/world_material_features.cpp"
SAVE_FEAT="$SCRATCH/world_material_features.cpp.save"
cp "$SRC_FEAT" "$SAVE_FEAT"
HASH_FEAT=$(git hash-object "$SRC_FEAT")
if ! patch_in "$SRC_FEAT" \
  'if (at != detailMaterial.fields.end() && !at->second.empty()) {' \
  'if (false) {  // mutation m2'; then
  echo "  MUTATION-SETUP=FAIL (m2: the patch did not apply)"; fail=1
else
  bash build-target.sh lighting_semantics_probe > "$SCRATCH/m2.build" 2>&1
  "$PROBE" --self-test > "$SCRATCH/m2-self.txt" 2>&1
  if grep -q '"check":"detail.typeRead","ok":true' "$SCRATCH/m2-self.txt"; then
    echo "  NOT-RED m2: the fixture still reads %detailtype"; mfail=1
  else
    echo "  RED m2 as required: the fixture no longer reads %detailtype"
  fi
  cp "$SCRATCH/m2-self.txt" "$OUT/mutated-m2-detailtype-unread.txt"
fi
restore_source "$SAVE_FEAT" "$SRC_FEAT" m2 "$HASH_FEAT"

# m3: one switch flips two semantics. The A/B stops being one-at-a-time, which
# is the whole experimental design; B must show specular=1.
echo "-- m3: --world-dynamic-light also flips specular --"
SRC_MAIN="native/src/main.cpp"
SAVE_MAIN="$SCRATCH/main.cpp.save"
cp "$SRC_MAIN" "$SAVE_MAIN"
HASH_MAIN=$(git hash-object "$SRC_MAIN")
if ! patch_in "$SRC_MAIN" \
  'if (worldDynamicLightGiven) persistent.render.dynamicLighting = worldDynamicLightRequested;' \
  'if (worldDynamicLightGiven) { persistent.render.dynamicLighting = worldDynamicLightRequested; persistent.render.specular = worldDynamicLightRequested; }  // mutation m3'; then
  echo "  MUTATION-SETUP=FAIL (m3: the patch did not apply)"; fail=1
else
  bash build-target.sh tf2_demo_native > "$SCRATCH/m3.build" 2>&1
  if capture M3 --world-dynamic-light on; then
    M3LINE=$(lightingline "$SCRATCH/M3.trace")
    M3_SPEC=$(lfield "$M3LINE" specular)
    if [ "$M3_SPEC" = "0" ]; then
      echo "  NOT-RED m3: the dynamic-light switch left specular at 0"; mfail=1
    else
      echo "  RED m3 as required: specular = $M3_SPEC in the dynamic-light-only run"
    fi
    lightingline "$SCRATCH/M3.trace" > "$OUT/mutated-m3-coupled-switch.txt"
  else
    echo "  MUTATION-RUN=FAIL (m3: no tick-2000 capture)"; mfail=1
  fi
fi
restore_source "$SAVE_MAIN" "$SRC_MAIN" m3 "$HASH_MAIN"

# m4: the bump switch reports effective with no texture bound. The shader then
# samples an unbound SRV, and the trace would claim a normal map that is not
# there. F (switch on, no texture) is the frame that catches it.
echo "-- m4: bump reports effective without a bound texture --"
SRC_REN="native/include/native_renderer.h"
SAVE_REN="$SCRATCH/native_renderer.h.save"
cp "$SRC_REN" "$SAVE_REN"
HASH_REN=$(git hash-object "$SRC_REN")
if ! patch_in "$SRC_REN" \
  'return settings_.bumpMapping && worldBumpTexture_.view() != nullptr;' \
  'return settings_.bumpMapping;  // mutation m4'; then
  echo "  MUTATION-SETUP=FAIL (m4: the patch did not apply)"; fail=1
else
  bash build-target.sh tf2_demo_native > "$SCRATCH/m4.build" 2>&1
  if capture M4 --world-bump on; then
    M4LINE=$(lightingline "$SCRATCH/M4.trace")
    M4_BUMP=$(lfield "$M4LINE" bump)
    M4_TEX=$(lfield "$M4LINE" bumpTexture)
    if [ "$M4_BUMP" = "0" ]; then
      echo "  NOT-RED m4: the unbound bump switch still reads 0"; mfail=1
    else
      echo "  RED m4 as required: bump = $M4_BUMP with bumpTexture = $M4_TEX (was 0 / -)"
    fi
    lightingline "$SCRATCH/M4.trace" > "$OUT/mutated-m4-unbound-bump.txt"
  else
    echo "  MUTATION-RUN=FAIL (m4: no tick-2000 capture)"; mfail=1
  fi
fi
restore_source "$SAVE_REN" "$SRC_REN" m4 "$HASH_REN"

# Rebuild from the restored tree so the binaries on disk match the sources the
# gate just certified.
bash build-target.sh tf2_demo_native lighting_semantics_probe > "$SCRATCH/restore.build" 2>&1

echo "== verdict =="
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: forward pins held, all four mutations went red, every source came back byte-identical."
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
exit 1
