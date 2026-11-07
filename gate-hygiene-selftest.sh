#!/usr/bin/env bash
# gate-hygiene-selftest.sh -- the launch-hygiene scanner as the object under test.
#
# Three mock trees, three verdicts that must not blur:
#   H1 a NEW unisolated launch            -> FAIL "new unisolated launch"
#   H2 a baseline row that is no longer real -> FAIL "baseline stale"
#   H3 the positive control (clean tree)   -> PASS violations=0 baseline=0
# H3 also carries a TARGETS=(...) declaration with the product name NOT first:
# a declaration is not a launch, so it must not create a violation row.
#
# Static: writes only under mktemp, launches no product, takes no host lock.
# Usage: bash gate-hygiene-selftest.sh
# Token: GATE-HYGIENE-SELFTEST=PASS|FAIL assertions=N
set -uo pipefail
cd "$(dirname "$0")" || exit 2
HERE=$(pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT INT TERM

failures=0; count=0
ok() { count=$((count + 1)); printf '  OK   %s\n' "$1"; }
bad() { count=$((count + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }
expect_rc() { if [ "$2" = "$3" ]; then ok "$1 (rc=$3)"; else bad "$1 (rc=$2, expected $3)"; fi; }
expect_has() { if grep -qaF "$3" "$2"; then ok "$1"; else bad "$1 (no '$3')"; fi; }
expect_lacks() { if grep -qaF "$3" "$2"; then bad "$1 (found '$3')"; else ok "$1"; fi; }

new_tree() { # new_tree <name> -> echoes dir with evidence baseline path ready
  local d="$W/$1"
  mkdir -p "$d/evidence/gate-launch-hygiene"
  : > "$d/evidence/gate-launch-hygiene/baseline.txt"
  printf '%s' "$d"
}

write_violator() { # write_violator <dir> <name>
  cat > "$1/$2.sh" <<'SH'
#!/usr/bin/env bash
EXE=native/build-nmake/tf2_demo_native.exe
DEMO=$1
"$EXE" --audio-disabled --hidden-window --demo "$DEMO"
SH
}

write_clean() { # write_clean <dir> <name>
  cat > "$1/$2.sh" <<'SH'
#!/usr/bin/env bash
EXE=native/build-nmake/tf2_demo_native.exe
DEMO=$1
TMP=$(mktemp -d)
"$EXE" --audio-disabled --hidden-window --demo "$DEMO" --settings-file "$TMP/settings.ini" --ui-settings-file "$TMP/ui.ini"
SH
}

echo "=== gate-launch-hygiene selftest (mock trees, no product) ==="

# H1: new violation, empty baseline -> FAIL new unisolated launch
d1=$(new_tree h1)
write_violator "$d1" evil
timeout 120 bash "$HERE/gate-launch-hygiene-check.sh" --root "$d1" > "$W/h1.log" 2>&1; rc=$?
expect_rc "H1 new unisolated launch fails" "$rc" 1
expect_has "H1 failure is named" "$W/h1.log" "new unisolated launch"
expect_has "H1 names the offending script" "$W/h1.log" "evil.sh"
expect_has "H1 final token" "$W/h1.log" "GATE-LAUNCH-HYGIENE=FAIL"

# H2: baseline row that no longer violates -> FAIL baseline stale
d2=$(new_tree h2)
write_clean "$d2" clean
printf 'gone.sh|--settings-file\n' > "$d2/evidence/gate-launch-hygiene/baseline.txt"
timeout 120 bash "$HERE/gate-launch-hygiene-check.sh" --root "$d2" > "$W/h2.log" 2>&1; rc=$?
expect_rc "H2 stale baseline fails" "$rc" 1
expect_has "H2 failure is named" "$W/h2.log" "baseline stale"
expect_has "H2 names the stale row" "$W/h2.log" "gone.sh"
expect_has "H2 final token" "$W/h2.log" "GATE-LAUNCH-HYGIENE=FAIL"

# H3: positive control -- compliant launch + a TARGETS declaration (product not
# first in the array) must both be quiet.
d3=$(new_tree h3)
write_clean "$d3" clean
cat > "$d3/build-decl.sh" <<'SH'
#!/usr/bin/env bash
PRIMARY=tf2_demo_native
TARGETS=(some_probe "$PRIMARY")
echo "building ${TARGETS[*]}"
SH
timeout 120 bash "$HERE/gate-launch-hygiene-check.sh" --root "$d3" > "$W/h3.log" 2>&1; rc=$?
expect_rc "H3 clean tree passes" "$rc" 0
expect_has "H3 token reports zero violations" "$W/h3.log" "GATE-LAUNCH-HYGIENE=PASS violations=0"
expect_lacks "H3 TARGETS declaration is not a violation" "$W/h3.log" "build-decl.sh"
expect_lacks "H3 compliant launch is not a violation" "$W/h3.log" "clean.sh"

echo
if [ "$failures" -ne 0 ]; then
  echo "GATE-HYGIENE-SELFTEST=FAIL assertions=$count failures=$failures"
  exit 1
fi
echo "GATE-HYGIENE-SELFTEST=PASS assertions=$count"