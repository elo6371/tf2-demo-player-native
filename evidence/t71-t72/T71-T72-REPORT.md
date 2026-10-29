# T71 / T72 — the review workspace and the bookmark store

Status: store + product wiring implemented and gated on the integrated tree
(D:/TF2_Native_Test, branch p0-entity-protocol). Commit: see PROGRESS.txt.

## 1. What the tasks asked for

T71 (the task list that defined it was deleted in the 2026-10-29 cleanup; what follows is the
requirement summary this report has always carried) asks for a versioned per-demo
workspace bound to the demo's content hash, with open / explicit save / autosave / rename,
and named results for every way the file can fail to load: hash mismatch, corrupt file,
older schema, moved path, unsaved exit. The saved state covers camera bookmarks, the current
shot and target, HUD panel visibility and layout, the minimap fold state, the event filter,
the logs.tf URL, and the playback position and speed marked separately from the demo data.

T72 (:423-434) asks for bookmarks that carry a title, a note, an event-type tag and, where
available, a player identity plus a camera reference; a list sorted by time with search,
filter, edit, delete and jump; creation from the verified T64 events returning to the same
tick; two events on the same tick must stay distinguishable; the records live in the T71
workspace and survive a close and reopen; and negative controls (empty list, duplicate tick,
no identity, corrupt note, mutated-no-write) so an empty list cannot pass as green.

## 2. What was delivered

| file | lines | what it is |
|---|---|---|
| native/include/workspace_types.h | 255 | schema-1 document model, bookmark records, percent codec, UTF-8 validator |
| native/include/review_workspace.h | 136 | WorkspaceStore: open/create/save/autosave/rename/close/migrate + named status enums |
| native/src/review_workspace.cpp | 1167 | deterministic INI serializer/parser, atomic temp+MoveFileExW write, read-only refusal, old-schema migration |
| native/include/review_bookmarks.h | 204 | store + eventKey identity (tick:packetOrdinal:messageBit), add/update/remove/query/sort/jump |
| native/src/review_bookmarks.cpp | 565 | bookmark logic, players/identity tracing, jump with camera-then-seek and rollback |
| native/include/review_session.h | 124 | ReviewSession: the layer the product drives (ports, document, bookmarks, apply/save/finish) |
| native/src/review_session.cpp | 384 | session logic, playback capture/apply, environment capture/apply hooks |
| native/tools/review_workspace_probe.cpp | 935 | store-level probe: 104 named assertions + 169 field comparisons |
| review-workspace-check.sh | 556 | the gate: sections 1-7 drive the stores, section 8 drives the product |
| review-workspace-mutations.sh | 319 | six mutants, each rebuilt, each required to red a named assertion |

Product wiring in native/src/main.cpp (+254 lines): a ProductReviewPorts implementation over
the live playback state and camera director, beginReviewSession() called with the demo hash
and the decoded game events, a --workspace <file> flag, twelve test-only script commands
(workspace-report, workspace-apply, workspace-save, bookmark-add, bookmark-add-event,
bookmark-list, bookmark-query, bookmark-update, bookmark-remove, bookmark-jump, save-bookmark,
plus session save/apply), and an exit hook that calls finish() so the session's close status is
reported. native/CMakeLists.txt gains three sources for the product target and the probe target.

## 3. Design decisions and why

- The store never guesses. Every failure mode has its own name (WorkspaceOpenState::Missing,
  Corrupt, HashMismatch, OldSchema) and every refusal carries a reason string naming the key,
  path or hash involved. A boolean would have made "wrong demo" and "file deleted" the same
  answer, and the task's acceptance sentence distinguishes them.
- The demo hash is the identity, not the path. A workspace copied next to a moved demo still
  opens (movedPath is reported), while a workspace whose hash differs is refused even when the
  path matches.
- Writes are atomic: a temp file in the same directory, then MoveFileExW with
  MOVEFILE_REPLACE_EXISTING. A crash mid-save leaves the previous file intact, which is the
  property the "mutated-no-write" control exercises.
- The bookmark identity is (tick, packetOrdinal, messageBit), not the tick. Two events on one
  tick are common in a TF2 demo (a kill and its assist, a cap and a round end); keying by tick
  would silently merge them, and T72 asks explicitly that they stay distinguishable. The gate
  asserts same_tick_keys_differ and same_tick_jump_separate.
- The camera section is captured through a hook, because ReviewSession::begin() runs before the
  renderer exists. The first product smoke wrote valid=0 cameraReason=no-renderer; the fix is
  ReviewEnvironmentCapture, called at capture time (after the renderer is up) and paired with
  ReviewEnvironmentApply on load, so the eight T50 camera slots round-trip.

## 4. The product path, end to end (section 8 of the gate)

Four real runs of native/build-nmake/tf2_demo_native.exe against the snakewater demo, driven by
--camera-script files, with --workspace pointing at a fresh file:

run 1 (save-bookmark 2 "Gate Slot"; bookmark-add "Smoke" "note from the product"; workspace-save):
- "review: workspace: create status=created"
- the file exists after run 1 (3120 bytes), demoHash == the demo's own sha256 (308cc1453672...)
- camera section valid=1 reason=read-back-from-renderer, slotCount=8, slot.2.saved=1,
  slot.2.name=Gate%20Slot
- bookmark count=1 title=Smoke tick=19, with createdUtc/updatedUtc written

run 2 (pause; bookmark-list tick; bookmark-jump 1; workspace-report; quit):
- "review: workspace: open state=opened loaded=1 hash=... bookmarks=1 next=2 reason=loaded"
- "review: bookmark: jump status=applied id=1 tick=19 key=none reason=applied"
- the paused transport still reads tick 19 after the jump
- "review: workspace: exit open=1 autosave=saved close=clean bookmarks=1 dirty=0 reason=saved"

negative controls in the same section:
- a copy whose demoHash was replaced with 64 a's: state=hash_mismatch, loaded=0, and the file is
  byte-unchanged (sha256 before == after), plus an assertion that the seeded hash really was the
  fake one, so the control is not a no-op;
- a file containing the text "this is not a workspace": state=corrupt, loaded=0.

Total: assertions_ok=186 assertions_bad=0, REVIEW-WORKSPACE-CHECK=PASS (owner lead-t71-gate3,
2026-10-25 23:37, log .scratch/t71-gate3.log).

## 5. Two defects the new evidence found

### 5.1 The product could not read its own file (schema, found only by running it twice)

First two-run smoke: run 1 wrote the file, run 2 reported
"review: workspace: open state=corrupt loaded=0 ... reason=missing-key workspace.camera.slot.0.saved".
Cause: the writer keyed each camera slot by the record's number ("slot." + to_string(slot.slot),
1-based from the product) while the parser keyed by the array position ("slot." + to_string(index),
0-based). The probe had never seen it because its fixture built 0-based slots, so the codec agreed
with itself. Fix: keys are the position on both sides and the slot number travels as its own field;
the fixture is now 1-based with a comment saying why. File: native/src/review_workspace.cpp
(writer, parser), native/include/workspace_types.h (WorkspaceCameraSlot::slot default 1),
native/tools/review_workspace_probe.cpp (fixture).

### 5.2 A gate assertion that was a coin flip

The first version of section 8 asserted "the transport is on the bookmark's tick" by reading a
state line after the jump. The demo plays while the script runs, so the bookmark tick itself varied
with load speed (18 or 19) and the reading passed only when the two happened to agree: the 185/0 run
was luck, and the mutation suite's own baseline run then read "the jump landed on tick=19 but the
bookmark says 18" and refused to mutate anything. Fix: the script pauses before the jump and the
assertion reads the tick the jump operation itself reports, with the later state line as a second,
corroborating assertion. A reading that can pass and fail on identical code is worse than a failing
one, because it teaches everyone to re-run until it is green.

## 6. Mutation

Six mutants, each a real source edit, each rebuilt, each required to turn the gate red for a named
reason and then restored byte-identically with the gate green again. Same tree, same gate; the run at
00:08 (owner lead-t71-mut3) proved m1-m5 and the run at 00:39 (owner lead-t71-mut6) proved m6, because
the script's default case list had been `${*:-m1 m2 m3 m4}` and the fix was made between the
two runs; both logs are committed.

| mutant | the edit | the reading the gate printed |
|---|---|---|
| m1 | the hash check always matches | `step load-hash-mismatch: result=opened`, `expect hash_mismatch_named: ok=0`, `expect hash_mismatch_save_refused: ok=0` |
| m2 | a corrupt file is accepted as opened | `step load-corrupt: result=opened`, `expect corrupt_named: ok=0`, `expect corrupt_save_refused: ok=0` |
| m3 | sort by tick keeps insertion order | `step list [order=tick ids=2,1,3,4,5]`, `expect sort_ascending: ok=0` |
| m4 | the event key collapses to the tick | `expect same_tick_keys_differ: ok=0`, `expect bookmarks_persist_eventkeys: ok=0` |
| m5 | the camera section is never captured | `the camera section is empty`, `slot 2 saved=` |
| m6 | the seek reports applied without moving | `the jump reports tick=12 but the bookmark says 30`, `after the pause the transport reads tick=12 while the bookmark says 30` | |

Verdicts: `MUT-RC=0` for the five-case run, MUT-RC=0 for the m6-only re-run (owner lead-t71-mut6), every restore `byte-identical`, and the
baseline green again afterwards both times. The two mutants that only the PRODUCT path can see are m5
and m6: the store round-trip passes under both of them, which is why section 8 exists.

Two of the suite's own defects were found by running it and are fixed here: the default case list
silently skipped m5 and m6 while the PASS sentence claimed `m1-m6` (now the sentence prints the
case list it actually ran), and m6's named pattern had been copied from an OK line rather than from the
FAIL text the assertion prints (now `after the pause the transport reads tick=` and
`the jump reports tick=`).

## 7. Limits and what is not verified

What is proven, and by what:

- the store is driven by a probe for every named state (missing, corrupt, hash mismatch, old schema,
  read-only, rename, autosave, unsaved exit) and by the PRODUCT for the create / open / jump / autosave /
  close path and for two negative controls;
- the product path runs four real processes against one demo and one workspace file;
- the bookmark identity is `tick:packetOrdinal:messageBit` and the same-tick case is asserted at store
  level (`same_tick_keys_differ`, `same_tick_jump_separate`) but not yet through the product.

What is NOT proven, and must not be read as proven:

1. **No interactive UI.** The product is driven through `--camera-script`, an explicitly test-only channel
   (`native/src/main.cpp:841`"TEST-ONLY"). There is no menu, no clickable bookmark list, no keyboard
   binding for these commands; the store and the session are real, the presentation is not written.
2. **One demo, one map.** Every product assertion is the snakewater demo. The odyssey document exists only
   in probe fixtures.
3. **Only the camera section is proven end to end.** The HUD, minimap, event-filter, logs-URL and playback
   sections are serialised and parsed with field-level assertions in the probe, but a real run does not
   write them, because those panels need a window that a hidden-window capture does not create.
4. **Event-created bookmarks are store-level only.** Section 8 adds a manual bookmark through the product;
   `bookmark-add-event` exists as a command and is exercised by the probe against decoded T64 events, but
   the product-side path is not asserted.
5. **Migration has no real input.** Old-schema loading is exercised with a synthetic schema-0 document; no
   pre-T71 file exists anywhere in the tree or in the user's data.
6. **No concurrency.** Two product instances on one workspace are not tested; the atomic write protects
   against a crash, not against a second writer.
7. **Autosave cadence is not measured.** The exit-time autosave is asserted (`autosave=saved`); there is no
   assertion about how often a periodic autosave fires or how large a bookmarks file may get.

## 8. Gates and chain wiring

- review-workspace-check.sh: 186 assertions, 0 failures, REVIEW-WORKSPACE-CHECK=PASS.
- verify-all.sh step 30/30 runs this gate and pins WS_RC=0, the PASS line and WS_OK=186.
- verify-all.sh exe_count pin moved 52 -> 53 because of the new review_workspace_probe target.
