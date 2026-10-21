#!/usr/bin/env python3
"""T45: exact relation between A, F_old, F_new on the residual pixels."""
import collections, struct

T40 = r"D:\TF2_Native_Worktrees\t40-pixel-pins\.scratch\t40\frames"
HERE = r"D:\TF2_Native_Worktrees\t45-visible-material"


def read_bmp24(path):
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    flip = height > 0
    height = abs(height)
    stride = ((width * 3) + 3) & ~3
    out = bytearray(width * height * 3)
    for y in range(height):
        src = (height - 1 - y) if flip else y
        start = offset + src * stride
        out[y * width * 3:(y + 1) * width * 3] = data[start:start + width * 3]
    return width, height, out


def mask(a, b):
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

d_old = mask(preA, preF)
d_new = mask(postA, postF)
D = mask(preA, postA)


def ident(px, i):
    return px[i*3+2] | (px[i*3+1] << 8) | (px[i*3] << 16)


res = [i for i in range(w*h) if d_old[i] != d_new[i] and not D[i]
       and pre_id[i*3:i*3+3] == post_id[i*3:i*3+3]]
oldonly = [i for i in res if d_old[i]]
newonly = [i for i in res if d_new[i]]
print("residual=%d old_only=%d new_only=%d" % (len(res), len(oldonly), len(newonly)))

# relation of the pair-delta on residual pixels
# The pair delta is F - A, the detail layer's contribution. It is computed on the
# frame of the build named by the caller, so the two directions cannot be the same
# measurement by accident (the first version of this script had exactly that bug:
# it printed the pre-fix delta twice).
def deltas(F, A, idx):
    out = collections.Counter()
    for i in idx:
        d = tuple(F[i*3+k] - A[i*3+k] for k in range(3))
        out[d] += 1
    return out

print("old-only pixels: PRE pair delta (F_old-A_old) top=%s"
      % deltas(preF, preA, oldonly).most_common(6))
print("  among them the NEW pair delta is exactly 0 for all: %s"
      % all(tuple(postF[i*3+k]-postA[i*3+k] for k in range(3)) == (0,0,0) for i in oldonly))
print("new-only pixels: NEW pair delta (F_new-A_new) top=%s"
      % deltas(postF, postA, newonly).most_common(6))
print("  among them the PRE pair delta is exactly 0 for all: %s"
      % all(tuple(preF[i*3+k]-preA[i*3+k] for k in range(3)) == (0,0,0) for i in newonly))

# F_old vs F_new on residual pixels
sd = collections.Counter()
for i in res:
    sd[max(abs(preF[i*3+k]-postF[i*3+k]) for k in range(3))] += 1
print("F_old vs F_new max-channel delta hist=%s" % sorted(sd.items())[:15])

# F_old vs A at old-only, F_new vs A at new-only, magnitude
def mags(idx, F):
    m = collections.Counter()
    for i in idx:
        m[max(abs(F[i*3+k]-preA[i*3+k]) for k in range(3))] += 1
    return m
print("old_only |F_old-A| hist=%s" % sorted(mags(oldonly, preF).items())[:12])
print("new_only |F_new-A| hist=%s" % sorted(mags(newonly, postF).items())[:12])

# So the residual is a per-pixel difference in whether the detail MULTIPLY happened.
# Compare F_new/F_old ratio where both apply (common detail pixels) to see whether the
# detail texture sample is identical there.
common = [i for i in range(w*h) if d_old[i] and d_new[i]]
same_F = sum(1 for i in common if preF[i*3:i*3+3] == postF[i*3:i*3+3])
print("common detail pixels=%d with F_old==F_new: %d (%.1f%%)"
      % (len(common), same_F, 100.0*same_F/len(common)))
same_A = sum(1 for i in common if preA[i*3:i*3+3] == postA[i*3:i*3+3])
print("  of those, A_old==A_new: %d" % same_A)

# ASCII map of a 40x26 window at the top of the big component
x0, y0, x1, y1 = 44, 260, 100, 300
print("map window x=%d..%d y=%d..%d  O=old-only N=new-only b=both .=neither" % (x0, x1, y0, y1))
for y in range(y0, y1):
    row = ""
    for x in range(x0, x1):
        i = y*w+x
        if D[i]:
            row += "#"
        elif d_old[i] and d_new[i]:
            row += "b"
        elif d_old[i]:
            row += "O"
        elif d_new[i]:
            row += "N"
        else:
            row += "."
    print(row)
