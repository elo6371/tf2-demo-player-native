#!/usr/bin/env python3
"""T62: what a team's skin family paints, measured on the body's own pixels.

The identity frame the product writes under --world-entity-id 9 names every
drawn instance by a unique red byte (see t61-entityid.py), so a set of pixels in
that frame is a statement about ONE body. This script takes that set and reads
the SAME pixels out of a normally rendered frame at the same tick and camera.
The result is the mean colour the body's materials actually put there, which is
what a skin-family check has to be about: "family=1" in the ledger is a request,
and this is the paint.

Usage:
  player-body-appearance-pixels.py --ids IDENTITY.bmp
  player-body-appearance-pixels.py IDENTITY.bmp ID NORMAL.bmp

With --ids it prints one line per body colour found, so a caller can pick the
body to test instead of guessing which id a capture happened to draw.
"""
import struct
import sys


def load(path):
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise SystemExit("not a BMP: " + path)
    off = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit("want 24 or 32 bpp, got %d" % bpp)
    flip = height < 0
    height = abs(height)
    nbytes = bpp // 8
    stride = ((width * nbytes) + 3) & ~3
    px = []
    for y in range(height):
        row = data[off + y * stride: off + y * stride + width * nbytes]
        px.append([tuple(row[x * nbytes:x * nbytes + 3][::-1]) for x in range(width)])
    if not flip:
        px.reverse()
    return width, height, px


def is_id(rgb):
    r, g, b = rgb
    return g == 0 and b == 0 and r > 0


def main(argv):
    if len(argv) >= 3 and argv[1] == "--ids":
        w, h, identity = load(argv[2])
        counts = {}
        for y in range(h):
            for x in range(w):
                rgb = identity[y][x]
                if is_id(rgb):
                    counts[rgb[0]] = counts.get(rgb[0], 0) + 1
        for ident in sorted(counts):
            print("ID id=%d pixels=%d" % (ident, counts[ident]))
        print("IDS ids=%d pixels=%d" % (len(counts), sum(counts.values())))
        return 0
    if len(argv) != 4:
        print("usage: player-body-appearance-pixels.py --ids IDENTITY.bmp")
        print("       player-body-appearance-pixels.py IDENTITY.bmp ID NORMAL.bmp")
        return 2
    identity_path, ident, normal_path = argv[1], int(argv[2]), argv[3]
    w, h, identity = load(identity_path)
    w2, h2, normal = load(normal_path)
    if (w, h) != (w2, h2):
        print("FAIL size mismatch identity=%dx%d normal=%dx%d" % (w, h, w2, h2))
        return 1
    body = [(x, y) for y in range(h) for x in range(w)
            if identity[y][x][0] == ident and identity[y][x][1] == 0
            and identity[y][x][2] == 0]
    if not body:
        print("FAIL no pixels for id=%d in %s" % (ident, identity_path))
        return 1
    rs = gs = bs = 0
    for x, y in body:
        r, g, b = normal[y][x]
        rs += r
        gs += g
        bs += b
    n = len(body)
    mean_r, mean_g, mean_b = rs / n, gs / n, bs / n
    print("BODY id=%d pixels=%d mean_r=%.1f mean_g=%.1f mean_b=%.1f r_minus_b=%.1f b_minus_r=%.1f"
          % (ident, n, mean_r, mean_g, mean_b, mean_r - mean_b, mean_b - mean_r))
    print("SIZE %dx%d" % (w, h))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
