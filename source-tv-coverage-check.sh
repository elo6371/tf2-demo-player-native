#!/usr/bin/env bash
# source-tv-coverage-check.sh -- decode every real SourceTV demo this machine has,
# and assert the reading is zero-failure on all of them.
#
# Why this exists
# ---------------
# The P0/P1 chain's SourceTV evidence was thin and nobody had said how thin. The
# 24-demo pinned calibration set (evidence/corpus-calib/demos.txt) holds exactly one
# SourceTV recording -- 73.dem -- so 112175 of the 1361392 packets it covers are
# SourceTV. Everything else SourceTV-side in the chain is bagel and snakewater inside
# the nine-demo oracle set. The review's second open item, "real SourceTV coverage is
# limited", is that number, not an opinion.
#
# A header scan of the whole local corpus (1643 .dem, evidence/header-scan/) found
# nine SourceTV recordings in total, so "widen the coverage" is bounded: decode all
# nine and check each one, rather than a four-hour census of 1634 POV files that share
# one code path with the five POV demos already in the oracle set.
#
# What it asserts per demo
# ------------------------
#   A1 the header classifier and the in-stream scan agree it is SourceTV
#   A2 the in-stream SourceTV signals are set (hltv=1, replay_bit=0, source_tv_flag=1)
#   A3 the demo index covers the whole file: index_state=ok and
#      index_tail_bytes == the file's byte size. This is what makes "the demo was read
#      end to end" a reading instead of a hope -- a probe that stopped at the first
#      packet would otherwise report the same zero failures.
#   A4 packets_scanned and map match the pinned values (a frozen file; a mismatch
#      means the input moved, which is loud on purpose)
#   A5 entity_failures=0, malformed_packets=0, unknown_message_packets=0,
#      unknown_message_types=<none>, delta_base_unavailable=0
#
# A5's delta_base_unavailable speaks to the P0 task text ("SourceTV delta/base
# semantics"): it counts entity updates whose delta base was never available, which is
# the SourceTV-specific failure mode a POV recording cannot show.
#
# Cost: ~3.5 min for all nine (664 MB, ~918k packets). The probe runs once per demo and
# its raw output is kept under evidence/source-tv-coverage/, so --mutation only
# perturbs the comparison and does not re-decode.
#
# Usage: bash source-tv-coverage-check.sh [--mutation] [--refresh]
#          --mutation  nudge two compared values and require exactly 9 red rows each
#          --refresh   re-run the probe even if a fresh dump is already present
# Exit:  0 = every demo read end to end with zero failures.
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_protocol_probe.exe
DEMOS_DIR="${SOURCE_TV_DEMOS_DIR:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos}"
OUT="${SOURCE_TV_OUT:-evidence/source-tv-coverage}"

MUTATION=0
REFRESH=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    --refresh)  REFRESH=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# name|size_bytes|packets_scanned|map
#
# Every field was read off a full decode of the frozen file, and the byte size is the
# file's own. Do not "fix" a mismatch by editing a row: a row that disagrees with the
# file is the gate doing its job, and the honest response is to find out whether the
# input moved.
DEMOS=(
  "73.dem|106397681|112175|koth_ashville_final1"
  "SUNSHINE.dem|82759239|120929|cp_sunshine"
  "gullyscout.dem|76066801|120929|cp_gullywash_f9"
  "proc2.dem|38541519|61544|cp_process_f12"
  "processdemo.dem|81960416|120904|cp_process_f12"
  "prodcutscout.dem|62389932|88307|koth_product_final"
  "review1.dem|82903524|91247|koth_ashville_final1"
  "review12.dem|80261474|120917|cp_reckoner"
  "review44.dem|53569778|80591|cp_sultry_b8a"
)

[ -x "$PROBE" ] || { echo "FATAL: probe not found: $PROBE"; exit 1; }
mkdir -p "$OUT"

# The probe prints every field on one line, separated by spaces, so splitting on
# spaces and anchoring the name is exact -- a substring match would confuse
# `recording` with `recording_stream`.
field() { # field <dump> <name>
  tr ' ' '\n' < "$1" | sed -n "s/^$2=//p" | head -1
}

# Collect: one probe run per demo, kept as raw output.
#
#   collect strict   re-run unless the dump is newer than the probe binary. Exe hashes
#                    are not reproducible on this machine (two forced relinks of the
#                    same source differ by two bytes), so mtime is the only freshness
#                    signal available, and a stale dump compared against a new binary
#                    is exactly the "gate that proves nothing" failure this file
#                    exists to avoid.
#   collect missing  run only where there is no usable dump at all. Used by
#                    --mutation, which perturbs the comparison and nothing else: the
#                    straight run in the same step has just written these dumps with
#                    the current binary, so re-decoding 664 MB to re-derive identical
#                    numbers would double the step's cost and buy no information.
collect() {
  local mode="$1" entry name path dump
  for entry in "${DEMOS[@]}"; do
    name=$(printf '%s' "$entry" | cut -d'|' -f1)
    path="$DEMOS_DIR/$name"
    dump="$OUT/$name.txt"
    if [ ! -f "$path" ]; then
      printf 'MISSING %s (%s)\n' "$name" "$path" > "$dump"
      continue
    fi
    if [ -s "$dump" ] && ! grep -q '^MISSING ' "$dump"; then
      if [ "$mode" = "missing" ]; then
        continue
      fi
      if [ "$REFRESH" -eq 0 ] && [ "$dump" -nt "$PROBE" ]; then
        continue
      fi
    fi
    "$PROBE" "$path" > "$dump" 2>/dev/null
    tr -d '\r' < "$dump" > "$dump.lf" && mv "$dump.lf" "$dump"
  done
}

ok=0
fail=0
demos_read=0
sum_packets=0
sum_failures=0

# run_assertions <perturb>
#   perturb=0  the real comparison
#   perturb=1  packets_scanned expected one higher (a demo read only in part)
#   perturb=2  the expected in-stream STV flag flipped (a recording misclassified)
run_assertions() {
  local perturb="$1" entry name size packets map path dump
  local want_packets want_flag ef got
  ok=0; fail=0; demos_read=0; sum_packets=0; sum_failures=0
  for entry in "${DEMOS[@]}"; do
    name=$(printf '%s' "$entry" | cut -d'|' -f1)
    size=$(printf '%s' "$entry" | cut -d'|' -f2)
    packets=$(printf '%s' "$entry" | cut -d'|' -f3)
    map=$(printf '%s' "$entry" | cut -d'|' -f4)
    path="$DEMOS_DIR/$name"
    dump="$OUT/$name.txt"
    want_packets="$packets"
    want_flag=1
    [ "$perturb" -eq 1 ] && want_packets=$((packets + 1))
    [ "$perturb" -eq 2 ] && want_flag=0

    if [ ! -f "$path" ]; then
      printf '  FAIL input %s: missing (%s)\n' "$name" "$path"
      fail=$((fail + 1))
      continue
    fi
    if [ ! -s "$dump" ] || grep -q '^MISSING ' "$dump"; then
      printf '  FAIL input %s: no probe output at %s\n' "$name" "$dump"
      fail=$((fail + 1))
      continue
    fi
    demos_read=$((demos_read + 1))

    # A1 -- both classifiers, because the P0 defect was the header one saying POV.
    if [ "$(field "$dump" recording)" = "SourceTV" ] \
       && [ "$(field "$dump" recording_stream)" = "SourceTV" ]; then
      printf '  OK   A1 recording type %s: header+stream=SourceTV\n' "$name"
      ok=$((ok + 1))
    else
      printf '  FAIL A1 recording type %s: header=%s stream=%s want=SourceTV\n' \
        "$name" "$(field "$dump" recording)" "$(field "$dump" recording_stream)"
      fail=$((fail + 1))
    fi

    # A2 -- the in-stream signals, independently of the header.
    if [ "$(field "$dump" server_info_hltv)" = "1" ] \
       && [ "$(field "$dump" server_info_replay_bit)" = "0" ] \
       && [ "$(field "$dump" source_tv_flag)" = "$want_flag" ]; then
      printf '  OK   A2 STV signals %s: hltv=1 replay=0 flag=%s\n' "$name" "$want_flag"
      ok=$((ok + 1))
    else
      printf '  FAIL A2 STV signals %s: hltv=%s replay=%s flag=%s want_flag=%s\n' \
        "$name" "$(field "$dump" server_info_hltv)" "$(field "$dump" server_info_replay_bit)" \
        "$(field "$dump" source_tv_flag)" "$want_flag"
      fail=$((fail + 1))
    fi

    # A3 -- the whole file was indexed and walked.
    if [ "$(field "$dump" index_state)" = "ok" ] \
       && [ "$(field "$dump" index_tail_bytes)" = "$size" ]; then
      printf '  OK   A3 whole file %s: index_state=ok tail_bytes=%s\n' "$name" "$size"
      ok=$((ok + 1))
    else
      printf '  FAIL A3 whole file %s: index_state=%s tail_bytes=%s want=%s\n' \
        "$name" "$(field "$dump" index_state)" "$(field "$dump" index_tail_bytes)" "$size"
      fail=$((fail + 1))
    fi

    # A4 -- the reading is the one pinned for this frozen file.
    if [ "$(field "$dump" packets_scanned)" = "$want_packets" ] \
       && [ "$(field "$dump" map)" = "$map" ]; then
      printf '  OK   A4 pinned input %s: packets_scanned=%s map=%s\n' \
        "$name" "$(field "$dump" packets_scanned)" "$(field "$dump" map)"
      ok=$((ok + 1))
    else
      printf '  FAIL A4 pinned input %s: packets_scanned=%s map=%s want=%s/%s\n' \
        "$name" "$(field "$dump" packets_scanned)" "$(field "$dump" map)" "$want_packets" "$map"
      fail=$((fail + 1))
    fi

    # A5 -- the failures this whole exercise is about.
    if [ "$(field "$dump" entity_failures)" = "0" ] \
       && [ "$(field "$dump" malformed_packets)" = "0" ] \
       && [ "$(field "$dump" unknown_message_packets)" = "0" ] \
       && [ "$(field "$dump" delta_base_unavailable)" = "0" ] \
       && grep -q 'unknown_message_types=<none>' "$dump"; then
      printf '  OK   A5 zero failures %s: entity=0 malformed=0 unknown=0 delta_base_unavailable=0\n' "$name"
      ok=$((ok + 1))
    else
      printf '  FAIL A5 zero failures %s: entity=%s malformed=%s unknown=%s delta_base_unavailable=%s none_lines=%s\n' \
        "$name" "$(field "$dump" entity_failures)" "$(field "$dump" malformed_packets)" \
        "$(field "$dump" unknown_message_packets)" "$(field "$dump" delta_base_unavailable)" \
        "$(grep -c 'unknown_message_types=<none>' "$dump")"
      fail=$((fail + 1))
    fi

    sum_packets=$((sum_packets + ${want_packets:-0}))
    ef=$(field "$dump" entity_failures)
    sum_failures=$((sum_failures + ${ef:-0}))
  done

  # The work counts. Nine demos and 917543 packets is the pinned coverage; a run that
  # read nothing must not be able to print PASS.
  if [ "$demos_read" -eq "${#DEMOS[@]}" ]; then
    printf '  OK   W1 demos read: %s/%s\n' "$demos_read" "${#DEMOS[@]}"
    ok=$((ok + 1))
  else
    printf '  FAIL W1 demos read: %s/%s\n' "$demos_read" "${#DEMOS[@]}"
    fail=$((fail + 1))
  fi
  if [ "$sum_packets" -ge 900000 ]; then
    printf '  OK   W2 packets covered: %s (floor 900000)\n' "$sum_packets"
    ok=$((ok + 1))
  else
    printf '  FAIL W2 packets covered: %s (floor 900000)\n' "$sum_packets"
    fail=$((fail + 1))
  fi
}

echo "source-tv-coverage-check  demos_dir=$DEMOS_DIR"
echo

if [ "$MUTATION" -eq 1 ]; then
  collect missing
  printf '  --- mutation P1: packets_scanned expected one higher per demo ---\n'
  run_assertions 1
  p1_fail=$fail
  printf '  P1 assertions_ok=%s assertions_fail=%s\n' "$ok" "$fail"
  echo
  printf '  --- mutation P2: in-stream SourceTV flag expected 0 ---\n'
  run_assertions 2
  p2_fail=$fail
  printf '  P2 assertions_ok=%s assertions_fail=%s\n' "$ok" "$fail"
  echo
  # Each perturbation must be caught, and by exactly the demos it targets: a
  # perturbation that reddens everything, or nothing, would not show that the
  # comparison is per-demo.
  mut_rc=0
  [ "$p1_fail" -eq "${#DEMOS[@]}" ] || mut_rc=1
  [ "$p2_fail" -eq "${#DEMOS[@]}" ] || mut_rc=1
  if [ "$mut_rc" -eq 0 ]; then
    echo "MUTATION-CAUGHT=PASS (2/2 perturbations caught, red_rows=$((p1_fail + p2_fail)))"
  else
    echo "MUTATION-CAUGHT=FAIL (p1_fail=$p1_fail p2_fail=$p2_fail want ${#DEMOS[@]} each)"
  fi
  exit "$mut_rc"
fi

collect strict
run_assertions 0

echo
printf 'demos_read=%s packets_scanned=%s entity_failures=%s assertions_ok=%s assertions_fail=%s\n' \
  "$demos_read" "$sum_packets" "$sum_failures" "$ok" "$fail"
if [ "$fail" -eq 0 ]; then
  echo "SOURCE-TV-COVERAGE=PASS (demos=$demos_read packets=$sum_packets assertions_ok=$ok)"
  exit 0
fi
echo "SOURCE-TV-COVERAGE=FAIL (assertions_fail=$fail)"
exit 1
