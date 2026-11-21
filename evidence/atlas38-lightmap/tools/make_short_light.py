#!/usr/bin/env python3
"""T105: derive a map whose lighting page is too short for its own faces.

Lump 8's declared length is cut to a third of what the compiler wrote, so face
records that point near the end of the page now address bytes outside it. The
packer refuses those faces and, with TF2_NATIVE_LM_DROPS=1, names every one of
them. This is the case the old counter could not describe: it is not a shelf
overflow, it is the face's own light offset leaving the page.

usage: make_short_light.py [--map cp_snakewater_final1] [--divisor 3] [--out <path>]
"""
import argparse
import os
import shutil
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TF = r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
LUMP_LIGHTING = 8


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", default="cp_snakewater_final1")
    parser.add_argument("--divisor", type=int, default=3)
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    src = os.path.join(TF, "maps", args.map + ".bsp")
    out = args.out or os.path.join(REPO, ".scratch", "short-light-" + args.map + ".bsp")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    shutil.copyfile(src, out)
    data = bytearray(open(out, "rb").read())
    offset, length = struct.unpack_from("<ii", data, 8 + LUMP_LIGHTING * 16)
    print("lump8 offset", offset, "length", length)
    shortened = length // args.divisor
    struct.pack_into("<ii", data, 8 + LUMP_LIGHTING * 16, offset, shortened)
    open(out, "wb").write(data)
    print("patched lump8 length ->", shortened, "(divisor", str(args.divisor) + ")")
    print("wrote", out)


if __name__ == "__main__":
    main()
