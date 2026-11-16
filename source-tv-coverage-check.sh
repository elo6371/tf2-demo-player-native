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
# Input resolution (2026-11-16)
# -----------------------------
# The Steam install's tf/demos directory no longer holds eight of the nine pinned
# recordings -- only proc2.dem is still there. Three of the eight were found intact
# elsewhere on this host and are resolved by explicit path in REPLACED below; each
# decodes to exactly the values pinned in the 2026-11-10 dumps (73.dem 112,179
# commands / 112,175 packets, SUNSHINE.dem 120,933 / 120,929, prodcutscout.dem
# 88,311 / 88,307) and each now carries a sha256 pin (A6).
#
# review1.dem (82,903,524 B, sha256 bf36a2b0dd7a433d30d59a561d1f7ba5a1db3749b783b9f2e35c7353d8575def,
# 91,247 packets, koth_ashville_final1) is gone. Per the 2026-11-16 ruling the slot
# reads the 2026-11-15 SourceTV recording of the same map instead, registered in
# docs/GATE-INPUT-REPLACEMENT-2026-11-16.md. That is a replacement, not a recovery: a
# shorter recording (69,646 ticks vs 91,265), so the old nine-row pin of 917,543 falls
# to 895,939 packets (-21,604, -2.35%).
#
# The 2026-11-16 corpus ruling then covered the remaining four lost files:
#   gullyscout.dem, processdemo.dem and review44.dem are also gone, and each slot now
#   reads a registered same-map SourceTV recording (explicit path, sha256 pin in A6 and
#   the reason in REPLACED_NOTE). Their rows carry the replacement's measured values.
#   review12.dem (cp_reckoner, 80,261,474 B, 120,917 packets) is NOT replaced: no
#   cp_reckoner SourceTV recording exists on this host, so it left the executable set
#   and is registered as BLOCKED-INPUT below rather than being papered over.
#
# So the active set is eight recordings, 8/8 when every row reads, and the historical
# nine-recording target is 8/9 verified -- not "coverage restored".
#
# Accounting, in one place (the two numbers that used to float around are different
# things): 917,543 = original nine-recording target; 895,939 = pin after the review1
# substitution; 834,460 = the active eight-row pin
# (112175+120929+120922+61544+120920+88307+69643+140020). W2 compares the measured sum
# to that exact pin, not to a rounded floor under it.
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
# Cost: ~4 min for all eight (612 MB, ~834k packets). The probe runs once per demo and
# its raw output is kept under evidence/source-tv-coverage/, so --mutation only
# perturbs the comparison and does not re-decode.
#
# Usage: bash source-tv-coverage-check.sh [--mutation] [--refresh]
#          --mutation  nudge two compared values and require exactly 8 red rows each
#                      (one per active row; a work-count red is counted separately)
#          --refresh   re-run the probe even if a fresh dump is already present
# Exit:  0 = every active demo read end to end with zero failures.
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_protocol_probe.exe
DEMOS_DIR="${SOURCE_TV_DEMOS_DIR:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos}"
OUT="${SOURCE_TV_OUT:-evidence/source-tv-coverage}"

# Exact packet pin for the active eight-recording set (see "Accounting" above). W2
# requires the measured sum to reach it, so a run that read nothing, skipped a row or
# read a row wrong cannot print PASS, and no row may be edited to reach it.
W2_PIN=834460

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
  "gullyscout.dem|81660813|120922|cp_gullywash_f9"
  "proc2.dem|38541519|61544|cp_process_f12"
  "processdemo.dem|90995077|120920|cp_process_f12"
  "prodcutscout.dem|62389932|88307|koth_product_final"
  "review1.dem|64122700|69643|koth_ashville_final1"
  "review44.dem|85506321|140020|cp_sultry_b8a"
)

# Overridden inputs: slot name -> absolute path, the file's own sha256 pin (A6), and a
# one-line note printed at start-up. Three entries are relocations of the pinned bytes;
# review1.dem is a replacement, and its row above carries the replacement's measured
# values, not the lost recording's.
declare -A REPLACED=(
  ["73.dem"]="D:/TF2_Demo_Player/work/protocol-rescue-20261005/73.dem"
  ["SUNSHINE.dem"]="E:/demo测试/cp_sunshine/STV/SUNSHINE.dem"
  ["prodcutscout.dem"]="E:/demo测试/koth_product_final/STV/prodcutscout.dem"
  ["review1.dem"]="E:/demo测试/koth_ashville_final1/STV/597450fbb345099739e824bfc762773f_match-20261115-1910-koth_ashville_final1.dem"
  ["gullyscout.dem"]="E:/demo测试/cp_gullywash_f9/STV/gully4454.dem"
  ["processdemo.dem"]="E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
  ["review44.dem"]="E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem"
)
declare -A REPLACED_SHA=(
  ["73.dem"]="69f9a864c88c6cf885cf46068f0be590e2fbd47b8d8ef9bbb2e6c71bccebccce"
  ["SUNSHINE.dem"]="7b8db6b4d5f375761723ededb89c232c842d1998f61d6134d9ad4ddc712e6a08"
  ["prodcutscout.dem"]="378734bc25bb4d3746503b36f9602489b195a58ecb2932e01436bc670d63557e"
  ["review1.dem"]="4edc6058e9b3de17b2a2f58bbd88e7dab149613d9798b012f69ad8ae10976b06"
  ["gullyscout.dem"]="3d9c3e64a0e94e618bbfa2075467f32722db4d82ee52f9ea1e9e88e093ece16d"
  ["processdemo.dem"]="81ceb1afa4c54ad5fdf14201b1f1a9049c183d11aa70c6ed431a69ebf9f73b94"
  ["review44.dem"]="032bbd5ab719012cc2a7b4858f0a5d4e1ba882b4cdae1d86f2b966a3578c4374"
)
declare -A REPLACED_NOTE=(
  ["73.dem"]="relocated, pinned bytes unchanged"
  ["SUNSHINE.dem"]="relocated, pinned bytes unchanged"
  ["prodcutscout.dem"]="relocated, pinned bytes unchanged"
  ["review1.dem"]="replacement for the lost file: original 82903524 B sha256 bf36a2b0dd7a433d30d59a561d1f7ba5a1db3749b783b9f2e35c7353d8575def, 91247 packets"
  ["gullyscout.dem"]="registered same-map replacement (2026-11-16 ruling): the pinned cp_gullywash_f9 recording is gone; this SourceTV recording decodes to 120922 packets / 81660813 B"
  ["processdemo.dem"]="registered same-map replacement (2026-11-16 ruling): the pinned cp_process_f12 recording is gone; this SourceTV recording decodes to 120920 packets / 90995077 B"
  ["review44.dem"]="registered same-map replacement (2026-11-16 ruling): the pinned cp_sultry_b8a recording is gone; this SourceTV recording decodes to 140020 packets / 85506321 B"
)
demo_path() { # demo_path <slot name>
  if [ -n "${REPLACED[$1]:-}" ]; then printf '%s' "${REPLACED[$1]}"; else printf '%s/%s' "$DEMOS_DIR" "$1"; fi
}

# Blocked inputs: pinned recordings that left the executable set without a
# replacement. They are reported at start-up and in the summary; they are not silent.
# name|size_bytes|last_measured_packets|map
BLOCKED_INPUTS=(
  "review12.dem|80261474|120917|cp_reckoner"
)
BLOCKED_NOTE="no cp_reckoner SourceTV recording exists on this host; the pinned file is gone (2026-11-16 ruling: BLOCKED-INPUT, not replaced). Historical nine-recording target stands at 8/9 verified."

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
#                    exists to avoid. A dump that is fresh by mtime but records a file
#                    length other than the row's is stale too: `git checkout --`
#                    restores a dump with the current time, and mtime alone would keep
#                    trusting a reading of the file the slot pointed at before.
#   collect missing  run only where there is no usable dump at all. Used by
#                    --mutation, which perturbs the comparison and nothing else: the
#                    straight run in the same step has just written these dumps with
#                    the current binary, so re-decoding 664 MB to re-derive identical
#                    numbers would double the step's cost and buy no information.
collect() {
  local mode="$1" entry name size path dump
  for entry in "${DEMOS[@]}"; do
    name=$(printf '%s' "$entry" | cut -d'|' -f1)
    size=$(printf '%s' "$entry" | cut -d'|' -f2)
    path=$(demo_path "$name")
    dump="$OUT/$name.txt"
    if [ ! -f "$path" ]; then
      # A moved input must not destroy the last reading of the file the slot used to
      # point at: keep an existing dump and let the assertion report the missing input.
      if [ ! -s "$dump" ]; then
        printf 'MISSING %s (%s)\n' "$name" "$path" > "$dump"
      fi
      continue
    fi
    if [ -s "$dump" ] && ! grep -q '^MISSING ' "$dump"; then
      if [ "$mode" = "missing" ]; then
        continue
      fi
      if [ "$REFRESH" -eq 0 ] && [ "$dump" -nt "$PROBE" ] \
         && [ "$(field "$dump" index_tail_bytes)" = "$size" ]; then
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
    path=$(demo_path "$name")
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

    # A6 -- a replaced slot proves it read the registered file, not whatever else sits
    # at that path. Only replaced slots carry this pin: the shipped eight were frozen
    # in place before any sha256 was recorded for them, and inventing hashes for files
    # the gate never measured would be a claim, not an assertion.
    if [ -n "${REPLACED_SHA[$name]:-}" ]; then
      got_sha=$(sha256sum "$path" 2>/dev/null | cut -d' ' -f1)
      if [ "$got_sha" = "${REPLACED_SHA[$name]}" ]; then
        printf '  OK   A6 replacement identity %s: sha256=%s\n' "$name" "$got_sha"
        ok=$((ok + 1))
      else
        printf '  FAIL A6 replacement identity %s: sha256=%s want=%s\n' "$name" "$got_sha" "${REPLACED_SHA[$name]}"
        fail=$((fail + 1))
      fi
    fi

    sum_packets=$((sum_packets + ${want_packets:-0}))
    ef=$(field "$dump" entity_failures)
    sum_failures=$((sum_failures + ${ef:-0}))
  done

  # The work counts. Eight active recordings and 834,460 packets is the active-set pin
  # after the 2026-11-16 corpus ruling (three registered same-map replacements, review12
  # out of the executable set as BLOCKED-INPUT). The two older numbers stay apart:
  # 917,543 is the original nine-recording target, 895,939 the pin after the review1
  # substitution -- neither is this gate's pin now. W2 compares the measured sum to the
  # exact pin, so a run that read nothing or read a row wrong cannot print PASS.
  if [ "$demos_read" -eq "${#DEMOS[@]}" ]; then
    printf '  OK   W1 demos read: %s/%s\n' "$demos_read" "${#DEMOS[@]}"
    ok=$((ok + 1))
  else
    printf '  FAIL W1 demos read: %s/%s\n' "$demos_read" "${#DEMOS[@]}"
    fail=$((fail + 1))
  fi
  if [ "$sum_packets" -ge "$W2_PIN" ]; then
    printf '  OK   W2 packets covered: %s (pin %s)\n' "$sum_packets" "$W2_PIN"
    ok=$((ok + 1))
  else
    printf '  FAIL W2 packets covered: %s (pin %s)\n' "$sum_packets" "$W2_PIN"
    fail=$((fail + 1))
  fi
}

echo "source-tv-coverage-check  demos_dir=$DEMOS_DIR  active=${#DEMOS[@]}  blocked=${#BLOCKED_INPUTS[@]}"
for _name in "${!REPLACED[@]}"; do
  printf 'input override %s -> %s\n' "$_name" "${REPLACED[$_name]}"
  printf '                 %s\n' "${REPLACED_NOTE[$_name]:-}"
done
for _row in "${BLOCKED_INPUTS[@]}"; do
  printf 'blocked input %s\n' "$_row"
done
printf 'blocked note: %s\n' "$BLOCKED_NOTE"
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
  # Expected reds = one per corpus row (each perturbed row reddens, each missing input
  # fails as missing) plus whichever work counts fail on this corpus. The work counts are
  # not perturbations, so they are counted separately instead of being folded into the
  # per-demo proof: a corpus that cannot cover the pin must show that here, not hide it.
  work_reds=0
  [ "$demos_read" -eq "${#DEMOS[@]}" ] || work_reds=$((work_reds + 1))
  [ "$sum_packets" -ge "$W2_PIN" ] || work_reds=$((work_reds + 1))
  want_reds=$((${#DEMOS[@]} + work_reds))
  mut_rc=0
  [ "$p1_fail" -eq "$want_reds" ] || mut_rc=1
  [ "$p2_fail" -eq "$want_reds" ] || mut_rc=1
  if [ "$mut_rc" -eq 0 ]; then
    echo "MUTATION-CAUGHT=PASS (2/2 caught: per_demo_reds=${#DEMOS[@]} work_reds=$work_reds red_rows=$((p1_fail + p2_fail)))"
  else
    echo "MUTATION-CAUGHT=FAIL (p1_fail=$p1_fail p2_fail=$p2_fail want $want_reds each = ${#DEMOS[@]} demos + $work_reds work counts)"
  fi
  exit "$mut_rc"
fi

collect strict
run_assertions 0

echo
printf 'demos_read=%s packets_scanned=%s entity_failures=%s assertions_ok=%s assertions_fail=%s\n' \
  "$demos_read" "$sum_packets" "$sum_failures" "$ok" "$fail"
if [ "$fail" -eq 0 ]; then
  _target=$((${#DEMOS[@]} + ${#BLOCKED_INPUTS[@]}))
  echo "SOURCE-TV-COVERAGE=PASS (active=$demos_read/${#DEMOS[@]} blocked=${#BLOCKED_INPUTS[@]} packets=$sum_packets pin=$W2_PIN assertions_ok=$ok)"
  echo "coverage: active set $demos_read/${#DEMOS[@]}; historical $_target-recording target $demos_read/$_target verified (review12.dem BLOCKED-INPUT)"
  exit 0
fi
echo "SOURCE-TV-COVERAGE=FAIL (assertions_fail=$fail)"
exit 1
