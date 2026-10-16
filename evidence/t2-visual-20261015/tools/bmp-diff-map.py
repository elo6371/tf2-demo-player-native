#!/usr/bin/env python3
"""bmp-diff-map.py A.bmp B.bmp OUT.png [--grid N]

Render WHERE two 24bpp BMPs differ as a PNG: differing pixels red, identical
pixels the dimmed gray of frame A. A magenta grid every N pixels (default 100)
gives coordinates without reading numbers off the image.
"""
import struct
import sys
import zlib


def chunks(typ, data):
    out = struct.pack(">I", len(data)) + typ + data
    out += struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
    return out


def read_frame(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    flip = h < 0
    h = abs(h)
    return d[off:], w, h, bpp // 8, flip


def main(a_path, b_path, out_path, grid=100):
    a, w, h, B, aflip = read_frame(a_path)
    b, w2, h2, B2, _ = read_frame(b_path)
    assert (w, h, B) == (w2, h2, B2)
    stride = w * B
    raw = bytearray()
    for ry in range(h):          # output rows top-down
        y = ry if aflip else (h - 1 - ry)
        base = y * stride
        line = bytearray([0])
        for x in range(w):
            o = base + x * B
            if a[o:o + B] != b[o:o + B]:
                line += bytes((255, 0, 0))
            else:
                r, g, bb = a[o + 2] // 3, a[o + 1] // 3, a[o] // 3
                if x % grid == 0 or ry % grid == 0:
                    r, g, bb = 200, 0, 200
                line += bytes((r, g, bb))
        raw += line
    png = b"\x89PNG\r\n\x1a\n"
    png += chunks(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunks(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunks(b"IEND", b"")
    open(out_path, "wb").write(png)
    n = sum(1 for i in range(0, len(a), B) if a[i:i + B] != b[i:i + B])
    print("%s vs %s -> %s  differing_pixels=%d" % (a_path, b_path, out_path, n))


if __name__ == "__main__":
    grid = 100
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--grid" in sys.argv:
        grid = int(sys.argv[sys.argv.index("--grid") + 1])
    main(args[0], args[1], args[2], grid)
