#!/usr/bin/env bash
# userinfo-check.sh -- forward gate for the player-identity decoder.
#
# What it checks: the "userinfo" string table (a verbatim player_info_t per slot)
# is decoded by demo_open_probe, names come out of it, a damaged message is
# counted instead of silently yielding an empty table, and asking for a name at
# a tick gives the same answer regardless of the order the ticks are asked in.
#
# Every value assertion is preceded by an existence assertion on the same
# reading, so a missing trace line cannot pass as a match.
#
# The script launches the probe, so run it inside the host lock:
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> \
#     bash userinfo-check.sh [--mutation]
# It deliberately does not acquire the lock itself (hostlock is not reentrant).
#
#   bash userinfo-check.sh              # assertions only (fixtures + 3 demos + seek)
#   bash userinfo-check.sh --quick      # fixtures only (seconds; used after a restore)
#   bash userinfo-check.sh --ticktest   # fixtures + one demo's tick-2000 answer
#   bash userinfo-check.sh --mutation   # one mutation per named assertion, then green
#   bash userinfo-check.sh --mutation --skip-baseline
#                                       # the same, when the caller already ran the
#                                       # full baseline and only wants the mutants
set -uo pipefail
cd "$(dirname "$0")"

PY=C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe
PROBE=native/build-nmake/demo_open_probe.exe
OUT=evidence/t100-userinfo/runs
FIX=evidence/t100-userinfo/fixtures
GOLD=evidence/t100-userinfo/golden
DEMO_DIR="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"
DEMOS="review1 prodcutscout 73"
# The last tick is far beyond any demo's end: every slot that ever had a name must
# resolve there. 60000 is after signon in this corpus, 2000 is before it.
AT_FORWARD="0,2000,60000,99999999"
AT_REVERSE="99999999,60000,2000,0"
# Repeat asks for a tick that resolves names twice, so the two printed blocks can
# be compared. A tick before signon resolves nothing and would make the check
# vacuous, which is why 2000 is not used here.
AT_REPEAT="600000,600000"

MUTATION=0
QUICK=0
SKIP_BASELINE=0
TICKTEST=0
for argument in "$@"; do
  [ "$argument" = "--mutation" ] && MUTATION=1
  [ "$argument" = "--quick" ] && QUICK=1
  [ "$argument" = "--skip-baseline" ] && SKIP_BASELINE=1
  [ "$argument" = "--ticktest" ] && TICKTEST=1
done
# The corpus sections are the slow part (3 demos x 4 probe runs). A mutation run
# reuses the baseline the caller already measured and only pays for the parts the
# mutant is supposed to break.
SKIP_CORPUS=$QUICK
[ "$SKIP_BASELINE" -eq 1 ] && SKIP_CORPUS=1

fail=0
asserts=0
ok() {
  printf 'ok   %s\n' "$1"
  asserts=$((asserts + 1))
}
bad() {
  printf 'FAIL %s\n' "$1"
  fail=$((fail + 1))
}
# assert_eq <label> <got> <want>
assert_eq() {
  if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 = $2, expected $3"; fi
}
# assert_ne <label> <got> <unwanted> -- fails when the reading equals a shape it
# must not have (used for the silent-empty negative control).
assert_ne() {
  if [ "$2" != "$3" ]; then ok "$1"; else bad "$1 = $2, which is exactly the forbidden shape"; fi
}
# assert_ge <label> <got> <minimum>
assert_ge() {
  if [ -n "$2" ] && [ "$2" -ge "$3" ] 2>/dev/null; then ok "$1"; else bad "$1 = $2, expected >= $3"; fi
}
# field <wide line> <key> -- last value of key= on a one-line probe summary
field() {
  printf '%s' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | tail -1
}
# has_field <label> <wide line> <key> -- the existence assertion that must come
# before any assertion about that key's value.
has_field() {
  if printf '%s' "$2" | tr ' ' '\n' | grep -q "^$3="; then
    ok "$1"
  else
    bad "$1 (the reading has no $3 field)"
  fi
}
# file_field <file> <key> -- value of key= on the first line that carries it.
# The key may be the first token on the line (the fixture summary starts with
# userinfo_fixture_bytes=), so the anchor accepts start of line or whitespace; a
# pattern that only accepted whitespace would read the first key as absent and
# let an empty value through.
file_field() {
  grep -m1 -oE "(^|[[:space:]])$2=[^ ]*" "$1" | tail -1 | sed -E "s/^[[:space:]]*$2=//"
}
has_file_field() {
  if grep -qE "(^|[[:space:]])$3=" "$2" 2>/dev/null; then ok "$1"; else bad "$1 (the reading has no $3 field)"; fi
}
assert_same_file() {
  if cmp -s "$2" "$3"; then
    ok "$1"
  else
    bad "$1"
    diff "$3" "$2" | head -8 | sed 's/^/    /'
  fi
}
count_rows() {
  grep -c "^$1" "$2" 2>/dev/null || true
}

if [ ! -x "$PROBE" ]; then
  echo "FATAL: missing probe $PROBE -- run 'bash build-target.sh demo_open_probe' first" >&2
  exit 1
fi
mkdir -p "$OUT"
for input in "$DEMO_DIR/review1.dem" "$DEMO_DIR/prodcutscout.dem" "$DEMO_DIR/73.dem"; do
  [ -f "$input" ] || { echo "FATAL: missing input $input" >&2; exit 1; }
done
[ -f evidence/t100-userinfo/gen_fixtures.py ] || { echo "FATAL: missing gen_fixtures.py" >&2; exit 1; }

# A probe older than any source it is built from would report on a decoder that
# is no longer in the tree; the assertion would then be about dead code.
STALE=$(find native/src native/include native/tools -newer "$PROBE" | head -3)
if [ -n "$STALE" ]; then
  echo "FATAL: STALE-BINARY: $STALE newer than $PROBE -- run 'bash build-target.sh demo_open_probe' first" >&2
  exit 1
fi
printf 'PROBE=%s sha256=%s\n' "$PROBE" "$(sha256sum "$PROBE" | cut -d' ' -f1)"
printf 'GATE=%s sha256=%s\n' "$0" "$(sha256sum "$0" | cut -d' ' -f1)"
printf 'HEAD=%s branch=%s\n' "$(git rev-parse HEAD)" "$(git rev-parse --abbrev-ref HEAD)"

# --- fixtures ---------------------------------------------------------------
# Synthetic bit-exact svc_CreateStringTable messages for the "userinfo" table.
"$PY" evidence/t100-userinfo/gen_fixtures.py "$FIX" > "$OUT/fixture-build.txt" 2>&1 || {
  echo "FATAL: the fixture generator failed; see $OUT/fixture-build.txt" >&2
  exit 1
}
run_fixture() {
  local name="$1"
  "$PROBE" --userinfo-fixture "$FIX/userinfo-$name.demmsg" > "$OUT/fixture-$name.txt" 2>&1
  return $?
}

echo
echo "=== fixtures ==="
run_fixture good
assert_eq "fixtures: good exits 0" "$?" "0"
# Existence first: every value read below must be present on the summary line,
# otherwise an empty read could be compared as if it were a measurement.
for key in userinfo_fixture_bytes userinfo_fixture_packet_valid userinfo_entries userinfo_slots userinfo_named userinfo_failures userinfo_unparsed userinfo_text_slot_mismatches userinfo_layout_mask; do
  has_file_field "fixtures: good summary carries $key" "$OUT/fixture-good.txt" "$key"
done
assert_eq "fixtures: good streams valid" "$(file_field "$OUT/fixture-good.txt" userinfo_fixture_packet_valid)" "1"
assert_eq "fixtures: good entries" "$(file_field "$OUT/fixture-good.txt" userinfo_entries)" "2"
assert_eq "fixtures: good slots" "$(file_field "$OUT/fixture-good.txt" userinfo_slots)" "2"
assert_eq "fixtures: good named" "$(file_field "$OUT/fixture-good.txt" userinfo_named)" "2"
assert_eq "fixtures: good failures" "$(file_field "$OUT/fixture-good.txt" userinfo_failures)" "0"
assert_eq "fixtures: good unparsed" "$(file_field "$OUT/fixture-good.txt" userinfo_unparsed)" "0"
has_file_field "fixtures: good printed text/slot mismatches" "$OUT/fixture-good.txt" userinfo_text_slot_mismatches
assert_eq "fixtures: good entry text matches its slot" "$(file_field "$OUT/fixture-good.txt" userinfo_text_slot_mismatches)" "0"
has_file_field "fixtures: good printed layout mask" "$OUT/fixture-good.txt" userinfo_layout_mask
assert_eq "fixtures: good layout_mask" "$(file_field "$OUT/fixture-good.txt" userinfo_layout_mask)" "1"
assert_eq "fixtures: good row count" "$(count_rows '^[0-9]' "$OUT/fixture-good.txt")" "2"
cut -f1-6 "$OUT/fixture-good.txt" | grep '^[0-9]' > "$OUT/fixture-good-rows.txt" || true
printf '0\t1\t7\tAlpha\t0\t0\n1\t2\t9\tBravo\t0\t1\n' > "$OUT/fixture-good-want.txt"
assert_same_file "fixtures: good rows are the two players that were written" "$OUT/fixture-good-rows.txt" "$OUT/fixture-good-want.txt"

run_fixture truncated
assert_eq "fixtures: truncated still exits 0" "$?" "0"
for key in userinfo_fixture_bytes userinfo_fixture_packet_valid userinfo_entries userinfo_failures userinfo_unparsed; do
  has_file_field "fixtures: truncated summary carries $key" "$OUT/fixture-truncated.txt" "$key"
done
assert_eq "fixtures: truncated stream is rejected" "$(file_field "$OUT/fixture-truncated.txt" userinfo_fixture_packet_valid)" "0"
assert_eq "fixtures: truncated decodes no entry" "$(file_field "$OUT/fixture-truncated.txt" userinfo_entries)" "0"
SILENT=$(field "$(cat "$OUT/fixture-truncated.txt")" userinfo_fixture_packet_valid):$(file_field "$OUT/fixture-truncated.txt" userinfo_entries):$(file_field "$OUT/fixture-truncated.txt" userinfo_failures):$(file_field "$OUT/fixture-truncated.txt" userinfo_unparsed)
assert_ne "fixtures: truncated is not a valid-looking empty table" "$SILENT" "1:0:0:0"

run_fixture shortpayload
assert_eq "fixtures: shortpayload still exits 0" "$?" "0"
for key in userinfo_entries userinfo_failures userinfo_named; do
  has_file_field "fixtures: shortpayload summary carries $key" "$OUT/fixture-shortpayload.txt" "$key"
done
assert_eq "fixtures: shortpayload decoded only the complete entry" "$(file_field "$OUT/fixture-shortpayload.txt" userinfo_entries)" "1"
assert_ge "fixtures: shortpayload counted the truncated walk" "$(file_field "$OUT/fixture-shortpayload.txt" userinfo_failures)" "1"
assert_eq "fixtures: shortpayload named" "$(file_field "$OUT/fixture-shortpayload.txt" userinfo_named)" "1"

run_fixture badname
assert_eq "fixtures: badname still exits 0" "$?" "0"
for key in userinfo_entries userinfo_unparsed userinfo_named; do
  has_file_field "fixtures: badname summary carries $key" "$OUT/fixture-badname.txt" "$key"
done
assert_eq "fixtures: badname entries are present" "$(file_field "$OUT/fixture-badname.txt" userinfo_entries)" "2"
assert_eq "fixtures: badname counted as unparsed" "$(file_field "$OUT/fixture-badname.txt" userinfo_unparsed)" "2"
assert_eq "fixtures: badname produced no name" "$(file_field "$OUT/fixture-badname.txt" userinfo_named)" "0"
assert_ne "fixtures: badname is not a silent empty table" "$(file_field "$OUT/fixture-badname.txt" userinfo_entries)" "0"

# --- corpus -----------------------------------------------------------------
# Fixed readings from nine frozen demo files. Each demo is a snapshot of the
# server that recorded it, not a claim about live servers.
run_demo() {
  local demo="$1"
  local ats="$2"
  local tag="$3"
  "$PROBE" --scan --userinfo --userinfo-at "$ats" "$DEMO_DIR/$demo.dem" > "$OUT/$demo-$tag.txt" 2>&1
  return $?
}
check_corpus() {
  local demo="$1"
  local tag="$2"
  local file="$OUT/$demo-$tag.txt"
  local line
  line=$(grep -m1 'userinfo_table_id=' "$file" || true)
  has_field "$demo: table id reported" "$line" userinfo_table_id
  assert_ne "$demo: userinfo table exists" "$(field "$line" userinfo_table_id)" "4294967295"
  has_field "$demo: create count reported" "$line" userinfo_create
  assert_ge "$demo: table created at least once" "$(field "$line" userinfo_create)" "1"
  has_field "$demo: entry count reported" "$line" userinfo_entries
  assert_ge "$demo: entries decoded" "$(field "$line" userinfo_entries)" "1"
  has_field "$demo: slot count reported" "$line" userinfo_slots
  assert_ge "$demo: slots decoded" "$(field "$line" userinfo_slots)" "1"
  has_field "$demo: named count reported" "$line" userinfo_named
  assert_ge "$demo: names decoded" "$(field "$line" userinfo_named)" "1"
  has_field "$demo: failure count reported" "$line" userinfo_failures
  assert_eq "$demo: no table decode failure" "$(field "$line" userinfo_failures)" "0"
  has_field "$demo: unparsed count reported" "$line" userinfo_unparsed
  assert_eq "$demo: no unparsed entry" "$(field "$line" userinfo_unparsed)" "0"
  has_field "$demo: empty-name count reported" "$line" userinfo_empty_names
  has_field "$demo: missing-userdata count reported" "$line" userinfo_missing_userdata
  # Every record whose user data was absent leaves the name empty and nothing
  # else does, so the two counters must agree exactly. A decoder that silently
  # substitutes something for the name (the slot's text, a placeholder) breaks
  # this without touching either count alone.
  assert_eq "$demo: empty names are exactly the slots without user data" \
    "$(field "$line" userinfo_empty_names)" \
    "$(( $(field "$line" userinfo_missing_userdata) + $(field "$line" userinfo_unparsed) ))"
  has_field "$demo: sample count reported" "$line" userinfo_samples
  assert_eq "$demo: one sample per named slot in this corpus" "$(field "$line" userinfo_samples)" "$(field "$line" userinfo_named)"
  has_field "$demo: text/slot mismatch count reported" "$line" userinfo_text_slot_mismatches
  assert_eq "$demo: entry text agrees with the slot" "$(field "$line" userinfo_text_slot_mismatches)" "0"
  has_field "$demo: table reset count reported" "$line" userinfo_table_resets
  assert_eq "$demo: the table is created once" "$(field "$line" userinfo_table_resets)" "0"
  has_field "$demo: duplicate userid count reported" "$line" userinfo_duplicate_userids
  assert_eq "$demo: no duplicate userid" "$(field "$line" userinfo_duplicate_userids)" "0"
  has_field "$demo: layout mask reported" "$line" userinfo_layout_mask
  assert_ge "$demo: a layout matched" "$(field "$line" userinfo_layout_mask)" "1"
  # Rows: entry/entity/userId/name/firstTick/lastTick/updates/layoutId/tableText/guid
  grep '^[0-9]' "$file" | cut -f1-9 | sort -t"$(printf '\t')" -k1,1n > "$OUT/$demo-$tag-rows.txt" || true
  assert_ge "$demo: slot rows printed" "$(wc -l < "$OUT/$demo-$tag-rows.txt" | tr -d ' ')" "1"
  assert_eq "$demo: one row per decoded slot" "$(wc -l < "$OUT/$demo-$tag-rows.txt" | tr -d ' ')" "$(field "$line" userinfo_slots)"
  local row_entity_bad
  row_entity_bad=$(awk -F'\t' 'NF>=3 && $2 != $1+1 {print; bad=1} END{if (bad) exit 1}' "$OUT/$demo-$tag-rows.txt" | head -3)
  assert_eq "$demo: every slot maps to entity slot+1" "$row_entity_bad" ""
  local row_text_bad
  row_text_bad=$(awk -F'\t' 'NF>=9 && $9 != $1 {print; bad=1} END{if (bad) exit 1}' "$OUT/$demo-$tag-rows.txt" | head -3)
  assert_eq "$demo: every entry text is its own slot number" "$row_text_bad" ""
  local row_name_bad
  row_name_bad=$(awk -F'\t' 'NF>=4 && $4 ~ /[\001-\037\177]/ {print; bad=1} END{if (bad) exit 1}' "$OUT/$demo-$tag-rows.txt" | head -3)
  assert_eq "$demo: every decoded name is printable text" "$row_name_bad" ""
  # A slot either has a decoded player (layout 0, name non-empty) or has no user
  # data at all (layout 0xff, name empty). Anything else is a decoder that half
  # read a struct and reported it as a person.
  local row_layout_bad
  row_layout_bad=$(awk -F'\t' 'NF>=8 && (($4 != "" && $8 != 0) || ($4 == "" && $8 != 255)) {print; bad=1} END{if (bad) exit 1}' "$OUT/$demo-$tag-rows.txt" | head -3)
  assert_eq "$demo: every row is either a decoded player or an empty slot" "$row_layout_bad" ""
  local named_rows
  named_rows=$(awk -F'\t' '$4 != ""' "$OUT/$demo-$tag-rows.txt" | wc -l | tr -d ' ')
  assert_eq "$demo: named row count agrees with the counter" "$named_rows" "$(field "$line" userinfo_named)"
  if [ -f "$GOLD/$demo.tsv" ]; then
    assert_same_file "$demo: slot rows match the recorded snapshot" "$OUT/$demo-$tag-rows.txt" "$GOLD/$demo.tsv"
  else
    bad "$demo: snapshot $GOLD/$demo.tsv is missing"
  fi
}

echo
echo "=== corpus ==="
if [ "$SKIP_CORPUS" -eq 1 ]; then
  echo "(skipped: --quick / --skip-baseline run only what the mutant must break)"
fi
for demo in $DEMOS; do
  [ "$SKIP_CORPUS" -eq 1 ] && break
  run_demo "$demo" "$AT_FORWARD" forward
  assert_eq "$demo: probe exits 0" "$?" "0"
  check_corpus "$demo" forward
done

# --- seek stability ---------------------------------------------------------
# One process asks for ticks forward, another backwards, a third twice; the
# answer for a given tick must be identical in all of them.
echo
echo "=== seek stability ==="
for demo in $DEMOS; do
  [ "$SKIP_CORPUS" -eq 1 ] && break
  run_demo "$demo" "$AT_REVERSE" reverse
  assert_eq "$demo: reversed probe exits 0" "$?" "0"
  run_demo "$demo" "$AT_REPEAT" repeat
  assert_eq "$demo: repeated probe exits 0" "$?" "0"
  local_resolved=$(count_rows '^userinfo_at' "$OUT/$demo-forward.txt")
  assert_ge "$demo: names resolve at the requested ticks" "$local_resolved" "1"
  # Only slots that ever carried a name have a sample, so the base is the named
  # rows, not the slot count: a slot with no user data must NOT be resolvable.
  named_rows=$(awk -F'\t' '$4 != ""' "$OUT/$demo-forward-rows.txt" | wc -l | tr -d ' ')
  assert_ge "$demo: there are named slots to resolve" "$named_rows" "1"
  last_tick=$(printf '%s' "$AT_FORWARD" | awk -F, '{print $NF}')
  at_last=$(awk -F'\t' -v t="$last_tick" '$1 == "userinfo_at" && $2 == t' "$OUT/$demo-forward.txt" | wc -l | tr -d ' ')
  assert_eq "$demo: the last tick resolves every named slot" "$at_last" "$named_rows"
  # The table is created at signon; a tick before it must resolve nothing. A
  # query that ignores its tick argument and returns the final name would fail
  # here, which is the whole point of asking at more than one tick.
  at_early=$(awk -F'\t' '$1 == "userinfo_at" && $2 == "2000"' "$OUT/$demo-forward.txt" | wc -l | tr -d ' ')
  assert_eq "$demo: a tick before the userinfo table resolves no name" "$at_early" "0"
  for tick in 2000 60000 600000; do
    awk -F'\t' -v t="$tick" '$1 == "userinfo_at" && $2 == t' "$OUT/$demo-forward.txt" > "$OUT/$demo-at-$tick-fwd.txt"
    awk -F'\t' -v t="$tick" '$1 == "userinfo_at" && $2 == t' "$OUT/$demo-reverse.txt" > "$OUT/$demo-at-$tick-rev.txt"
    if [ -s "$OUT/$demo-at-$tick-fwd.txt" ]; then
      assert_same_file "$demo: name at tick $tick does not depend on asking order" \
        "$OUT/$demo-at-$tick-rev.txt" "$OUT/$demo-at-$tick-fwd.txt"
    fi
  done
  awk -F'\t' '$1 == "userinfo_at" && $2 == "2000"' "$OUT/$demo-repeat.txt" > "$OUT/$demo-at-repeat.txt"
  half=$(wc -l < "$OUT/$demo-at-repeat.txt" | tr -d ' ')
  if [ "$half" -ge 2 ] && [ $((half % 2)) -eq 0 ]; then
    half=$((half / 2))
    head -n "$half" "$OUT/$demo-at-repeat.txt" > "$OUT/$demo-at-repeat-a.txt"
    tail -n "$half" "$OUT/$demo-at-repeat.txt" > "$OUT/$demo-at-repeat-b.txt"
    assert_same_file "$demo: the same tick twice gives the same names" \
      "$OUT/$demo-at-repeat-a.txt" "$OUT/$demo-at-repeat-b.txt"
  else
    bad "$demo: repeated tick produced an even, non-empty pair of blocks ($half rows)"
  fi
done
# Two separate processes on the same demo must print the same bytes.
for demo in $DEMOS; do
  [ "$SKIP_CORPUS" -eq 1 ] && break
  run_demo "$demo" "$AT_FORWARD" again
  assert_eq "$demo: second process exits 0" "$?" "0"
  assert_same_file "$demo: probe output is deterministic across processes" \
    "$OUT/$demo-again.txt" "$OUT/$demo-forward.txt"
done

# --- tick answer (one demo) --------------------------------------------------
# The full seek section proves order independence; a mutation that removes the
# tick filter only needs one demo to go red, so the mutation suite runs this
# smaller section instead of the whole corpus.
if [ "$TICKTEST" -eq 1 ]; then
  echo
  echo "=== tick answer ==="
  run_demo review1 "$AT_FORWARD" tickonly
  assert_eq "ticktest: probe exits 0" "$?" "0"
  early=$(awk -F'\t' '$1 == "userinfo_at" && $2 == "2000"' "$OUT/review1-tickonly.txt" | wc -l | tr -d ' ')
  assert_eq "review1: a tick before the userinfo table resolves no name" "$early" "0"
  late_tick=$(printf '%s' "$AT_FORWARD" | awk -F, '{print $NF}')
  late=$(awk -F'\t' -v t="$late_tick" '$1 == "userinfo_at" && $2 == t' "$OUT/review1-tickonly.txt" | wc -l | tr -d ' ')
  assert_ge "review1: the last tick resolves names" "$late" "1"
fi

# --- mutation ---------------------------------------------------------------
MUTATION_FAIL=0
SOURCE=""
BACKUP=""
# run_mutation_check <tag> <named assertion> [quick|full]
# The mutated gate must report a FAIL line carrying that named assertion. The
# name is matched as a substring because corpus assertions are prefixed with the
# demo they were read from ("FAIL review1: ...").
run_mutation_check() {
  local tag="$1"
  local expected="$2"
  local mode="${3:-quick}"
  local check_file="$OUT/mutation/$tag-check.txt"
  mkdir -p "$OUT/mutation"
  case "$mode" in
    full) bash userinfo-check.sh > "$check_file" 2>&1 ;;
    ticktest) bash userinfo-check.sh --ticktest > "$check_file" 2>&1 ;;
    *) bash userinfo-check.sh --quick > "$check_file" 2>&1 ;;
  esac
  local check_rc=$?
  if grep -F "FAIL " "$check_file" | grep -qF "$expected"; then
    ok "mutation $tag caught by \"$expected\""
    printf 'MUTATION-CAUGHT=PASS %s mode=%s check_rc=%s\n' "$tag" "$mode" "$check_rc"
    return 0
  fi
  echo "MUTATION-CAUGHT=FAIL $tag (no FAIL line containing '$expected' in $check_file, check rc=$check_rc)"
  grep -F "FAIL " "$check_file" | head -5 | sed 's/^/    /'
  return 1
}
restore_and_verify() {
  local tag="$1"
  if [ -z "$BACKUP" ] || [ ! -f "$BACKUP" ]; then
    echo "FATAL: the backup for $tag is missing before restore" >&2
    exit 1
  fi
  cp "$BACKUP" "$SOURCE"
  if ! cmp -s "$BACKUP" "$SOURCE"; then
    echo "FATAL: $SOURCE was not restored byte for byte" >&2
    exit 1
  fi
  echo "RESTORED=1 $tag sha256=$(sha256sum "$SOURCE" | cut -d' ' -f1)"
  BACKUP=""
  SOURCE=""
  bash build-target.sh demo_open_probe > "$OUT/mutation/$tag-restore-build.log" 2>&1
  local build_rc=$?
  echo "restore build rc=$build_rc"
  if [ "$build_rc" -ne 0 ]; then
    echo "FATAL: the restored build did not compile" >&2
    tail -20 "$OUT/mutation/$tag-restore-build.log" >&2
    exit 1
  fi
  # Quick is enough here: the fixture half proves the restored binary decodes
  # again, and the full gate below proves the corpus half after the last restore.
  bash userinfo-check.sh --quick > "$OUT/mutation/$tag-restored.txt" 2>&1
  local restored_rc=$?
  if [ "$restored_rc" -eq 0 ]; then
    echo "BASELINE-GREEN=1 $tag"
  else
    echo "BASELINE-GREEN=0 $tag"
    grep -F "FAIL " "$OUT/mutation/$tag-restored.txt" | head -5 | sed 's/^/    /'
    MUTATION_FAIL=1
  fi
}
apply_patch() {
  local tag="$1"
  local file="$2"
  local anchor_file="$OUT/mutation/$tag-anchor.txt"
  SOURCE="$file"
  BACKUP=".scratch/userinfo-$tag.bak"
  mkdir -p .scratch
  cp "$SOURCE" "$BACKUP"
  if ! "$PY" - "$SOURCE" "$anchor_file" <<'PYEOF'
import io
import sys

path = sys.argv[1]
anchor_path = sys.argv[2]
raw = io.open(anchor_path, "rb").read()
text = raw.decode("utf-8")
anchor, inject = text.split("\n@@INJECT@@\n", 1)
source = io.open(path, "rb").read()
crlf = b"\r\n" in source
body = source.decode("utf-8").replace("\r\n", "\n") if crlf else source.decode("utf-8")
if body.count(anchor) != 1:
    sys.stderr.write("anchor occurs %d times\n" % body.count(anchor))
    sys.exit(1)
body = body.replace(anchor, inject)
if crlf:
    body = body.replace("\n", "\r\n")
io.open(path, "wb").write(body.encode("utf-8"))
print("MUTATION-PATCHED %s" % path)
PYEOF
  then
    echo "FATAL: the $tag mutation did not apply; the mutated build would prove nothing" >&2
    cp "$BACKUP" "$SOURCE"
    exit 1
  fi
  diff -u "$BACKUP" "$SOURCE" > "$OUT/mutation/$tag.diff" || true
  echo "mutation $tag source sha256=$(sha256sum "$SOURCE" | cut -d' ' -f1) diff=$OUT/mutation/$tag.diff"
  bash build-target.sh demo_open_probe > "$OUT/mutation/$tag-build.log" 2>&1
  local build_rc=$?
  echo "mutation $tag build rc=$build_rc"
  if [ "$build_rc" -ne 0 ]; then
    echo "FATAL: the $tag mutated build did not compile; the mutation proves nothing" >&2
    tail -20 "$OUT/mutation/$tag-build.log" >&2
    cp "$BACKUP" "$SOURCE"
    exit 1
  fi
}

if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== mutations ==="
  mkdir -p "$OUT/mutation"

  printf '%s\n@@INJECT@@\n%s\n' \
'    ++summary.userInfoUnparsedCount;' \
'    // MUTATION m1: a struct no layout matches is dropped without a count. The
    // bad-name fixture then looks exactly like a table of players with no names.' \
    > "$OUT/mutation/m1-anchor.txt"
  apply_patch m1 native/src/demo_header_strtab.cpp
  run_mutation_check m1 "fixtures: badname counted as unparsed" || MUTATION_FAIL=1
  restore_and_verify m1

  printf '%s\n@@INJECT@@\n%s\n' \
'    } else if (!walkStringTablePayload(tablePayload, entryCount, entryIndexBits,
                                       fixedUserData != 0u, fixedUserDataBits,
                                       summary, UserInfoSink{summary, packetTick})) {
      ++summary.userInfoDecodeFailures;
    }' \
'    } else if (!walkStringTablePayload(tablePayload, entryCount, entryIndexBits,
                                       fixedUserData != 0u, fixedUserDataBits,
                                       summary, UserInfoSink{summary, packetTick})) {
      // MUTATION m2: a userinfo payload that runs out mid-entry is not counted.
    }' \
    > "$OUT/mutation/m2-anchor.txt"
  apply_patch m2 native/src/demo_header_strtab.cpp
  run_mutation_check m2 "fixtures: shortpayload counted the truncated walk" || MUTATION_FAIL=1
  restore_and_verify m2

  printf '%s\n@@INJECT@@\n%s\n' \
'  {0, 32, 36, 72, 76, 108, 109, 132},' \
'  {4, 32, 36, 72, 76, 108, 109, 132}, // MUTATION m3: name read four bytes late.' \
    > "$OUT/mutation/m3-anchor.txt"
  apply_patch m3 native/src/demo_header_strtab.cpp
  run_mutation_check m3 "fixtures: good rows are the two players that were written" || MUTATION_FAIL=1
  restore_and_verify m3

  printf '%s\n@@INJECT@@\n%s\n' \
'  decoded.entityIndex = entryIndex + 1u;' \
'  decoded.entityIndex = entryIndex; // MUTATION m4: slot and entity confused.' \
    > "$OUT/mutation/m4-anchor.txt"
  apply_patch m4 native/src/demo_header_strtab.cpp
  run_mutation_check m4 "fixtures: good rows are the two players that were written" || MUTATION_FAIL=1
  restore_and_verify m4

  # m5: the entry text is compared with the neighbouring slot, so every real
  # table row is reported as a misread slot. The claim under test is that the
  # text really is this entry's slot number, not merely "some number".
  printf '%s\n@@INJECT@@\n%s\n' \
'    if (!tableText.empty() && tableText != std::to_string(entryIndex)) {' \
'    if (!tableText.empty() && tableText != std::to_string(entryIndex + 1u)) { // MUTATION m5: text compared with the wrong slot.' \
    > "$OUT/mutation/m5-anchor.txt"
  apply_patch m5 native/src/demo_header_strtab.cpp
  run_mutation_check m5 "fixtures: good entry text matches its slot" || MUTATION_FAIL=1
  restore_and_verify m5

  # m6: the tick filter is dropped, so a query at tick 2000 -- before the table
  # exists -- answers with the player's final name. The claim under test is that
  # a name is answered for the tick it was asked about.
  printf '%s\n@@INJECT@@\n%s\n' \
'    if (candidate.tick < 0 || candidate.tick > tick) continue;' \
'    if (candidate.tick < 0) continue; // MUTATION m6: the tick query ignores the tick.' \
    > "$OUT/mutation/m6-anchor.txt"
  apply_patch m6 native/src/demo_header_strtab.cpp
  run_mutation_check m6 "a tick before the userinfo table resolves no name" ticktest || MUTATION_FAIL=1
  restore_and_verify m6

  # m7: the create-table arm is removed, so the table is never registered. This
  # is the "decoder is not wired at all" shape; it must not look like "the demo
  # has no userinfo table", which the gate treats as a missing input.
  printf '%s\n@@INJECT@@\n%s\n' \
'  if (name == "userinfo") {
    if (summary.userInfoTableId != 0xffffffffu) ++summary.userInfoTableResetCount;' \
'  if (name == "userinfo-mutation-never-taken") { // MUTATION m7: the userinfo path is not wired.
    if (summary.userInfoTableId != 0xffffffffu) ++summary.userInfoTableResetCount;' \
    > "$OUT/mutation/m7-anchor.txt"
  apply_patch m7 native/src/demo_header_strtab.cpp
  run_mutation_check m7 "fixtures: good entries" || MUTATION_FAIL=1
  restore_and_verify m7

  # The per-mutant restore check is the quick half; this is the full gate on the
  # restored tree, so the run does not end with only fixture-level evidence.
  echo
  echo "=== restored tree, full gate ==="
  bash userinfo-check.sh > "$OUT/mutation/restored-full.txt" 2>&1
  FULL_RC=$?
  echo "restored full gate rc=$FULL_RC asserts=$(grep -c '^ok ' "$OUT/mutation/restored-full.txt") fails=$(grep -c '^FAIL ' "$OUT/mutation/restored-full.txt")"
  if [ "$FULL_RC" -eq 0 ]; then
    echo "BASELINE-GREEN-FULL=1"
  else
    echo "BASELINE-GREEN-FULL=0"
    grep -F "FAIL " "$OUT/mutation/restored-full.txt" | head -5 | sed 's/^/    /'
    MUTATION_FAIL=1
  fi
fi

echo
printf 'ASSERTIONS=%d\n' "$asserts"
if [ "$fail" -ne 0 ] || [ "$MUTATION_FAIL" -ne 0 ]; then
  printf 'RESULT=FAIL\n'
  exit 1
fi
printf 'RESULT=PASS\n'
exit 0
