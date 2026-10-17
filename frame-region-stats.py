#!/usr/bin/env python3
"""frame-region-stats.py -- T10: measurable presence of objects in frame regions.

For each requested rectangle, reports over its pixels:
  pixels, mean luminance, luminance stddev, the fraction of pixels within 24
  luminance levels of the frame's corner (background) colour, and the fraction
  of near-black (<16) and near-white (>239) pixels.

The background colour is measured from the frame's own top-left 8x8 block, so
the tool never hardcodes a clear colour. "The object is visible in the region"
is then decidable from numbers: a region with an object has pixels far from the
background and a luminance spread; an empty region collapses onto the
background with near-zero spread.
"""
import struct
import sys

def load_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:2] == b'BM', 'not a bmp'
    pixelOffset = struct.unpack_from('<I', data, 10)[0]
    width = struct.unpack_from('<i', data, 18)[0]
    height = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    assert bpp == 24, bpp
    rowSize = ((width * 3) + 3) & ~3
    pixels = []
    for y in range(height):
        row = data[pixelOffset + y * rowSize: pixelOffset + y * rowSize + width * 3]
        rowPixels = []
        for x in range(width):
            b, g, r = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            rowPixels.append((r, g, b))
        pixels.append(rowPixels)
    return width, height, pixels

def lum(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]

def main():
    path = sys.argv[1]
    rects = []
    for arg in sys.argv[2:]:
        x, y, w, h = (int(v) for v in arg.split(','))
        rects.append((x, y, w, h))
    width, height, pixels = load_bmp(path)
    # background from the top-left 8x8 block
    bg = [0.0, 0.0, 0.0]
    for y in range(8):
        for x in range(8):
            for c in range(3):
                bg[c] += pixels[y][x][c]
    bg = [c / 64.0 for c in bg]
    bgLum = lum(bg)
    print('frame path=%s size=%dx%d background=(%.0f,%.0f,%.0f) backgroundLum=%.1f'
          % (path, width, height, bg[0], bg[1], bg[2], bgLum))
    for (x, y, w, h) in rects:
        x = max(0, x); y = max(0, y)
        w = min(w, width - x); h = min(h, height - y)
        n = w * h
        if n <= 0:
            print('region x=%d,y=%d,w=%d,h=%d empty' % (x, y, w, h))
            continue
        s = 0.0; s2 = 0.0; nearBg = 0; black = 0; white = 0; grad = 0.0; grey = 0
        for yy in range(y, y + h):
            row = pixels[yy]
            for xx in range(x, x + w):
                p = row[xx]
                l = lum(p)
                s += l; s2 += l * l
                if abs(p[0] - bg[0]) + abs(p[1] - bg[1]) + abs(p[2] - bg[2]) < 24:
                    nearBg += 1
                if l < 16: black += 1
                if l > 239: white += 1
                # neutral grey in the rifle's flat-shaded band: a colour-uniform
                # object has near-equal channels; textured world pixels rarely
                # sit neutral AND bright at once.
                if 140 <= l <= 225 and max(p) - min(p) < 14:
                    grey += 1
                if xx + 1 < width:
                    grad += abs(l - lum(row[xx + 1]))
                if yy + 1 < height:
                    grad += abs(l - lum(pixels[yy + 1][xx]))
        mean = s / n
        var = max(0.0, s2 / n - mean * mean)
        print('region x=%d,y=%d,w=%d,h=%d pixels=%d meanLum=%.1f stdLum=%.2f '
              'nearBackground=%.4f nearBlack=%.4f nearWhite=%.4f meanGradient=%.2f neutralGrey=%d'
              % (x, y, w, h, n, mean, var ** 0.5, nearBg / n, black / n, white / n, grad / n, grey))

if __name__ == '__main__':
    main()
