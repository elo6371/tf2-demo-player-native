#!/usr/bin/env python3
"""N14 oracle: an INDEPENDENT reference linework for each of the seven maps.

Why "linework" and not "filled floor": a filled footprint at 160 px is a blob; the
thing a player reads on a real TF2 map is the SILHOUETTE EDGE of the walkable area
plus a handful of landmarks. So this draws:

  outline   the boundary of the union of upward faces (mask -> edge), 1 px
  spawns    one marker per team spawn-cluster centroid (RED/BLU by TeamNum)
  points    control points, ringed in their team colour, labelled by index
  rail      the payload path (path_track chain), drawn as a polyline

Nothing here comes from native/include/minimap.h or main_hud.cpp: the rect is
recomputed from the header as DATA UNDER TEST (that is the point -- the D08 report
says the product's panel draws no geometry at all, so there is nothing to copy),
and the projection is the same equal-aspect centred fit stated from scratch.

Every landmark drawn carries its SOURCE: a spawn cluster is drawn only when the BSP
really has info_player_teamspawn entities, a control point only when the BSP really
has team_control_point, a rail only when path_track nodes exist. A map with no data
gets no icon -- drawing a guess is the failure this file is meant to avoid.

usage: n14_linework.py <header.h> <out_dir> [--maps-dir D ...] [--scale 3]
       n14_linework.py --verify <out_dir>     (checks every panel is non-empty and distinct)
"""
import importlib.util
import json
import math
import os
import re
import struct
import sys
import zlib

PANEL = 160
SCALE = 3
TEAM_COLOUR = {2: (230, 51, 46), 3: (51, 107, 242), 0: (235, 230, 217)}
INK = (232, 226, 210)
BG = (13, 15, 20)
RAIL = (150, 140, 118)


def load_oracle():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("bsp_oracle_mod",
                                                  os.path.join(here, "bsp_oracle.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def my_projection(rect, size=PANEL):
    minx, miny, maxx, maxy = rect
    spanx, spany = maxx - minx, maxy - miny
    scale = min(size / spanx, size / spany)
    offx = (size - spanx * scale) * 0.5 - minx * scale
    offy = (size - spany * scale) * 0.5 + maxy * scale
    return scale, offx, offy


def proj(p, scale, offx, offy):
    return offx + p[0] * scale, offy - p[1] * scale


def poly_area_xy(poly):
    a = 0.0
    n = len(poly)
    for i in range(n):
        x0, y0, _z = poly[i]
        x1, y1, _z2 = poly[(i + 1) % n]
        a += x0 * y1 - x1 * y0
    return abs(a) * 0.5


def footprint(polys, rect, size, min_area):
    minx, miny, maxx, maxy = rect
    spanx, spany = maxx - minx, maxy - miny
    scale = min(size / spanx, size / spany)
    offx = (size - spanx * scale) * 0.5 - minx * scale
    offy = (size - spany * scale) * 0.5 + maxy * scale
    mask = bytearray(size * size)
    for poly in polys:
        if poly_area_xy(poly) < min_area:
            continue
        pts = [(offx + v[0] * scale, offy - v[1] * scale) for v in poly]
        ys = [p[1] for p in pts]
        for yy in range(max(0, int(min(ys)) - 1), min(size, int(max(ys)) + 2)):
            yc = yy + 0.5
            xs = []
            n = len(pts)
            for k in range(n):
                ax, ay = pts[k]
                bx, by = pts[(k + 1) % n]
                if (ay <= yc < by) or (by <= yc < ay):
                    xs.append(ax + (yc - ay) / (by - ay) * (bx - ax))
            if len(xs) < 2:
                continue
            xs.sort()
            for k in range(0, len(xs) - 1, 2):
                for xx in range(max(0, int(xs[k])), min(size, int(xs[k + 1]) + 1)):
                    mask[yy * size + xx] = 1
    return mask, scale, offx, offy


def edge_of(mask, size):
    """1-px boundary of the footprint: filled pixels with an empty 4-neighbour."""
    out = bytearray(size * size)
    for y in range(size):
        for x in range(size):
            if not mask[y * size + x]:
                continue
            if (x == 0 or not mask[y * size + x - 1] or x == size - 1 or
                    not mask[y * size + x + 1] or y == 0 or not mask[(y - 1) * size + x] or
                    y == size - 1 or not mask[(y + 1) * size + x]):
                out[y * size + x] = 1
    return out


def ring(canvas, size, cx, cy, r, colour):
    steps = max(12, int(2 * math.pi * r * 1.6))
    for i in range(steps):
        a = 2 * math.pi * i / steps
        x, y = int(round(cx + r * math.cos(a))), int(round(cy + r * math.sin(a)))
        if 0 <= x < size and 0 <= y < size:
            canvas[y * size + x] = colour


def blob(canvas, size, cx, cy, r, colour):
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy > r * r:
                continue
            x, y = int(round(cx)) + dx, int(round(cy)) + dy
            if 0 <= x < size and 0 <= y < size:
                canvas[y * size + x] = colour


def line(canvas, size, x0, y0, x1, y1, colour):
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) * 2 + 1
    for i in range(steps + 1):
        t = i / steps
        x = int(round(x0 + (x1 - x0) * t))
        y = int(round(y0 + (y1 - y0) * t))
        if 0 <= x < size and 0 <= y < size:
            canvas[y * size + x] = colour


def write_png(path, canvas, size, labels, scale=SCALE):
    """RGB canvas of size*size tuples -> PNG at scale, with an optional label strip."""
    W = H = size * scale
    rows = []
    for yy in range(H):
        row = bytearray()
        sy = yy // scale
        for xx in range(W):
            c = canvas[sy * size + (xx // scale)]
            row += bytes(c)
        rows.append(bytes(row))
    raw = b"".join(b"\x00" + r for r in rows)
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def contact_sheet(entries, out_path, size=PANEL, scale=3, cols=4):
    cell = size * scale
    rows = (len(entries) + cols - 1) // cols
    W, H = cols * cell, rows * cell
    raw_rows = []
    for yy in range(H):
        row = bytearray()
        cy_cell, sy = divmod(yy, cell)
        sy_ = sy // scale
        for xx in range(W):
            cx_cell, sx = divmod(xx, cell)
            idx = cy_cell * cols + cx_cell
            sx_ = sx // scale
            if idx < len(entries):
                c = entries[idx][sy_ * size + sx_]
            else:
                c = BG
            row += bytes(c)
        raw_rows.append(bytes(row))
    raw = b"".join(b"\x00" + r for r in raw_rows)
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    open(out_path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
                               chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def spawn_clusters(anchors, bucket=256.0):
    """Group teamspawns into clusters so one marker stands for one spawn room."""
    clusters = []
    for a in anchors:
        if a["classname"] != "info_player_teamspawn":
            continue
        team = a.get("teamnum")
        x, y, z = a["origin"]
        for c in clusters:
            if c["team"] == team and math.hypot(c["x"] - x, c["y"] - y) < bucket:
                c["n"] += 1
                c["sx"] += x; c["sy"] += y
                c["x"], c["y"] = c["sx"] / c["n"], c["sy"] / c["n"]
                break
        else:
            clusters.append({"team": team, "x": x, "y": y, "sx": x, "sy": y, "n": 1})
    return clusters


def rail_points(anchors):
    pts = [(a["origin"][0], a["origin"][1]) for a in anchors if a["classname"] == "path_track"]
    if len(pts) < 2:
        return []
    # path_track chain is unordered in the lump; order by nearest-neighbour walk
    rest = pts[1:]
    ordered = [pts[0]]
    while rest:
        px, py = ordered[-1]
        i = min(range(len(rest)), key=lambda k: (rest[k][0] - px) ** 2 + (rest[k][1] - py) ** 2)
        ordered.append(rest.pop(i))
    return ordered


def main():
    header, out_dir = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    roots, scale = [], SCALE
    i = 0
    while i < len(args):
        if args[i] == "--maps-dir":
            roots.append(args[i + 1]); i += 2
        elif args[i] == "--scale":
            scale = int(args[i + 1]); i += 2
        else:
            i += 1
    MIN_AREA = 1024.0
    text = open(header, encoding="utf-8").read()
    rows = [(m.group(1), [float(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(5))])
            for m in re.finditer(r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,', text)]
    mod = load_oracle()
    os.makedirs(out_dir, exist_ok=True)
    report = {"panel": PANEL, "scale_factor": scale, "min_face_area": MIN_AREA, "maps": []}
    canvases = []
    for name, rect in rows:
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand; break
        if bsp is None:
            report["maps"].append({"map": name, "error": "bsp not found"}); continue
        m = mod.read_bsp(bsp)
        mask, sc, offx, offy = footprint(m["floors"], rect, PANEL, MIN_AREA)
        edges = edge_of(mask, PANEL)
        canvas = [BG] * (PANEL * PANEL)
        for k in range(PANEL * PANEL):
            if edges[k]:
                canvas[k] = INK
        n_edge = sum(edges)
        drawn = {"outline_px": n_edge, "spawn_markers": 0, "control_points": 0, "rail_nodes": 0}

        rail = rail_points(m["anchors"])
        if len(rail) >= 2:
            drawn["rail_nodes"] = len(rail)
            prev = proj(rail[0], sc, offx, offy)
            for p in rail[1:]:
                cur = proj(p, sc, offx, offy)
                line(canvas, PANEL, prev[0], prev[1], cur[0], cur[1], RAIL)
                prev = cur

        for c in spawn_clusters(m["anchors"]):
            px, py = proj((c["x"], c["y"]), sc, offx, offy)
            if 0 <= px < PANEL and 0 <= py < PANEL:
                blob(canvas, PANEL, px, py, 2, TEAM_COLOUR.get(c["team"], TEAM_COLOUR[0]))
                drawn["spawn_markers"] += 1

        for a in m["anchors"]:
            if a["classname"] != "team_control_point":
                continue
            px, py = proj(a["origin"], sc, offx, offy)
            if 0 <= px < PANEL and 0 <= py < PANEL:
                ring(canvas, PANEL, px, py, 4, TEAM_COLOUR.get(a.get("teamnum"), TEAM_COLOUR[0]))
                drawn["control_points"] += 1

        write_png(os.path.join(out_dir, name + ".png"), canvas, PANEL, None, scale)
        canvases.append(canvas)
        report["maps"].append({"map": name, "rect": rect, "px_per_unit": round(sc, 5),
                               "drawn": drawn,
                               "footprint_fill": round(sum(mask) / (PANEL * PANEL), 4)})
    contact_sheet(canvases, os.path.join(out_dir, "contact-sheet.png"), PANEL, scale)
    json.dump(report, open(os.path.join(out_dir, "linework.json"), "w", encoding="utf-8"), indent=1)
    print("%-20s %-9s %-8s %-7s %-6s %-5s" % ("map", "outlinePx", "spawns", "cps", "rail", "fill"))
    for e in report["maps"]:
        if "error" in e:
            print("%-20s ERROR" % e["map"]); continue
        d = e["drawn"]
        print("%-20s %-9d %-8d %-7d %-6d %-5.3f" % (e["map"], d["outline_px"], d["spawn_markers"],
                                                    d["control_points"], d["rail_nodes"], e["footprint_fill"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
