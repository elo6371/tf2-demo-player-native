#!/usr/bin/env bash
# mutate.sh -- prove the acceptance readings can go red.
#
# SOUL.md rule 1: a criterion that has never been seen to fail is not evidence.
# Each case reintroduces one defect the P0 fix removed (or an equivalent),
# rebuilds, runs the affected reading, and asserts the reading moved away from
# the fixed-build value. Then it restores the tree with `git checkout` and
# rebuilds clean, and finally checks the restored readings are byte-identical.
#
# The tree MUST be committed first: `git checkout --` is the restore mechanism.
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

# Applies one textual replacement. The source tree is CRLF, so the patterns are
# written with LF and converted before matching.
patch_in() { # patch_in <file> <find> <replace>
  python - "$1" "$2" "$3" <<'PY'
import io, sys
path, find, repl = sys.argv[1], sys.argv[2], sys.argv[3]
raw = io.open(path, 'rb').read()
crlf = b'\r\n' in raw
data = raw.decode('utf-8')
if crlf:
    find = find.replace('\n', '\r\n')
    repl = repl.replace('\n', '\r\n')
if find not in data:
    sys.exit('PATTERN NOT FOUND: ' + find[:70].replace('\r', '\\r').replace('\n', '\\n'))
io.open(path, 'wb').write(data.replace(find, repl, 1).encode('utf-8'))
PY
}

restore() {
  git checkout -- native/ >/dev/null 2>&1
  bash build-target.sh entity_protocol_probe entity_message_fixture_probe >/dev/null 2>&1
}

value() { grep -oE "$2" "$1" | head -1 | cut -d= -f2; }

# A mutation passes only if the reading is no longer the fixed-build value.
must_move() { # must_move <label> <file> <regex> <fixed-value>
  local label="$1" file="$2" regex="$3" fixed="$4" got
  got="$(value "$file" "$regex")"
  if [ -n "$got" ] && [ "$got" != "$fixed" ]; then
    printf 'MUTATION-RED   %-56s %s (fixed=%s)\n' "$label" "$got" "$fixed"
  else
    printf 'MUTATION-GREEN %-56s got=%s fixed=%s\n' "$label" "${got:-<none>}" "$fixed"
    rc_all=1
  fi
}

must_appear() { # must_appear <label> <file> <literal-line>
  local label="$1" file="$2" want="$3"
  if grep -qF -- "$want" "$file"; then
    printf 'MUTATION-RED   %-56s %s\n' "$label" "$want"
  else
    printf 'MUTATION-GREEN %-56s missing: %s\n' "$label" "$want"
    rc_all=1
  fi
}

echo "=== fixed-build readings (the values every mutation must move away from) ==="
"$PROBE" "$BAGEL"   > "$OUT/bagel.fixed.txt"   2>&1
"$PROBE" "$PROTO23" > "$OUT/proto23.fixed.txt" 2>&1
"$FIXPROBE"         > "$OUT/fixture.fixed.txt" 2>&1
echo "bagel:   entity_failures=$(value "$OUT/bagel.fixed.txt" 'entity_failures=[0-9]+') malformed=$(value "$OUT/bagel.fixed.txt" 'malformed_packets=[0-9]+') baselines=$(value "$OUT/bagel.fixed.txt" 'instance_baselines=[0-9]+')"
echo "proto23: malformed=$(value "$OUT/proto23.fixed.txt" 'malformed_packets=[0-9]+') unknown=$(value "$OUT/proto23.fixed.txt" 'unknown_message_packets=[0-9]+')"
echo "fixture: $(grep -oE 'fixture_failures=[0-9]+' "$OUT/fixture.fixed.txt")"

for case_id in $CASES; do
  echo
  case "$case_id" in
    m1)
      # Delete the svc_SetPause arm outright so the message falls through to the
      # unknown-type arm, exactly as it did before the fix.
      echo "--- m1: delete the svc_SetPause(11) dispatch arm"
      patch_in "$SRC" \
        '    else if (type == 11) { if (!readSetPause(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
' '' || { rc_all=1; continue; }
      bash build-target.sh entity_protocol_probe >/dev/null 2>&1
      "$PROBE" "$BAGEL" > "$OUT/bagel.m1.txt" 2>&1
      must_move "m1 svc_SetPause removed -> unknown_message_types" "$OUT/bagel.m1.txt" \
        'unknown_message_types=[^ ]*' '<none>'
      must_move "m1 svc_SetPause removed -> entity_failures" "$OUT/bagel.m1.txt" \
        'entity_failures=[0-9]+' '0'
      must_move "m1 svc_SetPause removed -> malformed_packets" "$OUT/bagel.m1.txt" \
        'malformed_packets=[0-9]+' '0'
      ;;
    m2)
      # Restore the 1024-byte cap at all five string-table user-data sites, which
      # is the state the baseline build was in.
      echo "--- m2: restore the 1024-byte cap at all five string-table sites"
      patch_in "$SRC" \
        '          if (!update.read(14, userBytes)) { ok = false; break; }' \
        '          if (!update.read(14, userBytes) || userBytes > 1024u) { ok = false; break; }' || { rc_all=1; continue; }
      patch_in "$SRC" \
        '        if (!data.read(14, userDataBytes)) return false;' \
        '        if (!data.read(14, userDataBytes) || userDataBytes > 1024u) return false;' || { rc_all=1; continue; }
      patch_in "$SRC" \
        '        if (static_cast<std::size_t>(userBytes) * 8u > bits.remaining()) return false;' \
        '        if (userBytes > 1024u) return false;' || { rc_all=1; continue; }
      patch_in "$SRC" \
        '          if (!bits.skip(static_cast<std::size_t>(userBytes) * 8u)) return false;' \
        '          if (userBytes > 1024u || !bits.skip(static_cast<std::size_t>(userBytes) * 8u)) return false;' || { rc_all=1; continue; }
      patch_in "$SRC" \
        '          if (!table.read(14, userBytes)) { tableOk = false; break; }' \
        '          if (!table.read(14, userBytes) || userBytes > 1024u) { tableOk = false; break; }' || { rc_all=1; continue; }
      bash build-target.sh entity_protocol_probe >/dev/null 2>&1
      "$PROBE" "$BAGEL" > "$OUT/bagel.m2.txt" 2>&1
      must_move "m2 1024 cap restored -> instance_baselines" "$OUT/bagel.m2.txt" \
        'instance_baselines=[0-9]+' '126'
      must_move "m2 1024 cap restored -> malformed_packets" "$OUT/bagel.m2.txt" \
        'malformed_packets=[0-9]+' '0'
      must_move "m2 1024 cap restored -> baseline_misses" "$OUT/bagel.m2.txt" \
        'baseline_misses=[0-9]+' '16'
      ;;
    m3)
      echo "--- m3: revert svc_Prefetch to the >23 width rule"
      patch_in "$SRC" \
        '  const std::uint32_t width = summary.networkProtocol > 22 ? 14u : 13u;' \
        '  const std::uint32_t width = summary.networkProtocol > 23 ? 14u : 13u;' || { rc_all=1; continue; }
      bash build-target.sh entity_protocol_probe >/dev/null 2>&1
      "$PROBE" "$PROTO23" > "$OUT/proto23.m3.txt" 2>&1
      must_move "m3 prefetch width reverted -> malformed_packets" "$OUT/proto23.m3.txt" \
        'malformed_packets=[0-9]+' '0'
      must_move "m3 prefetch width reverted -> unknown_message_packets" "$OUT/proto23.m3.txt" \
        'unknown_message_packets=[0-9]+' '0'
      must_move "m3 prefetch width reverted -> unknown_message_types" "$OUT/proto23.m3.txt" \
        'unknown_message_types=[^ ]*' '<none>'
      ;;
    m4)
      echo "--- m4: make Preserve history events carry a full state again"
      patch_in "$SRC" \
        '                               false, {}, std::move(changes)});' \
        '                               true, summary.entityStates[static_cast<std::size_t>(lastEntity)], std::move(changes)});' || { rc_all=1; continue; }
      bash build-target.sh entity_message_fixture_probe >/dev/null 2>&1
      "$FIXPROBE" > "$OUT/fixture.m4.txt" 2>&1
      must_move "m4 full-state preserve events -> fixture_failures" "$OUT/fixture.m4.txt" \
        'fixture_failures=[0-9]+' '0'
      must_appear "m4 full-state preserve events -> delta event not found" "$OUT/fixture.m4.txt" \
        'FAIL history: preserve event recorded'
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
      must_move "m5 preserve loses the base -> fixture_failures" "$OUT/fixture.m5.txt" \
        'fixture_failures=[0-9]+' '0'
      must_appear "m5 preserve loses the base -> untouched prop fails" "$OUT/fixture.m5.txt" \
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
