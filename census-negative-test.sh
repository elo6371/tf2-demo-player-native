#!/usr/bin/env bash
# census-negative-test.sh -- prove CORPUS-CENSUS can fail.
#
# Why: the first version of corpus-census.py parsed only the `header=` line, so
# every counter came back None, the "must be zero" loop skipped them, and 24
# demos printed CORPUS-CENSUS=PASS while checking nothing. A verdict that cannot
# go red is not evidence. This script mutates the raw probe reports (no probe
# re-run, so it costs seconds) and asserts the verdict flips to FAIL.
#
# Why the input set is pinned (2026-10-08): this used to run `--sample 24` over
# the live Steam demos directory. The corpus grew (1643 -> 1656 files, two
# recordings the machine made while this was being worked on), the evenly spaced
# sample moved to neighbouring files, and 23 of the 24 frozen reports stopped
# being read by the census -- so every mutation below would have been inert and
# the suite went red for the right reason. A gate whose input set is a property
# of the machine cannot be re-run, so the set is pinned by name in
# evidence/corpus-calib/demos.txt and asserted to match the frozen reports.
#
# Usage: bash census-negative-test.sh
set -uo pipefail

cd "$(dirname "$0")"
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
DEMOS="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"
SRC=evidence/corpus-calib
LIST="$SRC/demos.txt"        # the pinned input set: one demo file name per line
NEG=evidence/corpus-negative

fail=0
step() { printf '\n=== %s ===\n' "$1"; }
ok()   { printf '  OK   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }

run() { # run <outdir>  -> echoes verdict
  "$PY" corpus-census.py --demos-dir "$DEMOS" --demos-list "$LIST" --outdir "$1" --resume 2>&1
}

step "prepare: copy raw reports to $NEG"
# Clear every file, not just *.txt: a leftover from an earlier run would be
# included in the digest below and read as a failed restore. Strays are moved
# out rather than deleted: this sandbox charges about five seconds per file for
# a bulk delete (24 files measured at 2m02s) while a rename is instant, and the
# point of this suite is that it costs seconds. Steady state is zero strays, and
# the quarantine is under .scratch/ (gitignored), not under evidence/.
mkdir -p "$NEG/reports" .scratch/corpus-negative-strays
for f in "$NEG"/reports/*; do
  [ -e "$f" ] || continue
  [ -f "$SRC/reports/$(basename "$f")" ] || mv -f "$f" .scratch/corpus-negative-strays/
done
cp "$SRC"/reports/*.txt "$NEG/reports/"
# The digest is taken over exactly the copied files: globbing the corpus
# directory afterwards would compare a different set and report a bogus drift.
mapfile -t SAMPLED < <(cd "$NEG/reports" && ls *.txt | sort)
digest() { for f in "${SAMPLED[@]}"; do cat "$NEG/reports/$f"; done | sha256sum | cut -d' ' -f1; }
BEFORE=$(digest)
echo "  reports=${#SAMPLED[@]}  digest=$BEFORE"

# The pinned list and the frozen reports must describe the same set. A name in
# the list without a report would be scanned fresh (slow, and the mutation
# target could move); a report without a name would sit unread. Assert the
# correspondence here, so a re-calibration that misses one side goes red at the
# top instead of surfacing as a mysterious inert mutation below.
if [ ! -f "$LIST" ]; then
  bad "pinned demo list is missing: $LIST"
else
  PINNED=$(grep -cv '^[[:space:]]*\(#\|$\)' "$LIST" || true)
  if [ "$PINNED" -eq "${#SAMPLED[@]}" ]; then
    ok "pinned list and frozen reports agree: $PINNED demos"
  else
    bad "pinned list has $PINNED demos, frozen reports ${#SAMPLED[@]}"
  fi
  ORPHANS=0
  while IFS= read -r name; do
    # demos.txt is tracked, so with core.autocrlf=true (the system gitconfig on
    # this machine, and the Git-for-Windows default) a fresh checkout gives it
    # CRLF. bash's `read` does not strip the CR, so every name arrived as
    # "73.dem\r": both lookups below -- $SRC/reports/73.dem\r.txt and
    # $DEMOS/73.dem\r -- then missed, and all 24 demos were reported as having no
    # frozen report and not being in the corpus on a fresh checkout. sed and awk
    # here open files in text mode and hide the CR, which is why the same file
    # reads correctly everywhere else; `read` is the one that does not.
    name=${name%$'\r'}
    case "$name" in ''|'#'*) continue ;; esac
    if [ ! -f "$SRC/reports/${name%.dem}.txt" ]; then
      bad "pinned demo has no frozen report: $name"; ORPHANS=1
    fi
    if [ ! -f "$DEMOS/$name" ]; then
      bad "pinned demo is not in the corpus: $name"; ORPHANS=1
    fi
  done < "$LIST"
  if [ "$ORPHANS" -eq 0 ]; then
    ok "every pinned demo exists and has a frozen report"
  fi
fi

step "baseline: unmodified reports must PASS"
OUT=$(run "$NEG")
echo "$OUT" | grep -E '^(clean|dirty|skipped|sum_packets)=' | sed 's/^/  /'
if grep -q '^CORPUS-CENSUS=PASS$' <<<"$OUT"; then ok "baseline PASS"; else bad "baseline not PASS"; fi
# The baseline skip count is *not* assumed to be zero: seven of the corpus demos
# are truncated-tail recordings (TF2 writes the header at record start and only
# backfills it on dem_stop), and one could legitimately be in any given set.
# Mutation I below must therefore be judged by the *increase* it causes, not by
# a hard-coded total: the previous version asserted `skipped=1` and went red the
# first time the live corpus grew enough for a truncated demo to land in the
# sample.
BASE_SKIPPED=$(sed -n 's/^skipped=//p' <<<"$OUT" | head -1)
if [[ "$BASE_SKIPPED" =~ ^[0-9]+$ ]]; then
  ok "baseline skipped=$BASE_SKIPPED is a number"
else
  bad "baseline skipped is not a number (got '${BASE_SKIPPED}')"
  BASE_SKIPPED=0
fi
# Informational: with a pinned list this is expected to be 0 -- the reports
# directory starts clean and only the pinned 24 are copied in.
EXTRA=$(cd "$NEG/reports" && ls *.txt | sort | comm -13 <(printf '%s\n' "${SAMPLED[@]}") - | wc -l)
echo "  extra_reports=$EXTRA (pinned list: expected 0)"

VICTIM=$(ls "$NEG"/reports/autorecord_*.txt | head -1)
echo "  victim=$VICTIM"

# The victim must be a demo the census actually read. That is now structural --
# every report in this directory is on the pinned list -- but membership is
# still asserted: on 2026-10-08 this very check caught the live sample moving
# out from under the frozen reports, and a structural argument that is never
# checked is how that class of failure comes back. corpus.csv lists exactly the
# rows the run consumed; it names demos with their .dem extension while the raw
# report file is <stem>.txt, so both spellings are accepted.
VICTIM_DEMO=$(basename "$VICTIM" .txt)
if cut -d, -f1 "$NEG/corpus.csv" | grep -qxF -e "$VICTIM_DEMO" -e "$VICTIM_DEMO.dem"; then
  ok "victim $VICTIM_DEMO is in the scanned set"
else
  bad "victim $VICTIM_DEMO is NOT in corpus.csv -- the mutations below would be inert"
fi
# ...and it must not be one of the truncated-tail demos either: those are filed
# as skipped, so mutating a counter inside one changes a report the verdict never
# reads, and mutations A-H would pass by doing nothing.
if grep -q '^index_state=ok' "$VICTIM"; then
  ok "victim is a clean report (its counters are actually read)"
else
  bad "victim $VICTIM_DEMO is not index_state=ok -- mutations A-H would be inert"
fi

step "mutation A: entity_failures 0 -> 3 (must FAIL)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/entity_failures=0/entity_failures=3/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'entity_failures=3' <<<"$OUT"; then ok "failure names the counter"; else bad "failure does not name the counter"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation B: remove malformed_packets entirely (must FAIL as missing:)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/ malformed_packets=0//' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'missing:.*malformed_packets' <<<"$OUT"; then ok "reported as missing, not skipped"; else bad "missing field was not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation C: zero the packet count (must FAIL as probe_decoded_nothing)"
# The indexer produced entries and the scan still decoded zero packets. That is a
# probe failure and must never read as "nothing was wrong". (This assertion was
# stale for one run: the census renamed the reason from no_packets_scanned to
# probe_decoded_nothing when the three-state split landed.)
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/packets_scanned=[0-9]*/packets_scanned=0/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'probe_decoded_nothing' <<<"$OUT"; then ok "empty decode detected"; else bad "empty decode not detected"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation D: POV classifier disagreement (must FAIL)"
# Pretend the header classifier mislabelled an autorecord_ file. The ground-truth
# check must catch it even though every counter is still zero.
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/recording=POV/recording=SourceTV/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'POV CLASSIFIER DISAGREEMENTS' <<<"$OUT"; then ok "disagreement reported"; else bad "disagreement not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation E: in-stream HLTV bit set but classifier still says POV (must FAIL)"
# This is the failure the stream-level fields exist to catch: an svc_ServerInfo
# that declares HLTV while the classifier keeps calling the demo POV.
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/server_info_hltv=0/server_info_hltv=1/; s/source_tv_flag=0/source_tv_flag=1/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'hltv_bit_ignored' <<<"$OUT"; then ok "reported as hltv_bit_ignored"; else bad "hltv bit not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation F: autorecord_ file reclassified as SourceTV on the stream path (must FAIL)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/recording_stream=POV/recording_stream=SourceTV/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'pov_ground_truth_violated' <<<"$OUT"; then ok "reported as pov_ground_truth_violated"; else bad "ground-truth violation not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation G: source_tv_flag disagrees with the HLTV bit (must FAIL)"
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/source_tv_flag=0/source_tv_flag=1/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q 'source_tv_flag=1' <<<"$OUT"; then ok "inconsistent flag reported"; else bad "inconsistent flag not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation H: index_state=invalid must FAIL, not be filed as truncated"
# index_state=invalid means the indexer walked into something it does not
# understand. Folding that into the truncated-tail skip would turn a real failure
# into a silent pass, so this case pins the distinction.
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/^index_state=ok/index_state=invalid/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'index_state=invalid' <<<"$OUT"; then ok "reported as invalid, not skipped"; else bad "invalid index not reported"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation I: index_state=truncated_tail must SKIP, not FAIL and not clean"
# index=0 is what the probe actually reports for a truncated file (the indexer
# returned false) even though index_entries is non-zero. The field sits mid-line
# in `header=1 index=1 scan=1 ...`, so match it with surrounding spaces.
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/^index_state=ok/index_state=truncated_tail/; s/ index=1 / index=0 /' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=PASS$' <<<"$OUT"; then ok "verdict stayed green (a skip is not a failure)"; else bad "verdict went red"; fi
MUT_SKIPPED=$(sed -n 's/^skipped=//p' <<<"$OUT" | head -1)
EXPECT_SKIPPED=$((BASE_SKIPPED + 1))
if [ "$MUT_SKIPPED" = "$EXPECT_SKIPPED" ]; then
  ok "counted as skipped ($BASE_SKIPPED -> $MUT_SKIPPED)"
else
  bad "skip count did not rise by one: baseline=$BASE_SKIPPED mutated=${MUT_SKIPPED:-<absent>} expected=$EXPECT_SKIPPED"
fi
if grep -q 'truncated_tail_files=' <<<"$OUT"; then ok "named in truncated_tail_files"; else bad "not named"; fi
mv "$VICTIM.orig" "$VICTIM"

step "mutation J: truncated_tail with a non-zero counter must still FAIL"
# The scanned prefix of a truncated file must be as clean as a complete file.
cp "$VICTIM" "$VICTIM.orig"
sed -i 's/^index_state=ok/index_state=truncated_tail/; s/ index=1 / index=0 /; s/entity_failures=0/entity_failures=2/' "$VICTIM"
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=FAIL$' <<<"$OUT"; then ok "verdict went red"; else bad "verdict stayed green"; fi
if grep -q 'entity_failures=2' <<<"$OUT"; then ok "names the counter"; else bad "does not name the counter"; fi
mv "$VICTIM.orig" "$VICTIM"

step "restore: reports must be byte-identical and PASS again"
# Digest the snapshot taken at the start, not a fresh glob: the corpus is live and
# the sample can move underneath us between runs.
AFTER=$(digest)
if [ "$BEFORE" = "$AFTER" ]; then ok "digest unchanged ($AFTER)"; else bad "digest drifted: $BEFORE -> $AFTER"; fi
OUT=$(run "$NEG")
if grep -q '^CORPUS-CENSUS=PASS$' <<<"$OUT"; then ok "restored PASS"; else bad "restored run not PASS"; fi

printf '\n'
if [ "$fail" -eq 0 ]; then echo "CENSUS-NEGATIVE=PASS"; else echo "CENSUS-NEGATIVE=FAIL"; fi
exit "$fail"
