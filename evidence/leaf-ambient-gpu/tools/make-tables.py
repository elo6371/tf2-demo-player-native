# T105/task-37 Half B evidence generator (leaf37f: post-shear-fix).
#
# Reads only the raw .log/.bmp a capture round wrote and prints the tables the doc
# quotes, so the doc cannot drift from the artefacts.
#
# usage: python evidence/leaf-ambient-gpu/tools/make-tables.py <dir-with-captures>
#   <dir-with-captures> holds bare.log/bare.bmp, off.*, basic.*, leaf1..leafN.*,
#   leaf-trace.*, sunshine-leaf*.{log,bmp}. The pre-fix round's captures may sit in
#   the same directory as leaf-preview.png + leaf-pre-fix.bmp; every table is
#   written next to the outputs listed at the bottom.
#
# WHY THE NUMBERS IN HERE CHANGED ON 2026-11-22 (leaf37f): the leaf arm used to be
# drawn through a sheared slot table -- LeafAmbientCubeFace was six float3 (72 B)
# and the upload memcpy'd six float4 (96 B) per cube, so face 1 was read from
# source floats 4-6 and the tail of the table was heap residue. Any leaf number
# produced before commit b1293105 is a single draw of a nondeterministic arm and is
# NOT comparable to anything after it; those pins are kept in
# predecessor-md5.txt/retired-pins.txt as history, never as a baseline.
import re, os, sys, hashlib
from PIL import Image

base = (sys.argv[1] if len(sys.argv) > 1 else ".scratch/leafgpu/").rstrip("/") + "/"
out_dir = base

def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def lines(name):
    p = base + name + ".log"
    return open(p, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(p) else []

def leaf_runs():
    """leaf1..leafN in numeric order, as the batch wrote them."""
    found = []
    for f in os.listdir(base):
        m = re.fullmatch(r"leaf(\d+)\.bmp", f)
        if m:
            found.append((int(m.group(1)), "leaf" + m.group(1)))
    return [n for _, n in sorted(found)]

# ---- 1. pins, and the determinism section -------------------------------
PRE = {}   # pins from before the fix, retired: every one was drawn through the shear
if os.path.exists(base + "pre-fix-md5.txt"):
    for ln in open(base + "pre-fix-md5.txt"):
        parts = ln.split()
        if len(parts) == 2:
            PRE[parts[1]] = parts[0]

runs = leaf_runs()
out = ["# pins: one capture per run, --capture-tick 600, audio disabled, hidden window",
       "# exe: $(sha256 recorded in leaf37f-fix-batch.log) -- the build of commit b1293105"]
order = ["bare", "off", "basic"] + runs + ["leaf-trace", "sunshine-leaf", "sunshine-leaf2", "sunshine-leaf-trace"]
for name in order:
    bmp = base + name + ".bmp"
    if not os.path.exists(bmp):
        out.append("%-20s MISSING" % name)
        continue
    cur = md5(bmp)
    old = PRE.get(name)
    out.append("%-20s md5=%s%s" % (name, cur, ("  retired-pre-fix=" + old) if old else ""))
out.append("")
out.append("# determinism: consecutive captures of the SAME arm with the SAME exe")
# One group = one arm on ONE map. Never mix cp_process_f12 and cp_sunshine in a group: two
# different maps give two different pictures, and counting them as one arm reports a false
# distinct-md5=2 for a perfectly deterministic arm.
for group, names in (("leaf", runs),
                     ("basic", [n for n in ("basic1", "basic2", "basic3") if os.path.exists(base + n + ".bmp")]),
                     ("leaf-trace", [n for n in ("leaf-trace",) if os.path.exists(base + n + ".bmp")]),
                     ("sunshine-leaf", [n for n in ("sunshine-leaf", "sunshine-leaf2") if os.path.exists(base + n + ".bmp")])):
    if not names:
        continue
    got = [md5(base + n + ".bmp") for n in names]
    out.append("%-12s runs=%d distinct-md5=%d  %s" % (
        group, len(got), len(set(got)), " ".join(sorted(set(got)))))
if len(runs) >= 2:
    zero = all(md5(base + runs[0] + ".bmp") == md5(base + n + ".bmp") for n in runs[1:])
    out.append("leaf arm byte-identical across all %d runs: %s" % (len(runs), "YES" if zero else "NO"))
out.append("")
out.append("# capture lines of the leaf arms (counters are per completed entity update)")
for name in order:
    for ln in lines(name):
        if ln.startswith("capture: leaf-ambient") or ln.startswith("capture: model-lighting") or ln.startswith("capture: camera"):
            out.append("%-20s %s" % (name, ln))
open(out_dir + "pins-and-counters.txt", "w", encoding="utf-8").write("\n".join(out) + "\n")

# ---- 2. per entity cubes (unchanged by the fix: the trace prints before lighting)
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
                     absent=m.group(11), cube=cube, mean=mean, up=up, down=down, empty=empty))
ents.sort(key=lambda e: -e["up"])
o = ["# cp_process_f12, tick 600, --lighting leaf --lighting-trace",
     "# every player instance the trace printed, sorted by measured +Z (up-face) luminance;",
     "# +Z face = how much sky that leaf can see, so it is the indoor/outdoor measurement.",
     "# faces are +X,-X,+Y,-Y,+Z,-Z of the leaf cube, in LINEAR radiance",
     "# these numbers did NOT move when the shear was fixed: the trace branch returns at",
     "# native/src/native_renderer_shaders.cpp:627-629, before modelLightingRgb reads the cube,",
     "# and the printed cube is the CPU-side value, not a readback of the GPU table."]
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
open(out_dir + "entity-cubes-tick600.txt", "w", encoding="utf-8").write("\n".join(o) + "\n")

# ---- 3. the two visible players ----------------------------------------
leaf = Image.open(base + (runs[0] if runs else "leaf") + ".bmp").convert("RGB")
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
picks = [("courtyard player (bottom of frame)", 729, 590),
         ("doorway player (right of frame)", 1072, 544)]
o = ["# the two players the capture actually shows, 1:1 crops, same box in both arms",
     "# located by the red-uniform mask (r>80 and r>1.7*green and r>1.7*blue); the engine",
     "# prints no screen projection, so the box comes from the measured blob, not from a",
     "# projection of the trace origin (see projection-caveat.txt for that attempt).",
     "# leaf column is the post-shear-fix capture; the pre-fix leaf column is gone from this",
     "# file on purpose -- it was one draw of an arm that produced three different frames",
     "# from three identical commands, so a difference against it means nothing."]
for label, bx, by in picks:
    box = (max(0, min(W - CW, bx - 20)), max(0, min(H - CH, by - 60)), 0, 0)
    box = (box[0], box[1], box[0] + CW, box[1] + CH)
    a = stats(leaf.crop(box)); b = stats(basic.crop(box))
    d_img = [round(a["cropMean"][i] - b["cropMean"][i], 2) for i in range(3)]
    o.append("")
    o.append("%s box=%s" % (label, box))
    o.append("  leaf : redFrac=%.4f  redMean=%s  cropMean=%s" % (a["red"] / a["n"], tuple(round(v, 1) for v in a["redMean"]), tuple(round(v, 1) for v in a["cropMean"])))
    o.append("  basic: redFrac=%.4f  redMean=%s  cropMean=%s" % (b["red"] / b["n"], tuple(round(v, 1) for v in b["redMean"]), tuple(round(v, 1) for v in b["cropMean"])))
    o.append("  leaf-minus-basic crop mean (R,G,B) = %s" % d_img)
open(out_dir + "player-crops-stats.txt", "w", encoding="utf-8").write("\n".join(o) + "\n")

# ---- 4. arm diffs ------------------------------------------------------
def diff(pa, pb):
    a = Image.open(pa).convert("RGB"); b = Image.open(pb).convert("RGB")
    if a.size != b.size:
        return dict(size="%s vs %s" % (a.size, b.size))
    xa, xb = a.load(), b.load(); Wd, Hd = a.size
    changed = big = 0; total = 0; mx = 0
    x0 = y0 = 10 ** 9; x1 = y1 = -1
    for y in range(Hd):
        for x in range(Wd):
            ca, cb = xa[x, y], xb[x, y]
            d = max(abs(ca[0] - cb[0]), abs(ca[1] - cb[1]), abs(ca[2] - cb[2]))
            if d:
                changed += 1
                x0 = min(x0, x); y0 = min(y0, y); x1 = max(x1, x); y1 = max(y1, y)
            if d >= 16: big += 1
            total += d
            if d > mx: mx = d
    return dict(n=Wd * Hd, changed=changed, big=big, mean=total / (Wd * Hd), max=mx,
                bbox=(x0, y0, x1, y1))

def row(a, b, label, n, list_of_files):
    if not (os.path.exists(list_of_files[0]) and os.path.exists(list_of_files[1])):
        return "%-24s MISSING (%s)" % (label, " / ".join(list_of_files))
    d = diff(*list_of_files)
    if "size" in d:
        return "%-24s SIZE MISMATCH %s" % (label, d["size"])
    return "%-24s changed=%d (%.3f%%) changed>=16=%d meanAbs=%.3f max=%d bbox=(%d,%d)-(%d,%d)" % (
        label, d["changed"], 100.0 * d["changed"] / d["n"], d["big"], d["mean"], d["max"],
        d["bbox"][0], d["bbox"][1], d["bbox"][2], d["bbox"][3])

o = ["# pixel differences between captures of the SAME tick (1264x681 = 860784 px)",
     "# --lighting-trace renders the interpolated normal as colour by design",
     "# (native/src/native_renderer_shaders.cpp:623-629), so its capture is a debug view,",
     "# not a lighting view: the cube values are printed before that shader branch runs.",
     "# leaf1 = the post-shear-fix pin capture."]
first = runs[0] if runs else "leaf"
o.append(row("leaf", "basic", "leaf vs basic", None, (base + first + ".bmp", base + "basic.bmp")))
o.append(row("leaf", "leaf-trace", "leaf vs leaf-trace", None, (base + first + ".bmp", base + "leaf-trace.bmp")))
o.append(row("basic", "leaf-trace", "basic vs leaf-trace", None, (base + "basic.bmp", base + "leaf-trace.bmp")))
o.append(row("bare", "basic", "bare vs basic", None, (base + "bare.bmp", base + "basic.bmp")))
o.append(row("off", "bare", "off vs bare", None, (base + "off.bmp", base + "bare.bmp")))
if len(runs) > 1:
    for n in runs[1:]:
        o.append(row("leaf", n, first + " vs " + n, None, (base + first + ".bmp", base + n + ".bmp")))
if os.path.exists(base + "leaf-pre-fix.bmp"):
    o.append(row("leaf", "pre-fix", "pre-fix leaf vs pin leaf", None,
                 (base + "leaf-pre-fix.bmp", base + first + ".bmp")))
open(out_dir + "arm-diffs.txt", "w", encoding="utf-8").write("\n".join(o) + "\n")

# ---- 5. screen means and clipping --------------------------------------
def screen(p):
    im = Image.open(p).convert("RGB"); px = im.load(); w, h = im.size
    n = w * h; sr = sg = sb = 0; all255 = any255 = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            sr += r; sg += g; sb += b
            if r == 255 and g == 255 and b == 255: all255 += 1
            if r == 255 or g == 255 or b == 255: any255 += 1
    return dict(n=n, mean=(sr / n, sg / n, sb / n), all255=all255, any255=any255)

o = ["# whole-frame statistics of the same tick, post-shear-fix captures"]
cast = [(n, base + n + ".bmp") for n in ("bare", "off", "basic")] + [(first, base + first + ".bmp")]
if os.path.exists(base + "leaf-pre-fix.bmp"):
    cast.append(("leaf-pre-fix", base + "leaf-pre-fix.bmp"))
st = {}
for name, p in cast:
    if not os.path.exists(p):
        continue
    s = screen(p); st[name] = s
    o.append("%-13s mean=(%7.3f,%7.3f,%7.3f) all255=%d (%.4f%%) any255=%d (%.4f%%)" % (
        name, s["mean"][0], s["mean"][1], s["mean"][2], s["all255"], 100.0 * s["all255"] / s["n"],
        s["any255"], 100.0 * s["any255"] / s["n"]))
if "basic" in st and first in st:
    d = [st[first]["mean"][i] - st["basic"]["mean"][i] for i in range(3)]
    o.append("leaf-minus-basic full-frame mean per channel = %.3f %.3f %.3f" % (d[0], d[1], d[2]))
    o.append("leaf-only saturated pixels (any255) = %d" % (st[first]["any255"] - st["basic"]["any255"]))
open(out_dir + "clip-measurement.txt", "w", encoding="utf-8").write("\n".join(o) + "\n")
print("WROTE pins-and-counters.txt entity-cubes-tick600.txt player-crops-stats.txt "
      "arm-diffs.txt clip-measurement.txt into " + out_dir)
