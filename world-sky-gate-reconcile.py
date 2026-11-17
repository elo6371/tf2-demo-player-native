#!/usr/bin/env python3
"""world-sky-gate-reconcile.py -- W-1: reconcile the sky-on and sky-off frames.

WHY THIS FILE EXISTS
--------------------
The negative control used to pin a constant: "--no-world-sky reproduces 2,072
enclosed clear pixels". That constant is a property of one defect, not of the
sky pass. Once the displacement parameterisation is fixed (work/disp-param, P0)
the same pixels are painted by terrain, the constant becomes 0, and a pin would
turn a correct merge red. This tool states the control in a form that holds
under any world geometry:

    diff           pixels that are CLEAR in the sky-off frame and NOT clear in
                   the sky-on frame -- i.e. pixels the sky pass painted
    far            how many of those pixels sit at the far plane in the sky-on
                   depth plane (no world geometry in front of them)
    enclosed_off   enclosed clear pixels in the sky-off frame
    enclosed_on    enclosed clear pixels in the sky-on frame
    enclosed_diff  how many of diff's pixels were ENCLOSED in the sky-off frame

The old assertion becomes the identity
    enclosed_off - enclosed_on == enclosed_diff
which cannot go stale, while  far == diff  is the soundness statement: the sky
painted only pixels the world does not own, never one of the world's.

Usage:
  world-sky-gate-reconcile.py ON.bmp OFF.bmp DEPTH.pgm
Prints one line:
  diff=N far=F enclosed_off=E1 enclosed_on=E2 enclosed_diff=E3
DEPTH.pgm is a --capture-pass-depth plane taken on the sky-on frame at a point
where the world has already drawn (name suffix 10-sky or 30-world-done); the
plane format is P5, 16-bit, depth = value / 65535. Exit 2 = unreadable input.
"""
import struct
import sys

CLEAR = (14, 18, 22)
FAR = 1.0 - 1.0e-6


def read_bmp(path):
    """Return (clear_mask_rows, width, height); mask 1 == exact clear colour."""
    with open(path, 'rb') as handle:
        data = handle.read()
    if len(data) < 54 or data[0:2] != b'BM':
        raise ValueError('not a BMP: ' + path)
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    compression = struct.unpack_from('<I', data, 30)[0]
    if compression != 0 or bpp not in (24, 32):
        raise ValueError('unsupported BMP (bpp=%d compression=%d)' % (bpp, compression))
    stride = ((width * bpp // 8) + 3) & ~3
    flip = height > 0
    height = abs(height)
    step = bpp // 8
    rows = []
    for y in range(height):
        start = offset + (height - 1 - y if flip else y) * stride
        row = bytearray(width)
        for x in range(width):
            base = start + x * step
            row[x] = 1 if (data[base + 2], data[base + 1], data[base]) == CLEAR else 0
        rows.append(row)
    return rows, width, height


def enclosed_mask(rows, width, height):
    """1 where a clear pixel is NOT 4-connected to the frame border."""
    seen = [bytearray(width) for _ in range(height)]
    stack = []
    for x in range(width):
        for y in (0, height - 1):
            if rows[y][x] and not seen[y][x]:
                seen[y][x] = 1
                stack.append((x, y))
    for y in range(height):
        for x in (0, width - 1):
            if rows[y][x] and not seen[y][x]:
                seen[y][x] = 1
                stack.append((x, y))
    while stack:
        x, y = stack.pop()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < width and 0 <= ny < height and rows[ny][nx] and not seen[ny][nx]:
                seen[ny][nx] = 1
                stack.append((nx, ny))
    return [[1 if rows[y][x] and not seen[y][x] else 0 for x in range(width)]
            for y in range(height)]


def read_pgm(path):
    """Return (plane, width, height) with depth in [0, 1]."""
    with open(path, 'rb') as handle:
        data = handle.read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1
    magic, width, height, maxv = fields[0], int(fields[1]), int(fields[2]), int(fields[3])
    if magic != b'P5' or maxv != 65535:
        raise ValueError('unsupported PGM (magic=%r maxval=%d)' % (magic, maxv))
    plane = []
    for y in range(height):
        row = []
        for x in range(width):
            off = pos + 2 * (y * width + x)
            row.append(((data[off] << 8) | data[off + 1]) / 65535.0)
        plane.append(row)
    return plane, width, height


def main(argv):
    if len(argv) != 4:
        print(__doc__.strip().splitlines()[-1])
        return 2
    try:
        on_rows, width, height = read_bmp(argv[1])
        off_rows, off_width, off_height = read_bmp(argv[2])
        plane, plane_width, plane_height = read_pgm(argv[3])
    except (OSError, ValueError, IndexError) as error:
        print('cannot read input: %s' % error)
        return 2
    if (off_width, off_height) != (width, height) or (plane_width, plane_height) != (width, height):
        print('input size mismatch: frame %dx%d, off %dx%d, depth %dx%d'
              % (width, height, off_width, off_height, plane_width, plane_height))
        return 2
    on_enclosed = enclosed_mask(on_rows, width, height)
    off_enclosed = enclosed_mask(off_rows, width, height)
    diff = far = enclosed_diff = 0
    for y in range(height):
        on_row, off_row, plane_row = on_rows[y], off_rows[y], plane[y]
        for x in range(width):
            # Only pixels the sky OFF frame left clear and the sky ON frame did
            # not: the world draws identically in both runs, so a pixel that
            # changed owner can only have been painted by the sky pass.
            if on_row[x] or not off_row[x]:
                continue
            diff += 1
            if plane_row[x] >= FAR:
                far += 1
            if off_enclosed[y][x]:
                enclosed_diff += 1
    # The two enclosed totals come from their own masks: enclosed_off counts
    # every enclosed clear pixel of the OFF frame, not just the ones the sky
    # filled, and the identity the gate asserts is their difference.
    enclosed_off = sum(sum(row) for row in off_enclosed)
    enclosed_on = sum(sum(row) for row in on_enclosed)
    print('diff=%d far=%d enclosed_off=%d enclosed_on=%d enclosed_diff=%d'
          % (diff, far, enclosed_off, enclosed_on, enclosed_diff))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
