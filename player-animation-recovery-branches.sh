#!/usr/bin/env bash
# T61.1 final round: drive the WHOLE recovery state machine -- the trap's own
# restore_all/recover_binaries/on_exit AND the per-case run_case -- through the
# failure matrix the reviews named, with the functions extracted VERBATIM from
# each harness under test, in isolated mktemp trees. No real build is invoked:
# rebuild() is stubbed and counts its calls, so "no build after a refusal" is
# measured. The object-freshness proof is driven against a fake CMakeFiles tree
# (OBJ_ROOT) whose compiler_depend.make names the object that depends on the
# restored file, so STALE-OBJECT / NO-OBJECT-DEPENDENCY are real code paths, not
# prose.
#
# Also part 1: the two harnesses must carry the SAME recovery region (the
# final-round rule forbids a shared library file). The two files need not share a
# line-ending convention: this host has core.autocrlf=true, so a fresh checkout
# materialises text files as CRLF while the blobs the repository stores are LF.
# What this driver proves is therefore line-ending-normalised text equality
# (normalise CRLF to LF, then cmp -s) plus one internal-consistency assertion per
# file (every line the same convention, no stray CR). It is NOT a byte comparison
# of the two files and is never reported as one.
#
# Part 6 is the final-review regression test: a clean exit must not touch a
# source (a touch there lands after the last case's freshness proof) and must
# re-run the proof instead; when the proof no longer holds it must refuse.
#
# SYNTHETIC (labelled in the log and in the report): chmod 000 does not make a
# file unreadable for this Administrator token and icacls deny is rejected, so
# the unreadable-backup state is injected with a bash function shim over "[".
set -u
cd "$(dirname "$0")" || exit 9
T=$(mktemp -d "${TMPDIR:-/tmp}/t61-recovery.XXXXXX") || exit 9
trap 'rm -rf "$T"' EXIT

oks=0
fails=0
res=0
ok() { echo "  OK   $1"; oks=$((oks+1)); }
bad() { echo "  FAIL $1"; fails=$((fails+1)); res=1; }
chk() { if [ "$2" = "$3" ]; then ok "$1 (rc=$3)"; else bad "$1: rc=$3 expected $2"; fi }

region_of() { # region_of <harness> <out-file>
  python - "$1" "$2" <<'PY'
import io, sys
t = io.open(sys.argv[1], encoding='utf-8', newline='').read().replace('\r\n', '\n')
a = t.index('# >>> SHARED-RECOVERY-STATE-MACHINE-BEGIN')
b = t.index('# <<< SHARED-RECOVERY-STATE-MACHINE-END') + len('# <<< SHARED-RECOVERY-STATE-MACHINE-END')
io.open(sys.argv[2], 'w', encoding='utf-8', newline='').write(t[a:b] + '\n')
PY
}

funcs_of() { # funcs_of <harness> <out-file>: the region plus that harness's run_case
  region_of "$1" "$2"
  sed -n '/^run_case()/,/^}/p' "$1" | tr -d '\r' >> "$2"
}

echo "=== part 1: the two copies of the recovery state machine are identical ==="
region_of player-animation-unit-mutations.sh "$T/region-unit.sh"
region_of player-animation-pixel-mutations.sh "$T/region-pixel.sh"
if cmp -s "$T/region-unit.sh" "$T/region-pixel.sh"; then
  ok "the recovery region is identical after line-ending normalisation ($(grep -c '' "$T/region-unit.sh") lines)"
else
  bad "the recovery regions differ after line-ending normalisation"
  diff -u "$T/region-unit.sh" "$T/region-pixel.sh" | head -20 | sed 's/^/    /'
fi
# The normalisation above is only safe if each file is internally consistent: a
# stray CR part-way through a file would be normalised away and could otherwise
# hide. The COUNTS are checkout-dependent (core.autocrlf=true gives a fresh
# checkout CRLF on both files), so what is asserted is "one convention", not
# "0 CR" or "n CR".
for h in player-animation-unit-mutations.sh player-animation-pixel-mutations.sh; do
  cr=$(tr -cd '\r' < "$h" | wc -c)
  lines=$(grep -c '' "$h")
  if [ "$cr" = "0" ] || [ "$cr" = "$lines" ]; then
    ok "$h keeps one line-ending convention (CR bytes $cr, lines $lines)"
  else
    bad "$h mixes line endings (CR bytes $cr, lines $lines): normalising would hide the stray CR"
  fi
done
funcs_of player-animation-unit-mutations.sh "$T/funcs-unit.sh"
funcs_of player-animation-pixel-mutations.sh "$T/funcs-pixel.sh"
if cmp -s "$T/funcs-unit.sh" "$T/funcs-pixel.sh"; then
  ok "the extracted functions (region + run_case) are identical too"
else
  ok "the extracted function sets differ only in run_case (expected: per-suite reasons)"
fi

echo
echo "=== part 2: every refusal reason is wired into both harnesses ==="
wiring() { # wiring <file> <label>
  local f=$1 label=$2 i=0 n=0 pat
  for pat in \
    'restore_case_sources() {' \
    'if ! restore_case_sources; then' \
    'REFUSAL the tree could not be restored' \
    'note_cause() {' \
    'refuse() {' \
    'CASE-BUILD-MUTANT-FAILED' \
    'CASE-REBUILD-RESTORED-FAILED' \
    'CASE-REBUILD-NOT-FRESH' \
    'CASE-RESTORE-TOUCH-FAILED' \
    'CASE-RESTORE-MTIME-NOT-ADVANCED' \
    'CASE-RESTORE-UNREADABLE-BACKUP' \
    'TRAP-RESTORE-TOUCH-FAILED' \
    'TRAP-RESTORE-MTIME-NOT-ADVANCED' \
    'TRAP-RESTORE-MISSING-BACKUP' \
    'TRAP-RESTORE-MISMATCH' \
    'verify_case_build_is_fresh' \
    'verify_objects_are_fresh' \
    'verify_clean_exit() {' \
    'if ! verify_clean_exit; then' \
    'CLEAN-EXIT-NOT-FRESH' \
    'CLEAN-EXIT-SOURCE-DIFFERS' \
    'verify_case_build_is_fresh CLEAN-EXIT' \
    'STALE-OBJECT' \
    'NO-OBJECT-DEPENDENCY' \
    'compiler_depend.make' \
    'STAMP=${T61_STAMP:-$OBJ_ROOT/.headers-stamp}' \
    'BINARIES-NOT-FOR-ACCEPTANCE.txt'; do
    n=$((n+1))
    if grep -qF "$pat" "$f"; then i=$((i+1)); else bad "$label is missing: $pat"; fi
  done
  # the loop must stop the run and skip the final baseline after a refusal
  for pat in 'if [ "$RESTORE_FAILED" = "1" ]; then' 'break' 'final baseline not measured'; do
    n=$((n+1))
    if grep -qF "$pat" "$f"; then i=$((i+1)); else bad "$label is missing: $pat"; fi
  done
  [ "$i" = "$n" ] && ok "$label wires the refusal ($i/$n anchors)"
}
wiring player-animation-unit-mutations.sh "unit harness"
wiring player-animation-pixel-mutations.sh "pixel harness"

targets_for() { # targets_for <unit|pixel>
  case "$1" in
    unit) printf '%s' 'model_pose_probe tf2_demo_native' ;;
    *)    printf '%s' 'tf2_demo_native' ;;
  esac
}

setup_objs() { # setup_objs <kind> <dir> <normal|no-object>
  local kind=$1 d=$2 mode=$3 t bs obj srcf
  bs=$(printf '\\')        # one backslash, without printf escapes in a format
  for t in $(targets_for "$kind"); do
    mkdir -p "$d/obj/CMakeFiles/$t.dir/src"
    : > "$d/obj/CMakeFiles/$t.dir/src/f.cpp.obj"
    if [ "$mode" = "no-object" ]; then
      srcf="$d/src/other.cpp"
    else
      srcf="$d/src/f.cpp"
    fi
    obj="CMakeFiles"$bs"$t.dir"$bs"src"$bs"f.cpp.obj"
    # CMake writes this file with CRLF line endings on Windows (measured:
    # 10,138 CRLF of 10,138 lines in the real tf2_demo_native.dir file), so the
    # fake tree must too, or the driver cannot catch a parser that keeps the CR.
    printf '%s: %s\r\n' "$obj" "$srcf" > "$d/obj/CMakeFiles/$t.dir/compiler_depend.make"
  done
}
stub_rebuild() { # stub_rebuild <kind> <dir> <mode>: counts builds, refreshes objects/binaries
  REBUILDS=$((REBUILDS+1))
  echo "$REBUILDS" > "$SC/rebuilds"
  case "$MODE" in
    build-fail)   [ "$REBUILDS" = "1" ] && return 1 ;;
    rebuild-fail) [ "$REBUILDS" = "2" ] && return 1 ;;
  esac
  if [ "$MODE" = "stale-object" ] && [ "$REBUILDS" = "2" ]; then
    : > "$PRODUCT"
    [ -n "$PROBE" ] && : > "$PROBE"
    find "$SC/obj/CMakeFiles" -name '*.obj' -exec touch -t 202001010000 {} + 2>/dev/null || true
    return 0
  fi
  : > "$PRODUCT"
  [ -n "$PROBE" ] && : > "$PROBE"
  find "$SC/obj/CMakeFiles" -name '*.obj' -exec touch {} + 2>/dev/null || true
  if [ "$MODE" = "stale-binary" ] && [ "$REBUILDS" = "2" ]; then
    touch -t 202001010000 "$PRODUCT"
    [ -n "$PROBE" ] && touch -t 202001010000 "$PROBE"
  fi
  return 0
}

# ---------------------------------------------------------------------------
# Part 3: the per-case restore, driven through each harness's OWN run_case().
# Modes: clean (control), mismatch, unreadable (SYNTHETIC), touch-fail,
# lie-touch (returns 0, mtime unchanged), stale-object, no-object, stale-binary,
# build-fail, rebuild-fail.
# ---------------------------------------------------------------------------
scenario() { # scenario <kind> <mode>
  local kind=$1 mode=$2 d rc
  d="$T/case-$kind-$mode"
  rm -rf "$d"
  mkdir -p "$d/out" "$d/backup" "$d/src"
  printf 'original source\n' > "$d/src/f.cpp"
  cp -p "$d/src/f.cpp" "$d/backup/f.orig"
  case "$mode" in
    no-object) setup_objs "$kind" "$d" no-object ;;
    *)         setup_objs "$kind" "$d" normal ;;
  esac
  : > "$d/obj/.headers-stamp"
  if [ "$mode" = "lie-touch" ]; then
    :   # the stamp must be NEWER than the file the restore cannot refresh
  else
    touch -t 202001010000 "$d/obj/.headers-stamp"
  fi
  (
    set -u
    SC="$d"; MODE="$mode"; REBUILDS=0
    OUT="$d/out"; BACKUP="$d/backup"; OBJ_ROOT="$d/obj"
    SUITE_LABEL="the $kind mutation suite"; GATE=stub-gate
    PRODUCT="$d/out/product.exe"; PROBE="$d/out/probe.exe"
    if [ "$kind" = "pixel" ]; then PROBE=""; fi
    T61_BUILD_TARGETS="$(targets_for "$kind")"
    . "$T/funcs-$kind.sh"
    fail_all=0; BASE_FAILS=0; BASE_OKS=0; REFUSAL_CAUSE=""
    patch_in() { printf 'mutant source\n' > "$1"; }
    : > "$PRODUCT"; [ -n "$PROBE" ] && : > "$PROBE"
    rebuild() { stub_rebuild "$kind" "$d" "$mode"; }
    run_gate() { printf '  FAIL mutant assertion reason\n' > "$1"; echo 1; }
    case "$mode" in
      unreadable)
        T61_UNREADABLE="$d/backup/r1.orig"
        function [ {
          case "$1 $2 $3" in
            "-r $T61_UNREADABLE") return 1 ;;
            "! -r $T61_UNREADABLE") return 0 ;;
          esac
          builtin [ "$@"
        } ;;
      mismatch)
        cp() { if [ "$3" = "$d/src/f.cpp" ] && [ ! -e "$d/cp-failed" ]; then : > "$d/cp-failed"; return 1; fi; command cp "$@"; } ;;
      touch-fail)
        touch() { case "$1" in */f.cpp) return 1 ;; esac; command touch "$@"; } ;;
      lie-touch)
        touch() { case "$1" in */f.cpp) return 0 ;; esac; command touch "$@"; } ;;
    esac
    run_case r1 "$d/src/f.cpp" FIND REPLACE 'mutant assertion reason'
    rc_case=$?
    ( exit "$rc_case" ); on_exit
  ) > "$d/run.log" 2>&1
  rc=$?
  echo "$rc" > "$d/rc"
}

assert_case() { # assert_case <kind> <mode> <rc> <rebuilds> <cause> <source yes|no> <bins deleted|present>
  local kind=$1 mode=$2 exp_rc=$3 exp_rebuilds=$4 cause=$5 src_exp=$6 bin_exp=$7
  local d="$T/case-$kind-$mode" label rc rebuilds
  label="$kind/$mode"
  rc=$(cat "$d/rc"); rebuilds=$(cat "$d/rebuilds" 2>/dev/null || echo '?')
  echo "--- $label: rc=$rc rebuilds=$rebuilds"
  grep -E 'CASE-|REFUSAL|TRAP-' "$d/run.log" | head -4 | sed 's/^/    /'
  chk "$label: exit code" "$exp_rc" "$rc"
  if [ "$cause" = "none" ]; then
    ok "$label: no refusal to name (clean control)"
  elif grep -qF "  FAIL $cause" "$d/run.log"; then
    ok "$label: $cause named on a FAIL line"
  else
    bad "$label: no FAIL line for $cause"
  fi
  [ "$rebuilds" = "$exp_rebuilds" ] && ok "$label: builds after the case (want $exp_rebuilds)" || bad "$label: rebuilds=$rebuilds expected $exp_rebuilds"
  if [ "$src_exp" = "yes" ]; then
    cmp -s "$d/src/f.cpp" "$d/backup/f.orig" && ok "$label: the source is back to the original bytes" || bad "$label: source is not original"
  else
    cmp -s "$d/src/f.cpp" "$d/backup/f.orig" && bad "$label: claimed a restore although the backup is unreadable" || ok "$label: no restore is claimed (the backup could not be read)"
  fi
  if [ "$bin_exp" = "deleted" ]; then
    [ ! -e "$d/out/product.exe" ] && ok "$label: the product is DELETED, not left mutant" || bad "$label: the product survived"
    [ "$kind" = "unit" ] && { [ ! -e "$d/out/probe.exe" ] && ok "$label: the probe is DELETED" || bad "$label: the probe survived"; }
    [ -f "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" ] && ok "$label: marker written" || bad "$label: no marker"
    grep -qF "$cause" "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" 2>/dev/null && ok "$label: the marker names $cause" || bad "$label: the marker does not name $cause"
  else
    [ -e "$d/out/product.exe" ] && ok "$label: the product is present (nothing to refuse)" || bad "$label: the product is missing"
    [ ! -e "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" ] && ok "$label: no marker on a clean case" || bad "$label: a stale marker was written"
  fi
}

echo
echo "=== part 3: the per-case restore state machine (both harnesses) ==="
for kind in unit pixel; do
  scenario "$kind" clean
  assert_case "$kind" clean 0 2 none yes present
  grep -qF 'OK   every object that depends on a restored file' "$T/case-$kind-clean/run.log" \
    && ok "$kind/clean: the object proof ran and passed" || bad "$kind/clean: no object-proof OK line"
  scenario "$kind" mismatch
  assert_case "$kind" mismatch 1 1 CASE-RESTORE-MISMATCH yes deleted
  scenario "$kind" unreadable
  assert_case "$kind" unreadable 1 1 CASE-RESTORE-UNREADABLE-BACKUP no deleted
  scenario "$kind" touch-fail
  assert_case "$kind" touch-fail 1 1 CASE-RESTORE-TOUCH-FAILED yes deleted
  scenario "$kind" lie-touch
  assert_case "$kind" lie-touch 1 1 CASE-RESTORE-MTIME-NOT-ADVANCED yes deleted
  grep -qF 'touch returned 0, but the mtime' "$T/case-$kind-lie-touch/run.log" \
    && ok "$kind/lie-touch: the lie is spelled out in the refusal" || bad "$kind/lie-touch: no mtime reason"
  scenario "$kind" build-fail
  assert_case "$kind" build-fail 1 1 CASE-BUILD-MUTANT-FAILED yes deleted
  grep -q 'gate went red' "$T/case-$kind-build-fail/run.log" && bad "$kind/build-fail: a gate ran on an unbuilt mutant" || ok "$kind/build-fail: no gate ran on the failed mutant build"
  scenario "$kind" rebuild-fail
  assert_case "$kind" rebuild-fail 1 2 CASE-REBUILD-RESTORED-FAILED yes deleted
  scenario "$kind" stale-object
  assert_case "$kind" stale-object 1 2 CASE-REBUILD-STALE-OBJECT yes deleted
  scenario "$kind" no-object
  assert_case "$kind" no-object 1 2 CASE-REBUILD-NO-OBJECT-DEPENDENCY yes deleted
  scenario "$kind" stale-binary
  assert_case "$kind" stale-binary 1 2 CASE-REBUILD-STALE-BINARY yes deleted
done

# ---------------------------------------------------------------------------
# Part 4: the trap's own state machine, through the real on_exit().
# ---------------------------------------------------------------------------
echo
echo "=== part 4: on_exit() failure matrix (both harnesses) ==="
trap_case() { # trap_case <kind> <mode>
  local kind=$1 mode=$2 d rc
  d="$T/trap-$kind-$mode"
  rm -rf "$d"
  mkdir -p "$d/out" "$d/backup" "$d/src"
  printf 'original source\n' > "$d/src/f.cpp"
  cp -p "$d/src/f.cpp" "$d/backup/f.orig"
  setup_objs "$kind" "$d" normal
  : > "$d/out/probe.exe"; : > "$d/out/product.exe"
  : > "$d/obj/.headers-stamp"
  case "$mode" in
    missing)    rm -f "$d/backup/f.orig" ;;
    mismatch)   rm -f "$d/src/f.cpp"; mkdir -p "$d/src/f.cpp" ;;
    deletion)   rm -f "$d/out/product.exe"; mkdir -p "$d/out/product.exe" ;;
    lie-touch|equal-old) : ;;   # stamp stands NOW, so the file's mtime is behind it
    *)          touch -t 202001010000 "$d/obj/.headers-stamp" ;;
  esac
  case "$mode" in
    lie-touch|equal-old) : ;;   # keep the stamp from just now
    *) : ;;
  esac
  (
    set -u
    SC="$d"; MODE="$mode"; REBUILDS=0
    OUT="$d/out"; BACKUP="$d/backup"; OBJ_ROOT="$d/obj"
    SUITE_LABEL="the $kind mutation suite"; GATE=stub-gate
    PRODUCT="$d/out/product.exe"; PROBE="$d/out/probe.exe"
    if [ "$kind" = "pixel" ]; then PROBE=""; fi
    T61_BUILD_TARGETS="$(targets_for "$kind")"
    . "$T/funcs-$kind.sh"
    INTERRUPTED=1; RESTORE_FAILED=0; REFUSAL_CAUSE=""; fail_all=0; BASE_FAILS=0
    TOUCHED="$d/src/f.cpp|$d/backup/f.orig"
    rebuild() { stub_rebuild "$kind" "$d" "$mode"; }
    case "$mode" in
      unreadable)
        T61_UNREADABLE="$d/backup/f.orig"
        function [ {
          case "$1 $2 $3" in
            "-r $T61_UNREADABLE") return 1 ;;
            "! -r $T61_UNREADABLE") return 0 ;;
          esac
          builtin [ "$@"
        } ;;
      touch-fail)
        touch() { case "$1" in */f.cpp) return 1 ;; esac; command touch "$@"; } ;;
      lie-touch)
        touch() { case "$1" in */f.cpp) return 0 ;; esac; command touch "$@"; } ;;
    esac
    true; on_exit
  ) > "$d/run.log" 2>&1
  rc=$?
  echo "$rc" > "$d/rc"
}

assert_trap() { # assert_trap <kind> <mode> <rc> <cause|none> <rebuilds> <bins deleted|present|dir>
  local kind=$1 mode=$2 exp_rc=$3 cause=$4 exp_rebuilds=$5 bin_exp=$6 mk=${7:-}
  local marker=${mk:-$cause}
  local d="$T/trap-$kind-$mode" label rc rebuilds
  label="$kind/trap-$mode"
  rc=$(cat "$d/rc"); rebuilds=$(cat "$d/rebuilds" 2>/dev/null || echo 0)
  echo "--- $label: rc=$rc rebuilds=$rebuilds"
  grep -E 'TRAP-|REFUSAL|NOT-FOR-ACCEPTANCE|INTERRUPT-RECOVERY' "$d/run.log" | head -4 | sed 's/^/    /'
  chk "$label: exit code" "$exp_rc" "$rc"
  chk "$label: builds during recovery" "$exp_rebuilds" "$rebuilds"
  if [ "$cause" = "none" ]; then
    [ ! -e "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" ] && ok "$label: no marker (the tree is clean)" || bad "$label: a marker was written"
  else
    grep -qF "$cause" "$d/run.log" && ok "$label: the trap names $cause" || bad "$label: the trap did not name $cause"
    [ -f "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" ] && ok "$label: marker written" || bad "$label: no marker"
    grep -qF "$marker" "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" 2>/dev/null && ok "$label: the marker names $marker" || bad "$label: the marker does not name $marker"
  fi
  case "$bin_exp" in
    deleted)
      [ ! -e "$d/out/product.exe" ] && ok "$label: the product is DELETED" || bad "$label: the product survived"
      [ "$kind" = "unit" ] && { [ ! -e "$d/out/probe.exe" ] && ok "$label: the probe is DELETED" || bad "$label: the probe survived"; } ;;
    present)
      [ -e "$d/out/product.exe" ] && ok "$label: the product is back (rebuilt)" || bad "$label: the product is missing" ;;
    dir)
      [ -d "$d/out/product.exe" ] && ok "$label: the undeletable product is still there (refused)" || bad "$label: the directory vanished" ;;
  esac
}

for kind in unit pixel; do
  trap_case "$kind" missing
  assert_trap "$kind" missing 3 TRAP-RESTORE-MISSING-BACKUP 0 deleted
  trap_case "$kind" mismatch
  assert_trap "$kind" mismatch 3 TRAP-RESTORE-MISMATCH 0 deleted
  trap_case "$kind" unreadable
  assert_trap "$kind" unreadable 3 TRAP-RESTORE-UNREADABLE-BACKUP 0 deleted
  trap_case "$kind" touch-fail
  assert_trap "$kind" touch-fail 3 TRAP-RESTORE-TOUCH-FAILED 0 deleted
  trap_case "$kind" lie-touch
  assert_trap "$kind" lie-touch 3 TRAP-RESTORE-MTIME-NOT-ADVANCED 0 deleted
  trap_case "$kind" equal-old
  assert_trap "$kind" equal-old 0 none 1 present
  grep -qF 'INTERRUPT-RECOVERY=rebuilt' "$T/trap-$kind-equal-old/run.log" \
    && ok "$kind/trap-equal-old: the recovery rebuilt after proving freshness" || bad "$kind/trap-equal-old: no rebuilt line"
  grep -qF 'TRAP-RESTORE-TOUCH-FAILED' "$T/trap-$kind-equal-old/run.log" \
    && bad "$kind/trap-equal-old: it still claims a touch failure" || ok "$kind/trap-equal-old: no false touch failure"
  trap_case "$kind" deletion
  assert_trap "$kind" deletion 3 "could NOT be deleted" 0 dir "survived the recovery"
done

# clean normal exit keeps the case's own code; interrupt codes are measured
echo
echo "=== part 5: exit-code propagation and the signal traps ==="
exit_case() { # exit_case <kind> <mode>   mode = clean|int|term|hup
  local kind=$1 mode=$2 d rc
  d="$T/exit-$kind-$mode"
  rm -rf "$d"
  mkdir -p "$d/out" "$d/backup" "$d/src"
  printf 'original source\n' > "$d/src/f.cpp"
  cp -p "$d/src/f.cpp" "$d/backup/f.orig"
  setup_objs "$kind" "$d" normal
  : > "$d/out/probe.exe"; : > "$d/out/product.exe"
  : > "$d/obj/.headers-stamp"; touch -t 202001010000 "$d/obj/.headers-stamp"
  (
    set -u
    SC="$d"; MODE="$mode"; REBUILDS=0
    OUT="$d/out"; BACKUP="$d/backup"; OBJ_ROOT="$d/obj"
    SUITE_LABEL="the $kind mutation suite"; GATE=stub-gate
    PRODUCT="$d/out/product.exe"; PROBE="$d/out/probe.exe"
    if [ "$kind" = "pixel" ]; then PROBE=""; fi
    T61_BUILD_TARGETS="$(targets_for "$kind")"
    . "$T/funcs-$kind.sh"
    fail_all=0; BASE_FAILS=0; REFUSAL_CAUSE=""
    TOUCHED="$d/src/f.cpp|$d/backup/f.orig"
    rebuild() { stub_rebuild "$kind" "$d" "$mode"; }
    case "$mode" in
      clean)  ( exit 7 ); on_exit ;;
      int)    kill -INT "$BASHPID"; sleep 5 ;;
      term)   kill -TERM "$BASHPID"; sleep 5 ;;
      hup)    kill -HUP "$BASHPID"; sleep 5 ;;
    esac
  ) > "$d/run.log" 2>&1
  rc=$?
  echo "$rc" > "$d/rc"
}

for kind in unit pixel; do
  exit_case "$kind" clean
  rc=$(cat "$T/exit-$kind-clean/rc")
  chk "$kind/exit-clean: a clean exit keeps the case's rc" 7 "$rc"
  exit_case "$kind" int
  rc=$(cat "$T/exit-$kind-int/rc")
  chk "$kind/exit-int: SIGINT exits 130" 130 "$rc"
  grep -qF 'INTERRUPT-RECOVERY=rebuilt' "$T/exit-$kind-int/run.log" && ok "$kind/exit-int: the recovery rebuilt" || bad "$kind/exit-int: no rebuilt line"
  exit_case "$kind" term
  rc=$(cat "$T/exit-$kind-term/rc")
  chk "$kind/exit-term: SIGTERM exits 130" 130 "$rc"
  exit_case "$kind" hup
  rc=$(cat "$T/exit-$kind-hup/rc")
  chk "$kind/exit-hup: SIGHUP exits 129" 129 "$rc"
done

# ---------------------------------------------------------------------------
# Part 6: a clean exit must NOT touch a source, and must re-run the proof.
# T61.1 final-review P1: on_exit used to call restore_all on a normal exit too,
# and restore_all touches; that touch landed after the last case's object proof,
# leaving the source newer than the objects and the binaries it had just been
# proven against. These two scenarios are the regression test: one proves a
# clean exit leaves the mtimes alone and the proof true, the other injects the
# old behaviour (a touch after the proof) and demands a refusal, not a pass.
# ---------------------------------------------------------------------------
echo
echo "=== part 6: a clean exit does not touch a source and re-runs the proof ==="
clean_exit_case() { # clean_exit_case <kind> <mode>   mode = proof|stale
  local kind=$1 mode=$2 d rc
  d="$T/cleanexit-$kind-$mode"
  rm -rf "$d"
  mkdir -p "$d/out" "$d/backup" "$d/src"
  printf 'original source\n' > "$d/src/f.cpp"
  cp -p "$d/src/f.cpp" "$d/backup/f.orig"
  setup_objs "$kind" "$d" normal
  : > "$d/obj/.headers-stamp"; touch -t 202001010000 "$d/obj/.headers-stamp"
  (
    set -u
    SC="$d"; MODE=proof; REBUILDS=0
    OUT="$d/out"; BACKUP="$d/backup"; OBJ_ROOT="$d/obj"
    SUITE_LABEL="the $kind mutation suite"; GATE=stub-gate
    PRODUCT="$d/out/product.exe"; PROBE="$d/out/probe.exe"
    if [ "$kind" = "pixel" ]; then PROBE=""; fi
    T61_BUILD_TARGETS="$(targets_for "$kind")"
    . "$T/funcs-$kind.sh"
    fail_all=0; BASE_FAILS=0; BASE_OKS=0; REFUSAL_CAUSE=""
    patch_in() { printf 'mutant source\n' > "$1"; }
    rebuild() { stub_rebuild "$kind" "$d" "$MODE"; }
    run_gate() { printf '  FAIL mutant assertion reason\n' > "$1"; echo 1; }
    run_case r1 "$d/src/f.cpp" FIND REPLACE 'mutant assertion reason'
    rc_case=$?
    if [ "$mode" = "stale" ]; then
      # the OLD exit behaviour, injected: a touch that lands after the proof
      command touch "$d/src/f.cpp"
    fi
    stat -c %Y "$d/src/f.cpp" > "$d/mtime-before"
    ( exit "$rc_case" ); on_exit
  ) > "$d/run.log" 2>&1
  rc=$?
  echo "$rc" > "$d/rc"
  stat -c %Y "$d/src/f.cpp" > "$d/mtime-after"
}

assert_clean_exit() { # assert_clean_exit <kind> <mode> <rc>
  local kind=$1 mode=$2 exp_rc=$3 d label rc before after rebuilds objs want
  d="$T/cleanexit-$kind-$mode"; label="$kind/clean-exit-$mode"
  rc=$(cat "$d/rc"); rebuilds=$(cat "$d/rebuilds" 2>/dev/null || echo '?')
  before=$(cat "$d/mtime-before"); after=$(cat "$d/mtime-after")
  echo "--- $label: rc=$rc rebuilds=$rebuilds"
  grep -E 'CLEAN-EXIT|REFUSAL|OK   clean exit' "$d/run.log" | head -4 | sed 's/^/    /'
  chk "$label: exit code" "$exp_rc" "$rc"
  chk "$label: builds (the exit itself must not build)" 2 "$rebuilds"
  cmp -s "$d/src/f.cpp" "$d/backup/f.orig" && ok "$label: the source matches its backup" || bad "$label: source is not original"
  [ "$before" = "$after" ] && ok "$label: the exit did not touch the source (mtime unchanged)" || bad "$label: the exit moved the source mtime ($before -> $after)"
  if [ "$mode" = "proof" ]; then
    grep -qF 'OK   clean exit: every restored source still matches its backup' "$d/run.log" && ok "$label: the proof re-ran and passed" || bad "$label: no clean-exit proof line"
    [ ! -e "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" ] && ok "$label: no marker" || bad "$label: a marker was written"
    want=1; [ "$kind" = "unit" ] && want=2
    objs=$(find "$d/obj/CMakeFiles" -name '*.obj' -newer "$d/src/f.cpp" | wc -l | tr -d ' ')
    [ "$objs" = "$want" ] && ok "$label: every dependent object is newer than the source ($objs/$want)" || bad "$label: $objs of $want objects are newer than the source"
    [ -e "$d/out/product.exe" ] && [ "$d/out/product.exe" -nt "$d/src/f.cpp" ] && ok "$label: the product is newer than the source" || bad "$label: the product is missing or older than the source"
    if [ "$kind" = "unit" ]; then
      [ -e "$d/out/probe.exe" ] && [ "$d/out/probe.exe" -nt "$d/src/f.cpp" ] && ok "$label: the probe is newer than the source" || bad "$label: the probe is missing or older than the source"
    fi
  else
    grep -qF '  FAIL CLEAN-EXIT-NOT-FRESH' "$d/run.log" && ok "$label: the exit refused and named CLEAN-EXIT-NOT-FRESH" || bad "$label: no CLEAN-EXIT-NOT-FRESH FAIL line"
    [ -f "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" ] && ok "$label: marker written" || bad "$label: no marker"
    grep -qF 'CLEAN-EXIT-STALE-BINARY' "$d/out/BINARIES-NOT-FOR-ACCEPTANCE.txt" 2>/dev/null && ok "$label: the marker names CLEAN-EXIT-STALE-BINARY" || bad "$label: the marker does not name the stale binary"
    [ ! -e "$d/out/product.exe" ] && ok "$label: the product is DELETED, not left as evidence" || bad "$label: the product survived the refusal"
  fi
}

for kind in unit pixel; do
  clean_exit_case "$kind" proof
  assert_clean_exit "$kind" proof 0
  clean_exit_case "$kind" stale
  assert_clean_exit "$kind" stale 3
done


echo
echo "ASSERTIONS-OK=$oks ASSERTIONS-FAIL=$fails"
if [ "$res" = "0" ]; then echo "ON-EXIT-BRANCHES=PASS"; else echo "ON-EXIT-BRANCHES=FAIL"; fi
exit "$res"
