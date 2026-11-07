#!/usr/bin/env bash
# debt-claim-check.sh -- T85 task-20: make "this debt can be judged red"
# machine-checkable instead of a human sentence in a table cell.
#
# WHAT THIS GATE IS
# -----------------
# docs/T85-DEBT-LIST.md has two tables: 39 clauses (8.1-1 .. 8.7-4) and 10
# cross-cutting debts (D-13 .. D-22). Each row carries a claim about whether a
# gate can actually fail on it. That claim used to be prose only -- exactly the
# failure mode the handoff warns about ("documentation says supported, no gate
# goes red"). This gate reads the LAST cell of every row and requires a
# machine-readable claim in one of three forms:
#
#   CAN-RED gate:<script.sh>#<literal-token>   the script exists AND contains
#                                              the literal token (grep -F)
#   CAN-RED-UNANCHORED <reason>                admitted: no gate yet
#   CANNOT-RED <reason>                        admitted: not a gate subject
#
# A CAN-RED without a resolvable anchor is a failure ("claim without gate").
# An empty last cell, an unknown marker, or a CANNOT-RED with a blank reason
# are failures too. If nothing at all is anchored, the gate fails: an all-
# UNANCHORED ledger must not look green.
#
# The token is a literal substring, so a gate's verdict token (e.g.
# "MAP-GEOMETRY-LIGHTING=PASS") or a named assertion line both qualify.
#
# Usage:
#   debt-claim-check.sh [--debt FILE] [--root DIR] [--quiet]
#
# Exit: 0 PASS / 1 FAIL / 90 tool error (missing debt file, python absent).
# Last line: DEBT-CLAIM=PASS|FAIL claims= anchored= cannot= unanchored=

set -uo pipefail

HERE=$(cd -- "$(dirname -- "$0")" && pwd -P)
DEBT="$HERE/docs/T85-DEBT-LIST.md"
ROOT="$HERE"
QUIET=0

while [ $# -gt 0 ]; do
  case "$1" in
    --debt)  DEBT=${2:?--debt needs a path}; shift 2 ;;
    --root)  ROOT=${2:?--root needs a path}; shift 2 ;;
    --quiet) QUIET=1; shift ;;
    -h|--help) sed -n '2,40p' "$0" | sed 's/^# {0,1}//'; exit 0 ;;
    *) echo "debt-claim-check: unknown arg: $1" >&2; exit 90 ;;
  esac
done

[ -f "$DEBT" ] || { echo "debt-claim-check: no debt file: $DEBT" >&2; echo "DEBT-CLAIM=FAIL claims=0 anchored=0 cannot=0 unanchored=0"; exit 90; }
[ -d "$ROOT" ] || { echo "debt-claim-check: no root dir: $ROOT" >&2; echo "DEBT-CLAIM=FAIL claims=0 anchored=0 cannot=0 unanchored=0"; exit 90; }

PY=${PYTHON:-python}
command -v "$PY" >/dev/null 2>&1 || PY=python3
command -v "$PY" >/dev/null 2>&1 || { echo "debt-claim-check: no python" >&2; echo "DEBT-CLAIM=FAIL claims=0 anchored=0 cannot=0 unanchored=0"; exit 90; }

DEBT="$DEBT" ROOT="$ROOT" QUIET="$QUIET" "$PY" - <<'PYCODE'
import os, re, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

debt = os.environ["DEBT"]
root = os.environ["ROOT"]
quiet = os.environ.get("QUIET") == "1"

ID_RE = re.compile(r"^\|\s*(8\.[1-7]-\d+|D-\d+)\s*\|")

rows = []
try:
    with open(debt, encoding="utf-8") as fh:
        for line in fh:
            m = ID_RE.match(line)
            if not m:
                continue
            cells = [c.strip() for c in re.split(r"(?<!\\)\|", line.rstrip("\n"))]
            if cells and cells[0] == "":
                cells = cells[1:]
            if cells and cells[-1] == "":
                cells = cells[:-1]
            claim = cells[-1] if cells else ""
            rows.append((m.group(1), claim))
except OSError as exc:
    print("debt-claim-check: cannot read %s: %s" % (debt, exc), file=sys.stderr)
    print("DEBT-CLAIM=FAIL claims=0 anchored=0 cannot=0 unanchored=0")
    sys.exit(90)

if not rows:
    print("FAIL: no claim rows found in %s (expected ids 8.x-n or D-nn)" % debt)
    print("DEBT-CLAIM=FAIL claims=0 anchored=0 cannot=0 unanchored=0")
    sys.exit(1)

failures = []
seen = set()
anchored = cannot = unanchored = 0
unanchored_ids = []

for rid, claim in rows:
    if rid in seen:
        failures.append("FAIL: duplicate claim id: %s" % rid)
    seen.add(rid)

    if not claim:
        failures.append("FAIL: %s no machine claim marker (last cell empty)" % rid)
        print("CLAIM %s FAIL empty" % rid)
        continue

    m = re.match(r"^CAN-RED\s+gate:(\S+)#(.+)$", claim)
    if claim.startswith("CAN-RED-UNANCHORED"):
        reason = claim[len("CAN-RED-UNANCHORED"):].strip()
        if len(reason) < 4:
            failures.append("FAIL: %s CAN-RED-UNANCHORED without reason" % rid)
            print("CLAIM %s FAIL unanchored-without-reason" % rid)
            continue
        unanchored += 1
        unanchored_ids.append(rid)
        print("CLAIM %s PASS unanchored %s" % (rid, reason))
        continue
    if claim.startswith("CANNOT-RED"):
        reason = claim[len("CANNOT-RED"):].strip()
        if len(reason) < 4:
            failures.append("FAIL: %s CANNOT-RED without reason" % rid)
            print("CLAIM %s FAIL cannot-without-reason" % rid)
            continue
        cannot += 1
        print("CLAIM %s PASS cannot %s" % (rid, reason))
        continue
    if claim.startswith("CAN-RED"):
        if not m:
            failures.append("FAIL: %s claim without gate: %r (expected 'CAN-RED gate:<script>#<token>')" % (rid, claim))
            print("CLAIM %s FAIL unanchored" % rid)
            continue
        script, token = m.group(1), m.group(2).strip()
        path = script if os.path.isabs(script) else os.path.join(root, script)
        if not os.path.isfile(path):
            failures.append("FAIL: %s anchor script missing: %s" % (rid, script))
            print("CLAIM %s FAIL missing-script %s" % (rid, script))
            continue
        try:
            with open(path, "rb") as fh:
                blob = fh.read()
        except OSError as exc:
            failures.append("FAIL: %s anchor script unreadable: %s (%s)" % (rid, script, exc))
            print("CLAIM %s FAIL unreadable-script %s" % (rid, script))
            continue
        if token.encode("utf-8") not in blob:
            failures.append("FAIL: %s anchor token not found: %s#%s" % (rid, script, token))
            print("CLAIM %s FAIL missing-token %s#%s" % (rid, script, token))
            continue
        anchored += 1
        print("CLAIM %s PASS anchored %s#%s" % (rid, script, token))
        continue

    failures.append("FAIL: %s unrecognized claim marker: %r" % (rid, claim))
    print("CLAIM %s FAIL unrecognized %s" % (rid, claim))

if anchored == 0:
    failures.append("FAIL: no anchored claims at all -- every CAN-RED would be prose")

if unanchored_ids and not quiet:
    print("UNANCHORED: %s" % " ".join(unanchored_ids))

status = "FAIL" if failures else "PASS"
for line in failures:
    print(line)
print("DEBT-CLAIM=%s claims=%d anchored=%d cannot=%d unanchored=%d"
      % (status, len(rows), anchored, cannot, unanchored))
sys.exit(0 if status == "PASS" else 1)
PYCODE