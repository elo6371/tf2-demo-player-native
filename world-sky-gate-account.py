#!/usr/bin/env python3
"""world-sky-gate-account.py -- W-1: count the clear pixels a frame still has.

WHY THIS FILE EXISTS
--------------------
The sky pass is drawn before the world and writes no depth, so a depth test
cannot say whether a pixel was painted. The only sound test is the colour: the
frame's clear colour is RGB(14,18,22), and after the sky is drawn no pixel the
world leaves empty may keep it.

This tool answers exactly two numbers for one BMP:

    clear_total     every pixel equal to the clear colour
    enclosed_clear  those not 4-connected to the frame border through clear
                    pixels -- the "black block inside the picture" population
                    the user report is about

Usage:
  world-sky-gate-account.py FRAME.bmp [--coords] [--diff OTHER.bmp]
Prints one line: `clear_total=N enclosed_clear=M`. With --diff it adds
`frame_diff=N`, the number of pixels whose RGB differs from OTHER.bmp at all
(raw pixel comparison, for frames that must be identical pictures). Exit 2 =
unreadable frame.
"""
import struct
import sys

CLEAR = (14, 18, 22)


def read_bmp(path):
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


def read_pixels(path):
    """Return (pixel_bytes, width, height) -- raw RGB triples, no interpretation."""
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
    pixels = bytearray()
    for y in range(height):
        start = offset + (height - 1 - y if flip else y) * stride
        for x in range(width):
            base = start + x * step
            pixels += bytes((data[base + 2], data[base + 1], data[base]))
    return pixels, width, height


def main(argv):
    args = argv[1:]
    coords = '--coords' in args
    args = [arg for arg in args if arg != '--coords']
    other = None
    if '--diff' in args:
        index = args.index('--diff')
        if index + 1 >= len(args):
            print(__doc__.strip().splitlines()[-1])
            return 2
        other = args[index + 1]
        del args[index:index + 2]
    if len(args) != 1:
        print(__doc__.strip().splitlines()[-1])
        return 2
    try:
        rows, width, height = read_bmp(args[0])
    except (OSError, ValueError) as error:
        print('cannot read frame: %s' % error)
        return 2
    total = sum(sum(row) for row in rows)
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
    enclosed = 0
    points = []
    for y in range(height):
        row, mask = rows[y], seen[y]
        for x in range(width):
            if row[x] and not mask[x]:
                enclosed += 1
                if coords and len(points) < 200:
                    points.append((x, y))
    print('clear_total=%d enclosed_clear=%d' % (total, enclosed))
    if coords:
        print('enclosed_points=' + ' '.join('%d,%d' % point for point in points))
    if other:
        # Raw pixel comparison, every channel, no clear-colour interpretation:
        # used where two frames must be identical pictures (the explicit
        # skybox=0 key against --no-world-sky), not merely equal in one mask.
        try:
            theirs, their_width, their_height = read_pixels(other)
            ours, our_width, our_height = read_pixels(args[0])
        except (OSError, ValueError) as error:
            print('cannot read frame: %s' % error)
            return 2
        if (their_width, their_height) != (our_width, our_height):
            print('frame_diff=-1 (size %dx%d vs %dx%d)'
                  % (our_width, our_height, their_width, their_height))
            return 0
        differing = sum(1 for i in range(0, len(ours), 3) if ours[i:i + 3] != theirs[i:i + 3])
        print('frame_diff=%d' % differing)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
