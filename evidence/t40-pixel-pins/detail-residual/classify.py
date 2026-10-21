#!/usr/bin/env python3
"""T45: classify T40's detail-layer residual between two builds of the same camera.

WHY THIS EXISTS
  T40 left one NOT VERIFIED item: 2,812 px where the detail layer's *effect*
  toggled between the pre-T29 frame and the current frame, on faces the T26
  face-id mask says did not move and where the baseline (all-switches-off) byte
  is identical. T45 attributes that residual to the world near clip with the T29
  gate's own mutation, and this script is the re-derivable half of the claim: it
  takes the two builds' A (baseline) / F (detail) frames plus the two face-id
  frames and prints the population and the per-face split.

DEFINITIONS (each printed, so a report cannot quietly widen one)
  same-face px        the three face-id bytes are equal in old and new and not
                      the sky id 0: the winning surface did not move.
  baseline-stable px  of those, the A pixel is byte-identical: the surface is
                      not merely the same face, it is drawn identically.
  effect(px)          F != A at that pixel. Nonzero means the detail branch
                      reached the pixel with a texel that is not neutral.
  toggle px           same-face + baseline-stable + effect(old) != effect(new).
                      That is the residual T40 reported; it is a *layer* change
                      on an unmoved surface, not occlusion.
  A-differs px        same-face + A differs: the baseline itself moved, which is
                      what a different atlas plan or uv would look like.
  F-differs px        same-face + F differs: every pixel the detail layer drew
                      differently, toggle or not.

  Face ids decode as px[2] | px[1]<<8 | px[0]<<16 (the encoding T26's gate uses;
  the id is a float written as $colour and read back through the UNORM target).

Usage:
  classify-detail-residual.py --old-a A.bmp --old-f F.bmp --old-ids IDS.bmp \
      --new-a A.bmp --new-f F.bmp --new-ids IDS.bmp [--old-depth D.bmp ...]

  --old-depth/--new-depth, --old-albedo/--new-albedo, --old-flat/--new-flat are
  optional and only refine the attribution: they say whether the depth readback,
  the albedo-only frame or the flat frame moved under the residual pixels.

Exit: 0 = frames readable and classified, 2 = a frame is missing/unreadable.
"""
import argparse
import collections
import struct
import sys


def read_bmp24(path):
    with open(path, 'rb') as handle:
        data = handle.read()
    if len(data) < 54 or data[0:2] != b'BM':
        raise SystemExit('not a BMP: ' + path)
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    if bits != 24:
        raise SystemExit('not a 24-bit BMP: ' + path)
    flip = height > 0
    height = abs(height)
    stride = ((width * 3) + 3) & ~3
    out = bytearray(width * height * 3)
    for y in range(height):
        src = (height - 1 - y) if flip else y
        start = offset + src * stride
        out[y * width * 3:(y + 1) * width * 3] = data[start:start + width * 3]
    return width, height, out


def ident(px, i):
    return px[i * 3 + 2] | (px[i * 3 + 1] << 8) | (px[i * 3] << 16)


def pixel(px, i):
    return px[i * 3:i * 3 + 3]


def main():
    ap = argparse.ArgumentParser()
    for name in ('old-a', 'old-f', 'old-ids', 'new-a', 'new-f', 'new-ids'):
        ap.add_argument('--' + name, required=True)
    for name in ('old-depth', 'new-depth', 'old-albedo', 'new-albedo', 'old-flat', 'new-flat'):
        ap.add_argument('--' + name, default=None)
    args = ap.parse_args()

    w, h, old_a = read_bmp24(args.old_a)
    _, _, old_f = read_bmp24(args.old_f)
    _, _, old_ids = read_bmp24(args.old_ids)
    w2, h2, new_a = read_bmp24(args.new_a)
    _, _, new_f = read_bmp24(args.new_f)
    _, _, new_ids = read_bmp24(args.new_ids)
    if (w, h) != (w2, h2) or len(old_a) != len(new_a):
        raise SystemExit('frame size mismatch')

    optional = {}
    for name in ('old-depth', 'new-depth', 'old-albedo', 'new-albedo', 'old-flat', 'new-flat'):
        path = getattr(args, name.replace('-', '_'))
        if path:
            optional[name] = read_bmp24(path)[2]

    same_face = 0
    baseline_differs = 0
    baseline_differs_max = 0
    f_differs = 0
    f_differs_max = 0
    toggle = 0
    toggle_old_only = 0
    toggle_new_only = 0
    toggle_faces = collections.Counter()
    f_faces = collections.Counter()
    toggle_px = []
    for i in range(w * h):
        if pixel(old_ids, i) != pixel(new_ids, i) or ident(new_ids, i) == 0:
            continue
        same_face += 1
        da = max(abs(old_a[i * 3 + c] - new_a[i * 3 + c]) for c in range(3))
        df = max(abs(old_f[i * 3 + c] - new_f[i * 3 + c]) for c in range(3))
        if da:
            baseline_differs += 1
            baseline_differs_max = max(baseline_differs_max, da)
        if df:
            f_differs += 1
            f_differs_max = max(f_differs_max, df)
            f_faces[ident(new_ids, i)] += 1
        if da == 0:
            old_effect = pixel(old_f, i) != pixel(old_a, i)
            new_effect = pixel(new_f, i) != pixel(new_a, i)
            if old_effect != new_effect:
                toggle += 1
                toggle_faces[ident(new_ids, i)] += 1
                toggle_px.append(i)
                if old_effect:
                    toggle_old_only += 1
                else:
                    toggle_new_only += 1

    print('frame=%dx%d pixels=%d' % (w, h, w * h))
    print('same-face-id pixels=%d (%.2f%% of frame)' % (same_face, 100.0 * same_face / (w * h)))
    print('baseline A differs on same-face px=%d (%.3f%%) max_delta=%d'
          % (baseline_differs, 100.0 * baseline_differs / same_face, baseline_differs_max))
    print('detail F differs on same-face px=%d (%.3f%%) max_delta=%d faces=%d'
          % (f_differs, 100.0 * f_differs / same_face, f_differs_max, len(f_faces)))
    print('toggle px=%d (%.3f%% of same-face) old_only=%d new_only=%d faces=%d'
          % (toggle, 100.0 * toggle / same_face, toggle_old_only, toggle_new_only, len(toggle_faces)))
    print('toggle faces (id:px): ' + ', '.join('%d:%d' % kv for kv in toggle_faces.most_common(30)))
    print('F-differs faces (id:px): ' + ', '.join('%d:%d' % kv for kv in f_faces.most_common(50)))
    for label, pair in (('depth', ('old-depth', 'new-depth')),
                        ('albedo', ('old-albedo', 'new-albedo')),
                        ('flat', ('old-flat', 'new-flat'))):
        if pair[0] in optional and pair[1] in optional:
            changed = sum(1 for i in toggle_px
                          if pixel(optional[pair[0]], i) != pixel(optional[pair[1]], i))
            print('%s changed under toggle px=%d/%d' % (label, changed, toggle))
    return 0


if __name__ == '__main__':
    sys.exit(main())
