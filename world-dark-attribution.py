#!/usr/bin/env python
"""T105 C/D readings: the per-mode colour table and the dark-pixel lightmap join.

Both halves read frames and traces produced by the product, so they are pure
post-processing and can be re-run without touching the host lock:

  # C: what each capture mode says at the same points
  python world-dark-attribution.py modes CAPTURE_DIR \
      --points 470,280 597,190 512,178 447,173 899,2 877,100

    CAPTURE_DIR is a directory holding verify/up0-normal.bmp, up0-texel.bmp,
    up0-mutation.bmp, up0-coverage.bmp and grid/entityid.bmp (paths are fixed
    because they name the diagnostic that produced them; see the doc).

  # D: join --world-hole-trace to the dark/albedo split
  python world-dark-attribution.py join NORMAL.bmp ALBEDO.bmp TRACE

    TRACE is the TF2_NATIVE_TRACE file of a run with
    --world-hole-trace "x,y;x,y;...". Prints the dark/lit lightmap ranges and,
    per material sampled both dark and lit, the two ends side by side.
"""
import argparse
import re
import struct
import sys

DRAWN = re.compile(r'drawn=face=(\d+) material=(\S+) tri=(\d+) depth=([0-9.]+)')
MODES = [('normal', 'verify/up0-normal.bmp'), ('texel', 'verify/up0-texel.bmp'),
         ('mutation', 'verify/up0-mutation.bmp'), ('coverage', 'verify/up0-coverage.bmp'),
         ('entityid', 'grid/entityid.bmp'), ('albedo', 'cd/albedo.bmp')]
DEFAULT_POINTS = [(470, 280), (597, 190), (512, 178), (447, 173), (899, 2), (877, 100)]


def load_bmp(path):
    with open(path, 'rb') as handle:
        data = handle.read()
    if data[:2] != b'BM':
        raise ValueError('not a BMP: %s' % path)
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 24:
        raise ValueError('%s is %d bpp, expected 24' % (path, bpp))
    row = (width * 3 + 3) & ~3
    flip = height > 0
    height = abs(height)
    pixels = []
    for y in range(height):
        src = height - 1 - y if flip else y
        base = offset + src * row
        line = data[base:base + width * 3]
        pixels.append([(line[i * 3 + 2], line[i * 3 + 1], line[i * 3]) for i in range(width)])
    return pixels


def luma(colour):
    return 0.2126 * colour[0] + 0.7152 * colour[1] + 0.0722 * colour[2]


def field(line, name):
    for token in line.split():
        if token.startswith(name + '='):
            return token.split('=', 1)[1]
    return None


def rgb(text):
    if not text or ',' not in text:
        return None
    try:
        return tuple(int(part) for part in text.split(',')[:3])
    except ValueError:
        return None


def mode_table(base, points):
    frames = {}
    for name, rel in MODES:
        try:
            frames[name] = load_bmp(base.rstrip('/') + '/' + rel)
        except (OSError, ValueError) as exc:
            print('mode %s unavailable: %s' % (name, exc))
    ordered = [name for name, _ in MODES if name in frames]
    print('points   ' + ' '.join('%-15s' % name for name in ordered))
    for x, y in points:
        cells = ['%-15s' % str(frames[name][y][x]) for name in ordered]
        print('%3d,%-4d ' % (x, y) + ' '.join(cells))


def parse_trace(trace, normal, albedo):
    rows = []
    with open(trace, 'r', errors='replace') as handle:
        for line in handle:
            if 'gpu: world hole-trace point=' not in line:
                continue
            try:
                x, y = (int(value) for value in field(line, 'point').split(','))
            except (AttributeError, ValueError):
                continue
            match = DRAWN.search(line)
            if not match or not (0 <= y < len(normal) and 0 <= x < len(normal[0])):
                continue
            rows.append({'x': x, 'y': y, 'frame': normal[y][x], 'albedo': albedo[y][x],
                         'material': match.group(2), 'tri': match.group(3),
                         'light': rgb(field(line, 'lightrgb')),
                         'factor': float(field(line, 'lightfactor') or '0'),
                         'branch': field(line, 'lightbranch')})
    return rows


def join(normal_path, albedo_path, trace_path):
    normal = load_bmp(normal_path)
    albedo = load_bmp(albedo_path)
    rows = parse_trace(trace_path, normal, albedo)
    dark = [r for r in rows if luma(r['frame']) < 16 and luma(r['albedo']) >= 40]
    lit = [r for r in rows if luma(r['frame']) >= 60]
    print('points=%d drawnFaces=%d dark=%d lit=%d' % (len(rows), len(rows), len(dark), len(lit)))
    for label, group in (('DARK', dark), ('LIT ', lit)):
        if not group:
            continue
        lightluma = sorted(luma(r['light'] or (0, 0, 0)) for r in group)
        factors = sorted(r['factor'] for r in group)
        branches = {}
        for row in group:
            branches[row['branch']] = branches.get(row['branch'], 0) + 1
        print('%s lightLuma min=%.1f med=%.1f max=%.1f factor min=%.4f med=%.4f max=%.4f branch=%s' % (
            label, lightluma[0], lightluma[len(lightluma) // 2], lightluma[-1],
            factors[0], factors[len(factors) // 2], factors[-1],
            ','.join('%s:%d' % (key, value) for key, value in sorted(branches.items()))))
    bymaterial = {}
    for row in rows:
        bymaterial.setdefault(row['material'], []).append(row)
    both = []
    for material, samples in bymaterial.items():
        dark_samples = [s for s in samples if luma(s['frame']) < 16 and luma(s['albedo']) >= 40]
        lit_samples = [s for s in samples if luma(s['frame']) >= 60]
        if dark_samples and lit_samples:
            both.append((material, samples, dark_samples, lit_samples))
    both.sort(key=lambda item: -len(item[1]))
    print()
    print('SAME-MATERIAL materials=%d both-ends=%d' % (len(bymaterial), len(both)))
    for material, samples, dark_samples, lit_samples in both[:12]:
        dark_pick = min(dark_samples, key=lambda s: luma(s['light'] or (0, 0, 0)))
        lit_pick = max(lit_samples, key=lambda s: luma(s['light'] or (0, 0, 0)))
        print('M %s samples=%d dark=%d lit=%d' % (material, len(samples), len(dark_samples), len(lit_samples)))
        for label, pick in (('dark', dark_pick), ('lit ', lit_pick)):
            print('  %s %d,%d frame=%s albedo=%s tri=%s lightrgb=%s lightLuma=%.1f factor=%.4f branch=%s' % (
                label, pick['x'], pick['y'], pick['frame'], pick['albedo'], pick['tri'],
                pick['light'], luma(pick['light'] or (0, 0, 0)), pick['factor'], pick['branch']))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subs = parser.add_subparsers(dest='mode', required=True)
    modes = subs.add_parser('modes')
    modes.add_argument('base')
    modes.add_argument('--points', nargs='*', default=None)
    join_cmd = subs.add_parser('join')
    join_cmd.add_argument('normal')
    join_cmd.add_argument('albedo')
    join_cmd.add_argument('trace')
    args = parser.parse_args()
    if args.mode == 'modes':
        points = DEFAULT_POINTS
        if args.points:
            points = [tuple(int(v) for v in item.split(',')) for item in args.points]
        mode_table(args.base, points)
    else:
        join(args.normal, args.albedo, args.trace)
    return 0


if __name__ == '__main__':
    sys.exit(main())
