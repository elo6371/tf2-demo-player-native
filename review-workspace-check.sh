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

# --- 8. the product itself: create -> reopen -> jump, and two named refusals --
# Sections 1..7 drive the stores through the probe. This one proves the PRODUCT
# drives them: a real demo, real runs, and the file the product wrote. No reading
# below comes from the probe's output -- each one is a trace line or a byte in
# the workspace file, so "the probe can save a workspace" cannot stand in for
# "the player can save a workspace".
PRODUCT=native/build-nmake/tf2_demo_native.exe
DEMO="${DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}"
TF_ROOT="${TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}"
PROD_OUT="$OUT/product"
mkdir -p "$PROD_OUT"

ws_section() { # ws_section <file> <section> <key> -> the value, empty when absent
  sed -n "/^\[$2\]/,/^\[/p" "$1" | sed -n "s/^$3=//p" | head -1
}

if [ ! -f "$PRODUCT" ]; then
  bad "product missing: $PRODUCT (build it with: bash build-target.sh tf2_demo_native)"
elif [ ! -f "$DEMO" ]; then
  bad "demo missing: $DEMO"
else
  WS="$PROD_OUT/ws.ini"
  rm -f "$WS"
  printf 'save-bookmark 2 Gate Slot\nbookmark-add Smoke note from the product\nworkspace-save\nquit\n' > "$PROD_OUT/run1.script"
  # pause BEFORE the jump: a playing transport advances a tick between two script
  # steps (measured: bookmark tick 19 landed 19 on one run, bookmark 18 landed 19 on
  # the next), so an unpaused run turns this reading into a coin flip.
  printf 'pause\nbookmark-list tick\nbookmark-jump 1\nworkspace-report\nquit\n' > "$PROD_OUT/run2.script"
  printf 'quit\n' > "$PROD_OUT/run3.script"

  run_product() { # run_product <tag> <workspace> <script>
    TF2_NATIVE_TRACE="$PROD_OUT/$1.trace" timeout 240 "$PRODUCT" --tf-root "$TF_ROOT" \
      --audio-device 6 --audio-disabled --hidden-window --demo "$DEMO" \
      --workspace "$2" --camera-script "$3" > "$PROD_OUT/$1.log" 2>&1
  }

  run_product run1 "$WS" "$PROD_OUT/run1.script"
  rc=$?
  if [ "$rc" -eq 0 ]; then ok "product run 1 exited 0 (created the workspace)"; else bad "product run 1 rc=$rc (see $PROD_OUT/run1.log)"; fi
  if grep -qF 'review: workspace: create status=created' "$PROD_OUT/run1.trace"; then
    ok "run 1 created the workspace through the product's own store"
  else
    bad "run 1 never printed a create line"
  fi
  if [ -s "$WS" ]; then ok "the workspace file exists after run 1 ($(wc -c < "$WS" | tr -d ' ') bytes)"; else bad "no workspace file at $WS"; fi

  demo_sha=$(sha256sum "$DEMO" | cut -d' ' -f1)
  stored_sha=$(sed -n 's/^demoHash=//p' "$WS" | head -1)
  if [ -n "$stored_sha" ] && [ "$stored_sha" = "$demo_sha" ]; then
    ok "the file is bound to the demo's own sha256 (${demo_sha:0:12}...)"
  else
    bad "file demoHash=${stored_sha:-<missing>} but the demo hashes to ${demo_sha:0:12}..."
  fi

  cam_valid=$(ws_section "$WS" workspace.camera valid)
  cam_reason=$(ws_section "$WS" workspace.camera cameraReason)
  slot_count=$(ws_section "$WS" workspace.camera slotCount)
  slot2_saved=$(ws_section "$WS" workspace.camera slot.2.saved)
  slot2_name=$(ws_section "$WS" workspace.camera slot.2.name)
  if [ "$cam_valid" = "1" ]; then
    ok "the camera section is filled (valid=1 reason=${cam_reason:-<missing>})"
  else
    bad "the camera section is empty (valid=${cam_valid:-<missing>} reason=${cam_reason:-<missing>})"
  fi
  if [ "$slot_count" = "8" ]; then ok "the file carries all 8 T50 slots"; else bad "slotCount=${slot_count:-<missing>} (want 8)"; fi
  if [ "$slot2_saved" = "1" ] && printf '%s' "$slot2_name" | grep -qE '^Gate(%20| )Slot$'; then
    ok "slot 2 round-trips as saved with its name (${slot2_name})"
  else
    bad "slot 2 saved=${slot2_saved:-<missing>} name=${slot2_name:-<missing>}"
  fi

  bm_count=$(sed -n 's/^count=//p' "$WS" | head -1)
  bm_tick=$(sed -n '/^\[review\.bookmark\.1\]/,/^\[/p' "$WS" | sed -n 's/^tick=//p' | head -1)
  bm_title=$(sed -n '/^\[review\.bookmark\.1\]/,/^\[/p' "$WS" | sed -n 's/^title=//p' | head -1)
  if [ "$bm_count" = "1" ] && [ "$bm_title" = "Smoke" ]; then
    ok "the bookmark is in the file (count=1 title=$bm_title tick=${bm_tick:-<missing>})"
  else
    bad "bookmark record count=${bm_count:-<missing>} title=${bm_title:-<missing>}"
  fi
  if [ -n "$bm_tick" ] && [ "$bm_tick" -ge 0 ] 2>/dev/null; then ok "the bookmark carries a real demo tick"; else bad "bookmark tick=${bm_tick:-<missing>}"; fi

  run_product run2 "$WS" "$PROD_OUT/run2.script"
  rc=$?
  if [ "$rc" -eq 0 ]; then ok "product run 2 exited 0 (reopened the same file)"; else bad "product run 2 rc=$rc (see $PROD_OUT/run2.log)"; fi
  open_line=$(grep -F 'review: workspace: open state=' "$PROD_OUT/run2.trace" | head -1)
  open_state=$(field "$open_line" state)
  open_loaded=$(field "$open_line" loaded)
  open_bookmarks=$(field "$open_line" bookmarks)
  if [ "$open_state" = "opened" ] && [ "$open_loaded" = "1" ] && [ "$open_bookmarks" = "1" ]; then
    ok "the second run opens the file it wrote (state=opened loaded=1 bookmarks=1)"
  else
    bad "second run open state=${open_state:-<missing>} loaded=${open_loaded:-<missing>} bookmarks=${open_bookmarks:-<missing>} (reason=$(field "$open_line" reason))"
  fi
  jump_line=$(grep -F 'review: bookmark: jump status=' "$PROD_OUT/run2.trace" | head -1)
  jump_status=$(field "$jump_line" status)
  if [ "$jump_status" = "applied" ]; then ok "a stored bookmark jumps: status=applied"; else bad "bookmark jump status=${jump_status:-<missing>} (reason=$(field "$jump_line" reason))"; fi
  jump_tick=$(field "$jump_line" tick)
  landed=$(grep -F 'review: workspace: state ' "$PROD_OUT/run2.trace" | tail -1 | sed -n 's/.*[[:space:]]tick=\([0-9-]*\).*/\1/p')
  if [ -n "$jump_tick" ] && [ "$jump_tick" = "$bm_tick" ]; then
    ok "the jump reports landing on the bookmark's own tick ($jump_tick)"
  else
    bad "the jump reports tick=${jump_tick:-<missing>} but the bookmark says ${bm_tick:-<missing>}"
  fi
  if [ -n "$landed" ] && [ "$landed" = "$bm_tick" ]; then
    ok "the paused transport still reads the bookmark's tick ($landed) after the jump"
  else
    bad "after the pause the transport reads tick=${landed:-<missing>} while the bookmark says ${bm_tick:-<missing>}"
  fi
  exit_line=$(grep -F 'review: workspace: exit ' "$PROD_OUT/run2.trace" | tail -1)
  if [ "$(field "$exit_line" autosave)" = "saved" ] && [ "$(field "$exit_line" close)" = "clean" ]; then
    ok "the run leaves the file saved and the session closed cleanly"
  else
    bad "exit line: $(printf '%s' "$exit_line" | sed 's/^ *//')"
  fi

  # Negative control 1: a file whose demoHash is not this demo must be refused by
  # name and must not be rewritten. A gate that only proves the happy path would
  # pass on a product that ignored the hash entirely.
  cp "$WS" "$PROD_OUT/ws-badhash.ini"
  good_sha=$(sed -n 's/^demoHash=//p' "$PROD_OUT/ws-badhash.ini" | head -1)
  fake_sha=$(printf 'a%.0s' $(seq 1 64))
  sed -i "s/^demoHash=$good_sha\$/demoHash=$fake_sha/" "$PROD_OUT/ws-badhash.ini"
  seeded_sha=$(sed -n 's/^demoHash=//p' "$PROD_OUT/ws-badhash.ini" | head -1)
  before_bytes=$(sha256sum "$PROD_OUT/ws-badhash.ini" | cut -d' ' -f1)
  run_product run3 "$PROD_OUT/ws-badhash.ini" "$PROD_OUT/run3.script"
  mismatch_line=$(grep -F 'review: workspace: open state=' "$PROD_OUT/run3.trace" | head -1)
  if [ "$(field "$mismatch_line" state)" = "hash_mismatch" ] && [ "$(field "$mismatch_line" loaded)" = "0" ]; then
    ok "a workspace from another demo is refused by name (state=hash_mismatch)"
  else
    bad "wrong-demo open state=$(field "$mismatch_line" state) (want hash_mismatch)"
  fi
  after_bytes=$(sha256sum "$PROD_OUT/ws-badhash.ini" | cut -d' ' -f1)
  if [ "$before_bytes" = "$after_bytes" ]; then
    ok "the refused file is not rewritten"
  else
    bad "the refused file was rewritten by a run that could not load it"
  fi
  if [ "$seeded_sha" = "$fake_sha" ]; then ok "the seeded hash really was the wrong one (the control is not a no-op)"; else bad "the seeded hash edit did not take"; fi

  # Negative control 2: a file that is not a workspace must be named corrupt.
  printf 'this is not a workspace\n' > "$PROD_OUT/ws-corrupt.ini"
  run_product run4 "$PROD_OUT/ws-corrupt.ini" "$PROD_OUT/run3.script"
  corrupt_line=$(grep -F 'review: workspace: open state=' "$PROD_OUT/run4.trace" | head -1)
  if [ "$(field "$corrupt_line" state)" = "corrupt" ] && [ "$(field "$corrupt_line" loaded)" = "0" ]; then
    ok "a corrupt workspace is refused by name (state=corrupt)"
  else
    bad "corrupt open state=$(field "$corrupt_line" state) (want corrupt)"
  fi
fi

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
