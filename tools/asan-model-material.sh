#!/usr/bin/env bash
# tools/asan-model-material.sh -- sanitizer readings for the model/material paths.
#
# WHY THIS FILE EXISTS
#   The 2026-11-19 sanitizer readings covered install CLI and POV demo decoding
#   only (docs/N23-WALL-LEVEL-2026-11-18.md §8). The model and material paths had
#   no corpus, so they had no reading at all. The corpus is the extracted sample
#   tree this machine already carries in D:/TF2_Demo_Player/work/_tf2-samples-all
#   (66 MDL, 26k VMT, 1495 VTF) plus the VPK set in D:/TF2_Demo_Player/tmp_material_tf_all.
#
# WHAT IT RUNS
#   * model_robustness_probe for every .mdl in the corpus (one process each, so a
#     report names the file that caused it), then the five malformed fixtures on
#     one real model to exercise the refusal path.
#   * material_chain_probe --vmt / --vtf on a spread sample, then its whole-root
#     mode against the loose tree and against the VPK set.
#
# WHAT IT REPORTS
#   evidence/asan-2026-11-19/readings.tsv -- kind, subject, exit, asan reports,
#   first report line. Logs are kept only for runs that produced a report.
#
# USAGE
#   tools/asan-model-material.sh
#   MODEL_ROOT=... VPK_ROOT=... ASAN_BUILD_DIR=... tools/asan-model-material.sh
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO" || exit 1
. ./tools/tf2-toolchain.sh || { echo "NO-TOOLCHAIN" >&2; exit 1; }

B="${ASAN_BUILD_DIR:-/d/_shared/n23-asan-build}"
MODEL_ROOT="${MODEL_ROOT:-/d/TF2_Demo_Player/work/_tf2-samples-all}"
# The stub directory above holds *_dir.vpk only (no numbered volumes), so reads
# there fail by construction and the root reading is about that stub. The real
# install carries the volumes, and the product resolves models AND materials
# through it, so it is the corpus for the VPK sections.
VPK_ROOT="${VPK_ROOT:-/d/SteamLibrary/steamapps/common/Team Fortress 2/tf}"
STUB_ROOT="${STUB_ROOT:-/d/TF2_Demo_Player/tmp_material_tf_all}"
OUT="${OUT_DIR:-evidence/asan-2026-11-19}"
# find(1) under MSYS prints D:/... while the caller may pass /d/...; one spelling has
# to win before relative paths can be derived, so every corpus path goes through here.
win_path() { cygpath -m "$1" 2>/dev/null || printf '%s' "$1"; }
MODEL_ROOT_W="$(win_path "$MODEL_ROOT")"
VPK_ROOT_W="$(win_path "$VPK_ROOT")"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=0:abort_on_error=0:print_summary=1}"

mkdir -p "$OUT/logs" "$OUT/json"
TSV="$OUT/readings.tsv"
printf 'kind\tsubject\texit\tasan_reports\tnote\tfirst_report\n' > "$TSV"

reports=0
failures=0
run_case() {
  local kind="$1" subject="$2"; shift 2
  local log; log="$(mktemp)"
  # </dev/null: a child that reads stdin must not eat the enclosing while-loop's input.
  "$@" >"$log" 2>&1 </dev/null
  local rc=$?
  local hits first
  hits="$(grep -c 'AddressSanitizer' "$log" || true)"
  hits="${hits:-0}"
  # The note is the one line of the probe's own output that says what it decided
  # (selection/LOD, refusal reason, or the material root's counts), so the table
  # carries a reading next to the sanitizer verdict instead of an exit code alone.
  note="$(grep -m1 -E 'selection |robust error=|^\{|materialize ' "$log" | tr -d '\r' | cut -c1-160)"
  # The probe's own JSON is the reading; keep it per case so a later reader does not
  # have to re-run the corpus to see what a row meant.
  local tag; tag="$(printf '%s-%s' "$kind" "$subject" | sed 's#[/\\: ]#-#g')"
  grep -m1 '^{' "$log" | tr -d '\r' > "$OUT/json/$tag.json" 2>/dev/null || rm -f "$OUT/json/$tag.json"
  first="$(grep -m1 'ERROR: AddressSanitizer' "$log" | tr -d '\r' | cut -c1-140)"
  if [ "$hits" -gt 0 ]; then
    reports=$((reports + hits))
    cp "$log" "$OUT/logs/$tag.log"
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$kind" "$subject" "$rc" "$hits" "${note:-}" "${first:-}" >> "$TSV"
  [ "$rc" -eq 0 ] || failures=$((failures + 1))
  rm -f "$log"
}

echo "ASAN-MODEL-MATERIAL corpus=$MODEL_ROOT"
echo "ASAN-MODEL-MATERIAL vpk=$VPK_ROOT build=$B"

# 1. every model in the corpus (mapfile, not "while read < <(find)": a command
# substitution inside run_case inherits the loop's stdin and drains the list)
mapfile -t models < <(find "$MODEL_ROOT/models" -name '*.mdl' | sort)
for model in "${models[@]}"; do
  rel="${model#"$MODEL_ROOT_W"/}"
  run_case model "$rel" "$B/model_robustness_probe.exe" --tf-root "$MODEL_ROOT_W" --model "$rel"
done

# 1b. models resolved through the VPK set of the installed game (asset_root path)
for rel in models/player/scout.mdl models/player/soldier.mdl models/player/demo.mdl \
           models/player/heavy.mdl models/player/engineer.mdl models/player/medic.mdl; do
  run_case model-vpk "$rel" "$B/model_robustness_probe.exe" --tf-root "$VPK_ROOT_W" --model "$rel"
done

# 2. the five malformed fixtures on one real model
for fixture in descriptor-not-triangles descriptor-out-of-bounds bone-out-of-range no-descriptors lod0-absent; do
  run_case fixture "$fixture" "$B/model_robustness_probe.exe" --tf-root "$MODEL_ROOT" \
    --model models/player/demo.mdl --fixture "$fixture"
done

# 3. materials: a spread sample of VMT, parsed with the include root
mapfile -t vmts < <(find "$MODEL_ROOT/materials" -name '*.vmt' | sort | awk 'NR % 2000 == 1')
for vmt in "${vmts[@]}"; do
  rel="${vmt#"$MODEL_ROOT_W"/}"
  run_case vmt "$rel" "$B/material_chain_probe.exe" --vmt "$vmt" --vmt-include-root "$MODEL_ROOT/materials"
done

# 4. textures: a spread sample of VTF
mapfile -t vtfs < <(find "$MODEL_ROOT" -name '*.vtf' | sort | awk 'NR % 150 == 1')
for vtf in "${vtfs[@]}"; do
  rel="${vtf#"$MODEL_ROOT_W"/}"
  run_case vtf "$rel" "$B/material_chain_probe.exe" --vtf "$vtf"
done

# 5. whole-root modes: loose tree, then the VPK set
run_case root-loose "$MODEL_ROOT" timeout 900 "$B/material_chain_probe.exe" "$MODEL_ROOT_W"
run_case root-vpk "$VPK_ROOT" timeout 900 "$B/material_chain_probe.exe" "$VPK_ROOT_W"
run_case root-vpk-stub "$STUB_ROOT" timeout 900 "$B/material_chain_probe.exe" "$(win_path "$STUB_ROOT")"

tail -1 "$TSV" >/dev/null
rows=$(awk 'END { print NR - 1 }' "$TSV")
hitrows=$(awk -F'\t' 'NR>1 && $4>0' "$TSV" | wc -l)
echo "ASAN-MODEL-MATERIAL rows=$rows reports=$reports hit-rows=$hitrows nonzero-exit=$failures"
grep -v '^kind' "$TSV" | awk -F'\t' '$4>0' | cut -c1-160
