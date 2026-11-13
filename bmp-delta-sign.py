#!/usr/bin/env python3
"""bmp-delta-sign.py A.bmp B.bmp -- which way did the pixels move?

WHY THIS EXISTS
  `bmp-pixel-diff.py` answers "how many bytes differ and by how much", which is
  the right question for "did this semantic reach a pixel". It cannot answer
  "did it add light or remove it", and for a lighting task that is the semantic
  claim: a term that can only multiply the frame by 0.62..1.0 is a directional
  attenuator, not a light, no matter how many pixels it changes.

  So this tool reports the DIRECTION of every differing byte:
    brighter  B > A
    darker    B < A
    same      B == A
    max_up    the largest increase, max_down the largest decrease

  A run that claims "dynamicLighting adds a light" and reports brighter=0 has
  refuted itself in one line. Likewise a specular lobe that reports darker=0 is
  additive, and a bump map that changes both directions is perturbing normals
  rather than scaling the image.

The frames must be 24-bit BMPs of the same size, the format the product's
capture writes. Exit codes: 0 = readable, 2 = missing/mismatched/unreadable.

Usage: bmp-delta-sign.py A.bmp B.bmp
"""
import struct
import sys


def load(path):
    with open(path, 'rb') as handle:
        data = handle.read()
    if data[:2] != b'BM':
        raise ValueError('not a BMP: ' + path)
    pixel_offset = struct.unpack_from('<I', data, 10)[0]
    width = struct.unpack_from('<i', data, 18)[0]
    height = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 24:
        raise ValueError('expected 24bpp, got %d: %s' % (bpp, path))
    row_size = ((width * 3) + 3) & ~3
    rows = []
    for y in range(height):
        start = pixel_offset + y * row_size
        rows.append(data[start:start + width * 3])
    return width, height, rows


def main():
    if len(sys.argv) != 3:
        sys.stderr.write('usage: bmp-delta-sign.py A.bmp B.bmp\n')
        return 2
    try:
        wa, ha, a = load(sys.argv[1])
        wb, hb, b = load(sys.argv[2])
    except (OSError, ValueError, struct.error) as error:
        sys.stderr.write('BMP-SIGN status=ERROR reason=%s\n' % error)
        return 2
    if (wa, ha) != (wb, hb):
        sys.stderr.write('BMP-SIGN status=ERROR reason=geometry %dx%d vs %dx%d\n'
                         % (wa, ha, wb, hb))
        return 2
    brighter = darker = same = 0
    max_up = max_down = 0
    for y in range(ha):
        row_a, row_b = a[y], b[y]
        for index in range(wa * 3):
            left, right = row_a[index], row_b[index]
            if right > left:
                brighter += 1
                if right - left > max_up:
                    max_up = right - left
            elif right < left:
                darker += 1
                if left - right > max_down:
                    max_down = left - right
            else:
                same += 1
    print('BMP-SIGN brighter=%d darker=%d same=%d max_up=%d max_down=%d extent=%dx%dx24 status=OK'
          % (brighter, darker, same, max_up, max_down, wa, ha))
    return 0


if __name__ == '__main__':
    sys.exit(main())
