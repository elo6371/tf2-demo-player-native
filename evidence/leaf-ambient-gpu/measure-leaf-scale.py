# T105 / task-37 Half B: which scale factor turns a baked leaf cube into a
# lighting value the model-lighting arm can actually draw with?
#
# The leaf lump stores LINEAR radiance around 1e-3 (evidence/leaf-ambient/
# real-cp_process_f12.txt: face luminance p50 = 0.00048). The global cube that
# task-36 built from worldLights_ on the SAME map is two to three orders of
# magnitude brighter (evidence/t105-model-lighting/trace-basic.txt). Feeding a
# leaf cube into native_renderer_shaders.cpp's modelLightingRgb unscaled makes
# every model black, so the shader multiplies each face by one constant on the
# way into the slot table (native/include/leaf_ambient_table.h::
# kLeafAmbientScale). This script is the measurement that chooses it.
#
# It reproduces the shader's arithmetic exactly:
#   amb      = sum over axes of cube[i].rgb * n[i]^2      (sign picks + or -)
#   hl       = saturate(dot(n, normalize(sun.xyz)) * 0.5 + 0.5)
#   lighting = amb + sun.w * hl^2                         (per channel)
#   the arm's own aggregate = max channel  (native_renderer_shaders.cpp:621)
#
# and reads every input out of a COMMITTED file, so every number printed here
# is traceable and re-derivable:
#   global cube  evidence/t105-model-lighting/trace-basic.txt
#   demo cubes   evidence/leaf-ambient/demo-cp_process_f12.txt
#   census cubes evidence/leaf-ambient/real-cp_process_f12.txt
#
# usage: python measure-leaf-scale.py <repo-root>
import json, math, re, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else '.'
REC601 = (0.299, 0.587, 0.114)

def luminance(rgb):
    return sum(REC601[i] * rgb[i] for i in range(3))

def parse_global(path):
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
    # The log prints faces [4] (+Z sky), [5] (-Z ground) and [0] (+X side). The
    # other three are not printed, and task-36's own floor check
    # (evidence/t105-model-lighting/check-lighting-floor.py:82) reconstructs
    # them as the printed side face -- the same assumption is made here so the
    # two measurements stay comparable.
    return int(m.group(1)), sun, intensity, [side, side, side, side, sky, ground]

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

def lighting_rgb(n, cube, sun, intensity):
    amb = [0.0, 0.0, 0.0]
    for axis in range(3):
        face = cube[axis] if n[axis] >= 0 else cube[axis + 3]
        for c in range(3):
            amb[c] += face[c] * n[axis] * n[axis]
    sl = math.sqrt(sum(v * v for v in sun)) or 1.0
    d = sum(n[k] * sun[k] / sl for k in range(3))
    hl = min(max(d * 0.5 + 0.5, 0.0), 1.0)
    return [amb[c] + intensity * hl * hl for c in range(3)]

def lighting(n, cube, sun, intensity):
    # the shader's own aggregate: lighting = max(lightingRgb.r, max(.g, .b))
    return max(lighting_rgb(n, cube, sun, intensity))

def parse_cubes(path, key='cube'):
    cubes = []
    for raw in open(path, 'r', errors='replace'):
        raw = raw.strip()
        if not raw.startswith('{'):
            continue
        try:
            row = json.loads(raw)
        except ValueError:
            continue
        text = row.get(key, '')
        if not text:
            continue
        faces = []
        ok = True
        for face in text.split('|'):
            parts = face.split(',')
            if len(parts) != 3:
                ok = False
                break
            try:
                faces.append([float(v) for v in parts])
            except ValueError:
                ok = False
                break
        if ok and len(faces) == 6:
            cubes.append(faces)
    return cubes

def pct(values, p):
    if not values:
        return float('nan')
    ordered = sorted(values)
    idx = int(round((len(ordered) - 1) * p / 100.0))
    return ordered[max(0, min(len(ordered) - 1, idx))]

def describe(label, values):
    print('%s n=%d mean=%.6f min=%.6f max=%.6f p0=%.6f p5=%.6f p25=%.6f p50=%.6f p75=%.6f p95=%.6f p100=%.6f'
          % (label, len(values), sum(values) / len(values), min(values), max(values),
             pct(values, 0), pct(values, 5), pct(values, 25), pct(values, 50),
             pct(values, 75), pct(values, 95), pct(values, 100)))

def sweep_cubes(cubes, k, dirs, sun, intensity):
    out = []
    for cube in cubes:
        scaled = [[c * k for c in face] for face in cube]
        for n in dirs:
            out.append(lighting(n, scaled, sun, intensity))
    return out

arm, sun, intensity, global_cube = parse_global(ROOT + '/evidence/t105-model-lighting/trace-basic.txt')
print('global cube source: evidence/t105-model-lighting/trace-basic.txt')
print('global arm=%d sun=(%.3f,%.3f,%.3f) intensity=%.3f' % (arm, sun[0], sun[1], sun[2], intensity))
print('global +X/-X/+Y/-Y=side=(%.3f,%.3f,%.3f) +Z=sky=(%.3f,%.3f,%.3f) -Z=ground=(%.3f,%.3f,%.3f)'
      % tuple(global_cube[0] + global_cube[4] + global_cube[5]))
print('global face luminance side=%.6f sky=%.6f ground=%.6f'
      % (luminance(global_cube[0]), luminance(global_cube[4]), luminance(global_cube[5])))

dirs = sphere(64)
global_values = [lighting(n, global_cube, sun, intensity) for n in dirs]
describe('GLOBAL-ARM', global_values)
print('GLOBAL-ARM frac>1.0=%.4f frac<0.25=%.4f'
      % (sum(1 for v in global_values if v > 1.0) / len(global_values),
         sum(1 for v in global_values if v < 0.25) / len(global_values)))

demo = parse_cubes(ROOT + '/evidence/leaf-ambient/demo-cp_process_f12.txt')
census = parse_cubes(ROOT + '/evidence/leaf-ambient/real-cp_process_f12.txt')
print('demo cubes=%d (evidence/leaf-ambient/demo-cp_process_f12.txt)' % len(demo))
print('census cubes=%d (evidence/leaf-ambient/real-cp_process_f12.txt)' % len(census))

face_lum_census = [luminance(face) for cube in census for face in cube]
cube_lum_census = [sum(luminance(face) for face in cube) / 6.0 for cube in census]
face_lum_demo = [luminance(face) for cube in demo for face in cube]
cube_lum_demo = [sum(luminance(face) for face in cube) / 6.0 for cube in demo]
describe('CENSUS-FACE-LUM', face_lum_census)
describe('CENSUS-CUBE-MEAN-LUM', cube_lum_census)
describe('DEMO-FACE-LUM', face_lum_demo)
describe('DEMO-CUBE-MEAN-LUM', cube_lum_demo)

global_mean = sum(global_values) / len(global_values)
print('RATIO global-mean-face-lum/census-median-face-lum=%.1f'
      % (luminance(global_cube[4]) / pct(face_lum_census, 50)))
print('RATIO global-mean-face-lum/demo-median-cube-lum=%.1f'
      % (luminance(global_cube[4]) / pct(cube_lum_demo, 50)))

# The mapping this script argues for: choose k so the leaf arm's MEAN lighting
# over the same direction sphere equals the global arm's mean. That keeps the
# average model brightness a reader of the basic pin would recognise while
# letting each leaf keep its own contrast -- which is the entire point of
# per-leaf ambient. Bisection on k, mean is monotonic in k.
lo, hi = 1.0, 100000.0
for _ in range(80):
    mid = 0.5 * (lo + hi)
    mean_mid = sum(sweep_cubes(demo, mid, dirs, sun, intensity)) / (len(demo) * len(dirs))
    if mean_mid < global_mean:
        lo = mid
    else:
        hi = mid
chosen_k = 0.5 * (lo + hi)
print('MEAN-MATCHED k=%.3f (leaf mean lighting == global mean lighting %.6f)' % (chosen_k, global_mean))

print('SWEEP k mean dark(<0.25) clip(>1.0) cubefloor-median cubefloor-min')
for k in (100, 160, 250, 400, 462, 600, 800, 952, 1100, 1632, 2400, int(round(chosen_k))):
    values = sweep_cubes(demo, k, dirs, sun, intensity)
    floors = []
    for cube in demo:
        scaled = [[c * k for c in face] for face in cube]
        floors.append(min(lighting(n, scaled, sun, intensity) for n in dirs))
    print('%5d %.6f %.4f %.4f %.6f %.6f'
          % (k, sum(values) / len(values),
             sum(1 for v in values if v < 0.25) / len(values),
             sum(1 for v in values if v > 1.0) / len(values),
             pct(floors, 50), min(floors)))

# What this costs: the global arm's minimum over the sphere (the number task-36
# had to keep above 0.62) versus the leaf arm's, on the same directions.
old_old = []
OLD = (0.35, 0.55, 0.75)
for n in dirs:
    sl = math.sqrt(sum(v * v for v in OLD))
    d = max(sum(n[k] * OLD[k] / sl for k in range(3)), 0.0)
    old_old.append(0.62 + 0.38 * d)
print('FLAT-OLD-ARM min=%.6f max=%.6f (task-36 floor 0.62)' % (min(old_old), max(old_old)))
print('GLOBAL-ARM min=%.6f max=%.6f' % (min(global_values), max(global_values)))

leaf_at_k = sweep_cubes(demo, chosen_k, dirs, sun, intensity)
describe('LEAF-ARM-k%.0f' % chosen_k, leaf_at_k)

# HALF AND DOUBLE: the two neighbouring factors a reviewer tries first, because
# the only honest claim a single constant can make is "this one, and here is
# what happens if it is off by a factor of two". Both tables use the SAME
# arithmetic as the sweep above; the second runs over the whole census (all
# 3537 baked leaves on the map) as well as the 76 demo cubes, so the answer is
# not an artifact of the tick-600 sample. The census arm rides a decimated
# direction set to stay linear in the map's leaf count; the stride is printed.
print('HALF-DOUBLE-DEMO k mean dark(<0.25) clip(>1.0) cubefloor-median cubefloor-min')
for k in (chosen_k / 2.0, chosen_k, chosen_k * 2.0):
    values = sweep_cubes(demo, k, dirs, sun, intensity)
    floors = []
    for cube in demo:
        scaled = [[c * k for c in face] for face in cube]
        floors.append(min(lighting(n, scaled, sun, intensity) for n in dirs))
    print('%.1f %.6f %.4f %.4f %.6f %.6f'
          % (k, sum(values) / len(values),
             sum(1 for v in values if v < 0.25) / len(values),
             sum(1 for v in values if v > 1.0) / len(values),
             pct(floors, 50), min(floors)))

dirs_sparse = dirs[::20]
print('HALF-DOUBLE-CENSUS k dirs=%d cubemean-p50 cubemean-p95 cubemean-max dark(<0.25) clip(>1.0)'
      % len(dirs_sparse))
for k in (chosen_k / 2.0, chosen_k, chosen_k * 2.0):
    cube_means = []
    for cube in census:
        scaled = [[c * k for c in face] for face in cube]
        cube_means.append(sum(lighting(n, scaled, sun, intensity) for n in dirs_sparse) / len(dirs_sparse))
    flat = [v for v in cube_means]
    print('%.1f %d %.6f %.6f %.6f %.4f %.4f'
          % (k, len(dirs_sparse), pct(flat, 50), pct(flat, 95), max(flat),
             sum(1 for v in flat if v < 0.25) / len(flat),
             sum(1 for v in flat if v > 1.0) / len(flat)))

print('VERDICT MEAN-MATCHED k=%.1f leaves the basic arm' % chosen_k)
print('VERDICT the 0.62 floor is NOT inherited: the leaf arm min is %.6f against the global arm''s %.6f'
      % (min(leaf_at_k), min(global_values)))
