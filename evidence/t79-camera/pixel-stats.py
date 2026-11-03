import sys
from PIL import Image, ImageChops


def components(mask, w, h):
    """Connected components (8-neighbour) of a boolean pixel mask."""
    seen = set()
    comps = []
    for pt in mask:
        if pt in seen:
            continue
        stack = [pt]
        seen.add(pt)
        members = []
        while stack:
            cx, cy = stack.pop()
            members.append((cx, cy))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (cx + dx, cy + dy)
                    if q in mask and q not in seen:
                        seen.add(q)
                        stack.append(q)
        comps.append(members)
    comps.sort(key=len, reverse=True)
    return comps


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
    # Shape of the residual, not just its size. One big blob is an object that is
    # in one frame and not the other; a few hundred scattered pixels along edges
    # is the two cameras disagreeing by a sub-pixel. The count alone cannot tell
    # those apart, and they mean opposite things -- so both are readings here.
    mask = {(x, y) for y in range(h) for x in range(w) if max(px[y * w + x]) > 8}
    comps = components(mask, w, h)
    biggest = 0
    biggest_box = None
    if comps:
        biggest = len(comps[0])
        xs = [c[0] for c in comps[0]]; ys = [c[1] for c in comps[0]]
        biggest_box = (min(xs), min(ys), max(xs), max(ys))
    return dict(size=(w, h), pixels=n, any_gt0=any_, gt2=gt2, gt8=gt8,
                pct_gt8=round(100.0 * gt8 / n, 3), maxdelta=mx,
                cells_with_diff=len(cells), top_cells=top,
                comps=len(comps), comps_ge200=sum(1 for c in comps if len(c) >= 200),
                largest_comp=biggest, largest_comp_box=biggest_box)


# Optional directory keeps this reader usable for a new, immutable run without
# overwriting the snakewater readings beside this script. With no argument it
# retains the original evidence-directory behavior.
from pathlib import Path
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
for pair in [("R.bmp", "D.bmp"), ("R.bmp", "D2.bmp")]:
    s = stats(root / pair[0], root / pair[1])
    print(pair[0], "vs", pair[1], s)
