# T105: does --lighting basic change ONLY pixels the normal-trace frame also
# changes?
#
# The trace run is '--lighting basic --lighting-trace'. With the probe on, the
# entity/prop pixel shader returns the encoded normal before any lighting
# arithmetic, and the world pixel shader has lightingParams.x = 0 (b2 is bound
# only in the entity pass), so trace-vs-off marks EXACTLY the pixels the entity
# and prop passes drew. If basic is a model-only change, every pixel it moves
# must be inside that set.
#
# This is a containment test, not a tautology: basic's changed set is measured
# on a DIFFERENT pair of frames from the mask's.
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

def changed(r1, r2, y, x, thr):
    o = x * 3
    return max(abs(r2[y][o] - r1[y][o]), abs(r2[y][o+1] - r1[y][o+1]), abs(r2[y][o+2] - r1[y][o+2])) > thr

for thr in (0, 1, 10):
    tb = tt = inside = outside = 0
    outside_px = []
    for y in range(H):
        for x in range(W):
            cb = changed(A, BB, y, x, thr)
            ct = changed(A, TT, y, x, thr)
            if cb: tb += 1
            if ct: tt += 1
            if cb:
                if ct:
                    inside += 1
                else:
                    outside += 1
                    if len(outside_px) < 2000:
                        outside_px.append((x, y))
    print('threshold=%d  basic_changed=%d  trace_changed=%d  inside=%d  OUTSIDE=%d (%.3f%% of basic)'
          % (thr, tb, tt, inside, outside, 100.0 * outside / max(tb, 1)))
    if outside_px:
        xs = [p[0] for p in outside_px]; ys = [p[1] for p in outside_px]
        print('   first outside pixels bbox x=%d..%d y=%d..%d sample=%s'
              % (min(xs), max(xs), min(ys), max(ys), outside_px[:6]))
