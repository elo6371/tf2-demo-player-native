#!/usr/bin/env python3
"""N14 basemap cost: how many Rect primitives does a BSP-derived basemap need?

The product's HUD can draw exactly three primitive kinds (HudPrimitiveKind: Rect=0,
Text=1, Circle=2 -- native/include/renderer*.h) and there is no line or polygon
primitive. So any basemap must be emitted as a run of Rects: for each panel row,
one Rect per horizontal span of "floor here". A 160x160 solid silhouette would be
160 Rects if every row were full; real maps are sparser, and the number of runs is
the number that decides whether this is a sane shape budget or a 5000-quad monster.

Everything is measured at the panel's real pixel size (160x160, 2 px border) so the
numbers are the ones the product would actually pay, and each variant is scored on
two axes that trade against each other:
  cost        runs (Rect primitives) -- what the gate's shapes== budget must grow by
  identity    mean pairwise panel difference -- if cleanup makes every map look
              the same, a cheap basemap has bought nothing.
"""
import importlib.util
import json
import os
import sys
import zlib
import struct

PANEL = 160
BORDER = 2      # kMinimapBorderWidth
INTERIOR = PANEL - 2 * BORDER


def load_oracle():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("bsp_oracle_mod",
                                                  os.path.join(here, "bsp_oracle.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def poly_area(poly):
    """Footprint area in world units^2 (Newell on the xy projection)."""
    a = 0.0
    n = len(poly)
    for i in range(n):
        x0, y0, _z0 = poly[i]
        x1, y1, _z1 = poly[(i + 1) % n]
        a += x0 * y1 - x1 * y0
    return abs(a) * 0.5


def fill_mask(polys, rect, size, min_area=0.0, zmin=None, zmax=None):
    """Even-odd scanline fill of upward faces into a size x size mask."""
    minx, miny, maxx, maxy = rect
    spanx, spany = maxx - minx, maxy - miny
    scale = min(size / spanx, size / spany)
    # The panel is a SQUARE and the world rect is not, so the unused axis has dead
    # bands. Emitting the full square (not the spanned part) is what the product
    # actually shows, and it makes every map's mask the same size, so two maps can
    # be compared pixel-for-pixel instead of by inspection.
    w = h = size
    offx = (w - spanx * scale) * 0.5 - minx * scale
    offy = (h - spany * scale) * 0.5 + maxy * scale
    mask = bytearray(w * h)
    kept = 0
    for poly in polys:
        if min_area > 0.0 and poly_area(poly) < min_area:
            continue
        zs = [v[2] for v in poly]
        zc = sum(zs) / len(zs)
        if zmin is not None and zc < zmin:
            continue
        if zmax is not None and zc > zmax:
            continue
        kept += 1
        pts = [(offx + v[0] * scale, offy - v[1] * scale) for v in poly]
        ys = [p[1] for p in pts]
        ylo = max(0, int(min(ys)) - 1)
        yhi = min(h - 1, int(max(ys)) + 1)
        for yy in range(ylo, yhi + 1):
            yc = yy + 0.5
            xs = []
            n = len(pts)
            for k in range(n):
                ax, ay = pts[k]
                bx, by = pts[(k + 1) % n]
                if (ay <= yc < by) or (by <= yc < ay):
                    t = (yc - ay) / (by - ay)
                    xs.append(ax + t * (bx - ax))
            if len(xs) < 2:
                continue
            xs.sort()
            base = yy * w
            for k in range(0, len(xs) - 1, 2):
                xa = max(0, int(xs[k]))
                xb = min(w - 1, int(xs[k + 1]))
                for xx in range(xa, xb + 1):
                    mask[base + xx] = 1
    return mask, w, h, scale, kept


def runs_of(mask, w, h):
    """Horizontal runs = one Rect primitive each."""
    runs = 0
    for yy in range(h):
        base = yy * w
        prev = 0
        for xx in range(w):
            cur = mask[base + xx]
            if cur and not prev:
                runs += 1
            prev = cur
    return runs


def panel_diff(a, b, w, h):
    d = 0
    for i in range(w * h):
        if a[i] != b[i]:
            d += 1
    return d


def write_png(path, mask, w, h, scale=3, fg=(232, 226, 210), bg=(13, 15, 20)):
    W, H = w * scale, h * scale
    raw = bytearray()
    for yy in range(H):
        raw.append(0)
        sy = yy // scale
        for xx in range(W):
            sx = xx // scale
            c = fg if mask[sy * w + sx] else bg
            raw += bytes(c)
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    header, out_json, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    roots, i = [], 4
    while i < len(sys.argv):
        if sys.argv[i] == "--maps-dir":
            roots.append(sys.argv[i + 1]); i += 2
        else:
            i += 1
    import re
    text = open(header, encoding="utf-8").read()
    rows = [(m.group(1), [float(m.group(2)), float(m.group(3)),
                          float(m.group(4)), float(m.group(5))])
            for m in re.finditer(r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,', text)]
    mod = load_oracle()
    os.makedirs(out_dir, exist_ok=True)
    # min face footprint in world u^2. A player is 83 u tall, ~32 u wide: 1024 u^2 is
    # a 32x32 patch, i.e. "a place someone can stand".
    VARIANTS = [("raw", dict(min_area=0.0)),
                ("area1k", dict(min_area=1024.0)),
                ("area4k", dict(min_area=4096.0))]
    report = {"panel": PANEL, "border": BORDER, "interior": INTERIOR,
              "primitive_kinds": "Rect=0 Text=1 Circle=2 (no line/polygon)", "maps": []}
    masks = {}
    for name, rect in rows:
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand; break
        if bsp is None:
            report["maps"].append({"map": name, "error": "bsp not found"}); continue
        m = mod.read_bsp(bsp)
        entry = {"map": name, "rect": rect, "variants": {}}
        for vname, kw in VARIANTS:
            mask, w, h, scale, kept = fill_mask(m["floors"], rect, INTERIOR, **kw)
            runs = runs_of(mask, w, h)
            filled = sum(mask)
            entry["variants"][vname] = {
                "faces_kept": kept, "runs": runs, "fill": round(filled / (w * h), 4),
                "px_per_u": round(scale, 5),
            }
            masks[(name, vname)] = (mask, w, h)
            if vname == "area1k":
                write_png(os.path.join(out_dir, name + ".png"), mask, w, h)
        report["maps"].append(entry)

    # identity: does the cheapest cleanup still tell the maps apart?
    names = [e["map"] for e in report["maps"] if "error" not in e]
    for vname, _kw in VARIANTS:
        worst = None
        tot = 0
        n = 0
        for a in range(len(names)):
            for b in range(a + 1, len(names)):
                ma, w, h = masks[(names[a], vname)]
                mb, _w2, _h2 = masks[(names[b], vname)]
                d = panel_diff(ma, mb, w, h)
                frac = d / (w * h)
                tot += frac; n += 1
                if worst is None or frac < worst[2]:
                    worst = (names[a], names[b], frac)
        report.setdefault("identity", {})[vname] = {
            "pairs": n,
            "mean_diff_fraction": round(tot / n, 4) if n else 0.0,
            "most_similar_pair": list(worst) if worst else None,
            "most_similar_fraction": round(worst[2], 4) if worst else 0.0,
        }
    json.dump(report, open(out_json, "w", encoding="utf-8"), indent=1)
    print("%-20s %-7s %-6s %-7s %-7s %-6s" % ("map", "v", "faces", "runs", "fill", "px/u"))
    for e in report["maps"]:
        if "error" in e:
            print("%-20s ERROR %s" % (e["map"], e["error"])); continue
        for vname, _kw in VARIANTS:
            v = e["variants"][vname]
            print("%-20s %-7s %-6d %-7d %-7.3f %-6.4f" % (e["map"], vname, v["faces_kept"],
                                                          v["runs"], v["fill"], v["px_per_u"]))
    print(json.dumps(report["identity"], indent=1))


if __name__ == "__main__":
    main()
