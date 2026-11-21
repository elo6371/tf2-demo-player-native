#!/usr/bin/env python3
"""T105: derive an HDR-only copy of a shipped map.

Lump 8 (the LDR lighting page) has its header entry zeroed; lump 53 (the HDR
page) is left exactly as the compiler wrote it. On the base commit this copy
read lightmapMode "unavailable" -- the equal-length guard at the head of the
parse asked for a page the map does not carry, and the packer never ran. On
this branch the same bytes pack from lump 53 and reach the readings of the
untouched map.

usage: make_hdr_only.py [--map cp_snakewater_final1] [--out <path>]
"""
import argparse
import os
import shutil
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TF = r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
LUMP_LIGHTING = 8
LUMP_LIGHTING_HDR = 53


def lump(data, index):
    return struct.unpack_from("<ii", data, 8 + index * 16)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", default="cp_snakewater_final1")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    src = os.path.join(TF, "maps", args.map + ".bsp")
    out = args.out or os.path.join(REPO, ".scratch", "hdr-only-" + args.map + ".bsp")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    shutil.copyfile(src, out)
    data = bytearray(open(out, "rb").read())
    assert bytes(data[:4]) == b"VBSP", data[:4]
    ldr = lump(data, LUMP_LIGHTING)
    hdr = lump(data, LUMP_LIGHTING_HDR)
    print("lump8", ldr, "lump53", hdr)
    assert hdr[1] > 0, "this map carries no HDR page, so it cannot show the tail"
    struct.pack_into("<ii", data, 8 + LUMP_LIGHTING * 16, 0, 0)
    open(out, "wb").write(data)
    check = bytearray(open(out, "rb").read())
    print("patched lump8", lump(check, LUMP_LIGHTING), "lump53", lump(check, LUMP_LIGHTING_HDR))
    print("wrote", out)


if __name__ == "__main__":
    main()
