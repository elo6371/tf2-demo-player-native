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
# Usage: bash mutate.sh [m1 m2 m3 m4 m5 m6 m7 m8 m9 m10 m11 m12 m13]
set -uo pipefail
cd "$(dirname "$0")"

# Mutations rebuild the same binaries as verify-all. Direct mutation runs must
# therefore share verify-all's filesystem lock; the parent chain sets the
# environment marker so its step 8 does not try to acquire the lock twice.
LOCK_DIR=.scratch/verify-all.lock
if [ "${VERIFY_ALL_LOCK_HELD:-0}" != "1" ]; then
  if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    echo "MUTATION-LOCK=FAIL (another build or mutation owns $LOCK_DIR)"
    exit 2
  fi
  trap 'rmdir "$LOCK_DIR" 2>/dev/null || true' EXIT
fi

SRC=native/src/demo_header.cpp
MODELSRC=native/src/entity_model.cpp
MODELTOOL=native/tools/entity_model_probe.cpp
MAINSRC=native/src/main.cpp
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

CASES="${*:-m1 m2 m3 m4 m5 m6 m7 m8 m9 m10 m11 m12 m13}"
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
  # binary left over from the previous mutation. tf2_demo_native is in the list
  # because m12 mutates main.cpp: without it the restore would put the source back
  # and leave the *mutated* program on disk for anything that runs it afterwards
  # (verify-all.sh's frame-capture step is the first such caller).
  rebuild entity_protocol_probe entity_message_fixture_probe presentation_probe entity_model_probe tf2_demo_native || true
}

value() { grep -oE "$2" "$1" | head -1 | cut -d= -f2; }

# mutate.sh rebuilds a mutated source and then reads the probe's output. It used
# to run `bash build-target.sh ... >/dev/null 2>&1` and carry on regardless, so a
# build that failed produced an empty capture, every assertion on that capture
# read nothing, and the case was reported as MUTATION-GREEN -- "the mutation did
# not move the reading" -- when the reading had never been taken. That is exactly
# what happened to m5 in the 2026-10-10 chain: entity_message_fixture_probe.exe
# was missing, and the compiler error went into /dev/null with the redirect.
#
# rebuild keeps the compiler output, and returns non-zero only when the build is
# still failing. Callers must `|| continue`: a case whose build failed has no
# readings to assert on, and skipping it is what lets the pinned red/hold count
# notice that the suite did less work than it claims.
rebuild() { # rebuild <target> [target...]
  local log=.scratch/mutation-build.log
  if bash build-target.sh "$@" > "$log" 2>&1; then
    return 0
  fi
  printf 'MUTATION-BUILD-FAIL rebuild failed: build-target.sh %s\n' "$*"
  grep -a -E "error C|fatal error|LNK[0-9]+" "$log" | head -10
  cp "$log" "$log.failed" 2>/dev/null
  echo "  full compiler output: $log.failed"
  rc_all=1
  return 1
}

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

# A mutation whose reading must get *worse*, not merely different. `must_move`
# would accept a worst gap that fell from 1180 to 1179; the defect being
# reintroduced is a multi-thousand-tick hole, so the assertion is a threshold.
must_exceed() { # must_exceed <label> <file> <regex> <threshold>
  local label="$1" file="$2" regex="$3" threshold="$4" got
  got="$(value "$file" "$regex")"
  if [ -n "$got" ] && [ "$got" -gt "$threshold" ]; then
    printf 'MUTATION-RED   %-56s %s (> %s)\n' "$label" "$got" "$threshold"
  else
    printf 'MUTATION-GREEN %-56s got=%s threshold=%s\n' "$label" "${got:-<none>}" "$threshold"
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
      rebuild entity_protocol_probe || continue
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
      rebuild entity_protocol_probe || continue
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
      rebuild entity_protocol_probe || continue
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
      rebuild entity_message_fixture_probe || continue
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
      rebuild entity_message_fixture_probe || continue
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
      rebuild presentation_probe entity_protocol_probe || continue
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
      rebuild entity_protocol_probe entity_model_probe || continue
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
      rebuild entity_model_probe || continue
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
      rebuild entity_model_probe || continue
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
    m9)
      # Reintroduce the retention defect §3.1 fixes: thin the archive by *index*,
      # which pins the head verbatim and leaves a multi-thousand-tick hole. The
      # replacement is the pre-fix body verbatim, so the case fails if the fix is
      # ever reworded without the gate noticing. The hole reads 51568 here rather
      # than the handoff's 54392: the duplicate-tick pushes that padded the old
      # archive are gone in this build, so the hole is the policy's alone.
      echo "--- m9: thin the history archive by index again (head pinned, big hole)"
      patch_in "$SRC" \
        '  const std::size_t last = archive.size() - 1;
  std::vector<char> kept(archive.size(), 0);
  kept[0] = 1;
  kept[last] = 1;
  std::size_t used = 2;
  std::priority_queue<HistoryGap, std::vector<HistoryGap>, HistoryGapWider> gaps;
  gaps.push(HistoryGap{archive[last].tick - archive[0].tick, 0, last});
  while (used < maxCount && !gaps.empty()) {
    const HistoryGap gap = gaps.top();
    gaps.pop();
    if (gap.right <= gap.left + 1) continue;  // no candidate left inside it
    // Nearest candidate to the tick midpoint. `archive` is tick-ordered, so this
    // is a binary search rather than a scan.
    const std::int64_t midpoint =
        (static_cast<std::int64_t>(archive[gap.left].tick) + archive[gap.right].tick) / 2;
    const auto lower = std::lower_bound(
        archive.begin() + static_cast<std::ptrdiff_t>(gap.left + 1),
        archive.begin() + static_cast<std::ptrdiff_t>(gap.right), midpoint,
        [](const EntityHistoryCheckpoint& item, std::int64_t value) { return item.tick < value; });
    std::size_t pick = static_cast<std::size_t>(lower - archive.begin());
    if (pick > gap.left + 1) {
      const std::size_t before = pick - 1;
      const std::int64_t distanceBefore = midpoint - archive[before].tick;
      const std::int64_t distanceAt = pick < gap.right
          ? archive[pick].tick - midpoint : std::numeric_limits<std::int64_t>::max();
      if (distanceBefore <= distanceAt) pick = before;
    }
    if (pick <= gap.left) pick = gap.left + 1;
    if (pick >= gap.right) pick = gap.right - 1;
    kept[pick] = 1;
    ++used;
    gaps.push(HistoryGap{archive[pick].tick - archive[gap.left].tick, gap.left, pick});
    gaps.push(HistoryGap{archive[gap.right].tick - archive[pick].tick, pick, gap.right});
  }
  std::vector<EntityHistoryCheckpoint> reduced;
  reduced.reserve(used);
  for (std::size_t index = 0; index < archive.size(); ++index) {
    if (kept[index]) reduced.push_back(std::move(archive[index]));
  }
  archive = std::move(reduced);' \
        '  const std::size_t last = archive.size() - 1;
  std::vector<EntityHistoryCheckpoint> kept;
  kept.reserve(maxCount);
  std::size_t previous = static_cast<std::size_t>(-1);
  for (std::size_t slot = 0; slot < maxCount; ++slot) {
    std::size_t index = (last * slot) / (maxCount - 1);
    if (index <= previous) index = std::min(last, previous + 1);
    previous = index;
    kept.push_back(std::move(archive[index]));
    if (index == last) break;
  }
  archive = std::move(kept);' || { rc_all=1; continue; }
      rebuild presentation_probe entity_model_probe || continue
      ./native/build-nmake/presentation_probe.exe > "$OUT/recording.m9.txt" 2>&1
      must_appear "m9 index-even thinning -> fixture coverage flag" \
        "$OUT/recording.m9.txt" '"historyCoverage":false'
      must_move_j "m9 index-even thinning -> fixture worst gap" "$OUT/recording.m9.txt" \
        historyWorstGap '4096'
      # The gate is run end to end while the defect is live, with its output
      # redirected so a red run cannot overwrite the fixed-build evidence. The
      # exit status plus the verdict line is the assertion: the gate must catch
      # the policy, not merely the fixture.
      HISTORY_COVERAGE_OUT="$OUT/historycoverage-m9" bash history-coverage-check.sh \
        > "$OUT/gate.m9.txt" 2>&1
      m9rc=$?
      must_refuse "m9 index-even thinning -> gate refuses" "$OUT/gate.m9.txt" \
        'HISTORY-COVERAGE=FAIL' "$m9rc"
      must_exceed "m9 index-even thinning -> bagel worst gap" \
        "$OUT/historycoverage-m9/bagel-summary.txt" 'history gap worst=[0-9]+' 10000
      ;;
    m10)
      # Reintroduce the weapon-render defect §3.3 fixes: make the world-model
      # route unreachable, so a weapon's path falls back to m_nModelIndex -- the
      # first-person arms composite. The wrong path still resolves to a real
      # asset, so every counter the old acceptance chain measured stays green;
      # the readings that move are the ones this round added, and the gate that
      # has to catch it is the one this round wrote. `false &&` rather than
      # deleting the block: the defect being reproduced is "the route was never
      # there", and an unreachable block leaves the same runtime shape while
      # keeping the compilers quiet about everything around it.
      echo "--- m10: world-model route unreachable again (weapons name their arms)"
      patch_in "$SRC" \
        '    if (reference.hasWorldModelIndex) {
' '    if (false && reference.hasWorldModelIndex) {
' || { rc_all=1; continue; }
      rebuild entity_protocol_probe entity_model_probe || continue
      # The fixture is the sharpest witness: its whole branch table is pinned, so
      # the defect collapses the reading to zeros. The assertion is the refusal,
      # not merely a moved number -- the fixture must not print a summary that
      # looks like a pass. The literal carries the collapsed shape *and* the one
      # number that moves the other way: armsWeapon reads 2, not 1, because
      # entity 1 -- the reference the fixed route rescues, whose world index 11
      # names the weapon while its model index 10 is arms -- falls back to the
      # arms path too, alongside entity 10 which was built to be that case.
      "$MODELPROBE" --self-test > "$OUT/model.m10-selftest.json" 2> "$OUT/model.m10-selftest.txt"
      m10src=$?
      must_refuse "m10 world route gone -> fixture self-test refuses" \
        "$OUT/model.m10-selftest.txt" \
        'weapon-wiring-fixture refs=11 known=0 resolved=0 zero=0 unresolved=0 outOfRange=0 unresolvedMax=-1 onlyWorld=0 fromWorld=0 agrees=0 viewZero=0 differs=0 armsWeapon=2' \
        "$m10src"
      # The real demo: the route's counters collapse while the property keeps
      # being read -- which is exactly what separates "did not read it" from
      # "read it and ignored it" (the defect's actual shape).
      "$PROBE" "$POV" > "$OUT/pov.m10.txt" 2>&1
      must_move "m10 world route gone -> protocol probe fromWorld" "$OUT/pov.m10.txt" \
        'asset_model_path_from_world_model=[0-9]+' '69'
      must_move "m10 world route gone -> protocol probe known" "$OUT/pov.m10.txt" \
        'asset_world_model_index_known=[0-9]+' '71'
      must_move "m10 world route gone -> view/model compare dies with the block" \
        "$OUT/pov.m10.txt" 'asset_weapon_view_model_zero=[0-9]+' '2'
      "$MODELPROBE" --tf-root "$TFROOT" --demo "$POV" > "$OUT/model.m10.txt" 2>&1
      must_move_j "m10 world route gone -> requests leave the world route" \
        "$OUT/model.m10.txt" worldModelRequests '69'
      must_move_j "m10 world route gone -> weapons name their arms" \
        "$OUT/model.m10.txt" armsWeaponRefs '0'
      # The gate, end to end, with its output redirected so a red run cannot
      # overwrite the fixed-build evidence in evidence/weapon-world-model/.
      WEAPON_WORLD_MODEL_OUT="$OUT/weaponworldmodel-m10" bash weapon-world-model-check.sh \
        > "$OUT/gate.m10.txt" 2>&1
      m10rc=$?
      must_refuse "m10 world route gone -> weapon gate refuses" "$OUT/gate.m10.txt" \
        'WEAPON-WORLD-MODEL=FAIL' "$m10rc"
      # The gate's own bagel run is the second demo, read out of the redirected
      # evidence rather than paid for with another full bagel scan here.
      must_move_j "m10 world route gone -> gate bagel run reads known=0" \
        "$OUT/weaponworldmodel-m10/bagel.json" assetWorldModelKnown '33'
      ;;
    m11)
      # Reintroduce the slot-selection defect the freshness round fixed: drop the
      # lastWriteTick term from preferCandidate, so the rule falls back to
      # rank-then-name -- the rule that resolved a 2379-tick-stale copy of the POV
      # target's origin while a freshly written copy sat in the neighbouring slot.
      # The number of candidates examined does not change, so every count-based
      # reading stays green; the witness has to be the gate this round wrote, and
      # the assertion is its refusal, not merely a moved number.
      echo "--- m11: freshness ignored again (the stale slot wins on rank)"
      patch_in "$MODELSRC" \
        '  if (lastWriteTick != bestTick) return lastWriteTick > bestTick;
' '  // mutation: the freshness term is removed, rank decides alone
' || { rc_all=1; continue; }
      rebuild entity_model_probe || continue
      SLOT_FRESHNESS_OUT="$OUT/slotfreshness-m11" bash slot-freshness-check.sh \
        > "$OUT/gate.m11.txt" 2>&1
      m11rc=$?
      must_refuse "m11 freshness ignored -> gate refuses" "$OUT/gate.m11.txt" \
        'SLOT-FRESHNESS=FAIL' "$m11rc"
      # The reading that has to move is the one that names the defect: on all three
      # POV queries the pick is a non-freshest slot again, and it is the Local slot
      # frozen at 51596 that comes back.
      must_move "m11 freshness ignored -> POV chosenStale" \
        "$OUT/slotfreshness-m11/pov.txt" 'chosenStale=[0-9]+' '0'
      must_appear "m11 freshness ignored -> the frozen Local slot is chosen again" \
        "$OUT/slotfreshness-m11/pov.txt" \
        'rank=0 (chosen) name=DT_TFLocalPlayerExclusive.m_vecOrigin'
      # Bagel must NOT move: there the rank rule already picks the fresh slot, so a
      # mutation that moved bagel as well would mean the gate is not reading the two
      # demos independently -- and "always prefer NonLocal" would pass it.
      must_hold "m11 freshness ignored -> bagel chosenStale stays 0" \
        "$OUT/slotfreshness-m11/bagel.txt" 'chosenStale=[0-9]+' '0'
      ;;
    m12)
      # Reintroduce the defect the entity-material round fixed, at the one point
      # the round's own gate has to be able to see: the resolution chain still
      # runs, the texture is still decoded, and the SRV never reaches the draw --
      # so every instance is painted with the world atlas again. The synthetic
      # half of the gate must NOT move (19 of medic's 21 slots still resolve),
      # because a red there would mean the mutation moved the wrong thing and the
      # gate's two layers are one reading counted twice.
      echo "--- m12 no model SRV reaches the draw (the atlas paints the models again)"
      patch_in "$MAINSRC" \
        '      const bool textureUploaded =
        renderer.uploadEntityModelTexture(prepared.cacheKey, preparedRgba, preparedWidth, preparedHeight);
' '      // mutation: the resolved texture never reaches the draw
      const bool textureUploaded = false;
' || { rc_all=1; continue; }
      rebuild tf2_demo_native || continue
      ENTITY_MATERIAL_OUT="$OUT/entitymaterial-m12" bash entity-material-check.sh \
        > "$OUT/gate.m12.txt" 2>&1
      m12rc=$?
      must_refuse "m12 no model SRV -> entity-material gate refuses" "$OUT/gate.m12.txt" \
        'ENTITY-MATERIAL=FAIL' "$m12rc"
      # The instance-level counter is the reading the defect targets: every
      # instance still drawn, none of them with a model texture bound. The
      # extractor cuts on the first '=', so each assertion names one field.
      must_move "m12 no model SRV -> every instance loses its texture" \
        "$OUT/gate.m12.txt" 'entityMaterials=[0-9]+/[0-9]+' '95/95'
      # The per-model account must move too, and for the reason the mutation has:
      # all 59 models still resolve a material, none of them upload.
      must_move "m12 no model SRV -> the ledger's uploaded count drops" \
        "$OUT/gate.m12.txt" 'uploaded=[0-9]+' '59'
      # And the parse layer holds, which is what keeps the two halves separate.
      must_hold "m12 no model SRV -> medic still resolves 19 slots" \
        "$OUT/gate.m12.txt" 'resolved=[0-9]+' '19'
      ;;
    m13)
      # Ablate the animation-property sweep so it stops counting anything. The
      # gate's central claim is a NEGATIVE -- CTFPlayer carries zero of four
      # animation properties -- and a negative reads identically to a rule that
      # never fires. This mutation makes the rule never fire (the leaf-name
      # comparison is neutered), and the positive control is what has to catch it:
      # classesWithSequence must collapse from 197 to 0 and the gate must refuse.
      # Without clause 2 the whole gate would pass while measuring nothing, which
      # is the failure mode this case exists to rule out.
      echo "--- m13 the animation-property sweep stops matching (the positive control must catch it)"
      patch_in "$MODELTOOL" \
        '      if (leaf == "m_nSequence") ++sequence;
' '      // mutation: the sweep never matches, so every count reads zero
      if (false && leaf == "m_nSequence") ++sequence;
' || { rc_all=1; continue; }
      rebuild entity_model_probe || continue
      ANIM_AVAILABILITY_OUT="$OUT/animavail-m13" bash animation-availability-check.sh \
        > "$OUT/gate.m13.txt" 2>&1
      m13rc=$?
      must_refuse "m13 sweep ablated -> availability gate refuses" "$OUT/gate.m13.txt" \
        'ANIMATION-AVAILABILITY=FAIL' "$m13rc"
      # The positive control moving to 0 is the reading the mutation targets. If
      # this stayed at 197 the mutation did not take, and if the gate still passed
      # with it at 0 then clause 2 is not load bearing.
      must_move "m13 sweep ablated -> classesWithSequence collapses" \
        "$OUT/animavail-m13/bagel.txt" 'classesWithSequence=[0-9]+' '197'
      # Asserted with must_appear, not must_hold: the player line has four '='
      # fields, and mutate.sh's value() cuts on the first '=' and returns a single
      # field, so a multi-field line cannot be compared with it. A literal
      # substring match is also what the claim actually is -- this exact line,
      # unchanged, is present in the mutated run.
      must_appear "m13 sweep ablated -> the CTFPlayer line is unchanged" \
        "$OUT/animavail-m13/bagel.txt" \
        'animprop-player class=CTFPlayer id=247 sequence=0 cycle=0 rate=0 pose=0'
      # tfPlayerFound must hold too: the class is still found, it is the counting
      # that broke. This is the difference between "the player has none" and "the
      # sweep looked at nothing".
      must_hold "m13 sweep ablated -> CTFPlayer is still found (found=1)" \
        "$OUT/animavail-m13/bagel.txt" 'tfPlayerFound=[0-9]+' '1'
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
