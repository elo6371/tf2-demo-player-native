#!/usr/bin/env python3
"""N14/D41 construction budget: what does the product actually have to draw?

Two numbers decide the D41 design and neither can be guessed:

1. RUN BUDGET. The HUD has only Rect and Circle primitives, so a basemap is a
   list of horizontal runs -- one Rect each. This measures the run count for the
   silhouette (filled) and for the outline (1-px edge), because the outline is
   what reads as a map and the fill is what reads as a blob.

2. FLOOR STRATEGY. A minimap that overlays every walkable surface is unusable on
   a multi-level map: two corridors one above the other become one corridor.
   Floors are bucketed by z (256 u buckets), the bucket holding the most area is
   the MAIN band, and the tool reports (a) what fraction of floor mass the main
   band holds, (b) how many extra bands survive an area filter, (c) the run cost
   of main-band-only vs all-bands. The per-map decision in the doc is then a
   measured choice, not a preference.

usage: n14_budget.py <header.h> <out.json> [--maps-dir D ...]
"""
import importlib.util
import json
import os
import re
import sys

PANEL = 160
SCALE = 3


def load(path, modname):
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(modname, os.path.join(here, path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def runs_of(mask, size=PANEL):
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

    report = {"panel": PANEL, "maps": {}}
    for name, rect in rows:
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand
                break
        if bsp is None:
            continue
        m = bo.read_bsp(bsp)
        polys = m["floors"]

        # ---- floor bands ------------------------------------------------------
        bands = {}
        for p in polys:
            area = lw.poly_area_xy(p)
            if area < 1024.0:
                continue
            z = sum(v[2] for v in p) / len(p)
            k = int(z // 256.0)
            b = bands.setdefault(k, {"area": 0.0, "faces": 0})
            b["area"] += area
            b["faces"] += 1
        total = sum(b["area"] for b in bands.values()) or 1.0
        ordered = sorted(bands.items(), key=lambda kv: -kv[1]["area"])
        main_k, main_b = ordered[0]
        keep = [(k, b) for k, b in ordered if b["area"] / total >= 0.05]
        band_rows = []
        for k, b in ordered[:6]:
            band_rows.append({"z_lo": k * 256, "z_hi": k * 256 + 256,
                              "area": round(b["area"]), "faces": b["faces"],
                              "share": round(b["area"] / total, 3)})

        # ---- masks ------------------------------------------------------------
        mask_all, _w, _h, scale, kept_all = rl.fill_mask(polys, rect, PANEL, min_area=1024.0)
        edge_all = lw.edge_of(mask_all, PANEL)
        zmin = main_k * 256 - 384
        zmax = main_k * 256 + 256 + 384
        mask_main, _w2, _h2, _s2, kept_main = rl.fill_mask(polys, rect, PANEL,
                                                           min_area=1024.0,
                                                           zmin=zmin, zmax=zmax)
        edge_main = lw.edge_of(mask_main, PANEL)

        entry = {
            "rect": rect,
            "px_per_world_unit": round(scale, 6),
            "faces_total": len(polys),
            "faces_kept_all": kept_all,
            "faces_kept_main": kept_main,
            "bands": band_rows,
            "main_band": {"z_lo": main_k * 256, "z_hi": main_k * 256 + 256,
                          "share": round(main_b["area"] / total, 3),
                          "bands_over_5pct": len(keep)},
            "main_band_window": [zmin, zmax],
            "fill_frac_all": round(sum(mask_all) / (PANEL * PANEL), 4),
            "fill_frac_main": round(sum(mask_main) / (PANEL * PANEL), 4),
            "runs_fill_all": runs_of(mask_all),
            "runs_edge_all": runs_of(edge_all),
            "runs_fill_main": runs_of(mask_main),
            "runs_edge_main": runs_of(edge_main),
            "edge_px_all": sum(edge_all),
            "edge_px_main": sum(edge_main),
            "landmarks": {
                "spawn_clusters": len(lw.spawn_clusters(m["anchors"])),
                "control_points": sum(1 for a in m["anchors"] if a["classname"] == "team_control_point"),
                "rail_nodes": len(lw.rail_points(m["anchors"])),
                "train_watchers": sum(1 for a in m["anchors"] if a["classname"] == "team_train_watcher"),
            },
        }
        report["maps"][name] = entry
        print("%-20s main_band_share=%-5.2f bands5%%=%d  runs fill all/main=%-4d/%-4d  edge all/main=%-4d/%-4d  edge_px %d/%d"
              % (name, entry["main_band"]["share"], entry["main_band"]["bands_over_5pct"],
                 entry["runs_fill_all"], entry["runs_fill_main"],
                 entry["runs_edge_all"], entry["runs_edge_main"],
                 entry["edge_px_all"], entry["edge_px_main"]))

    edges = [v["runs_edge_all"] for v in report["maps"].values()]
    fills = [v["runs_fill_all"] for v in report["maps"].values()]
    report["summary"] = {
        "maps": len(report["maps"]),
        "edge_runs_min": min(edges), "edge_runs_max": max(edges),
        "edge_runs_mean": round(sum(edges) / len(edges), 1),
        "fill_runs_min": min(fills), "fill_runs_max": max(fills),
        "max_band_share": max(v["main_band"]["share"] for v in report["maps"].values()),
        "maps_with_multi_floor": sorted(n for n, v in report["maps"].items()
                                        if v["main_band"]["share"] < 0.85),
    }
    json.dump(report, open(out_json, "w", encoding="utf-8"), indent=1)
    print(json.dumps(report["summary"], indent=1))


if __name__ == "__main__":
    main()
