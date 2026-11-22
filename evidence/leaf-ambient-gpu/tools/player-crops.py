# T105/task-37 Half B: the two players the tick actually shows, 1:1 in both arms.
# The engine prints no screen projection, so a player is located by its red
# uniform (the mask below) and the crop is cut around that measured blob.
from PIL import Image, ImageDraw
base = ".scratch/leafgpu/"
leaf = Image.open(base + "leaf.bmp").convert("RGB")
basic = Image.open(base + "basic.bmp").convert("RGB")
W, H = leaf.size
pl = leaf.load()

def is_red(c):
    r, g, b = c
    return r > 80 and r > 1.7 * g and r > 1.7 * b

seen = [[False] * W for _ in range(H)]
blobs = []
stack = []
for y0 in range(0, H, 2):
    for x0 in range(0, W, 2):
        if seen[y0][x0] or not is_red(pl[x0, y0]):
            continue
        stack = [(x0, y0)]
        seen[y0][x0] = True
        pts = []
        while stack:
            x, y = stack.pop()
            pts.append((x, y))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H and not seen[ny][nx] and is_red(pl[nx, ny]):
                        seen[ny][nx] = True
                        stack.append((nx, ny))
        if len(pts) >= 120:
            xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
            blobs.append((min(xs), min(ys), max(xs), max(ys), len(pts)))
blobs.sort(key=lambda b: -b[4])
print("BLOBS", blobs)

def stats(crop):
    px = crop.load()
    n = red = 0; sr = sg = sb = 0; ar = ag = ab = 0
    for y in range(crop.height):
        for x in range(crop.width):
            c = px[x, y]
            n += 1; ar += c[0]; ag += c[1]; ab += c[2]
            if is_red(c):
                red += 1; sr += c[0]; sg += c[1]; sb += c[2]
    return dict(pixels=n, red=red, redFrac=red / n,
                redMean=(sr / red, sg / red, sb / red) if red else None,
                cropMean=(ar / n, ag / n, ab / n))

CW, CH = 120, 200
chosen = []
for b in blobs[:4]:
    x0, y0, x1, y1, cnt = b
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    bx = (max(0, min(W - CW, cx - CW // 2)), max(0, min(H - CH, cy - CH // 2)))
    box = (bx[0], bx[1], bx[0] + CW, bx[1] + CH)
    sl = stats(leaf.crop(box)); sb2 = stats(basic.crop(box))
    chosen.append((b, box, sl, sb2))
    print("BLOB bbox=%s box=%s  leaf: redFrac=%.4f redMean=%s cropMean=%s" % (b, box, sl["redFrac"], sl["redMean"], sl["cropMean"]))
    print("                                basic: redFrac=%.4f redMean=%s cropMean=%s" % (sb2["redFrac"], sb2["redMean"], sb2["cropMean"]))

sheet = Image.new("RGB", (CW * 2 + 8, (CH + 20) * len(chosen)), (18, 18, 18))
d = ImageDraw.Draw(sheet)
for i, (b, box, sl, sb2) in enumerate(chosen):
    y = i * (CH + 20)
    sheet.paste(leaf.crop(box), (0, y + 20))
    sheet.paste(basic.crop(box), (CW + 8, y + 20))
    d.text((2, y + 5), "blob %d  LEFT leaf / RIGHT basic   leafRedMean=%s basicRedMean=%s" % (
        i, tuple(round(v, 1) for v in sl["redMean"]), tuple(round(v, 1) for v in sb2["redMean"])), fill=(255, 255, 0))
sheet.save(base + "accept-players-1to1.png")
print("WROTE accept-players-1to1.png", sheet.size)
