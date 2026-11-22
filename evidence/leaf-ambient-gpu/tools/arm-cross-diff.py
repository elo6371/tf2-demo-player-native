import sys, itertools
from PIL import Image
base = ".scratch/leafgpu/"
names = ["bare", "basic", "leaf", "leaf-trace"]
imgs = {n: Image.open(base + n + ".bmp").convert("RGB") for n in names}
W, H = imgs["leaf"].size
pix = {n: imgs[n].load() for n in names}
for a, b in itertools.combinations(names, 2):
    pa, pb = pix[a], pix[b]
    changed = 0; sumabs = 0
    for y in range(H):
        for x in range(W):
            ca, cb = pa[x, y], pb[x, y]
            d = max(abs(ca[0]-cb[0]), abs(ca[1]-cb[1]), abs(ca[2]-cb[2]))
            if d: changed += 1
            sumabs += d
    print("%-11s vs %-11s changed=%d (%.3f%%) meanAbs=%.3f" % (a, b, changed, 100.0*changed/(W*H), sumabs/(W*H)))
# mask of leaf vs leaf-trace
pa, pb = pix["leaf"], pix["leaf-trace"]
mask = Image.new("RGB", (W, H))
pm = mask.load()
for y in range(H):
    for x in range(W):
        ca, cb = pa[x, y], pb[x, y]
        d = max(abs(ca[0]-cb[0]), abs(ca[1]-cb[1]), abs(ca[2]-cb[2]))
        pm[x, y] = (255, 0, 0) if d >= 16 else ((0, 120, 0) if d else (0, 0, 0))
mask.save(base + "diff-leaf-vs-leaftrace.png")
# same-box side by side crops for a few rows of the mask
print("mask written diff-leaf-vs-leaftrace.png")
