#!/usr/bin/env python3
"""vpk_lookup.py -- does a material path exist in TF2's VPK set, without the product.

WHY THIS EXISTS
    N07's open item is naming the VMTs the maps reference but the install does not
    have. material_coverage_probe counts them (vmtMissing=10 on sultry) but has no
    listing mode, and adding one means a product change. The directory of a VPK is
    a plain tree, so the answer is readable offline.

USAGE
    python vpk_lookup.py <tf-root> <path> [<path> ...]
    python vpk_lookup.py --stdin <tf-root>      # one path per line

WHAT IT DOES NOT DO
    It does not decode VMTs. It answers "is there an entry for this path", which
    is the whole question behind vmtMissing.
"""
import os
import struct
import sys

SIGNATURE = 0x55AA1234  # Source 2013's little-endian magic


def read_tree(path):
    """Return {(dir, name): (archive, offset, length)} for every file in the VPK."""
    with open(path, "rb") as handle:
        data = handle.read()
    if struct.unpack_from("<I", data, 0)[0] != SIGNATURE:
        raise SystemExit("not a VPK: %s" % path)
    version = struct.unpack_from("<I", data, 4)[0]
    tree_size = struct.unpack_from("<I", data, 8)[0]
    # v2 appends four section sizes (file data, archive md5, other md5, signature)
    # before the tree; reading the tree at 12 on a v2 file lands in zeros and every
    # lookup "misses" -- measured: entries=0 on a 5 MB directory.
    at = 28 if version == 2 else 12
    end = at + tree_size
    out = {}
    # The tree nests three levels: extension -> folder -> filename -> entry. An
    # empty string closes a level; an empty extension closes the whole tree.
    while at < end:
        ext, at = cstr(data, at)
        if ext == "":
            break
        while True:
            folder, at = cstr(data, at)
            if folder == "":
                break
            while True:
                name, at = cstr(data, at)
                if name == "":
                    break
                crc, preload, archive, offset, length, term = struct.unpack_from("<IHHIIH", data, at)
                at += 18
                if term != 0xFFFF:
                    raise SystemExit("bad entry terminator at %d" % at)
                full = ("%s/%s.%s" % (folder, name, ext)).lstrip("/")
                out[full.lower()] = (archive, offset, length, preload)
    return version, out


def cstr(buf, at):
    zero = buf.find(b"\x00", at)
    return buf[at:zero].decode("latin-1"), zero + 1


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    if args[0] == "--stdin":
        tf = args[1]
        wanted = [line.strip() for line in sys.stdin if line.strip()]
    else:
        tf = args[0]
        wanted = args[1:]
    vpk = os.path.join(tf, "tf2_misc_dir.vpk")
    if not os.path.exists(vpk):
        print("no vpk at %s" % vpk)
        return 2
    version, tree = read_tree(vpk)
    found = missing = 0
    for path in wanted:
        key = path.replace("\\", "/").lstrip("/").lower()
        if key in tree:
            found += 1
        else:
            missing += 1
            print("MISSING %s" % path)
    print("vpk=%s version=%d entries=%d wanted=%d found=%d missing=%d"
          % (os.path.basename(vpk), version, len(tree), len(wanted), found, missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
