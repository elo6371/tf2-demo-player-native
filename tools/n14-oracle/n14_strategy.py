#!/usr/bin/env python3
"""N14/D41 strategy cost: three ways to draw a basemap, priced in Rect primitives.

The HUD has Rect and Circle only, so every strategy is a run-length bill:

  A  FILL-ALL      every upward face, filled. Cheapest, but two corridors one above
                   the other merge into one blob -- a player cannot tell which level
                   the dot is on.
  B  EDGE-ALL      the 1-px boundary of the union of all floors. Reads as a map,
                   but stacked floors still overlap into one outline.
  C  EDGE + BANDS  the union outline (so the map's true silhouette is intact) plus
                   the internal boundary of each floor band that carries >= 5% of
                   floor area. C is the only one where a multi-storey map shows its
                   storeys instead of its shadow.

Cost is reported as runs, because one run = one Rect primitive. The comparison that
matters for the doc is C/B: how much does per-floor readability cost on top of the
silhouette we already have to pay for?

usage: n14_strategy.py <header.h> <out.json> [--maps-dir D ...]
"""
import importlib.util
import json
import os
import re
import sys

PANEL = 160
BAND = 256.0        # z bucket, world units
BAND_MIN_SHARE = 0.05
MIN_AREA = 1024.0   # world units^2


def load(path, modname):
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(modname, os.path.join(here, path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def runs(mask, size=PANEL):
    n = 0
    for y in range(size):
        prev = 0
        base = y * size
        for x in range(size):
            cur = mask[base + x]
            if cur and not prev:
                n += 1
            prev = cur
    return n


def main():
    header, out_json = sys.argv[1], sys.argv[2]
    roots, i = [], 3
    while i < len(sys.argv):
        if sys.argv[i] == "--maps-dir":
            roots.append(sys.argv[i + 1]); i += 2
        else:
            i += 1
    text = open(header, encoding="utf-8").read()
    rows = [(m.group(1), [float(m.group(k)) for k in (2, 3, 4, 5)])
            for m in re.finditer(r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,', text)]
    lw = load("n14_linework.py", "lw")
    rl = load("n14_runlength.py", "rl")
    bo = load("bsp_oracle.py", "bo")

    out = {}
    print("%-20s %7s %7s %7s %6s %7s" % ("map", "fillAll", "edgeAll", "strategyC", "bands", "C/edge"))
    for name, rect in rows:
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand
                break
        if bsp is None:
            continue
        polys = bo.read_bsp(bsp)["floors"]
        bands = {}
        for p in polys:
            a = lw.poly_area_xy(p)
            if a < MIN_AREA:
                continue
            z = sum(v[2] for v in p) / len(p)
            k = int(z // BAND)
            bands[k] = bands.get(k, 0.0) + a
        total = sum(bands.values()) or 1.0
        allm, _w, _h, _s, _k = rl.fill_mask(polys, rect, PANEL, min_area=MIN_AREA)
        edge_all = lw.edge_of(allm, PANEL)
        combined = bytearray(edge_all)
        kept = 0
        for k, area in bands.items():
            if area / total < BAND_MIN_SHARE:
                continue
            kept += 1
            bm, _a, _b, _c, _d = rl.fill_mask(polys, rect, PANEL, min_area=MIN_AREA,
                                              zmin=k * BAND - 16.0, zmax=k * BAND + BAND + 16.0)
            be = lw.edge_of(bm, PANEL)
            for i2 in range(PANEL * PANEL):
                if be[i2]:
                    combined[i2] = 1
        r_fill = runs(allm); r_edge = runs(edge_all); r_c = runs(combined)
        out[name] = {"fill_all": r_fill, "edge_all": r_edge, "edge_bands_union": r_c,
                     "bands_kept": kept, "c_over_edge": round(r_c / r_edge, 3) if r_edge else None}
        print("%-20s %7d %7d %7d %6d %7.2f" % (name, r_fill, r_edge, r_c, kept,
                                                out[name]["c_over_edge"]))
    cs = [v["edge_bands_union"] for v in out.values()]
    es = [v["edge_all"] for v in out.values()]
    out["_summary"] = {
        "C_min": min(cs), "C_max": max(cs), "C_mean": round(sum(cs) / len(cs), 1),
        "edge_mean": round(sum(es) / len(es), 1),
        "note": "one run = one Rect primitive; C includes the union silhouette",
    }
    json.dump(out, open(out_json, "w", encoding="utf-8"), indent=1)
    print(json.dumps(out["_summary"], indent=1))


if __name__ == "__main__":
    main()
