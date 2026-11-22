# T105/task-37 Half B acceptance: project each traced entity onto the capture,
# cut a 1:1 crop in the leaf arm and the same crop in the basic arm, and report
# the cube numbers next to the pixels they claim to light.
import sys, os, re, json, math
from PIL import Image, ImageDraw

base = ".scratch/leafgpu/"
log = sys.argv[1] if len(sys.argv) > 1 else base + "leaf-trace.log"
tag = sys.argv[2] if len(sys.argv) > 2 else "proc"
img_leaf = Image.open(base + "leaf.bmp" if tag == "proc" else base + "sunshine-leaf.bmp").convert("RGB")
img_basic = Image.open(base + "basic.bmp").convert("RGB") if tag == "proc" else None
W, H = img_leaf.size
txt = open(log, encoding="utf-8", errors="replace").read()

pat = (r"capture: camera eye=\(([-\d.]+),([-\d.]+),([-\d.]+)\) forward=\(([-\d.]+),([-\d.]+),([-\d.]+)\)"
       r" hflip=(\d+) vfov=([-\d.]+) aspect=([-\d.]+) worldCenter=\(([-\d.]+),([-\d.]+)\)"
       r" worldMinZ=([-\d.]+) worldScale=([-\d.]+)")
cam = re.search(pat, txt)
if not cam:
    print("NO-CAMERA-LINE in", log)
    sys.exit(3)
g = cam.groups()
eye = [float(g[0]), float(g[1]), float(g[2])]
fwd = [float(g[3]), float(g[4]), float(g[5])]
hflip = int(g[6]); vfov = float(g[7]); aspect = float(g[8])
cx, cy = float(g[9]), float(g[10]); minz = float(g[11]); scale = float(g[12])

def to_box(p):
    return [(p[0] - cx) * scale, (p[1] - cy) * scale, (p[2] - minz) * scale + 0.1]

def norm(v):
    m = math.sqrt(sum(c * c for c in v))
    return [c / m for c in v]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]

f = norm(fwd)
up = [0.0, 0.0, 1.0] if abs(f[2]) <= 0.95 else [0.0, 1.0, 0.0]
r = norm(cross(up, f))
u = cross(f, r)
tanV = math.tan(math.radians(vfov) / 2.0)

def project(p_map):
    d = [a - b for a, b in zip(to_box(p_map), eye)]
    z = dot(d, f)
    if z <= 0.0004:
        return None
    nx = dot(d, r) / (z * tanV * aspect)
    if hflip:
        nx = -nx
    ny = dot(d, u) / (z * tanV)
    return ((1 + nx) / 2.0 * W, (1 - ny) / 2.0 * H, z)

leaf_re = re.compile(r"lighting-leaf: index=(-?\d+) key=(\S+) origin=\(([-\d.]+),([-\d.]+),([-\d.]+)\)"
                     r" point=\(([-\d.]+),([-\d.]+),([-\d.]+)\) leaf=(-?\d+) slot=(-?\d+) absent=(\w+)"
                     r" cube=(\([^)]*\)\([^)]*\)\([^)]*\)\([^)]*\)\([^)]*\)\([^)]*\))")
ents = []
for m in leaf_re.finditer(txt):
    idx = int(m.group(1)); key = m.group(2)
    origin = [float(m.group(i)) for i in (3, 4, 5)]
    leaf = int(m.group(9)); slot = int(m.group(10)); absent = m.group(11)
    nums = [float(v) for v in re.findall(r"[-\d.]+", m.group(12))]
    cube = [nums[i*3:(i+1)*3] for i in range(6)]
    lum = sum(sum(c) for c in cube) / 18.0
    up_lum = sum(cube[4]) / 3.0
    ents.append(dict(index=idx, key=key, origin=origin, leaf=leaf, slot=slot, absent=absent,
                     cube=cube, lum=lum, up=up_lum))

def red_frac(im, box):
    x0, y0, x1, y1 = box
    a = im.crop(box)
    px = a.load()
    n = red = 0
    for yy in range(a.height):
        for xx in range(a.width):
            rr, gg, bb = px[xx, yy]
            n += 1
            if rr > 80 and rr > gg * 1.7 and rr > bb * 1.7:
                red += 1
    return red / max(1, n)

CW, CH = 120, 200
rows = []
for e in ents:
    pr = project(e["origin"])
    e["screen"] = None if pr is None else [round(pr[0], 1), round(pr[1], 1), round(pr[2], 4)]
    if pr is None or not (0 <= pr[0] < W and 0 <= pr[1] < H):
        e["incrop"] = False
        rows.append(e)
        continue
    sx, sy = int(round(pr[0])), int(round(pr[1]))
    # the trace origin is the model origin; players stand up from it, so bias the
    # crop upward by a third of its height and keep it inside the frame.
    box = (max(0, sx - CW // 2), max(0, sy - CH + 40), 0, 0)
    box = (box[0], box[1], min(W, box[0] + CW), min(H, box[1] + CH))
    e["crop"] = box
    e["red_leaf"] = round(red_frac(img_leaf, box), 4)
    if img_basic is not None:
        e["red_basic"] = round(red_frac(img_basic, box), 4)
    e["incrop"] = True
    rows.append(e)

rows.sort(key=lambda e: (not e.get("incrop"), e["index"]))
print("LOG", log, "W,H", W, H, "CAMERA", eye, fwd, "hflip", hflip, "vfov", vfov, "aspect", aspect)
print("WORLD center=(%.4f,%.4f) minZ=%.4f scale=%.6f" % (cx, cy, minz, scale))
for e in rows:
    print("ENT index=%-3d %-34s leaf=%-5d slot=%-3d meanLum=%.6f upLum=%.6f screen=%s incrop=%s crop=%s redLeaf=%s redBasic=%s" % (
        e["index"], os.path.basename(e["key"]), e["leaf"], e["slot"], e["lum"], e["up"],
        e["screen"], e.get("incrop"), e.get("crop"), e.get("red_leaf"), e.get("red_basic")))

# annotated frame + per-entity leaf|basic strips for the entities that project
# inside the picture and actually look like a player in BOTH arms.
frame = img_leaf.copy()
d = ImageDraw.Draw(frame)
for e in rows:
    if not e.get("incrop"):
        continue
    x0, y0, x1, y1 = e["crop"]
    d.rectangle([x0, y0, x1, y1], outline=(0, 255, 0), width=1)
    d.text((x0 + 2, max(0, y0 - 11)), "i%d L%d" % (e["index"], e["leaf"]), fill=(0, 255, 0))
frame.save(base + "accept-%s-annotated.png" % tag)
frame.save(base + "accept-%s-annotated.bmp" % tag)

cands = [e for e in rows if e.get("incrop") and e.get("red_leaf", 0) >= 0.02]
if img_basic is not None:
    cands = [e for e in cands if e.get("red_basic", 0) >= 0.02]
cands.sort(key=lambda e: e["lum"])
print("PLAYER-CANDIDATES", [(e["index"], round(e["lum"], 6), round(e["up"], 6)) for e in cands])
if cands:
    sel = [cands[0], cands[-1]]
    sheet = Image.new("RGB", (CW * 2 + 6, (CH + 22) * 2), (16, 16, 16))
    ds = ImageDraw.Draw(sheet)
    for j, e in enumerate(sel):
        box = e["crop"]
        crop_leaf = img_leaf.crop(box)
        crop_basic = img_basic.crop(box) if img_basic is not None else crop_leaf
        sheet.paste(crop_leaf, (0, j * (CH + 22) + 22))
        sheet.paste(crop_basic, (CW + 6, j * (CH + 22) + 22))
        ds.text((2, j * (CH + 22) + 4), "idx%d leaf=%d meanLum=%.6f upLum=%.6f" % (e["index"], e["leaf"], e["lum"], e["up"]), fill=(255, 255, 0))
    ds.text((2, 0), "LEFT leaf arm / RIGHT basic arm (1:1 crops)", fill=(0, 255, 255))
    sheet.save(base + "accept-%s-pair.png" % tag)
    print("PAIR-WRITTEN accept-%s-pair.png  indices=%s" % (tag, [e["index"] for e in sel]))
