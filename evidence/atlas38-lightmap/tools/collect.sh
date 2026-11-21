#!/usr/bin/env bash
# T105: regenerate every reading quoted in evidence/atlas38-lightmap/README.md.
#
# Nothing here needs the host lock: it only parses BSPs, reads files and prints.
# The two derived maps come from the generator scripts in this directory, so the
# whole directory can be re-run from a clean checkout with a build tree present.
#
#   bash evidence/atlas38-lightmap/tools/collect.sh [--with-hdr-base <probe.exe>]
#
# --with-hdr-base re-runs the HDR-only before/after pair against a probe built
# from the BASE commit (390a7c76) so hdr-only-before-after.txt has a real
# before-state in it rather than a remembered one.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
cd "$ROOT" || exit 2
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
EXE="native/build-nmake/world_material_probe.exe"
OUT="evidence/atlas38-lightmap"
BASE_PROBE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --with-hdr-base) BASE_PROBE="${2:?--with-hdr-base needs a probe path}"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

[ -x "$EXE" ] || { echo "missing $EXE -- build world_material_probe first" >&2; exit 2; }

# ---- derived maps: the two tails that need bytes the corpus does not carry ----
python "$HERE/make_hdr_only.py"    > "$OUT/hdr-only-map-build.txt" 2>&1 || exit 2
python "$HERE/make_short_light.py" > "$OUT/drops-map-build.txt"    2>&1 || exit 2
HDR_BSP=.scratch/hdr-only-cp_snakewater_final1.bsp
SHORT_BSP=.scratch/short-light-cp_snakewater_final1.bsp
[ -f "$HDR_BSP" ] || { echo "expected $HDR_BSP" >&2; exit 2; }
[ -f "$SHORT_BSP" ] || { echo "expected $SHORT_BSP" >&2; exit 2; }

probe() { "$EXE" --bsp "$1" --tf-root "$TF" --map-name "$2" 2>/dev/null; }
pick() { grep -E '"lightmapMode"|"lightmapAtlas"|"lightmapSamples"|"lightmapFaces"|"atlasReason"|"reading":"lightmap.(facesPacked|facesDropped|facesWithLightmap|trianglesWithLightUv|uvAllInRange|atlas|state)"'; }

# ---- 1. default path on six shipped maps -----------------------------------
{
  for spec in cp_snakewater_final1 pl_upward cp_process_final pl_badwater cp_badlands koth_viaduct; do
    echo "== $spec"
    probe "$TF/maps/$spec.bsp" "$spec" | pick
  done
} > "$OUT/readings-off.txt" 2>&1

# ---- 2. the border A/B ------------------------------------------------------
{
  for spec in cp_snakewater_final1 pl_upward cp_process_final pl_badwater; do
    echo "== $spec  border=0"
    probe "$TF/maps/$spec.bsp" "$spec" \
      | grep -oE '"atlas":"[^"]*"|"reading":"lightmap\.(facesPacked|facesDropped|atlasReason)","value":"[^"]*"'
    echo "== $spec  border=1 (TF2_NATIVE_LM_BORDER=1)"
    TF2_NATIVE_LM_BORDER=1 probe "$TF/maps/$spec.bsp" "$spec" \
      | grep -oE '"atlas":"[^"]*"|"reading":"lightmap\.(facesPacked|facesDropped|atlasReason)","value":"[^"]*"'
  done
} > "$OUT/atlas-border-ab.txt" 2>&1

# ---- 3. the HDR-only copy, this build --------------------------------------
{
  echo "== synthetic HDR-only copy of cp_snakewater_final1 (lump 8 header zeroed, lump 53 intact)"
  probe "$HDR_BSP" hdr-only | pick
} > "$OUT/hdr-only.txt" 2>&1

# ---- 3b. optional: the same copy on a probe built from the base commit ------
if [ -n "$BASE_PROBE" ]; then
  [ -x "$BASE_PROBE" ] || { echo "not executable: $BASE_PROBE" >&2; exit 2; }
  {
    echo "== HDR-only copy: base commit 390a7c76 vs this branch, same bytes"
    echo "-- base 390a7c76 world_material_probe"
    "$BASE_PROBE" --bsp "$HDR_BSP" --tf-root "$TF" --map-name hdr-only 2>/dev/null | pick
    echo "-- this branch world_material_probe"
    probe "$HDR_BSP" hdr-only | pick
    echo
    echo "-- the untouched map, both builds (the readings the copy has to match)"
    echo "base:"; "$BASE_PROBE" --bsp "$TF/maps/cp_snakewater_final1.bsp" --tf-root "$TF" --map-name cp_snakewater_final1 2>/dev/null | pick
    echo "this:"; probe "$TF/maps/cp_snakewater_final1.bsp" cp_snakewater_final1 | pick
  } > "$OUT/hdr-only-before-after.txt" 2>&1
fi

# ---- 4. the drop listing ----------------------------------------------------
{
  echo "== truncated-lighting copy (lump 8 length / 3), TF2_NATIVE_LM_DROPS=1"
  TF2_NATIVE_LM_DROPS=1 "$EXE" --bsp "$SHORT_BSP" --tf-root "$TF" --map-name short-light 2>&1 \
    | grep -A70 "lightmap atlas drops" | head -70
  echo
  echo "== eight shipped maps, TF2_NATIVE_LM_DROPS=1 (silent when nothing is refused)"
  for spec in cp_badlands cp_dustbowl cp_gorge koth_viaduct pl_badwater pl_goldrush ctf_2fort cp_well; do
    printf "%-18s " "$spec"
    TF2_NATIVE_LM_DROPS=1 "$EXE" --bsp "$TF/maps/$spec.bsp" --tf-root "$TF" --map-name "$spec" 2>&1 \
      | grep -m1 "lightmap atlas drops"
  done
} > "$OUT/drops.txt" 2>&1

# ---- 5. the self-test ------------------------------------------------------
"$EXE" --self-test > "$OUT/selftest.txt" 2>&1
echo "selftest rc=$?"

ls -l "$OUT"
