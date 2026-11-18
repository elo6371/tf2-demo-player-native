#!/usr/bin/env python3
"""N14 containment: does the product's declared rectangle actually contain the map?

Three independent questions, because they can disagree:
  A ANCHORS   gameplay entities (control points, spawns, cart paths, capture zones)
              that fall OUTSIDE the declared rect. A map can have floor outside its
              playable area, but an info_player_teamspawn or team_train_watcher
              outside the panel is playable space the player cannot see.
  B MASS      raster floor pixels classified by where their world position falls:
              inside the declared rect vs outside it. Reported as a split, not a
              verdict -- out-of-bounds terrain is real floor and is not an error.
  C SPAN      per-axis reach of the anchor cloud vs the rect, so a crop that cuts a
              whole lane shows as a large unused margin on the opposite side.

Use: n14_containment.py <header.h> <out.json> --maps-dir D [--maps-dir D2]
"""
import importlib.util
import json
import math
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

# classes whose position is playable space the player can be standing in or looking at
PLAYABLE_CLASSES = {
    "info_player_teamspawn", "info_player_start", "team_control_point",
    "team_train_watcher", "item_teamflag", "func_capturezone",
    "trigger_capture_area", "func_respawnroom",
}


def mass_split(floors, rect, grid=900):
    """Raster the whole floor extent once, then split pixels by the declared rect."""
    lo = [1e30, 1e30]
    hi = [-1e30, -1e30]
    for poly in floors:
        for (x, y, _z) in poly:
            if x < lo[0]: lo[0] = x
            if y < lo[1]: lo[1] = y
            if x > hi[0]: hi[0] = x
            if y > hi[1]: hi[1] = y
    lo[0] = min(lo[0], rect[0]); lo[1] = min(lo[1], rect[1])
    hi[0] = max(hi[0], rect[2]); hi[1] = max(hi[1], rect[3])
    spanx, spany = hi[0] - lo[0], hi[1] - lo[1]
    scale = (grid - 1) / max(spanx, spany)
    w, h = max(1, int(spanx * scale) + 1), max(1, int(spany * scale) + 1)
    mod = load_oracle()
    grid_px, covered = mod.rasterise(floors, lo, hi, scale, w, h)
    inside = outside = 0
    out_bbox = [1e30, 1e30, -1e30, -1e30]
    for yy in range(h):
        wy = hi[1] - (yy + 0.5) / scale
        row = yy * w
        for xx in range(w):
            if not grid_px[row + xx]:
                continue
            wx = lo[0] + (xx + 0.5) / scale
            if rect[0] <= wx <= rect[2] and rect[1] <= wy <= rect[3]:
                inside += 1
            else:
                outside += 1
                if wx < out_bbox[0]: out_bbox[0] = wx
                if wy < out_bbox[1]: out_bbox[1] = wy
                if wx > out_bbox[2]: out_bbox[2] = wx
                if wy > out_bbox[3]: out_bbox[3] = wy
    total = inside + outside
    return {
        "grid": [w, h], "scale_px_per_unit": round(scale, 6),
        "raster_bbox": [round(v, 1) for v in lo + hi],
        "floor_px_total": total,
        "floor_px_inside_rect": inside,
        "floor_px_outside_rect": outside,
        "floor_mass_outside_fraction": round(outside / total, 4) if total else 0.0,
        "outside_bbox": None if outside == 0 else [round(v, 1) for v in out_bbox],
    }


def main():
    header, out_json = sys.argv[1], sys.argv[2]
    roots = []
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--maps-dir":
            roots.append(sys.argv[i + 1]); i += 2
        else:
            i += 1

    text = open(header, encoding="utf-8").read()
    rows = [{"map": m.group(1), "rect": [float(m.group(2)), float(m.group(3)),
                                         float(m.group(4)), float(m.group(5))],
             "table": m.group(6), "count": int(m.group(7))}
            for m in MAP_ROW_RE.finditer(text)]
    mod = load_oracle()
    report = {"header": header, "maps": []}

    for row in rows:
        name, rect = row["map"], row["rect"]
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand
                break
        if bsp is None:
            report["maps"].append({"map": name, "error": "bsp not found"})
            continue
        m = mod.read_bsp(bsp)

        outside_by_class = {}
        anchor_xy = []
        for a in m["anchors"]:
            if a["classname"] not in PLAYABLE_CLASSES or not a["origin"]:
                continue
            x, y = a["origin"][0], a["origin"][1]
            anchor_xy.append((x, y))
            if not (rect[0] <= x <= rect[2] and rect[1] <= y <= rect[3]):
                key = a["classname"]
                slot = outside_by_class.setdefault(key, {"count": 0, "examples": []})
                slot["count"] += 1
                if len(slot["examples"]) < 4:
                    slot["examples"].append({"origin": [x, y, a["origin"][2]],
                                             "name": a["targetname"]})

        ax0 = min(p[0] for p in anchor_xy) if anchor_xy else None
        ax1 = max(p[0] for p in anchor_xy) if anchor_xy else None
        ay0 = min(p[1] for p in anchor_xy) if anchor_xy else None
        ay1 = max(p[1] for p in anchor_xy) if anchor_xy else None
        spanx, spany = rect[2] - rect[0], rect[3] - rect[1]

        report["maps"].append({
            "map": name, "bsp": bsp.replace("\\", "/"), "declared_rect": rect,
            "anchors_outside_rect": outside_by_class,
            "anchors_outside_total": sum(v["count"] for v in outside_by_class.values()),
            "anchor_bbox": None if not anchor_xy else [round(ax0, 1), round(ay0, 1),
                                                      round(ax1, 1), round(ay1, 1)],
            "margin_units": None if not anchor_xy else {
                "left": round(ax0 - rect[0], 1), "right": round(rect[2] - ax1, 1),
                "bottom": round(ay0 - rect[1], 1), "top": round(rect[3] - ay1, 1)},
            "margin_fraction_of_rect": None if not anchor_xy else {
                "left": round((ax0 - rect[0]) / spanx, 4),
                "right": round((rect[2] - ax1) / spanx, 4),
                "bottom": round((ay0 - rect[1]) / spany, 4),
                "top": round((rect[3] - ay1) / spany, 4)},
            "floor_mass": mass_split(m["floors"], rect),
        })

    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    print("%-20s %-8s %-9s %-8s %s" % ("map", "anchOut", "floorOut", "floorIn", "margins L R B T (fraction)"))
    for mp in report["maps"]:
        if "error" in mp:
            print("%-20s ERROR" % mp["map"]); continue
        fm = mp["floor_mass"]
        mg = mp["margin_fraction_of_rect"] or {}
        print("%-20s %-8d %-9.3f %-8d %s" % (
            mp["map"], mp["anchors_outside_total"], fm["floor_mass_outside_fraction"],
            fm["floor_px_inside_rect"],
            " ".join("%.3f" % mg.get(k, 0) for k in ("left", "right", "bottom", "top"))))


if __name__ == "__main__":
    main()
