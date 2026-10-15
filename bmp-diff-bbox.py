#!/usr/bin/env python3
"""bmp-diff-bbox.py A.bmp B.bmp

Locate WHERE two 24/32bpp BI_RGB BMPs differ: number of differing bytes plus
the bounding box of differing pixels in top-left origin coordinates. Rows that
are byte-identical are skipped whole, so a pair whose difference is a small
region costs about the size of that region.

Exit 0 always (it is a reading, not a gate); prints one line:
  differing_pixels=N bbox=(x=.. .. .., y=.. .. ..) w=.. h=..
or
  differing_pixels=0 (frames byte-identical)
"""
import struct
import sys


def read_frame(path):
    data = open(path, "rb").read()
    assert data[0:2] == b"BM", path + " is not a BMP"
    off = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    assert bpp in (24, 32), "want 24 or 32 bpp, got %d" % bpp
    flip = height < 0
    height = abs(height)
    return data[off:], width, height, bpp // 8, flip


def main(a_path, b_path):
    a, aw, ah, abpp, aflip = read_frame(a_path)
    b, bw, bh, bbpp, bflip = read_frame(b_path)
    assert (aw, ah, abpp) == (bw, bh, bbpp), "geometry differs: %dx%dx%d vs %dx%dx%d" % (
        aw, ah, abpp, bw, bh, bbpp)
    stride = aw * abpp
    minx, miny, maxx, maxy = aw, ah, -1, -1
    count = 0
    for y in range(ah):
        base = y * stride
        if a[base:base + stride] == b[base:base + stride]:
            continue
        for x in range(aw):
            o = base + x * abpp
            if a[o:o + abpp] != b[o:o + abpp]:
                count += 1
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                ty = y if aflip else (ah - 1 - y)
                if ty < miny:
                    miny = ty
                if ty > maxy:
                    maxy = ty
    if count == 0:
        print("differing_pixels=0 (frames byte-identical)")
        return 0
    print("differing_pixels=%d bbox=(x=%d..%d, y=%d..%d) w=%d h=%d" % (
        count, minx, maxx, miny, maxy, maxx - minx + 1, maxy - miny + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
