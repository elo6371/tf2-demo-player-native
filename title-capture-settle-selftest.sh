#!/usr/bin/env bash
# title-capture-settle-selftest.sh -- the controls for D-89.
#
# WHAT THIS PROVES, AND HOW
#   D-89: title-capture.ps1 stopped reading after a fixed deadline and then exited
#   0 whenever it had SOME title. On a polluted window the first title is a bare
#   caption ("len=30 brackets=no") and the VPK bracket arrives 29.5-31.6 s in, so
#   a 30 s deadline could capture the caption, call it a success, and let the gate
#   compare a bracket it never read. The fix raises the deadline to 60 s and makes
#   a bracket-less title a failed capture (exit 3), and the gate now asserts the
#   bracket instead of inferring it.
#
#   Nothing here launches the product (no build, no lock): the title-capture loop
#   needs an installed product and a real window. Instead every control measures
#   the files that will run:
#
#   POS-1  title-capture.ps1 still parses (PowerShell's own parser, no errors);
#   POS-2  the parsed default of -Seconds is 60;
#   POS-3  the settle guard's regex, taken from the file, does NOT fire on a
#          connected title and DOES fire on a bare caption -- i.e. the branch
#          discriminates rather than always failing or never failing;
#   POS-4  that branch ends in exit 3, so a bare caption cannot be a passing run;
#   POS-5  the generated installed-product driver (extracted from the gate's own
#          heredoc) parses and hands title-capture Seconds = 60;
#   POS-6  the gate asserts "brackets=yes" on the capture log;
#   POS-7  the arithmetic: the measured worst case (31.6 s) has a 28.4 s margin
#          under 60 s and the deadline covers it;
#   NEG-1  the pre-fix files (git show HEAD:...) used 45 s default and 30 s at the
#          call site, and 30 s < 31.6 s -- the old deadline could not cover the
#          measured settle window, so the old bug was real and not hypothetical;
#   NEG-2  the pre-fix harness had no settle guard at all, which is why
#          brackets=no could exit 0.
#
#   What it does NOT prove: that a real capture now waits long enough (that needs
#   an E5 build and the forward run's 15 cases), and that 60 s covers a window
#   slower than 31.6 s -- only that it covers the measurement quoted in D-89.
#
# Usage: bash title-capture-settle-selftest.sh
#   Exit 0 = every control held, 1 = at least one did not, 2 = could not run.
set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
CAP="$REPO/title-capture.ps1"
GATE="$REPO/install-lifecycle-check.sh"
for f in "$CAP" "$GATE"; do
  if [ ! -f "$f" ]; then echo "D89-SELFTEST=CANNOT-RUN missing $f" >&2; exit 2; fi
done
command -v powershell >/dev/null 2>&1 || { echo "D89-SELFTEST=CANNOT-RUN no powershell" >&2; exit 2; }

WORK="${TMPDIR:-/tmp}/d89-settle-selftest.$$"
rm -rf "$WORK"; mkdir -p "$WORK"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT INT TERM

CHECKS=0; FAIL=0
ok()    { CHECKS=$((CHECKS + 1)); echo "  ok   $1"; }
wrong() { CHECKS=$((CHECKS + 1)); FAIL=$((FAIL + 1)); echo "  FAIL $1 = $2, expected $3"; }
eq()    { if [ "$2" = "$3" ]; then ok "$1 ($2)"; else wrong "$1" "$2" "$3"; fi; }
# Pattern matching goes through case, not "printf | grep -q": under pipefail the
# printf dies of SIGPIPE the moment grep -q finds its match, which turns a large
# matching input into a false FAIL.
has()   { case "$2" in *"$3"*) ok "$1";; *) wrong "$1" "<no '$3'>" "contains '$3'";; esac; }
hasnt() { case "$2" in *"$3"*) wrong "$1" "<has '$3'>" "does not contain '$3'";; *) ok "$1";; esac; }

echo "D-89 settle controls on $(basename "$REPO")"
echo "  capture: $CAP"
echo "  capture sha256: $(sha256sum "$CAP" | cut -d' ' -f1)"
echo "  gate sha256:    $(sha256sum "$GATE" | cut -d' ' -f1)"
echo

# The gate's generated driver is what actually hands parameters to title-capture,
# so it is extracted from the heredoc rather than re-typed here.
awk '/cat > "\$driver" <<.PS./{f=1;next} /^PS$/{f=0} f' "$GATE" > "$WORK/driver.ps1"

cat > "$WORK/read.ps1" <<'PS'
$ErrorActionPreference = 'Stop'
$cap = $args[0]
$driver = $args[1]
$errs = $null; $tokens = $null
$null = [System.Management.Automation.Language.Parser]::ParseFile($cap, [ref]$tokens, [ref]$errs)
"CAPTURE_PARSE_ERRORS=" + @($errs).Count
$ast = [System.Management.Automation.Language.Parser]::ParseFile($cap, [ref]$tokens, [ref]$errs)
$p = $ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Seconds' } | Select-Object -First 1
"CAPTURE_SECONDS_DEFAULT=" + $p.DefaultValue.Value
$text = [System.IO.File]::ReadAllText($cap)
$mk = "notmatch '"
$i = $text.IndexOf($mk) + $mk.Length
$j = $text.IndexOf("'", $i)
$pat = $text.Substring($i, $j - $i)
"GUARD_PATTERN=" + $pat
$settled = 'C:\fixtures\maps\t32_marker.bsp [VPK entries: 1, VPK refused: 0]'
$bare = 'Half-Life 2'
"SETTLED_GUARD_FIRES=" + ($settled -notmatch $pat)
"BARE_GUARD_FIRES=" + ($bare -notmatch $pat)
$derrs = $null; $dtokens = $null
$null = [System.Management.Automation.Language.Parser]::ParseFile($driver, [ref]$dtokens, [ref]$derrs)
"DRIVER_PARSE_ERRORS=" + @($derrs).Count
"DRIVER_TEXT_SECONDS=" + ([regex]::Match([System.IO.File]::ReadAllText($driver), 'Seconds = ([0-9]+)').Groups[1].Value)
PS

powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$WORK/read.ps1")" \
  "$(cygpath -w "$CAP")" "$(cygpath -w "$WORK/driver.ps1")" > "$WORK/read.txt" 2>&1
read_rc=$?
eq "the PowerShell probe itself ran" "$read_rc" "0"
READ="$(cat "$WORK/read.txt")"
val() { printf '%s\n' "$READ" | sed -n "s/^$1=//p" | tail -1; }

eq "POS-1 title-capture.ps1 still parses" "$(val CAPTURE_PARSE_ERRORS)" "0"
eq "POS-2 the parsed default deadline is 60 s" "$(val CAPTURE_SECONDS_DEFAULT)" "60"
eq "POS-3 the settle guard looks for the VPK bracket" "$(val GUARD_PATTERN)" '\[VPK entries:'
eq "POS-3 a connected title does not trip the guard" "$(val SETTLED_GUARD_FIRES)" "False"
eq "POS-3 a bare caption trips the guard" "$(val BARE_GUARD_FIRES)" "True"
GUARD_BLOCK="$(sed -n '/did not settle/,+2p' "$CAP")"
has "POS-4 the unsettled branch prints TITLE-CAPTURE=FAIL" "$GUARD_BLOCK" "TITLE-CAPTURE=FAIL"
has "POS-4 and exits 3 rather than 0" "$GUARD_BLOCK" "exit 3"
eq  "POS-5 the driver parses" "$(val DRIVER_PARSE_ERRORS)" "0"
eq  "POS-5 the driver hands title-capture 60 s" "$(val DRIVER_TEXT_SECONDS)" "60"
has "POS-6 the gate asserts the bracket, not just PASS" "$(cat "$GATE")" '"brackets=yes"'
read -r MARGIN RATIO <<< "$(awk 'BEGIN { worst=31.6; cap=60; printf "%.1f %.2f", cap-worst, cap/worst }')"
eq "POS-7 the deadline covers the measured worst case (60 > 31.6)" \
   "$(awk -v c=60 -v w=31.6 'BEGIN { print (c > w) ? "yes" : "no" }')" "yes"
eq "POS-7 the margin under 60 s is 28.4 s" "$MARGIN" "28.4"
eq "POS-7 and the ratio is 1.90x" "$RATIO" "1.90"

# ---------------------------------------------------------------------- NEG-1/2
echo
echo "NEG: the pre-fix files could not have covered the measured settle window"
git -C "$REPO" show HEAD:title-capture.ps1 > "$WORK/capture-before.ps1" 2>/dev/null
git -C "$REPO" show HEAD:install-lifecycle-check.sh > "$WORK/gate-before.sh" 2>/dev/null
eq "NEG-1 the pre-fix capture file is readable from history" \
   "$([ -s "$WORK/capture-before.ps1" ] && echo present || echo absent)" "present"
cat > "$WORK/read-before.ps1" <<'PS'
$errs = $null; $errs2 = $null; $tokens = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($args[0], [ref]$tokens, [ref]$errs2)
$p = $ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Seconds' } | Select-Object -First 1
"BEFORE_SECONDS_DEFAULT=" + $p.DefaultValue.Value
PS
powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$WORK/read-before.ps1")" \
  "$(cygpath -w "$WORK/capture-before.ps1")" > "$WORK/read-before.txt" 2>&1
BEFORE="$(cat "$WORK/read-before.txt")"
eq "NEG-1 the pre-fix default deadline was 45 s" \
   "$(printf '%s\n' "$BEFORE" | sed -n 's/^BEFORE_SECONDS_DEFAULT=//p' | tail -1)" "45"
eq "NEG-1 and the call site asked for 30 s" \
   "$(sed -n 's/.*Seconds = \([0-9]*\).*/\1/p' "$WORK/gate-before.sh" | head -1)" "30"
eq "NEG-1 30 s cannot cover the measured 31.6 s settle" \
   "$(awk 'BEGIN { print (30 > 31.6) ? "covers" : "short" }')" "short"
eq "NEG-2 the pre-fix harness had no settle guard" \
   "$(grep -c 'did not settle' "$WORK/capture-before.ps1")" "0"
hasnt "NEG-2 so a bracket-less title was a PASS with exit 0" \
   "$(sed -n '/TITLE-CAPTURE=PASS/,$p' "$WORK/capture-before.ps1")" "exit 3"

echo
echo "the two files the controls read still parse as shell"
bash -n "$GATE"; eq "bash -n install-lifecycle-check.sh" "$?" "0"

echo
echo "  checks=$CHECKS fail=$FAIL"
if [ "$FAIL" -eq 0 ]; then echo "  D89-SELFTEST=PASS assertions=$CHECKS"; exit 0; fi
echo "  D89-SELFTEST=FAIL assertions=$CHECKS fail=$FAIL"; exit 1
