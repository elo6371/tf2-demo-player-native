#!/usr/bin/env python3
"""N14 basemap: render what a 160x160 minimap panel WOULD show if it drew BSP linework.

Why this exists at all: the product panel currently draws only a background, four
borders, control-point rings and a viewer dot (native/src/main_hud.cpp
hudMinimapCompose). "A dot on a sign" cannot replace a readable basemap, so before any
product change is proposed this script answers three questions with numbers instead of
opinion:

  1 SHAPE     At the panel's real resolution (160x160 px), does the BSP floor geometry
              form a shape a player can recognise? Answered by eye from the contact
              sheet, and quantitatively by the pairwise difference between maps -- two
              maps that render to nearly the same picture cannot both be recognisable.
  2 FIT       How much of the panel's square is filled with floor, and how much is dead
              band, when the projection is the equal-aspect centred one the header
              describes?
  3 FLOOR     Multi-level maps: what fraction of the visible floor is covered by other
              floor above it? A single top-down silhouette merges floors, and a player
              who reads a merged silhouette as one level will misjudge where they are.

Projection here is computed from the declared rectangle alone (equal aspect, centred,
+y north, +y down on the panel) and is NOT taken from the product's helper -- the whole
point is that an independent implementation agreeing with the product is evidence, and
one calling it is not.
"""
import importlib.util
import json
import os
import re
import sys

MAP_ROW_RE = re.compile(
    r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(\w+),\s*(\d+)\}')


def load_oracle():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("bsp_oracle_mod",
                                                  os.path.join(here, "bsp_oracle.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def my_projection(rect, w, h):
    """Equal-aspect centred fit, written from the stated convention, not copied."""
    minx, miny, maxx, maxy = rect
    spanx, spany = maxx - minx, maxy - miny
    scale = min(w / spanx, h / spany)
    offx = (w - spanx * scale) * 0.5 - minx * scale
    # +y is north (up in world), panel +y is down: flip about the rect's top edge
    offy = (h - spany * scale) * 0.5 + maxy * scale
    return scale, offx, offy


def to_px(scale, offx, offy, wx, wy):
    return wx * scale + offx, offy - wy * scale


def render_panel(m, rect, size):
    """Fill a size x size mask with the map's floor polygons (even-odd)."""
    scale, offx, offy = my_projection(rect, size, size)
    polys = []
    for poly in m["floors"]:
        pts = [to_px(scale, offx, offy, v[0], v[1]) for v in poly]
        if len(pts) >= 3:
            polys.append(pts)
    mask = bytearray(size * size)
    ys = [p[1] for poly in polys for p in poly]
    for yy in range(size):
        yc = yy + 0.5
        xs = []
        for pts in polys:
            n = len(pts)
            for i in range(n):
                x0, y0 = pts[i]
                x1, y1 = pts[(i + 1) % n]
                if (y0 <= yc < y1) or (y1 <= yc < y0):
                    t = (yc - y0) / (y1 - y0)
                    xs.append((x0 + t * (x1 - x0), 1))
                elif y0 == yc == y1:
                    xs.append((x0, 2)); xs.append((x1, 2))
        if not xs:
            continue
        xs.sort()
        row = yy * size
        i = 0
        winding = 0
        n = len(xs)
        while i < n:
            j = i
            while j < n and xs[j][0] == xs[i][0]:
                winding += 1 if xs[j][1] == 2 else 0
                j += 1
            i = j
        # even-odd scanline via crossing parity
        spans = sorted(x for x, _ in xs)
        for k in range(0, len(spans) - 1, 2):
            xa, xb = spans[k], spans[k + 1]
            ia = max(0, int(round(xa)))
            ib = min(size - 1, int(round(xb)))
            for xx in range(ia, ib + 1):
                mask[row + xx] = 1
    return mask, (scale, offx, offy)


def write_png(path, pixels, w, h, palette):
    """Minimal RGB PNG writer (zlib only)."""
    import zlib
    import struct
    raw = bytearray()
    for yy in range(h):
        raw.append(0)
        for xx in range(w):
            v = pixels[yy * w + xx]
            raw.extend(palette[v] if v < len(palette) else (0, 0, 0))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    header, out_json = sys.argv[1], sys.argv[2]
    out_dir = sys.argv[3]
    roots, i = [], 4
    while i < len(sys.argv):
        if sys.argv[i] == "--maps-dir":
            roots.append(sys.argv[i + 1]); i += 2
        else:
            i += 1
    text = open(header, encoding="utf-8").read()
    rows = [{"map": m.group(1), "rect": [float(m.group(2)), float(m.group(3)),
                                         float(m.group(4)), float(m.group(5))]}
            for m in MAP_ROW_RE.finditer(text)]
    mod = load_oracle()
    os.makedirs(out_dir, exist_ok=True)
    SIZE = 160
    BG = (13, 15, 20)
    FLOOR = (96, 108, 122)
    EDGE = (168, 182, 196)
    palette = [BG, FLOOR, EDGE]

    report = {"header": header, "panel_px": SIZE, "maps": []}
    masks = {}
    for row in rows:
        name, rect = row["map"], row["rect"]
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand; break
        if bsp is None:
            report["maps"].append({"map": name, "error": "bsp not found"}); continue
        m = mod.read_bsp(bsp)
        mask, proj = render_panel(m, rect, SIZE)
        masks[name] = mask
        filled = sum(mask)
        # edge pixels: floor next to non-floor
        edges = 0
        for yy in range(SIZE):
            for xx in range(SIZE):
                if not mask[yy * SIZE + xx]:
                    continue
                if (xx == 0 or xx == SIZE - 1 or yy == 0 or yy == SIZE - 1
                        or not mask[yy * SIZE + xx + 1] or not mask[yy * SIZE + xx - 1]
                        or not mask[(yy + 1) * SIZE + xx] or not mask[(yy - 1) * SIZE + xx]):
                    edges += 1
        px = bytearray(SIZE * SIZE)
        for k in range(SIZE * SIZE):
            px[k] = 1 if mask[k] else 0
        write_png(os.path.join(out_dir, name + ".png"), px, SIZE, SIZE, palette)
        report["maps"].append({
            "map": name, "declared_rect": rect,
            "projection": {"scale": round(proj[0], 6), "offx": round(proj[1], 4),
                           "offy": round(proj[2], 4)},
            "floor_fill_fraction": round(filled / (SIZE * SIZE), 4),
            "floor_px": filled, "edge_px": edges,
            "edge_density": round(edges / max(1, filled), 4),
        })

    # pairwise difference: two maps that render nearly identically cannot both be readable
    names = [r["map"] for r in report["maps"] if r["map"] in masks]
    pairs = []
    for a in range(len(names)):
        for b in range(a + 1, len(names)):
            ma, mb = masks[names[a]], masks[names[b]]
            diff = sum(1 for k in range(SIZE * SIZE) if ma[k] != mb[k])
            pairs.append({"a": names[a], "b": names[b],
                          "differing_px": diff,
                          "fraction_of_panel": round(diff / (SIZE * SIZE), 4)})
    pairs.sort(key=lambda p: p["differing_px"])
    report["pairwise"] = pairs
    report["most_similar_pairs"] = pairs[:5]

    # contact sheet: one row per map, 160px panel, 3x nearest-neighbour upscale
    COLS, SCALE = 4, 3
    rows_n = (len(names) + COLS - 1) // COLS
    W, H = COLS * SIZE * SCALE, rows_n * SIZE * SCALE
    sheet = bytearray(W * H)
    for idx, name in enumerate(names):
        cx, cy = (idx % COLS) * SIZE * SCALE, (idx // COLS) * SIZE * SCALE
        mk = masks[name]
        for yy in range(SIZE * SCALE):
            for xx in range(SIZE * SCALE):
                sheet[(cy + yy) * W + cx + xx] = mk[(yy // SCALE) * SIZE + (xx // SCALE)]
    write_png(os.path.join(out_dir, "contact-sheet.png"), sheet, W, H, palette)

    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    print("%-20s %-8s %-8s %-7s" % ("map", "fill", "edgeDens", "scale"))
    for mp in report["maps"]:
        if "error" in mp:
            print("%-20s ERROR" % mp["map"]); continue
        print("%-20s %-8.3f %-8.3f %-7.4f" % (mp["map"], mp["floor_fill_fraction"],
                                             mp["edge_density"], mp["projection"]["scale"]))
    print("\nmost similar pairs (fewest differing pixels):")
    for p in report["most_similar_pairs"]:
        print("  %-20s vs %-20s %5d px  %.3f" % (p["a"], p["b"], p["differing_px"],
                                                 p["fraction_of_panel"]))


if __name__ == "__main__":
    main()
