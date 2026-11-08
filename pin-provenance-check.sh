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
# WHAT IT ASSERTS (17 assertions per class: demo scout soldier medic)
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

"$PY" - "$PIN" "$QUIET" <<'PYCODE'
import hashlib
import json
import math
import os
import sys

pin = sys.argv[1]
quiet = sys.argv[2] == "1"
CLASSES = ["demo", "scout", "soldier", "medic"]
TAGS = ["animated", "identity", "identity-2", "off"]
HEXDIGITS = set("0123456789abcdef")

total = 0
failed = 0
failures = []


def check(ok, label, msg):
    """Count one assertion; record the failure with its own name."""
    global total, failed
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

    own = failed - before
    print("  %-8s %s %d assertions  exe=%s git=%s" % (
        cls, "FAIL" if own else "PASS", total - start_total, exe, head))

if failures:
    for line in failures:
        print("  " + line)
    print("  %d of %d assertions failed" % (failed, total))
    print("PIN-PROVENANCE=FAIL (%d assertions)" % total)
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
