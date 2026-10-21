#!/usr/bin/env python3
"""T45: geometry/depth/albedo structure of T40's 2,812 px detail residual.

Pure BMP analysis over frames that already exist:
  pre  2026-10-20: t40-pixel-pins/.scratch/t40/frames/material-old + t26-pre
  post current:   this worktree evidence/material-* (T40 run) + t26 captures
No product launch, so no host lock is needed (T35 did the same for its account).
"""
import collections, struct, sys

T40 = r"D:\TF2_Native_Worktrees\t40-pixel-pins\.scratch\t40\frames"
HERE = r"D:\TF2_Native_Worktrees\t45-visible-material"


def read_bmp24(path):
    data = open(path, "rb").read()
    if len(data) < 54 or data[0:2] != b"BM":
        raise SystemExit("not a BMP: " + path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    flip = height > 0
    height = abs(height)
    stride = ((width * 3) + 3) & ~3
    out = bytearray(width * height * 3)
    for y in range(height):
        src = (height - 1 - y) if flip else y
        start = offset + src * stride
        out[y * width * 3:(y + 1) * width * 3] = data[start:start + width * 3]
    return width, height, out


def pixmask(a, b):
    m = bytearray(len(a) // 3)
    for i in range(len(a)):
        if a[i] != b[i]:
            m[i // 3] = 1
    return m


w, h, preA = read_bmp24(T40 + r"\material-old\A.bmp")
_, _, postA = read_bmp24(T40 + r"\material-new\A.bmp")
_, _, preF = read_bmp24(T40 + r"\material-old\F.bmp")
_, _, postF = read_bmp24(T40 + r"\material-new\F.bmp")
_, _, pre_id = read_bmp24(T40 + r"\t26-pre\world-ids.bmp")
_, _, post_id = read_bmp24(HERE + r"\evidence\t26-world-holes\captures\world-ids.bmp")
_, _, pre_depth = read_bmp24(T40 + r"\t26-pre\depth-base.bmp")
_, _, post_depth = read_bmp24(HERE + r"\evidence\t26-world-holes\captures\depth-base.bmp")
_, _, pre_lm = read_bmp24(T40 + r"\t26-pre\lightmap-off.bmp")
_, _, post_lm = read_bmp24(HERE + r"\evidence\t26-world-holes\captures\lightmap-off.bmp")
_, _, pre_flat = read_bmp24(T40 + r"\t26-pre\flat-base.bmp")
_, _, post_flat = read_bmp24(HERE + r"\evidence\t26-world-holes\captures\flat-base.bmp")

old = pixmask(preA, preF)
new = pixmask(postA, postF)
D = pixmask(preA, postA)


def ident(px, i):
    return px[i * 3 + 2] | (px[i * 3 + 1] << 8) | (px[i * 3] << 16)


res = []
for i in range(w * h):
    if old[i] != new[i] and not D[i] and pre_id[i*3:i*3+3] == post_id[i*3:i*3+3]:
        res.append(i)
print("residual same-face pixels=%d" % len(res))
faces = collections.Counter(ident(post_id, i) for i in res)
print("faces=%d top=%s" % (len(faces), faces.most_common(8)))
d_changed = sum(1 for i in res if pre_depth[i*3:i*3+3] != post_depth[i*3:i*3+3])
lm_changed = sum(1 for i in res if pre_lm[i*3:i*3+3] != post_lm[i*3:i*3+3])
fl_changed = sum(1 for i in res if pre_flat[i*3:i*3+3] != post_flat[i*3:i*3+3])
print("depth_changed=%d/%d albedo_changed=%d/%d flat_changed=%d/%d"
      % (d_changed, len(res), lm_changed, len(res), fl_changed, len(res)))


def id_changed(i):
    return pre_id[i*3:i*3+3] != post_id[i*3:i*3+3]


adj = 0
for i in res:
    x, y = i % w, i // w
    found = False
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and id_changed(ny * w + nx):
                found = True
    if found:
        adj += 1
print("adjacent_to_id_changed_1px=%d/%d (%.1f%%)" % (adj, len(res), 100.0*adj/len(res)))

# connected components of the residual
seen = bytearray(w * h)
comps = []
for start in res:
    if seen[start]:
        continue
    stack = [start]
    seen[start] = 1
    comp = []
    while stack:
        i = stack.pop()
        comp.append(i)
        x, y = i % w, i // w
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    j = ny * w + nx
                    if not seen[j] and (old[j] != new[j]) and not D[j] and pre_id[j*3:j*3+3] == post_id[j*3:j*3+3]:
                        seen[j] = 1
                        stack.append(j)
    comps.append(comp)
comps.sort(key=len, reverse=True)
print("components=%d sizes=%s" % (len(comps), [len(c) for c in comps[:20]]))
for c in comps[:15]:
    xs = [i % w for i in c]; ys = [i // w for i in c]
    fid = collections.Counter(ident(post_id, i) for i in c).most_common(2)
    side = collections.Counter("old" if old[i] else "new" for i in c)
    print("  comp n=%3d bbox=(%d..%d,%d..%d) faces=%s side=%s"
          % (len(c), min(xs), max(xs), min(ys), max(ys), fid, dict(side)))

# face 7536's whole extent and what changed inside it
if 7536 in faces:
    ext = [i for i in range(w * h) if ident(post_id, i) == 7536 and ident(pre_id, i) == 7536]
    print("face7536 same-id pixels=%d" % len(ext))
    print("  baseline_moved=%d detail_old=%d detail_new=%d detail_both=%d detail_neither=%d"
          % (sum(1 for i in ext if D[i]), sum(1 for i in ext if old[i]), sum(1 for i in ext if new[i]),
             sum(1 for i in ext if old[i] and new[i]), sum(1 for i in ext if not old[i] and not new[i])))
    print("  detail_new_only=%d detail_old_only=%d"
          % (sum(1 for i in ext if new[i] and not old[i]), sum(1 for i in ext if old[i] and not new[i])))
    # sample 12 pixels across the face's bbox: A/F values pre and post
    xs = [i % w for i in ext]; ys = [i // w for i in ext]
    print("  face7536 bbox=(%d..%d,%d..%d)" % (min(xs), max(xs), min(ys), max(ys)))
    for i in ext[:600:60]:
        print("   x=%d y=%d oldF-A=%s newF-A=%s Aold=%s Anew=%s Fold=%s Fnew=%s"
              % (i % w, i // w,
                 tuple(preF[i*3+k]-preA[i*3+k] for k in range(3)),
                 tuple(postF[i*3+k]-postA[i*3+k] for k in range(3)),
                 tuple(preA[i*3:i*3+3]), tuple(postA[i*3:i*3+3]),
                 tuple(preF[i*3:i*3+3]), tuple(postF[i*3:i*3+3])))
