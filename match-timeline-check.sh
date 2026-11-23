#!/usr/bin/env bash
# I6 gate (2026-11-23): the per-round timeline.
#
# What it pins:
#   1. the probe's timeline arithmetic -- rounds come from teamplay_round_start,
#      every kill lands in exactly one round or in "outside", and every anomaly is
#      counted exactly once;
#   2. the tick-space conversion (server -> playback) against a measured offset;
#   3. determinism: two probe runs are byte-identical;
#   4. the product publishes the same timeline at its startup analysis, WITHOUT the
#      kill rows (a thousand kills must not become a thousand trace lines);
#   5. a second map still produces a valid timeline.
set -uo pipefail

cd "$(dirname "$0")" || exit 1
export LC_ALL=C
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/demo_open_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
DEMO2="D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem"
OUT=".scratch/match-timeline"

fail=0
mkdir -p "$OUT"
win() { cygpath -m "$1"; }   # never prefix $PWD: an absolute /d/... path stays usable

assert_eq() { # <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"
    fail=1
  fi
}
assert_gt() { # <label> <actual> <floor>
  if [ "${2:-x}" -gt "$3" ] 2>/dev/null; then
    echo "  OK   $1 = $2 (> $3)"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected > $3"
    fail=1
  fi
}
sfield() { # <line> <field>
  printf '%s\n' "$1" | grep -oE "$2=[^ ]*" | tail -1 | cut -d= -f2
}

for path in "$TF" "$DEMO" "$DEMO2"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

echo "=== 0/5 build ==="
TF2_IDENTITY_ALLOW_UNTRACKED=1 bash build-target.sh demo_open_probe tf2_demo_native > "$OUT/build.log" 2>&1
assert_eq "build rc" "$?" "0"
if [ "$fail" != "0" ]; then echo "MATCH-TIMELINE=FAIL"; exit 1; fi
if grep -aqE "warning C[0-9]{4}|error C[0-9]{4}" "$OUT/build.log"; then
  echo "  FAIL the build printed a diagnostic"
  grep -aE "warning C[0-9]{4}|error C[0-9]{4}" "$OUT/build.log" | head -5
  fail=1
else
  echo "  OK   build log has no diagnostic"
fi

echo "=== 1/5 the probe's timeline on cp_snakewater ==="
timeout 420 "$PROBE" --scan --motion-detect --match-timeline --kills "$(win "$DEMO")" > "$OUT/probe.txt" 2>&1
assert_eq "probe rc" "$?" "0"
# The probe writes CRLF and the row assertions anchor on $, so normalise in place (after
# the rc assert, which has to read the very next command's status).
tr -d "\r" < "$OUT/probe.txt" > "$OUT/probe.lf" && mv "$OUT/probe.lf" "$OUT/probe.txt"
headline=$(grep -a "match-timeline valid=1" "$OUT/probe.txt" | tail -1)
echo "  ..   $headline"
rounds=$(sfield "$headline" rounds)
starts=$(sfield "$headline" starts)
kills=$(sfield "$headline" kills)
outside=$(sfield "$headline" outsideKills)
anomalies=$(sfield "$headline" anomalies)
outside_an=$(sfield "$headline" outsideAnomalies)
kill_lines=$(sfield "$headline" killLines)
kill_omitted=$(sfield "$headline" killLinesOmitted)
offset=$(sfield "$headline" offset)
assert_gt "rounds" "$rounds" "1"
assert_eq "rounds == starts" "$rounds" "$starts"
assert_gt "kills" "$kills" "0"
assert_eq "server-to-playback offset" "$offset" "10319"
assert_eq "killLines + omitted == kills" "$((kill_lines + kill_omitted))" "$kills"
assert_gt "printed kill rows" "$kill_lines" "0"

sum_round_kills=$(awk '/^match-round /{ for (i = 1; i <= NF; ++i) { split($i, kv, "="); if (kv[1] == "kills") total += kv[2] } } END { print total + 0 }' "$OUT/probe.txt")
assert_eq "sum(round kills) == kills - outside" "$sum_round_kills" "$((kills - outside))"
sum_round_an=$(awk '/^match-round /{ for (i = 1; i <= NF; ++i) { split($i, kv, "="); if (kv[1] == "stalls" || kv[1] == "teleports") total += kv[2] } } END { print total + 0 }' "$OUT/probe.txt")
assert_eq "sum(round anomalies) == anomalies - outside" "$sum_round_an" "$((anomalies - outside_an))"
sum_round_hints=$(awk '/^match-round /{ for (i = 1; i <= NF; ++i) { split($i, kv, "="); if (kv[1] == "sharpTurns") total += kv[2] } } END { print total + 0 }' "$OUT/probe.txt")
hints=$(sfield "$headline" hints)
outside_hints=$(sfield "$headline" outsideHints)
assert_gt "hints" "$hints" "0"
assert_eq "sum(round hints) == hints - outside" "$sum_round_hints" "$((hints - outside_hints))"
# The timeline must count the SAME anomalies the detector printed, not a second run's.
assert_eq "anomalies == detector rows" "$(grep -ac "^motion-anomaly " "$OUT/probe.txt")" "$anomalies"
assert_eq "hints == detector hint rows" "$(grep -ac "^motion-hint " "$OUT/probe.txt")" "$hints"
malformed=$(awk '/^match-round /{ for (i = 1; i <= NF; ++i) { split($i, kv, "="); if (kv[1] == "start") start = kv[2]; if (kv[1] == "end") end = kv[2] } if (start == "") bad = 1; if (end < start) bad = 1; if (seen && start <= prev) bad = 1; prev = start; seen = 1 } END { print bad + 0 }' "$OUT/probe.txt")
assert_eq "round starts increasing, end >= start" "$malformed" "0"
first_round=$(grep -ac "^match-round index=0 " "$OUT/probe.txt")
assert_eq "round 0 is present" "$first_round" "1"
bad_kill=$(grep -a "^match-kill " "$OUT/probe.txt" | grep -avcE "^match-kill tick=[0-9]+ playback=-?[0-9]+ round=-?[0-9]+ victim=-?[0-9]+ victimUserid=-?[0-9]+ victimName=[A-Za-z0-9._~%-]* attacker=-?[0-9]+ attackerTeam=-?[0-9]+ attackerName=[A-Za-z0-9._~%-]* weapon=[A-Za-z0-9._~%-]*$")
assert_eq "kill rows all well formed" "$bad_kill" "0"
# The timeline colours RED kills above the BLU ones, so the team must actually resolve;
# a demo whose kills all came back unknown would draw a flat single-sided timeline.
team_known=$(sfield "$headline" killsWithTeam)
assert_gt "kills with a resolved team" "$team_known" "0"
# -1 is legitimate here: the recording need not carry every killer's entity, and the
# timeline is supposed to show that as unknown rather than pick a side. So the rule is
# "2, 3 or -1" -- anything else is a bug -- and the resolved count must equal the header.
team_rows=$(grep -a "^match-kill " "$OUT/probe.txt")
assert_eq "team is RED, BLU or unknown" "$(printf '%s\n' "$team_rows" | sed -e 's/.*attackerTeam=\(-\{0,1\}[0-9]*\) .*/\1/' | grep -vc '^-1$\|^2$\|^3$')" "0"
assert_eq "resolved team rows == killsWithTeam" "$(printf '%s\n' "$team_rows" | grep -ac "attackerTeam=2 \|attackerTeam=3 ")" "$team_known"

echo "=== 2/5 determinism ==="
timeout 420 "$PROBE" --scan --motion-detect --match-timeline --kills "$(win "$DEMO")" > "$OUT/probe2.txt" 2>&1
tr -d "\r" < "$OUT/probe2.txt" > "$OUT/probe2.lf" && mv "$OUT/probe2.lf" "$OUT/probe2.txt"
assert_eq "probe rerun rc" "$?" "0"
if cmp -s "$OUT/probe.txt" "$OUT/probe2.txt"; then
  echo "  OK   two probe runs are byte-identical"
else
  echo "  FAIL two probe runs differ"
  fail=1
fi

echo "=== 3/5 the product's startup analysis ==="
TF2_NATIVE_TRACE="$(win "$OUT/product.trace")" timeout 600 "$EXE" \
  --tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled --playback-speed 8 \
  --settings-file "$(win "$OUT/product.ini")" --ui-settings-file "$(win "$OUT/product-ui.ini")" \
  --analysis-scan --capture-tick 1200 --capture-frame "$(win "$OUT/product.bmp")" \
  > "$OUT/product.log" 2>&1
assert_eq "product rc" "$?" "0"
tr -d "\r" < "$OUT/product.trace" > "$OUT/product.trace.lf"
pcount=$(grep -ac "match-timeline valid=1" "$OUT/product.trace.lf")
assert_eq "product printed the timeline once" "$pcount" "1"
phead=$(grep -a "match-timeline valid=1" "$OUT/product.trace.lf" | tail -1)
echo "  ..   $phead"
assert_eq "product rounds == probe rounds" "$(sfield "$phead" rounds)" "$rounds"
assert_eq "product kills == probe kills" "$(sfield "$phead" kills)" "$kills"
assert_eq "product offset == probe offset" "$(sfield "$phead" offset)" "$offset"
assert_eq "product printed no kill rows" "$(grep -ac "^match-kill \| match-kill " "$OUT/product.trace.lf")" "0"
assert_eq "product round summary rows" "$(grep -ac "analysis: round index=" "$OUT/product.trace.lf")" "$rounds"

echo "=== 4/5 a second map ==="
timeout 420 "$PROBE" --scan --motion-detect --match-timeline "$(win "$DEMO2")" > "$OUT/probe-upward.txt" 2>&1
tr -d "\r" < "$OUT/probe-upward.txt" > "$OUT/probe-upward.lf" && mv "$OUT/probe-upward.lf" "$OUT/probe-upward.txt"
assert_eq "upward probe rc" "$?" "0"
head2=$(grep -a "match-timeline valid=1" "$OUT/probe-upward.txt" | tail -1)
echo "  ..   $head2"
assert_gt "upward rounds" "$(sfield "$head2" rounds)" "0"
assert_gt "upward kills" "$(sfield "$head2" kills)" "0"
assert_eq "upward printed no kill rows" "$(grep -ac "^match-kill " "$OUT/probe-upward.txt")" "0"
# --match-timeline alone must bring the detector's input with it: without the census the
# counts silently read zero on a map that really does carry anomalies.
assert_gt "upward anomalies" "$(sfield "$head2" anomalies)" "0"
assert_eq "upward anomalies == detector rows" "$(grep -ac "^motion-anomaly " "$OUT/probe-upward.txt")" "$(sfield "$head2" anomalies)"

echo "=== 5/5 the switch stays off unless asked ==="
# The timeline is opt-in: a run that does not pass --match-timeline must print not one
# timeline line, so every existing consumer of the scan output stays byte-identical.
timeout 420 "$PROBE" --scan --motion-detect "$(win "$DEMO")" > "$OUT/noflag.txt" 2>&1
assert_eq "probe without the flag rc" "$?" "0"
assert_eq "  .. prints no timeline line" "$(grep -ac "match-timeline\|match-round\|match-kill" "$OUT/noflag.txt")" "0"

if [ "$fail" = "0" ]; then echo "MATCH-TIMELINE=PASS"; exit 0; fi
echo "MATCH-TIMELINE=FAIL"
exit 1
