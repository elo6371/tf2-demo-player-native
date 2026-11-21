# T105: is the clipping a small hot spot or spread over the frame?
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
W, H = O[0], O[1]; A, BB, TT = O[2], B[2], T[2]
any_c = 0; all_c = 0; cells = {}
pts = []
for y in range(H):
    for x in range(W):
        o = x * 3
        if max(abs(TT[y][o]-A[y][o]), abs(TT[y][o+1]-A[y][o+1]), abs(TT[y][o+2]-A[y][o+2])) <= 10:
            continue
        r, g, b = BB[y][o], BB[y][o+1], BB[y][o+2]
        hit = (r >= 254) or (g >= 254) or (b >= 254)
        if hit:
            any_c += 1
            pts.append((x, y))
            if r >= 254 and g >= 254 and b >= 254:
                all_c += 1
            cells[(x // 64, y // 64)] = cells.get((x // 64, y // 64), 0) + 1
print('entity-mask pixels clipped in any channel = %d, fully white = %d' % (any_c, all_c))
if pts:
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    print('clipped bbox x=%d..%d y=%d..%d' % (min(xs), max(xs), min(ys), max(ys)))
top = sorted(cells.items(), key=lambda kv: -kv[1])[:6]
for (cx, cy), n in top:
    print('  cell x=%4d y=%4d count=%4d' % (cx*64, cy*64, n))
