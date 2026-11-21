#!/usr/bin/env python3
"""vpk_dump.py -- print one file out of TF2's VPK set.

vpk_lookup.py answers "does this entry exist"; this prints its bytes, which is what
reading a VMT out of a packed install needs. Same directory-tree parse as that tool,
plus the data half: archive 0x7fff means the bytes live in the directory VPK itself,
after the tree; any other archive number is <stem>_NNN.vpk beside it.

usage: python vpk_dump.py <tf-root> <path>
"""
import os
import struct
import sys

SIGNATURE = 0x55AA1234

def cstr(buf, at):
    zero = buf.find(b"\x00", at)
    return buf[at:zero].decode("latin-1"), zero + 1

def read_tree(data):
    version = struct.unpack_from("<I", data, 4)[0]
    tree_size = struct.unpack_from("<I", data, 8)[0]
    at = 28 if version == 2 else 12
    end = at + tree_size
    out = {}
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
                full = ("%s/%s.%s" % (folder, name, ext)).lstrip("/")
                out[full.lower()] = (archive, offset, length, preload, data[at:at + preload])
                at += preload
    return version, out, end

def main():
    tf, want = sys.argv[1], sys.argv[2]
    vpk_path = os.path.join(tf, "tf2_misc_dir.vpk")
    data = open(vpk_path, "rb").read()
    version, tree, tree_end = read_tree(data)
    key = want.replace(os.sep, "/").lstrip("/").lower()
    if key not in tree:
        print("MISSING %s" % want)
        return 2
    archive, offset, length, preload, pre = tree[key]
    if preload:
        print("preload %d bytes unsupported" % preload)
        return 3
    if archive == 0x7FFF:
        blob = data[tree_end + offset: tree_end + offset + length]
    else:
        stem = vpk_path[: -len("_dir.vpk")]
        chunk = "%s_%03d.vpk" % (stem, archive)
        with open(chunk, "rb") as h:
            h.seek(offset)
            blob = h.read(length)
    sys.stdout.write(blob.decode("latin-1"))
    return 0

if __name__ == "__main__":
    sys.exit(main())