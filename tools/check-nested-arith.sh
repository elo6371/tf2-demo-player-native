#!/usr/bin/env bash
# check-nested-arith.sh -- refuse arithmetic expansions that nest a quoted command substitution.
#
# WHY (measured on this host, bash 5.3.20(1)-release (x86_64-pc-msys)):
#   $(( $(f "x") - 1 ))       -> the LITERAL text, silently, no error
#   $(( $(f x) - 1 ))         -> 6, correct: no quoted word inside the substitution
#   v=$(f "x"); $(( v - 1 ))  -> correct: this two-step shape is the fix
# so a check that reads a count can compare a string against an integer and print a
# verdict nobody can explain. The (( ... )) COMMAND form and the let builtin are
# immune; only the $(( )) EXPANSION is affected, which is why only it is scanned.
#
# Usage:
#   bash tools/check-nested-arith.sh              scan the repo-root *.sh files
#   bash tools/check-nested-arith.sh FILE...      scan the given files
#   bash tools/check-nested-arith.sh --selftest   prove the checker catches a bad site
#
# Exit: 0 clean, 1 a site was found (or a selftest control failed), 2 usage/environment.
# Limitation: here-document bodies are skipped, so a site written inside one is unseen.
set -u
SELF=${BASH_SOURCE[0]}
ROOT=$(cd "$(dirname "$SELF")/.." && pwd) || exit 2
PY=${PYTHON:-python}
command -v "$PY" >/dev/null 2>&1 || PY=python3
command -v "$PY" >/dev/null 2>&1 || { echo "CHECK-NESTED-ARITH: no python (set PYTHON)" >&2; exit 2; }

scan() {
  "$PY" - "$@" <<'PYEOF'
import sys


def scan(text):
    n = len(text)
    i = 0
    stack = [{'kind': 'top', 'pos': 0, 'quote': False}]
    stats = {'arith': 0, 'nested': 0}
    viol = []
    at_word_start = True

    def excerpt(pos):
        s = text.rfind('\n', 0, pos) + 1
        e = text.find('\n', pos)
        if e < 0:
            e = n
        return ' '.join(text[s:e].split())

    def mark_enclosing_arith():
        for f in reversed(stack):
            if f['kind'] == 'arith':
                f['quote'] = True
                return

    while i < n:
        top = stack[-1]
        kind = top['kind']
        c = text[i]
        if kind == 'arith':
            if c == '(' and text.startswith('(', i + 1):
                i += 2
                continue
            if c == ')' and text.startswith(')', i + 1):
                frame = stack.pop()
                if frame['quote']:
                    viol.append((frame['pos'], excerpt(frame['pos'])))
                    mark_enclosing_arith()
                i += 2
                continue
            if c == '$' and text.startswith('((', i + 1):
                stack.append({'kind': 'arith', 'pos': i, 'quote': False})
                stats['arith'] += 1
                i += 3
                continue
            if c == '$' and text.startswith('(', i + 1):
                stack.append({'kind': 'cmdsub', 'pos': i, 'quote': False})
                stats['nested'] += 1
                i += 2
                continue
            i += 1
            continue
        if kind == 'double':
            if c == '\\':
                i += 2
                continue
            if c == '"':
                stack.pop()
                i += 1
                continue
            if c == '$' and text.startswith('((', i + 1):
                stack.append({'kind': 'arith', 'pos': i, 'quote': False})
                stats['arith'] += 1
                i += 3
                continue
            if c == '$' and text.startswith('(', i + 1):
                stack.append({'kind': 'cmdsub', 'pos': i, 'quote': False})
                i += 2
                continue
            i += 1
            continue
        if c == '\\':
            i += 2
            at_word_start = False
            continue
        if c == '#' and at_word_start:
            j = text.find('\n', i)
            i = n if j < 0 else j
            at_word_start = True
            continue
        if c == '<' and text.startswith('<<<', i):
            # A here-string such as '<<<"$CAM"' has no body; its target must not be
            # lexed as a here-document delimiter (that is how files went blind).
            i += 3
            at_word_start = True
            continue
        if c == '<' and text.startswith('<', i + 1):
            j = i + 2
            dash = False
            if j < n and text[j] == '-':
                dash = True
                j += 1
            while j < n and text[j] in ' \t':
                j += 1
            if j < n and text[j] in '\'"':
                q = text[j]
                k = text.find(q, j + 1)
                if k < 0:
                    break
                delim = text[j + 1:k]
                j = k + 1
            else:
                k = j
                while k < n and (text[k].isalnum() or text[k] == '_'):
                    k += 1
                delim = text[j:k]
                j = k
            if not delim:
                i += 1
                continue
            nl = text.find('\n', j)
            if nl < 0:
                break
            k = nl + 1
            found = -1
            while k <= n:
                e = text.find('\n', k)
                if e < 0:
                    e = n
                cand = text[k:e]
                if (cand.strip() if dash else cand) == delim:
                    found = e
                    break
                if e >= n:
                    break
                k = e + 1
            i = (nl + 1) if found < 0 else found + 1
            at_word_start = True
            continue
        if c == "'":
            if kind == 'cmdsub':
                top['quote'] = True
            j = text.find("'", i + 1)
            i = n if j < 0 else j + 1
            at_word_start = False
            continue
        if c == '"':
            if kind == 'cmdsub':
                top['quote'] = True
            stack.append({'kind': 'double', 'pos': i, 'quote': False})
            i += 1
            at_word_start = False
            continue
        if c == '$' and text.startswith('((', i + 1):
            stack.append({'kind': 'arith', 'pos': i, 'quote': False})
            stats['arith'] += 1
            i += 3
            at_word_start = True
            continue
        if c == '$' and text.startswith('(', i + 1):
            stack.append({'kind': 'cmdsub', 'pos': i, 'quote': False})
            i += 2
            at_word_start = True
            continue
        if c == ')' and kind == 'cmdsub':
            frame = stack.pop()
            if frame['quote']:
                mark_enclosing_arith()
            i += 1
            at_word_start = False
            continue
        at_word_start = c in ' \t\n;&|('
        i += 1
    return stats, viol


def main():
    files = 0
    arith = 0
    nested = 0
    sites = 0
    for path in sys.argv[1:]:
        try:
            with open(path, 'r', encoding='utf-8', errors='replace') as fh:
                text = fh.read()
        except OSError as exc:
            print('FAIL %s: cannot read (%s)' % (path, exc))
            sites += 1
            continue
        files += 1
        stats, viol = scan(text)
        arith += stats['arith']
        nested += stats['nested']
        for pos, ex in viol:
            print('FAIL %s:%d  %s' % (path, text.count(chr(10), 0, pos) + 1, ex[:140]))
            sites += 1
    print('CHECK-NESTED-ARITH: files=%d arithmetic=%d nested_cmdsub=%d sites=%d' % (files, arith, nested, sites))
    if sites:
        print('VERDICT: FAIL (split each site: read the value into a variable, then do the arithmetic)')
        return 1
    print('VERDICT: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
PYEOF
}

# The two-step fix and the buggy shape, kept out of the checker's own scan path:
# both are single-quoted here, so this file has no live $(( )) to flag.
BAD_SITE='n=$(( $(f "x") - 1 )); printf "%s\n" "$n"'
FIXED_SITE='v=$(f "x"); n=$(( v - 1 )); printf "%s\n" "$n"'

selftest() {
  tmp=$(mktemp -d) || { echo "SELFTEST=FAIL reason=cannot mktemp" >&2; exit 2; }
  trap 'rm -rf "$tmp"' EXIT
  printf 'f() { printf %%s 7; }\n%s\n' "$BAD_SITE" > "$tmp/bad.sh"
  printf 'f() { printf %%s 7; }\n%s\n' "$FIXED_SITE" > "$tmp/good.sh"
  bad_out=$(scan "$tmp/bad.sh"); bad_rc=$?
  good_out=$(scan "$tmp/good.sh"); good_rc=$?
  observed=$(bash "$tmp/bad.sh" 2>&1)
  case $observed in
    *'$(('*) host=reproduces ;;
    *) host=does-not-reproduce ;;
  esac
  fail=0
  case $bad_out in
    *"$tmp/bad.sh:2"*) echo "SELFTEST: negative control flagged at bad.sh:2 (rc=$bad_rc)" ;;
    *) echo "SELFTEST: FAIL the checker did not flag the known-bad site (rc=$bad_rc)"; fail=1 ;;
  esac
  if [ "$bad_rc" -eq 0 ]; then echo "SELFTEST: FAIL the checker exited 0 on a known-bad site"; fail=1; fi
  if [ "$good_rc" -ne 0 ]; then echo "SELFTEST: FAIL the checker flagged the two-step fix"; fail=1; else echo "SELFTEST: positive control clean (rc=$good_rc)"; fi
  echo "SELFTEST: host bash $host the bug (bad.sh printed: $observed)"
  if [ "$fail" -ne 0 ]; then echo "SELFTEST=FAIL"; exit 1; fi
  echo "SELFTEST=PASS"
  exit 0
}

case ${1:-} in
  --selftest) shift; selftest ;;
  -h|--help) sed -n '2,20p' "$SELF"; exit 0 ;;
esac

if [ "$#" -eq 0 ]; then
  cd "$ROOT" || exit 2
  set -- ./*.sh
fi
scan "$@"

