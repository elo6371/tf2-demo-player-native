#!/usr/bin/env bash
# g2-probe-matrix.sh -- per-map geometry and material probe matrix (D01 input).
#
# Runs the two static probes that already exist over all seven first-release
# BSPs and records their own readings verbatim. It is a probe matrix, NOT a
# visual acceptance: a green probe says the loader accounted for the map, not
# that the frame looks like the map.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
export LC_ALL=C
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
OUT="${OUT:-/d/_shared/EXEC-2026-11-14/G2/probe-matrix}"
BIN=native/build-nmake
mkdir -p "$OUT" || exit 2

rel_path() { # rel_path <map> -> path relative to the TF root
  case "$1" in
    cp_sunshine|pl_upward_f12) echo "maps/$1.bsp" ;;
    *) echo "download/maps/$1.bsp" ;;
  esac
}

{
  echo "exe_sha256=$(sha256sum $BIN/world_geometry_probe.exe | awk '{print $1}')"
  echo "feature_exe_sha256=$(sha256sum $BIN/world_feature_probe.exe | awk '{print $1}')"
  echo "head=$(git rev-parse HEAD)"
} > "$OUT/identity.txt"

# PATH FORMS DIFFER (measured 2026-11-14): world_geometry_probe wants a path
# RELATIVE to --tf-root; world_feature_probe wants an ABSOLUTE path and cannot
# open a non-ASCII one at all. Both therefore read the copy inside the TF
# install, whose hash equals the archive copy (BATCH-G1 1.1).
for m in cp_process_f12 cp_gullywash_f9 cp_sultry_b8a koth_product_final koth_bagel_rc11 pl_upward_f12 cp_sunshine; do
  rel="$(rel_path "$m")"
  "$BIN/world_geometry_probe.exe" --tf-root "$TF" --map "$rel" > "$OUT/$m.geometry.log" 2>&1
  echo "GEO $m rc=$?"
  "$BIN/world_feature_probe.exe" --tf-root "$TF" --map "$TF/$rel" > "$OUT/$m.feature.log" 2>&1
  echo "FEAT $m rc=$?"
done
echo "PROBE-MATRIX-DONE out=$OUT"
