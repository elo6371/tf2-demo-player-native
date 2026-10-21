#!/usr/bin/env python3
"""T45: pixel/byte/direction counts for the material-normal reader pairs."""
import struct

D = "D:/TF2_Native_Worktrees/t40-pixel-pins/.scratch/world-material-semantics.20261021113848-82549/"


def read_bmp24(path):
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    flip = height > 0
    height = abs(height)
    stride = ((width * 3) + 3) & ~3
    out = bytearray(width * height * 3)
    for y in range(height):
        src = (height - 1 - y) if flip else y
        start = offset + src * stride
        out[y * width * 3:(y + 1) * width * 3] = data[start:start + width * 3]
    return width, height, out


def pair(name_a, name_b):
    w, h, a = read_bmp24(D + name_a)
    _, _, b = read_bmp24(D + name_b)
    px = 0
    up = down = 0
    maxd = 0
    for i in range(w * h):
        diff = False
        for c in range(3):
            d = b[i*3+c] - a[i*3+c]
            if d:
                diff = True
                if d > 0:
                    up += 1
                else:
                    down += 1
                maxd = max(maxd, abs(d))
        if diff:
            px += 1
    print("%-22s vs %-22s px=%5d bytes_up=%5d bytes_down=%5d max=%3d"
          % (name_a, name_b, px, up, down, maxd))


pair("C.bmp", "D.bmp")
pair("C12.bmp", "R1.bmp")
pair("S1.bmp", "S2.bmp")
pair("S1_12.bmp", "S2_12.bmp")
