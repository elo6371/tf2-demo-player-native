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
  # The product name is assembled by printf so this selftest file does not look
  # like a launch site to the scanner it tests (the fixture it writes does).
  {
    printf '%s\n' '#!/usr/bin/env bash'
    printf 'EXE=native/build-nmake/tf2_demo_%s.exe\n' native
    printf '%s\n' 'DEMO=$1'
    printf '%s\n' '"$EXE" --audio-disabled --hidden-window --demo "$DEMO"'
  } > "$1/$2.sh"
}

write_clean() { # write_clean <dir> <name>
  {
    printf '%s\n' '#!/usr/bin/env bash'
    printf 'EXE=native/build-nmake/tf2_demo_%s.exe\n' native
    printf '%s\n' 'DEMO=$1'
    printf '%s\n' 'TMP=$(mktemp -d)'
    printf '%s\n' '"$EXE" --audio-disabled --hidden-window --demo "$DEMO" --settings-file "$TMP/settings.ini" --ui-settings-file "$TMP/ui.ini"'
  } > "$1/$2.sh"
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
# D-77: D-71 gave the scanner a floor (scripts>=100 launchers>=40 sites>=60) and
# this fixture is two scripts, so the positive control started failing for being
# SMALL rather than dirty -- the floor could not tell the difference between a
# wrong --root and a mock tree. The fixture now passes --ignore-floors, and H3b
# proves the floor still fires when it is not ignored: otherwise "the clean tree
# passes" would only be measuring a switch.
timeout 120 bash "$HERE/gate-launch-hygiene-check.sh" --root "$d3" --ignore-floors > "$W/h3.log" 2>&1; rc=$?
expect_rc "H3 clean tree passes (floors off for the fixture)" "$rc" 0
expect_has "H3 token reports zero violations" "$W/h3.log" "GATE-LAUNCH-HYGIENE=PASS violations=0"
expect_has "H3 the floor skip is printed, not silent" "$W/h3.log" "floors: SKIPPED"
expect_lacks "H3 TARGETS declaration is not a violation" "$W/h3.log" "build-decl.sh"
expect_lacks "H3 compliant launch is not a violation" "$W/h3.log" "clean.sh"

# H3b: the same clean tree, floors ON, must be refused for being too small -- and
# name which count failed. A two-script tree cannot meet scripts>=100.
timeout 120 bash "$HERE/gate-launch-hygiene-check.sh" --root "$d3" > "$W/h3b.log" 2>&1; rc=$?
expect_rc "H3b the floor refuses a clean two-script tree" "$rc" 1
expect_has "H3b the refusal names the scripts floor" "$W/h3b.log" "the scan read scripts=2, below the floor 100"
expect_has "H3b the final token is FAIL" "$W/h3b.log" "GATE-LAUNCH-HYGIENE=FAIL"

echo
if [ "$failures" -ne 0 ]; then
  echo "GATE-HYGIENE-SELFTEST=FAIL assertions=$count failures=$failures"
  exit 1
fi
echo "GATE-HYGIENE-SELFTEST=PASS assertions=$count"