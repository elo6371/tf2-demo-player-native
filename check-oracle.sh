#!/usr/bin/env bash
# Cross-check the C++ decoder against the independent Rust oracle
# (demostf tf_demo_parser, built separately as tools/ent-oracle).
#
# For each demo the oracle is re-run on the spot, so the two sides are bound to
# the same inputs in the same run:
#
#   oracle `enter` lines        <-> entity_protocol_probe enter=
#   oracle `pkt` lines          <-> entity_protocol_probe packet_entities=
#   oracle sum(entities=N)      <-> entity_protocol_probe packet_entity_updates=
#
# A fourth value, the per-demo entity failure count, is required to be zero.
#
# Usage: bash check-oracle.sh [oracle-exe] [evidence-dir] [only]
#   only -- optional comma-separated demo names to restrict the cross-check to,
#   e.g. `bagel` or `bagel,snakewater`. Empty means all nine, which is what the
#   chain's step 4 asks for. This exists so the entity tier (verify-entity.sh) can
#   cross-check *one* real demo against the oracle in about a minute instead of
#   paying for all nine every time; the full set stays the chain's job.
#
#   Restricting the set is allowed to shrink the work but not to shrink the
#   check: an unknown name would otherwise compare zero demos and still print
#   ORACLE-GATE=PASS, which is this repo's recurring "pass while checking
#   nothing" failure. The gate now asserts compared == requested.
#
# Exit:  0 = every demo agrees on all three counts and reports zero failures.
#
# MUTATION (proof this gate can go red): the m3 case in mutate.sh reintroduces
# the svc_Prefetch width bug, which changes packet_entities on protocol23.
set -uo pipefail
cd "$(dirname "$0")"

ORACLE="${1:-D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe}"
DIR="${2:-evidence/final}"
ONLY="${3:-}"
PROBE=native/build-nmake/entity_protocol_probe.exe
SRC=D:/TF2_Demo_Player
T=$SRC/.scratch/tf2-demo-parser/test_data

# -f, not -x: a directory passes -x and would silently be accepted as the
# oracle path, which makes every count read 0 instead of failing loudly.
[ -f "$ORACLE" ] || { echo "FATAL: oracle not found: $ORACLE"; exit 1; }
[ -x "$PROBE" ]  || { echo "FATAL: probe not found: $PROBE"; exit 1; }

DEMOS=(
  "bagel|$SRC/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
  "snakewater|$SRC/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
  "ashville73|$SRC/work/protocol-rescue-20261005/73.dem"
  "decal|$T/decal.dem"
  "protocol23|$T/protocol23.dem"
  "comp|$T/comp.dem"
  "saytext2|$T/saytext2.dem"
  "short2024|$T/short-2024.dem"
  "small|$T/small.dem"
)

mkdir -p "$DIR"
# The raw oracle dumps are ~94 MB for these nine demos, so they stay on disk but
# out of git (see .gitignore). The gate itself is recorded in a small summary.
SUMMARY="$DIR/oracle-gate-summary.txt"
: > "$SUMMARY"
fail=0

# requested vs compared. `requested` counts the DEMOS entries the `only` filter
# names, so a mistyped name is visible as requested=0 rather than as a smaller
# all-green run. `compared` counts the demos that actually reached a verdict.
if [ -n "$ONLY" ]; then
  requested=0
  for entry in "${DEMOS[@]}"; do
    name=${entry%%|*}
    case ",$ONLY," in *",$name,"*) requested=$((requested + 1)) ;; esac
  done
  # `requested == compared == 0` is NOT the failure this guard is for -- it is the
  # failure it would have been. The first version of this restriction only
  # asserted compared == requested, and a mistyped name satisfied it at 0 == 0
  # while printing ORACLE-GATE=PASS. Verified on 2026-10-09: `only=smal` compared
  # nothing and passed. A filter that names no demo in the list is a caller error
  # and is refused, with the offending names printed.
  unknown=""
  for want in ${ONLY//,/ }; do
    found=0
    for entry in "${DEMOS[@]}"; do
      [ "${entry%%|*}" = "$want" ] && { found=1; break; }
    done
    [ "$found" -eq 1 ] || unknown="$unknown $want"
  done
  if [ -n "$unknown" ]; then
    echo "FATAL: demo name(s) not in this gate's list:$unknown"
    echo "known names: $(printf '%s ' "${DEMOS[@]%%|*}")"
    exit 1
  fi
else
  requested=${#DEMOS[@]}
fi
compared=0
{
printf '%-12s %8s %8s  %8s %8s  %10s %10s  %7s  %s\n' \
  demo o_enter c_enter o_pkts c_pkts o_entities c_entities fails verdict
} | tee -a "$SUMMARY"

for entry in "${DEMOS[@]}"; do
  name=${entry%%|*}
  path=${entry#*|}
  if [ -n "$ONLY" ]; then
    case ",$ONLY," in *",$name,"*) ;; *) continue ;; esac
  fi
  s="$DIR/$name.txt"
  if [ ! -f "$s" ]; then echo "MISSING probe report: $s"; fail=1; continue; fi

  "$ORACLE" "$path" 0 > "$DIR/oracle-survey-$name.txt" 2>/dev/null
  "$ORACLE" "$path" 1 > "$DIR/oracle-enters-$name.txt" 2>/dev/null

  o_pkts=$(awk '/^pkt /{n++} END{print n+0}' "$DIR/oracle-survey-$name.txt")
  o_ent=$(awk '/^pkt /{while (match($0,/entities=[0-9]+/)) {s+=substr($0,RSTART+9,RLENGTH-9); $0=substr($0,RSTART+RLENGTH)}} END{print s+0}' "$DIR/oracle-survey-$name.txt")
  o_enter=$(awk '/^enter /{n++} END{print n+0}' "$DIR/oracle-enters-$name.txt")

  flat=$(tr -d '\r' < "$s" | tr '\n' ' ')
  c_enter="" c_pkts="" c_ent="" c_fail=""
  [[ $flat =~ (^|[[:space:]])enter=([0-9]+) ]]                  && c_enter=${BASH_REMATCH[2]}
  [[ $flat =~ (^|[[:space:]])packet_entities=([0-9]+) ]]        && c_pkts=${BASH_REMATCH[2]}
  [[ $flat =~ (^|[[:space:]])packet_entity_updates=([0-9]+) ]]  && c_ent=${BASH_REMATCH[2]}
  [[ $flat =~ (^|[[:space:]])entity_failures=([0-9]+) ]]        && c_fail=${BASH_REMATCH[2]}

  verdict="OK"
  [ "$o_enter" = "$c_enter" ] || verdict="MISMATCH-enter"
  [ "$o_pkts"  = "$c_pkts"  ] || verdict="MISMATCH-packets"
  [ "$o_ent"   = "$c_ent"   ] || verdict="MISMATCH-entities"
  [ "$c_fail"  = "0"        ] || verdict="NONZERO-FAILURES"
  [ "$verdict" = "OK" ] || fail=1

  printf '%-12s %8s %8s  %8s %8s  %10s %10s  %7s  %s\n' \
    "$name" "$o_enter" "$c_enter" "$o_pkts" "$c_pkts" "$o_ent" "$c_ent" "$c_fail" "$verdict" \
    | tee -a "$SUMMARY"
  compared=$((compared + 1))
done

echo | tee -a "$SUMMARY"
# `compared == requested` is the work-count assertion. Without it, a filter that
# names no demo (a typo, or an empty --only) compares nothing, exits 0, and reads
# as agreement across the corpus.
echo "compared=$compared requested=$requested" | tee -a "$SUMMARY"
[ "$compared" -eq "$requested" ] || fail=1
if [ "$fail" -eq 0 ]; then echo "ORACLE-GATE=PASS" | tee -a "$SUMMARY"; else echo "ORACLE-GATE=FAIL" | tee -a "$SUMMARY"; fi
exit "$fail"
