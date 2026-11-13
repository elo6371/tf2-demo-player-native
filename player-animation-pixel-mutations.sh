#!/usr/bin/env bash
# player-animation-pixel-mutations.sh -- prove the T61 PIXEL gate can go red.
#
# player-animation-pixels.sh asserts on the picture: the entity pass's measured
# mask is a shaded body (F3), and the animation drive moves those pixels between
# the on and off frames of the same scene tick (F4). This script reintroduces
# two real defects and requires the gate to go red for a NAMED pixel reason:
#
#   m6  the entity vertex shader stops writing the neutral blend/feature
#       values, so the shared world pixel shader reads whatever the unwritten
#       TEXCOORD4 slot happens to hold (1.0 on this driver) and the body is
#       replaced by the unbound blend atlas: the ORIGINAL black-silhouette
#       defect, reintroduced. Must red F3 (body distinct colours / black share).
#   m7  the drive still computes the cycle but the sampling request is no
#       longer rewritten (self-assignment), so --player-animation changes
#       nothing the user sees. Must red F4 (the on/off frames become identical).
#
# T61.1 added three cases for the sequence-pick assertions:
#
#   m8  the matcher goes back to a plain substring search, so "walk" matches
#       inside "airwalk" again and the moving pick is an air-walk clip ->
#       must red "air-walk clip". This is the defect itself, not a proxy.
#   m9  the token boundary stays but the primary-token preference is dropped,
#       so the pick falls back to an arm-layer clip -> must red "is not a run
#       cycle". This is what the token rule ALONE would have shipped, so the
#       gate is forced to distinguish "suppressed a word" from "found a cycle".
#   m10 the pick is correct but a different clip is handed to the renderer, so
#       only the frame-level reading notices -> must red "rendered scene
#       reports reason=".
#
# T61.1 review 3 added two more cases:
#
#   m11 one body samples the wrong clip while its own pick line stays right ->
#       must red the per-instance band ("but its pick is"), which no
#       scene-level set membership check can see.
#   m12 two bodies are painted one entity id, so the ledger claims an id twice
#       -> must red "two bodies share one entity id". Until review 3 the reader
#       DROPPED the second ledger line and continued, so a merged colour could
#       still read as a pass.
#
# WHAT WOULD MAKE THIS SCRIPT DISHONEST, AND WHAT IT DOES INSTEAD
#   * a mutation that does not compile proves nothing -> the rebuild is checked
#     and the gate's own F1 freshness guard runs inside every case;
#   * a mutation caught by a MISSING trace line or a vanished counter proves
#     nothing about pixels -> both cases keep animsum and the debug view intact
#     and the named reasons required below are PIXEL assertions;
#   * a tree left holding a mutant proves nothing -> restore is cmp -s verified,
#     the file is touched, and the suite re-runs the gate at the end to prove it
#     is green again;
#   * an INTERRUPT after the mutant build would leave a mutant product behind
#     -> the trap restores the sources, deletes the product, rebuilds it and
#     reports INTERRUPT-RECOVERY=rebuilt, or NOT-FOR-ACCEPTANCE with a marker
#     file when the rebuild fails.
#
# T61.1 review 5, P1: a per-case restore that did not verify is a REFUSAL, not a
# warning. The run stops, the trap deletes the product and writes the marker,
# and the exit code says the tree is not clean -- a case that cannot be undone
# must never be followed by another build or by a "green" baseline.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it builds and runs captures).
# Usage: bash player-animation-pixel-mutations.sh [case ...]  (default: m6 m7 m8 m9 m10 m11 m12)
# Exit:  0 = every case went red for its named pixel reason and the tree came
#            back green; 1 = otherwise.
set -uo pipefail
cd "$(dirname "$0")"

GATE=player-animation-pixels.sh
# T61.1 review 3, P2: the two directories below are overridable so the interrupt
# demo (player-animation-interrupt-demo.sh) can drive a real case without
# writing over the cited evidence.
OUT=${T61_MUTATION_OUT:-evidence/t61-animation/pixel-mutations}
export T61_PIXEL_OUT=${T61_MUTATION_PIXEL_OUT:-evidence/t61-animation/pixels/mutations}
BACKUP=.scratch/t61-pixel-mutations
PROBE=""
PRODUCT=native/build-nmake/tf2_demo_native.exe
SUITE_LABEL="the pixel mutation suite"
T61_BUILD_TARGETS="tf2_demo_native"
OBJ_ROOT=${T61_OBJ_ROOT:-native/build-nmake}
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
BASE_RC=$(run_gate "$OUT/baseline.log")
BASE_FAILS=$(count_fails "$OUT/baseline.log")
if [ "$BASE_RC" = "0" ] && [ "$BASE_FAILS" = "0" ]; then
  echo "  OK   baseline rc=0 FAIL-count=0 (log $OUT/baseline.log)"
  grep -E '^  OK ' "$OUT/baseline.log" | head -20 | sed 's/^/       /'
else
  echo "  FAIL baseline rc=$BASE_RC FAIL-count=$BASE_FAILS (log $OUT/baseline.log)"
  grep -E 'FAIL|FATAL' "$OUT/baseline.log" | head -20 | sed 's/^/       /'
  echo "FAIL player-animation-pixel-mutations: baseline is not green; mutations would not mean anything."
  exit 1
fi

run_case() { # run_case <tag> <file> <find> <replace> <named pixel reason>...
  local tag="$1" file="$2" find="$3" replace="$4"; shift 4
  if [ "$#" -eq 0 ]; then
    echo "  FAIL case $tag lists no named assertion: a case with an empty expectation list checks nothing, and a for-loop over an empty list prints a PASS it did not earn"
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
  # A mutant that does not compile would make the gate fail on a stale binary,
  # which proves nothing about pixels. Demand a real compile.
  if ! rebuild; then
    # T61.1 review 6, P1: same fix as the unit harness -- this branch restored,
    # touched and rebuilt with every error swallowed and never raised
    # RESTORE_FAILED, so a failed mutant build could leave a product the trap
    # never looked at. Refuse, restore through the verifier, rebuild nothing.
    refuse CASE-BUILD-MUTANT-FAILED "the mutant would not compile, so no gate ran and the binaries on disk are not evidence"
    restore_case_sources || true
    return 1
  fi
  local rc fails
  rc=$(run_gate "$log")
  fails=$(count_fails "$log")
  echo "  gate rc=$rc FAIL-count=$fails (baseline $BASE_FAILS)"
  if [ "$rc" != "0" ] && [ "$fails" -gt "$BASE_FAILS" ]; then
    echo "  OK   gate went red and the FAIL count rose"
  else
    echo "  FAIL gate did not go red: rc=$rc FAIL-count=$fails"; fail_all=1
  fi
  local pattern
  for pattern in "$@"; do
    # The reason must appear on a FAIL line. A bare grep over the whole log is
    # satisfied by an OK line carrying the same words, and also by a case whose
    # gate came back GREEN: m8 once matched the gate PASS line "OK   no moving
    # pick is an air-walk clip" while the real red was a different assertion, and
    # m10 scored OK on rc=0 FAIL-count=0. A named reason means THIS assertion
    # failed, so the match is anchored to a FAIL line.
    if grep -F "$pattern" "$log" | grep -q "^  FAIL "; then
      echo "  OK   named PIXEL reason present: $pattern"
    else
      echo "  FAIL named PIXEL reason missing: $pattern"
      grep -E 'FAIL' "$log" | head -10 | sed 's/^/       | /'
      fail_all=1
    fi
  done
  # T61.1 review 5, P1: verify the restore through the same state machine the
  # trap uses, and treat a failure as a REFUSAL. Returning here is what stops
  # the caller from touching the source and rebuilding the mutant into the
  # product -- the old code set fail_all and carried on, so a second, lucky
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
  echo "  OK   every object that depends on a restored file, plus the product, is newer than it (the recompile is proven)"
  return 0
}

if [ "$#" -eq 0 ]; then set -- m6 m7 m8 m9 m10 m11 m12; fi
for want in "$@"; do
  case "$want" in
    m6)
      # The first run measured this case: m6 leaves 537 distinct colours and 84%
      # of the body pixels black, so the distinct-colours assertion (whose text is
      # "flat silhouette") stays green; the assertion that names the defect is the
      # black-share line, whose own text is "(silhouette again)". The first run's
      # log is kept as m6.log.first-expectation, so the correction is auditable.
      run_case m6 native/src/native_renderer.cpp \
'  output.blend = -1.0;
  output.blendModulate = 0.0;
  output.feature = float4(0.0, 0.0, 0.0, 0.0);
  output.tangent = float4(1.0, 0.0, 0.0, 0.0);
' \
'  // m6: the neutral writes are gone on purpose.
' \
        'body pure-black share' 'silhouette again' ;;
    m7)
      run_case m7 native/src/main.cpp \
'                request.sequence = chosen;
                request.hasCycle = true;
                request.cycle = step.cycle;
                request.hasPlaybackRate = false;
' \
'                request.sequence = request.sequence;
                request.hasCycle = request.hasCycle;
                request.cycle = request.cycle;
                request.hasPlaybackRate = false;
' \
        'animation-off moved' ;;
    m8)
      # T61.1 review 2: the mutant must be the DEFECT, not a rewrite of the
      # matcher. The defect was one argument at one call site -- the moving arm
      # asked for Substring matching, so "walk" matched inside "airwalk" and the
      # leading-token preference (primary = wholeToken && ...) was switched off
      # along with it. The earlier version of this case replaced the whole
      # header, which proves only that a mangled matcher is caught; the reviewer
      # was right to call that INVALID as a stand-in for this regression.
      #
      # One token changes, so the case also proves the call site is wired to the
      # mode it names: a selector that ignored the mode argument would leave
      # this mutant green.
      run_case m8 native/src/main.cpp \
    '      moving ? tf2::native::SequenceMatchMode::WholeToken
             : tf2::native::SequenceMatchMode::Substring,' \
    '      moving ? tf2::native::SequenceMatchMode::Substring  // m8: the pre-T61.1 test
             : tf2::native::SequenceMatchMode::Substring,' \
        'air-walk clip' ;;
    m9)
      # T61.1: keep the token boundary, drop the primary-token preference. The
      # pick therefore falls to the FIRST whole-token match in index order, which
      # on a player model is an arm-layer clip rather than a cycle. That is what
      # the boundary rule ALONE would have shipped, and the gate must reject it
      # as a fix -- the reason names the arm-layer picks, not an air-walk clip.
      #
      # The anchor lives in the shared header, not in native/src/main.cpp:
      # choosing moved there when the selector was factored out for the probe,
      # and the old two-line anchor in main.cpp no longer exists.
      run_case m9 native/include/animation_sequence_match.h \
    '      const bool primary = wholeToken
        && sequenceLabelPrimaryTokenMatches(candidate, needleText);' \
    '      const bool primary = wholeToken;  // m9: boundary kept, preference dropped' \
        'is not a run cycle' ;;
    m10)
      # T61.1: the pick line is untouched; the clip that is DRAWN is not. Swapping
      # chosen leaves "anim: pick" naming run_PRIMARY / stand_PRIMARY while the body
      # is sampled from the other drive state sequence, so the rendered reason and
      # the resolved pick disagree. Only the set-membership assertion added by this
      # task can see that: forbidding the word airwalk cannot, because the wrong
      # clip here is a legitimate one called stand_PRIMARY.
      # The earlier m10 rewrote runtime.sequenceLabel, which is never read for the
      # scene line (that reading comes from the table by index), so it scored a
      # vacuous OK on rc=0 FAIL-count=0; kept in the report as a retraction.
      run_case m10 native/src/main.cpp \
'const std::int32_t chosen = runtime.moving ? choice.moving : choice.idle;' \
'const std::int32_t chosen = runtime.moving ? choice.idle : choice.moving; /* m10 */' \
        'the pick and the picture disagree' ;;
    m11)
      # T61.1 P1: ONE body picks the wrong clip while every other reading stays
      # consistent. m10 swaps chosen for every body, so the pick line and the
      # picture both move and a scene-level set membership check can see it;
      # here the pick line is untouched and only this instance's own sequence
      # changes, which is why the per-instance band in t61-entityid.py exists.
      # The override is armed on the frame that is about to be captured, not on
      # the first moving frame of the run: the capture happens thousands of
      # ticks after playback starts, and a one-shot taken at the first moving
      # body would have expired long before the picture was taken.
      run_case m11 native/src/main.cpp \
'                request.sequence = chosen;
                request.hasCycle = true;
                request.cycle = step.cycle;
                request.hasPlaybackRate = false;
' \
'                request.sequence = chosen;
                // m11: on the frame about to be captured, ONE moving body
                // samples the clip for the other drive state. Its own pick
                // line is untouched, so only a per-instance reading sees it.
                static bool m11Done = false;
                if (!m11Done && runtime.moving && choice.idle >= 0
                    && captureArmed && captureTick > 0
                    && g_playback.tick >= captureTick) {
                  m11Done = true;
                  request.sequence = choice.idle;
                }
                request.hasCycle = true;
                request.cycle = step.cycle;
                request.hasPlaybackRate = false;
' \
        'but its pick is' ;;
    m12)
      # T61.1 review 3, P1: give every body one of two ids. The paint then puts
      # many bodies in ONE colour, which no pixel mask can disambiguate. Until
      # review 3 the reader kept the first ledger line and silently dropped the
      # rest, so a merged colour could be read as the wrong body and the gate
      # could still pass. The refusal is now its own named reading.
      #
      # The anchor is the id assignment itself, so the case also proves the
      # ledger line and the painted colour come from the same number: a mutant
      # that only changed one of them would be caught by the JOIN band instead.
      run_case m12 native/src/main.cpp \
'              const int entityId = static_cast<int>(draws.size()) + 1;' \
'              const int entityId = 1 + (static_cast<int>(draws.size()) % 2);  // m12: two ids for every body' \
        'two bodies share one entity id' ;;
    *)
      echo "unknown case: $want"; fail_all=1 ;;
  esac
  # T61.1 review 5, P1: a failed per-case restore forbids every later build --
  # that is the point of the refusal. Stop here; on_exit deletes the product and
  # writes the marker.
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
if [ "$FIN_RC" = "0" ] && [ "$FIN_FAILS" = "0" ]; then
  echo "  OK   final baseline green (FAIL-count=0)"
else
  echo "  FAIL final baseline rc=$FIN_RC FAIL-count=$FIN_FAILS"; fail_all=1
fi
fi

echo
if [ "$fail_all" = "0" ]; then
  echo "MUTATION-SUITE=PASS: every pixel case reddened a NAMED pixel assertion, the tree was restored byte-for-byte, and the final baseline is green."
else
  echo "MUTATION-SUITE=FAIL"
fi
exit "$fail_all"
