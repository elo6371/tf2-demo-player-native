#!/usr/bin/env python3
"""face-trace-verify.py -- T21: compare a captured frame against the product's
own per-face material trace.

WHY THIS EXISTS
---------------
A material-fetch counter says "85 materials got a tile". It cannot say that the
tile under a wall's pixels is the wall's texture, that the uv which reached the
shader is the face's own uv, or that the texel the shader fetched is the texel
the VTF holds. Those are the questions T21 asks, and the only instrument that
answers them is a pixel.

The product emits one `gpu: world face-trace` line per sampled screen point on
the captured frame. Each line carries the chain it walked (material -> VMT store
-> VTF store -> tile rectangle -> atlas uv -> the texel the CPU sampled through
that uv) and the value the pixel shader should write there:

  albedo     the material path's own output, before lighting (--world-albedo)
  expected   albedo * encoded lightmap * lighting (the textured frame)

This tool reads the same screen points from the bitmap and compares. A tile
that points at another material, a uv that misses its tile, a downscaled
texture whose uv was normalised by the wrong size, and a shader that dropped a
lightmap term all move the number this tool reports.

Usage:
  face-trace-verify.py --trace T --frame F.bmp --mode albedo|textured
                       [--tol N] [--min-points N] [--region x,y,w,h ...]

Exit codes: 0 = every compared point matched within the tolerance,
            1 = at least one point did not, 2 = the input could not be read.
"""
import re
import struct
import sys

EXIT_PASS, EXIT_FAIL, EXIT_ERROR = 0, 1, 2
TOKEN = re.compile(r'([A-Za-z][A-Za-z0-9_]*)=([^ ]+)')


def load_bmp(path):
    with open(path, 'rb') as handle:
        data = handle.read()
    if data[:2] != b'BM':
        raise ValueError('not a BMP: ' + path)
    pixel_offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 24:
        raise ValueError('want 24bpp, got %d' % bpp)
    flip = height < 0
    height = abs(height)
    stride = (width * 3 + 3) // 4 * 4
    rows = []
    for y in range(height):
        source = y if flip else height - 1 - y
        base = pixel_offset + source * stride
        row = []
        for x in range(width):
            b, g, r = data[base + x * 3], data[base + x * 3 + 1], data[base + x * 3 + 2]
            row.append((r, g, b))
        rows.append(row)
    return width, height, rows


def parse_trace(path):
    faces = []
    summary = None
    refusals = []
    with open(path, 'rb') as handle:
        text = handle.read().decode('utf-8', 'replace')
    for line in text.splitlines():
        at = line.find('gpu: world face-trace-summary ')
        if at >= 0:
            summary = dict(TOKEN.findall(line[at:]))
            continue
        at = line.find('gpu: world visible-refusal ')
        if at >= 0:
            refusals.append(dict(TOKEN.findall(line[at:])))
            continue
        at = line.find('gpu: world face-trace ')
        if at < 0:
            continue
        faces.append(dict(TOKEN.findall(line[at:])))
    return faces, summary, refusals


def numbers(field):
    return [int(value) for value in field.split(',')]


def main(argv):
    trace_path = frame_path = mode = None
    tolerance = None
    min_points = 1
    regions = []
    at = 1
    while at < len(argv):
        arg = argv[at]
        if arg == '--trace':
            trace_path = argv[at + 1]; at += 2
        elif arg == '--frame':
            frame_path = argv[at + 1]; at += 2
        elif arg == '--mode':
            mode = argv[at + 1]; at += 2
        elif arg == '--tol':
            tolerance = int(argv[at + 1]); at += 2
        elif arg == '--min-points':
            min_points = int(argv[at + 1]); at += 2
        elif arg == '--region':
            regions.append(tuple(int(v) for v in argv[at + 1].split(','))); at += 2
        else:
            print('FACE-CHECK status=ERROR reason=unknown-argument %s' % arg)
            return EXIT_ERROR
    if not trace_path or not frame_path or mode not in ('albedo', 'albedo-old', 'textured'):
        print('FACE-CHECK status=ERROR reason=usage')
        return EXIT_ERROR
    # The tolerance is stated, not inherited: the albedo view writes the sampled
    # texel through the swap chain, so a bilinear tap that differs by one LSB in
    # the CPU mirror is not a material defect; the textured view adds a lightmap
    # tap and the rasterizer's interpolation, which is why it gets its own
    # number. Both are small enough that a wrong tile (delta > 20), a repeating
    # texture (delta > 20) or a missing light term (delta > 40) cannot hide.
    if tolerance is None:
        tolerance = 3 if mode in ('albedo', 'albedo-old') else 6
    try:
        width, height, pixels = load_bmp(frame_path)
        faces, summary, refusals = parse_trace(trace_path)
    except (OSError, ValueError) as error:
        print('FACE-CHECK status=ERROR reason=%s' % str(error).replace(' ', '-'))
        return EXIT_ERROR
    compared = 0
    mismatched = 0
    missing = 0
    skipped_depth = 0
    worst = -1
    worst_at = '-'
    for face in faces:
        point = face.get('point', '')
        if 'tri' not in face or face.get('tri') in (None, 'none'):
            missing += 1
            print('FACE-POINT point=%s status=no-face reason=%s' % (point, face.get('reason', '-')))
            continue
        if face.get('depthMatch') == '0':
            # The frame's depth disagrees with the traced face: a model, the
            # water pass or a nearer triangle owns this pixel. The trace still
            # names the chain it walked, but a colour comparison here would be
            # a comparison with another surface, so it is reported and skipped
            # rather than counted as a material defect.
            skipped_depth += 1
            print('FACE-POINT point=%s status=not-this-face material=%s'
                  % (point, face.get('material', '-')))
            continue
        field = face.get({'albedo': 'albedo', 'albedo-old': 'albedoTile'}.get(mode, 'expected'), '-')
        if field == '-':
            # A point where the two uv hypotheses predict the same texel is not
            # evidence for either one, and a face with no tile has no rival
            # prediction at all. Skipping is the honest count; reporting it as a
            # match would let the old uv pass on the strength of points it never
            # disagreed about.
            if mode == 'albedo-old':
                skipped_depth += 1
                continue
            missing += 1
            print('FACE-POINT point=%s status=no-prediction material=%s' % (point, face.get('material', '-')))
            continue
        if not point or ',' not in point:
            print('FACE-CHECK status=ERROR reason=point-field-missing')
            return EXIT_ERROR
        px, py = (int(v) for v in point.split(','))
        if px < 0 or py < 0 or px >= width or py >= height:
            print('FACE-POINT point=%s status=outside-frame' % point)
            missing += 1
            continue
        if regions and not any(x <= px < x + w and y <= py < y + h for (x, y, w, h) in regions):
            continue
        want = numbers(field)
        got = pixels[py][px]
        delta = max(abs(want[c] - got[c]) for c in range(3))
        compared += 1
        status = 'ok' if delta <= tolerance else 'MISMATCH'
        if delta > worst:
            worst = delta
            worst_at = point
        if status != 'ok':
            mismatched += 1
        print('FACE-POINT point=%s status=%s material=%s tile=%s/%s rect=%s,%s src=%s'
              ' want=%d,%d,%d got=%d,%d,%d maxDelta=%d'
              % (point, status, face.get('material', '-'), face.get('tile', '-').split('/')[0],
                 face.get('tile', '-').split('/')[-1], face.get('rect', ',').split(',')[0],
                 face.get('rect', ',').split(',')[-1], face.get('src', '-'),
                 want[0], want[1], want[2], got[0], got[1], got[2], delta))
    print('FACE-CHECK status=%s mode=%s compared=%d mismatched=%d skipped=%d depthSkipped=%d tolerance=%d'
          ' worstDelta=%d worstPoint=%s points=%s tiled=%s untiled=%s blend=%s oversized=%s'
          ' beyondOldCap=%s discriminating=%s'
          % ('PASS' if mismatched == 0 and compared >= min_points else 'FAIL',
             mode, compared, mismatched, missing, skipped_depth, tolerance, worst, worst_at,
             summary.get('points', '-') if summary else '-',
             summary.get('tiled', '-') if summary else '-',
             summary.get('untiled', '-') if summary else '-',
             summary.get('blend', '-') if summary else '-',
             summary.get('oversized', '-') if summary else '-',
             summary.get('beyondOldCap', '-') if summary else '-',
             summary.get('discriminating', '-') if summary else '-'))
    for row in refusals:
        print('FACE-REFUSAL reason=%s materials=%s visibleTriangles=%s'
              % (row.get('reason', '-'), row.get('materials', '-'), row.get('visibleTriangles', '-')))
    if mismatched:
        return EXIT_FAIL
    if compared < min_points:
        print('FACE-CHECK status=FAIL reason=too-few-compared compared=%d want>=%d' % (compared, min_points))
        return EXIT_FAIL
    return EXIT_PASS


if __name__ == '__main__':
    sys.exit(main(sys.argv))
