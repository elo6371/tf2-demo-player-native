# T71 workspace + T72 bookmark store — implementation report

- Worktree: D:/TF2_Native_Worktrees/t71-workspace
- Branch: task/t71-workspace
- HEAD: 7b447a3c885e65eeef482473842d61a04314a09a ("Merge T60: third-person player model ...") — no commit made, no push.
- Scope: per-demo workspace INI document (T71 state) + bookmark store (T72) + probe gate + mutation suite, all at store level. Product UI/CLI wiring (main.cpp --workspace*/--bookmark*) is DEFERRED to integration and is NOT VERIFIED here.

## Deliverables (working tree)

| Path | Lines | Role |
| --- | --- | --- |
| native/include/workspace_types.h | 251 | schema 1 document model: camera/HUD/minimap/eventFilter/logs/playback, bookmark records, percent codec, UTF-8 validator |
| native/include/review_workspace.h | 136 | WorkspaceStore: path identity by demo sha256, create/open/save/autosave/rename/close/migrate, named open/save/new/autosave/rename/close states |
| native/src/review_workspace.cpp | 1160 | deterministic INI serializer/parser (no Win32 profile API), atomic temp+MoveFileExW write, read-only refusal, strict validation, old-schema migration candidate |
| native/include/review_bookmarks.h | 204 | ReviewBookmarkStore: addManual/addEvent/update/remove/sorted/query/jump with ReviewJumpPorts transaction, enums, counters |
| native/src/review_bookmarks.cpp | 565 | eventKey tick:packetOrdinal:messageBit identity, duplicate detection, sort (tick/created/title), search/filter, jump camera-then-seek rollback |
| native/tools/review_workspace_probe.cpp | 932 | --workspace-test probe: 104 named assertions + 169 field comparisons, synthetic demo bytes and events |
| review-workspace-check.sh | 406 | gate (minimap conventions): NOT RUN never PASS; probe assertion failures surface as FAIL lines |
| review-workspace-mutations.sh | 291 | m1-m4 defect re-injection, named-reason red requirement, cmp -s restore, baseline-green recheck |
| native/CMakeLists.txt | +14 | review_workspace_probe target (probe + review_workspace.cpp + review_bookmarks.cpp + install_manifest.cpp), /W4 /permissive- /EHsc /utf-8 |
| PROGRESS.txt | +9 | step log |

No existing product source was modified; product binaries are untouched.

## Commands and exit codes (all under hostlock owner=t71)

| Command | Exit | Note |
| --- | --- | --- |
| hostlock run t71 bash -c 'bash build-target.sh review_workspace_probe' (attempt 1) | 2 | probe: C2059/C2661 — variable named `far` (MSVC legacy keyword). Fixed: farRecord. |
| same (attempt 2) | 2 | review_workspace.cpp:188 C2663 — SectionValues::take() const mutating std::set consumed. Fixed: mutable consumed. |
| same (attempt 3) | 0 | build clean |
| hostlock run t71 bash -c 'bash review-workspace-check.sh' | 1 | first green-ish run: 4 bad — probe rc=1 from filter_player_entindex, plus playback step ambiguity. Both fixed: entindex:3 matches ids 3,4 (hurt reuses death players); playback step now carries rollback=1. |
| build + bash review-workspace-check.sh | 0 | assertions_ok=167 assertions_bad=0; probe 104 assertions 0 failed; 169 roundtrip fields 0 mismatched |
| bash review-workspace-mutations.sh (under hostlock) | 0 | m1-m4 PASS |

Logs: .scratch/gate-run.log, .scratch/mutations-run.log; per-case evidence under evidence/t70-t72/runtime/review-workspace/mutations/.

## Gate readings (final)

- probe: native/build-nmake/review_workspace_probe.exe --workspace-test rc=0, 309 lines
- summary: add=5 update=1 delete=1 refused=6 loadFailures=0 saveFailures=0 jumpFailures=3 duplicateEvent=1 fields=169 fieldMismatches=0
- 167 OK / 0 FAIL; REVIEW-WORKSPACE-CHECK=PASS RC=0
- Roundtrip: new=created, save=ok, load=opened, resave=ok (byte-identical), load-moved=opened; 31 gate field_is values (schema/name/hashAlgo/hash/autosave, camera fov 95.5 yaw 45.5 tick 1234 8 slots, hud 125/3 panels, minimap koth_bagel zoom 1.5, eventFilter player_death userid:7, logs https://logs.tf/123456, playback 0.5@987, extraKeyCount/extraSectionCount) all match.
- Hash mismatch: open state=hash_mismatch, named reason, save refused, file bytes intact.
- Corrupt: state=corrupt named, save refused, bytes intact; header-only patch also corrupt.
- Old schema: state=old_schema, migrate rewrites header + every record hash/recordVersion, reopens; future version 99 refused reason=unsupported-version.
- Bookmarks: manual tick=cursor 100/50 -> ids 1,2; events 300:2:5 / 300:2:9 / 300:3:5 -> ids 3,4,5; duplicate already_exists existing=3; list order=tick ids=2,1,3,4,5; query/filter/tick range; update title/note only (tick/createdAt/demoHash immutable); delete id2 count=4, repeat not_found; jump applied key=300:2:5 and 300:3:5 at tick 300; out-of-range keeps cursor/camera; playback refusal rolls camera back (apply=1 restore=1 seek=1); camera refusal restores and never seeks; hash-mismatch/not-loaded/not-found jumps refused; persistence count=4 eventKeys preserved; close unsaved/clean; rename; missing; wrong-path; no-hash; already-exists; save-not-loaded; read-only refused with bytes intact.

## Mutation results (each: gate red with named reason, then restored)

| Case | Defect | FAIL count | Named reasons seen | Restore |
| --- | --- | --- | --- | --- |
| m1 | demo-hash check skipped | 5 | step load-hash-mismatch result=opened; expect hash_mismatch_named ok=0; hash_mismatch_save_refused ok=0 | cmp -s OK |
| m2 | corrupt file treated as empty workspace | 16 | step load-corrupt result=opened; corrupt_named ok=0; corrupt_save_refused ok=0 | cmp -s OK |
| m3 | tick sort left in insertion order | 5 | step list [order=tick ids=2,1,3,4,5]; sort_ascending ok=0 | cmp -s OK |
| m4 | eventKey collapsed to tick | 9 | same_tick_keys_differ ok=0; bookmarks_persist_eventkeys ok=0 | cmp -s OK |

Baseline before mutations: rc=0 FAIL-count=0. After every restore: rebuild rc=0, gate rc=0 FAIL-count=0. No mutant markers remain; m1.orig/m2.orig (review_workspace.cpp) and m3.orig/m4.orig (review_bookmarks.cpp) are byte-identical to the current sources (cmp OK). Mutations run: MUT-RC=0.

## Lock end state

Last command exited via LOCKED-COMMAND-EXIT rc=0 owner=t71; hostlock status immediately after: no ticket for t71 in queue, reservation=none, product_processes=0 (transient holder lead-t70 heartbeat only). No product process was started by this task.

## NOT VERIFIED / coverage limits

- Product wiring: no main.cpp args, no UI, no real .dem opened; T71 ops new/save/autosave/rename/open and T72 add/jump are store-level only.
- T72 acceptance rows needing a real demo: A2 (real eventKey from t72-event-map.tsv), A3 (real tick-334 multi-event), A9 (real cursor/settle), A11 (real close/reopen) — NOT VERIFIED.
- Store-level rows verified with synthetic fixtures: A1 (manual tick=cursor + seek-pending refusal), A4, A5, A8 (tick sort only; created/title sort implemented but not asserted), A10, A12, A13, A14, A15 (empty workspace load/save nextId=1), A16 (read-only refusal path), A18 (list-item/trace field presence).
- Not separately asserted: A6 note/event-name text search (only title hit asserted), A7 combined AND filter, A17 delete-last-then-reopen nextId (empty workspace save/reopen is asserted; bookmark-list-to-zero persistence is not).
- Deviation noted: jump() also calls restoreCamera() when applyCamera returns false, so a camera refusal leaves the pre-jump camera (defensive rollback; DESIGN only requires rollback on playback refusal). Probe asserts applyCalls=1 restoreCalls=1 seekCalls=0 for that path.
- Gate/mutation scripts assume POSIX shell + python (patch_in) as used by the existing gate conventions.
