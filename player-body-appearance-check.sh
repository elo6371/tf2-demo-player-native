#!/usr/bin/env bash
# player-body-appearance-check.sh -- T62: a third-person body is painted in its
# team's skin family, measured on that body's own pixels.
#
# WHY THIS EXISTS
# The ledger proves what the product REQUESTED (team=N family=F); it cannot
# prove what the materials PUT ON SCREEN. This script closes that gap for the
# two team families: the identity frame (--world-entity-id) marks one body's
# pixels with a unique red byte, and the SAME pixels of a normally rendered
# frame at the same tick and camera give the mean colour that body actually
# carries. family 0 is red, family 1 is blu (see the comment at
# material_slot_resolution.h:66 and main.cpp:9033-9055), so a red body must
# read red-dominant and a blu body blue-dominant, in the same class, at the
# same tick.
#
# THE CAMERA
# Derived, not tuned: eye = body origin + (-250, 0, +58), pitch 4.1, yaw 0 --
# the same lens geometry class-pixels-check.sh uses (and the reference camera
# player-animation-pixels.sh was hand-built from). targets.txt is an INPUT:
# the script fails if the id frame refuses to draw the named body.
# Read it and the settings from INPUT; OUT may be a new, empty output directory.
#
# WHY tick 63000 OF THE SNAKEWATER RECORDING
# It is the only tick this recording offers where player bodies of BOTH teams
# are drawn: the five team-2 bodies (ent 2,3,4,7,13) and the six team-3 bodies
# (ent 5,6,8,9,10,15) are all in the ledger. Two classes give a body of each
# team in one frame each: Demoman (ent 4 red / ent 5 blu) and Scout (ent 3 red
# / ent 15 blu). The pair is what makes the claim about FAMILY rather than
# about the class: same model, opposite teams, opposite paint.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it launches captures).
# Usage: bash player-body-appearance-check.sh
# Exit:  0 = PASS, 1 = FAIL, 2 = a required input is missing (FATAL).
set -uo pipefail
cd "$(dirname "$0")"

INPUT=${T62_BODY_INPUT:-evidence/t62-matrix/body-appearance}
OUT=${T62_BODY_OUT:-$INPUT}
PRODUCT=${T62_BODY_PRODUCT:-native/build-nmake/tf2_demo_native.exe}
PY=${T62_BODY_PYTHON:-C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe}
TF=${T62_BODY_TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${T62_BODY_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}
SPEED=${T62_BODY_SPEED:-32}
CAMERA_OFFSET_X=-250
CAMERA_OFFSET_Z=58
CAMERA_PITCH=4.1
CAMERA_YAW=0.0
MIN_BODY_PIXELS=200
# Measured on this frame: red demo 48.1, blu demo 21.6, red scout 89.3, blu
# scout 43.8 (mean_r - mean_b). 15 is the floor one body clears with margin;
# the PAIR_MIN_GAP assertion below is the stronger claim.
MIN_TINT=15
PAIR_MIN_GAP=40

fail=0
say_fail() { echo "  FAIL $*"; fail=1; }

mkdir -p "$OUT"
for f in settings.json ui.json; do
  [ -e "$INPUT/$f" ] || cp "evidence/t62-matrix/class-pixels/$f" "$INPUT/$f"
done
for path in "$PRODUCT" "$TF" "$DEMO" "$PY" "$INPUT/targets.txt" "$INPUT/settings.json" "$INPUT/ui.json"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done

# run_frame <dir> <tag> <eye...> <flag...>
run_frame() {
  local dir="$1" tag="$2" eyeX="$3" eyeY="$4" eyeZ="$5"; shift 5
  local bmp="$dir/$tag.bmp" trace="$dir/$tag.trace"
  rm -f "$bmp" "$trace"
  TF2_NATIVE_TRACE="$trace" timeout 1200 "$PRODUCT" \
    --tf-root "$TF" --hidden-window --audio-disabled \
    --settings-file "$INPUT/settings.json" --ui-settings-file "$INPUT/ui.json" \
    --demo "$DEMO" --capture-tick "$TICK" --playback-speed "$SPEED" \
    --capture-frame "$bmp" \
    --debug-camera "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" "$CAMERA_YAW" \
    "$@" > "$dir/$tag.log" 2>&1
  local rc=$?
  local view frameTick sceneTick
  view=$(grep -a -o "gpu: world debug view=[a-z-]*" "$trace" 2>/dev/null | tail -1 | sed "s/.*view=//")
  frameTick=$(grep -a -o "capture: armed requested=[0-9]* frame_tick=[0-9]*" "$trace" 2>/dev/null | tail -1 | sed "s/.*frame_tick=//")
  sceneTick=$(grep -a "scene: anim " "$trace" 2>/dev/null | tail -1 | grep -o "tick=[0-9]*" | tail -1 | sed "s/tick=//")
  printf "%s %s %s %s\n" "${rc:-1}" "${view:-<none>}" "${frameTick:-<none>}" "${sceneTick:-<none>}"
}

# ledger_body <trace> <ent> <model> -- the entity: id= line for THIS body.
ledger_body() {
  # The trace prefixes every line with its elapsed seconds ("53293.2 entity: id=.."),
  # so this match is NOT anchored -- an anchored pattern silently finds nothing.
  grep -a "entity: id=" "$1" 2>/dev/null | grep -a " ent=$2 " | grep -a "model=$3 " | tail -1
}

field_of() { printf "%s" "$1" | grep -o " $2=[^ ]*" | head -1 | sed "s/ $2=//"; }

# ids_pixels <summary-of-ids> <id>

echo "=== T62 body appearance: one red and one blu body, same class, same tick ==="
echo "  demo=$DEMO"
echo "  speed=$SPEED camera-offset=(${CAMERA_OFFSET_X},0,${CAMERA_OFFSET_Z}) pitch=$CAMERA_PITCH yaw=$CAMERA_YAW"
echo "  min-body-pixels=$MIN_BODY_PIXELS min-tint=$MIN_TINT"
echo

judged=0
declare -A TINT_ID TINT_MODEL TINT_TEAM TINT_FAMILY TINT_PIX TINT_RMB

while read -r LABEL CLASS ENT TICK ORIGIN_X ORIGIN_Y ORIGIN_Z; do
  case "$LABEL" in ""|"#"*) continue;; esac
  model="models/player/${CLASS}.mdl"
  dir="$OUT/${LABEL}"
  mkdir -p "$dir"
  eyeX=$(awk -v x="$ORIGIN_X" -v d="$CAMERA_OFFSET_X" "BEGIN { printf \"%.1f\", x + d }")
  eyeY="$ORIGIN_Y"
  eyeZ=$(awk -v z="$ORIGIN_Z" -v d="$CAMERA_OFFSET_Z" "BEGIN { printf \"%.1f\", z + d }")
  echo "--- ${LABEL}: ${CLASS} ent=$ENT tick=$TICK origin=${ORIGIN_X},${ORIGIN_Y},${ORIGIN_Z} eye=${eyeX},${eyeY},${eyeZ}"

  read -r id_rc id_view id_ft id_st < <(run_frame "$dir" identity "$eyeX" "$eyeY" "$eyeZ" --player-animation --world-entity-id)
  read -r on_rc on_view on_ft on_st < <(run_frame "$dir" painted "$eyeX" "$eyeY" "$eyeZ" --player-animation)
  echo "  captures identity=$id_rc/${id_view}/${id_ft}/${id_st} painted=$on_rc/${on_view:-<normal>}/${on_ft}/${on_st}"

  [ "$id_rc" = "0" ] || say_fail "${LABEL}: identity capture exited $id_rc"
  [ "$on_rc" = "0" ] || say_fail "${LABEL}: painted capture exited $on_rc"
  [ "$id_view" = "entity-id" ] || say_fail "${LABEL}: identity frame reports view=${id_view:-<none>}, not entity-id"
  [ "$id_ft" = "$TICK" ] || say_fail "${LABEL}: identity frame landed tick ${id_ft:-<none>}, not $TICK"
  [ "$on_ft" = "$TICK" ] || say_fail "${LABEL}: painted frame landed tick ${on_ft:-<none>}, not $TICK"
  [ -n "$on_st" ] && [ "$on_st" = "$id_st" ] || say_fail "${LABEL}: the two passes resolved different scene ticks (identity=${id_st:-<none>} painted=${on_st:-<none>})"

  line=$(ledger_body "$dir/identity.trace" "$ENT" "$model")
  if [ -z "$line" ]; then
    say_fail "${LABEL}: the id frame has no ledger line for ent=$ENT $model"
    echo
    continue
  fi
  echo "  LEDGER $line"
  id=$(field_of "$line" id)
  team=$(field_of "$line" team)
  family=$(field_of "$line" family)
  want_family=0; [ "$team" = "3" ] && want_family=1
  case "$team" in
    2|3) echo "  OK   team=$team (want_family=$want_family got_family=$family)" ;;
    *) say_fail "${LABEL}: ledger reports team=${team:-<none>}, not 2 or 3" ;;
  esac
  [ "$family" = "$want_family" ] || say_fail "${LABEL}: team $team requested family $want_family, ledger says family=${family:-<none>}"

  ids=$("$PY" player-body-appearance-pixels.py --ids "$dir/identity.bmp" 2>&1)
  px=$(printf "%s\n" "$ids" | grep -a "^ID id=$id " | grep -o "pixels=[0-9]*" | sed "s/pixels=//")
  px=${px:-0}
  if [ "$px" -ge "$MIN_BODY_PIXELS" ]; then
    echo "  OK   the body's own mask is $px pixels (>= $MIN_BODY_PIXELS)"
  else
    say_fail "${LABEL}: body mask ${px} < $MIN_BODY_PIXELS pixels; too small to judge the paint"
    echo
    continue
  fi

  # The tool prints two lines (BODY ... then SIZE ...); read them whole, since a
  # plain `read` would split the first line on its spaces.
  pix=$("$PY" player-body-appearance-pixels.py "$dir/identity.bmp" "$id" "$dir/painted.bmp" 2>&1)
  mean_line=$(printf '%s\n' "$pix" | sed -n '1p')
  size_line=$(printf '%s\n' "$pix" | sed -n '2p')
  echo "  $mean_line"
  echo "  $size_line"
  rmb=$(field_of "$mean_line" r_minus_b)
  bmr=$(field_of "$mean_line" b_minus_r)
  tint=0
  case "$team" in
    2)
      tint=$(awk -v v="${rmb:-0}" "BEGIN { printf \"%.1f\", v }")
      if awk -v v="${rmb:-0}" -v m="$MIN_TINT" "BEGIN { exit !(v >= m) }"; then
        echo "  OK   red body reads red-dominant: mean_r - mean_b = ${rmb} (>= $MIN_TINT)"
      else
        say_fail "${LABEL}: team 2 body reads mean_r - mean_b = ${rmb:-<none>}; expected >= $MIN_TINT"
      fi
      ;;
    3)
      tint=$(awk -v v="${bmr:-0}" "BEGIN { printf \"%.1f\", v }")
      if awk -v v="${bmr:-0}" -v m="$MIN_TINT" "BEGIN { exit !(v >= m) }"; then
        echo "  OK   blu body reads blue-dominant: mean_b - mean_r = ${bmr} (>= $MIN_TINT)"
      else
        say_fail "${LABEL}: team 3 body reads mean_b - mean_r = ${bmr:-<none>}; expected >= $MIN_TINT"
      fi
      ;;
  esac

  TINT_ID[$CLASS]="${TINT_ID[$CLASS]:-}${id},"
  TINT_MODEL[$CLASS]="$model"
  TINT_TEAM[$CLASS]="${TINT_TEAM[$CLASS]:-}${team},"
  TINT_PIX[$CLASS]="${TINT_PIX[$CLASS]:-}${px},"
  TINT_RMB[$CLASS]="${TINT_RMB[$CLASS]:-}${team}:${rmb:-${bmr}},"
  judged=$((judged + 1))
  rm -f "$dir/identity.trace" "$dir/painted.trace"
  echo
done < "$INPUT/targets.txt"

# --- the pair claim: the same model must be painted by BOTH teams, and the two
# readings must point in OPPOSITE directions. One body per team could pass by
# accident (a red-ish map lighting a red body); a same-model pair cannot.
#
# WHICH PAIRS THIS BATCH OWES. A label is <team>-<class>, so targets.txt records
# whether a pair was requested at all. Without that, "this batch only asked for
# one team" and "this batch asked for both and one body was not drawn" would
# print the same sentence, and only the second is a failure: the nine-class
# sweep is run as eight small batches so one lock holder does not starve the
# next, and the singles batch (BLU Scout, BLU Sniper) makes no pair claim by
# construction.
declare -A WANT_TEAMS
while read -r LABEL CLASS ENT TICK OX OY OZ; do
  case "$LABEL" in ''|'#'*) continue;; esac
  case "$LABEL" in
    red-*) WANT_TEAMS[$CLASS]="${WANT_TEAMS[$CLASS]:-}r" ;;
    blu-*) WANT_TEAMS[$CLASS]="${WANT_TEAMS[$CLASS]:-}b" ;;
  esac
done < "$INPUT/targets.txt"

echo "=== pair check: same model, both teams ==="
pair_found=0
want_pairs=0
# NOTE: do NOT write for CLASS in ${!ARR[@]+"${!ARR[@]}"} here. On an
# associative array that form is re-read as an indirect expansion and bash
# aborts the loop body with "invalid variable name" (measured 2026-11-05,
# bash 5.3.15), which silently leaves the judged-team bookkeeping empty.
# A plain "${!ARR[@]}" is safe: bash >= 4.4 does not trip set -u on an empty
# associative array, verified on the same shell.
for CLASS in "${!WANT_TEAMS[@]}"; do
  case "${WANT_TEAMS[$CLASS]}" in
    *r*b*|*b*r*)
      want_pairs=$((want_pairs + 1))
      teams="${TINT_TEAM[$CLASS]:-}"
      case "$teams" in
        *2*3*|*3*2*)
          pair_found=$((pair_found + 1))
          red_rmb=""; blu_rmb=""
          for part in ${TINT_RMB[$CLASS]//,/ }; do
            case "$part" in
              2:*) red_rmb=${part#2:} ;;
              3:*) blu_rmb=${part#3:} ;;
            esac
          done
          gap=$(awk -v r="${red_rmb:-0}" -v b="${blu_rmb:-0}" 'BEGIN { printf "%.1f", r - b }')
          echo "  PAIR $CLASS is judged in both teams (${teams%,}) with ids ${TINT_ID[$CLASS]%,} and pixels ${TINT_PIX[$CLASS]%,}"
          echo "  PAIR $CLASS red-minus-blu gap = $gap (red ${red_rmb:-<none>}, blu ${blu_rmb:-<none>})"
          if awk -v g="$gap" -v m="$PAIR_MIN_GAP" 'BEGIN { exit !(g >= m) }'; then
            echo "  OK   the two teams separate by $gap >= $PAIR_MIN_GAP"
          else
            say_fail "$CLASS: the red body reads ${red_rmb:-<none>} and the blu body ${blu_rmb:-<none>}; the red-minus-blu gap $gap is below $PAIR_MIN_GAP"
          fi
          ;;
        *)
          say_fail "$CLASS was targeted in both teams but judged only in team(s) ${teams:-<none>}, so its pair claim cannot be made (occluded or mis-framed body?)"
          ;;
      esac
      ;;
  esac
done

if [ "$want_pairs" = "0" ]; then
  echo "  OK   this batch requests one team per class, so it makes no pair claim (the pairs are made by the batches that target both)"
fi
[ "$pair_found" = "$want_pairs" ] || say_fail "only $pair_found of the $want_pairs requested pairs were judged in both teams"
# HOW MANY TEAMS THIS BATCH OWES A BODY IN. A batch that asks for one team only
# (the singles batch) must not be failed for not covering the other team, but it
# still has to judge at least one body: a batch that judged nothing proves nothing
# whatever it asked for.
want_teams=""
while read -r LABEL CLASS ENT TICK OX OY OZ; do
  case "$LABEL" in
    red-*) case "$want_teams" in *2*) ;; *) want_teams="${want_teams}2" ;; esac ;;
    blu-*) case "$want_teams" in *3*) ;; *) want_teams="${want_teams}3" ;; esac ;;
  esac
done < "$INPUT/targets.txt"
judged_teams=""
for CLASS in "${!TINT_TEAM[@]}"; do judged_teams="${judged_teams}${TINT_TEAM[$CLASS]}"; done
case "$want_teams" in
  *2*3*|*3*2*)
    case "$judged_teams" in
      *2*3*|*3*2*) echo "  OK   this batch asked for both teams and judged bodies in both" ;;
      *) say_fail "a two-team batch judged bodies in team(s) ${judged_teams:-<none>} only; a team claim needs at least one body per team" ;;
    esac
    ;;
  *)
    [ "$judged" -ge 1 ] || say_fail "no body was judged at all, so this batch makes no claim"
    ;;
esac

echo
echo "==========================================================================="
if [ "$fail" = "0" ]; then
  echo "BODY-APPEARANCE=PASS"
else
  echo "BODY-APPEARANCE=FAIL (see the FAIL lines above)"
fi
exit "$fail"
