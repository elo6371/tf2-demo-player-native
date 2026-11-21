# T105: pick 1:1 crops by measurement, not by eye.
# A crop is only evidence if the pixels inside it are ENTITY pixels (mask from
# trace-vs-off) and if the body inside it spans the terminator (spread of N.L).
import struct, sys

def read_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    off = struct.unpack_from('<I', data, 10)[0]
    w = struct.unpack_from('<i', data, 18)[0]
    h = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    bottom_up = h > 0
    h = abs(h)
    stride = ((w * bpp // 8) + 3) & ~3
    px = []
    step = bpp // 8
    for y in range(h):
        sy = (h - 1 - y) if bottom_up else y
        base = off + sy * stride
        row = bytearray(w * 3)
        for x in range(w):
            p = base + x * step
            row[x * 3 + 0] = data[p + 2]
            row[x * 3 + 1] = data[p + 1]
            row[x * 3 + 2] = data[p + 0]
        px.append(bytes(row))
    return w, h, px

def lum(px, w, x, y):
    o = x * 3
    return 0.299 * px[y][o] + 0.587 * px[y][o + 1] + 0.114 * px[y][o + 2]

off_w, off_h, A = read_bmp(sys.argv[1])
W, H, B = read_bmp(sys.argv[2])
_, _, T = read_bmp(sys.argv[3])
win = int(sys.argv[4]) if len(sys.argv) > 4 else 72
stride_px = max(win // 4, 8)
# optional x window to search in: keeps the two crops on two different bodies
xlo = int(sys.argv[5]) if len(sys.argv) > 5 else 0
xhi = int(sys.argv[6]) if len(sys.argv) > 6 else 100000

mask = [[False] * W for _ in range(H)]
nl = [[0.0] * W for _ in range(H)]
for y in range(H):
    for x in range(W):
        o = x * 3
        d = max(abs(B[y][o] - A[y][o]), abs(B[y][o + 1] - A[y][o + 1]), abs(B[y][o + 2] - A[y][o + 2]))
        if d > 10:
            mask[y][x] = True
            n = [T[y][o] / 255.0 * 2 - 1, T[y][o + 1] / 255.0 * 2 - 1, T[y][o + 2] / 255.0 * 2 - 1]
            L = 0.5 * 0.0  # placeholder
            nl[y][x] = n

# sun L = map sun (0.5,-0.866,0)
LX, LY, LZ = 0.5, -0.866, 0.0
def dotn(x, y):
    o = x * 3
    n = [T[y][o] / 255.0 * 2 - 1, T[y][o + 1] / 255.0 * 2 - 1, T[y][o + 2] / 255.0 * 2 - 1]
    m = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) ** 0.5 or 1.0
    return (n[0] * LX + n[1] * LY + n[2] * LZ) / m

rows = []
for y0 in range(0, H - win + 1, stride_px):
    for x0 in range(xlo, W - win + 1, stride_px):
        if x0 > xhi:
            continue
        cnt = 0
        vals = []
        for y in range(y0, y0 + win):
            mrow = mask[y]
            for x in range(x0, x0 + win):
                if mrow[x]:
                    cnt += 1
                    vals.append(dotn(x, y))
        if cnt < win * win * 0.35:
            continue
        vals.sort()
        n = len(vals)
        spread = vals[int(n * 0.9)] - vals[int(n * 0.1)]
        lo = sum(1 for v in vals if v < -0.05)
        hi = sum(1 for v in vals if v > 0.45)
        frac = cnt / float(win * win)
        rows.append((spread * frac, x0, y0, cnt, frac, spread, vals[0], vals[-1], lo, hi))

rows.sort(reverse=True)
print('win=%d stride=%d candidates=%d' % (win, stride_px, len(rows)))
print('%4s %4s %6s %5s %6s %6s %6s %5s %5s' % ('x', 'y', 'maskpx', 'fill', 'spread', 'nmin', 'nmax', 'back', 'sun'))
for r in rows[:14]:
    print('%4d %4d %6d %5.2f %6.3f %6.3f %6.3f %5d %5d' % (r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9]))
