#!/usr/bin/env bash
# mutate.sh -- prove the acceptance readings can go red.
#
# SOUL.md rule 1: a criterion that has never been seen to fail is not evidence.
# Each case reintroduces one defect the P0/P1 fixes removed (or an equivalent),
# rebuilds, runs the affected reading, and asserts the reading moved away from
# the fixed-build value. Then it restores the tree with `git checkout` and
# rebuilds clean, and finally checks the restored readings are byte-identical.
#
# The tree MUST be committed first: `git checkout --` is the restore mechanism.
#
# Usage: bash mutate.sh [m1 m2 m3 m4 m5 m6 m7 m8]
set -uo pipefail
cd "$(dirname "$0")"

SRC=native/src/demo_header.cpp
MODELTOOL=native/tools/entity_model_probe.cpp
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
PROTO23="D:/TF2_Demo_Player/.scratch/tf2-demo-parser/test_data/protocol23.dem"
# The P1 cases need a real demo whose entities carry m_nModelIndex and a TF root
# to resolve the paths against. POV rather than SourceTV because it scans in
# seconds instead of minutes and exercises the same create-table path.
POV="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
TFROOT="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
PROBE=native/build-nmake/entity_protocol_probe.exe
FIXPROBE=native/build-nmake/entity_message_fixture_probe.exe
MODELPROBE=native/build-nmake/entity_model_probe.exe
OUT=evidence/mutation
mkdir -p "$OUT"

CASES="${*:-m1 m2 m3 m4 m5 m6 m7 m8}"
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
  # Rebuild every target any case touched, or the next case would read a stale
  # binary left over from the previous mutation.
  bash build-target.sh entity_protocol_probe entity_message_fixture_probe presentation_probe entity_model_probe >/dev/null 2>&1
}

value() { grep -oE "$2" "$1" | head -1 | cut -d= -f2; }

# The entity_model_probe reports JSON, not key=value lines, so it needs its own
# extractor. `value` would cut on '=' and return nothing at all.
jvalue() { grep -oE "\"$2\":[0-9]+" "$1" | head -1 | cut -d: -f2; }

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

# Some readings must NOT move under a given mutation. A mutation that changes
# them means the two signals are not as independent as the design claims.
must_hold() { # must_hold <label> <file> <regex> <expected>
  local label="$1" file="$2" regex="$3" want="$4" got
  got="$(value "$file" "$regex")"
  if [ "$got" = "$want" ]; then
    printf 'MUTATION-HOLD  %-56s %s (unchanged)\n' "$label" "$got"
  else
    printf 'MUTATION-BROKE %-56s got=%s want=%s\n' "$label" "${got:-<none>}" "$want"
    rc_all=1
  fi
}

# JSON counterparts, for entity_model_probe's single-line report.
must_move_j() { # must_move_j <label> <file> <json-key> <fixed-value>
  local label="$1" file="$2" key="$3" fixed="$4" got
  got="$(jvalue "$file" "$key")"
  if [ -n "$got" ] && [ "$got" != "$fixed" ]; then
    printf 'MUTATION-RED   %-56s %s (fixed=%s)\n' "$label" "$got" "$fixed"
  else
    printf 'MUTATION-GREEN %-56s got=%s fixed=%s\n' "$label" "${got:-<none>}" "$fixed"
    rc_all=1
  fi
}

# A mutation that must turn a silent wrong reading into a loud refusal. The exit
# status is the assertion that matters: the defect this guards against exited 0
# while printing a summary that looked entirely reasonable.
must_refuse() { # must_refuse <label> <file> <literal-substring> <exit-code>
  local label="$1" file="$2" want="$3" rc="$4"
  if [ "$rc" -ne 0 ] && grep -qF -- "$want" "$file"; then
    printf 'MUTATION-RED   %-56s exit=%s %s\n' "$label" "$rc" "$want"
  else
    printf 'MUTATION-GREEN %-56s exit=%s missing: %s\n' "$label" "$rc" "$want"
    rc_all=1
  fi
}

echo "=== fixed-build readings (the values every mutation must move away from) ==="
"$PROBE" "$BAGEL"   > "$OUT/bagel.fixed.txt"   2>&1
"$PROBE" "$PROTO23" > "$OUT/proto23.fixed.txt" 2>&1
"$PROBE" "$POV"     > "$OUT/pov.fixed.txt"     2>&1
"$FIXPROBE"         > "$OUT/fixture.fixed.txt" 2>&1
"$MODELPROBE" --tf-root "$TFROOT" --demo "$POV" > "$OUT/model.fixed.txt" 2>&1
./native/build-nmake/presentation_probe.exe > "$OUT/recording.fixed.txt" 2>&1
echo "bagel:   entity_failures=$(value "$OUT/bagel.fixed.txt" 'entity_failures=[0-9]+') malformed=$(value "$OUT/bagel.fixed.txt" 'malformed_packets=[0-9]+') baselines=$(value "$OUT/bagel.fixed.txt" 'instance_baselines=[0-9]+') recording=$(value "$OUT/bagel.fixed.txt" 'recording=[^ ]*')"
echo "proto23: malformed=$(value "$OUT/proto23.fixed.txt" 'malformed_packets=[0-9]+') unknown=$(value "$OUT/proto23.fixed.txt" 'unknown_message_packets=[0-9]+')"
echo "pov:     model_precache=$(value "$OUT/pov.fixed.txt" 'model_precache_entries=[0-9]+') sound_precache=$(value "$OUT/pov.fixed.txt" 'sound_precache_entries=[0-9]+') from_precache=$(value "$OUT/pov.fixed.txt" 'asset_model_path_from_precache=[0-9]+') unresolved=$(value "$OUT/pov.fixed.txt" 'asset_model_index_unresolved=[0-9]+')"
echo "model:   requests=$(jvalue "$OUT/model.fixed.txt" requests) demoRenderable=$(jvalue "$OUT/model.fixed.txt" demoRenderable)"
echo "fixture: $(grep -oE 'fixture_failures=[0-9]+' "$OUT/fixture.fixed.txt")"
echo "recording probe: $(grep -oE '"recording":[a-z]+' "$OUT/recording.fixed.txt")"

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
      # Restore the 1024-byte cap at every string-table user-data site, which is
      # the state the baseline build was in. P1 folded the two soundprecache entry
      # walks (create and update) into walkPrecacheTablePayload, so those two
      # sites are now one line and the count is four, not five. The instancebaseline
      # walk kept its three sites, and those are the ones that carry bagel's
      # 7669-byte entry, so the readings this case asserts are unchanged.
      echo "--- m2: restore the 1024-byte cap at all four string-table sites"
      patch_in "$SRC" \
        '        if (!table.read(14, userBytes)) return false;' \
        '        if (!table.read(14, userBytes) || userBytes > 1024u) return false;' || { rc_all=1; continue; }
      patch_in "$SRC" \
        '        if (!data.read(14, userDataBytes)) return false;' \
        '        if (!data.read(14, userDataBytes) || userDataBytes > 1024u) return false;' || { rc_all=1; continue; }
      patch_in "$SRC" \
        '        if (static_cast<std::size_t>(userBytes) * 8u > bits.remaining()) return false;' \
        '        if (userBytes > 1024u) return false;' || { rc_all=1; continue; }
      patch_in "$SRC" \
        '          if (!bits.skip(static_cast<std::size_t>(userBytes) * 8u)) return false;' \
        '          if (userBytes > 1024u || !bits.skip(static_cast<std::size_t>(userBytes) * 8u)) return false;' || { rc_all=1; continue; }
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
    m6)
      # Put the recording classifier back to reading servername only. This is the
      # state the P0 acceptance run was in when it reported all nine oracle demos
      # as POV; five of them are SourceTV match demos (clientname="SourceTV Demo").
      echo "--- m6: classifier reads servername only again"
      patch_in "$SRC" \
        '  return containsInsensitive(header.serverName, "sourcetv")
      || containsInsensitive(header.serverName, "hltv")
      || containsInsensitive(header.clientName, "sourcetv")
      || containsInsensitive(header.clientName, "hltv");' \
        '  return containsInsensitive(header.serverName, "sourcetv")
      || containsInsensitive(header.serverName, "hltv");' || { rc_all=1; continue; }
      patch_in "$SRC" \
        '  result.headerName = header.recordingType == DemoRecordingType::SourceTv
      || namesIndicateSourceTv(header);' \
        '  result.headerName = header.recordingType == DemoRecordingType::SourceTv;' || { rc_all=1; continue; }
      bash build-target.sh presentation_probe entity_protocol_probe >/dev/null 2>&1
      ./native/build-nmake/presentation_probe.exe > "$OUT/recording.m6.txt" 2>&1
      must_appear "m6 classifier regressed -> presentation_probe recording flag" \
        "$OUT/recording.m6.txt" '"recording":false'
      "$PROBE" "$BAGEL" > "$OUT/bagel.m6.txt" 2>&1
      must_move "m6 classifier regressed -> bagel header verdict" "$OUT/bagel.m6.txt" \
        'recording=[^ ]*' 'SourceTV'
      must_move "m6 classifier regressed -> bagel recording_header_name" "$OUT/bagel.m6.txt" \
        'recording_header_name=[0-9]' '1'
      # This one must NOT move. recording_stream consults svc_ServerInfo's
      # m_bIsHLTV, which is an independent signal from the header names, so a
      # broken header classifier cannot hide a SourceTV demo that carries the
      # bit. Asserting it pins that redundancy instead of assuming it -- the
      # first version of this case wrongly expected it to move and read GREEN.
      must_hold "m6 classifier regressed -> bagel stream verdict stays" "$OUT/bagel.m6.txt" \
        'recording_stream=[A-Za-z]*' 'SourceTV'
      must_hold "m6 classifier regressed -> bagel hltv bit stays" "$OUT/bagel.m6.txt" \
        'server_info_hltv=[0-9]' '1'
      ;;
    m7)
      # Put readCreateStringTable back to handling soundprecache only, which is
      # the state the P1 fix found it in. modelprecache is then never retained,
      # no entity's m_nModelIndex resolves to a path, and the render request list
      # empties out again.
      echo "--- m7: readCreateStringTable stops retaining modelprecache"
      patch_in "$SRC" \
        '  if (name == "soundprecache" || name == "modelprecache") {' \
        '  if (name == "soundprecache") {' || { rc_all=1; continue; }
      bash build-target.sh entity_protocol_probe entity_model_probe >/dev/null 2>&1
      "$PROBE" "$POV" > "$OUT/pov.m7.txt" 2>&1
      "$MODELPROBE" --tf-root "$TFROOT" --demo "$POV" > "$OUT/model.m7.txt" 2>&1
      must_move "m7 modelprecache dropped -> model_precache_entries" "$OUT/pov.m7.txt" \
        'model_precache_entries=[0-9]+' '1248'
      must_move "m7 modelprecache dropped -> asset_model_path_from_precache" "$OUT/pov.m7.txt" \
        'asset_model_path_from_precache=[0-9]+' '387'
      # This is the counter whose whole purpose is to be zero on a healthy demo.
      # With the table gone every in-range index becomes an in-range miss, so
      # this reading is what actually says "the table was doing the work".
      must_move "m7 modelprecache dropped -> asset_model_index_unresolved" "$OUT/pov.m7.txt" \
        'asset_model_index_unresolved=[0-9]+' '0'
      must_move_j "m7 modelprecache dropped -> entity_model_probe requests" "$OUT/model.m7.txt" \
        requests '387'
      # sound_precache_entries must NOT move. P0's acceptance depends on that
      # table decoding identically, and a mutation that moved it would mean the
      # shared entry walk did not preserve the old behaviour.
      must_hold "m7 modelprecache dropped -> sound_precache_entries stays" "$OUT/pov.m7.txt" \
        'sound_precache_entries=[0-9]+' '6720'
      ;;
    m8)
      # Two builds, because the assertion is the difference between them.
      #
      # First: drop the caller's copy of networkProtocol while the guard in
      # scanKnownDemoMessages stands. The scan must refuse loudly.
      echo "--- m8a: entity_model_probe stops copying networkProtocol in (guard stands)"
      patch_in "$MODELTOOL" \
        '    summary.networkProtocol = header.networkProtocol;
' '' || { rc_all=1; continue; }
      bash build-target.sh entity_model_probe >/dev/null 2>&1
      "$MODELPROBE" --tf-root "$TFROOT" --demo "$POV" > "$OUT/model.m8a.txt" 2>&1
      m8rc=$?
      must_refuse "m8a protocol not set -> probe refuses to scan" "$OUT/model.m8a.txt" \
        '"ok":false' "$m8rc"
      must_appear "m8a protocol not set -> error names the cause" "$OUT/model.m8a.txt" \
        'network protocol not set'
      # Second: remove the guard as well, which is the shape the defect actually
      # shipped in. The same missing assignment now exits 0 and prints a summary
      # that looks entirely reasonable -- 673 references and zero render
      # requests. That silence is why the P1 gap was hard to see in the first
      # place, and reproducing it is what makes the guard's value measurable
      # rather than asserted.
      echo "--- m8b: ... and the guard is removed too (the defect's real shape)"
      patch_in "$SRC" \
        '  if (networkProtocol <= 0) return false;
' '' || { rc_all=1; continue; }
      bash build-target.sh entity_model_probe >/dev/null 2>&1
      "$MODELPROBE" --tf-root "$TFROOT" --demo "$POV" > "$OUT/model.m8b.txt" 2>&1
      m8brc=$?
      must_move_j "m8b guard gone -> requests collapse" "$OUT/model.m8b.txt" requests '387'
      must_move_j "m8b guard gone -> assetRefs differ too" "$OUT/model.m8b.txt" assetRefs '675'
      if [ "$m8brc" -eq 0 ]; then
        printf 'MUTATION-RED   %-56s exit=0 while reporting a wrong summary\n' \
          "m8b guard gone -> the wrong reading exits 0"
      else
        printf 'MUTATION-GREEN %-56s exit=%s\n' \
          "m8b guard gone -> the wrong reading exits 0" "$m8brc"
        rc_all=1
      fi
      ;;
    *) echo "unknown case $case_id"; rc_all=1 ;;
  esac
  restore
done

echo
echo "=== restored tree: readings must be identical to the fixed build ==="
"$PROBE" "$BAGEL"   > "$OUT/bagel.restored.txt"   2>&1
"$PROBE" "$PROTO23" > "$OUT/proto23.restored.txt" 2>&1
"$PROBE" "$POV"     > "$OUT/pov.restored.txt"     2>&1
"$FIXPROBE"         > "$OUT/fixture.restored.txt" 2>&1
"$MODELPROBE" --tf-root "$TFROOT" --demo "$POV" > "$OUT/model.restored.txt" 2>&1
./native/build-nmake/presentation_probe.exe > "$OUT/recording.restored.txt" 2>&1
for pair in "bagel.fixed.txt bagel.restored.txt" "proto23.fixed.txt proto23.restored.txt" \
            "pov.fixed.txt pov.restored.txt" "model.fixed.txt model.restored.txt" \
            "fixture.fixed.txt fixture.restored.txt" "recording.fixed.txt recording.restored.txt"; do
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
