#!/usr/bin/env python3
"""bmp-to-png.py SRC.bmp DST.png

Convert a 24-bit BI_RGB BMP (what tf2_demo_native writes: 54-byte header,
40-byte DIB, rows padded to 4 bytes, bottom-up unless the height is negative)
to PNG using only the standard library. PIL is not installed on this machine,
and the point of the frame captures is that the artefacts are verifiable
without pulling in another dependency.
"""
import struct
import sys
import zlib


def chunks(typ, data):
    out = struct.pack(">I", len(data)) + typ + data
    out += struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
    return out


def main(src, dst):
    d = open(src, "rb").read()
    assert d[0:2] == b"BM", "not a BMP: " + src
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    assert bpp == 24 and int.from_bytes(d[30:34], "little") == 0, "want 24bpp BI_RGB"
    flip = h < 0  # negative height means top-down
    h = abs(h)
    stride = (w * 3 + 3) // 4 * 4
    raw = bytearray()
    rows = range(h) if flip else range(h - 1, -1, -1)
    for y in rows:
        base = off + y * stride
        line = bytearray([0])  # PNG filter type 0
        for x in range(w):
            b, g, r = d[base + x * 3], d[base + x * 3 + 1], d[base + x * 3 + 2]
            line += bytes((r, g, b))
        raw += line
    png = b"\x89PNG\r\n\x1a\n"
    png += chunks(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunks(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunks(b"IEND", b"")
    open(dst, "wb").write(png)
    print("%s -> %s  %dx%d" % (src, dst, w, h))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
