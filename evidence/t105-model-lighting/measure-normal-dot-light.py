#!/usr/bin/env python3
"""T105: entity mask from --lighting-trace, then delta-vs-normal correlation."""
import struct, sys, math

def load(path):
    d = open(path, 'rb').read()
    assert d[:2] == b'BM', path
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp == 24
    flip = h < 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    px = []
    for y in range(h):
        row = d[off + y*stride: off + y*stride + w*3]
        px.append([(row[x*3+2], row[x*3+1], row[x*3]) for x in range(w)])
    if not flip:
        px.reverse()
    return w, h, px

def lum(p):
    return 0.299*p[0] + 0.587*p[1] + 0.114*p[2]

off_p, basic_p, trace_p = sys.argv[1], sys.argv[2], sys.argv[3]
L = [float(v) for v in sys.argv[4].split(',')] if len(sys.argv) > 4 else [0.5, -0.866, 0.0]
box = [int(v) for v in sys.argv[5].split(',')] if len(sys.argv) > 5 else None
nrm = math.sqrt(sum(c*c for c in L)); L = [c/nrm for c in L]

w, h, OFF = load(off_p)
_, _, BASE = load(basic_p)
_, _, TRACE = load(trace_p)
x0, y0, x1, y1 = box if box else (0, 0, w, h)

mask = []
for y in range(y0, y1):
    for x in range(x0, x1):
        t = TRACE[y][x]; o = OFF[y][x]
        if max(abs(t[0]-o[0]), abs(t[1]-o[1]), abs(t[2]-o[2])) > 10:
            mask.append((x, y))
print('entity mask pixels=%d (from trace vs off, box=%s)' % (len(mask), box))
if not mask:
    raise SystemExit(0)
# normal from the trace colour
bins = {}
samples = []
for (x, y) in mask:
    t = TRACE[y][x]
    n = [(t[0]/255.0*2-1), (t[1]/255.0*2-1), (t[2]/255.0*2-1)]
    ln = math.sqrt(sum(c*c for c in n))
    if ln < 1e-6: continue
    n = [c/ln for c in n]
    nl = sum(n[i]*L[i] for i in range(3))
    d = lum(BASE[y][x]) - lum(OFF[y][x])
    samples.append((nl, d, lum(OFF[y][x]), lum(BASE[y][x])))
samples.sort()
print('normal.L range %.2f .. %.2f over %d px' % (samples[0][0], samples[-1][0], len(samples)))
for lo in [-1.0, -0.75, -0.5, -0.25, 0.0, 0.25, 0.5, 0.75]:
    hi = lo + 0.25
    sel = [s for s in samples if lo <= s[0] < hi]
    if not sel: continue
    print('  N.L %+.2f..%+.2f  n=%5d  off=%.1f  base=%.1f  delta=%+.1f' % (
        lo, hi, len(sel), sum(s[2] for s in sel)/len(sel), sum(s[3] for s in sel)/len(sel),
        sum(s[1] for s in sel)/len(sel)))
# how many entity pixels are near-black in each frame
for name, P in (('OFF ', OFF), ('BASE', BASE)):
    v = [lum(P[y][x]) for (x, y) in mask]
    v.sort()
    print('%s entity mean=%.1f p10=%.1f p50=%.1f p90=%.1f dark<16=%.3f dark<32=%.3f dark<48=%.3f' % (
        name, sum(v)/len(v), v[len(v)//10], v[len(v)//2], v[len(v)*9//10],
        sum(1 for u in v if u < 16)/len(v), sum(1 for u in v if u < 32)/len(v),
        sum(1 for u in v if u < 48)/len(v)))
