#!/usr/bin/env python3
"""N14: characterise every mass of floor geometry that falls OUTSIDE a map's
declared minimap rect.

Why: docs/N14-MINIMAP-BASEMAP-2026-11-15.md has to say, for each map, whether
the out-of-rect mass is (i) a 3D skybox plate, (ii) real playable surface the
rect clips away, or (iii) nothing at all -- and "it is probably the skybox"
is not an answer the lead accepts.

Two numbers are reported per cluster and they are NOT the same thing:

  cluster_area   full XY area of every polygon whose CENTROID is outside. An
                 upper bound: a small roof straddling the rect edge counts
                 entirely, even though most of it is inside.
  exact_outside  the area geometrically outside the rect, via half-plane
                 clipping. This is the number that describes the picture.

A cluster wholly outside the rect has cluster_area == exact_outside. When the
two differ, the cluster straddles the edge, i.e. it is part of the map itself
and the rect cuts through it. That self-consistency is the reason both are
printed: for cp_gullywash_f9 (418,132), koth_bagel_rc11 (925,696) and
pl_upward_f12 (33,152,947) the two agree to the unit, which is what a detached
plate looks like; cp_sunshine's 1,869,778 vs 477,316 is what a cropped map
edge looks like.

Geometry alone cannot separate a skybox plate from a piece of map, so three
independent indicators are printed and the doc cites whichever apply:

  camIn        the sky_camera origin lies inside the cluster's XY bbox.
  NAMES-CAMERA a material in the cluster spells the camera's XY in its name
               (VBSP auto-names water brushes maps/<map>/water/water_<x>_<y>_<z>).
  z relative   gullywash's plate sits 2,910 units above the highest floor the
               rect contains, which no player can stand on. This does NOT
               generalise: koth_bagel_rc11's plate z=288 is inside that map's
               playable z range, so altitude proves nothing there.

Usage: n14_outside_mass.py <minimap.h> <out.json> --maps-dir DIR [--maps-dir DIR]
"""
import json
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bsp_oracle as B

GAP = 128.0


def read_header(path):
    text = open(path, encoding="utf-8").read()
    rx = re.compile(r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,')
    return {m.group(1): [float(m.group(k)) for k in (2, 3, 4, 5)] for m in rx.finditer(text)}


def find_bsp(name, roots):
    for r in roots:
        p = os.path.join(r, name + ".bsp")
        if os.path.exists(p):
            return p
    return None


def area_xy(pts):
    a = 0.0
    n = len(pts)
    for k in range(n):
        x0, y0, _ = pts[k]
        x1, y1, _ = pts[(k + 1) % n]
        a += x0 * y1 - x1 * y0
    return abs(a) * 0.5


def half_plane(poly, axis, bound, sign):
    """Keep the part where sign * (coord - bound) >= 0. sign=+1 keeps coord>=bound."""
    out = []
    n = len(poly)

    def inside(p):
        return sign * (p[axis] - bound) >= 0.0

    for i in range(n):
        a = poly[i]
        b = poly[(i + 1) % n]
        ia, ib = inside(a), inside(b)
        if ia:
            out.append(a)
        if ia != ib:
            t = (bound - a[axis]) / (b[axis] - a[axis])
            out.append(tuple(a[k] + (b[k] - a[k]) * t for k in range(3)))
    return out


def exact_outside(poly, rect):
    """XY area of poly lying outside rect, by Sutherland-Hodgman triangulation-free
    decomposition: total minus the clipped-inside part. Half-plane clipping keeps
    the loop convex-safe for the convex polygons a BSP face always is."""
    q = poly
    for axis, bound, sign in ((0, rect[0], +1), (0, rect[2], -1),
                              (1, rect[1], +1), (1, rect[3], -1)):
        q = half_plane(q, axis, bound, sign)
        if len(q) < 3:
            return area_xy(poly)
    return area_xy(poly) - area_xy(q)


def texnames(blob):
    sd = B.lump(blob, 43)
    st = B.lump(blob, 44)
    n = len(st) // 4
    offs = struct.unpack_from("<%di" % n, st, 0) if n else ()
    strings = []
    for o in offs:
        if 0 <= o < len(sd):
            e = sd.find(b"\x00", o)
            strings.append(sd[o:e if e >= 0 else len(sd)].decode("ascii", "replace"))
        else:
            strings.append("")
    td = B.lump(blob, 2)
    out = []
    for k in range(len(td) // 32):
        sid = struct.unpack_from("<i", td, k * 32 + 12)[0]
        out.append(strings[sid].lower() if 0 <= sid < len(strings) else "")
    return out


def floor_polys(blob):
    """(poly, material) for every upward, non-tool, non-skybox face."""
    names = texnames(blob)
    vraw = B.lump(blob, 3)
    nv = len(vraw) // 12
    verts = struct.unpack_from("<%df" % (nv * 3), vraw, 0) if nv else ()
    eraw = B.lump(blob, 12)
    edges = struct.unpack_from("<%dH" % (len(eraw) // 2), eraw, 0) if eraw else ()
    sraw = B.lump(blob, 13)
    surf = struct.unpack_from("<%di" % (len(sraw) // 4), sraw, 0) if sraw else ()
    praw = B.lump(blob, 1)
    npl = len(praw) // 20
    planes = struct.unpack_from("<%df" % (npl * 4), praw, 0) if npl else ()
    tiraw = B.lump(blob, 6)
    nti = len(tiraw) // 72
    # texdata index is the last int of the 72-byte texinfo_t (+68). +0 is the
    # first textureVec float; see bsp_oracle.read_bsp and bsp_map.cpp:777.
    ti_tex = [struct.unpack_from("<i", tiraw, k * 72 + 68)[0] for k in range(nti)]
    fraw = B.lump(blob, 7)
    nf = len(fraw) // 56
    for fi in range(nf):
        b = fi * 56
        pn = struct.unpack_from("<H", fraw, b)[0]
        side = fraw[b + 2]
        fe = struct.unpack_from("<i", fraw, b + 4)[0]
        ne = struct.unpack_from("<h", fraw, b + 8)[0]
        ti = struct.unpack_from("<h", fraw, b + 10)[0]
        if pn >= npl or ne < 3 or fe < 0 or fe + ne > len(surf):
            continue
        pb = pn * 4
        nx, ny, nz = planes[pb], planes[pb + 1], planes[pb + 2]
        if side:
            nx, ny, nz = -nx, -ny, -nz
        if nz <= 0.7:
            continue
        t = ti_tex[ti] if 0 <= ti < nti else -1
        mat = names[t] if 0 <= t < len(names) else ""
        if mat.startswith("tools/") or mat.startswith("editor/") or "skybox" in mat:
            continue
        pts, ok = [], True
        for k in range(ne):
            se = surf[fe + k]
            vi = edges[se * 2] if se >= 0 else edges[(-se) * 2 + 1]
            if vi >= nv:
                ok = False
                break
            pts.append((verts[vi * 3], verts[vi * 3 + 1], verts[vi * 3 + 2]))
        if ok and len(pts) >= 3:
            yield pts, mat


def sky_camera(blob):
    for e in B.parse_entities(B.lump(blob, 0).decode("utf-8", "replace")):
        if e.get("classname") == "sky_camera":
            return {"origin": B.vec3(e.get("origin", "")), "scale": e.get("scale", "1")}
    return None


def main(argv):
    header = argv[1]
    out_path = argv[2]
    roots = [argv[i + 1] for i, a in enumerate(argv) if a == "--maps-dir"]
    rows = read_header(header)
    report = {}
    print("%-19s %-25s %10s %10s %6s %6s %5s %s" % (
        "map", "cluster bbox(x0,y0,x1,y1)", "cluster_ar", "exact_out", "zLo", "zHi", "camIn", "top materials"))
    for name, rect in rows.items():
        path = find_bsp(name, roots)
        if path is None:
            print("%-19s MISSING BSP" % name)
            continue
        blob = open(path, "rb").read()
        cam = sky_camera(blob)
        total = inside = 0.0
        zlo_in, zhi_in = 1e9, -1e9
        outs = []
        for pts, mat in floor_polys(blob):
            a = area_xy(pts)
            total += a
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            if rect[0] <= cx <= rect[2] and rect[1] <= cy <= rect[3]:
                inside += a
                for p in pts:
                    zlo_in = min(zlo_in, p[2])
                    zhi_in = max(zhi_in, p[2])
                continue
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            zs = [p[2] for p in pts]
            outs.append({"bbox": [min(xs), min(ys), max(xs), max(ys)],
                         "z": [min(zs), max(zs)], "area": a, "material": mat,
                         "exact_outside": exact_outside(pts, rect)})
        parent = list(range(len(outs)))

        def root(i):
            while parent[i] != i:
                parent[i] = parent[parent[i]]
                i = parent[i]
            return i

        for i in range(len(outs)):
            for j in range(i + 1, len(outs)):
                a, b = outs[i]["bbox"], outs[j]["bbox"]
                if (a[0] - GAP <= b[2] and b[0] - GAP <= a[2]
                        and a[1] - GAP <= b[3] and b[1] - GAP <= a[3]):
                    ri, rj = root(i), root(j)
                    if ri != rj:
                        parent[rj] = ri
        groups = {}
        for i, o in enumerate(outs):
            groups.setdefault(root(i), []).append(o)
        clusters = []
        for g in groups.values():
            bx = [o["bbox"][0] for o in g] + [o["bbox"][2] for o in g]
            by = [o["bbox"][1] for o in g] + [o["bbox"][3] for o in g]
            bz = [o["z"][0] for o in g] + [o["z"][1] for o in g]
            ar = sum(o["area"] for o in g)
            eo = sum(o["exact_outside"] for o in g)
            mats = {}
            for o in g:
                mats[o["material"]] = mats.get(o["material"], 0.0) + o["area"]
            top = sorted(mats.items(), key=lambda kv: -kv[1])[:3]
            bw = max(1.0, max(bx) - min(bx))
            bh = max(1.0, max(by) - min(by))
            camtok = None
            if cam:
                camtok = "%d_%d" % (int(round(cam["origin"][0])), int(round(cam["origin"][1])))
            clusters.append({"polys": len(g), "area": ar, "exact_outside": eo,
                             "bbox": [min(bx), min(by), max(bx), max(by)],
                             "z": [min(bz), max(bz)],
                             "bbox_xy_area": bw * bh,
                             "stack_factor": ar / (bw * bh),
                             "materials": [{"name": k, "area": v} for k, v in top],
                             "name_cites_sky_camera": sorted(k for k in mats if camtok and camtok in k),
                             "sky_camera_inside_bbox": bool(
                                 cam and min(bx) <= cam["origin"][0] <= max(bx)
                                 and min(by) <= cam["origin"][1] <= max(by))})
        clusters.sort(key=lambda c: -c["area"])
        report[name] = {"rect": rect, "sky_camera": cam,
                        "floor_area": total, "floor_inside": inside,
                        "floor_outside_cluster_area": total - inside,
                        "floor_outside_exact": sum(o["exact_outside"] for o in outs),
                        "floor_inside_z": [zlo_in, zhi_in] if zhi_in > -1e8 else None,
                        "clusters": clusters}
        if not clusters:
            print("%-19s %-25s %10s %10s %6s %6s %5s %s" % (
                name, "-", "0", "0", "-", "-", "-", "no out-of-rect floor"))
        for c in clusters:
            print("%-19s %-25s %10.0f %10.0f %6.0f %6.0f %5s %s%s" % (
                name,
                "%.0f,%.0f,%.0f,%.0f" % tuple(c["bbox"]),
                c["area"], c["exact_outside"], c["z"][0], c["z"][1],
                "YES" if c["sky_camera_inside_bbox"] else "no",
                "; ".join("%s %.0f" % (m["name"][:30], m["area"]) for m in c["materials"]),
                ("  NAMES-CAMERA:" + ",".join(c["name_cites_sky_camera"]))
                if c["name_cites_sky_camera"] else ""))
        print("%-19s   floor total %.0f, cluster-area outside %.0f, exact outside %.0f, in-rect z %.0f..%.0f" % (
            "", total, total - inside, report[name]["floor_outside_exact"],
            report[name]["floor_inside_z"][0] if report[name]["floor_inside_z"] else 0,
            report[name]["floor_inside_z"][1] if report[name]["floor_inside_z"] else 0))
    json.dump(report, open(out_path, "w"), indent=1)
    print()
    print("wrote %s" % out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
