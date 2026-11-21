# T105: is --lighting basic touching WORLD geometry, and are crop B's darker
# pixels entity pixels or not?
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

def lum(rows, x, y):
    o = x * 3
    return 0.299 * rows[y][o] + 0.587 * rows[y][o + 1] + 0.114 * rows[y][o + 2]

def region(name, x0, y0, w, h):
    for tag, img in (('basic', BB), ('trace', TT)):
        ch = 0
        up = dn = 0
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                a, b = lum(A, x, y), lum(img, x, y)
                if abs(b - a) > 1.0:
                    ch += 1
                if b > a + 1.0:
                    up += 1
                elif b < a - 1.0:
                    dn += 1
        tot = w * h
        print('%-14s %-5s changed=%6d/%6d (%.2f%%) brighter=%6d darker=%6d' % (name, tag, ch, tot, 100.0 * ch / tot, up, dn))

print('== whole-frame world-only probes (named after t105-clusters.py findings) ==')
region('dome', 40, 40, 200, 140)
region('silo-wall', 1184, 480, 64, 64)
region('left-wall', 100, 300, 60, 60)
region('railings', 900, 200, 160, 120)
region('sky-up', 400, 20, 200, 60)

print()
print('== crop B (1134,540,72,72) deltas split by entity mask ==')
x0, y0, w, h = 1134, 540, 72, 72
ent_ch = ent_up = ent_dn = 0
wor_ch = wor_up = wor_dn = 0
ent_n = 0
worst = []
for y in range(y0, y0 + h):
    for x in range(x0, x0 + w):
        o = x * 3
        tr = max(abs(TT[y][o] - A[y][o]), abs(TT[y][o+1] - A[y][o+1]), abs(TT[y][o+2] - A[y][o+2]))
        isent = tr > 10
        a, b = lum(A, x, y), lum(BB, x, y)
        if isent:
            ent_n += 1
            if abs(b - a) > 1.0: ent_ch += 1
            if b > a + 1.0: ent_up += 1
            elif b < a - 1.0: ent_dn += 1
            worst.append((b - a, x, y, a, b))
        else:
            if abs(b - a) > 1.0: wor_ch += 1
            if b > a + 1.0: wor_up += 1
            elif b < a - 1.0: wor_dn += 1
print('entity-mask px=%d  changed=%d brighter=%d darker=%d' % (ent_n, ent_ch, ent_up, ent_dn))
print('non-mask    px=%d  changed=%d brighter=%d darker=%d' % (w*h-ent_n, wor_ch, wor_up, wor_dn))
worst.sort()
print('most darkened entity px (delta,x,y,off,basic):')
for r in worst[:6]:
    print('   %+.1f  (%d,%d)  off=%.1f basic=%.1f' % r)
print('most brightened:')
for r in worst[-4:]:
    print('   %+.1f  (%d,%d)  off=%.1f basic=%.1f' % r)
