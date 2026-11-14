#!/usr/bin/env python
"""D08 five-map minimap check: the panel, its objectives, and the orientation.

Reads the frames the capture run wrote and answers, per map:

  P  the panel is where the spec says and nothing outside it changed
  R  every control point the BSP carries has a ring at the projected position
  O  the ring layout keeps the world's orientation (x same sign, y mirrored),
     checked pairwise against the BSP entity origins, not against the bounds

Bounds are the product's declared play-area rectangle (minimap.h), not the BSP
world hull: they are a design choice, so they are assumed here. Origins are read
from the BSP entity lump and are independent of the product table.
"""
import math
import struct
import sys

PANEL, INSET, MARGIN = 160, 8, 4

# bounds: native/include/minimap.h MinimapMapRow (declared play area)
# points: BSP team_control_point origins, extracted independently (.scratch/g1-minimap-five/bsp-tables.txt)
MAPS = {
    "cp_process_f12": dict(
        bounds=(-5056.0, -4608.0, 5056.0, 4608.0),
        points=[("cp_blu1", 3, -3540.0, -1280.0), ("cp_blu2", 3, -1664.0, -2176.0),
                ("cp_3", 0, 0.0, 0.0), ("cp_red2", 2, 1664.0, 2176.0),
                ("cp_red1", 2, 3540.0, 1280.0)]),
    "cp_gullywash_f9": dict(
        bounds=(-5202.0, -3990.0, 5202.0, 3990.0),
        points=[("cap_blue_1", 3, 3768.0, -757.0), ("cap_blue_2", 3, 1041.0, -1612.0),
                ("cap_center", 0, 0.0, 0.0), ("cap_red_2", 2, -1041.0, 1612.0),
                ("cap_red_1", 2, -3766.2, 758.0)]),
    "cp_sultry_b8a": dict(
        bounds=(-4832.0, -6016.0, 4768.0, 5984.0),
        points=[("cp_blu1", 3, -2719.0, 3892.0), ("cp_blu2", 3, -2536.0, 796.0),
                ("cp_mid", 0, -32.0, -16.0), ("cp_red2", 2, 2472.0, -824.0),
                ("cp_red1", 2, 2657.0, -3920.0)]),
    "koth_product_final": dict(
        bounds=(-5426.0, -4704.0, 2304.0, 4704.0),
        points=[("control_point_1", 0, -1536.0, 0.0)]),
    "koth_bagel_rc11": dict(
        bounds=(-4256.0, -2816.0, 4256.0, 3072.0),
        points=[("cp_koth", 0, 0.0, 0.0)]),
    "pl_upward_f12": dict(
        bounds=(-3072.0, -4096.0, 4096.0, 3072.0),
        points=[("cap_1", 0, 1024.0, -1536.0), ("cap_2", 0, 1536.0, 1088.0),
                ("cap_3", 0, -1024.0, 624.0), ("cap_4", 0, 512.0, -384.0)]),
    "cp_sunshine": dict(
        bounds=(-11008.0, 128.0, -256.0, 10368.0),
        points=[("cp_blu1", 3, -7632.0, 8940.0), ("cp_blu2", 3, -7704.0, 6824.0),
                ("cp_3", 0, -5632.01, 5248.0), ("cp_red2", 2, -3560.0, 3672.0),
                ("cp_red1", 2, -3632.0, 1556.0)]),
}

TEAM_CLASS = {3: "blu", 2: "red", 0: "neutral"}


def read_bmp(path):
    data = open(path, "rb").read(2048)
    if len(data) < 54 or data[0:2] != b"BM":
        sys.exit("not a BMP: " + path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    depth = struct.unpack_from("<H", data, 28)[0]
    if depth != 24 or width <= 0 or height <= 0:
        sys.exit("expected 24bpp, got %d (%dx%d)" % (depth, width, height))
    stride = ((width * 3) + 3) & ~3
    need = offset + stride * height
    raw = open(path, "rb").read(need)
    if len(raw) < need:
        sys.exit("truncated BMP: " + path)
    return width, height, [raw[offset + stride * r: offset + stride * (r + 1)] for r in range(height)]


def rgb(rows, w, h, x, y):
    if x < 0 or y < 0 or x >= w or y >= h:
        return (0, 0, 0)
    r = rows[h - 1 - y]
    i = x * 3
    return (r[i + 2], r[i + 1], r[i])


def classify(c):
    r, g, b = c
    if r < 45 and g < 45 and b < 55:
        return "bg"
    if g > 190 and r < 140 and b < 190:
        return "viewer"
    if r > 150 and g < 110 and b < 110:
        return "red"
    if b > 140 and r < 120 and g < 150:
        return "blu"
    if r > 195 and g > 190 and b > 175:
        return "neutral"
    if 100 <= r <= 195 and abs(r - g) < 30 and abs(g - b) < 30:
        return "border"
    return "other"


def panel_rect(w, h):
    return (w - INSET - MARGIN - PANEL, INSET + MARGIN, PANEL, PANEL)


def projection(bounds, size=PANEL):
    minx, miny, maxx, maxy = bounds
    spanx, spany = maxx - minx, maxy - miny
    scale = min(size / spanx, size / spany)
    offx = (size - spanx * scale) / 2.0 - minx * scale
    offy = (size - spany * scale) / 2.0 + maxy * scale
    return scale, offx, offy


def project(x, y, scale, offx, offy):
    return x * scale + offx, offy - y * scale


def window(rows, w, h, cx, cy, radius=2):
    counts = {}
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            k = classify(rgb(rows, w, h, int(round(cx)) + dx, int(round(cy)) + dy))
            counts[k] = counts.get(k, 0) + 1
    return counts


def panel_stats(on, off):
    w, h, rows_on = read_bmp(on)
    w2, h2, rows_off = read_bmp(off)
    if (w, h) != (w2, h2):
        return None
    x0, y0, pw, ph = panel_rect(w, h)
    inside = outside = 0
    minx = miny = 10 ** 9
    maxx = maxy = -1
    for y in range(h):
        ro, rf = rows_on[h - 1 - y], rows_off[h2 - 1 - y]
        for x in range(w):
            i = x * 3
            if ro[i:i + 3] != rf[i:i + 3]:
                if x0 <= x < x0 + pw and y0 <= y < y0 + ph:
                    inside += 1
                else:
                    outside += 1
                minx = min(minx, x); maxx = max(maxx, x)
                miny = min(miny, y); maxy = max(maxy, y)
    return dict(w=w, h=h, x0=x0, y0=y0, pw=pw, ph=ph, inside=inside, outside=outside,
                bbox=(minx, miny, maxx, maxy), rows=rows_on)


def check(name, out_dir):
    cfg = MAPS[name]
    ok = bad = 0
    fails = []

    def want(cond, label, detail=""):
        nonlocal ok, bad
        if cond:
            ok += 1
        else:
            bad += 1
            fails.append("%s %s" % (label, detail))

    st = panel_stats("%s/%s.bmp" % (out_dir, name), "%s/%s.off.bmp" % (out_dir, name))
    if st is None:
        print("MAP %s FAIL: the two frames are not the same size" % name)
        return 1, 0
    want(st["pw"] == 160 and st["ph"] == 160, "panel size", str((st["pw"], st["ph"])))
    want(20000 <= st["inside"] <= 25600, "pixels changed inside the panel", str(st["inside"]))
    want(st["outside"] == 0, "pixels changed outside the panel", str(st["outside"]))
    want(st["bbox"] == (st["x0"], st["y0"], st["x0"] + st["pw"] - 1, st["y0"] + st["ph"] - 1),
         "changed pixels occupy the panel rect", str(st["bbox"]))

    scale, offx, offy = projection(cfg["bounds"])
    proj = []
    for cp_name, team, x, y in cfg["points"]:
        px, py = project(x, y, scale, offx, offy)
        proj.append((cp_name, team, x, y, px, py))
        ax, ay = st["x0"] + px, st["y0"] + py
        c = window(st["rows"], st["w"], st["h"], ax, ay)
        want(0 <= px <= st["pw"] and 0 <= py <= st["ph"], "%s inside the panel" % cp_name,
             "px=%.2f py=%.2f" % (px, py))
        want(min(px, py, st["pw"] - px, st["ph"] - py) >= 2.0, "%s not on the panel edge" % cp_name,
             "margin=%.2f" % min(px, py, st["pw"] - px, st["ph"] - py))
        klass = TEAM_CLASS[team]
        want(c.get(klass, 0) >= 20, "%s ring is %s at the projected position" % (cp_name, klass),
             "window=%s ax=%.1f ay=%.1f" % (c, ax, ay))

    # Orientation without trusting the bounds: for every pair, the panel's x order
    # must match the world's x order and its y order must be inverted.
    pairs = 0
    for i in range(len(proj)):
        for j in range(i + 1, len(proj)):
            a, b = proj[i], proj[j]
            if abs(a[2] - b[2]) > 1.0:
                pairs += 1
                want((a[4] - b[4]) * (a[2] - b[2]) > 0, "x order %s/%s" % (a[0], b[0]),
                     "world dx=%.1f panel dx=%.2f" % (a[2] - b[2], a[4] - b[4]))
            if abs(a[3] - b[3]) > 1.0:
                pairs += 1
                want((a[5] - b[5]) * (a[3] - b[3]) < 0, "y is mirrored %s/%s" % (a[0], b[0]),
                     "world dy=%.1f panel dy=%.2f" % (a[3] - b[3], a[5] - b[5]))

    verdict = "PASS" if bad == 0 else "FAIL"
    print("MAP %-20s %s checks=%d failed=%d pairs=%d inside=%d outside=%d" % (
        name, verdict, ok + bad, bad, pairs, st["inside"], st["outside"]))
    for f in fails:
        print("   RED %s" % f)
    return ok + bad, bad


def trace_line(out_dir, name):
    path = "%s/%s.trace" % (out_dir, name)
    for line in open(path, "r", errors="replace"):
        # The publish line, not any line that happens to mention the word: the
        # output directory itself is called "minimap-five", and a substring test
        # matched the settings-file line first.
        if "minimap: hud state=" in line:
            return line.strip()
    return ""


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    names = sys.argv[2:] or list(MAPS)
    total = failed = 0
    for name in names:
        line = trace_line(out_dir, name)
        m = {}
        for token in line.split():
            if "=" in token:
                k, v = token.split("=", 1)
                m[k] = v
        expect_cps = len(MAPS[name]["points"])
        pre = []
        if not line:
            pre.append("no minimap line in the trace")
        else:
            if m.get("state") != "ready":
                pre.append("state=%s" % m.get("state"))
            if m.get("map") != name:
                pre.append("map=%s" % m.get("map"))
            if m.get("cps") != str(expect_cps):
                pre.append("cps=%s want %d" % (m.get("cps"), expect_cps))
            if m.get("drawn") != str(expect_cps):
                pre.append("drawn=%s want %d" % (m.get("drawn"), expect_cps))
            if m.get("viewerInside") != "1":
                pre.append("viewerInside=%s" % m.get("viewerInside"))
        o, b = check(name, out_dir)
        total += o
        failed += b + len(pre)
        print("   reading: %s" % line)
        for p in pre:
            print("   RED %s" % p)
        # The paused-panel defect (fixed 2026-11-14) published a second line with
        # reason=no-camera-pose on a stopped clock. Any such line in the run is a
        # regression, even though the frame may still carry the panel.
        stale = [l.strip() for l in open("%s/%s.trace" % (out_dir, name), errors="replace")
                 if "minimap: hud state=" in l and "reason=ok" not in l]
        total += 1
        if stale:
            failed += 1
            print("   RED %d minimap publish lines refused a pose; first: %s" % (len(stale), stale[0]))
    print("MINIMAP-FIVE checks=%d failed=%d" % (total, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
