import sys
from PIL import Image, ImageChops
def stats(pa, pb):
    a = Image.open(pa).convert("RGB"); b = Image.open(pb).convert("RGB")
    d = ImageChops.difference(a, b)
    px = list(d.getdata())
    n = len(px)
    any_ = sum(1 for p in px if max(p) > 0)
    gt2 = sum(1 for p in px if max(p) > 2)
    gt8 = sum(1 for p in px if max(p) > 8)
    mx = max(max(p) for p in px)
    # 64x64 cell histogram of ">8" pixels
    w, h = a.size
    cells = {}
    i = 0
    for y in range(h):
        cy = y // 64
        for x in range(w):
            p = px[i]; i += 1
            if max(p) > 8:
                cells[(cy, x // 64)] = cells.get((cy, x // 64), 0) + 1
    top = sorted(cells.items(), key=lambda kv: -kv[1])[:5]
    return dict(size=(w, h), pixels=n, any_gt0=any_, gt2=gt2, gt8=gt8,
                pct_gt8=round(100.0 * gt8 / n, 3), maxdelta=mx,
                cells_with_diff=len(cells), top_cells=top)
for pair in [("R.bmp", "D.bmp"), ("R.bmp", "D2.bmp")]:
    s = stats(pair[0], pair[1])
    print(pair[0], "vs", pair[1], s)
