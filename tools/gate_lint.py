#!/usr/bin/env python3
"""tools/gate_lint.py -- static check that no gate reintroduces the three defects
found across the suite on 2026-11-22 (docs/GATE-FIXES-2026-11-22.md).

  L1 cp-p-restore   `cp -p <backup> <source>` with no `touch` within the next
                    15 lines. Restores the old mtime; NMake keeps the mutant .obj.
  L2 const-mutant   a mutation text containing `(false &&`.
                    C4127 -> C2220 under /W4 /WX; the case never builds.
  L3 procsub-shim   diff/comm/paste/cmp/join given a `<(...)` argument. Those
                    resolve to the WinuxCmd shim here, which reads /dev/fd/N as
                    0 bytes (the step-7 "0a1,18" false red).

Why Python (2026-11-23): the bash version read each gate into an array and
pattern-matched it line by line. On this host that run printed no verdict, exited
0 and wrote ~116 lines of "unbound variable" / "command not found" to stderr --
gate TEXT was being evaluated as shell. A user's minimal mapfile + case script
reproduced it, so the failure is in the host's shell layer, not in one regex.
Here the gate files are only ever data: read as bytes, decoded, matched. Nothing
from a linted file can reach an interpreter.

The rules, the scope and the allow-list format are unchanged from the bash
version, so tools/gate-lint-allow.txt carries over as it is.

Usage: python tools/gate_lint.py            # every root *.sh and tools/*.sh
       python tools/gate_lint.py FILE...    # the named files
       python tools/gate_lint.py --selftest # the rules as the object under test
Exceptions: tools/gate-lint-allow.txt, one `RULE FILE:LINE  reason` per line.
  An entry with no reason is ignored (an exception must say why).
  An entry for a linted file that matches nothing is STALE-ALLOW and fails the run.
Exit: 0 clean, 1 findings, 2 usage/input error.
Token (exactly one line): GATE-LINT=PASS|FAIL findings=N files=N allowed=N stale_allow=N
"""
from __future__ import annotations

import os
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ALLOW_FILE = os.path.join("tools", "gate-lint-allow.txt")
SKIP_NAMES = {"gate-lint.sh", "gate-lib.sh"}
TOUCH_WINDOW = 15

# Same patterns as the bash version. Python's search() and ERE's [[ =~ ]] agree on
# whether a line matches; only which of several matches is reported could differ,
# and the destination group is anchored on the first `cp -p` either way.
RE_CPP = re.compile(r"(^|[\s;&|])cp\s+-p\s+(\S+)\s+([^\s;&|]+)")
RE_SUB = re.compile(r"(^|[^A-Za-z0-9_-])(diff|comm|paste|cmp|join)(\s[^#]*)?<[(]")
RE_CONST = re.compile(r"\(false &&")
# A cp -p destination that is a backup or scratch location is a backup, not a restore.
SAFE_DST = ("bak", "BAK", "BK", "backup", "BACKUP", ".orig", "RESTORE_DIR",
            "tmp", "TMP", "OUT", "evidence", ".scratch")


def read_lines(path: str) -> list[str]:
    with open(path, "rb") as handle:
        data = handle.read()
    # Gates are UTF-8 (some carry Chinese paths); a stray byte must not stop the lint.
    return split_text(data.decode("utf-8", errors="replace"))


def split_text(text: str) -> list[str]:
    # A CRLF checkout (core.autocrlf=true) must lint exactly like an LF one.
    return [line.rstrip("\r") for line in text.split("\n")]


def lint_lines(name: str, lines: list[str]) -> list[tuple[str, str, int, str]]:
    """Every finding as (rule, file, 1-based line, stripped text). Pure function."""
    found: list[tuple[str, str, int, str]] = []
    for index, raw in enumerate(lines):
        text = raw.lstrip()
        # Comment lines are skipped: the fixes themselves describe the bad shapes.
        if not text or text.startswith("#"):
            continue
        match = RE_CPP.search(text)
        if match:
            dst = match.group(3)
            if not any(token in dst for token in SAFE_DST):
                window = lines[index + 1:index + 1 + TOUCH_WINDOW]
                if not any("touch" in later for later in window):
                    found.append(("cp-p-restore", name, index + 1, text))
        if RE_CONST.search(text):
            found.append(("const-mutant", name, index + 1, text))
        if RE_SUB.search(text):
            found.append(("procsub-shim", name, index + 1, text))
    return found


def parse_allow(lines: list[str]) -> tuple[dict[str, str], list[str]]:
    """{"RULE FILE:LINE": reason}, plus the entries ignored for lacking a reason."""
    allowed: dict[str, str] = {}
    ignored: list[str] = []
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 2)
        if len(parts) < 3 or not parts[2].strip():
            ignored.append(line)
            continue
        allowed[parts[0] + " " + parts[1]] = parts[2].strip()
    return allowed, ignored


def evaluate(files: dict[str, list[str]], allow_lines: list[str]) -> tuple[list[str], int]:
    """Lint already-read files against an allow list. Returns (output lines, exit code)."""
    out: list[str] = []
    allowed, ignored = parse_allow(allow_lines)
    for entry in ignored:
        out.append("  NOTE allow entry without a reason ignored: " + entry)
    used: set[str] = set()
    findings = 0
    allowed_hits = 0
    for name in files:
        for rule, fname, line, text in lint_lines(name, files[name]):
            key = f"{rule} {fname}:{line}"
            if key in allowed:
                used.add(key)
                allowed_hits += 1
                continue
            findings += 1
            out.append(f"  FAIL {rule:<13} {fname}:{line}  {text}")
    stale = 0
    for key in sorted(allowed):
        if key in used:
            continue
        fname = key.split(" ", 1)[1].rsplit(":", 1)[0]
        # Only stale if its file was linted this run.
        if fname in files:
            stale += 1
            out.append(f"  STALE-ALLOW {key}  ({allowed[key]})")
    verdict = "PASS" if findings == 0 and stale == 0 else "FAIL"
    out.append(f"GATE-LINT={verdict} findings={findings} files={len(files)} "
               f"allowed={allowed_hits} stale_allow={stale}")
    return out, 0 if verdict == "PASS" else 1


def default_files() -> list[str]:
    names: list[str] = []
    for folder in (".", "tools"):
        full = os.path.join(REPO, folder)
        for entry in sorted(os.listdir(full)):
            if not entry.endswith(".sh") or entry in SKIP_NAMES:
                continue
            if os.path.isfile(os.path.join(full, entry)):
                names.append(entry if folder == "." else folder + "/" + entry)
    return names


def run(argv: list[str]) -> int:
    if argv and argv[0] == "--selftest":
        return selftest()
    names = [arg.replace("\\", "/") for arg in argv] if argv else default_files()
    if names and names[0].startswith("./"):
        names = [name[2:] if name.startswith("./") else name for name in names]
    if not names:
        print("GATE-LINT=FAIL (no input files)")
        return 2
    files: dict[str, list[str]] = {}
    for name in names:
        path = os.path.join(REPO, name)
        if not os.path.isfile(path):
            print(f"GATE-LINT=FAIL (missing {name})")
            return 2
        files[name] = read_lines(path)
    allow_path = os.path.join(REPO, ALLOW_FILE)
    allow_lines = read_lines(allow_path) if os.path.isfile(allow_path) else []
    out, code = evaluate(files, allow_lines)
    print("\n".join(out))
    return code


def selftest() -> int:
    """Each rule must fire on its shape and stay quiet on the near-misses that the
    suite actually contains. Counts are exact: a rule that went quiet, or one that
    started firing on comments, both turn this red."""
    checks = 0
    failures = 0

    def expect(label: str, got, want) -> None:
        nonlocal checks, failures
        checks += 1
        if got == want:
            print(f"  OK   {label} = {want!r}")
        else:
            failures += 1
            print(f"  FAIL {label} = {got!r}, expected {want!r}")

    def rules(text: str) -> list[str]:
        return [f[0] for f in lint_lines("t.sh", text.split("\n"))]

    # L1
    expect("L1 restore with no touch", rules('cp -p "$BK/x.cpp" native/src/x.cpp'), ["cp-p-restore"])
    expect("L1 restore touched 15 lines later",
           rules('cp -p "$B" native/src/x.cpp\n' + "\n" * 14 + 'touch native/src/x.cpp'), [])
    expect("L1 touch 16 lines later is too late",
           rules('cp -p "$B" native/src/x.cpp\n' + "\n" * 15 + 'touch native/src/x.cpp'), ["cp-p-restore"])
    expect("L1 backup destination", rules('cp -p native/src/x.cpp "$BK/x.cpp"'), [])
    expect("L1 scratch destination", rules('cp -p a .scratch/a'), [])
    expect("L1 plain cp", rules('cp "$BK/x.cpp" native/src/x.cpp'), [])
    expect("L1 after ;", rules('true; cp -p "$B" src/a.cpp'), ["cp-p-restore"])
    expect("L1 inside a word is not cp", rules('scp -p host:a src/a.cpp'), [])
    expect("L1 commented", rules('  # cp -p "$B" native/src/x.cpp'), [])
    # L2
    expect("L2 constant false", rules("'  if (false && reference.hasWorldModelIndex) {'"), ["const-mutant"])
    expect("L2 the fixed shape", rules("'  if (!(x) && (x)) {'"), [])
    expect("L2 commented", rules("# not `if (false && x)`"), [])
    # L3
    expect("L3 diff of two substitutions", rules('diff <(sort a) <(sort b)'), ["procsub-shim"])
    expect("L3 cmp with options", rules('cmp -s <(head -c 10 a) b'), ["procsub-shim"])
    expect("L3 comm after a pipe", rules('x | comm -12 <(sort a) b'), ["procsub-shim"])
    expect("L3 plain files", rules('diff a.txt b.txt'), [])
    expect("L3 substitution into a bash builtin", rules('while read l; do :; done < <(ls)'), [])
    expect("L3 a longer word ending in diff", rules('mydiff <(x)'), [])
    expect("L3 hyphenated word", rules('git-diff <(x)'), [])
    expect("L3 commented", rules('# diff <(a) <(b)'), [])
    expect("L3 # before the substitution", rules('diff a b # <(x)'), [])
    # All three on one line are three findings, not one.
    expect("three rules on one line",
           sorted(rules("cp -p $B src/a.cpp; x='(false && y)'; diff <(a) b")),
           ["const-mutant", "cp-p-restore", "procsub-shim"])
    # Line endings and bytes.
    crlf = lint_lines("t.sh", split_text("echo\r\ndiff <(a) b\r\n"))
    expect("CRLF file: line number", [f[2] for f in crlf], [2])
    expect("CRLF file: no trailing CR in the text", [f[3] for f in crlf], ["diff <(a) b"])
    expect("undecodable byte does not stop the lint",
           [f[0] for f in lint_lines("t.sh", split_text(b"echo \xff\ndiff <(a) b".decode("utf-8", errors="replace")))],
           ["procsub-shim"])
    # Allow list.
    files = {"a.sh": ['diff <(x) y', 'cmp <(x) y'], "b.sh": ["echo ok"]}
    out, code = evaluate(files, ["procsub-shim a.sh:1  fixture reason", "procsub-shim a.sh:9  gone",
                                 "procsub-shim c.sh:1  file not linted", "procsub-shim a.sh:2"])
    expect("allow: exit code", code, 1)
    expect("allow: token", out[-1], "GATE-LINT=FAIL findings=1 files=2 allowed=1 stale_allow=1")
    expect("allow: unexcused line still fails", sum(1 for l in out if l.split()[:3] == ["FAIL", "procsub-shim", "a.sh:2"]), 1)
    expect("allow: stale entry reported", sum(1 for l in out if "STALE-ALLOW procsub-shim a.sh:9" in l), 1)
    expect("allow: entry without reason ignored", sum(1 for l in out if l.startswith("  NOTE")), 1)
    expect("allow: entry for an unlinted file is not stale", any("c.sh" in l for l in out), False)
    out, code = evaluate({"b.sh": ["echo ok"]}, [])
    expect("clean: exit code", code, 0)
    expect("clean: token", out[-1], "GATE-LINT=PASS findings=0 files=1 allowed=0 stale_allow=0")
    expect("exactly one token line", sum(1 for l in out if l.startswith("GATE-LINT=")), 1)

    verdict = "PASS" if failures == 0 else "FAIL"
    print(f"GATE-LINT-SELFTEST={verdict} checks={checks} failed={failures}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
