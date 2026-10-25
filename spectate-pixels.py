#!/usr/bin/env python3
"""Compare two 24-bit BMP captures and print the reading a gate can assert.

T66 uses this for one narrow question: does the corpse rule change a frame in
which nobody has died? A rule that hides living players would show up here as a
non-zero PIXELS count, and the same file is the tool the earlier T61 gate used,
so a BMP that this script cannot read is a defect in the capture, not a pass.

Usage: python spectate-pixels.py <a.bmp> <b.bmp>
Output: PIXELS=<n> WORST=<m> SIZE=<W>x<H> BPP=<bpp> BBOX=<x0,y0..x1,y1>
Exit:  0 on success, 2 on an unreadable or mismatched pair.
"""
import struct
import sys


def read_bmp(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"BM":
        raise SystemExit("not a BMP: " + path)
    off = struct.unpack_from("<I", data, 10)[0]
    _size, w, h, _planes, bpp, comp, _imgsize, _x, _y, _used, _imp = struct.unpack_from("<IiiHHIIiiII", data, 14)
    if comp != 0:
        raise SystemExit("compressed BMP not supported: %s bpp=%d comp=%d" % (path, bpp, comp))
    if bpp not in (24, 32):
        raise SystemExit("unsupported bpp %d in %s" % (bpp, path))
    step = bpp // 8
    stride = ((w * step) + 3) & ~3
    bottom_up = h > 0
    rows = abs(h)
    pixels = []
    for row in range(rows):
        src = (rows - 1 - row) if bottom_up else row
        base = off + src * stride
        line = data[base:base + w * step]
        pixels.append([(line[i * step + 2], line[i * step + 1], line[i * step]) for i in range(w)])
    return w, rows, bpp, pixels


def main():
    if len(sys.argv) != 3:
        print("usage: spectate-pixels.py <a.bmp> <b.bmp>", file=sys.stderr)
        return 2
    left, right = sys.argv[1], sys.argv[2]
    wa, ha, bpp, a = read_bmp(left)
    wb, hb, bpp_b, b = read_bmp(right)
    if (wa, ha) != (wb, hb):
        print("PIXELS=error SIZE=%dx%d vs %dx%d" % (wa, ha, wb, hb))
        return 2
    differing = 0
    worst = 0
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for y in range(ha):
        row_a, row_b = a[y], b[y]
        for x in range(wa):
            ca, cb = row_a[x], row_b[x]
            delta = max(abs(ca[0] - cb[0]), abs(ca[1] - cb[1]), abs(ca[2] - cb[2]))
            if delta == 0:
                continue
            differing += 1
            if delta > worst:
                worst = delta
            if x < x0: x0 = x
            if y < y0: y0 = y
            if x > x1: x1 = x
            if y > y1: y1 = y
    box = "%d,%d..%d,%d" % (x0, y0, x1, y1) if differing else "-"
    print("PIXELS=%d WORST=%d SIZE=%dx%d BPP=%d BBOX=%s" % (differing, worst, wa, ha, bpp, box))
    return 0


if __name__ == "__main__":
    sys.exit(main())
