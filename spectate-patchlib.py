#!/usr/bin/env python3
"""spectate-patchlib.py -- apply or revert one T66 mutant, byte-faithfully.

The gate it feeds compares readings, so a mutant that silently fails to apply
would leave a healthy tree behind and the "mutation suite" would be measuring
nothing. Every edit here therefore asserts that its needle occurs EXACTLY once,
joins the replacement with the file's own line ending (this repository checks out
CRLF and a needle written with LF matches nothing), and leaves a backup that is
compared byte-for-byte on restore.

  python spectate-patchlib.py apply <case>
  python spectate-patchlib.py restore <case>
  python spectate-patchlib.py verify <case>   (0 = applied, 1 = not applied)
  python spectate-patchlib.py list

Exit: 0 success, 1 verification failed, 2 unknown case, 3 needle not found once.
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BACKUP_DIR = os.path.join(HERE, ".scratch", "mut-backup")

H = "native/include/spectate_target.h"
C = "native/src/spectate_target.cpp"
M = "native/src/main.cpp"

CASES = {
    # The handover window collapses to zero: the camera jumps between players in
    # one frame, which is exactly the instant cut the task forbids.
    "m1": {
        "file": H,
        "needle": "static constexpr double kHandoverSeconds = 0.3;",
        "replacement": "static constexpr double kHandoverSeconds = 0.0;  // T66 m1: an instant cut",
        "what": "the handover window is zero seconds",
    },
    # The dead stay in the submitted set: a corpse is drawn after its owner died.
    "m2": {
        "file": C,
        "needle": "  return std::binary_search(deadIndices_.begin(), deadIndices_.end(), entityIndex);",
        "replacement": "  return false;  // T66 m2: corpses stay in the draw set",
        "what": "hidesModel never hides anybody",
    },
    # The pre-fix manual wiring: the manual pick changes the target but leaves the
    # alive/present readings from the OLD target, so the automatic rule steals the
    # pick back and animates the move.
    "m3": {
        "file": C,
        "needle": "      targetPresent = true;\n      targetAlive = true;\n      targetSlot = manualSlot;",
        "replacement": "      targetSlot = manualSlot;  // T66 m3: the pre-fix wiring: stale alive state",
        "what": "a manual pick is turned into a handover by the automatic rule",
    },
    # The demo's own observer handle is accepted without asking whether its target
    # is alive, so the camera follows a permanently dead spectator slot.
    "m4": {
        "file": C,
        "needle": "      directorUsable = slotObservable(*slot) && slotAlive(*slot);",
        "replacement": "      directorUsable = true;  // T66 m4: the demo's handle is trusted unvalidated",
        "what": "the demo director handle is trusted without validating the slot it names",
    },
    # No living player left, and the camera jumps to the world origin. The task calls
    # this out by name ("never jump to the world origin"), and the self-test asserts
    # it, so the mutation shows up in the decision section instead of in a capture.
    "m5": {
        "file": C,
        "needle": "  if (desired < 0) {\n    ++noTargetFrames_;",
        "replacement": ("  if (desired < 0) {\n    ++noTargetFrames_;\n"
                        "    focus_[0] = 0.0f; focus_[1] = 0.0f; focus_[2] = 0.0f; focusValid_ = true;  // T66 m5: jump to the world origin"),
        "what": "an empty roster jumps the camera to the world origin",
    },
    # The handover clock derived from the demo tick instead of the wall clock -- the
    # pre-fix wiring, where a paused recording froze the handover in flight.
    "m6": {
        "file": M,
        "needle": "      g_spectateDirector.update(wallSeconds, spectateSlots, spectateDirectorHandle, spectateSceneTick);",
        "replacement": ("      g_spectateDirector.update(static_cast<double>(spectateSceneTick) / 66.0, spectateSlots, spectateDirectorHandle, spectateSceneTick);"
                        "  // T66 m6: the demo clock drives the handover"),
        "what": "the handover clock is derived from the demo tick, so a pause freezes it",
    },
}


def read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read()


def write_bytes(path, data):
    with open(path, "wb") as handle:
        handle.write(data)


def eol_of(data):
    return b"\r\n" if b"\r\n" in data else b"\n"


def with_file_eol(text, eol):
    """The needles are written with LF; the checkout is CRLF."""
    return text.encode("utf-8").replace(b"\r\n", b"\n").replace(b"\n", eol)


def backup_path(case, relpath):
    return os.path.join(BACKUP_DIR, os.path.basename(relpath) + "." + case + ".orig")


def digest(data):
    return hashlib.sha256(data).hexdigest()[:16]


def apply(case):
    spec = CASES[case]
    path = os.path.join(HERE, spec["file"])
    data = read_bytes(path)
    needle = with_file_eol(spec["needle"], eol_of(data))
    replacement = with_file_eol(spec["replacement"], eol_of(data))
    count = data.count(needle)
    if count != 1:
        sys.stderr.write(
            "PATCH-FAIL case=%s file=%s needle occurs %d times, expected exactly 1\n"
            "  needle=%r\n  file eol=%r  file bytes=%d\n"
            % (case, spec["file"], count, needle, eol_of(data), len(data)))
        return 3
    os.makedirs(BACKUP_DIR, exist_ok=True)
    bak = backup_path(case, spec["file"])
    if not os.path.exists(bak):
        write_bytes(bak, data)
    else:
        if digest(read_bytes(bak)) != digest(data):
            sys.stderr.write("PATCH-FAIL case=%s: backup %s does not match the file on disk; "
                             "a previous mutant was not restored\n" % (case, bak))
            return 1
    patched = data.replace(needle, replacement, 1)
    write_bytes(path, patched)
    print("PATCH-APPLIED case=%s file=%s bytes=%d->%d %s"
          % (case, spec["file"], len(data), len(patched), spec["what"]))
    return 0


def restore(case):
    spec = CASES[case]
    path = os.path.join(HERE, spec["file"])
    bak = backup_path(case, spec["file"])
    if not os.path.exists(bak):
        sys.stderr.write("PATCH-FAIL case=%s: no backup at %s\n" % (case, bak))
        return 1
    original = read_bytes(bak)
    write_bytes(path, original)
    restored = read_bytes(path)
    if digest(restored) != digest(original):
        sys.stderr.write("PATCH-FAIL case=%s: restore did not reproduce the backup\n" % case)
        return 1
    print("PATCH-RESTORED case=%s file=%s bytes=%d sha=%s"
          % (case, spec["file"], len(restored), digest(restored)))
    return 0


def verify(case):
    spec = CASES[case]
    path = os.path.join(HERE, spec["file"])
    data = read_bytes(path)
    eol = eol_of(data)
    needle = with_file_eol(spec["needle"], eol)
    replacement = with_file_eol(spec["replacement"], eol)
    if replacement in data:
        print("PATCH-STATE case=%s applied=1" % case)
        return 0
    if needle in data:
        print("PATCH-STATE case=%s applied=0" % case)
        return 1
    sys.stderr.write("PATCH-FAIL case=%s: neither the original nor the mutant text is present\n" % case)
    return 3


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    command = argv[1]
    if command == "list":
        for name in sorted(CASES):
            print("%s %s %s" % (name, CASES[name]["file"], CASES[name]["what"]))
        return 0
    if len(argv) < 3 or argv[2] not in CASES:
        sys.stderr.write("unknown case %r; known: %s\n" % (argv[2] if len(argv) > 2 else None,
                                                            ", ".join(sorted(CASES))))
        return 2
    case = argv[2]
    if command == "apply":
        return apply(case)
    if command == "restore":
        return restore(case)
    if command == "verify":
        return verify(case)
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
