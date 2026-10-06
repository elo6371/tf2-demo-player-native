#!/usr/bin/env bash
# mutate.sh -- prove the acceptance readings can go red.
#
# SOUL.md rule 1: a criterion that has never been seen to fail is not evidence.
# Each case reintroduces exactly one defect the P0 fix removed (or one
# equivalent), rebuilds, runs the affected reading, and asserts the reading
# changes. Then it restores the tree with `git checkout` and rebuilds clean.
#
# The tree MUST be committed before running this, because `git checkout --` is
# the restore mechanism.
#
# Usage: bash mutate.sh [m1 m2 m3 m4 m5]
set -uo pipefail
cd "$(dirname "$0")"

SRC=native/src/demo_header.cpp
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
PROTO23="D:/TF2_Demo_Player/.scratch/tf2-demo-parser/test_data/protocol23.dem"
PROBE=native/build-nmake/entity_protocol_probe.exe
FIXPROBE=native/build-nmake/entity_message_fixture_probe.exe
OUT=evidence/mutation
mkdir -p "$OUT"

CASES="${*:-m1 m2 m3 m4 m5}"
rc_all=0

if ! git diff --quiet -- native/; then
  echo "refusing to run: native/ has uncommitted changes, git checkout would lose them" >&2
  exit 2
fi

patch_in() { # patch_in <file> <find> <replace>
  python - "$1" "$2" "$3" <<'PY'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
data = io.open(path, 'r', encoding='utf-8', newline='').read()
if find not in data:
    sys.exit('PATTERN NOT FOUND: ' + find[:70])
data = data.replace(find, repl, 1)
io.open(path, 'w', encoding='utf-8', newline='').write(data)
PY
}

restore() {
  git checkout -- native/ >/dev/null 2>&1
  bash build-target.sh entity_protocol_probe entity_message_fixture_probe >/dev/null 2>&1
}

report() { # report <label> <file> <regex> <expected>
  local label="$1" file="$2" regex="$3" want="$4" got
  got="$(grep -oE "$regex" "$file" | head -1)"
  if [ "$got" = "$want" ]; then
    printf 'MUTATION-RED   %-52s %s\n' "$label" "$got"
  else
    printf 'MUTATION-GREEN %-52s got=%s want=%s\n' "$label" "${got:-<none>}" "$want"
    rc_all=1
  fi
}

echo "=== fixed-build readings (the values every mutation must move away from) ==="
"$PROBE" "$BAGEL"   > "$OUT/bagel.fixed.txt"   2>&1
"$PROBE" "$PROTO23" > "$OUT/proto23.fixed.txt" 2>&1
"$FIXPROBE"         > "$OUT/fixture.fixed.txt" 2>&1
grep -oE 'entity_failures=[0-9]+' "$OUT/bagel.fixed.txt" | head -1
grep -oE 'malformed_packets=[0-9]+' "$OUT/proto23.fixed.txt" | head -1
grep -oE 'fixture_failures=[0-9]+' "$OUT/fixture.fixed.txt"

for case_id in $CASES; do
  echo
  case "$case_id" in
    m1)
      echo "--- m1: drop the svc_SetPause(11) dispatch arm"
      patch_in "$SRC" \
        '    else if (type == 11) { if (!readSetPause(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }' \
        '    else if (type == 11) { result.packetValid = false; }' || { rc_all=1; continue; }
      bash build-target.sh entity_protocol_probe >/dev/null 2>&1
      "$PROBE" "$BAGEL" > "$OUT/bagel.m1.txt" 2>&1
      report "m1 svc_SetPause removed -> unknown types return" "$OUT/bagel.m1.txt" \
        'unknown_message_types=[^ ]*' 'unknown_message_types=11,11'
      report "m1 svc_SetPause removed -> entity_failures return" "$OUT/bagel.m1.txt" \
        'entity_failures=[0-9]+' 'entity_failures=6581'
      ;;
    m2)
      echo "--- m2: restore the 1024-byte string-table user-data cap"
      patch_in "$SRC" \
        '        if (static_cast<std::size_t>(userBytes) * 8u > bits.remaining()) return false;' \
        '        if (userBytes > 1024u) return false;' || { rc_all=1; continue; }
      bash build-target.sh entity_protocol_probe >/dev/null 2>&1
      "$PROBE" "$BAGEL" > "$OUT/bagel.m2.txt" 2>&1
      report "m2 1024 cap restored -> instance baselines collapse" "$OUT/bagel.m2.txt" \
        'instance_baselines=[0-9]+' 'instance_baselines=6'
      report "m2 1024 cap restored -> malformed_packets return" "$OUT/bagel.m2.txt" \
        'malformed_packets=[0-9]+' 'malformed_packets=3'
      ;;
    m3)
      echo "--- m3: revert svc_Prefetch to the >23 width rule"
      patch_in "$SRC" \
        '  const std::uint32_t width = summary.networkProtocol > 22 ? 14u : 13u;' \
        '  const std::uint32_t width = summary.networkProtocol > 23 ? 14u : 13u;' || { rc_all=1; continue; }
      bash build-target.sh entity_protocol_probe >/dev/null 2>&1
      "$PROBE" "$PROTO23" > "$OUT/proto23.m3.txt" 2>&1
      report "m3 prefetch width reverted -> malformed_packets return" "$OUT/proto23.m3.txt" \
        'malformed_packets=[0-9]+' 'malformed_packets=1703'
      report "m3 prefetch width reverted -> unknown_message_packets return" "$OUT/proto23.m3.txt" \
        'unknown_message_packets=[0-9]+' 'unknown_message_packets=1189'
      ;;
    m4)
      echo "--- m4: make Preserve history events carry a full state again"
      patch_in "$SRC" \
        '                               false, {}, std::move(changes)});' \
        '                               true, summary.entityStates[static_cast<std::size_t>(lastEntity)], std::move(changes)});' || { rc_all=1; continue; }
      bash build-target.sh entity_message_fixture_probe >/dev/null 2>&1
      "$FIXPROBE" > "$OUT/fixture.m4.txt" 2>&1
      report "m4 full-state preserve events -> delta assertion fails" "$OUT/fixture.m4.txt" \
        'FAIL history: preserve event carries one property delta' \
        'FAIL history: preserve event carries one property delta'
      ;;
    m5)
      echo "--- m5: drop the base state on the preserve path (no swap-back)"
      patch_in "$SRC" \
        '      EntityState candidate;
      std::swap(candidate, liveState);
      std::vector<EntityPropChange> changes;' \
        '      EntityState candidate;
      std::vector<EntityPropChange> changes;' || { rc_all=1; continue; }
      bash build-target.sh entity_message_fixture_probe >/dev/null 2>&1
      "$FIXPROBE" > "$OUT/fixture.m5.txt" 2>&1
      report "m5 preserve loses the base -> untouched prop fails" "$OUT/fixture.m5.txt" \
        'FAIL delta preserve: untouched m_iTeamNum kept at 200' \
        'FAIL delta preserve: untouched m_iTeamNum kept at 200'
      ;;
    *) echo "unknown case $case_id"; rc_all=1 ;;
  esac
  restore
done

echo
echo "=== restored tree: readings must be identical to the fixed build ==="
"$PROBE" "$BAGEL"   > "$OUT/bagel.restored.txt"   2>&1
"$PROBE" "$PROTO23" > "$OUT/proto23.restored.txt" 2>&1
"$FIXPROBE"         > "$OUT/fixture.restored.txt" 2>&1
for pair in "bagel.fixed.txt bagel.restored.txt" "proto23.fixed.txt proto23.restored.txt" "fixture.fixed.txt fixture.restored.txt"; do
  set -- $pair
  if diff -q "$OUT/$1" "$OUT/$2" >/dev/null; then
    echo "RESTORED-IDENTICAL $1"
  else
    echo "RESTORED-DIFFERS  $1"; diff "$OUT/$1" "$OUT/$2" | head -20; rc_all=1
  fi
done

echo
if [ "$rc_all" -eq 0 ]; then echo "MUTATION-SUITE=PASS"; else echo "MUTATION-SUITE=FAIL"; fi
exit "$rc_all"
