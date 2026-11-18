#!/usr/bin/env python3
"""N14 floor strategy: how badly would one top-down silhouette mislead the player?

A minimap that draws every upward face of a BSP merges floors. On a multi-level map
(two bases, catwalks above a bridge, a lower route under a ramp) the merged shape is
still readable as a comp layout, but the player cannot tell which level the dot is on.
This measures the size of that problem instead of arguing about it:

  SHADOWED  fraction of floor area that has other floor directly above it, computed by
            rasterising each floor polygon's height range into a coarse grid and
            counting cells whose height span exceeds a level threshold. A cell with
            floor at z and also floor at z+800 has two levels stacked in one panel pixel.
  LEVELS    number of distinct height bands (clustered by rounded z / 128) that carry
            at least 2% of floor area -- an upper bound on how many floor layers a
            panel would need to disambiguate.

Not a verdict machine: the product may legitimately choose "silhouette only" and put
the level problem on the caller. This reports the number so that choice is informed.
"""
import importlib.util
import json
import os
import re
import sys


def load_oracle():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("bsp_oracle_mod",
                                                  os.path.join(here, "bsp_oracle.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MAP_ROW_RE = re.compile(
    r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(\w+),\s*(\d+)\}')
LEVEL_THRESHOLD = 400.0   # world units; a player is ~83 u tall, a floor-to-floor is >256


def main():
    header, out_json = sys.argv[1], sys.argv[2]
    roots, i = [], 3
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
    report = {"header": header, "level_threshold": LEVEL_THRESHOLD, "maps": []}

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
        spanx = rect[2] - rect[0]
        spany = rect[3] - rect[1]
        GRID = 256
        scale = min(GRID / spanx, GRID / spany)
        w = max(1, int(spanx * scale))
        h = max(1, int(spany * scale))
        lo = [1e9] * (w * h)
        hi = [-1e9] * (w * h)
        band_area = {}
        for poly in m["floors"]:
            zs = [v[2] for v in poly]
            z0, z1 = min(zs), max(zs)
            # rasterise the polygon footprint into the grid, then widen the z range
            pts = [((v[0] - rect[0]) * scale, v[1] * scale) for v in poly]
            ys = [p[1] for p in pts]
            y0 = max(0, int((min(ys)) * 1.0) + 0)
            ys_max = min(h - 1, int(max(ys)) + 1)
            band = int(round(sum(zs) / len(zs) / 128.0))
            for yy in range(max(0, int(min(ys)) - 1), min(h, ys_max + 1)):
                yc = (yy + 0.5) / scale
                xs = []
                n = len(poly)
                for k in range(n):
                    ax, ay, _az = poly[k]
                    bx, by, _bz = poly[(k + 1) % n]
                    if (ay <= yc < by) or (by <= yc < ay):
                        t = (yc - ay) / (by - ay)
                        xs.append(ax + t * (bx - ax))
                if len(xs) < 2:
                    continue
                xs.sort()
                for k in range(0, len(xs) - 1, 2):
                    xa = int((xs[k] - rect[0]) * scale)
                    xb = int((xs[k + 1] - rect[0]) * scale)
                    for xx in range(max(0, xa), min(w, xb + 1)):
                        idx = yy * w + xx
                        if z0 < lo[idx]: lo[idx] = z0
                        if z1 > hi[idx]: hi[idx] = z1
                        band_area[band] = band_area.get(band, 0) + 1
        filled = stacked = 0
        max_span = 0.0
        spans = []
        for idx in range(w * h):
            if hi[idx] < lo[idx]:
                continue
            filled += 1
            span = hi[idx] - lo[idx]
            if span > max_span: max_span = span
            spans.append(span)
            if span > LEVEL_THRESHOLD:
                stacked += 1
        spans.sort()
        total_band = sum(band_area.values()) or 1
        levels = sorted(b for b, c in band_area.items() if c / total_band >= 0.02)
        report["maps"].append({
            "map": name, "declared_rect": rect, "grid": [w, h],
            "floor_cells": filled,
            "stacked_cells": stacked,
            "stacked_fraction": round(stacked / filled, 4) if filled else 0.0,
            "max_height_span_units": round(max_span, 1),
            "median_height_span_units": round(spans[len(spans) // 2], 1) if spans else 0.0,
            "p90_height_span_units": round(spans[int(len(spans) * 0.9)], 1) if spans else 0.0,
            "height_bands_over_2pct": len(levels),
            "height_bands": levels,
        })

    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    print("%-20s %-8s %-9s %-8s %-8s %s" % ("map", "cells", "stackedFrac", "maxSpan", "p90Span", "levels>=2%"))
    for mp in report["maps"]:
        if "error" in mp:
            print("%-20s ERROR" % mp["map"]); continue
        print("%-20s %-8d %-9.3f %-8.0f %-8.0f %d" % (mp["map"], mp["floor_cells"],
              mp["stacked_fraction"], mp["max_height_span_units"],
              mp["p90_height_span_units"], mp["height_bands_over_2pct"]))


if __name__ == "__main__":
    main()
