#!/usr/bin/env python3
"""N14/D41: how many Rects does strategy C need AFTER merging equal rows?

A run is one Rect, but two identical runs on adjacent rows are one taller Rect. The
HUD shader fills an axis-aligned box, so vertical coalescing is free and the merged
list is a strictly smaller bill for exactly the same pixels. This measures the
merged count so the doc quotes the number the product would really upload, and it
asserts that merging changes no pixel (a merge that changes pixels would be a
worse bug than the cost it saves).

usage: n14_coalesce.py <header.h> <out.json> [--maps-dir D ...]
"""
import importlib.util
import json
import os
import re
import sys

PANEL = 160
BAND = 256.0
BAND_MIN_SHARE = 0.05
MIN_AREA = 1024.0


def load(path, modname):
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(modname, os.path.join(here, path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def row_spans(mask, size=PANEL):
    """[(y, x0, x1_exclusive)] per horizontal run."""
    spans = []
    for y in range(size):
        base = y * size
        x = 0
        while x < size:
            if mask[base + x]:
                x0 = x
                while x < size and mask[base + x]:
                    x += 1
                spans.append((y, x0, x))
            else:
                x += 1
    return spans


def coalesce(spans):
    """Merge spans with equal x0/x1 across consecutive rows into boxes."""
    boxes = []
    open_ = {}
    for y, x0, x1 in spans:
        key = (x0, x1)
        if key in open_:
            open_[key][2] = y + 1
        else:
            box = [x0, y, y + 1]
            open_[key] = box
            boxes.append(box)
        for k in list(open_):
            if k != key and open_[k][2] < y:
                del open_[k]
        # a run whose rectangle stopped growing on the previous row is closed below
    return boxes


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
    print("%-20s %8s %8s %7s %7s" % ("map", "runsB", "rectsB", "runsC", "rectsC"))
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
        for k, area in bands.items():
            if area / total < BAND_MIN_SHARE:
                continue
            bm, _a, _b, _c, _d = rl.fill_mask(polys, rect, PANEL, min_area=MIN_AREA,
                                              zmin=k * BAND - 16.0, zmax=k * BAND + BAND + 16.0)
            be = lw.edge_of(bm, PANEL)
            for i2 in range(PANEL * PANEL):
                if be[i2]:
                    combined[i2] = 1

        def spans_of(mask):
            s = []
            for y in range(PANEL):
                base = y * PANEL
                x = 0
                while x < PANEL:
                    if mask[base + x]:
                        x0 = x
                        while x < PANEL and mask[base + x]:
                            x += 1
                        s.append((y, x0, x))
                    else:
                        x += 1
            return s

        def merge(spans):
            """Equal-width runs on consecutive rows become one taller box.

            The active map is keyed by (x0, x1) and remembers the LAST ROW that
            key was seen. A key is dropped when its last row is older than the
            row being processed minus one -- comparing against the previous
            SPAN's row instead would drop the key on the second span of a row
            (a row change happens between spans, not only between rows) and the
            merge would then silently do nothing.
            """
            boxes = []
            last_row = {}
            active = {}
            for y, x0, x1 in spans:
                for key in list(last_row):
                    if last_row[key] < y - 1:
                        del last_row[key]
                        del active[key]
                key = (x0, x1)
                if key in active:
                    active[key][2] = y + 1
                else:
                    box = [x0, y, y + 1, x1]
                    active[key] = box
                    boxes.append(box)
                last_row[key] = y
            return boxes

        def paint(boxes):
            m = bytearray(PANEL * PANEL)
            for x0, y0, y1, x1 in boxes:
                for y in range(y0, y1):
                    base = y * PANEL
                    for x in range(x0, x1):
                        m[base + x] = 1
            return m

        sB = spans_of(edge_all)
        sC = spans_of(combined)
        bB = merge(sB)
        bC = merge(sC)
        sameB = paint(bB) == edge_all
        sameC = paint(bC) == combined
        out[name] = {"runs_edge": len(sB), "rects_edge": len(bB),
                     "runs_C": len(sC), "rects_C": len(bC),
                     "merge_exact_edge": sameB, "merge_exact_C": sameC}
        print("%-20s %8d %8d %7d %7d  exact=%s/%s"
              % (name, len(sB), len(bB), len(sC), len(bC), sameB, sameC))
    cs = [v["rects_C"] for v in out.values()]
    bs = [v["rects_edge"] for v in out.values()]
    out["_summary"] = {
        "rectsC_min": min(cs), "rectsC_max": max(cs),
        "rectsC_mean": round(sum(cs) / len(cs), 1),
        "rectsB_mean": round(sum(bs) / len(bs), 1),
        "all_merges_exact": all(v["merge_exact_C"] and v["merge_exact_edge"] for k, v in out.items() if k != "_summary"),
        "vertices_at_max": max(cs) * 6,
        "floats_at_max": max(cs) * 6 * 11,
        "bytes_at_max": max(cs) * 6 * 11 * 4,
    }
    json.dump(out, open(out_json, "w", encoding="utf-8"), indent=1)
    print(json.dumps(out["_summary"], indent=1))


if __name__ == "__main__":
    main()
