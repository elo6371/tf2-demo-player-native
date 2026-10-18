#!/usr/bin/env python3
"""bmp-crop-grid.py OUT.png --region X,Y,W,H --scale N [--label TEXT] A.bmp B.bmp ...

One PNG holding the SAME rectangle of several frames side by side, nearest-neighbour
scaled, with a one-pixel separator between panels. What this is for: T14's frames
differ by 7190 pixels inside a 49x351 box, and a whole-frame screenshot at 1264x681
turns that into "the picture looks the same". Cropped and tripled, it is a rifle.

Panels are stacked left to right in argument order; the separator is a flat grey
column so a reader can see where one frame ends. No PIL: the BMP reader and PNG
writer are the same standard-library ones bmp-to-png.py uses, because the point of
these captures is that the artefacts are checkable without another dependency.

Exit: 0 = written, 2 = bad arguments or an unreadable frame.
"""
import struct
import sys
import zlib


def chunks(typ, data):
    out = struct.pack(">I", len(data)) + typ + data
    out += struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
    return out


def read_bmp(path):
    d = open(path, "rb").read()
    if d[0:2] != b"BM":
        raise ValueError("not a BMP: " + path)
    off = struct.unpack_from("<I", d, 10)[0]
    width, height = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24 or int.from_bytes(d[30:34], "little") != 0:
        raise ValueError("want 24bpp BI_RGB: " + path)
    flip = height < 0
    height = abs(height)
    stride = (width * 3 + 3) // 4 * 4
    rows = []
    for y in range(height):
        # Stored bottom-up unless the height is negative; normalise to top-down so
        # the crop rectangle means the same thing whatever the writer did.
        src = y if flip else height - 1 - y
        base = off + src * stride
        rows.append(bytes(d[base:base + width * 3]))
    return width, height, rows


def panel(width, height, rows, region, scale):
    x0, y0, w, h = region
    out = []
    for y in range(h):
        line = bytearray()
        row = rows[y0 + y]
        for x in range(w):
            b, g, r = row[(x0 + x) * 3], row[(x0 + x) * 3 + 1], row[(x0 + x) * 3 + 2]
            line += bytes((r, g, b)) * scale
        for _ in range(scale):
            out.append(bytes(line))
    return w * scale, h * scale, out


def main(argv):
    if len(argv) < 6:
        print(__doc__.strip())
        return 2
    dst = argv[1]
    region = None
    scale = 1
    frames = []
    i = 2
    while i < len(argv):
        arg = argv[i]
        if arg == "--region":
            region = tuple(int(v) for v in argv[i + 1].split(","))
            i += 2
        elif arg == "--scale":
            scale = int(argv[i + 1])
            i += 2
        elif arg == "--label":
            i += 2  # labels are for the caller's echo, not the pixels
        else:
            frames.append(arg)
            i += 1
    if region is None or len(region) != 4 or not frames:
        print("need --region X,Y,W,H and at least one frame")
        return 2

    panels = []
    for frame in frames:
        width, height, rows = read_bmp(frame)
        x0, y0, w, h = region
        if x0 + w > width or y0 + h > height:
            print("region %s does not fit %s (%dx%d)" % (region, frame, width, height))
            return 2
        panels.append(panel(width, height, rows, region, scale))

    gap = 2
    out_w = sum(p[0] for p in panels) + gap * (len(panels) - 1)
    out_h = max(p[1] for p in panels)
    raw = bytearray()
    for y in range(out_h):
        line = bytearray([0])
        for index, (pw, ph, rows) in enumerate(panels):
            line += rows[y] if y < ph else bytes(pw * 3)
            if index != len(panels) - 1:
                line += bytes((96, 96, 96)) * gap
        raw += line
    png = b"\x89PNG\r\n\x1a\n"
    png += chunks(b"IHDR", struct.pack(">IIBBBBB", out_w, out_h, 8, 2, 0, 0, 0))
    png += chunks(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunks(b"IEND", b"")
    open(dst, "wb").write(png)
    print("%s -> %s panels=%d %dx%d region=%s scale=%d"
          % ("+".join(frames), dst, len(panels), out_w, out_h,
             ",".join(str(v) for v in region), scale))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
