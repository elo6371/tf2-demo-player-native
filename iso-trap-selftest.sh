#!/usr/bin/env bash
# iso-trap-selftest.sh -- prove that verify-iso.sh chains an EXIT trap instead of
# re-parsing its quotes, and that the cleanup still happens.
#
# What it reproduces: a gate installs "rm -rf \"$LOCK_DIR\"" as its EXIT action,
# then the isolated-settings block chains a second action on top. The broken
# chain ran neither, printed "command not found", and leaked both the lock and
# the isolated directory. Each case below is a real child process, so the trap
# fires exactly where it fires in a gate.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ASSERTIONS=0
FAILED=0
ok()   { ASSERTIONS=$((ASSERTIONS + 1)); echo "  OK   $*"; }
bad()  { ASSERTIONS=$((ASSERTIONS + 1)); FAILED=$((FAILED + 1)); echo "  FAIL $*"; }
is_file()      { if [ -f "$2" ]; then ok "$1"; else bad "$1"; fi; }
is_no_file()   { if [ ! -e "$2" ]; then ok "$1"; else bad "$1"; fi; }
contains()     { case "$2" in *"$3"*) ok "$1" ;; *) bad "$1" ;; esac; }
not_contains() { case "$2" in *"$3"*) bad "$1" ;; *) ok "$1" ;; esac; }
is_empty()     { if [ -z "$2" ]; then ok "$1"; else bad "$1"; fi; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/tf2-iso-selftest.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT

echo "=== case 1: a previous EXIT trap is chained, not replaced ==="
MARKER="$TMP/prev.marker"
ISO1="$TMP/iso-chained"
mkdir -p "$ISO1"
cat > "$TMP/child-chained.sh" <<'CHILD'
set -u
MARKER="$1"
ISO="$2"
HERE="$3"
trap 'echo LOCKCLEAN >> "$MARKER"' EXIT
. "$HERE/verify-iso.sh"
iso_chain_exit "$ISO"
exit 0
CHILD
out1=$(bash "$TMP/child-chained.sh" "$MARKER" "$ISO1" "$HERE" 2>&1)
rc1=$?
if [ "$rc1" -eq 0 ]; then ok "the chained child exits 0"; else bad "the chained child exits 0 (got $rc1)"; fi
is_file "the previous EXIT action ran (marker exists)" "$MARKER"
if [ -f "$MARKER" ]; then
  contains "the previous EXIT action wrote its marker" "$(cat "$MARKER")" "LOCKCLEAN"
else
  bad "the previous EXIT action wrote its marker"
fi
is_no_file "the isolated directory was removed" "$ISO1"
not_contains "the chain prints no command-not-found" "$out1" "command not found"
not_contains "the chain prints no other complaint" "$out1" "No such file or directory"

echo "=== case 2: no previous EXIT trap still cleans up ==="
ISO2="$TMP/iso-alone"
mkdir -p "$ISO2"
cat > "$TMP/child-alone.sh" <<'CHILD'
set -u
ISO="$1"
HERE="$2"
. "$HERE/verify-iso.sh"
iso_chain_exit "$ISO"
exit 0
CHILD
out2=$(bash "$TMP/child-alone.sh" "$ISO2" "$HERE" 2>&1)
rc2=$?
if [ "$rc2" -eq 0 ]; then ok "the lone child exits 0"; else bad "the lone child exits 0 (got $rc2)"; fi
is_no_file "the isolated directory was removed without a previous trap" "$ISO2"
is_empty "the lone child prints nothing" "$out2"

echo "=== case 3: a previous trap with a quoted argument still runs whole ==="
MARKER3="$TMP/third.marker"
ISO3="$TMP/iso-third"
mkdir -p "$ISO3"
cat > "$TMP/child-arg.sh" <<'CHILD'
set -u
MARKER="$1"
ISO="$2"
HERE="$3"
note() { echo "$1" >> "$MARKER"; }
trap 'note "two words"' EXIT
. "$HERE/verify-iso.sh"
iso_chain_exit "$ISO"
exit 0
CHILD
out3=$(bash "$TMP/child-arg.sh" "$MARKER3" "$ISO3" "$HERE" 2>&1)
rc3=$?
if [ "$rc3" -eq 0 ]; then ok "the quoted-argument child exits 0"; else bad "the quoted-argument child exits 0 (got $rc3)"; fi
is_no_file "the isolated directory was removed with a quoted-argument trap" "$ISO3"
contains "the chained trap kept the quoted argument intact" "$(cat "$MARKER3" 2>/dev/null)" "two words"
not_contains "the quoted-argument chain prints no command-not-found" "$out3" "command not found"

echo
if [ "$FAILED" -eq 0 ]; then
  echo "ISO-TRAP-SELFTEST=PASS ($ASSERTIONS assertions)"
  exit 0
fi
echo "ISO-TRAP-SELFTEST=FAIL ($ASSERTIONS assertions, $FAILED failed)"
exit 1
