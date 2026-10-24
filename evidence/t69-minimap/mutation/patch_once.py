#!/usr/bin/env python3
"""Byte-exact single-occurrence patch for the T69 mutation suite.

WHY THIS EXISTS
  A mutation that silently matches nothing -- or matches twice and edits both
  copies -- is not a mutation. The build stays green, the gate stays green, and
  the suite reports "not caught" when in truth nothing was changed. So the match
  count is asserted to be exactly 1 BEFORE any byte is written, and the exit
  code distinguishes "anchor missing" from "anchor ambiguous" from "applied".

  The patched file is written in place; the caller keeps the byte-exact backup
  and is responsible for restoring it and proving the restore with cmp.

Usage: patch_once.py <file> <needle-file> <replacement-file>
Exit:  0 applied; 3 needle not found; 4 needle ambiguous; 2 usage or IO error.
"""
import sys


def main(argv):
    if len(argv) != 4:
        sys.stderr.write("usage: patch_once.py <file> <needle-file> <replacement-file>\n")
        return 2
    target, needle_path, repl_path = argv[1], argv[2], argv[3]
    try:
        with open(target, "rb") as handle:
            data = handle.read()
        with open(needle_path, "rb") as handle:
            needle = handle.read()
        with open(repl_path, "rb") as handle:
            replacement = handle.read()
    except OSError as error:
        sys.stderr.write("PATCH-REJECT reason=io-error detail=%s\n" % error)
        return 2
    if not needle:
        sys.stderr.write("PATCH-REJECT reason=empty-needle\n")
        return 2
    count = data.count(needle)
    if count == 0:
        sys.stderr.write("PATCH-REJECT reason=needle-not-found count=0\n")
        return 3
    if count != 1:
        sys.stderr.write("PATCH-REJECT reason=needle-ambiguous count=%d\n" % count)
        return 4
    with open(target, "wb") as handle:
        handle.write(data.replace(needle, replacement, 1))
    print("PATCH-APPLIED count=1 bytes=%d" % len(data))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
