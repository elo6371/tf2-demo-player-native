# T105: does the new model lighting keep the old arm's floor?
#
# The claim the CPU block makes is a single inequality, and this script is the
# only thing that checks it:
#
#     min over all surface directions of the new lighting
#       >= min over all directions of the arm it replaces
#
# The old arm is native/src/native_renderer_shaders.cpp's flat fake:
#     diffuse = saturate(dot(n, normalize(0.35, 0.55, 0.75)))
#     lighting = 0.62 + 0.38 * diffuse
# whose minimum over the sphere is 0.62, reached by every back-facing direction.
# The new arm is the shader's modelLightingRgb:
#     ambient = sum over axes of cube[i].rgb * n[i]^2   (sign picks + or -)
#     L = normalize(sun.xyz)
#     hl = saturate(dot(n, L) * 0.5 + 0.5)
#     lighting = ambient + sun.w * hl^2
#
# It reads the six faces and the sun OUT OF A CAPTURE LOG rather than recomputing
# them, so it checks the values the GPU was actually given, not the source's
# intent. Usage:
#     python t105-lighting-floor.py .scratch/light36-basic/capture.log
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
    sun = g[0:3]
    intensity = g[3]
    sky, ground, side = g[4:7], g[7:10], g[10:13]
    return int(m.group(1)), sun, intensity, side, side, side, side, sky, ground

def sphere(step):
    out = []
    i = 0
    while i <= step:
        theta = math.pi * i / step
        ring = max(1, int(round(step * math.sin(theta))))
        for j in range(ring):
            phi = 2 * math.pi * j / ring
            out.append((math.sin(theta) * math.cos(phi),
                        math.sin(theta) * math.sin(phi),
                        math.cos(theta)))
        i += 1
    return out

def new_light(n, cube, sun, intensity):
    amb = 0.0
    for axis in range(3):
        face = cube[axis] if n[axis] >= 0 else cube[axis + 3]
        amb += face[0] * n[axis] * n[axis]  # luminance only; rgb is checked separately below
    sl = math.sqrt(sum(v * v for v in sun)) or 1.0
    d = sum(n[k] * sun[k] / sl for k in range(3))
    hl = min(max(d * 0.5 + 0.5, 0.0), 1.0)
    return amb + intensity * hl * hl

def new_rgb(n, cube, sun, intensity):
    amb = [0.0, 0.0, 0.0]
    for axis in range(3):
        face = cube[axis] if n[axis] >= 0 else cube[axis + 3]
        for c in range(3):
            amb[c] += face[c] * n[axis] * n[axis]
    sl = math.sqrt(sum(v * v for v in sun)) or 1.0
    d = sum(n[k] * sun[k] / sl for k in range(3))
    hl = min(max(d * 0.5 + 0.5, 0.0), 1.0)
    return [amb[c] + intensity * hl * hl for c in range(3)]

OLD = (0.35, 0.55, 0.75)
def old_light(n):
    sl = math.sqrt(sum(v * v for v in OLD))
    d = max(sum(n[k] * OLD[k] / sl for k in range(3)), 0.0)
    return 0.62 + 0.38 * d

arm, sun, intensity, px, nx, py, ny, sky, ground = parse(sys.argv[1])
cube = [px, nx, py, ny, sky, ground]
dirs = sphere(int(sys.argv[2]) if len(sys.argv) > 2 else 96)
nl = [new_light(n, cube, sun, intensity) for n in dirs]
ol = [old_light(n) for n in dirs]
worst = min(range(len(dirs)), key=lambda i: nl[i])
best = max(range(len(dirs)), key=lambda i: nl[i])
print('arm=%d sun=(%.3f,%.3f,%.3f) intensity=%.3f' % (arm, sun[0], sun[1], sun[2], intensity))
print('faces side=(%.3f,%.3f,%.3f) sky=(%.3f,%.3f,%.3f) ground=(%.3f,%.3f,%.3f)'
      % (px[0], px[1], px[2], sky[0], sky[1], sky[2], ground[0], ground[1], ground[2]))
print('directions sampled=%d' % len(dirs))
print('OLD arm  min=%.4f max=%.4f  (floor 0.62)' % (min(ol), max(ol)))
print('NEW arm  min=%.4f max=%.4f' % (min(nl), max(nl)))
print('NEW floor at n=(%.3f,%.3f,%.3f); NEW peak at n=(%.3f,%.3f,%.3f)'
      % (dirs[worst] + dirs[best]))
rgb_worst = new_rgb(dirs[worst], cube, sun, intensity)
print('NEW floor rgb=(%.3f,%.3f,%.3f)' % tuple(rgb_worst))
ok_floor = min(nl) >= 0.62 - 1e-6
ok_brighter = max(nl) > max(ol) - 1e-6
ok_no_black = min(rgb_worst) > 0.30
print('FLOOR-PRESERVED=%s BRIGHTER-PEAK=%s NO-BLACK-CHANNEL=%s'
      % ('PASS' if ok_floor else 'FAIL', 'PASS' if ok_brighter else 'FAIL',
         'PASS' if ok_no_black else 'FAIL'))
sys.exit(0 if (ok_floor and ok_brighter and ok_no_black) else 1)

# ---------------------------------------------------------------------------
# T105: is there a hard terminator?
#
# The brief asks for "side lighting is continuous (no hard terminator)". That is
# a claim about the FUNCTION, not about a picture, and it is measurable: sweep a
# great circle of surface directions through the terminator and look at how the
# light changes from one step to the next.
#
#   old arm  lighting = 0.62 + 0.38 * saturate(dot(n, L))
#            flat (slope 0) over the WHOLE back hemisphere -- 50% of the sphere
#            is one constant value, so the boundary where the slope jumps from
#            0.38 to 0 IS the terminator.
#   new arm  lighting = ambient(n) + sun.w * saturate(dot(n,L)*0.5+0.5)^2
#            flat only where saturate clips, which is the single back pole
#            (N.L = -1). Half-Lambert squared never reaches zero slope anywhere
#            else, so there is no boundary to see.
#
# The numbers printed are: the arc fraction on which the light does not change at
# all, and the largest slope jump between adjacent 1-degree steps (the step that
# a terminator would produce).
def sweep(light_fn, a, b, steps):
    vals = []
    for i in range(steps + 1):
        t = math.pi * i / steps
        n = (a[0] * math.cos(t) + b[0] * math.sin(t),
             a[1] * math.cos(t) + b[1] * math.sin(t),
             a[2] * math.cos(t) + b[2] * math.sin(t))
        vals.append(light_fn(n))
    return vals

sl = math.sqrt(sum(v * v for v in sun)) or 1.0
Ldir = [sun[k] / sl for k in range(3)]
up = [0.0, 0.0, 1.0] if abs(Ldir[2]) < 0.9 else [1.0, 0.0, 0.0]
e1 = [up[1] * Ldir[2] - up[2] * Ldir[1], up[2] * Ldir[0] - up[0] * Ldir[2], up[0] * Ldir[1] - up[1] * Ldir[0]]
m = math.sqrt(sum(v * v for v in e1))
e1 = [v / m for v in e1]
steps = 360
for name, fn in (('OLD', old_light), ('NEW', lambda n: new_light(n, cube, sun, intensity))):
    v = sweep(fn, Ldir, e1, steps)
    d = [v[i + 1] - v[i] for i in range(len(v) - 1)]
    flat = sum(1 for x in d if abs(x) < 1e-4)
    jumps = [abs(d[i + 1] - d[i]) for i in range(len(d) - 1)]
    print('%s arm: flat arc fraction=%.3f max slope step=%.5f range=[%.3f,%.3f]'
          % (name, flat / float(len(d)), max(jumps), min(v), max(v)))
newv = sweep(lambda n: new_light(n, cube, sun, intensity), Ldir, e1, steps)
newd = [newv[i + 1] - newv[i] for i in range(len(newv) - 1)]
newflat = sum(1 for x in newd if abs(x) < 1e-4) / float(len(newd))
print('TERMINATOR=%s (flat arc fraction %.3f)' % ('PASS' if newflat < 0.05 else 'FAIL', newflat))
