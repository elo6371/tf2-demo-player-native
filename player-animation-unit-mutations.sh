#!/usr/bin/env bash
# player-animation-unit-mutations.sh -- T61.1: the teeth of sections 1b/6 and 1c/6.
#
# WHY THIS SCRIPT EXISTS
# ----------------------
# The pixel harness (player-animation-pixel-mutations.sh) hardcodes
# GATE=player-animation-pixels.sh, and that gate never loads the sequence matcher
# or the entity-id encoder. The two unit sections that do load them live in
# player-animation-check.sh:
#
#   1b/6 matcher    11 assertions on model_pose_probe --matcher-selftest
#   1c/6 entity id   8 assertions on model_pose_probe --entity-id-selftest
#
# Those 19 assertions therefore had NO mutant that reddens them: m6-m11 run the
# pixel gate (which does not include 1b/6 or 1c/6) and m1-m5 mutate the clock and
# binding sources (which 1b/6 and 1c/6 do not read). An assertion with no mutant
# is a comment. This script mutates the two headers those sections read, rebuilds
# the PROBE -- the pixel harness deliberately rebuilds only the product -- and
# demands that the NAMED assertion red.
#
# Each case also makes STALE-BINARY=1 impossible as the reason: the probe is
# rebuilt AFTER the patch, and the log is checked for the token anyway.
#
# T61.1 review 3, P2: restoring the sources on exit was not enough. Each case
# compiles its mutant header into BOTH binaries, so an interrupt between the
# mutant rebuild and the restore left mutant binaries on disk and the next
# baseline would have run mutant code without saying so. On INT/TERM the trap
# now restores the sources, DELETES both binaries (a missing binary cannot pass
# for a fresh one -- the gate's executable check goes red at once), rebuilds
# them from the restored sources and prints INTERRUPT-RECOVERY=rebuilt, or
# NOT-FOR-ACCEPTANCE plus an $OUT/BINARIES-NOT-FOR-ACCEPTANCE.txt marker when
# the rebuild fails. A normal exit does not rebuild: the last case already did.
#
# T61.1 review 5, P1: a per-case restore that did not verify is a REFUSAL, not a
# warning. The run stops, the trap deletes the binaries and writes the marker,
# and the exit code says the tree is not clean -- a case that cannot be undone
# must never be followed by another build or by a "green" baseline.

# MUST RUN UNDER THE SHARED HOST LOCK (it builds).
# Usage: bash player-animation-unit-mutations.sh [case ...]  (default: u1 u2)
# Exit:  0 = every unit case reddened a NAMED assertion, the tree was restored
#            byte-for-byte, and the final baseline is green.
#        1 = otherwise.
set -uo pipefail
cd "$(dirname "$0")"

GATE=player-animation-check.sh
PROBE=native/build-nmake/model_pose_probe.exe
PRODUCT=native/build-nmake/tf2_demo_native.exe
SUITE_LABEL="the unit mutation suite"
T61_BUILD_TARGETS="model_pose_probe tf2_demo_native"
OBJ_ROOT=${T61_OBJ_ROOT:-native/build-nmake}
# T61.1 review 3, P2: overridable so player-animation-interrupt-demo.sh can run a
# case without writing into the cited evidence; the default is the cited path.
OUT=${T61_MUTATION_OUT:-evidence/t61-animation/unit-mutations}
# The gate writes its own logs under $OUT; keep the mutant runs out of the
# forward-gate evidence directory so a red mutant can never be mistaken for the
# tree's own numbers.
export PLAYER_ANIMATION_OUT="$OUT/runtime"
BACKUP=.scratch/t61-unit-mutations
# >>> SHARED-RECOVERY-STATE-MACHINE-BEGIN
# The recovery state machine below is IDENTICAL TEXT in
# player-animation-unit-mutations.sh and player-animation-pixel-mutations.sh --
# identical once line endings are normalised: this harness is LF, the pixel
# harness keeps CRLF, and no run compares the two files byte for byte. The
# final-round rule forbids a fifth file and a new abstraction, so the
# duplication stays and player-animation-recovery-branches.sh proves the
# equality by normalising CRLF to LF and then comparing the extracted text.
# Everything suite-specific arrives as variables set above this block:
# SUITE_LABEL, GATE, PROBE (the empty string for the pixel suite), PRODUCT,
# T61_BUILD_TARGETS, OBJ_ROOT, OUT, BACKUP.
if [ -n "$PROBE" ]; then BINWORD="both binaries"; else BINWORD="the product"; fi
if [ -n "$PROBE" ]; then BINS="$PROBE $PRODUCT"; else BINS="$PRODUCT"; fi
mkdir -p "$OUT" "$BACKUP"

# T61.1 review 7, P1: an exit status is not evidence. Review 7 reproduced the
# hole that leaves: when the bytes on disk already matched the backup the restore
# skipped touch altogether, and a touch injected to return 0 without moving the
# mtime was accepted as a restore -- yet build-target.sh:131 recompiles only when
# a header is newer than the stamp it writes at :167, so a stale mtime keeps the
# object that was compiled from the mutant. The stamp below is that same file; a
# restore now has to prove that the file it restored is newer than it.
STAMP=${T61_STAMP:-$OBJ_ROOT/.headers-stamp}
mtime_newer_than_stamp() { # mtime_newer_than_stamp <file>
  [ -f "$1" ] || return 1
  [ -f "$STAMP" ] || return 0
  [ "$1" -nt "$STAMP" ]
}

# T61.1 final round: a fresh binary is still not proof that the object it was
# linked from was recompiled. If an .obj compiled from the mutant survives, NMAKE
# relinks it and the binary still gets a new mtime. The proof is object-level,
# and it comes from the dependency file CMake itself writes: an object is only
# trusted when it is NEWER than the restored file it depends on.
object_paths_for() { # object_paths_for <restored file> -- objects that depend on it
  local f="$1" t d line obj dep want
  want=${f//\\//}
  while :; do case "$want" in /*) want=${want#/} ;; *) break ;; esac; done
  for t in $T61_BUILD_TARGETS; do
    d="$OBJ_ROOT/CMakeFiles/$t.dir"
    [ -f "$d/compiler_depend.make" ] || continue
    while IFS= read -r line; do
      line=${line%$'\r'}
      case "$line" in *": "*) ;; *) continue ;; esac
      obj=${line%%:*}
      dep=${line#*: }
      dep=${dep#\"}; dep=${dep%\"}
      dep=${dep//\\//}
      while :; do case "$dep" in /*) dep=${dep#/} ;; *) break ;; esac; done
      case "$dep" in
        "$want"|*/"$want")
          obj=${obj//\\//}
          printf '%s/%s\n' "$OBJ_ROOT" "$obj" ;;
      esac
    done < "$d/compiler_depend.make"
  done
}

verify_objects_are_fresh() { # verify_objects_are_fresh <code-prefix>
  local code="$1" entry f o seen n=0 bad=0
  for entry in $TOUCHED; do
    [ -n "$entry" ] || continue
    f=${entry%%|*}
    seen=0
    while IFS= read -r o; do
      [ -n "$o" ] || continue
      seen=$((seen + 1)); n=$((n + 1))
      if [ ! "$o" -nt "$f" ]; then
        echo "  FAIL $code-STALE-OBJECT $o is not newer than $f, which this run restored, so an object compiled from the mutant could be relinked rather than recompiled"
      note_cause "$code-STALE-OBJECT" "the object $o is not newer than the restored $f, so it may still be the object compiled from the mutant"
        bad=1
      fi
    done <<OBJEOF
$(object_paths_for "$f" | LC_ALL=C sort -u)
OBJEOF
    if [ "$seen" = "0" ]; then
      echo "  FAIL $code-NO-OBJECT-DEPENDENCY $f: no object under $OBJ_ROOT/CMakeFiles/<target>.dir depends on it, so a recompile cannot be proven"
      note_cause "$code-NO-OBJECT-DEPENDENCY" "no object that depends on the restored $f exists, so a recompile cannot be proven"
      bad=1
    fi
  done
  return "$bad"
}

TOUCHED=""
INTERRUPTED=0
RESTORE_FAILED=0
restore_all() {
  local entry f b
  for entry in $TOUCHED; do
    [ -n "$entry" ] || continue
    f=${entry%%|*}; b=${entry#*|}
    # T61.1 review 4, P1: a missing backup is a FAILED restore, not a no-op.
    if [ ! -f "$b" ]; then
      echo "  FAIL TRAP-RESTORE-MISSING-BACKUP $f (expected $b)" >&2
      note_cause TRAP-RESTORE-MISSING-BACKUP "the backup $b is missing, so the mutant cannot be undone"
      RESTORE_FAILED=1
      continue
    fi
    if [ ! -r "$b" ]; then
      echo "  FAIL TRAP-RESTORE-UNREADABLE-BACKUP $b" >&2
      note_cause TRAP-RESTORE-UNREADABLE-BACKUP "the backup $b exists but cannot be read, so the mutant cannot be undone"
      RESTORE_FAILED=1
      continue
    fi
    if ! cmp -s "$b" "$f"; then
      cp -p "$b" "$f" 2>/dev/null || true
      if ! cmp -s "$b" "$f"; then
        echo "  FAIL TRAP-RESTORE-MISMATCH $f" >&2
        note_cause TRAP-RESTORE-MISMATCH "the copy from $b did not verify, so the tree may still carry the mutant"
        RESTORE_FAILED=1
        continue
      fi
    fi
    # T61.1 review 7, P1: the bytes are the backup bytes now, but the mtime is
    # not evidence yet -- the old "cmp -s && continue" skipped the touch when the
    # bytes already matched. Touch every time, then require the result to be
    # newer than the stamp the next build compares against.
    if ! touch "$f" 2>/dev/null; then
      echo "  FAIL TRAP-RESTORE-TOUCH-FAILED $f" >&2
      note_cause TRAP-RESTORE-TOUCH-FAILED "the bytes were restored but the mtime could not be advanced, so a rebuild would skip the recompile"
      RESTORE_FAILED=1
    elif ! mtime_newer_than_stamp "$f"; then
      echo "  FAIL TRAP-RESTORE-MTIME-NOT-ADVANCED $f (the mtime is not newer than $STAMP)" >&2
      note_cause TRAP-RESTORE-MTIME-NOT-ADVANCED "touch returned 0, but the mtime of the file is not newer than the build stamp, so a rebuild would skip the recompile and keep the mutant object"
      RESTORE_FAILED=1
    fi
  done
}

# T61.1 review 5, P1: the per-case restore runs INSIDE the suite, and a failure
# there used to be recorded but then ignored -- the case still touched the file
# and rebuilt. This is the same state machine as restore_all, with one
# difference that matters: a failure is STICKY and it stops the run.
restore_case_sources() {
  local entry f b rc=0
  for entry in $TOUCHED; do
    [ -n "$entry" ] || continue
    f=${entry%%|*}; b=${entry#*|}
    if [ ! -f "$b" ]; then
      note_cause CASE-RESTORE-MISSING-BACKUP "the backup $b is missing, so the mutant cannot be undone"
      echo "  FAIL CASE-RESTORE-MISSING-BACKUP $f (expected $b): the mutant cannot be undone"
      RESTORE_FAILED=1; rc=1; continue
    fi
    if [ ! -r "$b" ]; then
      note_cause CASE-RESTORE-UNREADABLE-BACKUP "the backup $b exists but cannot be read"
      echo "  FAIL CASE-RESTORE-UNREADABLE-BACKUP $b"
      RESTORE_FAILED=1; rc=1; continue
    fi
    if ! cmp -s "$b" "$f"; then
      cp -p "$b" "$f" 2>/dev/null || true
      if ! cmp -s "$b" "$f"; then
        note_cause CASE-RESTORE-MISMATCH "the copy from $b did not verify, so the tree may still carry the mutant"
        echo "  FAIL CASE-RESTORE-MISMATCH $f: the copy from $b did not verify, so the tree may still carry the mutant"
        RESTORE_FAILED=1; rc=1; continue
      fi
    fi
    # T61.1 review 7, P1: touch every time (the old "cmp -s && continue" skipped
    # it when the bytes already matched), then prove the mtime moved past the
    # stamp the next build compares against. A touch that returns 0 without
    # moving the mtime is not a restore.
    if ! touch "$f" 2>/dev/null; then
      note_cause CASE-RESTORE-TOUCH-FAILED "the file is byte-identical but its mtime could not be advanced, so the next build would skip the recompile and keep the mutant object"
      echo "  FAIL CASE-RESTORE-TOUCH-FAILED $f: the file is byte-identical but its mtime could not be advanced, so the next build would skip the recompile and keep the mutant object"
      RESTORE_FAILED=1; rc=1
    elif ! mtime_newer_than_stamp "$f"; then
      note_cause CASE-RESTORE-MTIME-NOT-ADVANCED "touch returned 0, but the mtime of the file is not newer than the build stamp, so the next build would skip the recompile and keep the mutant object"
      echo "  FAIL CASE-RESTORE-MTIME-NOT-ADVANCED $f: touch returned 0, but the mtime is not newer than $STAMP, so the next build would skip the recompile and keep the mutant object"
      RESTORE_FAILED=1; rc=1
    fi
  done
  if [ "$rc" = "1" ]; then
    echo "  REFUSAL the tree could not be restored: no further build, no further case, no baseline. The EXIT trap deletes $BINWORD and writes $OUT/BINARIES-NOT-FOR-ACCEPTANCE.txt."
  fi
  return "$rc"
}

# T61.1 review 6, P1: the ways OUT of a case are one state machine, not four
# ad-hoc "fail_all=1; return" lines. Every abnormal path goes through refuse():
# the failure is STICKY, it names itself, no further build is attempted, and
# on_exit deletes the binaries, writes a marker that says WHY, and exits nonzero.
REFUSAL_CAUSE=""
note_cause() { # note_cause <code> <detail>: the FIRST reason wins
  [ -n "$REFUSAL_CAUSE" ] || REFUSAL_CAUSE="$1: $2"
}
refuse() { # refuse <code> <detail>
  note_cause "$1" "$2"
  RESTORE_FAILED=1
  fail_all=1
  echo "  FAIL $1: $2"
  echo "  REFUSAL the case cannot be undone ($1); no further build, no further case, no baseline."
}

# T61.1 review 3, P2: restoring the sources is NOT enough. An interrupt between
# "rebuild" and "restore + rebuild" leaves mutant binaries on disk, so the
# recovery DELETES them first (a missing binary cannot masquerade as a mutant),
# then rebuilds from the restored sources and reports which state the tree is in.
recover_binaries() {
  local b
  # T61.1 review 4, P2: "rm -f" is not proof of deletion (the script is not
  # "set -e"), so the recovery checks each binary is really gone before it
  # rebuilds or clears the marker.
  for b in $BINS; do rm -f "$b" 2>/dev/null || true; done
  for b in $BINS; do
    if [ -e "$b" ]; then
      note_cause RECOVERY-BINARY-DELETE-FAILED "the mutant binary $b survived deletion, so it must not be used as evidence"
      echo "  FAIL RECOVERY-BINARY-DELETE-FAILED $b: a mutant binary could NOT be deleted" >&2
      echo "  REFUSAL recovery cannot continue; refusing to rebuild or to call the tree clean. Delete it by hand, restore the sources from $BACKUP, then rebuild." >&2
      echo "INTERRUPT-RECOVERY=NOT-FOR-ACCEPTANCE: RECOVERY-BINARY-DELETE-FAILED ($b)" >&2
      printf '%s\n' "NOT-FOR-ACCEPTANCE: RECOVERY-BINARY-DELETE-FAILED: a mutant binary survived the recovery's delete ($b). Do not run a gate on this tree and do not quote a pass from it until the binary is gone and the sources are restored from $BACKUP." > "$OUT/BINARIES-NOT-FOR-ACCEPTANCE.txt"
      RESTORE_FAILED=1
      return 1
    fi
  done
  # T61.1 review 4: a rebuild is only safe when the restore VERIFIED. If it did
  # not, the mutant source is still on disk and a rebuild would compile it.
  if [ "$RESTORE_FAILED" = "1" ]; then
    echo "  REFUSAL recovery refused (${REFUSAL_CAUSE:-the source restore did NOT verify}); no rebuild was attempted." >&2
    echo "INTERRUPT-RECOVERY=NOT-FOR-ACCEPTANCE: ${REFUSAL_CAUSE:-the source restore did NOT verify}, so $BINWORD were DELETED rather than rebuilt -- a rebuild now would compile the mutant. Restore the sources from $BACKUP by hand, then rebuild before any gate run." >&2
    printf '%s\n' "NOT-FOR-ACCEPTANCE: $SUITE_LABEL refused (${REFUSAL_CAUSE:-the source restore did not verify}), so the sources may still carry a mutant and $BINWORD were DELETED. Do not run a gate on this tree and do not quote a pass from it until the sources are restored from $BACKUP and build-target.sh $T61_BUILD_TARGETS succeeds." > "$OUT/BINARIES-NOT-FOR-ACCEPTANCE.txt"
    return 1
  fi
  if rebuild > "$BACKUP/recovery-build.log" 2>&1; then
    if verify_case_build_is_fresh RECOVERY; then
      echo "INTERRUPT-RECOVERY=rebuilt: sources restored and $BINWORD rebuilt from them, with every object they touch newer than the restored file, so nothing mutant is left to run (log $BACKUP/recovery-build.log)" >&2
      rm -f "$OUT/BINARIES-NOT-FOR-ACCEPTANCE.txt"
    else
      note_cause RECOVERY-BUILD-NOT-FRESH "the recovery build did not prove that every restored source was recompiled"
      echo "  FAIL RECOVERY-BUILD-NOT-FRESH: a binary or an object is not newer than a source it restored" >&2
      echo "  REFUSAL the recovery build is not evidence; a mutant object could have been relinked rather than recompiled." >&2
      echo "INTERRUPT-RECOVERY=NOT-FOR-ACCEPTANCE: ${REFUSAL_CAUSE:-RECOVERY-BUILD-NOT-FRESH} (log $BACKUP/recovery-build.log)" >&2
      printf '%s\n' "NOT-FOR-ACCEPTANCE: $SUITE_LABEL refused (${REFUSAL_CAUSE:-RECOVERY-BUILD-NOT-FRESH}); the recovery build did not prove recompilation (see $BACKUP/recovery-build.log). Do not run a gate on this tree and do not quote a pass from it until build-target.sh $T61_BUILD_TARGETS recompiles the restored sources." > "$OUT/BINARIES-NOT-FOR-ACCEPTANCE.txt"
      RESTORE_FAILED=1
      return 1
    fi
  else
    note_cause RECOVERY-BUILD-FAILED "the recovery rebuild from restored sources failed"
    echo "  FAIL RECOVERY-BUILD-FAILED: sources were restored, but the rebuild failed (log $BACKUP/recovery-build.log)" >&2
    echo "  REFUSAL the recovery build failed; the binaries do not evidence this tree." >&2
    echo "INTERRUPT-RECOVERY=NOT-FOR-ACCEPTANCE: RECOVERY-BUILD-FAILED; rebuild before any gate run (log $BACKUP/recovery-build.log)" >&2
    printf '%s\n' "NOT-FOR-ACCEPTANCE: $SUITE_LABEL refused (RECOVERY-BUILD-FAILED) after sources were restored. Do not run a gate on this tree, and do not quote a pass from it, until build-target.sh $T61_BUILD_TARGETS succeeds. See $BACKUP/recovery-build.log" > "$OUT/BINARIES-NOT-FOR-ACCEPTANCE.txt"
    RESTORE_FAILED=1
    return 1
  fi
}

on_exit() {
  local rc=$?
  local recovery_failed=0
  trap - EXIT INT TERM HUP
  # T61.1 final-review P1: restore_all touches every file it restores, so it may
  # run only when the tree can still be carrying a mutant -- an interrupt, or a
  # restore that did not verify. On a clean exit it used to run too, and its
  # touch made every restored source NEWER than the objects and the binaries the
  # last case had just proven, so the exit itself invalidated the freshness
  # proof it left behind.
  if [ "$INTERRUPTED" = "1" ] || [ "$RESTORE_FAILED" = "1" ]; then
    restore_all
    # Either way the binaries must not stand as evidence.
    recover_binaries || recovery_failed=1
  else
    if ! verify_clean_exit; then
      recovery_failed=1
      # The refusal above set RESTORE_FAILED, so recover_binaries takes its
      # delete-and-mark branch: it deletes the binaries and writes the marker
      # instead of rebuilding from a tree the exit has just refused.
      recover_binaries || recovery_failed=1
    fi
  fi
  # T61.1 review 4, P2: a run that could not leave the tree clean must not exit
  # 0. A per-case refusal keeps its own 1; only an originally 0 run is upgraded
  # to 3, which says "the suite finished, the tree did not".
  if [ "$recovery_failed" = "1" ] || [ "$RESTORE_FAILED" = "1" ]; then
    [ "$rc" = "0" ] && rc=3
  fi
  exit "$rc"
}
# T61.1 review 4: closing a terminal sends SIGHUP, not SIGINT. 129 is the
# conventional exit for a HUP-terminated shell.
trap 'INTERRUPTED=1; exit 130' INT TERM
trap 'INTERRUPTED=1; exit 129' HUP
trap on_exit EXIT

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

BK=""
backup_file() { BK="$BACKUP/$2.orig"; cp -p "$1" "$BK" || return 1; TOUCHED="$TOUCHED $1|$BK"; }
# Both binaries: a mutant header is compiled into the probe AND the product, so
# leaving either older than a restored header would red the other gate's
# freshness assertion, and a mutant that dies at the freshness guard proves
# nothing about the assertion it was aimed at.
verify_rebuild_is_fresh() { # verify_rebuild_is_fresh <code-prefix> <binary>...
  local code="$1" bin entry f bad=0
  shift
  for bin in "$@"; do
    if [ ! -e "$bin" ]; then
      echo "  FAIL $code-NO-BINARY $bin: the rebuild did not produce it"
      note_cause "$code-NO-BINARY" "the rebuild did not produce $bin"
      bad=1
      continue
    fi
    for entry in $TOUCHED; do
      [ -n "$entry" ] || continue
      f=${entry%%|*}
      if [ ! "$bin" -nt "$f" ]; then
        echo "  FAIL $code-STALE-BINARY $bin: it is not newer than $f, which this run restored, so the object compiled from the mutant was relinked instead of recompiled"
        note_cause "$code-STALE-BINARY" "the binary $bin is not newer than the restored $f"
        bad=1
      fi
    done
  done
  return "$bad"
}

# The whole proof, in one call: the binaries are newer than every restored file
# AND every object CMake's dependency file says depends on a restored file is
# newer than it. Either half alone is repeatable by a relink.
verify_case_build_is_fresh() { # verify_case_build_is_fresh <code-prefix>
  local code="$1" bad=0
  if [ -n "$PROBE" ]; then
    verify_rebuild_is_fresh "$code" "$PROBE" "$PRODUCT" || bad=1
  else
    verify_rebuild_is_fresh "$code" "$PRODUCT" || bad=1
  fi
  verify_objects_are_fresh "$code" || bad=1
  return "$bad"
}

# T61.1 final-review P1: a normal exit must not touch a source. The touch in
# restore_all lands AFTER the last case's object proof, so the source would end
# up newer than the very objects and binaries it was proven against -- the tree
# on disk would no longer be the tree the proof described. A clean exit
# re-runs that proof instead: the source must still match its backup, and every
# object that depends on it plus the binaries must still be newer than it. When
# that no longer holds the exit refuses like any other unrecoverable case.
verify_clean_exit() {
  local entry f b bad=0
  for entry in $TOUCHED; do
    [ -n "$entry" ] || continue
    f=${entry%%|*}; b=${entry#*|}
    if ! cmp -s "$b" "$f"; then
      echo "  FAIL CLEAN-EXIT-SOURCE-DIFFERS $f: it does not match $b at exit, so a mutant is still on disk"
      note_cause CLEAN-EXIT-SOURCE-DIFFERS "the source $f does not match its backup $b at exit, so a mutant is still on disk"
      bad=1
    fi
  done
  if ! verify_case_build_is_fresh CLEAN-EXIT; then bad=1; fi
  if [ "$bad" = "1" ]; then
    refuse CLEAN-EXIT-NOT-FRESH "the tree at exit is not the tree the last case proved: a source does not match its backup, or an object or a binary is not newer than the source it was built from"
    return 1
  fi
  echo "  OK   clean exit: every restored source still matches its backup, and every object that depends on it plus $BINWORD is newer than it (nothing was touched after the proof)"
  return 0
}

rebuild() { bash build-target.sh $T61_BUILD_TARGETS; }
count_fails() { local n; n=$(grep -c '^  FAIL ' "$1" 2>/dev/null || true); : ${n:=0}; printf '%s' "$n"; }
count_oks() { local n; n=$(grep -c '^  OK ' "$1" 2>/dev/null || true); : ${n:=0}; printf '%s' "$n"; }
run_gate() { local log="$1"; bash "$GATE" > "$log" 2>&1; echo $?; }
# <<< SHARED-RECOVERY-STATE-MACHINE-END

fail_all=0
echo "=== baseline: the unmutated tree must be green before any case ==="
if ! rebuild; then
  echo "  FAIL baseline rebuild (a stale probe would red the freshness guard, not an assertion)"
  exit 1
fi
BASE_RC=$(run_gate "$OUT/baseline.log")
BASE_FAILS=$(count_fails "$OUT/baseline.log")
BASE_OKS=$(count_oks "$OUT/baseline.log")
if [ "$BASE_RC" = "0" ] && [ "$BASE_FAILS" = "0" ]; then
  echo "  OK   baseline rc=0 FAIL-count=0 OK-count=$BASE_OKS (log $OUT/baseline.log)"
  grep -E '^  OK   (BINARY-FRESH|matcher |entity id )' "$OUT/baseline.log" | sed 's/^/       /'
else
  echo "  FAIL baseline rc=$BASE_RC FAIL-count=$BASE_FAILS (log $OUT/baseline.log)"
  grep -E 'FAIL|FATAL' "$OUT/baseline.log" | head -20 | sed 's/^/       /'
  echo "FAIL player-animation-unit-mutations: baseline is not green; mutations would not mean anything."
  exit 1
fi

run_case() { # run_case <tag> <file> <find> <replace> <named assertion reason>...
  local tag="$1" file="$2" find="$3" replace="$4"; shift 4
  if [ "$#" -eq 0 ]; then
    echo "  FAIL case $tag lists no named assertion: a case with an empty expectation list checks nothing"
    fail_all=1; return
  fi
  local log="$OUT/$tag.log"
  echo
  echo "=== case $tag: mutate $file ==="
  backup_file "$file" "$tag" || { echo "  FAIL could not back up $file"; fail_all=1; return; }
  local backup="$BK"
  if ! patch_in "$file" "$find" "$replace"; then
    echo "  FAIL patch anchor not found in $file"; fail_all=1; return
  fi
  echo "  patched (backup $backup)"
  # A mutant that does not compile would make the gate fail on the PREVIOUS
  # binary, which proves nothing about the assertion. Demand a real compile.
  if ! rebuild; then
    # T61.1 review 6, P1: this branch used to restore, touch and rebuild on its
    # own with cp||true, touch||true, rebuild||true, and it never raised
    # RESTORE_FAILED. A failed mutant build leaves binaries nobody verified, and
    # a rebuild here would compile whatever the half-restored tree holds. Refuse
    # instead: the trap deletes them, marks the tree, and the run exits nonzero.
    refuse CASE-BUILD-MUTANT-FAILED "the mutant would not compile, so no gate ran and the binaries on disk are not evidence"
    restore_case_sources || true
    return 1
  fi
  local rc fails selftest
  rc=$(run_gate "$log")
  fails=$(count_fails "$log")
  # Keep what the probe printed UNDER THE MUTANT: the gate rewrites its own
  # copies on every run, so without this the mutant's selftest line would survive
  # only as the FAIL lines in the gate log.
  for selftest in matcher-selftest entity-id-selftest; do
    if [ -f "$OUT/runtime/$selftest.txt" ]; then
      cp -p "$OUT/runtime/$selftest.txt" "$OUT/$tag.$selftest.txt"
    fi
  done
  echo "  gate rc=$rc FAIL-count=$fails (baseline $BASE_FAILS)"
  if [ "$rc" != "0" ] && [ "$fails" -gt "$BASE_FAILS" ]; then
    echo "  OK   gate went red and the FAIL count rose"
  else
    echo "  FAIL gate did not go red: rc=$rc FAIL-count=$fails"; fail_all=1
  fi
  # A named reason means THIS assertion failed, so the match is anchored to a
  # line that begins "  FAIL " -- a bare grep over the log is satisfied by an OK
  # line carrying the same words (the defect m8 hit in the pixel harness).
  local pattern
  for pattern in "$@"; do
    if grep -F "$pattern" "$log" | grep -q "^  FAIL "; then
      echo "  OK   named reason present on a FAIL line: $pattern"
    else
      echo "  FAIL named reason missing: $pattern"
      grep -E 'FAIL' "$log" | head -10 | sed 's/^/       | /'
      fail_all=1
    fi
  done
  # The mutant must never be the reason the tree is stale for the next case.
  if grep -q 'STALE-BINARY=1' "$log"; then
    echo "  FAIL the gate reddened on STALE-BINARY=1: the probe was not rebuilt with the mutant"
    fail_all=1
  fi
  # T61.1 review 5, P1: verify the restore through the same state machine the
  # trap uses, and treat a failure as a REFUSAL. Returning here is what stops
  # the caller from touching the source and rebuilding the mutant into both
  # binaries -- the old code set fail_all and carried on, so a second, lucky
  # restore in the trap could leave the tree looking clean.
  if ! restore_case_sources; then
    fail_all=1
    return 1
  fi
  echo "  OK   restored byte-identical (cmp -s)"
  if ! rebuild; then
    refuse CASE-REBUILD-RESTORED-FAILED "the rebuild from the restored sources failed, so the binaries on disk do not evidence this tree"
    return 1
  fi
  # T61.1 review 7, P1: a rebuild that recompiled nothing is not a rebuild. The
  # binaries have to be newer than the files this case restored, or the object
  # built from the mutant was relinked and the "clean" tree still runs it.
  if ! verify_case_build_is_fresh CASE-REBUILD; then
    refuse CASE-REBUILD-NOT-FRESH "the rebuild did not leave every dependent object and every binary newer than the files this case restored, so an object compiled from the mutant could have been relinked instead of recompiled"
    return 1
  fi
  echo "  OK   every object that depends on a restored file, plus the probe and the product, is newer than it (the recompile is proven)"
  return 0
}

if [ "$#" -eq 0 ]; then set -- u1 u2 u3 u4 u5 u6 u7 u8 u9 u10; fi
for want in "$@"; do
  case "$want" in
    u1)
      # 1b/6's teeth. The same one-token change m9 makes for the pixel gate
      # (drop the leading-token preference, keep the boundary), read here through
      # the probe: the preference is what stops an arm-layer clip that merely
      # contains the token from winning over a real cycle, so the selftest line
      # reports leading=0 and leadingPick=0 and those two assertions red.
      # `chosen` stays 0 -- the synthetic table's index 0 is `walk_SECONDARY`,
      # which is already the first whole-token match in index order -- so the
      # rank-head assertion stays GREEN under this mutant. That is why this case
      # names the two assertions the mutant really breaks instead of a third it
      # cannot: the first run of this harness demanded it and failed on that
      # expectation, which is the harness doing its job.
      run_case u1 native/include/animation_sequence_match.h \
    '      const bool primary = wholeToken
        && sequenceLabelPrimaryTokenMatches(candidate, needleText);' \
    '      const bool primary = wholeToken;  // u1: boundary kept, preference dropped' \
        'matcher leading-token pick' 'matcher leading' ;;
    u2)
      # 1c/6's teeth. The encoder stops refusing and clamps instead: an id one
      # past the ceiling silently shares 255's colour, the selftest reports
      # over=0, and the assertion that names the refusal reds. This is the same
      # one-off clamp demonstration section 8.6 recorded by hand; here it is a
      # case the suite runs, with the restore and the rebuild checked like every
      # other.
      run_case u2 native/include/entity_id_color.h \
    '  if (color == nullptr || id < 1 || id > kEntityIdMax) return false;' \
    '  // u2: clamp instead of refusing, so 300 and 255 share one colour.
  if (color == nullptr || id < 1) return false;
  if (id > kEntityIdMax) id = kEntityIdMax;' \
        'entity id over' ;;
    u3)
      # 1b/6 ordered/head/ranked order: the ranker walks the needles as the
      # OUTER loop (needle-major), which is the nesting review 2 named. The
      # synthetic table is built so the two nestings disagree, so the ranked
      # list becomes 3,0,1,4: matcher ordered and matcher ranked order red, and
      # the list's head is no longer the selector's pick, so matcher head reds.
      run_case u3 native/include/animation_sequence_match.h \
    '  for (std::size_t index = 0; index < sequenceCount; ++index) {
    const std::string& candidate = labelAt(index);
    for (std::size_t needle = 0; needle < needleCount; ++needle) {
      const char* const needleText = needles[needle];
      const bool leading = wholeToken
        ? sequenceLabelPrimaryTokenMatches(candidate, needleText)
        : sequenceLabelHasSubstring(candidate, needleText);
      if (leading) { push(index); break; }
    }
  }' \
    '  for (std::size_t needle = 0; needle < needleCount; ++needle) {
    const char* const needleText = needles[needle];
    for (std::size_t index = 0; index < sequenceCount; ++index) {
      const std::string& candidate = labelAt(index);
      const bool leading = wholeToken
        ? sequenceLabelPrimaryTokenMatches(candidate, needleText)
        : sequenceLabelHasSubstring(candidate, needleText);
      if (leading) { push(index); }
    }
  }' \
        'matcher ordered' 'matcher ranked order' 'matcher head' 'matcher selftest exit' ;;
    u4)
      # 1b/6 rank head: the selector keeps the LAST primary match instead of
      # the first, so it answers 3 while the ranker's head is still 0. That is
      # the review-2 disagreement (a probe would report a pose for a sequence
      # the product never picks): matcher selector agrees with rank head and
      # matcher head both red.
      run_case u4 native/include/animation_sequence_match.h \
    'if (primary && animatedPrimary < 0) {' \
    'if (primary) {  // u4: the LAST primary wins, not the first' \
        'matcher selector agrees with rank head' 'matcher head' 'matcher selftest exit' ;;
    u5)
      # 1b/6 boundary: the SELECTOR forgets WholeToken mode, so both modes run
      # the substring rule and `walk` matches inside `airwalk` again. The
      # airwalk table then picks the air-walk clip (wholeTokenPick=0) instead of
      # the run cycle: matcher boundary and matcher whole-token pick red, and
      # the leading table's preference goes with the mode, so matcher leading
      # and matcher leading-token pick red too. The ranker is a separate
      # function (it reads the mode argument itself), so its list and the
      # rank-head pair stay green -- which is why this case names exactly the
      # five assertions the mutant measures.
      run_case u5 native/include/animation_sequence_match.h \
    'const bool wholeToken = mode == SequenceMatchMode::WholeToken;' \
    'const bool wholeToken = false;  // u5: the whole-token rule dropped, substring semantics everywhere' \
        'matcher boundary' 'matcher whole-token pick' 'matcher leading' \
        'matcher leading-token pick' 'matcher selftest exit' ;;
    u6)
      # 1b/6 substring defect: Substring mode is forgotten and both modes run
      # the token rule, so the airwalk table picks the run cycle instead of the
      # air-walk clip. matcher substring pick and matcher substringDefect red.
      run_case u6 native/include/animation_sequence_match.h \
    'const bool wholeToken = mode == SequenceMatchMode::WholeToken;' \
    'const bool wholeToken = true;  // u6: Substring mode forgotten' \
        'matcher substring pick' 'matcher substringDefect' 'matcher selftest exit' ;;
    u7)
      # 1c/6 one/max/injective: the encoder divides by kEntityIdMax - 1, so id
      # 1 no longer maps to 1/255, id 255 maps past 1.0 and the quantised byte
      # leaves the 1..255 range: entity id one, max and injective red.
      run_case u7 native/include/entity_id_color.h \
    'color[0] = static_cast<float>(id) / static_cast<float>(kEntityIdMax);' \
    'color[0] = static_cast<float>(id) / static_cast<float>(kEntityIdMax - 1);  // u7: one byte short of 255' \
        'entity id one' 'entity id max' 'entity id injective' 'entity id selftest exit' ;;
    u8)
      # 1c/6 over: the ceiling is written into the channel instead of refused,
      # so id 256 shares 255's red. entity id over reds.
      run_case u8 native/include/entity_id_color.h \
    'if (color == nullptr || id < 1 || id > kEntityIdMax) return false;' \
    'if (color == nullptr || id < 1) return false;  // u8: the ceiling is written, not refused' \
        'entity id over' 'entity id selftest exit' ;;
    u9)
      # 1c/6 zero and negative: the lower bound is removed, so 0 and -3 are
      # accepted as ids. entity id zero and entity id negative red.
      run_case u9 native/include/entity_id_color.h \
    'if (color == nullptr || id < 1 || id > kEntityIdMax) return false;' \
    'if (color == nullptr || id > kEntityIdMax) return false;  // u9: zero and negatives are ids' \
        'entity id zero' 'entity id negative' 'entity id selftest exit' ;;
    u10)
      # 1c/6 ceiling: the named constant is 254, so entity id ceiling reds
      # (maxId=254) and injectivity reds with it (id 255 is still accepted, now
      # exceeds 1.0 and collides); `one` stays green because the selftest's own
      # expectation divides by kEntityIdMax, so this case must not name it.
      run_case u10 native/include/entity_id_color.h \
    'inline constexpr int kEntityIdMax = 255;' \
    'inline constexpr int kEntityIdMax = 254;  // u10: the ceiling is off by one' \
        'entity id ceiling' 'entity id injective' 'entity id selftest exit' ;;
    *)
      echo "unknown case: $want"; fail_all=1 ;;
  esac
  # T61.1 review 5, P1: a failed per-case restore forbids every later build --
  # that is the point of the refusal. Stop here; on_exit deletes the binaries
  # and writes the marker.
  if [ "$RESTORE_FAILED" = "1" ]; then
    echo
    echo "=== REFUSAL: the per-case restore failed; the remaining cases are not run and no build is attempted ==="
    break
  fi
done

echo
if [ "$RESTORE_FAILED" = "1" ]; then
  # T61.1 review 5, P1: a green baseline measured now would describe a build the
  # refusal never allowed. Say plainly that the measurement was not taken.
  echo "=== final: NOT MEASURED -- the tree could not be left clean, so a baseline here would describe a mutant build ==="
  echo "  FAIL final baseline not measured (${REFUSAL_CAUSE:-a CASE-RESTORE-* line above failed})"; fail_all=1
else
echo "=== final: the tree must be green again after every restore ==="
FIN_RC=$(run_gate "$OUT/final.log")
FIN_FAILS=$(count_fails "$OUT/final.log")
FIN_OKS=$(count_oks "$OUT/final.log")
if [ "$FIN_RC" = "0" ] && [ "$FIN_FAILS" = "0" ]; then
  echo "  OK   final baseline green (OK-count=$FIN_OKS FAIL-count=0)"
else
  echo "  FAIL final baseline rc=$FIN_RC FAIL-count=$FIN_FAILS"; fail_all=1
fi
fi

echo
if [ "$fail_all" = "0" ]; then
  echo "UNIT-MUTATION-SUITE=PASS: every unit case reddened a NAMED assertion of 1b/6 or 1c/6, the probe was rebuilt with each mutant (never STALE-BINARY), the tree was restored byte-for-byte, and the final baseline is green."
else
  echo "UNIT-MUTATION-SUITE=FAIL"
fi
exit "$fail_all"
