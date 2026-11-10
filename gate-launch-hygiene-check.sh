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
#     (entity-material-check.sh:155 does subprocess.Popen), PowerShell launches
#     (install-readiness-check.sh:770, stability-run-matrix.ps1), build-only references.
#   - Flags forwarded through "$@": statically invisible, so an array that ends in
#     "$@" is judged by the literal flags in its own body.
#   - Whether the run ACTUALLY stays isolated at runtime; this gate is a static
#     ratchet and must be paired with verify-release.sh's machine-isolation step.
#
# USAGE
#   bash gate-launch-hygiene-check.sh                   # check (0 PASS / 1 FAIL / 90 tool error)
#   bash gate-launch-hygiene-check.sh --update-baseline # rewrite baseline, print +/- rows
#   bash gate-launch-hygiene-check.sh --root DIR        # scan another tree (negative controls)
#
# Last line is a machine token:
#   GATE-LAUNCH-HYGIENE=PASS|FAIL violations=N baseline=M warnings=W lifecycle=K
set -uo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SCAN_ROOT="$ROOT"
UPDATE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --root) SCAN_ROOT="$(cd "$2" && pwd)"; shift 2;;
    --update-baseline) UPDATE=1; shift;;
    --help|-h) sed -n '2,40p' "$0"; exit 0;;
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
                text += '\n'+lines[next_i][1]; next_i+=1
                k=close_paren(text,0)
            body=text[:k] if k>=0 else text
            arrays[name].append((ln,app,body))
            i=next_i; continue
        i+=1
    return pvars, arrays, assigns

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

def scan_script(path):
    raw=open(path,encoding='utf-8',errors='replace').read().split('\n')
    logs=logical(raw); pvars,arrays,assigns=collect(logs)
    sites=[]; wrapped=[]
    if not pvars: return sites,wrapped
    for ln,t in logs:
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
    return sites,wrapped

def main(root):
    scripts=sorted(f for f in os.listdir(root) if f.endswith('.sh') and os.path.isfile(os.path.join(root,f)))
    violations=[]; warnings=[]; lifecycle=[]; nlaunch=0; nsites=0
    for sc in scripts:
        sites,wrapped=scan_script(os.path.join(root,sc))
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
    print('STAT|scripts=%d|launchers=%d|sites=%d|violations=%d|warnings=%d|lifecycle=%d'%(
        len(scripts),nlaunch,nsites,len(set(violations)),len(set(warnings)),len(set(lifecycle))))

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
for SPEC in "scripts:${SC:-0}:100" "launchers:${LN:-0}:40" "sites:${ST:-0}:60"; do
  WHAT=${SPEC%%:*}; REST=${SPEC#*:}; GOT=${REST%%:*}; FLOOR=${REST#*:}
  if [ "${GOT:-0}" -lt "$FLOOR" ]; then
    echo "   FAIL: the scan read $WHAT=${GOT:-0}, below the floor $FLOOR -- a wrong --root or a scanner that no longer recognises launch sites reads exactly like a clean tree, which is why this floor exists (D-71)"
    FLOORFAIL=1
  fi
done
echo "== launch hygiene (D-22): static scan of $SCAN_ROOT"
echo "   $STAT"
if [ "$K" -gt 0 ]; then
  echo "   lifecycle-only (exempt: --install/--version/--install-status, no --demo):"
  grep '^LIFECYCLE|' "$TMP/scan.txt" | cut -d'|' -f2 | sed 's/^/     /'
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
  echo "GATE-LAUNCH-HYGIENE=PASS violations=$N baseline=$N warnings=$W lifecycle=$K"
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
  echo "GATE-LAUNCH-HYGIENE=PASS violations=$N baseline=$M warnings=$W lifecycle=$K"
  exit 0
else
  echo "GATE-LAUNCH-HYGIENE=FAIL violations=$N baseline=$M warnings=$W lifecycle=$K"
  exit 1
fi
