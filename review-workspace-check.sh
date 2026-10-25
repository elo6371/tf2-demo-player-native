#!/usr/bin/env bash
# review-workspace-check.sh -- T71 workspace + T72 bookmark store gate.
#
# WHY THIS GATE EXISTS
#   T71 owns the workspace file (the thing that survives a demo closing) and
#   T72 owns the bookmark store that lives inside it. Both are pure value code:
#   an INI document, a hash binding, and a list of records. Every assertion here
#   is a reading printed by the product's own WorkspaceStore / ReviewBookmarkStore
#   through native/tools/review_workspace_probe.cpp -- no screenshots, no second
#   copy of the serialization, no hand-edited fixture standing in for the parser.
#
# WHAT IT DOES NOT CLAIM
#   It does not claim that a UI button appears, that a real .dem file opens, or
#   that T64/T68 events are decoded. The probe drives the stores with synthetic
#   demo bytes and synthetic events, so the T72 acceptance rows that need a real
#   demo (A2/A3 real eventKey from t72-event-map.tsv, A9 real cursor, A11 real
#   reopen) are NOT VERIFIED here; what IS verified is the store-level contract
#   they rest on: exact tick retention, eventKey (tick:packet:bit) identity,
#   sort order, hash refusal, corrupt refusal, transaction rollback, and
#   persistence through the INI document.
#
# THE EXPECTED VALUES ARE HAND-DERIVED, NOT COPIED FROM THE PROBE
#   The fixture document (native/tools/review_workspace_probe.cpp
#   makePopulatedDocument) sets camera fov 95.5 / yaw 45.5 / tick 1234, HUD
#   scale 125 with 3 panels, minimap koth_bagel zoom 1.5, playback speed 0.5 at
#   tick 987, and a workspace name "Bagel Review". Those are the numbers this
#   gate expects; if the serializer drops one, the field line goes match=0.
#   The bookmark ids are derived from the add order: manual 100 -> 1, manual 50
#   -> 2, then three events at tick 300 (packet 2 bit 5 / packet 2 bit 9 /
#   packet 3 bit 5) -> 3,4,5, so order=tick must be 2,1,3,4,5.
#
# Usage: bash review-workspace-check.sh
# Exit:  0 = every assertion held; 1 = at least one FAIL; 2 = probe missing or
#        could not run (NOT RUN is never PASS).

set -uo pipefail
cd "$(dirname "$0")"
export LC_ALL=C

PROBE=native/build-nmake/review_workspace_probe.exe
OUT="${REVIEW_WORKSPACE_OUT:-.scratch/review-workspace}"
mkdir -p "$OUT"

ok_count=0
bad_count=0
fail=0

ok() {
  echo "OK   $*"
  ok_count=$((ok_count + 1))
}

bad() {
  echo "FAIL $*"
  bad_count=$((bad_count + 1))
  fail=1
}

# field <line> <key> -> the token after " key=" on that line.
field() {
  printf '%s\n' "$1" | sed -n "s/.*[[:space:]]$2=\([^[:space:]]*\).*/\1/p"
}

# step_is <step-name> <expected-result> [extra-grep]
step_is() {
  local name="$1" expected="$2" extra="${3:-}"
  local line got
  if [ -n "$extra" ]; then
    line=$(grep -F "review-workspace: step=$name result=" "$OUT/probe.txt" | grep -F "$extra" | head -1)
  else
    line=$(grep -F "review-workspace: step=$name result=" "$OUT/probe.txt" | head -1)
  fi
  if [ -z "$line" ]; then
    bad "step $name${extra:+ [$extra]}: no reading (expected result=$expected)"
    return
  fi
  got=$(field "$line" result)
  if [ "$got" = "$expected" ]; then
    ok "step $name: result=$got"
  else
    bad "step $name: result=${got:-<missing>} (expected $expected)"
  fi
}

# expect_ok <name> -- the probe's own named assertion must exist and pass.
expect_ok() {
  local name="$1"
  if grep -qF "review-workspace: expect name=$name ok=1" "$OUT/probe.txt"; then
    ok "expect $name: ok=1"
  elif grep -qF "review-workspace: expect name=$name ok=0" "$OUT/probe.txt"; then
    bad "expect $name: ok=0"
  else
    bad "expect $name: missing (an unexecuted assertion is INVALID, not PASS)"
  fi
}

# field_is <name> <expected-encoded> -- the roundtrip field must equal the
# fixture value on both sides of the serialize/parse trip.
field_is() {
  local name="$1" expected="$2" line got_exp got_act got_match
  line=$(grep -F "review-workspace: field name=$name " "$OUT/probe.txt" | head -1)
  if [ -z "$line" ]; then
    bad "field $name: no reading (expected $expected)"
    return
  fi
  got_exp=$(field "$line" expected)
  got_act=$(field "$line" actual)
  got_match=$(field "$line" match)
  if [ "$got_exp" = "$expected" ] && [ "$got_act" = "$expected" ] && [ "$got_match" = "1" ]; then
    ok "field $name: $got_act"
  else
    bad "field $name: expected=${got_exp:-<missing>} actual=${got_act:-<missing>} match=${got_match:-<missing>} (want $expected)"
  fi
}

for tool in grep sed awk; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 2; }
done

# --- 0. the subject exists and runs -----------------------------------------
if [ ! -f "$PROBE" ]; then
  echo "NOT RUN: probe missing: $PROBE"
  echo "  build it with: bash build-target.sh review_workspace_probe"
  echo "REVIEW-WORKSPACE-CHECK=FAIL"
  echo "RC=2"
  exit 2
fi
"$PROBE" --workspace-test "$OUT/scratch" > "$OUT/probe.txt" 2> "$OUT/probe.err"
probe_rc=$?
if ! grep -q '^review-workspace: probe done ' "$OUT/probe.txt"; then
  echo "NOT RUN: probe exited rc=$probe_rc without a completion line"
  sed 's/^/  /' "$OUT/probe.err" | head -10
  echo "REVIEW-WORKSPACE-CHECK=FAIL"
  echo "RC=3"
  exit 3
fi
if [ "$probe_rc" -ne 0 ]; then
  # A probe that reports failed assertions is a RED RUN, not an unrun gate: keep
  # going so every failed reading prints its own FAIL line. Exiting here as
  # "NOT RUN" would hide WHICH assertion failed and would make a mutation look
  # like an infrastructure error instead of a red gate.
  bad "probe exited rc=$probe_rc with failed assertions (see $OUT/probe.txt)"
else
  ok "probe ran: $PROBE --workspace-test rc=0 ($(wc -l < "$OUT/probe.txt" | tr -d ' ') lines)"
fi

demo_line=$(grep -m1 '^review-workspace: demo hashA=' "$OUT/probe.txt" || true)
hash_a=$(field "$demo_line" hashA)
hash_b=$(field "$demo_line" hashB)
if [ -n "$hash_a" ] && [ -n "$hash_b" ] && [ "$hash_a" != "$hash_b" ] \
   && [ "${#hash_a}" -eq 64 ]; then
  ok "demo fixtures: two distinct 64-hex hashes"
else
  bad "demo fixtures: hashA=${hash_a:-<missing>} hashB=${hash_b:-<missing>}"
fi

summary_line=$(grep -m1 '^review-workspace: summary ' "$OUT/probe.txt" || true)
if [ -z "$summary_line" ]; then
  bad "summary: no counters line"
else
  s_add=$(field "$summary_line" add)
  s_update=$(field "$summary_line" update)
  s_delete=$(field "$summary_line" delete)
  s_refused=$(field "$summary_line" refused)
  s_dup=$(field "$summary_line" duplicateEvent)
  s_jf=$(field "$summary_line" jumpFailures)
  s_fields=$(field "$summary_line" fields)
  s_fm=$(field "$summary_line" fieldMismatches)
  if [ "$s_add" = "5" ] && [ "$s_update" = "1" ] && [ "$s_delete" = "1" ] \
     && [ "$s_refused" = "6" ] && [ "$s_dup" = "1" ] && [ "$s_jf" = "3" ] \
     && [ "${s_fields:-0}" -ge 40 ] && [ "$s_fm" = "0" ]; then
    ok "summary: add=$s_add update=$s_update delete=$s_delete refused=$s_refused duplicateEvent=$s_dup jumpFailures=$s_jf fields=$s_fields fieldMismatches=$s_fm"
  else
    bad "summary: add=${s_add:-?} update=${s_update:-?} delete=${s_delete:-?} refused=${s_refused:-?} duplicateEvent=${s_dup:-?} jumpFailures=${s_jf:-?} fields=${s_fields:-?} fieldMismatches=${s_fm:-?} (expected 5/1/1/6/1/3, fields>=40, mismatches=0)"
  fi
fi

# Every named assertion the probe executed must pass; a missing assertion is
# INVALID, so the total is checked too (the probe has ~100 checks).
expect_total=$(grep -c '^review-workspace: expect name=' "$OUT/probe.txt" || true)
expect_bad=$(grep -c '^review-workspace: expect name=.* ok=0' "$OUT/probe.txt" || true)
if [ "$expect_total" -ge 95 ] && [ "$expect_bad" -eq 0 ]; then
  ok "probe assertions: $expect_total executed, 0 failed"
else
  bad "probe assertions: $expect_total executed, $expect_bad failed (expected >=95 and 0)"
fi
field_total=$(grep -c '^review-workspace: field name=' "$OUT/probe.txt" || true)
field_bad=$(grep -c '^review-workspace: field name=.* match=0' "$OUT/probe.txt" || true)
if [ "$field_total" -ge 40 ] && [ "$field_bad" -eq 0 ]; then
  ok "roundtrip fields: $field_total compared, 0 mismatched"
else
  bad "roundtrip fields: $field_total compared, $field_bad mismatched (expected >=40 and 0)"
fi

# --- 1. workspace create -> save -> load roundtrip --------------------------
# The document must survive its own serializer field-for-field, and a resave of
# the parsed document must be byte-identical (a stable writer, not "some INI").
step_is new created
step_is save ok
step_is load opened
step_is resave ok
step_is load-moved opened

field_is schema 1
field_is name 'Bagel%20Review'
field_is demoHashAlgo sha256
field_is demoHash "$hash_a"
field_is autosave 1
field_is camera.valid 1
field_is camera.focusX 1.5
field_is camera.focusY -2.25
field_is camera.focusZ 512.125
field_is camera.yaw 45.5
field_is camera.pitch -12.25
field_is camera.distance 320
field_is camera.fov 95.5
field_is camera.mode third-person
field_is camera.cameraTick 1234
field_is camera.slotCount 8
field_is hud.enabled 1
field_is hud.scale 125
field_is hud.panelCount 3
field_is minimap.collapsed 0
field_is minimap.zoom 1.5
field_is minimap.mapName koth_bagel
field_is minimap.available 1
field_is eventFilter.eventName player_death
field_is eventFilter.player 'userid%3A7'
field_is logs.tfUrl 'https%3A%2F%2Flogs.tf%2F123456'
field_is playback.speed 0.5
field_is playback.tick 987
field_is extraKeyCount 1
field_is extraSectionCount 1

expect_ok new_created
expect_ok new_file_exists
expect_ok save_ok
expect_ok save_bytes_readable
expect_ok load_opened
expect_ok roundtrip_fields
expect_ok resave_ok
expect_ok resave_bytes_readable
expect_ok resave_bytes_equal
expect_ok moved_copy
expect_ok moved_still_opens
expect_ok moved_path_named

# --- 2. demo hash mismatch is named, not silently applied -------------------
# The workspace was written for hash A; opening it with hash B must report
# hash_mismatch, must not load, and a save attempt must leave the bytes alone.
step_is load-hash-mismatch hash_mismatch
expect_ok hash_mismatch_named
expect_ok hash_mismatch_bytes_before
expect_ok hash_mismatch_save_refused
expect_ok hash_mismatch_bytes_after
expect_ok hash_mismatch_file_intact

# --- 3. corrupt workspace is named, not silently discarded ------------------
# A truncated/undecodable file must report corrupt and keep its bytes.
step_is load-corrupt corrupt
expect_ok corrupt_written
expect_ok corrupt_named
expect_ok corrupt_bytes_before
expect_ok corrupt_save_refused
expect_ok corrupt_bytes_after
expect_ok corrupt_file_intact
expect_ok header_only_corrupt

# --- 4. old schema migrates explicitly; unknown version refuses -------------
# version=1 is current; an older record version may be migrated (header AND
# every record rewritten); a future version must be refused by name.
step_is load-old-schema old_schema
step_is migrate ok
step_is migrate-unknown refused
expect_ok old_schema_written
expect_ok old_schema_named
expect_ok migrate_ok
expect_ok migrate_reopens
expect_ok migrated_bytes
expect_ok migrate_header_rewritten
expect_ok migrate_records_rewritten
expect_ok header_only_patched
expect_ok header_only_written
expect_ok future_written
expect_ok unknown_version_named
expect_ok unknown_version_migrate_refused

migrate_unknown_line=$(grep -m1 'review-workspace: step=migrate-unknown ' "$OUT/probe.txt" || true)
mu_reason=$(field "$migrate_unknown_line" reason)
if [ "$mu_reason" = "unsupported-version" ]; then
  ok "unknown version: migrate refused reason=unsupported-version"
else
  bad "unknown version: migrate reason=${mu_reason:-<missing>} (expected unsupported-version)"
fi

# --- 5. bookmark add / sort / query / edit / delete ------------------------
# Manual bookmarks store the actual cursor tick (100 then 50), event bookmarks
# store the event tick (300) and the T68 eventKey tick:packet:bit, so three
# events on one tick are three distinct rows.
step_is bookmark-load loaded
step_is bookmark-add added 'tick=100 id=1'
step_is bookmark-add added 'tick=50 id=2'
step_is bookmark-add-event added 'key=300:2:5 tick=300 id=3'
step_is bookmark-add-event added 'key=300:2:9 tick=300 id=4'
step_is bookmark-add-event added 'key=300:3:5 tick=300 id=5'
step_is bookmark-add-event already_exists 'key=300:2:5 existing=3'
step_is list ok 'order=tick ids=2,1,3,4,5'
step_is bookmark-update updated 'id=1'
step_is bookmark-delete removed 'id=2 count=4'

expect_ok bookmark_load_loaded
expect_ok bookmark_add_manual
expect_ok bookmark_add_second
expect_ok bookmark_add_event
expect_ok bookmark_add_event_same_tick
expect_ok bookmark_add_event_third
expect_ok duplicate_event_already_exists
expect_ok empty_title_refused
expect_ok seek_pending_refused
expect_ok no_demo_refused
expect_ok not_loaded_refused
expect_ok sort_ascending
expect_ok same_tick_keys_differ
expect_ok query_text
expect_ok filter_event
expect_ok filter_player_userid
expect_ok filter_player_entindex
expect_ok filter_tick_range
expect_ok filter_manual
expect_ok update_ok
expect_ok update_immutable
expect_ok update_empty_title_refused
expect_ok update_missing
expect_ok delete_ok
expect_ok delete_missing

# --- 6. jump is a transaction: camera first, rollback on refusal -----------
step_is bookmark-jump applied 'key=300:2:5 tick=300'
step_is bookmark-jump applied 'key=300:3:5 tick=300'
step_is bookmark-jump refused_out_of_range 'key=none tick=5000'
step_is bookmark-jump refused_playback 'key=300:2:5 tick=300 rollback=1'

expect_ok jump_applied
expect_ok same_tick_jump_separate
expect_ok jump_out_of_range_keeps
expect_ok jump_playback_rollback
expect_ok jump_camera_refused
expect_ok jump_missing
expect_ok jump_hash_mismatch
expect_ok jump_not_loaded

# --- 7. persistence, lifecycle, and failure visibility ---------------------
# Save through the workspace document, reopen by hash, and every bookmark must
# come back with its tick, eventKey, note and identity reason. The lifecycle
# rows cover autosave states, rename bounds, close-with-unsaved, and the
# read-only target where save must be refused and bytes must not change.
step_is bookmark-save ok 'count=4'
step_is rename renamed 'name=My%20Workspace'
step_is load-missing missing
step_is load-wrong-path corrupt
step_is save-readonly refused_read_only

expect_ok bookmark_save_to_doc
expect_ok bookmark_save_ok
expect_ok bookmark_reopen
expect_ok bookmarks_persist
expect_ok bookmarks_persist_eventkeys
expect_ok bookmarks_persist_note
expect_ok bookmarks_jump_after_reload
expect_ok no_identity_unavailable
expect_ok no_identity_trace_named
expect_ok lifecycle_open
expect_ok autosave_clean
expect_ok autosave_disabled
expect_ok autosave_saved
expect_ok rename_empty
expect_ok rename_too_long
expect_ok rename_ok
expect_ok rename_save
expect_ok close_unsaved
expect_ok close_clean
expect_ok missing_named
expect_ok wrong_path_named
expect_ok new_no_hash
expect_ok new_already_exists
expect_ok save_not_loaded
expect_ok readonly_seed
expect_ok readonly_attrib
expect_ok readonly_open
expect_ok readonly_bytes_before
expect_ok readonly_bytes_after
expect_ok readonly_refused
expect_ok event_key_manual_none
expect_ok players_trace_named
expect_ok list_line_has_fields

# --- summary ----------------------------------------------------------------
echo "assertions_ok=$ok_count assertions_bad=$bad_count"
if [ "$fail" -eq 0 ]; then
  echo "REVIEW-WORKSPACE-CHECK=PASS"
  echo "RC=0"
  exit 0
fi
echo "REVIEW-WORKSPACE-CHECK=FAIL"
echo "RC=1"
exit 1
