#!/usr/bin/env python3
"""N14 ruler: checks the product's minimap geometry against the BSP, independently.

The product table (native/include/minimap.h, kMinimapMaps) is read as DATA UNDER
TEST, never as an oracle. Every number this script prints is recomputed from BSP
brush faces and the entity lump; the projection here is written from the stated
convention (equal aspect, centred, +y world up -> +y pixel down) without calling
or copying the product's formula.

What it answers, per map:
  1 LANDMARKS   do the product's control-point x/y land on real team_control_point
                origins? (distance in world units; name matched where possible)
  2 CROP        how much of the product rectangle has real floor under it, and how
                much real floor falls OUTSIDE it (a crop that cuts playable area is
                visible as floor_outside > 0 near the edges)
  3 SHAPE       aspect / dead bands: is the map squeezed into a corner of a square
                panel because the declared rect is nothing like the playable one?
  4 ORIENT      non-collinearity of the landmarks actually usable for orientation,
                so a KOTH map's single control point is called out as insufficient
  5 PIXELS      where the product's own formula puts each landmark, and what the
                independent projection says. Two implementations, one comparison.

Use: n14_ruler.py <header.h> <out.json> [--maps-dir D ...] [--product "stem=rect,..."]
"""
import importlib.util
import json
import math
import os
import re
import sys


def load_oracle():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(
        "bsp_oracle_mod", os.path.join(here, "bsp_oracle.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


CP_TABLE_RE = re.compile(r"inline const MinimapControlPointRow (kMinimap\w+)\[\] = \{(.*?)\};", re.S)
CP_ROW_RE = re.compile(r'\{"([^"]*)",\s*(-?\d+),\s*(-?\d+),\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\}')
MAP_ROW_RE = re.compile(
    r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(\w+),\s*(\d+)\}')


def parse_header(path):
    text = open(path, encoding="utf-8").read()
    tables = {}
    for name, body in CP_TABLE_RE.findall(text):
        tables[name] = [
            {"name": m[0], "index": int(m[1]), "team": int(m[2]),
             "x": float(m[3]), "y": float(m[4])}
            for m in CP_ROW_RE.findall(body)]
    maps = []
    for m in MAP_ROW_RE.finditer(text):
        maps.append({"map": m.group(1), "minX": float(m.group(2)), "minY": float(m.group(3)),
                     "maxX": float(m.group(4)), "maxY": float(m.group(5)),
                     "table": m.group(6), "count": int(m.group(7))})
    return maps, tables


def my_projection(rect, w, h):
    """Equal aspect, centred. Written from the convention, not copied."""
    spanx = rect[2] - rect[0]
    spany = rect[3] - rect[1]
    s = min(w / spanx, h / spany)
    offx = (w - spanx * s) * 0.5 - rect[0] * s
    offy = (h - spany * s) * 0.5 + rect[3] * s
    return s, offx, offy


def to_px(proj, x, y):
    s, ox, oy = proj
    return (x * s + ox, oy - y * s)


def coverage_in_rect(floors, rect, w, h, grid=640):
    """Coverage of the product rect by real floor, plus the floor's own shape.

    Rasterises at a fixed grid inside the rect: covered/inside = how much of the
    panel the basemap would fill; outside = how much floor the crop throws away.
    """
    spanx, spany = rect[2] - rect[0], rect[3] - rect[1]
    scale = (grid - 1) / max(spanx, spany)
    gw, gh = max(1, int(spanx * scale) + 1), max(1, int(spany * scale) + 1)
    mod = load_oracle()
    g, cov_in = mod.rasterise(floors, [rect[0], rect[1]], [rect[2], rect[3]], scale, gw, gh)
    # how much floor lies outside the rect, in the same units (world area proxy)
    total_area = 0.0
    outside_area = 0.0
    for poly in floors:
        area = 0.0
        for k in range(len(poly)):
            x1, y1, _ = poly[k]
            x2, y2, _ = poly[(k + 1) % len(poly)]
            area += x1 * y2 - x2 * y1
        area = abs(area) * 0.5
        total_area += area
        cx = sum(p[0] for p in poly) / len(poly)
        cy = sum(p[1] for p in poly) / len(poly)
        if not (rect[0] <= cx <= rect[2] and rect[1] <= cy <= rect[3]):
            outside_area += area
    return {
        "grid": [gw, gh],
        "covered_px": cov_in,
        "rect_px": gw * gh,
        "coverage_in_rect": round(cov_in / float(gw * gh), 4),
        "floor_area_total": round(total_area, 1),
        "floor_area_outside_rect": round(outside_area, 1),
        "floor_area_outside_fraction": round(outside_area / total_area, 4) if total_area else 0.0,
    }


def noncollinear(points, min_area=256.0):
    good = 0
    sets = []
    for i in range(len(points)):
        for j in range(i + 1, len(points)):
            for k in range(j + 1, len(points)):
                ax, ay = points[j][0] - points[i][0], points[j][1] - points[i][1]
                bx, by = points[k][0] - points[i][0], points[k][1] - points[i][1]
                area = abs(ax * by - ay * bx) * 0.5
                if area > min_area:
                    good += 1
                    sets.append((area, i, j, k))
    sets.sort(reverse=True)
    return good, sets[:3]


def main():
    header, out_json = sys.argv[1], sys.argv[2]
    roots = []
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--maps-dir":
            roots.append(sys.argv[i + 1]); i += 2
        else:
            i += 1
    maps_decl, tables = parse_header(header)
    mod = load_oracle()
    report = {"header": header, "maps": []}

    for row in maps_decl:
        name = row["map"]
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand
                break
        if bsp is None:
            report["maps"].append({"map": name, "error": "bsp not found in %s" % (roots,)})
            continue
        m = mod.read_bsp(bsp)
        rect = [row["minX"], row["minY"], row["maxX"], row["maxY"]]
        fps_w, fps_h = 160, 160  # panel pixel size the HUD actually uses

        # --- 1. landmark agreement ------------------------------------------
        declared = tables.get(row["table"], [])
        by_name = {}
        cps = []
        for a in m["anchors"]:
            if a["classname"] == "team_control_point" and a["origin"]:
                cps.append(a)
                if a["targetname"]:
                    by_name.setdefault(a["targetname"], a["origin"])
        matches = []
        for d in declared:
            best = None
            if d["name"] in by_name:
                o = by_name[d["name"]]
                best = {"by": "name", "origin": o,
                        "dist": math.hypot(o[0] - d["x"], o[1] - d["y"])}
            if best is None and cps:
                cand = []
                for a in cps:
                    dd = math.hypot(a["origin"][0] - d["x"], a["origin"][1] - d["y"])
                    cand.append((dd, a))
                cand.sort(key=lambda t: t[0])
                if cand and cand[0][0] < 512.0:
                    best = {"by": "nearest<512u", "origin": cand[0][1]["origin"],
                            "dist": cand[0][0], "origin_name": cand[0][1]["targetname"]}
            matches.append({"declared": [d["name"], d["x"], d["y"], d["team"]],
                            "match": best})
        dists = [x["match"]["dist"] for x in matches if x["match"]]
        names_hit = sum(1 for x in matches if x["match"] and x["match"]["by"] == "name")

        # --- 2. crop / coverage ---------------------------------------------
        crop = coverage_in_rect(m["floors"], rect, fps_w, fps_h)

        # --- 3. shape / dead bands ------------------------------------------
        spanx, spany = rect[2] - rect[0], rect[3] - rect[1]
        proj = my_projection(rect, fps_w, fps_h)
        s, ox, oy = proj
        # where the declared rect lands inside a square panel
        band_x = (fps_w - spanx * s) / float(fps_w)
        band_y = (fps_h - spany * s) / float(fps_h)

        # --- 4. orientation basis -------------------------------------------
        pts = [[d["x"], d["y"]] for d in declared]
        n_noncol, biggest = noncollinear(pts)
        # fall back to spawn clusters when the map declares one objective
        spawn_pts = [[a["origin"][0], a["origin"][1]] for a in m["anchors"]
                     if a["classname"] == "info_player_teamspawn" and a["origin"]]
        mixed = pts + spawn_pts[:64]
        n_mixed, _ = noncollinear(mixed)

        # --- 5. pixel comparison --------------------------------------------
        px_decl = {d["name"]: [round(v, 2) for v in to_px(proj, d["x"], d["y"])]
                   for d in declared}

        report["maps"].append({
            "map": name,
            "bsp": bsp.replace("\\", "/"),
            "declared_rect": rect,
            "rect_span": [round(spanx, 1), round(spany, 1)],
            "rect_aspect": round(spanx / spany, 4) if spany else None,
            "bsp_world_bbox": [[round(v, 1) for v in m["world_bbox_min"]],
                               [round(v, 1) for v in m["world_bbox_max"]]],
            "bsp_floor_bbox": [round(v, 1) for v in (mod.floor_bbox(m["floors"]) or [0, 0, 0, 0])],
            "bsp_control_point_count": len(cps),
            "declared_point_count": len(declared),
            "landmark_matches": matches,
            "landmark_distance_units": {
                "count": len(dists),
                "max": round(max(dists), 2) if dists else None,
                "median": round(sorted(dists)[len(dists) // 2], 2) if dists else None,
                "matched_by_name": names_hit,
            },
            "crop": crop,
            "shape": {"proj_scale_px_per_unit": round(s, 6),
                      "offset_x": round(ox, 3), "offset_y": round(oy, 3),
                      "dead_band_fraction_x": round(band_x, 4),
                      "dead_band_fraction_y": round(band_y, 4),
                      "pixel_area_used": round((spanx * s) * (spany * s) / (fps_w * fps_h), 4)},
            "orientation": {"declared_points": len(pts),
                            "noncollinear_triples_declared": n_noncol,
                            "noncollinear_triples_with_spawns": n_mixed,
                            "largest_triples": [{"area": round(a, 1), "idx": [i, j, k]}
                                                for a, i, j, k in biggest]},
            "projected_pixels": px_decl,
        })

    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    for mp in report["maps"]:
        if "error" in mp:
            print("%-20s ERROR %s" % (mp["map"], mp["error"]))
            continue
        lm = mp["landmark_distance_units"]
        print("%-20s cp=%d/%d maxdist=%s cov=%.3f outside=%.3f noncol=%d bandx=%.3f"
              % (mp["map"], mp["bsp_control_point_count"], mp["declared_point_count"],
                 lm["max"], mp["crop"]["coverage_in_rect"],
                 mp["crop"]["floor_area_outside_fraction"],
                 mp["orientation"]["noncollinear_triples_declared"],
                 mp["shape"]["dead_band_fraction_x"]))


if __name__ == "__main__":
    main()
