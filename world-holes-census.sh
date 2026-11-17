#!/usr/bin/env bash
# world-holes-census.sh -- per-map face-level geometry account (read-only).
# Runs world_geometry_probe on each of the nine demo maps and keeps every
# geometry line. No product edit; the probe is a parser that launches nothing.
#
# Usage: bash world-holes-census.sh <probe.exe> [out-dir]
set -uo pipefail
cd "$(dirname "$0")"
PROBE="${1:?usage: world-holes-census.sh <probe.exe> [out-dir]}"
OUT="${2:-evidence/world-holes/census}"
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="E:/demo测试"
mkdir -p "$OUT"
# map -> bsp path (the demo directory's own bsp wins when the tf tree has a
# different revision of the same map name)
maps=(
  "pl_upward_f12|$TF/maps/pl_upward_f12.bsp"
  "cp_process_f12|$TF/maps/cp_process_f12.bsp"
  "cp_sunshine|$TF/maps/cp_sunshine.bsp"
  "cp_sultry_b8a|$DEMO/cp_sultry_b8a/地图/cp_sultry_b8a.bsp"
  "cp_gullywash_f9|$TF/maps/cp_gullywash_f9.bsp"
  "cp_reckoner|$TF/maps/cp_reckoner.bsp"
  "koth_bagel_rc11|$DEMO/koth_bagel_rc11/地图/koth_bagel_rc11.bsp"
  "koth_ashville_final1|$TF/maps/koth_ashville_final1.bsp"
  "koth_product_final|$TF/maps/koth_product_final.bsp"
)
for entry in "${maps[@]}"; do
  name="${entry%%|*}"; bsp="${entry#*|}"
  if [ ! -f "$bsp" ]; then echo "census: $name MISSING $bsp" >&2; echo "census $name status=missing-bsp" >> "$OUT/summary.txt"; continue; fi
  echo "== $name $bsp"
  "$PROBE" --tf-root "$TF" --map "$bsp" > "$OUT/$name.txt" 2> "$OUT/$name.err"
  rc=$?
  # the demand run: an unbounded budget measures what the map asks for
  "$PROBE" --tf-root "$TF" --map "$bsp" --triangle-limit 100000000 > "$OUT/$name.demand.txt" 2>/dev/null
  echo "   rc=$rc lines=$(wc -l < "$OUT/$name.txt")"
done
echo "census done: $(date -u +%FT%TZ)"
