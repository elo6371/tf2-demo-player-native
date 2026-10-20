#!/usr/bin/env bash
# model-inventory-sweep.sh -- T30: the full installed model inventory.
#
# WHY THIS IS A SCRIPT AND NOT PART OF THE GATE
#   The gate has to stay fast enough to run three times per task (forward, a
#   mutation run, a final forward re-run) and re-reading every shipped weapon
#   model inside each of those would cost minutes per run for a number that
#   does not change. The inventory is frozen evidence instead: this script
#   sweeps every model the install ships and writes one TSV row per model,
#   the gate reads the census counts live from vpk_query and asserts the
#   curated subset it draws.
#
# WHAT IT WRITES
#   evidence/t30/inventory.tsv        one row per model: path, probe rc, the
#                                     probe's summary line (all key=value).
#   evidence/t30/inventory-groups.txt the same sweep sorted into the classes
#                                     T30 asked about: single-range, multi-
#                                     range with distinct per-range textures,
#                                     LOD-0 fallback, refusals.
#
# Usage: bash model-inventory-sweep.sh   (run under _shared/hostlock.sh; it
#                                        opens every *_dir.vpk and decodes every
#                                        model, which is real work on a shared
#                                        host.)
# Exit: 0 = the sweep ran; 1 = the probe is missing or a whole class vanished.
set -uo pipefail
cd "$(dirname "$0")"
PROBE=native/build-nmake/model_robustness_probe.exe
CENSUS=native/build-nmake/vpk_query.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
OUT=evidence/t30/inventory.tsv
GROUPS_FILE=evidence/t30/inventory-groups.txt
mkdir -p "$(dirname "$OUT")"
if [ ! -x "$PROBE" ] || [ ! -x "$CENSUS" ]; then
  echo "FATAL: build model_robustness_probe vpk_query first" >&2; exit 1
fi
SCRATCH=.scratch/t30/inventory-sweep
mkdir -p "$SCRATCH"
echo '# T30 full installed-model sweep' > "$OUT"
echo "# probe: $PROBE (sha256 $(sha256sum "$PROBE" | cut -d' ' -f1))" >> "$OUT"
echo "# tf root: $TF" >> "$OUT"
echo "# command: model_robustness_probe --tf-root <tf> --model <each>" >> "$OUT"
echo "# columns: model<TAB>rc<TAB>probe summary line" >> "$OUT"
for dir in models/weapons/c_models models/weapons/v_models; do
  "$CENSUS" "$TF" "$dir" .mdl 2>/dev/null | grep "^    " | sed "s/^    //"
done > "$SCRATCH/models.txt"
echo "models to sweep: $(wc -l < "$SCRATCH/models.txt")"
: > "$SCRATCH/rows.tsv"
COUNT=0; FAILS=0
while read -r model; do
  [ -n "$model" ] || continue
  slug=$(printf "%s" "$model" | tr "/" "_")
  "$PROBE" --tf-root "$TF" --model "$model" > "$SCRATCH/$slug.json" 2> "$SCRATCH/$slug.err"
  rc=$?
  summary=$(grep -m1 "^robust model=" "$SCRATCH/$slug.err" | tr -d "\r")
  printf "%s\t%s\t%s\n" "$model" "$rc" "$summary" >> "$SCRATCH/rows.tsv"
  COUNT=$((COUNT + 1)); [ "$rc" -eq 0 ] || FAILS=$((FAILS + 1))
done < "$SCRATCH/models.txt"
cat "$SCRATCH/rows.tsv" >> "$OUT"
echo "swept $COUNT models, $FAILS with a refusal"
{
  echo "# T30 installed-model groups (from $OUT)"
  echo "# single-range models:"
  awk -F'\t' '$2 == 0 && $3 ~ / meshRanges=1 / {print "  " $1}' "$OUT"
  echo "# multi-range models whose ranges bind distinct textures:"
  awk -F'\t' '$2 == 0 && $3 ~ / meshRanges=[2-9]/ && $3 ~ / distinctRangeTextures=[2-9]/ {print "  " $1}' "$OUT"
  echo "# models whose selected LOD is not 0 (the LOD fallback class):"
  awk -F'\t' '$2 == 0 && $3 ~ / lodFallback=1 / {print "  " $1}' "$OUT"
  echo "# models whose animation source is not a separate companion:"
  awk -F'\t' '$2 == 0 && $3 ~ / separateAnimSource=0 / {print "  " $1}' "$OUT"
  echo "# models that produced a named refusal:"
  awk -F'\t' '$2 != 0 {print "  " $1 }' "$OUT"
} > "$GROUPS_FILE"
SINGLE=$(awk -F'\t' '$2 == 0 && $3 ~ / meshRanges=1 /' "$OUT" | wc -l)
MULTI=$(awk -F'\t' '$2 == 0 && $3 ~ / meshRanges=[2-9]/ && $3 ~ / distinctRangeTextures=[2-9]/' "$OUT" | wc -l)
echo "single-range=$SINGLE multi-range-distinct-textures=$MULTI"
if [ "$SINGLE" -eq 0 ] || [ "$MULTI" -eq 0 ]; then
  echo "INVENTORY-SWEEP=FAIL (a whole class vanished)"; exit 1
fi
echo "INVENTORY-SWEEP=DONE models=$COUNT refusals=$FAILS single=$SINGLE multi=$MULTI"
