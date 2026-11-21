# T105: is there a hard terminator?
#
# The brief asks for "side lighting is continuous (no hard terminator)". That is
# a claim about the FUNCTION, not about a picture, so this sweeps a great circle
# of surface directions through the terminator and measures how the light changes
# between adjacent 1-degree steps.
#
#   old arm  lighting = 0.62 + 0.38 * saturate(dot(n, L))
#            flat (slope 0) over the WHOLE back hemisphere -- half the sphere is
#            one constant value, and the boundary where the slope jumps from
#            0.38 to 0 IS the terminator.
#   new arm  lighting = ambient(n) + sun.w * saturate(dot(n,L)*0.5 + 0.5)^2
#            flat only where saturate clips, i.e. at the single back pole.
#
# Reported: the fraction of the arc on which the light does not change at all,
# and the largest jump in slope between neighbouring steps (a terminator shows up
# there as a spike).
import math, re, sys

def parse(path):
    txt = open(path, 'r', errors='replace').read()
    m = re.search(r'capture: model-lighting arm=(\d) trace=(\d) sunPresent=(\d) '
                  r'sun=\(([-\d.]+),([-\d.]+),([-\d.]+)\) intensity=([-\d.]+) '
                  r'sky=\(([-\d.]+),([-\d.]+),([-\d.]+)\) '
                  r'ground=\(([-\d.]+),([-\d.]+),([-\d.]+)\) '
                  r'side=\(([-\d.]+),([-\d.]+),([-\d.]+)\)', txt)
    if not m:
        sys.exit('no model-lighting line in ' + path)
    g = [float(v) for v in m.groups()[3:]]
    sun, intensity = g[0:3], g[3]
    sky, ground, side = g[4:7], g[7:10], g[10:13]
    cube = [side, side, side, side, sky, ground]
    return int(m.group(1)), sun, intensity, cube

def new_light(n, cube, sun, intensity):
    amb = 0.0
    for axis in range(3):
        face = cube[axis] if n[axis] >= 0 else cube[axis + 3]
        amb += face[0] * n[axis] * n[axis]
    sl = math.sqrt(sum(v * v for v in sun)) or 1.0
    d = sum(n[k] * sun[k] / sl for k in range(3))
    hl = min(max(d * 0.5 + 0.5, 0.0), 1.0)
    return amb + intensity * hl * hl

def old_light(n, sun, intensity, cube):
    o = (0.35, 0.55, 0.75)
    sl = math.sqrt(sum(v * v for v in o))
    d = max(sum(n[k] * o[k] / sl for k in range(3)), 0.0)
    return 0.62 + 0.38 * d

def sweep(fn, a, b, steps):
    vals = []
    for i in range(steps + 1):
        t = math.pi * i / steps
        n = (a[0] * math.cos(t) + b[0] * math.sin(t),
             a[1] * math.cos(t) + b[1] * math.sin(t),
             a[2] * math.cos(t) + b[2] * math.sin(t))
        vals.append(fn(n))
    return vals

arm, sun, intensity, cube = parse(sys.argv[1])

def plane(direction):
    # A great semicircle from the pole of *direction* to its antipode. Each arm
    # is swept about ITS OWN light direction: the old arm's hardcoded fake is
    # (0.35,0.55,0.75) and the map's sun is nowhere near it, so sweeping both
    # about one of them would measure the angle between two unrelated lights
    # instead of each arm's own terminator. That is the only fair comparison,
    # and it makes the old arm's flat-arc come out at 0.5 exactly -- which is
    # what a half-sphere-constant fake must produce, and is the check that the
    # sweep is centred on the right pole.
    m = math.sqrt(sum(v * v for v in direction)) or 1.0
    pole = [v / m for v in direction]
    up = [0.0, 0.0, 1.0] if abs(pole[2]) < 0.9 else [1.0, 0.0, 0.0]
    e = [up[1] * pole[2] - up[2] * pole[1], up[2] * pole[0] - up[0] * pole[2],
         up[0] * pole[1] - up[1] * pole[0]]
    m2 = math.sqrt(sum(v * v for v in e))
    return pole, [v / m2 for v in e]

steps = 360
res = {}
for name, fn in (('OLD', lambda n: old_light(n, sun, intensity, cube)),
                 ('NEW', lambda n: new_light(n, cube, sun, intensity))):
    a, b = plane((0.35, 0.55, 0.75) if name == 'OLD' else sun)
    v = sweep(fn, a, b, steps)
    d = [v[i + 1] - v[i] for i in range(len(v) - 1)]
    flat = sum(1 for x in d if abs(x) < 1e-4)
    jumps = [abs(d[i + 1] - d[i]) for i in range(len(d) - 1)]
    res[name] = (flat / float(len(d)), max(jumps), min(v), max(v))
    print('%s arm: flat-arc=%.3f max-slope-step=%.5f range=[%.3f,%.3f]'
          % (name, res[name][0], res[name][1], res[name][2], res[name][3]))
print('NOTE the arc is swept from the sun pole to its antipode, so it crosses the'
      ' terminator once; 0.500 flat means half the arc is one constant value.')
# The verdict compares the two arms and nothing else. An absolute threshold would
# be a number chosen to fit the answer: "flat over X of the arc" is only bad
# RELATIVE to the arm being replaced, and that arm is flat over exactly half the
# sphere by construction. The second metric is the sharper one -- a hard
# terminator is a discontinuity, and a discontinuity shows up as a spike in the
# step-to-step slope, which the flat-arc count can miss entirely.
ok_flat = res['NEW'][0] <= res['OLD'][0]
ok_slope = res['NEW'][1] <= res['OLD'][1]
print('flat-arc   new=%.3f old=%.3f -> %s' % (res['NEW'][0], res['OLD'][0], 'PASS' if ok_flat else 'FAIL'))
print('slope-jump new=%.5f old=%.5f -> %s (%.0fx smaller)'
      % (res['NEW'][1], res['OLD'][1], 'PASS' if ok_slope else 'FAIL',
         res['OLD'][1] / max(res['NEW'][1], 1e-12)))
print('TERMINATOR=%s' % ('PASS' if (ok_flat and ok_slope) else 'FAIL'))
sys.exit(0 if (ok_flat and ok_slope) else 1)
