#!/usr/bin/env python3
"""crop-stack.py A.bmp B.bmp OUT.png x y w h

Stack the same crop (top-down coords) from A over the crop from B in one PNG,
A on top, B below, 3-pixel white separator. For comparing what changed between
two frames at a known difference region.
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


def row_bytes(data, w, h, B, flip, y):
    stored = y if flip else (h - 1 - y)
    base = stored * w * B
    return data[base:base + w * B]


def main(a_path, b_path, out_path, x, y, w, h):
    a, aw, ah, B, aflip = read_frame(a_path)
    b, bw, bh, B2, bflip = read_frame(b_path)
    assert (aw, ah, B) == (bw, bh, B2) and x >= 0 and y >= 0 and x + w <= aw and y + h <= ah
    out_h = h * 2 + 3
    raw = bytearray()
    for ry in range(out_h):
        line = bytearray([0])
        if ry < h:
            src, sy = a, y + ry
            src_flip = aflip
        elif ry == h or ry == h + 1 or ry == h + 2:
            raw += bytes([0] * 1) + bytes((255, 255, 255)) * w
            continue
        else:
            src, sy = b, y + (ry - h - 3)
            src_flip = bflip
        row = row_bytes(src, aw, ah, B, src_flip, sy)
        for px in range(x, x + w):
            o = px * B
            line += bytes((row[o + 2], row[o + 1], row[o]))
        raw += line
    png = b"\x89PNG\r\n\x1a\n"
    png += chunks(b"IHDR", struct.pack(">IIBBBBB", w, out_h, 8, 2, 0, 0, 0))
    png += chunks(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunks(b"IEND", b"")
    open(out_path, "wb").write(png)
    print("%s (top) / %s (bottom) crop %d,%d %dx%d -> %s" % (a_path, b_path, x, y, w, h, out_path))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3],
         int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6]), int(sys.argv[7]))
