# T105/task-37 Half B evidence generator. Reads only the raw .log/.bmp this
# round wrote and prints the tables the doc quotes, so the doc cannot drift
# from the artefacts.
import re, os, hashlib
from PIL import Image

base = ".scratch/leafgpu/"

def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def lines(name):
    p = base + name + ".log"
    return open(p, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(p) else []

# ---- 1. pins -------------------------------------------------------------
baseline = {}
if os.path.exists(base + "baseline-md5.txt"):
    for ln in open(base + "baseline-md5.txt"):
        parts = ln.split()
        if len(parts) == 2:
            baseline[os.path.basename(parts[1]).replace(".bmp", "")] = parts[0]
out = ["# pins: one capture per run, --capture-tick 600, audio disabled, hidden window",
       "# predecessor md5 = the .bmp the predecessor's exe left in .scratch (captured 00:48-00:55)",
       "# new md5 = this round's build (tf2_demo_native.exe 09:42:57, three sources touched before the build)"]
for name in ["bare", "off", "basic", "leaf", "leaf-trace", "sunshine-leaf"]:
    bmp = base + name + ".bmp"
    if not os.path.exists(bmp):
        out.append("%-14s MISSING" % name)
        continue
    cur = md5(bmp)
    old = baseline.get(name, "(none)")
    same = "SAME" if old == cur else "DIFFERS"
    out.append("%-14s md5=%s  predecessor=%s  %s" % (name, cur, old, same))
out.append("")
out.append("# capture lines of the leaf arms (the counters are per completed entity update)")
for name in ["bare", "off", "basic", "leaf", "leaf-trace", "sunshine-leaf"]:
    for ln in lines(name):
        if ln.startswith("capture: leaf-ambient") or ln.startswith("capture: model-lighting") or ln.startswith("capture: camera"):
            out.append("%-14s %s" % (name, ln))
open(base + "pins-and-counters.txt", "w", encoding="utf-8").write("\n".join(out) + "\n")

# ---- 2. per entity cubes ------------------------------------------------
leaf_re = re.compile(r"lighting-leaf: index=(-?\d+) key=(\S+) origin=\(([-\d.]+),([-\d.]+),([-\d.]+)\)"
                     r" point=\(([-\d.]+),([-\d.]+),([-\d.]+)\) leaf=(-?\d+) slot=(-?\d+) absent=(\w+)"
                     r" cube=(\([^)]*\)\([^)]*\)\([^)]*\)\([^)]*\)\([^)]*\)\([^)]*\))")
ents = []
for ln in lines("leaf-trace"):
    m = leaf_re.search(ln)
    if not m:
        continue
    nums = [float(v) for v in re.findall(r"[-\d.]+", m.group(12))]
    cube = [nums[i*3:(i+1)*3] for i in range(6)]
    mean = sum(sum(c) for c in cube) / 18.0
    up = sum(cube[4]) / 3.0
    down = sum(cube[5]) / 3.0
    empty = all(v == 0.0 for c in cube for v in c)
    ents.append(dict(index=int(m.group(1)), key=m.group(2), leaf=int(m.group(9)), slot=int(m.group(10)),
                     absent=m.group(11), cube=cube, mean=mean, up=up, down=down, empty=empty,
                     origin=[float(m.group(i)) for i in (3, 4, 5)]))
ents.sort(key=lambda e: -e["up"])
o = ["# cp_process_f12, tick 600, --lighting leaf --lighting-trace",
     "# every player instance the trace printed, sorted by measured +Z (up-face) luminance;",
     "# +Z face = how much sky that leaf can see, so it is the indoor/outdoor measurement.",
     "# faces are +X,-X,+Y,-Y,+Z,-Z of the leaf cube, in LINEAR radiance"]
o.append("%-6s %-6s %-5s %-8s %-8s %-8s %-8s %-6s" % ("index", "leaf", "slot", "mean", "up(+Z)", "down(-Z)", "up/mean", "empty"))
for e in ents:
    o.append("%-6d %-6d %-5d %-8.6f %-8.6f %-8.6f %-8.3f %-6s" % (
        e["index"], e["leaf"], e["slot"], e["mean"], e["up"], e["down"], (e["up"] / e["mean"] if e["mean"] else 0.0), e["empty"]))
if ents:
    top, bot = ents[0], ents[-1]
    o.append("")
    o.append("OUTDOOR (brightest up face): index=%d leaf=%d slot=%d up=%.6f" % (top["index"], top["leaf"], top["slot"], top["up"]))
    o.append("INDOOR  (darkest  up face): index=%d leaf=%d slot=%d up=%.6f" % (bot["index"], bot["leaf"], bot["slot"], bot["up"]))
    if bot["up"] > 0:
        o.append("ratio outdoor/indoor up face = %.2f" % (round(top["up"], 6) / round(bot["up"], 6)))
    o.append("empty cubes among the printed players: %d" % sum(1 for e in ents if e["empty"]))
open(base + "entity-cubes-tick600.txt", "w", encoding="utf-8").write("\n".join(o) + "\n")

# ---- 3. the two visible players ----------------------------------------
leaf = Image.open(base + "leaf.bmp").convert("RGB")
basic = Image.open(base + "basic.bmp").convert("RGB")
W, H = leaf.size

def is_red(c):
    r, g, b = c
    return r > 80 and r > 1.7 * g and r > 1.7 * b

def stats(crop):
    px = crop.load(); n = red = 0; sr = sg = sb = 0; ar = ag = ab = 0
    for y in range(crop.height):
        for x in range(crop.width):
            c = px[x, y]; n += 1; ar += c[0]; ag += c[1]; ab += c[2]
            if is_red(c):
                red += 1; sr += c[0]; sg += c[1]; sb += c[2]
    return dict(n=n, red=red, redMean=(sr / red, sg / red, sb / red) if red else None,
                cropMean=(ar / n, ag / n, ab / n))

CW, CH = 120, 200
picks = [("courtyard player (bottom of frame)", 729, 590, 0.0615),
         ("doorway player (right of frame)", 1072, 544, 0.0348)]
o = ["# the two players the capture actually shows, 1:1 crops, same box in both arms",
     "# located by the red-uniform mask (r>80 and r>1.7*green and r>1.7*blue); the engine",
     "# prints no screen projection, so the box comes from the measured blob, not from a",
     "# projection of the trace origin (see projection-caveat.txt for that attempt)."]
for label, bx, by, _ in picks:
    box = (max(0, min(W - CW, bx - 20)), max(0, min(H - CH, by - 60)), 0, 0)
    box = (box[0], box[1], box[0] + CW, box[1] + CH)
    a = stats(leaf.crop(box)); b = stats(basic.crop(box))
    d_img = [round(a["cropMean"][i] - b["cropMean"][i], 2) for i in range(3)]
    o.append("")
    o.append("%s box=%s" % (label, box))
    o.append("  leaf : redFrac=%.4f  redMean=%s  cropMean=%s" % (a["redFracText"] if False else a["red"] / a["n"], tuple(round(v, 1) for v in a["redMean"]), tuple(round(v, 1) for v in a["cropMean"])))
    o.append("  basic: redFrac=%.4f  redMean=%s  cropMean=%s" % (b["red"] / b["n"], tuple(round(v, 1) for v in b["redMean"]), tuple(round(v, 1) for v in b["cropMean"])))
    o.append("  leaf-minus-basic crop mean (R,G,B) = %s" % d_img)
open(base + "player-crops-stats.txt", "w", encoding="utf-8").write("\n".join(o) + "\n")

# ---- 4. arm diffs ------------------------------------------------------
def diff(pa, pb):
    a = Image.open(pa).convert("RGB"); b = Image.open(pb).convert("RGB")
    if a.size != b.size:
        return "size mismatch %s %s" % (a.size, b.size)
    xa, xb = a.load(), b.load(); Wd, Hd = a.size
    changed = big = 0; total = 0; mx = 0
    for y in range(Hd):
        for x in range(Wd):
            ca, cb = xa[x, y], xb[x, y]
            d = max(abs(ca[0] - cb[0]), abs(ca[1] - cb[1]), abs(ca[2] - cb[2]))
            if d: changed += 1
            if d >= 16: big += 1
            total += d
            if d > mx: mx = d
    return dict(changed=changed, big=big, mean=total / (Wd * Hd), max=mx, n=Wd * Hd)

pairs = [("leaf", "basic"), ("leaf", "leaf-trace"), ("basic", "leaf-trace"), ("bare", "basic")]
o = ["# pixel differences between captures of the SAME tick (1264x681 = 860784 px)",
     "# --lighting-trace renders the interpolated normal as colour by design",
     "# (native/src/native_renderer_shaders.cpp:623-629), so its capture is a debug view,",
     "# not a lighting view: the cube values are printed before that shader branch runs."]
for a, b in pairs:
    d = diff(base + a + ".bmp", base + b + ".bmp")
    o.append("%-11s vs %-11s changed=%d (%.3f%%) changed>=16=%d meanAbs=%.3f max=%d" % (
        a, b, d["changed"], 100.0 * d["changed"] / d["n"], d["big"], d["mean"], d["max"]))
if os.path.exists(base + "leaf-preview.png"):
    d = diff(base + "leaf-preview.png", base + "leaf.bmp")
    o.append("predecessor leaf (png) vs this round leaf: changed=%d (%.3f%%) changed>=16=%d meanAbs=%.3f max=%d" % (
        d["changed"], 100.0 * d["changed"] / d["n"], d["big"], d["mean"], d["max"]))
open(base + "arm-diffs.txt", "w", encoding="utf-8").write("\n".join(o) + "\n")
print("WROTE pins-and-counters.txt entity-cubes-tick600.txt player-crops-stats.txt arm-diffs.txt")
