#!/usr/bin/env bash
# class-pose-family-check.sh -- T62: does the crouch / flinch sequence DATA decode?
#
# Why this gate exists. The per-class pixel chain measures the two poses the
# product can actually reach: the idle pick and the moving pick
# (native/src/main.cpp choosePlayerAnimationSequence -> idle|moving only). It
# cannot measure a crouch or a flinch, because no code path selects one. The
# honest reading for those two families is therefore split in two:
#
#   (a) the runtime never plays them -- a product gap, named in
#       evidence/t62-matrix/CHARACTER-MATRIX.md section 5 item 9, not a data gap;
#   (b) the MODEL DATA still has to be sound, and until today nobody had checked.
#       It was not: every Pyro sequence in the reload family carried an empty
#       0xFF-terminated bone chain and failed to decode (see RUNS-2026-11-04.md
#       section 11). A crouch/flinch family can fail the same way and the pixel
#       chain would never notice, because the product never asks for it.
#
# What this gate asserts, per class, over the class model own sequence table:
#   F1 the table names at least one crouch sequence (excluding reload and arm
#      layers, which are separate things that happen to carry the word);
#   F2 every sampled crouch/flinch sequence decodes (every animpose frame says
#      finite=1, no "status=sample-failed") and the label the sampler asked for
#      is the label the decoder reports;
#   F3 at least one sampled crouch sequence actually moves: two animpose frames
#      with different bone checksums, so the family is not silently a static
#      reference pose;
#   F4 the flinch family is reported per class and required to exist somewhere in
#      the nine tables (it is not a per-class guarantee: five of the nine carry
#      none, so per-class presence would be a false invariant).
#
# It reads model files only. No product, no capture, no host lock.
set -u
OUT="${T62_POSEFAMILY_OUT:-evidence/t62-matrix/pose-families}"
PROBE=native/build-nmake/model_pose_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
CLASSES=(scout soldier pyro demo heavy engineer medic sniper spy)
MAX_SAMPLES=3

failures=0
samples=0
sampled_ok=0
flinch_total=0
crouch_classes=0
mkdir -p "$OUT"
: > "$OUT/pose-families.txt"

say() { echo "$@" | tee -a "$OUT/pose-families.txt" ; }
failure() { echo "  FAIL $*" | tee -a "$OUT/pose-families.txt" ; failures=$((failures + 1)) ; }

[[ -x $PROBE ]] || { echo "FATAL: no probe at $PROBE (build it: bash build-target.sh model_pose_probe)"; exit 2; }

# candidates <dump> <family> -- prints "idx label frames", best first.
# Preference: a real AttackCrouch clip, then any clip with more than one frame,
# then the rest. The aimmatrix *crouch_idle* clips are one-frame reference
# poses; sampling only those would make F3 vacuous, which is exactly what the
# first run of this gate did.
candidates() {
  local dump=$1 family=$2
  awk -v family="$family" '
    family == "crouch" { if ($0 !~ /label=.*[Cc]rouch/) next }
    family == "flinch" { if ($0 !~ /label=.*flinch/) next }
    {
      idx = ""; lab = ""; frames = 0
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^index=/) { split($i, a, "="); idx = a[2] }
        if ($i ~ /^label=/) { split($i, a, "="); lab = a[2] }
        if ($i ~ /^frames=/) { split($i, a, "="); frames = a[2] + 0 }
      }
      if (lab ~ /^layer_/) next
      if (lab ~ /[Rr]eload/) next
      if (family == "crouch") {
        if (lab ~ /^AttackCrouch/) rank = 0
        else if (frames >= 2) rank = 1
        else rank = 2
      } else { rank = 0 }
      printf "%d\t%s\t%s\t%d\n", rank, idx, lab, frames
    }' "$dump" | sort -t "$(printf '\t')" -k1,1n -k2,2n | cut -f2- | tr "\t" " "
}

for class in "${CLASSES[@]}"; do
  model="models/player/${class}.mdl"
  dump="$OUT/${class}-sequences.txt"
  raw="$OUT/${class}-sequences.raw.txt"
  if ! "$PROBE" --player-animation-test --tf-root "$TF" --model "$model" --dump-sequences > "$raw" 2>&1; then
    failure "$class: --dump-sequences failed"
    continue
  fi
  total=$(grep -o "sequences=[0-9]*" "$raw" | head -1 | cut -d= -f2)
  grep -E "^seq index=" "$raw" > "$dump"
  rm -f "$raw"
  mapfile -t crouch < <(candidates "$dump" crouch)
  mapfile -t flinch < <(candidates "$dump" flinch)
  flinch_total=$((flinch_total + ${#flinch[@]}))
  if [[ ${#crouch[@]} -lt 1 ]]; then
    failure "$class: the table names no crouch sequence outside the reload and layer families"
  else
    crouch_classes=$((crouch_classes + 1))
  fi
  say "CLASS ${class} sequences=${total:-?} crouchCandidates=${#crouch[@]} flinchCandidates=${#flinch[@]}"
  moving_ok=0
  for family in crouch flinch; do
    if [[ $family == crouch ]]; then picks=("${crouch[@]}"); else picks=("${flinch[@]}"); fi
    [[ ${#picks[@]} -eq 0 ]] && continue
    n=0
    for entry in "${picks[@]}"; do
      [[ $n -ge $MAX_SAMPLES ]] && break
      n=$((n + 1))
      idx=${entry%% *}
      rest=${entry#* }
      want=${rest%% *}
      frame_log="$OUT/${class}-${family}-seq${idx}.txt"
      samples=$((samples + 1))
      if ! "$PROBE" --player-animation-test --tf-root "$TF" --model "$model" --sequence "$idx" --ticks 4 > "$frame_log" 2>&1; then
        failure "$class $family seq $idx: the probe exited non-zero"
        continue
      fi
      if grep -q "status=sample-failed" "$frame_log"; then
        reason=$(grep -o "reason=[^ ]*" "$frame_log" | head -1)
        failure "$class $family seq $idx (${want}): sample failed ${reason}"
        continue
      fi
      # The label has to come from an animpose line. The first lowercase
      # "label=" in the transcript belongs to a "contcand ... label=" line and
      # naming the class default pose was the first run bug here.
      got=$(grep -m1 "^animpose " "$frame_log" | grep -o "label=[^ ]*" | head -1)
      if [[ $got != "label=${want}" ]]; then
        failure "$class $family seq $idx: asked for ${want}, decoder reported ${got:-nothing}"
        continue
      fi
      stats=$(awk '
        /^animpose / {
          n++
          for (i = 1; i <= NF; ++i) {
            if ($i ~ /^finite=/) { split($i, a, "="); if (a[2] != "1") bad = 1 }
            if ($i ~ /^maxDelta=/) { split($i, a, "="); if (a[2] + 0 > max) max = a[2] + 0 }
            if ($i ~ /^checksum=/) { split($i, a, "="); cs[a[2]] = 1 }
          }
        }
        END { d = 0; for (k in cs) d++; printf "%d %.6f %d\n", (bad ? 0 : (n > 0 ? 1 : 0)), max, d }' "$frame_log")
      finite=$(echo "$stats" | cut -d" " -f1)
      maxdelta=$(echo "$stats" | cut -d" " -f2)
      distinct=$(echo "$stats" | cut -d" " -f3)
      if [[ ${finite:-0} -ne 1 ]]; then
        failure "$class $family seq $idx (${want}): a sampled frame was not finite"
        continue
      fi
      sampled_ok=$((sampled_ok + 1))
      [[ $family == crouch && ${distinct:-0} -ge 2 ]] && moving_ok=1
      say "  SAMPLE ${class} ${family} seq=${idx} label=${want} distinctPoses=${distinct} maxDelta=${maxdelta}"
    done
  done
  if [[ ${#crouch[@]} -ge 1 && $moving_ok -eq 0 ]]; then
    failure "$class: every sampled crouch sequence held one pose, so the family is a static reference pose"
  fi
done

if [[ $flinch_total -lt 1 ]]; then
  failure "no class table carries a flinch sequence, so F4 has nothing to measure"
fi
say ""
say "POSE-FAMILIES classes=${#CLASSES[@]} crouchClasses=${crouch_classes} flinchSequences=${flinch_total} sampled=${samples} decoded=${sampled_ok} failed=${failures}"
if [[ $failures -ne 0 ]]; then
  say "POSE-FAMILIES=FAIL (see the FAIL lines above)"
  exit 1
fi
say "POSE-FAMILIES=PASS every sampled crouch/flinch sequence decodes and at least one crouch clip moves"
