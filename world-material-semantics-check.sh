#!/usr/bin/env bash
# world-material-semantics-check.sh -- T27: the per-material semantics that reach a pixel.
#
# WHY THIS FILE EXISTS
# --------------------
# T24 (lighting-semantics-check.sh) closed the six-row lighting ledger by
# REFUSING three of the six: `$bumpmap2`, `%detailtype`, `$ssbump`. T27's task is
# the other half -- make the features that can change the final image actually
# change it -- and its acceptance rule is a chain, not a counter:
#
#     VMT declaration -> resource lookup -> shader input -> pixel delta
#
# so every feature below is pinned at all four links, with one semantic switch
# per capture. A counter that moves while the frame does not is the failure this
# gate is built to catch, and section 5 is where it is caught.
#
# THE CAPTURES
#   tick 2000 is the T24 reference camera, so its numbers are comparable with
#   that gate's. tick 1200 is a second camera where the four `$ssbump` materials
#   that declare `$bumpmap2` are on screen: R2 moves 38,538 px there and 0 px
#   from the 2000 camera. The fifth declaration -- the only one whose texture is
#   not an SSBump -- draws its 384 vertices at both cameras and moves nothing
#   (R3 0 px), so it is off-screen from that camera; that is a measurement, not a
#   guess, because its VTF is not flat (probe: meanDeviation 46.2, strongFraction
#   1.0). The 2000 camera was not tested with --world-bump2, so nothing is claimed
#   about it there.
#
#   Both ticks are pinned through capture(), which accepts a frame only when the
#   trace's last draw reports EXACTLY the requested tick. The product makes that
#   the normal case: `--capture-tick N` holds the playback cursor at N until the
#   draw that captures happens (see the comment in main.cpp), because the draw
#   cadence is wall clock and the tick cadence is the demo's, so a loaded machine
#   otherwise lands on N+1 or N+10. The loop still moves its target by the
#   observed error, which is the fallback for an older binary. A 2000-vs-2001
#   comparison compares two camera positions: that mistake was made once while
#   designing this gate (a 39,967-pixel "$bumpmap2 delta" that was pure camera
#   motion, withdrawn), and this pin is what it bought.
#
#   A   baseline, no switches                 determinism control
#   A2  baseline again                        must be 0 px
#   B   --world-material-bump on              normals arrive, no reader -> 0 px
#   C   --world-dynamic-light on              the T24 reference frame
#   D   C + material bump                     layer 1's own normal reaches a pixel
#   E   --world-detail on                     requested, 15/15 VMTs absent -> 0 px
#   F   E + --world-detail-texture <vtf>      the shader-input -> pixel leg
#   R1  dyn + material bump @ tick 1200       the camera where layer 2 is on screen
#   R1b R1 again                              determinism at the second camera
#   R2  R1 + --world-bump2 --bump2-texture    layer 2's normal reaches those pixels
#   R3  R1 + --world-bump2 (no texture)       the declared texture, drawn, 0 px
#   S1  --world-specular on                   the other reader of the same normal
#   S2  S1 + --world-material-bump on         the lobe moves, both directions
#
# WHY A->B IS 0 AND THAT IS THE MEASUREMENT
#   With `--world-material-bump on` the CPU builds the bump atlas, sets the
#   vertex bit and the shader samples t8 -- and the frame does not move, because
#   the only readers of `n` in this pixel shader are the dynamic-light and
#   specular terms, both off in B. C->D is the same normal map with its reader
#   on. That pair is what makes "the atlas exists" and "the picture changed" two
#   separate claims instead of one.
#
# THE DIRECTION QUESTION
#   `bmp-delta-sign.py` splits every differing byte into brighter/darker, and
#   both normal pairs are reported with it. C->D moves both ways, and so does
#   R1->R2 (8,790 brighter / 7,163 darker): a normal perturbation moves a
#   surface toward and away from the light. A one-way read would mean a scale
#   factor, not a lerp between two normals. The gate pins both directions, not
#   just "the frame changed".
#
# WHAT THIS GATE DOES NOT CLAIM
#   That the drawn result matches Source's. `%detailtype` is still refused by
#   name (its consumer is VBSP at map compile time, not a shader -- the key is
#   read, 1 material names `tf_forest_grass`, and no runtime pass consumes it),
#   `$ssbump` is still refused by name (BUMPMAP == 2 needs three
#   lightmaps and a bumpBasis), and the detail layer's pixel evidence uses a
#   diagnostic texture because every `$detail` VMT this map names is absent from
#   this install. Those three sentences are pinned as text, so a future change
#   that quietly drops a refusal goes red here.
#
# mutation: five source mutations, each must turn a forward pin red, and each
# source must come back byte-identical:
#   m1 the tangent frame is ignored (normal = geometric normal) -> C->D 0 px
#   m2 `$bumpmap2` samples layer 1's tile (the silent-replacement defect) -> the
#      R1->R2 delta is no longer 15,953 px while the row still reads applied
#   m3 `$detailblendmode` is parsed, reported and ignored -> F stops matching
#   m4 the feature bit is set from the VMT instead of from the blit -> the ledger
#      still reads materials=9 while D moves: the counter-only implementation
#   m5 the feature tile rect is computed at full atlas resolution -> the probe's
#      rect pin (a tile lands at the wrong scale) and the frame
#
# Usage: bash world-material-semantics-check.sh [--mutation]
# Exit:  0 = every assertion held (or, with --mutation, every mutation went red).
set -uo pipefail
cd "$(dirname "$0")"

PRODUCT=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/world_feature_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MAP=maps/pl_odyssey.bsp
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
DETAIL_TEXTURE=materials/brick/brickwall001.vtf
# The layer-2 diagnostic normal map. See the R2/R3 note in the header: the two
# facts below leave this demo with no visible pixel of the declared layer.
BUMP2_TEXTURE=materials/brick/brickwall001_normal.vtf
PYTHON="${PYTHON:-python}"

# Measured on this machine with the tick pinned by capture(). These are pins, not
# floors: a change in any one of them means a semantic moved, and the report
# quotes the same numbers.
PIN_DYN_DIFF=1584117; PIN_DYN_MAX=83
PIN_BUMP_DIFF=624; PIN_BUMP_MAX=21
PIN_DETAIL_DIFF=82213; PIN_DETAIL_MAX=60
# T27 final: the layer-2 diagnostic arm on the base normal the frame already
# computed. 15,953 px, moving BOTH ways (8,790 brighter / 7,163 darker): a
# real lerp between two normals. The earlier 38,538 px all-darker reading was
# an invalid vertex state (bit 2 without bit 1); it is preserved in the report
# as a superseded reading and is not a pin.
PIN_B2_DIFF=15953; PIN_B2_MAX=26
PIN_B2_UP=8790; PIN_B2_DOWN=7163
# The specular pair: the same material normal, read by the other consumer. 135
# px is small because the lobe is `0.12 * pow(dot(n,h), 32)` and the 9 bumped
# materials are a thin slice of this view -- small is not the same as noise, and
# the direction split is what says so (the highlight moves, it does not gain).
PIN_SPEC_DIFF=135; PIN_SPEC_MAX=3
PIN_SPEC_UP=123; PIN_SPEC_DOWN=12

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MODE="${1:-}"
SCRATCH=".scratch/world-material-semantics.$RUN_TAG"
OUT="evidence/material-semantics"
mkdir -p "$SCRATCH" "$OUT"

fail=0
assert_eq() { # assert_eq <name> <got> <want>
  if [ "$2" = "$3" ]; then echo "  OK $1 = $2"; else echo "  FAIL $1: got '$2' want '$3'"; fail=1; fi
}
assert_ne() { # assert_ne <name> <got> <mustNotBe>
  if [ "$2" = "$3" ]; then echo "  FAIL $1: still '$2'"; fail=1;
  else echo "  OK $1 = $2 (was $3)"; fi
}
assert_contains() { # assert_contains <name> <text> <needle>
  case "$2" in
    *"$3"*) echo "  OK $1 contains '$3'" ;;
    *) echo "  FAIL $1: '$2' does not contain '$3'"; fail=1 ;;
  esac
}

# A capture reads AND writes the settings file (the product saves the settings it
# ran with on exit), so every capture rewrites a scratch ini and none of them
# touch the machine's %LOCALAPPDATA% one.
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

capture() { # capture <name> <tick> [extra flags...]
  local name="$1" want="$2"; shift 2
  local bmp="$SCRATCH/$name.bmp" trace="$SCRATCH/$name.trace"
  local attempt rc tick target="$want"
  # The draw whose tick is first >= the target is the one that writes the file
  # (requestFrameCapture is consumed at the end of that same render()), so the
  # last `draw: begin tick=` in the trace IS the captured tick.
  #
  # A fixed target is not enough to hit a tick. The draw cadence (120 Hz target)
  # is not the tick cadence (`tickRate` from the demo header, ~66.7 Hz, advanced
  # by the same `elapsed` the draw gate reads), so the first draw that satisfies
  # `>= N` is usually at N+1 or N+2 -- and the offset grows when the machine is
  # loaded, at which point retrying N can never hit. So each attempt reports the
  # tick it actually drew, and the next target is moved by exactly that error
  # (a gain-1 controller): a target of N-2 that draws at N is an exact hit. That
  # keeps the captured tick an absolute pin regardless of what else is running.
  for attempt in 1 2 3 4 5 6 7 8 9 10 11 12; do
    rm -f "$bmp"
    write_gate_ini
    TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 300 "$PRODUCT" --tf-root "$TF" \
      --demo "$DEMO" --capture-tick "$target" --capture-frame "$(cygpath -m "$PWD/$bmp")" \
      --hidden-window --audio-disabled --viewmodel models/weapons/v_models/v_scattergun_scout.mdl \
      --viewmodel-render companion --settings-file "$(cygpath -m "$PWD/$GATE_INI")" \
      "$@" > "$SCRATCH/$name.log" 2>&1
    rc=$?
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
      | sed -n 's/.*tick=\([-0-9]*\).*/\1/p')
    if [ "$rc" = 0 ] && [ "$tick" = "$want" ] && [ -s "$bmp" ]; then
      [ "$attempt" -gt 1 ] && echo "  (capture $name pinned attempt $attempt: target $target drew $tick)"
      return 0
    fi
    echo "  (capture $name attempt $attempt: rc=$rc target=$target tick=${tick:-none})"
    case "$tick" in
      ''|*[!0-9]*) ;;
      *)
        target=$((target + want - tick))
        [ "$target" -lt 0 ] && target=0
        ;;
    esac
  done
  return 1
}

kv() { # kv <probe text file> <reading key>
  grep -F "\"reading\":\"$2\"" "$1" 2>/dev/null | head -1 \
    | sed -n 's/.*"value":"\(.*\)"}.*/\1/p'
}
ckdetail() { # ckdetail <self-test text> <check name>
  grep -F "\"check\":\"$2\"" "$1" 2>/dev/null | head -1 \
    | sed -n 's/.*"detail":"\([^"]*\)".*/\1/p'
}
lightingline() { grep -a "gpu: world lighting " "$1" 2>/dev/null | head -1 | tr -d '\r'; }
lfield() { # lfield <line> <key>
  printf '%s' "$1" | grep -oE "(^| )$2=[^ ]+" | head -1 | cut -d= -f2-
}
controlline() { grep -aF "gpu: world lighting-control control=$2 " "$1" 2>/dev/null | head -1 | tr -d '\r'; }
cfield() { lfield "$1" "$2"; }
featureline() { grep -a "gpu: world feature-fetch " "$1" 2>/dev/null | head -1 | tr -d '\r'; }
applyline() { grep -a "gpu: world feature-apply " "$1" 2>/dev/null | head -1 | tr -d '\r'; }
ff() { # ff <trace> <feature-fetch key>
  lfield "$(featureline "$1")" "$2"
}
# The apply line is three sections on one line: `bump k=v ... bump2 k=v ...
# detail k=v ...`. `af <trace> bump.tiles` reads one field out of one section, so
# a number cannot be attributed to the wrong layer.
af() { # af <trace> <layer.key>
  applyline "$1" | sed 's/.*feature-apply //' | tr ' ' '\n' | awk -v want="$2" '
    /^(bump|bump2|detail)$/ {layer=$1; next}
    layer != "" && index($0, "=") > 0 {
      split($0, pair, "=");
      if (layer "." pair[1] == want) { print substr($0, index($0, "=") + 1); exit }
    }'
}

# ---------------------------------------------------------------------------
# 0. build the two targets from this tree
# ---------------------------------------------------------------------------
echo "== 0. build =="
bash build-target.sh tf2_demo_native world_feature_probe > "$SCRATCH/build.txt" 2>&1
if [ $? -ne 0 ]; then echo "  FAIL the build did not finish (see $SCRATCH/build.txt)"; exit 1; fi
echo "  OK both targets built"

# ---------------------------------------------------------------------------
# 1. the probe's fixture: the four links, without a GPU
# ---------------------------------------------------------------------------
echo "== 1. probe self-test (fixture) =="
SELF="$SCRATCH/self.txt"
"$PROBE" --self-test > "$SELF" 2>&1
assert_eq "self-test verdict" "$(kv "$SELF" selfTest)" "true"
assert_eq "self-test checks passed" "$(grep -c '"ok":true' "$SELF")" "49"
assert_eq "self-test checks failed" "$(grep -c '"ok":false' "$SELF")" "0"
# link 1: the declaration. `$bumpmap1` is an alias and must lose to `$bumpmap`.
assert_contains "\$bumpmap1 alias" "$(ckdetail "$SELF" parse.bumpmap1.alias)" "fix/a_height"
assert_contains "\$bumpmap beats \$bumpmap1" "$(ckdetail "$SELF" parse.bumpmap.precedence)" "fix/plain"
assert_contains "\$ssbump parses" "$(ckdetail "$SELF" parse.ssbump)" "1 parses as true"
assert_contains "%detailtype read" "$(ckdetail "$SELF" parse.detailtype)" "2"
# link 2: the lookup. The mode histogram decides which TextureCombine arm draws.
assert_eq "detail mode 8 drawn" "$(ckdetail "$SELF" parse.detail.mode)" "8"
assert_contains "mode 8 names its arm" "$(ckdetail "$SELF" parse.detail.mode.supported)" "TCOMBINE_MULTIPLY"
assert_contains "drawn modes" "$(ckdetail "$SELF" parse.detail.mode.drawn)" "0,1,2,3,8"
assert_contains "refused modes" "$(ckdetail "$SELF" parse.detail.mode.refused)" "4,5,6,7,9,10,11"
assert_contains "malformed mode refused" "$(ckdetail "$SELF" parse.detail.mode.malformed)" "non-numeric"
assert_eq "detail scale" "$(ckdetail "$SELF" parse.detail.scale)" "1.900000"
assert_eq "detail factor" "$(ckdetail "$SELF" parse.detail.factor)" "0.375000"
# link 3: the shader input. The five arms are transcribed, not re-derived.
assert_eq "combine mode 0" "$(ckdetail "$SELF" combine.mode0)" "0.400000,0.375000,0.900000"
assert_eq "combine mode 1" "$(ckdetail "$SELF" combine.mode1)" "0.650000,0.625000,1.100000"
assert_eq "combine mode 2" "$(ckdetail "$SELF" combine.mode2)" "0.425000,0.437500,0.700000"
assert_eq "combine mode 3" "$(ckdetail "$SELF" combine.mode3)" "0.450000,0.375000,0.800000"
assert_eq "combine mode 8" "$(ckdetail "$SELF" combine.mode8)" "0.300000,0.312500,0.600000"
assert_contains "mode 2 uses detail alpha" "$(ckdetail "$SELF" combine.mode2.alpha)" "mode 2 scales by detail.a"
assert_contains "factor 0 is the identity" "$(ckdetail "$SELF" combine.factor.zero)" "factor 0 leaves the base"
assert_contains "tangent frame" "$(ckdetail "$SELF" tangent.u)" "dot(T)=0.707107"
assert_contains "bitangent sign" "$(ckdetail "$SELF" tangent.sign)" "0.707107 vs -0.707107"
assert_contains "tangent rotation" "$(ckdetail "$SELF" tangent.rotation)" "same angle"
assert_contains "degenerate tangent" "$(ckdetail "$SELF" tangent.degenerate)" "falls back"
# the atlas geometry: a tile at half resolution lands in the same normalised rect
assert_eq "feature tile size" "$(ckdetail "$SELF" rect.size)" "256x256"
assert_contains "feature tile centre" "$(ckdetail "$SELF" rect.centre.u)" "0.562500"
assert_contains "tiles do not overlap" "$(ckdetail "$SELF" rect.neighbours)" "257 <= 257"
# the ledger, in both states, so the sentences are pinned and not just produced
assert_contains "off row names the switch" "$(ckdetail "$SELF" ledger.off.bump.reason)" "--world-material-bump"
assert_contains "on row names the tangent basis" "$(ckdetail "$SELF" ledger.on.bump.reason)" "tangent basis"
assert_contains "bump2 row names the lerp" "$(ckdetail "$SELF" ledger.on.bump2.reason)" "lerp"
assert_contains "ssbump row splits the count" "$(ckdetail "$SELF" ledger.on.ssbump.split)" "1 of 2"
assert_contains "detail row keeps the key refused" "$(ckdetail "$SELF" ledger.on.detailtype)" "consumer"
assert_contains "bump2 without layer 1 is named" \
  "$(ckdetail "$SELF" ledger.requested.bump2)" "--world-material-bump is off"

# ---------------------------------------------------------------------------
# 2. the real map: declarations and lookups, before any rendering
# ---------------------------------------------------------------------------
echo "== 2. probe on $MAP =="
PROBE_MAP="$SCRATCH/probe-map.txt"
"$PROBE" --map "$TF/$MAP" --tf-root "$TF" > "$PROBE_MAP" 2>&1
assert_eq "distinct materials" "$(kv "$PROBE_MAP" feature.materials)" "108"
# $bumpmap: 45 declarations over 26 distinct textures; 36 of the 45 are $ssbump.
assert_eq "\$bumpmap declared" "$(kv "$PROBE_MAP" feature.bumpmap.declared)" "45"
assert_eq "\$bumpmap resolved" "$(kv "$PROBE_MAP" feature.bumpmap.resolved)" "45"
assert_eq "\$bumpmap missing" "$(kv "$PROBE_MAP" feature.bumpmap.missing)" "0"
assert_eq "\$bumpmap distinct paths" "$(kv "$PROBE_MAP" feature.bumpmap.distinctPaths)" "26"
assert_eq "non-ssbump \$bumpmap" "$(kv "$PROBE_MAP" feature.bumpmap.nonSsBump)" "9"
assert_contains "\$bumpmap first" "$(kv "$PROBE_MAP" feature.bumpmap.first)" "GREEK/GREEK_GROUND03B"
assert_contains "bump reason names the switch" "$(kv "$PROBE_MAP" feature.bumpmap.reason)" "--world-material-bump"
# $bumpmap2: 5 declarations, all 5 resolve -- this is a feature with real resources.
assert_eq "\$bumpmap2 declared" "$(kv "$PROBE_MAP" feature.bumpmap2.declared)" "5"
assert_eq "\$bumpmap2 resolved" "$(kv "$PROBE_MAP" feature.bumpmap2.resolved)" "5"
assert_eq "\$bumpmap2 missing" "$(kv "$PROBE_MAP" feature.bumpmap2.missing)" "0"
assert_eq "\$bumpmap2 first" "$(kv "$PROBE_MAP" feature.bumpmap2.first)" "GREEK/GREEK_BLEND_COBBLE_FLOOR_01_TO_SAND"
assert_contains "\$bumpmap2 reason names both switches" \
  "$(kv "$PROBE_MAP" feature.bumpmap2.reason)" "--world-material-bump and --world-bump2"
# 4 of the 5 are $ssbump: their second-layer texel is a bumpBasis weight, so the
# declared resource cannot be decoded as a normal even with both switches on.
assert_eq "\$bumpmap2 ssbump split" "$(kv "$PROBE_MAP" feature.bumpmap2.ssBump)" "4"
assert_contains "\$bumpmap2 reason names the split" \
  "$(kv "$PROBE_MAP" feature.bumpmap2.reason)" "4/5 of those materials also declare \$ssbump"
# %detailtype: 15 layers whose detail VMTs are all absent -- but the key lives in
# the SURFACE material and is readable (1 material, `tf_forest_grass`). The
# install's absence is the layer's, not the key's, and the row has to say both.
assert_eq "\$detail declared" "$(kv "$PROBE_MAP" feature.detail.declared)" "15"
assert_eq "\$detail resolved" "$(kv "$PROBE_MAP" feature.detail.resolved)" "0"
assert_eq "\$detail missing" "$(kv "$PROBE_MAP" feature.detail.missing)" "15"
# The key is a key in the SURFACE material (VBSP reads it at compile time), so it
# is readable even though all 15 detail VMTs are absent. "0 read" was the old
# read path looking in the detail VMT; the fixture's 2-vs-9 split pins this.
assert_eq "%detailtype read" "$(kv "$PROBE_MAP" feature.detail.typesRead)" "1"
assert_eq "%detailtype first value" "$(kv "$PROBE_MAP" feature.detail.firstType)" "tf_forest_grass"
assert_contains "\$detail reason names the switch" "$(kv "$PROBE_MAP" feature.detail.reason)" "--world-detail"
# $ssbump: 36 declarations, all resolving to the same VTF their $bumpmap names.
assert_eq "\$ssbump declared" "$(kv "$PROBE_MAP" feature.ssbump.declared)" "36"
assert_eq "\$ssbump resolved" "$(kv "$PROBE_MAP" feature.ssbump.resolved)" "36"
assert_contains "\$ssbump under --world-material-bump" "$(kv "$PROBE_MAP" feature.ssbump.reason)" \
  "refused by name even when --world-material-bump is on"

# ---------------------------------------------------------------------------
# 3. the reference frame: every switch off
# ---------------------------------------------------------------------------
echo "== 3. reference capture (all switches off) =="
capture A 2000 || { echo "  FAIL the reference capture did not produce tick 2000"; fail=1; }
ALINE=$(lightingline "$SCRATCH/A.trace")
assert_eq "A switches" \
  "$(lfield "$ALINE" dynamic)/$(lfield "$ALINE" specular)/$(lfield "$ALINE" bump)" "0/0/0"
assert_eq "A feature switches" \
  "$(ff "$SCRATCH/A.trace" bumpSwitch)/$(ff "$SCRATCH/A.trace" bump2Switch)/$(ff "$SCRATCH/A.trace" detailSwitch)" "0/0/0"
assert_eq "A feature tiles" \
  "$(af "$SCRATCH/A.trace" bump.tiles)/$(af "$SCRATCH/A.trace" bump2.tiles)/$(af "$SCRATCH/A.trace" detail.tiles)" "0/0/0"
assert_eq "A feature vertices" \
  "$(af "$SCRATCH/A.trace" bump.vertices)/$(af "$SCRATCH/A.trace" bump2.vertices)/$(af "$SCRATCH/A.trace" detail.vertices)" "0/0/0"
# Four refusals and two disabled rows: the same shape T24 pinned, now with the
# per-material path named in the bump row instead of "not implemented".
BROW=$(controlline "$SCRATCH/A.trace" bump)
assert_eq "A bump row" "$(cfield "$BROW" state)" "refused"
assert_contains "A bump row names both switches" "$BROW" "--world-material-bump"
B2ROW=$(controlline "$SCRATCH/A.trace" '$bumpmap2')
assert_eq "A \$bumpmap2 row" "$(cfield "$B2ROW" state)" "refused"
assert_eq "A \$bumpmap2 counts" \
  "$(cfield "$B2ROW" declared)/$(cfield "$B2ROW" visible)/$(cfield "$B2ROW" resolved)" "5/5/5"
DROW=$(controlline "$SCRATCH/A.trace" '%detailtype')
assert_eq "A %detailtype row" "$(cfield "$DROW" state)" "refused"
assert_eq "A %detailtype counts" \
  "$(cfield "$DROW" declared)/$(cfield "$DROW" resolved)/$(cfield "$DROW" missing)/$(cfield "$DROW" parsed)" \
  "15/0/15/1"
assert_contains "A %detailtype names the install" "$DROW" "absent from this install"
assert_contains "A %detailtype names the group" "$DROW" "first=tf_forest_grass"
assert_contains "A %detailtype names the compiler" "$DROW" "VBSP"
assert_contains "A %detailtype keeps its refusal" "$DROW" "no consumer here"
SROW=$(controlline "$SCRATCH/A.trace" '$ssbump')
assert_eq "A \$ssbump row" "$(cfield "$SROW" state)" "refused"

# ---------------------------------------------------------------------------
# 4. one semantic at a time
# ---------------------------------------------------------------------------
echo "== 4. single-semantic captures =="
capture A2 2000 || fail=1
capture B 2000 --world-material-bump on || fail=1
capture C 2000 --world-dynamic-light on || fail=1
capture D 2000 --world-dynamic-light on --world-material-bump on || fail=1
capture E 2000 --world-detail on || fail=1
capture F 2000 --world-detail on --world-detail-texture "$DETAIL_TEXTURE" || fail=1
capture R1 1200 --world-dynamic-light on --world-material-bump on || fail=1
capture R1b 1200 --world-dynamic-light on --world-material-bump on || fail=1
capture R2 1200 --world-dynamic-light on --world-material-bump on --world-bump2 on \
  --world-bump2-texture "$BUMP2_TEXTURE" || fail=1
capture R3 1200 --world-dynamic-light on --world-material-bump on --world-bump2 on || fail=1
# S1/S2: the same per-material normal, read by the specular lobe instead of the
# lightmap term. The lightmap term is a wide, mostly-saturating dot product; the
# specular term is a narrow lobe, so if the perturbation reaches it at all the
# highlight moves -- some pixels gain, some lose. That is the second consumer
# T27 asks about, and it is a different claim from "the frame changed".
capture S1 2000 --world-specular on || fail=1
capture S2 2000 --world-specular on --world-material-bump on || fail=1

# ---- B: the declaration and the lookup both happen, the reader does not ----
assert_eq "B feature switches" \
  "$(ff "$SCRATCH/B.trace" bumpSwitch)/$(ff "$SCRATCH/B.trace" bump2Switch)/$(ff "$SCRATCH/B.trace" detailSwitch)" "1/0/0"
assert_eq "B declaration/lookup" \
  "$(ff "$SCRATCH/B.trace" bumpDeclared)/$(ff "$SCRATCH/B.trace" bumpResolved)/$(ff "$SCRATCH/B.trace" bumpMissing)" "45/9/0"
assert_eq "B ssbump split" "$(ff "$SCRATCH/B.trace" bumpSsBump)" "36"
# The shader input: 9 tiles, 9 materials, every vertex of those materials.
assert_eq "B tiles/materials" "$(af "$SCRATCH/B.trace" bump.tiles)/$(af "$SCRATCH/B.trace" bump.materials)" "9/9"
assert_eq "B vertices" "$(af "$SCRATCH/B.trace" bump.vertices)" "19305"
BBROW=$(controlline "$SCRATCH/B.trace" bump)
assert_eq "B bump row" "$(cfield "$BBROW" state)" "applied"
assert_contains "B row names the tangent basis" "$BBROW" "per-face tangent basis"
assert_contains "B row splits ssbump" "$BBROW" "36 set \$ssbump"
# `$bumpmap2` was not requested here, and its row must explain that it cannot be
# drawn without layer 1 rather than claim a resource is missing (5/5 resolve).
B2B=$(controlline "$SCRATCH/B.trace" '$bumpmap2')
assert_eq "B \$bumpmap2 row" "$(cfield "$B2B" state)" "refused"
assert_contains "B \$bumpmap2 reason" "$B2B" "sampled only when --world-material-bump and --world-bump2 are both on"
assert_eq "B \$bumpmap2 resolves" "$(cfield "$B2B" resolved)" "5"
# The $ssbump row now carries both counts, so the refusal is a subtraction.
assert_contains "B \$ssbump split" "$(controlline "$SCRATCH/B.trace" '$ssbump')" "36 of 45"

# ---- C: the reader only; the per-material atlas is off ----
CLINE=$(lightingline "$SCRATCH/C.trace")
assert_eq "C switches" \
  "$(lfield "$CLINE" dynamic)/$(lfield "$CLINE" specular)/$(lfield "$CLINE" bump)" "1/0/0"
assert_eq "C bump row" "$(cfield "$(controlline "$SCRATCH/C.trace" bump)" state)" "refused"
assert_eq "C feature tiles" "$(af "$SCRATCH/C.trace" bump.tiles)" "0"

# ---- D: normal + reader ----
DLINE=$(lightingline "$SCRATCH/D.trace")
# The three fields on this line are the pass constants; the per-material path is
# not one of them, so D's switch triple matches C's. That is the point: what
# changed between C and D is the atlas, not a global.
assert_eq "D switches" \
  "$(lfield "$DLINE" dynamic)/$(lfield "$DLINE" specular)/$(lfield "$DLINE" bump)" "1/0/0"
assert_eq "D bump row" "$(cfield "$(controlline "$SCRATCH/D.trace" bump)" state)" "applied"
assert_eq "D tiles/vertices" \
  "$(af "$SCRATCH/D.trace" bump.tiles)/$(af "$SCRATCH/D.trace" bump.vertices)" "9/19305"

# ---- E: requested, but every resource this map names is absent ----
assert_eq "E detail switch" "$(ff "$SCRATCH/E.trace" detailSwitch)" "1"
assert_eq "E detail declared/resolved/forced" \
  "$(ff "$SCRATCH/E.trace" detailDeclared)/$(ff "$SCRATCH/E.trace" detailResolved)/$(ff "$SCRATCH/E.trace" detailForced)" "15/0/0"
assert_eq "E detail tiles" "$(af "$SCRATCH/E.trace" detail.tiles)" "0"
assert_eq "E detail vertices" "$(af "$SCRATCH/E.trace" detail.vertices)" "0"
EDROW=$(controlline "$SCRATCH/E.trace" '%detailtype')
assert_eq "E %detailtype row" "$(cfield "$EDROW" state)" "refused"
assert_contains "E row names the absence" "$EDROW" "absent from this install"
assert_contains "E row keeps %detailtype's own refusal" "$EDROW" "no consumer here"

# ---- F: the same layer with a diagnostic texture, so the shader leg is measured ----
assert_eq "F detail switch" "$(ff "$SCRATCH/F.trace" detailSwitch)" "1"
assert_eq "F detail declared/resolved/forced" \
  "$(ff "$SCRATCH/F.trace" detailDeclared)/$(ff "$SCRATCH/F.trace" detailResolved)/$(ff "$SCRATCH/F.trace" detailForced)" "15/15/15"
# The mode histogram: this map's `$detail` VMTs are absent, so the fallback mode
# is 0 (TCOMBINE_BASE) for all 15. "0x15" is "mode 0, 15 materials".
assert_eq "F detail mode histogram" "$(ff "$SCRATCH/F.trace" detailModes)" "0x15"
assert_eq "F detail tiles/materials" "$(af "$SCRATCH/F.trace" detail.tiles)/$(af "$SCRATCH/F.trace" detail.materials)" "15/15"
assert_eq "F detail vertices" "$(af "$SCRATCH/F.trace" detail.vertices)" "73770"
FDROW=$(controlline "$SCRATCH/F.trace" '%detailtype')
assert_eq "F %detailtype row" "$(cfield "$FDROW" state)" "refused"
assert_contains "F row says the layer draws" "$FDROW" "the \$detail layer itself draws for 15 materials"
assert_contains "F row keeps the key refused" "$FDROW" "consumer"

# ---- R1/R2: layer 2 on the camera where it is visible ----
assert_eq "R1 bump2 tiles" "$(af "$SCRATCH/R1.trace" bump2.tiles)" "0"
assert_eq "R1 bump2 switch" "$(ff "$SCRATCH/R1.trace" bump2Switch)" "0"
assert_eq "R1 bump tiles" "$(af "$SCRATCH/R1.trace" bump.tiles)" "9"
# R3 is the product path for the declared resource: bit 2 is set only when the
# material's own layer 1 was blitted, so 4 $ssbump materials are refused by
# name and the fifth (128 triangles, 384 vertices) is the only layer-2 draw --
# and it moves no pixel. Its texels are not flat (probe: meanDeviation 46.2), so
# the 0 px is occlusion: the material is not on screen at tick 1200, tick 2000,
# or the probe's own camera pose for it. R1 vs R3 is pinned to 0 below.
assert_eq "R3 bump2 switch" "$(ff "$SCRATCH/R3.trace" bump2Switch)" "1"
assert_eq "R3 bump2 declared/resolved/missing" \
  "$(ff "$SCRATCH/R3.trace" bump2Declared)/$(ff "$SCRATCH/R3.trace" bump2Resolved)/$(ff "$SCRATCH/R3.trace" bump2Missing)" "5/1/0"
assert_eq "R3 bump2 ssbump split" "$(ff "$SCRATCH/R3.trace" bump2SsBump)" "4"
assert_eq "R3 bump2 forced" "$(ff "$SCRATCH/R3.trace" bump2Forced)" "0"
assert_eq "R3 bump2 tiles/materials/vertices" \
  "$(af "$SCRATCH/R3.trace" bump2.tiles)/$(af "$SCRATCH/R3.trace" bump2.materials)/$(af "$SCRATCH/R3.trace" bump2.vertices)" "1/1/384"
R3B2=$(controlline "$SCRATCH/R3.trace" '$bumpmap2')
assert_eq "R3 bump2 row" "$(cfield "$R3B2" state)" "applied"
assert_contains "R3 row names the product refusal" "$R3B2" "no base normal to lerp layer 2 into"
assert_contains "R3 row names the 4 refused bases" "$R3B2" "4 declared materials set \$ssbump"
# R2 is the layer's own arm, with the declared texels replaced by a diagnostic
# and, for the 4 $ssbump materials, the base normal the frame already computed.
# The eligibility rides feature.w, not a mask bit: setting bit 2 without bit 1
# is an invalid state and (measured) the compiler turns it into a 38,538 px
# shading change even with this arm deleted. The trace names the base.
R2B2=$(controlline "$SCRATCH/R2.trace" '$bumpmap2')
assert_eq "R2 bump2 switch" "$(ff "$SCRATCH/R2.trace" bump2Switch)" "1"
assert_eq "R2 bump2 forced" "$(ff "$SCRATCH/R2.trace" bump2Forced)" "5"
assert_eq "R2 bump2 ssbump still counted" "$(ff "$SCRATCH/R2.trace" bump2SsBump)" "4"
assert_eq "R2 bump2 tiles/materials" "$(af "$SCRATCH/R2.trace" bump2.tiles)/$(af "$SCRATCH/R2.trace" bump2.materials)" "5/5"
assert_eq "R2 bump2 vertices" "$(af "$SCRATCH/R2.trace" bump2.vertices)" "41118"
# The weight span is what makes the two normal layers disagree somewhere: a
# constant 1.0 would make the lerp a replacement and the layer boundary invisible
# as a blend. These are the material's own vertex alphas -- the override replaced
# the texels, not the weights.
assert_eq "R2 bump2 weight span" "$(af "$SCRATCH/R2.trace" bump2.weights)" "9(0.000..1.000)"
assert_eq "R2 \$bumpmap2 row" "$(cfield "$R2B2" state)" "applied"
assert_contains "R2 row names the shared weight" "$R2B2" "same vertex weight that selects the albedo layer"
assert_contains "R2 row admits the diagnostic texels" "$R2B2" "sample the --world-bump2-texture diagnostic"
assert_contains "R2 row names the diagnostic base" "$R2B2" "base normal this pass already computed"
assert_contains "R2 row names the 4 refused bases" "$R2B2" "4 of them set \$ssbump"

# ---- S1/S2: the same normal, a second consumer ----
# Neither frame has the lightmap term on (`dynamic=0`), so the only thing that
# can differ between them is the specular lobe reading a different normal.
S1LINE=$(lightingline "$SCRATCH/S1.trace")
S2LINE=$(lightingline "$SCRATCH/S2.trace")
assert_eq "S1 switches" \
  "$(lfield "$S1LINE" dynamic)/$(lfield "$S1LINE" specular)/$(lfield "$S1LINE" bump)" "0/1/0"
assert_eq "S2 switches" \
  "$(lfield "$S2LINE" dynamic)/$(lfield "$S2LINE" specular)/$(lfield "$S2LINE" bump)" "0/1/0"
assert_eq "S1 has no material normal" "$(af "$SCRATCH/S1.trace" bump.tiles)" "0"
assert_eq "S1 bump row" "$(cfield "$(controlline "$SCRATCH/S1.trace" bump)" state)" "refused"
assert_eq "S2 draws the material normal" \
  "$(af "$SCRATCH/S2.trace" bump.tiles)/$(af "$SCRATCH/S2.trace" bump.materials)" "9/9"
assert_eq "S2 bump row" "$(cfield "$(controlline "$SCRATCH/S2.trace" bump)" state)" "applied"

# ---------------------------------------------------------------------------
# 5. the pixel deltas, and their direction
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
DIFF_D=$(run_diff "$SCRATCH/C.bmp" "$SCRATCH/D.bmp")
DIFF_E=$(run_diff "$SCRATCH/A.bmp" "$SCRATCH/E.bmp")
DIFF_F=$(run_diff "$SCRATCH/A.bmp" "$SCRATCH/F.bmp")
DIFF_R1B=$(run_diff "$SCRATCH/R1.bmp" "$SCRATCH/R1b.bmp")
DIFF_R2=$(run_diff "$SCRATCH/R1.bmp" "$SCRATCH/R2.bmp")
DIFF_R3=$(run_diff "$SCRATCH/R1.bmp" "$SCRATCH/R3.bmp")
DIFF_S=$(run_diff "$SCRATCH/S1.bmp" "$SCRATCH/S2.bmp")

dfield() { printf '%s' "$1" | grep -oE "$2=[-0-9.]+" | head -1 | cut -d= -f2; }
assert_eq "determinism (A vs A2) differing" "$(dfield "$DIFF_A2" differing)" "0"
assert_eq "determinism (A vs A2) max_delta" "$(dfield "$DIFF_A2" max_delta)" "0"
# THE COUNTER-ONLY TEST. The atlas is built and bound and the frame is identical.
assert_eq "material bump without a reader differing" "$(dfield "$DIFF_B" differing)" "0"
assert_eq "material bump without a reader max_delta" "$(dfield "$DIFF_B" max_delta)" "0"
assert_eq "dynamic light differing" "$(dfield "$DIFF_C" differing)" "$PIN_DYN_DIFF"
assert_eq "dynamic light max_delta" "$(dfield "$DIFF_C" max_delta)" "$PIN_DYN_MAX"
assert_eq "material bump under dynamic light differing" "$(dfield "$DIFF_D" differing)" "$PIN_BUMP_DIFF"
assert_eq "material bump under dynamic light max_delta" "$(dfield "$DIFF_D" max_delta)" "$PIN_BUMP_MAX"
assert_eq "detail without a resource differing" "$(dfield "$DIFF_E" differing)" "0"
assert_eq "detail with a resource differing" "$(dfield "$DIFF_F" differing)" "$PIN_DETAIL_DIFF"
assert_eq "detail with a resource max_delta" "$(dfield "$DIFF_F" max_delta)" "$PIN_DETAIL_MAX"
assert_eq "determinism at tick 1200 differing" "$(dfield "$DIFF_R1B" differing)" "0"
assert_eq "\$bumpmap2 differing" "$(dfield "$DIFF_R2" differing)" "$PIN_B2_DIFF"
assert_eq "\$bumpmap2 max_delta" "$(dfield "$DIFF_R2" max_delta)" "$PIN_B2_MAX"
assert_eq "declared layer2 on the product path" "$(dfield "$DIFF_R3" differing)" "0"
assert_eq "declared layer2 on the product path max" "$(dfield "$DIFF_R3" max_delta)" "0"
assert_eq "specular reads the material normal differing" "$(dfield "$DIFF_S" differing)" "$PIN_SPEC_DIFF"
assert_eq "specular reads the material normal max_delta" "$(dfield "$DIFF_S" max_delta)" "$PIN_SPEC_MAX"

SIGN="$SCRATCH/sign.txt"
: > "$SIGN"
"$PYTHON" bmp-delta-sign.py "$SCRATCH/A.bmp" "$SCRATCH/B.bmp" | tee -a "$SIGN" > /dev/null
"$PYTHON" bmp-delta-sign.py "$SCRATCH/C.bmp" "$SCRATCH/D.bmp" | tee -a "$SIGN" > /dev/null
"$PYTHON" bmp-delta-sign.py "$SCRATCH/A.bmp" "$SCRATCH/F.bmp" | tee -a "$SIGN" > /dev/null
"$PYTHON" bmp-delta-sign.py "$SCRATCH/R1.bmp" "$SCRATCH/R2.bmp" | tee -a "$SIGN" > /dev/null
"$PYTHON" bmp-delta-sign.py "$SCRATCH/S1.bmp" "$SCRATCH/S2.bmp" | tee -a "$SIGN" > /dev/null
sfield() { sed -n "$1p" "$SIGN" | grep -oE "$2=[-0-9]+" | head -1 | cut -d= -f2; }
# B is the null: the same frame, byte for byte, with the atlas bound.
assert_eq "B brighter" "$(sfield 1 brighter)" "0"
assert_eq "B darker" "$(sfield 1 darker)" "0"
# A normal perturbation moves a surface both toward and away from the light.
# A one-way delta here would be a scale factor, not a perturbed normal.
assert_ne "material bump brighter" "$(sfield 2 brighter)" "0"
assert_ne "material bump darker" "$(sfield 2 darker)" "0"
# The detail layer multiplies the albedo (mode 0, factor 1) with a brick texture
# whose texels are darker than the 1.0 probe, so this delta is one-way by
# construction: base * 2 * detail <= base for detail <= 0.5.
assert_eq "detail brighter" "$(sfield 3 brighter)" "0"
assert_eq "detail darker" "$(sfield 3 darker)" "$PIN_DETAIL_DIFF"
assert_eq "\$bumpmap2 brighter" "$(sfield 4 brighter)" "$PIN_B2_UP"
assert_eq "\$bumpmap2 darker" "$(sfield 4 darker)" "$PIN_B2_DOWN"
# A specular lobe `0.12 * pow(dot(n,h), 32)` reads the same perturbed normal in
# both directions: where the perturbed normal turns toward the half vector the
# texel gains, where it turns away it loses. A one-way read here would mean the
# material normal only ever scaled the highlight.
assert_eq "specular highlight gained" "$(sfield 5 brighter)" "$PIN_SPEC_UP"
assert_eq "specular highlight lost" "$(sfield 5 darker)" "$PIN_SPEC_DOWN"

# ---------------------------------------------------------------------------
# 6. evidence: the lines and numbers, not 25 MB of BMPs
# ---------------------------------------------------------------------------
echo "== 6. evidence =="
grep -aE "gpu: world (feature|lighting|material-fetch|atlas|cubemap)" "$SCRATCH/R2.trace" \
  | tr -d '\r' > "$OUT/trace-bump2-tick1200.txt"
grep -aE "gpu: world (feature|lighting|material-fetch|atlas|cubemap)" "$SCRATCH/F.trace" \
  | tr -d '\r' > "$OUT/trace-detail-tick2000.txt"
grep -aE "gpu: world (feature|lighting|material-fetch|atlas|cubemap)" "$SCRATCH/S2.trace" \
  | tr -d '\r' > "$OUT/trace-specular-tick2000.txt"
if [ ! -s "$OUT/trace-bump2-tick1200.txt" ] || [ ! -s "$OUT/trace-detail-tick2000.txt" ] \
  || [ ! -s "$OUT/trace-specular-tick2000.txt" ]; then
  echo "  FAIL a reference trace had no world lines"; fail=1
fi
cp "$PROBE_MAP" "$OUT/probe-map.txt"
cp "$SELF" "$OUT/probe-self-test.txt"
cp "$DELTAS" "$OUT/deltas.txt"
cp "$SIGN" "$OUT/sign.txt"
{
  echo "run=$RUN_TAG"
  echo "map=$MAP demo=$(basename "$DEMO")"
  echo "tick2000=A,A2,B,C,D,E,F,S1,S2  tick1200=R1,R1b,R2,R3"
  echo "detailTexture=$DETAIL_TEXTURE"
  for name in A A2 B C D E F R1 R1b R2 R3 S1 S2; do
    if [ -s "$SCRATCH/$name.bmp" ]; then
      echo "$name sha256=$("$PYTHON" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$SCRATCH/$name.bmp")"
    fi
  done
} > "$OUT/frames.txt"

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then
    echo "PASS: three material features pinned declaration->lookup->shader input->pixel delta, the normal read by both consumers; the null frame is 0 px; three refusals still named."
  else echo "FAIL: $fail assertion groups did not hold."; fi
  exit "$fail"
fi

# ---------------------------------------------------------------------------
# 7. mutation mode: five mutations, each must turn a forward pin red
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
rebuild() { bash build-target.sh "$@" > "$SCRATCH/mutation.build" 2>&1; }

# m1: the tangent frame is ignored. Every normal map becomes a no-op while the
# ledger still reads applied -- the counter-only implementation this gate exists
# to reject. The D capture is the one that catches it.
echo "-- m1: tangentNormal returns the geometric normal --"
SRC_REN="native/src/native_renderer.cpp"
SAVE_REN="$SCRATCH/native_renderer.cpp.save"
cp "$SRC_REN" "$SAVE_REN"; HASH_REN=$(git hash-object "$SRC_REN")
if ! patch_in "$SRC_REN" \
  '  float3 n = decodeNormal(texel);
  return safeNormalize(T * n.x + B * n.y + Ng * n.z);' \
  '  float3 n = float3(0.0, 0.0, 1.0);  // mutation m1: the decoded texel is dropped
  return safeNormalize(T * n.x + B * n.y + Ng * n.z);'; then
  echo "  MUTATION-SETUP=FAIL (m1: the patch did not apply)"; fail=1
else
  rebuild tf2_demo_native
  if capture M1 2000 --world-dynamic-light on --world-material-bump on; then
    M1_DIFF=$(run_diff "$SCRATCH/C.bmp" "$SCRATCH/M1.bmp")
    M1_STATE=$(cfield "$(controlline "$SCRATCH/M1.trace" bump)" state)
    if [ "$(dfield "$M1_DIFF" differing)" = "$PIN_BUMP_DIFF" ]; then
      echo "  NOT-RED m1: the frame still differs by $PIN_BUMP_DIFF px"; mfail=1
    else
      echo "  RED m1 as required: differing=$(dfield "$M1_DIFF" differing) (was $PIN_BUMP_DIFF) while the row still reads $M1_STATE"
    fi
    controlline "$SCRATCH/M1.trace" bump > "$OUT/mutated-m1-tangent-ignored.txt"
  else
    echo "  MUTATION-RUN=FAIL (m1: no tick-2000 capture)"; mfail=1
  fi
fi
restore_source "$SAVE_REN" "$SRC_REN" m1 "$HASH_REN"

# m2: layer 2 reads layer 1's atlas -- the exact defect the $bumpmap2 row
# promises does not happen. The diagnostic texels are still decoded and still
# counted (`bump2Forced=5`, the row still reads applied), but the sample that
# reaches the lerp is whatever layer 1's tile holds: its own `$bumpmap` normal
# where the material has one, the atlas's cleared texel where it does not. The
# R2 capture is the one that catches it, because that is the only capture where
# layer 2 moves a pixel at all.
echo "-- m2: \$bumpmap2 samples layer 1's tile --"
cp "$SRC_REN" "$SAVE_REN"; HASH_REN=$(git hash-object "$SRC_REN")
if ! patch_in "$SRC_REN" \
  '  float3 n2 = tangentNormal(bumpAtlas2.Sample(sampler0, uv).xyz, Ng, tangent);' \
  '  float3 n2 = tangentNormal(bumpAtlas.Sample(sampler0, uv).xyz, Ng, tangent);  // mutation m2'; then
  echo "  MUTATION-SETUP=FAIL (m2: the patch did not apply)"; fail=1
else
  rebuild tf2_demo_native
  if capture M2 1200 --world-dynamic-light on --world-material-bump on --world-bump2 on \
    --world-bump2-texture "$BUMP2_TEXTURE"; then
    M2_DIFF=$(run_diff "$SCRATCH/R1.bmp" "$SCRATCH/M2.bmp")
    M2_STATE=$(cfield "$(controlline "$SCRATCH/M2.trace" '$bumpmap2')" state)
    if [ "$(dfield "$M2_DIFF" differing)" = "$PIN_B2_DIFF" ]; then
      echo "  NOT-RED m2: the frame still differs by $PIN_B2_DIFF px"; mfail=1
    else
      echo "  RED m2 as required: differing=$(dfield "$M2_DIFF" differing) (was $PIN_B2_DIFF) while the row still reads $M2_STATE"
    fi
    controlline "$SCRATCH/M2.trace" '$bumpmap2' > "$OUT/mutated-m2-bump2-replaced.txt"
  else
    echo "  MUTATION-RUN=FAIL (m2: no tick-1200 capture)"; mfail=1
  fi
fi
restore_source "$SAVE_REN" "$SRC_REN" m2 "$HASH_REN"

# m3: the mode is parsed, reported and ignored. The ledger still prints the
# histogram, the frame is a different picture.
echo "-- m3: combineDetail ignores \$detailblendmode --"
cp "$SRC_REN" "$SAVE_REN"; HASH_REN=$(git hash-object "$SRC_REN")
if ! patch_in "$SRC_REN" \
  '  if (mode == 0u) return baseColor * lerp(float3(1.0, 1.0, 1.0), 2.0 * detailColor.rgb, factor);' \
  '  if (mode == 0u) return lerp(baseColor, detailColor.rgb, factor);  // mutation m3: mode 0 drawn as mode 3'; then
  echo "  MUTATION-SETUP=FAIL (m3: the patch did not apply)"; fail=1
else
  rebuild tf2_demo_native
  if capture M3 2000 --world-detail on --world-detail-texture "$DETAIL_TEXTURE"; then
    M3_DIFF=$(run_diff "$SCRATCH/A.bmp" "$SCRATCH/M3.bmp")
    M3_MODES=$(ff "$SCRATCH/M3.trace" detailModes)
    if [ "$(dfield "$M3_DIFF" differing)" = "$PIN_DETAIL_DIFF" ]; then
      echo "  NOT-RED m3: the frame still differs by $PIN_DETAIL_DIFF px"; mfail=1
    else
      echo "  RED m3 as required: differing=$(dfield "$M3_DIFF" differing) (was $PIN_DETAIL_DIFF) while the histogram still reads $M3_MODES"
    fi
    featureline "$SCRATCH/M3.trace" > "$OUT/mutated-m3-mode-ignored.txt"
  else
    echo "  MUTATION-RUN=FAIL (m3: no tick-2000 capture)"; mfail=1
  fi
fi
restore_source "$SAVE_REN" "$SRC_REN" m3 "$HASH_REN"

# m4: the bit comes from the VMT instead of from the blit. 45 materials then
# claim a normal map, 9 have a tile under their uv, and the other 36 sample a
# neighbour's tile -- while `feature-apply bump materials` still reads 9, because
# the counter is built from the blit set. This is the counter-only defect.
echo "-- m4: the feature bit is set from the VMT, not from the blit --"
cp "$SRC_REN" "$SAVE_REN"; HASH_REN=$(git hash-object "$SRC_REN")
if ! patch_in "$SRC_REN" \
  '        if (bumpMaterials.count(triangle.material)) mask |= 1;' \
  '        if (material.bumpDeclared) mask |= 1;  // mutation m4: declared, not blitted'; then
  echo "  MUTATION-SETUP=FAIL (m4: the patch did not apply)"; fail=1
else
  rebuild tf2_demo_native
  if capture M4 2000 --world-dynamic-light on --world-material-bump on; then
    M4_DIFF=$(run_diff "$SCRATCH/C.bmp" "$SCRATCH/M4.bmp")
    M4_COUNT=$(af "$SCRATCH/M4.trace" bump.materials)
    if [ "$(dfield "$M4_DIFF" differing)" = "$PIN_BUMP_DIFF" ]; then
      echo "  NOT-RED m4: the frame still differs by $PIN_BUMP_DIFF px"; mfail=1
    else
      echo "  RED m4 as required: differing=$(dfield "$M4_DIFF" differing) (was $PIN_BUMP_DIFF) while feature-apply still reports materials=$M4_COUNT"
    fi
    applyline "$SCRATCH/M4.trace" > "$OUT/mutated-m4-declared-not-blitted.txt"
  else
    echo "  MUTATION-RUN=FAIL (m4: no tick-2000 capture)"; mfail=1
  fi
fi
restore_source "$SAVE_REN" "$SRC_REN" m4 "$HASH_REN"

# m5: the tile rect is computed against the full-resolution atlas. The probe's
# rect pins are the cheap half of this check (a tile at the wrong scale is a tile
# in the wrong place); the frame follows.
echo "-- m5: the feature tile rect ignores the half-resolution atlas --"
SRC_ATLAS="native/src/world_tile_atlas.cpp"
SAVE_ATLAS="$SCRATCH/world_tile_atlas.cpp.save"
cp "$SRC_ATLAS" "$SAVE_ATLAS"; HASH_ATLAS=$(git hash-object "$SRC_ATLAS")
if ! patch_in "$SRC_ATLAS" \
  '  long long width = scale(slot.width, atlasWidth, featureWidth);' \
  '  long long width = scale(slot.width, atlasWidth, atlasWidth);  // mutation m5: full-resolution rect'; then
  echo "  MUTATION-SETUP=FAIL (m5: the patch did not apply)"; fail=1
else
  rebuild world_feature_probe
  "$PROBE" --self-test > "$SCRATCH/m5-self.txt" 2>&1
  M5_SIZE=$(ckdetail "$SCRATCH/m5-self.txt" rect.size)
  if [ "$M5_SIZE" = "256x256" ]; then
    echo "  NOT-RED m5: the rect still reads 256x256"; mfail=1
  else
    echo "  RED m5 as required: the rect reads '$M5_SIZE' (was 256x256)"
  fi
  grep -F '"check":"rect.' "$SCRATCH/m5-self.txt" > "$OUT/mutated-m5-rect-scale.txt"
fi
restore_source "$SAVE_ATLAS" "$SRC_ATLAS" m5 "$HASH_ATLAS"

# Rebuild from the restored tree so the binaries on disk match the sources the
# gate just certified.
bash build-target.sh tf2_demo_native world_feature_probe > "$SCRATCH/restore.build" 2>&1

echo "== verdict =="
if [ "$fail" = 0 ] && [ "$mfail" = 0 ]; then
  echo "PASS: forward pins held, all five mutations went red, every source came back byte-identical."
  exit 0
fi
echo "FAIL: forward-fail=$fail mutation-not-red=$mfail"
exit 1
