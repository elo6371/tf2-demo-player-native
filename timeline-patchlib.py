#!/usr/bin/env python3
"""timeline-patchlib.py -- apply or revert one timeline mutant, byte-faithfully.

The gate it feeds measures the clock's behaviour, so a mutant that silently fails
to apply would leave a healthy tree behind and the mutation suite would be
measuring nothing. Every edit therefore asserts that its needle occurs EXACTLY
once, joins the replacement with the file's own line ending (this repository
checks out CRLF and a needle written with LF matches nothing), and leaves a
backup that is compared byte-for-byte on restore.

  python timeline-patchlib.py apply <case>
  python timeline-patchlib.py restore <case>
  python timeline-patchlib.py verify <case>   (0 = the tree is clean, 1 = it still
                                               carries the mutant)
  python timeline-patchlib.py list

Exit: 0 success, 1 verification failed, 2 unknown case, 3 needle not found once.
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BACKUP_DIR = os.path.join(HERE, ".scratch", "timeline-mut-backup")
M = "native/src/main.cpp"

CASES = {
    # m1: the T27 hold is removed, so --capture-tick N means "the first frame
    # drawn at or after N" -- the historical T26 defect. At 32x a drawn frame
    # spans several ticks, so section 1's tick assertion must name this one.
    "m1": {
        "file": M,
        "needle": """        if (captureArmed && captureTick > 0 && g_playback.tick > captureTick) {
          g_playback.tick = static_cast<std::int32_t>(captureTick);
          g_playbackControl.noteExternalCursor(g_playback.tick);
          g_playbackControl.resetAccumulator();
        }""",
        "replacement": """        // timeline mutation m1: the T27 hold is gone; a capture now lands on the
        // first drawn tick at or after the requested one.
        (void)captureTick;""",
        "what": "the capture hold is removed (capture lands at or after N)",
    },
    # m2: --playback-speed 8 is played at 4. This is the mapping error the task
    # names. Section 2's pixel clause cannot see it -- the frame at a pinned tick
    # is a pure function of the tick (T85 measured that) -- so the named red is
    # section 2's "the run really used the requested speed". The mutation driver
    # prints the pixel result next to it so the limit is on the record.
    "m2": {
        "file": M,
        "needle": """        if (end && *end == L'\\0' && parsed > 0.0) playbackSpeed = parsed;""",
        "replacement": """        if (end && *end == L'\\0' && parsed > 0.0) playbackSpeed = (parsed == 8.0) ? 4.0 : parsed;  // m2: 8x is played at 4x""",
        "what": "--playback-speed 8 is played at 4",
    },
}


def fail(message):
    sys.stderr.write(message + "\n")
    sys.exit(1)


def read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def backup_path(case):
    return os.path.join(BACKUP_DIR, case + "-" + os.path.basename(CASES[case]["file"]))


def apply_case(case):
    entry = CASES[case]
    path = os.path.join(HERE, entry["file"])
    text = read_bytes(path)
    newline = b"\r\n" if b"\r\n" in text else b"\n"
    needle = entry["needle"].encode("utf-8").replace(b"\n", newline)
    replacement = entry["replacement"].encode("utf-8").replace(b"\n", newline)
    occurrences = text.count(needle)
    if occurrences != 1:
        fail("the needle matches %d sites in %s, expected exactly 1" % (occurrences, entry["file"]))
    os.makedirs(BACKUP_DIR, exist_ok=True)
    with open(backup_path(case), "wb") as handle:
        handle.write(text)
    patched = text.replace(needle, replacement, 1)
    if patched == text:
        fail("the replacement did not change the file")
    with open(path, "wb") as handle:
        handle.write(patched)
    print("applied=%s file=%s sha256=%s mutant=%s" % (case, entry["file"], sha(patched)[:16], entry["what"]))


def restore_case(case):
    entry = CASES[case]
    path = os.path.join(HERE, entry["file"])
    backup = backup_path(case)
    if not os.path.exists(backup):
        fail("no backup for %s; was it ever applied in this tree?" % case)
    original = read_bytes(backup)
    with open(path, "wb") as handle:
        handle.write(original)
    print("restored=%s sha256=%s" % (case, sha(original)[:16]))


def verify_case(case):
    entry = CASES[case]
    path = os.path.join(HERE, entry["file"])
    backup = backup_path(case)
    if not os.path.exists(backup):
        fail("no backup for %s; nothing to verify" % case)
    original = read_bytes(backup)
    current = read_bytes(path)
    applied = 0 if current == original else 1
    print("applied=%d case=%s sha256_now=%s sha256_original=%s" % (applied, case, sha(current)[:16], sha(original)[:16]))
    sys.exit(0 if applied == 0 else 1)


def main():
    if len(sys.argv) < 2:
        fail("usage: timeline-patchlib.py {apply|restore|verify|list} [case]")
    action = sys.argv[1]
    if action == "list":
        for case in sorted(CASES):
            print("%s %s %s" % (case, CASES[case]["file"], CASES[case]["what"]))
        return
    if len(sys.argv) != 3:
        fail("usage: timeline-patchlib.py %s <case>" % action)
    case = sys.argv[2]
    if case not in CASES:
        fail("unknown case %r (known: %s)" % (case, ", ".join(sorted(CASES))))
    if action == "apply":
        apply_case(case)
    elif action == "restore":
        restore_case(case)
    elif action == "verify":
        verify_case(case)
    else:
        fail("unknown action %r" % action)


if __name__ == "__main__":
    main()
