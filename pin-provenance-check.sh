#!/usr/bin/env bash
# pin-provenance-check.sh -- T62/T85: the class-pixels pins carry their own
# provenance, and that provenance is checked against the files, not assumed.
#
# WHY THIS GATE EXISTS
#   evidence/t62-matrix/class-pixels/<class>/ holds the pinned BMPs that
#   class-pixels-check.sh compares byte-for-byte (and, for demo/, that
#   third-person-weapon-check.sh compares too). A pin nobody can attribute is
#   just a file with a date on it: "the product still produces these bytes" is
#   a claim about a *binary*, and until now the pin recorded that binary only
#   as a free-text "frame: product sha256=" line in a capture log that no
#   assertion reads. This gate turns the provenance into data (provenance.json,
#   one per class) and asserts that data against the pinned files.
#
# WHAT IT ASSERTS (21 assertions per class: demo scout soldier medic)
#   A1  provenance.json exists where the pin lives
#   A2  it parses as a JSON object
#   A3  the four ledger keys are present: exe_sha256 / git_head /
#       native_tree_sha256 / request
#   A4  its "class" names the directory it sits in
#   A5  exe_sha256 is 64 lowercase hex
#   A6  git_head is 40 lowercase hex
#   A7  native_tree_sha256 is 64 lowercase hex
#   A8  request carries demo / tick / speed / camera / flags
#   A9  request.demo is a non-empty path ending in .dem
#   A10 request.tick is an integer > 0
#   A11 request.speed is an integer in 1..64
#   A12 request.camera is 5 finite numbers (x,y,z,pitch,yaw), pitch in -90..90,
#       yaw in -360..360 -- the same order class-pixels-check.sh writes it in
#   A13 expected_bmp_sha256 names animated / identity / identity-2 / off and
#       each value is 64 lowercase hex
#   A14 every pinned BMP hashes (sha256 of the file) to its recorded value
#   A15 identity.bmp and identity-2.bmp are byte-identical, as the capture
#       contract ("the same identity capture twice") says
#   A16 identity carries ent > 0, model ending in .mdl, and
#       capture_twice_byte_identical == true
#   A17 all four pinned BMPs are non-empty and the same size (one frame size,
#       one viewport: a pin that mixes sizes is not one reading)
#   A18 the capture receipt (entity-id.txt) records the same four BMP sha256
#       values and the same product sha256 the ledger claims. The expected
#       bytes therefore come from a receipt written at capture time, not from
#       the BMPs this gate is asserting (D-36)
#   A19 the receipt's request/BODY and the four per-tag capture summaries
#       corroborate the ledger (demo/tick/speed/camera/flags, product sha256,
#       bmp sha256). Receipt, summaries and ledger are separate artifacts, so
#       editing one of them is not enough to keep the pin green (D-36)
#   A20 the recorded build identity is reproducible: git_head is a commit in
#       this repo, and hashing "git ls-tree -r <git_head> -- native" reproduces
#       native_tree_sha256. The build record is the repository itself, not the
#       capture log that names the exe (D-36)
#
# ASSERTION SET AND COUNT ARE PINNED (D-35)
#   EXPECTED_IDS below names every assertion per class. A deleted check() call
#   can no longer leave the gate green: the emitted ID set is compared with the
#   pinned set at the end, and the total must equal 4 x 21 = 84. A change in
#   that number is only legitimate when this list and the header above change
#   in the same commit, i.e. when a named assertion is genuinely added or
#   removed.
#
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   * That exe_sha256 equals the exe in this tree right now. A pin is a
#     historical capture; after a relink the tree's binary changes and the pin
#     does not. exe_sha256 says "this binary produced these bytes at read_at",
#     which is what a reader needs; the capture gates themselves are what tie
#     the pin to live rendering (they compare BMPs, not this sha).
#   * That the product reproduces the bytes. That needs a launch; this gate is
#     static, reads files only, starts no process, and runs in well under a
#     second.
#
# USAGE
#   bash pin-provenance-check.sh [--pin DIR] [--quiet]
#     --pin DIR   pin root to check (default: evidence/t62-matrix/class-pixels).
#                 Exists for the negative control: point it at a tampered copy.
#     --quiet     only the summary lines and the final token.
#
# EXIT
#   0 PASS / 1 FAIL / 2 tool error (no python, no pin dir, bad usage).
#   Final line: PIN-PROVENANCE=PASS|FAIL (N assertions)

set -uo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
PIN="$ROOT/evidence/t62-matrix/class-pixels"
QUIET=0

while [ $# -gt 0 ]; do
  case "$1" in
    --pin)
      shift
      if [ $# -eq 0 ]; then echo "pin-provenance-check: --pin needs a directory" >&2; exit 2; fi
      PIN=$1 ;;
    --quiet) QUIET=1 ;;
    -h|--help)
      sed -n '2,66p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *)
      echo "pin-provenance-check: unknown argument: $1" >&2
      exit 2 ;;
  esac
  shift
done

if [ ! -d "$PIN" ]; then
  echo "pin-provenance-check: no pin directory: $PIN" >&2
  echo "PIN-PROVENANCE=FAIL (0 assertions)"
  exit 2
fi

PY=$(printenv PYTHON) || true
[ -n "$PY" ] || PY=python
command -v "$PY" >/dev/null 2>&1 || PY=python3
command -v "$PY" >/dev/null 2>&1 || PY=py
command -v "$PY" >/dev/null 2>&1 || {
  echo "pin-provenance-check: no working python interpreter" >&2
  echo "PIN-PROVENANCE=FAIL (0 assertions)"
  exit 2
}

"$PY" - "$PIN" "$QUIET" "$ROOT" <<'PYCODE'
import hashlib
import json
import math
import os
import re
import subprocess
import sys

pin = sys.argv[1]
quiet = sys.argv[2] == "1"
root = sys.argv[3]
CLASSES = ["demo", "scout", "soldier", "medic"]
TAGS = ["animated", "identity", "identity-2", "off"]
HEXDIGITS = set("0123456789abcdef")

# D-35: the SET of assertions is pinned. Every check() call must use one of
# these IDs for its class; a call that disappears leaves a hole the set
# comparison below names, so deleting a test cannot keep the PASS token.
EXPECTED_IDS = ["A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9",
                "A10", "A11", "A12", "A12b", "A13", "A14", "A15", "A16",
                "A17", "A18", "A19", "A20"]

total = 0
failed = 0
failures = []
emitted = []


def check(ok, label, msg):
    """Count one assertion; record its ID so the SET can be pinned (D-35)."""
    global total, failed
    parts = label.split()
    emitted.append((parts[0], parts[1]))
    total += 1
    if not ok:
        failed += 1
        failures.append("%s: %s" % (label, msg))
    return ok


def is_hex(value, width):
    return isinstance(value, str) and len(value) == width and all(c in HEXDIGITS for c in value)


def is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def is_number(value):
    return (isinstance(value, int) or isinstance(value, float)) and not isinstance(value, bool) \
        and math.isfinite(float(value))


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_lines(path):
    """Return the file's lines, or None when it cannot be read."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read().splitlines()
    except OSError:
        return None


for cls in CLASSES:
    start_total = total
    before = failed
    here = os.path.join(pin, cls)
    jpath = os.path.join(here, "provenance.json")
    exe = "-"
    head = "-"

    if not check(os.path.isfile(jpath), cls + " A1", "no provenance.json in " + here):
        print("  %-8s FAIL %d assertions" % (cls, total))
        continue

    raw = None
    try:
        with open(jpath, "r", encoding="utf-8") as fh:
            raw = json.load(fh)
    except Exception as exc:  # noqa: BLE001 - the message IS the assertion detail
        check(False, cls + " A2", "provenance.json does not parse: %s" % exc)
    if raw is None:
        print("  %-8s FAIL %d assertions" % (cls, total))
        continue
    check(isinstance(raw, dict), cls + " A2", "top level is not a JSON object")
    if not isinstance(raw, dict):
        print("  %-8s FAIL %d assertions" % (cls, total))
        continue

    keys = ["exe_sha256", "git_head", "native_tree_sha256", "request"]
    missing = [k for k in keys if k not in raw]
    check(not missing, cls + " A3", "missing ledger key(s): " + ", ".join(missing))

    check(raw.get("class") == cls, cls + " A4",
          "class field is %r, directory is %r" % (raw.get("class"), cls))

    if is_hex(raw.get("exe_sha256"), 64):
        exe = raw["exe_sha256"][:8]
    check(is_hex(raw.get("exe_sha256"), 64), cls + " A5",
          "exe_sha256 is not 64 lowercase hex: %r" % (raw.get("exe_sha256"),))

    if is_hex(raw.get("git_head"), 40):
        head = raw["git_head"][:7]
    check(is_hex(raw.get("git_head"), 40), cls + " A6",
          "git_head is not 40 lowercase hex: %r" % (raw.get("git_head"),))

    check(is_hex(raw.get("native_tree_sha256"), 64), cls + " A7",
          "native_tree_sha256 is not 64 lowercase hex: %r" % (raw.get("native_tree_sha256"),))

    req = raw.get("request")
    need = ["demo", "tick", "speed", "camera", "flags"]
    check(isinstance(req, dict) and all(k in req for k in need), cls + " A8",
          "request is not an object with demo/tick/speed/camera/flags")
    if not isinstance(req, dict):
        req = {}

    demo = req.get("demo")
    check(isinstance(demo, str) and len(demo) > 4 and demo.lower().endswith(".dem"),
          cls + " A9", "request.demo is not a .dem path: %r" % (demo,))

    tick = req.get("tick")
    check(is_int(tick) and tick > 0, cls + " A10",
          "request.tick is not an integer > 0: %r" % (tick,))

    speed = req.get("speed")
    check(is_int(speed) and 1 <= speed <= 64, cls + " A11",
          "request.speed is not an integer in 1..64: %r" % (speed,))

    cam = req.get("camera")
    cam_ok = isinstance(cam, list) and len(cam) == 5 and all(is_number(v) for v in cam)
    detail = "request.camera is not 5 finite numbers: %r" % (cam,)
    if cam_ok:
        pitch, yaw = float(cam[3]), float(cam[4])
        if not (-90.0 <= pitch <= 90.0):
            cam_ok, detail = False, "request.camera pitch out of -90..90: %r" % pitch
        elif not (-360.0 <= yaw <= 360.0):
            cam_ok, detail = False, "request.camera yaw out of -360..360: %r" % yaw
    check(cam_ok, cls + " A12", detail)

    check(isinstance(req.get("flags"), str), cls + " A12b",
          "request.flags is not a string: %r" % (req.get("flags"),))

    expected = raw.get("expected_bmp_sha256")
    expected_ok = isinstance(expected, dict) and all(k in expected for k in TAGS) \
        and all(is_hex(expected.get(k), 64) for k in TAGS)
    check(expected_ok, cls + " A13",
          "expected_bmp_sha256 does not name animated/identity/identity-2/off with 64-hex values")
    if not isinstance(expected, dict):
        expected = {}

    actual = {}
    actual_sizes = {}
    for tag in TAGS:
        path = os.path.join(here, tag + ".bmp")
        if not os.path.isfile(path):
            actual[tag] = None
            continue
        actual[tag] = sha256_file(path)
        actual_sizes[tag] = os.path.getsize(path)

    mismatched = [t for t in TAGS if actual.get(t) is None or actual.get(t) != expected.get(t)]
    check(not mismatched, cls + " A14",
          "pinned bytes do not match expected_bmp_sha256: " + ", ".join(
              "%s(%s vs %s)" % (t, (actual.get(t) or "missing")[:12], str(expected.get(t))[:12])
              for t in mismatched))

    both = actual.get("identity") and actual.get("identity-2")
    check(bool(both) and actual["identity"] == actual["identity-2"], cls + " A15",
          "identity.bmp and identity-2.bmp differ: %s vs %s" % (
              (actual.get("identity") or "missing")[:12], (actual.get("identity-2") or "missing")[:12]))

    ident = raw.get("identity")
    ident_ok = isinstance(ident, dict) and is_int(ident.get("ent")) and ident.get("ent") > 0 \
        and isinstance(ident.get("model"), str) and ident["model"].lower().endswith(".mdl") \
        and ident.get("capture_twice_byte_identical") is True
    check(ident_ok, cls + " A16",
          "identity block needs ent>0, model ending .mdl, capture_twice_byte_identical=true")

    if len(actual_sizes) == len(TAGS):
        sizes = set(actual_sizes.values())
        check(0 not in sizes and len(sizes) == 1, cls + " A17",
              "pinned BMP sizes are not one non-zero size: " + repr(actual_sizes))
    else:
        check(False, cls + " A17", "a pinned BMP is missing, so one frame size cannot be shown")

    # D-36: expected bytes must be corroborated by an independent capture
    # receipt. entity-id.txt is written by the capture step at capture time;
    # it is not derived from the BMPs by this gate and not by the ledger writer.
    receipt_lines = read_lines(os.path.join(here, "entity-id.txt"))
    receipt_bmp = {}
    receipt_product = None
    r18 = []
    if receipt_lines is None:
        r18.append("no entity-id.txt capture receipt in " + here)
    else:
        for line in receipt_lines:
            m = re.match(r"^frame: bmp (animated|identity|identity-2|off) sha256=([0-9a-f]{64})$", line)
            if m:
                receipt_bmp[m.group(1)] = m.group(2)
            elif line.startswith("frame: product sha256="):
                receipt_product = line.split("sha256=", 1)[1].strip()
        for tag in TAGS:
            seen = receipt_bmp.get(tag)
            if seen is None:
                r18.append(tag + " missing from receipt")
            elif seen != actual.get(tag):
                r18.append("%s receipt=%s file=%s" % (tag, seen[:12], (actual.get(tag) or "missing")[:12]))
            elif seen != expected.get(tag):
                r18.append("%s receipt=%s ledger=%s" % (tag, seen[:12], str(expected.get(tag))[:12]))
        if receipt_product is None:
            r18.append("receipt has no product sha256")
        elif receipt_product != raw.get("exe_sha256_at_capture"):
            r18.append("receipt product=%s ledger capture-exe=%s" % (
                receipt_product[:12], str(raw.get("exe_sha256_at_capture"))[:12]))
    check(not r18, cls + " A18",
          "capture receipt does not corroborate the pin: " + "; ".join(r18[:4]))

    # D-36: the receipt request/BODY and the four per-tag capture summaries are
    # a second and third channel. Editing one artifact cannot keep the pin green.
    r19 = []
    if receipt_lines is None:
        r19.append("no receipt")
    else:
        got = {}
        for line in receipt_lines:
            for key in ("demo", "flags"):
                m = re.match(r"^frame: request %s=(.*)$" % key, line)
                if m:
                    got[key] = m.group(1)
            m = re.match(r"^frame: request tick=(\d+) speed=(\d+)$", line)
            if m:
                got["tick"], got["speed"] = int(m.group(1)), int(m.group(2))
            m = re.match(r"^frame: request camera=(.*)$", line)
            if m:
                got["camera"] = m.group(1)
        if got.get("demo") != req.get("demo"):
            r19.append("demo receipt=%r ledger=%r" % (got.get("demo"), req.get("demo")))
        if got.get("tick") != req.get("tick") or got.get("speed") != req.get("speed"):
            r19.append("tick/speed receipt=%r/%r ledger=%r/%r" % (
                got.get("tick"), got.get("speed"), req.get("tick"), req.get("speed")))
        if got.get("flags") != req.get("flags"):
            r19.append("flags receipt=%r ledger=%r" % (got.get("flags"), req.get("flags")))
        cam_ok = False
        if got.get("camera") is not None:
            try:
                vals = [float(v) for v in got["camera"].split(",")]
                ledger_cam = req.get("camera")
                cam_ok = isinstance(ledger_cam, list) and len(vals) == len(ledger_cam) == 5 and all(
                    abs(vals[i] - float(ledger_cam[i])) <= 1e-6 for i in range(5))
            except (TypeError, ValueError, IndexError):
                cam_ok = False
        if not cam_ok:
            r19.append("camera receipt=%r ledger=%r" % (got.get("camera"), req.get("camera")))
        bod = None
        for line in receipt_lines:
            if line.startswith("BODY "):
                bod = re.search(r"ent=(\d+).*model=(\S+)", line)
                if bod:
                    break
        ident = raw.get("identity") if isinstance(raw.get("identity"), dict) else {}
        if bod is None:
            r19.append("receipt has no BODY line")
        elif int(bod.group(1)) != ident.get("ent") or bod.group(2) != ident.get("model"):
            r19.append("BODY ent/model=%s/%s ledger=%r/%r" % (
                bod.group(1), bod.group(2), ident.get("ent"), ident.get("model")))
    for tag in TAGS:
        slines = read_lines(os.path.join(here, tag + ".summary"))
        if slines is None:
            r19.append(tag + ".summary missing")
            continue
        tag_ok = any(line.startswith("tag=" + tag + " ") for line in slines)
        bmp_line = None
        product_line = None
        for line in slines:
            if line.startswith("frame: bmp=" + tag + ".bmp sha256="):
                bmp_line = line.split("sha256=", 1)[1].strip()
            elif line.startswith("frame: product sha256="):
                product_line = line.split("sha256=", 1)[1].strip()
        if not tag_ok:
            r19.append("%s.summary has no tag=%s line" % (tag, tag))
        if bmp_line != actual.get(tag):
            r19.append("%s.summary bmp=%s file=%s" % (tag, str(bmp_line)[:12], (actual.get(tag) or "missing")[:12]))
        if product_line != raw.get("exe_sha256_at_capture"):
            r19.append("%s.summary product=%s capture-exe=%s" % (
                tag, str(product_line)[:12], str(raw.get("exe_sha256_at_capture"))[:12]))
    check(not r19, cls + " A19",
          "capture receipt/summaries disagree with the ledger: " + "; ".join(r19[:4]))

    # D-36: the build identity is checked against the repository, the only
    # record here that the capture run did not write. A fabricated git_head or
    # native tree hash fails even if every pixel artifact is consistent.
    r20 = []
    if not is_hex(raw.get("git_head"), 40):
        r20.append("git_head is not 40 hex, so no commit can be checked")
    else:
        try:
            exists = subprocess.run(["git", "-C", root, "cat-file", "-e",
                                     raw["git_head"] + "^{commit}"],
                                    capture_output=True, timeout=30)
            if exists.returncode != 0:
                r20.append("git_head %s is not a commit in this repo" % raw["git_head"][:10])
            else:
                ls = subprocess.run(["git", "-C", root, "ls-tree", "-r",
                                     raw["git_head"], "--", "native"],
                                    capture_output=True, timeout=60)
                if ls.returncode != 0:
                    r20.append("git ls-tree failed rc=%d" % ls.returncode)
                else:
                    got_tree = hashlib.sha256(ls.stdout).hexdigest()
                    if got_tree != raw.get("native_tree_sha256"):
                        r20.append("native tree at %s hashes to %s, ledger says %s" % (
                            raw["git_head"][:10], got_tree[:16], str(raw.get("native_tree_sha256"))[:16]))
        except (OSError, subprocess.TimeoutExpired) as exc:
            r20.append("git unavailable: %s" % exc)
    check(not r20, cls + " A20",
          "recorded build identity is not reproducible: " + "; ".join(r20))

    own = failed - before
    print("  %-8s %s %d assertions  exe=%s git=%s" % (
        cls, "FAIL" if own else "PASS", total - start_total, exe, head))

# D-35: the assertion SET and COUNT are pinned. A check() call that is deleted
# leaves a hole here even if every remaining assertion passes, so a PASS token
# can no longer be produced by deleting a test.
want_total = len(CLASSES) * len(EXPECTED_IDS)
set_problems = []
for cls in CLASSES:
    got = [aid for (c, aid) in emitted if c == cls]
    missing = [a for a in EXPECTED_IDS if a not in got]
    unknown = sorted(set(got) - set(EXPECTED_IDS))
    dup = sorted(a for a in set(got) if got.count(a) > 1)
    if missing:
        set_problems.append("%s missing %d: %s" % (cls, len(missing), ",".join(missing[:8])))
    if unknown:
        set_problems.append("%s unknown %s" % (cls, ",".join(unknown[:8])))
    if dup:
        set_problems.append("%s duplicated %s" % (cls, ",".join(dup)))
if total != want_total:
    set_problems.append("count %d != pinned %d" % (total, want_total))
if set_problems:
    failures.append("assertion-set (D-35): " + "; ".join(set_problems))

if failures:
    for line in failures:
        print("  " + line)
    print("  %d of %d assertions failed" % (failed, total))
    print("PIN-PROVENANCE=FAIL (%d of %d pinned assertions)" % (total, want_total))
    sys.exit(1)

print("PIN-PROVENANCE=PASS (%d assertions)" % total)
sys.exit(0)
PYCODE

rc=$?
case "$rc" in
  0|1) exit "$rc" ;;
  *)
    echo "PIN-PROVENANCE=FAIL (tool error: interpreter exit $rc)"
    exit 2 ;;
esac
