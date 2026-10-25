#!/usr/bin/env bash
# review-workspace-mutations.sh -- prove the T71/T72 gate readings can go red.
#
# SOUL.md rule 1: a criterion that has never been seen to fail is not evidence.
# review-workspace-check.sh asserts the workspace file roundtrip, the demo-hash
# binding, corrupt/old-schema refusal, bookmark identity (tick:packet:bit),
# sort order, transaction rollback and persistence. This script reintroduces
# each defect, rebuilds, and requires the gate to go red with a NAMED reason,
# then restores the file BYTE-FOR-BYTE (cmp -s) and requires the gate green.
#
# RULES THIS SCRIPT KEEPS (problems/INDEX.md)
#   * every FAIL line inside a mutation run is counted, not just the expected
#     one: the count must rise above the baseline's 0 (rule 2);
#   * the patch must actually appear where it is supposed to (rule 6), and the
#     restore is verified with cmp -s (rule 5);
#   * the tree is restored from an EXIT trap even if the run is interrupted, so
#     a killed run cannot leave a mutant binary behind (rule 5).
#
# MUST RUN UNDER THE SHARED HOST LOCK (it builds and runs the probe):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t71 \
#     bash -c 'cd /d/TF2_Native_Worktrees/t71-workspace && bash review-workspace-mutations.sh'
#
# Usage: bash review-workspace-mutations.sh [case ...]   (default: m1 m2 m3 m4 m5 m6)
# Exit:  0 = every case went red for its named reason, restored byte-for-byte,
#            and the baseline came back green.
set -uo pipefail
cd "$(dirname "$0")"

OUT=evidence/t70-t72/runtime/review-workspace/mutations
BACKUP=.scratch/review-workspace-mutations
mkdir -p "$OUT" "$BACKUP"
GATE=review-workspace-check.sh

TOUCHED=()   # "file|backup" pairs; the trap must not guess the backup name.
restore_all() {
  local rc=$? entry f b
  for entry in "${TOUCHED[@]:-}"; do
    [ -n "$entry" ] || continue
    f="${entry%%|*}"; b="${entry#*|}"
    [ -f "$b" ] || continue
    if cmp -s "$b" "$f"; then
      continue
    fi
    cp -p "$b" "$f" 2>/dev/null || true
    cmp -s "$b" "$f" || echo "TRAP-RESTORE-MISMATCH $f" >&2
    # Touch the restored file so build-target.sh really recompiles it: a
    # restored .cpp older than the mutant object would keep the mutant linked.
    touch "$f" 2>/dev/null || true
  done
  return $rc
}
trap restore_all EXIT INT TERM

# Applies one textual replacement. Sources are CRLF, so patterns written with LF
# are converted before matching. Fails loudly when the anchor is absent.
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

BK=""   # set by backup_file (a command substitution would lose TOUCHED)
backup_file() { # backup_file <file> <tag>
  BK="$BACKUP/$2.orig"
  cp -p "$1" "$BK" || return 1
  TOUCHED+=("$1|$BK")
}

rebuild() {
  # m5/m6 mutate native/src/review_session.cpp, which only the product links, so
  # a probe-only rebuild would leave the mutant out of the gate's section 8.
  bash build-target.sh review_workspace_probe tf2_demo_native
}

count_fails() { # count_fails <log>
  local n
  n=$(grep -c '^FAIL ' "$1" 2>/dev/null || true)
  printf '%s' "${n:-0}"
}

run_gate() { # run_gate <log>
  local log="$1"
  REVIEW_WORKSPACE_OUT="${log%.log}" bash "$GATE" > "$log" 2>&1
  echo $?
}

fail_all=0
BASELINE_FAILS=-1

if [ "${MUTATION_VERIFY:-0}" = "1" ]; then
  echo "=== verify-patches: anchors only, no build, no gate (MUTATION_VERIFY=1) ==="
else
echo "=== baseline: the unmutated tree must be green before any case ==="
BASE_LOG="$OUT/baseline.log"
BASE_RC=$(run_gate "$BASE_LOG")
BASELINE_FAILS=$(count_fails "$BASE_LOG")
if [ "$BASE_RC" = "0" ] && [ "$BASELINE_FAILS" = "0" ]; then
  echo "  OK   baseline rc=0 FAIL-count=0 (log $BASE_LOG)"
else
  echo "  FAIL baseline rc=$BASE_RC FAIL-count=$BASELINE_FAILS (log $BASE_LOG)"
  sed 's/^/       | /' "$BASE_LOG" | grep -E 'FAIL|NOT RUN' | head -20
  fail_all=1
fi
if [ "$fail_all" = "1" ]; then
  echo "FAIL review-workspace-mutations: baseline is not green; mutations would not mean anything."
  exit 1
fi
fi

# ---------------------------------------------------------------------------
# run_case <tag> <file> <find> <replace> <marker> <expected FAIL pattern>...
# ---------------------------------------------------------------------------
run_case() {
  local tag="$1" file="$2" find="$3" replace="$4" marker="$5"; shift 5
  local log="$OUT/$tag.log"
  echo
  echo "=== case $tag: mutate $file ==="
  if [ "${MUTATION_VERIFY:-0}" = "1" ]; then
    # Anchor check only: apply the patch to a COPY, prove the marker appears,
    # and leave the real tree alone. Used before taking the host lock.
    local tmp="$BACKUP/verify-$tag.src"
    cp -p "$file" "$tmp" || { echo "  FAIL could not copy $file"; fail_all=1; return; }
    if ! patch_in "$tmp" "$find" "$replace"; then
      echo "  FAIL patch anchor not found in $file"; fail_all=1; return
    fi
    if ! grep -qF "$marker" "$tmp"; then
      echo "  FAIL the patch is not present in $file after the edit (rule 6)"; fail_all=1; return
    fi
    echo "  OK   anchor applies and marker appears (dry run, tree untouched)"
    rm -f "$tmp"
    return
  fi
  backup_file "$file" "$tag" || { echo "  FAIL could not back up $file"; fail_all=1; return; }
  local backup="$BK"
  if ! patch_in "$file" "$find" "$replace"; then
    echo "  FAIL patch anchor not found in $file"; fail_all=1; return
  fi
  if ! grep -qF "$marker" "$file"; then
    echo "  FAIL the patch is not present in $file after the edit (rule 6)"; fail_all=1; return
  fi
  echo "  patched (backup $backup)"
  if ! rebuild; then echo "  FAIL rebuild after mutation"; fail_all=1; return; fi
  local rc fails
  rc=$(run_gate "$log")
  fails=$(count_fails "$log")
  echo "  gate rc=$rc FAIL-count=$fails (baseline $BASELINE_FAILS)"
  if [ "$rc" != "0" ] && [ "$fails" -gt "$BASELINE_FAILS" ]; then
    echo "  OK   gate went red and the FAIL count rose"
  else
    echo "  FAIL gate did not go red: rc=$rc FAIL-count=$fails"
    fail_all=1
  fi
  local pattern
  for pattern in "$@"; do
    if grep -qF "$pattern" "$log"; then
      echo "  OK   named reason present: $pattern"
    else
      echo "  FAIL named reason missing: $pattern"
      sed 's/^/       | /' "$log" | grep -E 'FAIL' | head -10
      fail_all=1
    fi
  done
  # restore byte-for-byte and prove it
  cp -p "$backup" "$file"
  if cmp -s "$backup" "$file"; then
    echo "  OK   restored byte-identical (cmp -s)"
  else
    echo "  FAIL restore differs from the backup"; fail_all=1
  fi
  touch "$file"
  if ! rebuild; then echo "  FAIL rebuild after restore"; fail_all=1; return; fi
  local back_rc back_fails
  back_rc=$(run_gate "$OUT/$tag-restored.log")
  back_fails=$(count_fails "$OUT/$tag-restored.log")
  if [ "$back_rc" = "0" ] && [ "$back_fails" = "0" ]; then
    echo "  OK   baseline green again after restore (FAIL-count=0)"
  else
    echo "  FAIL baseline not green after restore: rc=$back_rc FAIL-count=$back_fails"
    sed 's/^/       | /' "$OUT/$tag-restored.log" | grep -E 'FAIL' | head -10
    fail_all=1
  fi
}

# m1: skip the demo-hash binding. The workspace was written for demo A; opening
# it with demo B must be refused by name. With the check gone the store loads
# the wrong demo's bookmarks, which is exactly the silent-apply defect.
case_m1() {
  run_case m1 native/src/review_workspace.cpp \
"  if (document.demoHash != actualHash) {" \
"  if (document.demoHash == actualHash && document.demoHash.empty()) {" \
    "document.demoHash == actualHash && document.demoHash.empty()" \
    "step load-hash-mismatch: result=opened" \
    "expect hash_mismatch_named: ok=0" \
    "expect hash_mismatch_save_refused: ok=0"
}

# m2: treat an unreadable file as an empty but valid workspace. A corrupt file
# (bad record, truncated section) must be refused by name and its bytes kept;
# with the mutant it silently loads as empty and the next save would overwrite
# the user's file.
case_m2() {
  run_case m2 native/src/review_workspace.cpp \
"  if (!parse(text, document, reason, state)) {
    loadState_ = state;
    result.state = state;
    result.reason = reason;
    result.storedHash = document.demoHash;
    result.bookmarkCount = document.bookmarks.size();
    if (state == WorkspaceOpenState::OldSchema) {
      migrationCandidate_ = std::move(document);
      hasMigrationCandidate_ = true;
    }
    return result;
  }" \
"  if (!parse(text, document, reason, state)) {
    // mutant: an unreadable file becomes an empty but valid workspace
    document = WorkspaceDocument{};
    document.demoHash = actualHash;
    state = WorkspaceOpenState::Opened;
    reason = \"loaded\";
  }" \
    "document = WorkspaceDocument{};" \
    "step load-corrupt: result=opened" \
    "expect corrupt_named: ok=0" \
    "expect corrupt_save_refused: ok=0"
}

# m3: sort by tick must be ascending tick, then packet, then bit. Returning the
# insertion order is the natural bug: the store appends, and the list looks
# plausible until a bookmark is added out of order (cursor 100 then 50).
case_m3() {
  run_case m3 native/src/review_bookmarks.cpp \
"    case ReviewSort::Tick:
      std::sort(rows.begin(), rows.end(), [](const WorkspaceBookmarkRecord& a, const WorkspaceBookmarkRecord& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.eventPacketOrdinal != b.eventPacketOrdinal) return a.eventPacketOrdinal < b.eventPacketOrdinal;
        if (a.eventMessageBit != b.eventMessageBit) return a.eventMessageBit < b.eventMessageBit;
        return a.id < b.id;
      });
      break;" \
"    case ReviewSort::Tick:
      // mutant: leave the list in insertion order
      break;" \
    "mutant: leave the list in insertion order" \
    "step list [order=tick ids=2,1,3,4,5]" \
    "expect sort_ascending: ok=0"
}

# m4: the T68 identity of an event is tick:packetOrdinal:messageBit, not the
# tick alone. Three events on tick 300 are three rows; collapsing the key to the
# tick makes them indistinguishable in the list and in the persisted file.
case_m4() {
  run_case m4 native/src/review_bookmarks.cpp \
"  return std::to_string(record.tick) + \":\" + std::to_string(record.eventPacketOrdinal) + \":\" +
         std::to_string(record.eventMessageBit);" \
"  return std::to_string(record.tick);" \
    "return std::to_string(record.tick);" \
    "expect same_tick_keys_differ: ok=0" \
    "expect bookmarks_persist_eventkeys: ok=0"
}

# m5: the file is written without ever reading the camera back. The store is
# fine and the probe's roundtrip still passes -- only the PRODUCT's file loses
# the section, which is why this defect can only be seen from section 8.
case_m5() {
  run_case m5 native/src/review_session.cpp "  store_.markDirty();
  captureEnvironment();
}" "  store_.markDirty();
  // mutant: the camera section is never captured
}"     "// mutant: the camera section is never captured"     "the camera section is empty"     "slot 2 saved="
}

# m6: a jump that reports applied without moving the transport. The bookmark
# store still says applied and the row is still listed, so only the landed tick
# disagrees -- the exact shape of a silent lie the file cannot reveal.
case_m6() {
  run_case m6 native/src/review_session.cpp "bool ReviewSession::seek(std::int32_t tick) {
  return ports_.seek(tick);
}" "bool ReviewSession::seek(std::int32_t tick) {
  // mutant: the seek is reported, never performed
  (void)tick;
  return true;
}"     "// mutant: the seek is reported, never performed"     "after the pause the transport reads tick=" "the jump reports tick="
}

CASES="${*:-m1 m2 m3 m4 m5 m6}"
for c in $CASES; do
  case "$c" in
    m1) case_m1 ;;
    m2) case_m2 ;;
    m3) case_m3 ;;
    m4) case_m4 ;;
    m5) case_m5 ;;
    m6) case_m6 ;;
    *) echo "unknown case: $c"; fail_all=1 ;;
  esac
done

echo
if [ "$fail_all" = "0" ]; then
  if [ "${MUTATION_VERIFY:-0}" = "1" ]; then
    echo "PASS review-workspace-mutations (verify): every anchor applies and its marker appears; tree untouched."
  else
    echo "PASS review-workspace-mutations: $CASES each went red for its named reason; every file restored byte-identically; baseline green again."
  fi
else
  echo "FAIL review-workspace-mutations: see the FAIL lines above."
fi
exit "$fail_all"
