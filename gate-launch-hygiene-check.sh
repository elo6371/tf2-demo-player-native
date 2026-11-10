#!/usr/bin/env bash
# gate-launch-hygiene-check.sh -- T86 / D-22: no gate may launch the product
# without isolating the settings directory, and no new offender may sneak in.
#
# WHY THIS EXISTS
#   The product WRITES on every normal exit: settings.ini (camera bookmarks, hotkeys,
#   native/src/main.cpp:11613-11616 and :11627-11629) and ui.ini (recentDemo0..7,
#   native/src/native_ui.cpp:196-197 / :137-143). A gate that launches the product
#   without --settings-file / --ui-settings-file therefore edits the machine's real
#   %LOCALAPPDATA%/TF2 Demo Player/{settings.ini,ui.ini}. That is D-22.
#   Worse, a gate may pass a flag that POINTS AT TRACKED EVIDENCE (third-person-weapon
#   passes $BASELINE/ui.json, player-body-appearance passes $INPUT/ui.json,
#   probe_attach passes $BASE/ui.json): the flag exists, yet the run still rewrites a
#   tracked file and the input stops being reproducible (D-18). Both are violations.
#
# WHAT THIS GATE ASSERTS (static, no product launch, no host lock needed)
#   Enumerate every launch site in the tree's *.sh that actually executes the product
#   binary, then require each non-lifecycle launch to carry BOTH --settings-file and
#   --ui-settings-file, with values that do not resolve into evidence/ or AppData.
#   Verdict = SET EQUALITY against a frozen baseline:
#     - a launch row not in the baseline        -> FAIL "new unisolated launch"
#     - a baseline row that no longer violates  -> FAIL "baseline stale"
#   This is a ratchet: fixing a gate without updating the baseline is also a failure,
#   so the baseline always describes reality instead of drifting into decoration.
#   The baseline is read with CR stripped: core.autocrlf=true means a fresh checkout
#   delivers it with CRLF while the scan always emits LF, and a byte-wise compare
#   then calls every row new AND stale at once.
#
# WHAT IT DOES NOT SEE (model boundaries, not bugs -- do not report a false PASS)
#   - Launches made indirectly: python heredocs spawning the exe
#     (entity-material-check.sh:155 does subprocess.Popen), build-only references.
#     PowerShell launches are no longer silent (see D-50b below), but they are
#     WARN level: their flags are collected, never ratcheted.
#   - Flags forwarded through "$@": statically invisible, so an array that ends in
#     "$@" is judged by the literal flags in its own body.
#   - Whether the run ACTUALLY stays isolated at runtime; this gate is a static
#     ratchet and must be paired with verify-release.sh's machine-isolation step.
#   - A command BUILT at run time (D-76). Six gates wrap a launch as
#     cmd=$(printf %q ... "$PRODUCT" ...); bash -c "$cmd" -- the tokenizer sees
#     no exe in command position, so the launch was invisible. It is now a WARN
#     line per site instead of silence (fog-check.sh:613, world-geometry-budget-
#     check.sh:158, map-geometry-lighting-check.sh:273 as of 2026-11-10). Still not
#     checked: the flags inside that string. WARN, not VIOLATION, because six
#     scripts already have the shape and red lines would say the tree got worse
#     when what got worse is this scanner's reach.
#
# D-50b: the ratchet above reads root *.sh only, so the tree's PowerShell launchers
#   were entirely outside its reach. Measured on 2026-11-10 (t105-e5): 20 tracked
#   *.ps1, five of which execute the product, and TWO of those --
#   native/tools/native_acceptance_gate.ps1:58 and
#   native/tools/native_stability_gate.ps1:35 -- pass neither --settings-file nor
#   --ui-settings-file, so each run rewrites the machine's real
#   %LOCALAPPDATA%/TF2 Demo Player/{settings.ini,ui.ini} exactly as D-22 describes.
#   They are reported and counted (pslaunch= / psunflagged= on the token) but NOT
#   added as baseline rows: the baseline is a set of shell rows a single edit can
#   fix, and a row this scanner cannot re-derive after the fix would be stale the
#   moment it is fixed. Fixing those two launchers is what drives psunflagged to 0;
#   the count makes them visible until then -- the same reasoning that made D-76 a
#   WARN instead of a red line.
#   WHAT THE POWERSHELL PASS READS: every *.ps1 under the scanned root except
#   evidence/ (history, not launchers), .scratch/ and build*/; a site is a
#   Start-Process, a [System.Diagnostics.Process]::Start($psi) whose FileName names
#   the product, or an "& <exe>" call. Flags are collected from the list-building
#   lines ($a.Add('--x'), $a += @('--x'), $a = @('--x')), followed through
#   $psi.Arguments / -ArgumentList and one level of variable indirection. A launcher
#   that assembles flags inside a function, or splats @PSBoundParameters, is counted
#   unflagged -- conservative on purpose: silence is what this pass exists to end.
#
# D-92: the per-line loop read multi-line array literals as if their elements were
#   commands. install-lifecycle-check.sh:106-112 names the package payload in
#   PAYLOAD_NAMES=( ... ), and the two elements that contain the product's file name
#   (:108 tf2_demo_native.exe, :109 tf2_demo_native.identity) were each recorded as a
#   direct launch carrying neither flag -- two violations that were data, not code.
#   An element line inside a multi-line array literal is now skipped, with one
#   exception kept on purpose: a line that runs a command substitution ($( ) or a
#   backtick) is still scanned, because a launcher can hide inside it. array-def and
#   array-exec detection is untouched: "$ARR[@]" in command position and an array
#   whose body starts with the product are still read as launches. This tree had been
#   FAIL on this gate since 9fea1d1 (D-54) added the identity element; the earlier
#   CAND-A2 hygiene PASS was measured on b61af91, a tree from before that merge, and
#   cannot be cited for the tree that carries it.
#
# USAGE
#   bash gate-launch-hygiene-check.sh                   # check (0 PASS / 1 FAIL / 90 tool error)
#   bash gate-launch-hygiene-check.sh --update-baseline # rewrite baseline, print +/- rows
#   bash gate-launch-hygiene-check.sh --root DIR        # scan another tree (negative controls)
#   bash gate-launch-hygiene-check.sh --root DIR --ignore-floors
#                                                       # fixture trees only: a two-script
#                                                       # tree cannot meet a 100-script floor,
#                                                       # and the positive control that
#                                                       # proves a clean tree passes needs
#                                                       # the floors out of the way (D-77).
#                                                       # The shipping run never passes it;
#                                                       # the skip is printed, not silent.
#   bash gate-launch-hygiene-check.sh --selftest-pwsh   # D-50b controls: three mock trees
#                                                       # proving a PowerShell site is
#                                                       # counted and WARNed, a compliant one
#                                                       # is not, and the shell ratchet
#                                                       # still fails what it failed before.
#                                                       # No product, no lock, mktemp only.
#   bash gate-launch-hygiene-check.sh --selftest-array  # D-92 controls: three mock trees
#                                                       # proving an array element is data,
#                                                       # a launch after the array still
#                                                       # fails, and a launcher hidden in
#                                                       # $( ) inside the array is caught.
#                                                       # No product, no lock, mktemp only.
#
# Last line is a machine token:
#   GATE-LAUNCH-HYGIENE=PASS|FAIL violations=N baseline=M warnings=W lifecycle=K arrskipped=S pslaunch=A psunflagged=B
set -uo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SCAN_ROOT="$ROOT"
UPDATE=0
FLOORS=1
SELFTEST_PWSH=0
SELFTEST_ARRAY=0
while [ $# -gt 0 ]; do
  case "$1" in
    --root) SCAN_ROOT="$(cd "$2" && pwd)"; shift 2;;
    --update-baseline) UPDATE=1; shift;;
    --ignore-floors) FLOORS=0; shift;;
    --selftest-pwsh) SELFTEST_PWSH=1; shift;;
    --selftest-array) SELFTEST_ARRAY=1; shift;;
    --help|-h) sed -n '2,109p' "$0"; exit 0;;
    *) echo "GATE-LAUNCH-HYGIENE=FAIL unknown argument: $1"; exit 2;;
  esac
done
BASELINE="$SCAN_ROOT/evidence/gate-launch-hygiene/baseline.txt"
TMP="${TMPDIR:-/tmp}/gate-launch-hygiene.$$"
mkdir -p "$TMP"; trap 'rm -rf "$TMP"' EXIT

# The scanner is python: shell tokenisation is not something grep can do. Probe
# interpreters, because on this host 'python3' can be a Windows Store stub exiting 49,
# so a candidate only counts if it really prints something.
PY=""
for c in python python3 py; do
  if command -v "$c" >/dev/null 2>&1 && "$c" -c 'print(1)' >/dev/null 2>&1; then PY="$c"; break; fi
done
if [ -z "$PY" ]; then
  echo "GATE-LAUNCH-HYGIENE=FAIL tool-error: no working python interpreter"; exit 90
fi

# D-50b controls: three mock trees, no product, no lock, mktemp only. The shell
# ratchet has its own selftest (gate-hygiene-selftest.sh); this one covers the
# PowerShell pass, which that file cannot see.
run_pwsh_selftest() {
  local W; W=$(mktemp -d) || { echo "HYGIENE-PWSH-SELFTEST=FAIL tool-error: mktemp"; return 90; }
  local checks=0 fails=0 rc=0 exe_name d
  exe_name=$(printf 'tf2_demo_%s.exe' native)

  ok()   { checks=$((checks + 1)); printf '  OK   %s\n' "$1"; }
  bad()  { checks=$((checks + 1)); fails=$((fails + 1)); printf '  FAIL %s\n' "$1"; }
  has()  { if grep -qaF "$3" "$2"; then ok "$1"; else bad "$1 (no: $3)"; fi; }
  lacks(){ if grep -qaF "$3" "$2"; then bad "$1 (found: $3)"; else ok "$1"; fi; }
  rcis() { if [ "$2" = "$3" ]; then ok "$1 (rc=$3)"; else bad "$1 (rc=$2, expected $3)"; fi; }

  echo "=== gate-launch-hygiene selftest: PowerShell pass (D-50b), mock trees ==="

  # P1: a .ps1 that starts the product with no isolation flags -> counted, WARNed,
  #     and still PASS (the whole point of registering the shape first), with a
  #     copy under evidence/ proving that directory is not scanned.
  d=$W/p1; mkdir -p "$d/evidence/gate-launch-hygiene" "$d/evidence/old"
  : > "$d/evidence/gate-launch-hygiene/baseline.txt"
  {
    printf '$exe = Join-Path $PSScriptRoot %s\n' "'$exe_name'"
    printf '%s\n' "\$arguments = @('--no-vsync', '--demo', \$Demo)"
    printf '%s\n' 'Start-Process -FilePath $exe -ArgumentList $arguments'
  } > "$d/evil.ps1"
  cp "$d/evil.ps1" "$d/evidence/old/old.ps1"
  timeout 120 bash "$0" --root "$d" --ignore-floors > "$W/p1.log" 2>&1; rc=$?
  rcis "P1 an unflagged PowerShell launch is not fatal (WARN, not VIOLATION)" "$rc" 0
  has  "P1 the site is counted" "$W/p1.log" "pslaunch=1"
  has  "P1 the missing flags are counted" "$W/p1.log" "psunflagged=1"
  has  "P1 the scan names the file" "$W/p1.log" "evil.ps1"
  has  "P1 the PowerShell pass does not inflate the shell warning count" "$W/p1.log" "warnings=0"
  lacks "P1 evidence/ is not scanned for PowerShell launchers" "$W/p1.log" "old.ps1"

  # P2: the same launch with both flags plumbed through a List -> flagged.
  d=$W/p2; mkdir -p "$d/evidence/gate-launch-hygiene"
  : > "$d/evidence/gate-launch-hygiene/baseline.txt"
  {
    printf '$exe = Join-Path $PSScriptRoot %s\n' "'$exe_name'"
    printf '%s\n' '$args = New-Object System.Collections.Generic.List[string]'
    printf '%s\n' "\$args.Add('--demo'); \$args.Add(\$Demo)"
    printf '%s\n' "\$args.Add('--settings-file'); \$args.Add(\$settings)"
    printf '%s\n' "\$args.Add('--ui-settings-file'); \$args.Add(\$uiSettings)"
    printf '%s\n' '$psi = New-Object System.Diagnostics.ProcessStartInfo'
    printf '%s\n' '$psi.FileName = $exe'
    printf '%s\n' "\$psi.Arguments = (\$args | ForEach-Object { \$_ }) -join ' '"
    printf '%s\n' '$proc = [System.Diagnostics.Process]::Start($psi)'
  } > "$d/good.ps1"
  timeout 120 bash "$0" --root "$d" --ignore-floors > "$W/p2.log" 2>&1; rc=$?
  rcis "P2 a compliant PowerShell launch passes" "$rc" 0
  has  "P2 the site is still counted" "$W/p2.log" "pslaunch=1"
  has  "P2 no missing flag is reported" "$W/p2.log" "psunflagged=0"
  lacks "P2 no WARN line is printed for a compliant launcher" "$W/p2.log" "pass neither --settings-file nor --ui-settings-file"

  # P3: the shell ratchet still fires in a tree that also carries a .ps1 -- the new
  #     pass must add a reading, never replace the old verdict.
  d=$W/p3; mkdir -p "$d/evidence/gate-launch-hygiene"
  : > "$d/evidence/gate-launch-hygiene/baseline.txt"
  cp "$W/p1/evil.ps1" "$d/evil.ps1"
  {
    printf '%s\n' '#!/usr/bin/env bash'
    printf 'EXE=native/build-nmake/%s\n' "$exe_name"
    printf '%s\n' 'DEMO=$1'
    printf '%s\n' '"$EXE" --audio-disabled --hidden-window --demo "$DEMO"'
  } > "$d/evil.sh"
  timeout 120 bash "$0" --root "$d" --ignore-floors > "$W/p3.log" 2>&1; rc=$?
  rcis "P3 an unisolated shell launch still fails" "$rc" 1
  has  "P3 the failure is named" "$W/p3.log" "new unisolated launch"
  has  "P3 it names the shell script" "$W/p3.log" "evil.sh"
  has  "P3 the PowerShell count survives in the FAIL token" "$W/p3.log" "psunflagged=1"

  echo
  if [ "$fails" -ne 0 ]; then
    echo "HYGIENE-PWSH-SELFTEST=FAIL checks=$checks failures=$fails"
    return 1
  fi
  echo "HYGIENE-PWSH-SELFTEST=PASS checks=$checks"
  return 0
}

# D-92 controls: three mock trees. The first is the shape that made this tree red --
# a payload list that names the product twice with nothing executing it; the second
# proves a real launch after such a list still fails; the third proves that a launch
# hidden in a command substitution inside the array body is still caught, because
# that is the one element shape the skip deliberately leaves in the main scan.
run_array_selftest() {
  local W; W=$(mktemp -d) || { echo "HYGIENE-ARRAY-SELFTEST=FAIL tool-error: mktemp"; return 90; }
  local checks=0 fails=0 rc=0 exe_name d
  exe_name=$(printf 'tf2_demo_%s.exe' native)

  ok()   { checks=$((checks + 1)); printf '  OK   %s\n' "$1"; }
  bad()  { checks=$((checks + 1)); fails=$((fails + 1)); printf '  FAIL %s\n' "$1"; }
  has()  { if grep -qaF "$3" "$2"; then ok "$1"; else bad "$1 (no: $3)"; fi; }
  lacks(){ if grep -qaF "$3" "$2"; then bad "$1 (found: $3)"; else ok "$1"; fi; }
  rcis() { if [ "$2" = "$3" ]; then ok "$1 (rc=$3)"; else bad "$1 (rc=$2, expected $3)"; fi; }

  echo "=== gate-launch-hygiene selftest: an array element is data (D-92), mock trees ==="

  # A1: the install-lifecycle-check.sh shape -- a multi-line payload list naming the
  #     product. Nothing executes it, so there is no launch and no violation.
  d=$W/a1; mkdir -p "$d/evidence/gate-launch-hygiene"
  : > "$d/evidence/gate-launch-hygiene/baseline.txt"
  {
    printf '%s\n' '#!/usr/bin/env bash'
    printf '%s\n' 'PAYLOAD_NAMES=('
    printf '  %s\n' "$exe_name"
    printf '  %s\n' "$(printf 'tf2_demo_%s.identity' native)"
    printf '%s\n' ')'
    printf '%s\n' 'echo "${#PAYLOAD_NAMES[@]} names"'
  } > "$d/list.sh"
  timeout 120 bash "$0" --root "$d" --ignore-floors > "$W/a1.log" 2>&1; rc=$?
  rcis "A1 a payload list that names the product is not a launch" "$rc" 0
  has  "A1 the scan reports zero violations" "$W/a1.log" "violations=0"
  has  "A1 the skipped lines are counted, not silent" "$W/a1.log" "array-element lines skipped as data (D-92): 2"
  has  "A1 the token carries the skip count" "$W/a1.log" "arrskipped=2"
  lacks "A1 nothing is named as an unisolated launch" "$W/a1.log" "new unisolated launch"

  # A2: the same list followed by a real, unisolated launch -- that launch MUST fail.
  d=$W/a2; mkdir -p "$d/evidence/gate-launch-hygiene"
  : > "$d/evidence/gate-launch-hygiene/baseline.txt"
  cp "$W/a1/list.sh" "$d/list.sh"
  {
    printf '%s\n' '#!/usr/bin/env bash'
    printf '%s\n' 'DEMO=$1'
    printf 'EXE=native/build-nmake/%s\n' "$exe_name"
    printf '%s\n' '"$EXE" --demo "$DEMO"'
  } >> "$d/list.sh"
  timeout 120 bash "$0" --root "$d" --ignore-floors > "$W/a2.log" 2>&1; rc=$?
  rcis "A2 a real launch after an array body still fails" "$rc" 1
  has  "A2 the failure is named" "$W/a2.log" "new unisolated launch"
  has  "A2 it names the script" "$W/a2.log" "list.sh"

  # A3: a launcher hidden inside a command substitution that sits in the array body.
  #     The skip exempts lines with $( ), so this one must still be caught.
  d=$W/a3; mkdir -p "$d/evidence/gate-launch-hygiene"
  : > "$d/evidence/gate-launch-hygiene/baseline.txt"
  {
    printf '%s\n' '#!/usr/bin/env bash'
    printf '%s\n' 'DEMO=$1'
    printf 'EXE=native/build-nmake/%s\n' "$exe_name"
    printf '%s\n' 'TOOLS=('
    printf '%s\n' '"$(cygpath -w "$PWD")"/'"$exe_name"' --demo "$DEMO"'
    printf '%s\n' ')'
  } > "$d/a3.sh"
  timeout 120 bash "$0" --root "$d" --ignore-floors > "$W/a3.log" 2>&1; rc=$?
  rcis "A3 a launcher inside a command substitution in the array body is still caught" "$rc" 1
  has  "A3 the failure is named" "$W/a3.log" "new unisolated launch"
  has  "A3 it names the script" "$W/a3.log" "a3.sh"

  echo
  if [ "$fails" -ne 0 ]; then
    echo "HYGIENE-ARRAY-SELFTEST=FAIL checks=$checks failures=$fails"
    return 1
  fi
  echo "HYGIENE-ARRAY-SELFTEST=PASS checks=$checks"
  return 0
}

if [ "$SELFTEST_PWSH" -eq 1 ]; then
  run_pwsh_selftest
  exit $?
fi

if [ "$SELFTEST_ARRAY" -eq 1 ]; then
  run_array_selftest
  exit $?
fi

"$PY" - "$SCAN_ROOT" > "$TMP/scan.txt" <<'PYCODE'
import os, re, sys
from collections import defaultdict

HARD = ('--settings-file', '--ui-settings-file')
WARN = ('--audio-disabled', '--hidden-window')
LIFECYCLE_FLAGS = {'--install', '--install-status', '--version', '--uninstall'}

def logical(lines):
    out=[]; buf=''; start=None
    for i,l in enumerate(lines,1):
        s=l.rstrip('\n')
        if start is None: start=i
        if s.endswith('\\'):
            buf += s[:-1] + ' '; continue
        buf += s; out.append((start,buf)); buf=''; start=None
    if buf: out.append((start,buf))
    return out

def shell_tokens(s):
    toks=[]; cur=''; i=0; n=len(s)
    def flush():
        nonlocal cur
        if cur!='': toks.append(cur); cur=''
    while i<n:
        c=s[i]
        if c in ' \t':
            flush(); i+=1; continue
        if c=='#':
            break
        if c=="'":
            j=s.find("'",i+1)
            if j<0: cur+=s[i:]; i=n
            else: cur+=s[i:j+1]; i=j+1
            continue
        if c=='"':
            j=i+1; depth=0
            while j<n:
                if s[j]=='\\': j+=2; continue
                if s[j]=='$' and j+1<n and s[j+1]=='(':
                    depth+=1; j+=2; continue
                if depth>0 and s[j]=='(': depth+=1; j+=1; continue
                if depth>0 and s[j]==')': depth-=1; j+=1; continue
                if s[j]=='"' and depth==0: j+=1; break
                j+=1
            cur+=s[i:j]; i=j; continue
        if c=='\\' and i+1<n:
            cur+=s[i:i+2]; i+=2; continue
        cur+=c; i+=1
    flush(); return toks

ASSIGN=re.compile(r'^([A-Za-z_]\w*)=(.*)$')
ARR_HEAD=re.compile(r'^\s*(?:local\s+(?:-a\s+)?|declare\s+(?:-a\s+)?|readonly\s+)?([A-Za-z_]\w*)(\+?)=\(([\s\S]*)$')

def close_paren(text, start, depth=1):
    i=start; n=len(text); q=None
    while i<n:
        c=text[i]
        if q:
            if c=='\\': i+=2; continue
            if c==q: q=None
            i+=1; continue
        if c in '"\'': q=c; i+=1; continue
        if c=='(': depth+=1
        elif c==')':
            depth-=1
            if depth==0: return i
        i+=1
    return -1

def collect(lines):
    pvars=set(); arrays=defaultdict(list); assigns=[]; i=0
    body_lines=set()
    while i<len(lines):
        ln,t=lines[i]; st=t.strip()
        m=ASSIGN.match(st)
        if m and not st.startswith('#'):
            val=m.group(2).strip()
            assigns.append((ln,m.group(1),val))
            if 'tf2_demo_native' in val: pvars.add(m.group(1))
        a=ARR_HEAD.match(t)
        if a and not st.startswith('#'):
            name,app,rest=a.group(1),a.group(2),a.group(3)
            text=rest; next_i=i+1
            k=close_paren(text,0)
            while k<0 and next_i<len(lines):
                bln, bl = lines[next_i][0], lines[next_i][1]
                text += '\n'+bl; next_i+=1
                k=close_paren(text,0)
                # D-92: a line that is fully inside a multi-line array literal is an
                # element -- data, not a command position, so the main scan must not
                # read it as a launch. Two lines keep their place in that scan: the
                # line that CLOSES the literal (a command may follow the ")" on it),
                # and any line that runs a command substitution ($( ) or a backtick,
                # where a launcher can hide).
                if k<0 and '$(' not in bl and '`' not in bl:
                    body_lines.add(bln)
            body=text[:k] if k>=0 else text
            arrays[name].append((ln,app,body))
            i=next_i; continue
        i+=1
    return pvars, arrays, assigns, body_lines

def body_before(arrays,name,at):
    defs=[d for d in arrays.get(name,[]) if d[0]<=at]
    if not defs: return ''
    bases=[d for d in defs if not d[1]]
    base=bases[-1] if bases else defs[-1]
    parts=[base[2]]
    for ln,app,body in defs:
        if ln>base[0] and app: parts.append(body)
    return chr(10).join(parts)

def tok_strip(t):
    t=t.strip()
    if len(t)>=2 and t[0]==t[-1] and t[0] in '"\'':
        return t[1:-1]
    return t

def token_is_product(tok,pvars):
    t=tok_strip(tok)
    if re.match(r'^[A-Za-z_]\w*=',t): return False
    if 'tf2_demo_native' in t: return True
    m=re.match(r'^\$\{?([A-Za-z_]\w*)\}?$',t)
    return bool(m and m.group(1) in pvars)

PREFIX_WORDS={'if','then','else','elif','!','exec','env','nohup','time','command','do','while'}
def command_index(toks,pvars):
    i=0
    while i<len(toks):
        t=toks[i]
        if re.match(r'^[A-Za-z_]\w*=',tok_strip(t)): i+=1; continue
        if t in PREFIX_WORDS: i+=1; continue
        if t=='timeout':
            i+=1
            while i<len(toks) and toks[i].startswith('-'):
                if toks[i] in ('-k','-s','--kill-after','--signal'): i+=2
                else: i+=1
            i+=1  # the DURATION token, then the real command word
            continue
        break
    return i

def array_expansion(tok):
    t=tok_strip(tok)
    m=re.match(r'^\$\{?([A-Za-z_]\w*)\[@\]\}?$',t)
    return m.group(1) if m else None

def flag_map(tokens):
    out=defaultdict(list)
    for i,t in enumerate(tokens[:-1]):
        k=tok_strip(t)
        if k in HARD or k in WARN:
            out[k].append(tokens[i+1])
    return out

def resolve_value(tok, assigns, at):
    t=tok_strip(tok)
    # A default expression such as ${VAR:-evidence/...} can never be resolved to a
    # private path: if the literal text already names tracked evidence, it is shared
    # no matter which branch the shell takes.
    if 'evidence/' in t.replace('\\','/') or 'AppData' in t: return 'shared'
    m=re.match(r'^\$\{?([A-Za-z_]\w*)\}?(.*)$',t)
    if m:
        name,suffix=m.group(1),m.group(2)
        lit=None
        for ln,n,v in assigns:
            if n==name and ln<=at: lit=v
        if lit is None: return 'unknown'
        t=tok_strip(lit)+suffix
        if 'evidence/' in t.replace('\\','/') or 'AppData' in t: return 'shared'
    if '$' in t: return 'unknown'
    t=t.replace('\\','/')
    if 'evidence/' in t or '/AppData/' in t or 'LOCALAPPDATA' in t: return 'shared'
    return 'isolated'

# D-76: a command BUILT at run time. printf %q ... ; bash -c "$cmd" is how six
# gates wrap a product launch, and the tokenizer below cannot see the exe inside
# the string -- so the launch is neither a site nor a wrapped launch, and a gate
# that forgot --settings-file would have been invisible rather than red. This is
# a warning and not a violation on purpose: six scripts already have the shape and
# turning that into six red lines would say the tree got worse when what got worse
# is the SCANNER's reach. Make it a violation once every site is either visible or
# recorded as exempt by name.
DYN_EXEC=re.compile(r'(?:^|[;|&(]|\bthen\b|\bdo\b|\belse\b)\s*(?:bash|sh)\s+-c\s+["\']?\$')

def scan_script(path):
    raw=open(path,encoding='utf-8',errors='replace').read().split('\n')
    logs=logical(raw); pvars,arrays,assigns,arr_body=collect(logs)
    sites=[]; wrapped=[]; dynamic=[]
    if not pvars: return sites,wrapped,dynamic,len(arr_body)
    for ln,t in logs:
        # D-92: an array element is data. Skipping it here (rather than inside
        # collect) leaves array-def and array-exec detection untouched.
        if ln in arr_body: continue
        if not t.lstrip().startswith('#') and DYN_EXEC.search(t): dynamic.append(ln)
        toks=shell_tokens(t)
        if not toks: continue
        fl=[tok_strip(x) for x in toks if tok_strip(x).startswith('--')]
        if fl and any('tf2_demo_native' in tok_strip(x) for x in toks):
            wrapped.append((ln,fl))
        # NAME=$(cmd ...) is an assignment whose RHS is a command substitution: the
        # product name appearing inside sha256sum/cygpath arguments is not a launch.
        if re.match(r'^[A-Za-z_]\w*=\$\(', t.strip()): continue
        i=command_index(toks,pvars)
        if i>=len(toks): continue
        word=toks[i]
        extra=[]
        for x in toks[i:]:
            n=array_expansion(x)
            if n: extra += shell_tokens(body_before(arrays,n,ln))
        if token_is_product(word,pvars):
            sites.append((ln,'direct',toks+extra,assigns))
        else:
            n=array_expansion(word)
            if n:
                bt=shell_tokens(body_before(arrays,n,ln))
                j=command_index(bt,pvars)
                if j<len(bt) and token_is_product(bt[j],pvars):
                    sites.append((ln,'array-exec',bt+toks,assigns))
    for name,defs in arrays.items():
        for ln,app,body in defs:
            bt=shell_tokens(body); j=command_index(bt,pvars) if bt else 0
            if bt and j<len(bt) and token_is_product(bt[j],pvars):
                if not any(s[0]==ln for s in sites):
                    sites.append((ln,'array-def',bt,assigns))
    direct_lines=set(s[0] for s in sites)
    wrapped=[w for w in wrapped if w[0] not in direct_lines]
    return sites,wrapped,dynamic,len(arr_body)

# --------------------------------------------------------------- PowerShell (D-50b)
# The shell scan above reads root *.sh and nothing else, so every PowerShell
# launcher in the tree was invisible -- see the D-50b paragraph in the header.
# This pass finds the shape and counts it; it never touches the baseline.
PS_FLAGLIT = re.compile(r"""['"]--([\w-]+)['"]""")
PS_SKIP_DIRS = {'evidence', 'node_modules'}

def ps_files(root):
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames
                             if d not in PS_SKIP_DIRS
                             and not d.startswith('.')
                             and not d.startswith('build'))
        for fn in sorted(filenames):
            if fn.lower().endswith('.ps1'):
                p = os.path.join(dirpath, fn)
                out.append((os.path.relpath(p, root).replace('\\', '/'), p))
    return out

def ps_collect(lines):
    """var -> flags it accumulates, and var -> the RHS it was assigned."""
    flags = defaultdict(set); rhs = {}
    for t in lines:
        if t.lstrip().startswith('#'): continue
        if re.search(r'\.\s*Add\s*\(|\+=\s*@\(|=\s*@\(', t):
            fs = PS_FLAGLIT.findall(t)
            m = re.search(r'\$(\w+)\s*(?:\.\s*Add\s*\(|\+=|=\s*@\()', t)
            if fs and m:
                flags[m.group(1)] |= set('--' + x for x in fs)
        m = re.search(r'\$(\w+)\s*\+=\s*[\'"]--([\w-]+)[\'"]', t)
        if m: flags[m.group(1)].add('--' + m.group(2))
        m = re.match(r'\s*\$(\w+)\s*=\s*(.*)$', t)
        if m: rhs[m.group(1)] = m.group(2)
    return flags, rhs

def ps_resolve(var, flags, rhs, seen=frozenset(), depth=0):
    if depth > 4 or var in seen: return set()
    out = set(flags.get(var, ()))
    for v in re.findall(r'\$(\w+)', rhs.get(var, '')):
        out |= ps_resolve(v, flags, rhs, seen | {var}, depth + 1)
    return out

def ps_pvars(lines):
    """Variables whose assignment names the product, looking 10 lines ahead so
    that a multi-line assignment ($exe = if (...) { Join-Path ... 'tf2_demo_native.exe' })
    is still recognised."""
    out = set()
    for i, t in enumerate(lines):
        m = re.match(r'\s*\$(\w+)\s*=', t)
        if m and 'tf2_demo_native' in chr(10).join(lines[i:i + 10]):
            out.add(m.group(1))
    return out

def ps_scan(root):
    rows = []
    for rel, path in ps_files(root):
        try:
            lines = open(path, encoding='utf-8', errors='replace').read().split(chr(10))
        except OSError:
            continue
        flags, rhs = ps_collect(lines)
        pvars = ps_pvars(lines)
        psi_file = {}; psi_args = {}
        for t in lines:
            m = re.match(r'\s*\$(\w+)\.FileName\s*=\s*(.*)$', t)
            if m: psi_file[m.group(1)] = m.group(2)
            m = re.match(r'\s*\$(\w+)\.Arguments\s*=\s*(.*)$', t)
            if m: psi_args[m.group(1)] = m.group(2)
        def productish(expr):
            if 'tf2_demo_native' in expr: return True
            return any(v in pvars for v in re.findall(r'\$(\w+)', expr))
        for i, t in enumerate(lines):
            if t.lstrip().startswith('#'): continue
            found = None
            if 'Start-Process' in t:
                m = re.search(r'-FilePath\s+("[^"]+"|' + "'[^']+'" + r'|\S+)', t)
                if m: exe = m.group(1)
                else:
                    rest = t.split('Start-Process', 1)[1].strip()
                    exe = rest.split()[0] if rest else ''
                if productish(exe):
                    m2 = re.search(r'-ArgumentList\s+("[^"]*"|' + "'[^']*'" + r'|\S+)', t)
                    found = ('Start-Process', m2.group(1) if m2 else t)
            if found is None:
                m = re.search(r'::\s*Start\s*\(\s*\$(\w+)\s*\)', t)
                if m and productish(psi_file.get(m.group(1), '')):
                    found = ('ProcessStartInfo', psi_args.get(m.group(1), ''))
            if found is None:
                m = re.match(r'\s*&\s*("[^"]+"|' + "'[^']+'" + r'|\S+)', t)
                if m and productish(m.group(1)):
                    found = ('call-operator', t)
            if found is None: continue
            verb, aexpr = found
            fl = set('--' + x for x in PS_FLAGLIT.findall(aexpr))
            for v in re.findall(r'\$(\w+)', aexpr):
                fl |= ps_resolve(v, flags, rhs)
            missing = [h for h in HARD if h not in fl]
            rows.append((rel, i + 1, verb, 'unflagged' if missing else 'flagged'))
    return rows

def main(root):
    scripts=sorted(f for f in os.listdir(root) if f.endswith('.sh') and os.path.isfile(os.path.join(root,f)))
    violations=[]; warnings=[]; lifecycle=[]; nlaunch=0; nsites=0; arrskipped=0
    for sc in scripts:
        sites,wrapped,dynamic,ns=scan_script(os.path.join(root,sc))
        arrskipped+=ns
        if ns:
            print('INFO|%s|%d array-element line(s) read as data, not command positions (D-92)'%(sc,ns))
        for ln in dynamic:
            warnings.append((sc,'L%d dynamic exec: bash -c "$var" builds the command at run time, so a product launch inside it is not statically checkable (D-76)'%ln))
        if not sites:
            # Wrapper launches (product path as an ARGUMENT, e.g. run_exe ...): not
            # asserted, but a wrapper that only drives --version/--install/
            # --install-status/--uninstall with no --demo is why lifecycle is exempt.
            wflags=set(f for ln,fl in wrapped for f in fl)
            if wrapped and (wflags & LIFECYCLE_FLAGS) and '--demo' not in wflags:
                lifecycle.append(sc)
            elif wrapped:
                for ln,fl in wrapped:
                    print('INFO|%s|L%d|wrapped launch (product as argv, flags not statically checked): %s'%(
                        sc,ln,' '.join(sorted(set(fl)))))
            continue
        nlaunch+=1; nsites+=len(sites)
        allflags=set()
        for ln,kind,tokens,assigns in sites:
            allflags |= set(flag_map(tokens))
        if allflags & LIFECYCLE_FLAGS and '--demo' not in allflags:
            lifecycle.append(sc); continue
        for flag in HARD:
            verdicts=set()
            for ln,kind,tokens,assigns in sites:
                fm=flag_map(tokens)
                if flag not in fm: verdicts.add('missing'); continue
                for v in fm[flag]: verdicts.add(resolve_value(v,assigns,ln))
            if 'missing' in verdicts:
                violations.append((sc,flag))
            elif 'shared' in verdicts:
                violations.append((sc,'shared '+flag))
        for flag in WARN:
            if flag not in allflags: warnings.append((sc,'missing '+flag))
    for sc,what in sorted(set(violations)): print('VIOLATION|%s|%s'%(sc,what))
    for sc,what in sorted(set(warnings)): print('WARN|%s|%s'%(sc,what))
    for sc in sorted(set(lifecycle)): print('LIFECYCLE|%s'%sc)
    psrows=ps_scan(root)
    for rel,ln,verb,state in psrows:
        print('PS|%s|L%d|%s|%s'%(rel,ln,verb,state))
    pslaunch=len(psrows)
    psunflag=sum(1 for r in psrows if r[3]=='unflagged')
    print('STAT|scripts=%d|launchers=%d|sites=%d|violations=%d|warnings=%d|lifecycle=%d|arrskipped=%d|pslaunch=%d|psunflagged=%d'%(
        len(scripts),nlaunch,nsites,len(set(violations)),len(set(warnings)),len(set(lifecycle)),arrskipped,pslaunch,psunflag))

main(os.path.abspath(sys.argv[1]))
PYCODE
RC=$?
if [ $RC -ne 0 ]; then
  echo "GATE-LAUNCH-HYGIENE=FAIL tool-error: scanner exited $RC"; exit 90
fi

grep '^VIOLATION|' "$TMP/scan.txt" | cut -d'|' -f2,3 | sort > "$TMP/now.txt" || true
grep '^WARN|'      "$TMP/scan.txt" | cut -d'|' -f2,3 | sort > "$TMP/warn.txt" || true
STAT=$(grep '^STAT|' "$TMP/scan.txt" | head -1)
W=$(echo "$STAT" | sed -n 's/.*warnings=\([0-9]*\).*/\1/p')
K=$(echo "$STAT" | sed -n 's/.*lifecycle=\([0-9]*\).*/\1/p')
PSL=$(echo "$STAT" | sed -n 's/.*pslaunch=\([0-9]*\).*/\1/p')
PSU=$(echo "$STAT" | sed -n 's/.*psunflagged=\([0-9]*\).*/\1/p')
ARRS=$(echo "$STAT" | sed -n 's/.*arrskipped=\([0-9]*\).*/\1/p')
grep '^PS|' "$TMP/scan.txt" > "$TMP/ps.txt" || true
N=$(wc -l < "$TMP/now.txt" | tr -d ' ')
# D-71: nothing above this line noticed what the scan had actually read. An
# empty or wrong --root, or a scanner that stopped recognising launch sites,
# printed "STAT|scripts=0|launchers=0|sites=0", matched an empty baseline, and
# exited 0 with GATE-LAUNCH-HYGIENE=PASS: a clean bill of health for a tree it
# never looked at. Measured on 2026-11-10: scripts=123 launchers=54 sites=77
# (6973484, the commit that introduced this file, recorded 121/54/76). The
# floors sit about 20% below those readings -- high enough that a wrong root or
# a broken scanner fails, low enough that deleting a few scripts does not.
SC=$(echo "$STAT" | sed -n 's/.*scripts=\([0-9]*\).*/\1/p')
LN=$(echo "$STAT" | sed -n 's/.*launchers=\([0-9]*\).*/\1/p')
ST=$(echo "$STAT" | sed -n 's/.*sites=\([0-9]*\).*/\1/p')
FLOORFAIL=0
if [ "$FLOORS" -eq 0 ]; then
  echo "   floors: SKIPPED (--ignore-floors: a fixture tree cannot meet a 100-script floor; the shipping run never passes this)"
fi
# D-50b added pslaunch: measured 5 on 2026-11-10, floor 3 for the same reason as
# the others -- a PowerShell pass that finds nothing reads exactly like a tree
# with no launchers.
[ "$FLOORS" -eq 1 ] && for SPEC in "scripts:${SC:-0}:100" "launchers:${LN:-0}:40" "sites:${ST:-0}:60" "pslaunch:${PSL:-0}:3"; do
  WHAT=${SPEC%%:*}; REST=${SPEC#*:}; GOT=${REST%%:*}; FLOOR=${REST#*:}
  if [ "${GOT:-0}" -lt "$FLOOR" ]; then
    echo "   FAIL: the scan read $WHAT=${GOT:-0}, below the floor $FLOOR -- a wrong --root or a scanner that no longer recognises launch sites reads exactly like a clean tree, which is why this floor exists (D-71)"
    FLOORFAIL=1
  fi
done
echo "== launch hygiene (D-22): static scan of $SCAN_ROOT"
echo "   $STAT"
if [ "${ARRS:-0}" -gt 0 ]; then
  echo "   array-element lines skipped as data (D-92): ${ARRS}"
fi
if [ "$K" -gt 0 ]; then
  echo "   lifecycle-only (exempt: --install/--version/--install-status, no --demo):"
  grep '^LIFECYCLE|' "$TMP/scan.txt" | cut -d'|' -f2 | sed 's/^/     /'
fi
if [ "${PSL:-0}" -gt 0 ]; then
  echo "   PowerShell launch sites (D-50b: counted and reported, NOT in the ratchet;"
  echo "   see the header -- a row here is a launcher this scan can see but the"
  echo "   baseline cannot keep honest after a fix):"
  sed 's/^PS|\|/     /; s/|/  /g' "$TMP/ps.txt"
  if [ "${PSU:-0}" -gt 0 ]; then
    echo "   WARN: ${PSU} of ${PSL} pass neither --settings-file nor --ui-settings-file"
    echo "         (each run rewrites the machine's real settings.ini/ui.ini -- D-22)"
  fi
fi
if [ -s "$TMP/warn.txt" ]; then
  echo "   WARN (--audio-disabled/--hidden-window absent; registered, not fatal):"
  sed 's/^/     /' "$TMP/warn.txt"
fi

if [ "$UPDATE" -eq 1 ]; then
  mkdir -p "$(dirname "$BASELINE")"
  if [ -f "$BASELINE" ]; then cp "$BASELINE" "$TMP/old.txt"; else : > "$TMP/old.txt"; fi
  cp "$TMP/now.txt" "$BASELINE"
  ADD=$(comm -23 "$TMP/now.txt" "$TMP/old.txt" | wc -l | tr -d ' ')
  DEL=$(comm -13 "$TMP/now.txt" "$TMP/old.txt" | wc -l | tr -d ' ')
  echo "== baseline rewritten: $BASELINE (+$ADD / -$DEL rows)"
  comm -23 "$TMP/now.txt" "$TMP/old.txt" | sed 's/^/   + /'
  comm -13 "$TMP/now.txt" "$TMP/old.txt" | sed 's/^/   - /'
  echo "GATE-LAUNCH-HYGIENE=PASS violations=$N baseline=$N warnings=$W lifecycle=$K arrskipped=${ARRS:-0} pslaunch=${PSL:-0} psunflagged=${PSU:-0}"
  exit 0
fi

if [ ! -f "$BASELINE" ]; then
  echo "GATE-LAUNCH-HYGIENE=FAIL tool-error: baseline missing: $BASELINE (run --update-baseline after review)"
  exit 90
fi
# The baseline is a committed text file and this host runs core.autocrlf=true,
# so a fresh checkout materialises it with CRLF while the scan below always
# writes LF. Comparing the two byte-wise made every row look both new and stale
# at once on any checkout but the one that authored the file -- a false red that
# says nothing about the tree. CR is stripped, not rewritten: the file on disk
# stays as git delivered it.
tr -d '\r' < "$BASELINE" | sort > "$TMP/base.txt"
FB=0
if [ "$FLOORFAIL" -eq 1 ]; then
  FB=1
fi
NEW=$(comm -23 "$TMP/now.txt" "$TMP/base.txt")
if [ -n "$NEW" ]; then
  FB=1
  while IFS='|' read -r sc flag; do
    case "$flag" in
      shared\ *) echo "FAIL: new unisolated launch: $sc has ${flag#shared } pointing at tracked/oracle evidence (D-18)" ;;
      *)          echo "FAIL: new unisolated launch: $sc missing $flag" ;;
    esac
  done <<< "$NEW"
fi
STALE=$(comm -13 "$TMP/now.txt" "$TMP/base.txt")
if [ -n "$STALE" ]; then
  FB=1
  while IFS='|' read -r sc flag; do
    echo "FAIL: baseline stale: $sc no longer violates $flag (delete this line from baseline)"
  done <<< "$STALE"
fi
M=$(grep -c . "$TMP/base.txt" | tr -d ' ')
if [ "$FB" -eq 0 ]; then
  echo "   violations match baseline: $M rows, all still real"
  echo "GATE-LAUNCH-HYGIENE=PASS violations=$N baseline=$M warnings=$W lifecycle=$K arrskipped=${ARRS:-0} pslaunch=${PSL:-0} psunflagged=${PSU:-0}"
  exit 0
else
  echo "GATE-LAUNCH-HYGIENE=FAIL violations=$N baseline=$M warnings=$W lifecycle=$K arrskipped=${ARRS:-0} pslaunch=${PSL:-0} psunflagged=${PSU:-0}"
  exit 1
fi
