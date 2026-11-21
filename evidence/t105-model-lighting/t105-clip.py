# T105: does --lighting basic blow the highlights, and are models now brighter
# than the world they stand in?
import struct, sys

def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w = struct.unpack_from('<i', d, 18)[0]
    h = struct.unpack_from('<i', d, 22)[0]
    bpp = struct.unpack_from('<H', d, 28)[0]
    bottom = h > 0
    h = abs(h)
    stride = ((w * bpp // 8) + 3) & ~3
    step = bpp // 8
    rows = []
    for y in range(h):
        sy = (h - 1 - y) if bottom else y
        base = off + sy * stride
        row = bytearray(w * 3)
        for x in range(w):
            p = base + x * step
            row[x * 3] = d[p + 2]
            row[x * 3 + 1] = d[p + 1]
            row[x * 3 + 2] = d[p + 0]
        rows.append(bytes(row))
    return w, h, rows

O = read_bmp(sys.argv[1]); B = read_bmp(sys.argv[2]); T = read_bmp(sys.argv[3])
W, H = O[0], O[1]
A, BB, TT = O[2], B[2], T[2]

def stat(rows, name, mask=None):
    n = 0
    s = 0.0
    clip = 0
    mx = 0.0
    for y in range(H):
        r = rows[y]
        for x in range(W):
            if mask is not None and not mask[y][x]:
                continue
            o = x * 3
            l = 0.299 * r[o] + 0.587 * r[o + 1] + 0.114 * r[o + 2]
            n += 1
            s += l
            if l > mx:
                mx = l
            if r[o] >= 254 or r[o + 1] >= 254 or r[o + 2] >= 254:
                clip += 1
    print('%-28s n=%7d mean=%6.2f max=%6.1f clipped=%6d (%.3f%%)' % (name, n, s / max(n, 1), mx, clip, 100.0 * clip / max(n, 1)))

mask = [[False] * W for _ in range(H)]
for y in range(H):
    r, t = A[y], TT[y]
    for x in range(W):
        o = x * 3
        if max(abs(t[o] - r[o]), abs(t[o + 1] - r[o + 1]), abs(t[o + 2] - r[o + 2])) > 10:
            mask[y][x] = True

notmask = [[not mask[y][x] for x in range(W)] for y in range(H)]
print('== entity mask population (trace-vs-off > 10 per channel) ==')
stat(A, 'OFF entity', mask)
stat(BB, 'BASIC entity', mask)
print('== everything that is NOT an entity ==')
stat(A, 'OFF world', notmask)
stat(BB, 'BASIC world', notmask)
print('== whole frame ==')
stat(A, 'OFF whole')
stat(BB, 'BASIC whole')
