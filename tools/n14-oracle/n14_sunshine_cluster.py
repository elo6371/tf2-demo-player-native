#!/usr/bin/env python3
"""N14: name the cp_sunshine out-of-rect cluster.

Lead asked for exactly two things before that region may be called a defect:

  (a) the BSP classname of the connected cluster, and
  (b) whether it is covered by a trigger_* volume.

Both are answerable from the BSP alone, provided the face walk is done correctly.
An earlier attempt hand-rolled the walk, fed SURFEDGE indices straight into the
vertex lump and crashed with struct.error -- so this file copies the surfedge ->
edge -> vertex indirection verbatim from bsp_oracle.read_bsp and does not
invent its own.

What this prints (and writes as JSON):

  1. owner:    for every selected polygon, which dmodel contains its face, and
               the classname of the entity that model belongs to. model 0 is
               worldspawn (plain world geometry, no entity behind it).
  2. material: texinfo -> texdata -> string name per polygon.
  3. trigger:  every trigger_* brush model whose AABB intersects the region.
  4. entities: every entity whose origin lies inside the region, by classname.

Selection rule (stated so the number can be argued with): a polygon is
"in the region" when its XY centroid lies inside the target rect grown by
GROW units. Area is the XY projection, same convention as the other N14 tools.
"""

import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bsp_oracle as B

LUMP_MODELS = 14
DMODEL_BYTES = 48

# The cluster reported by skybox-aware.json / crop-tight.json for cp_sunshine:
# bbox [-14892, 12636, -14060, 15420], 4,632,576 u^2, 90 polygons.
REGION = (-14892.0, 12636.0, -14060.0, 15420.0)
GROW = 256.0


def read_models(blob):
    raw = B.lump(blob, LUMP_MODELS)
    models = []
    for i in range(len(raw) // DMODEL_BYTES):
        b = i * DMODEL_BYTES
        mn = struct.unpack_from("<3f", raw, b)
        mx = struct.unpack_from("<3f", raw, b + 12)
        org = struct.unpack_from("<3f", raw, b + 24)
        hn, ff, nf = struct.unpack_from("<3i", raw, b + 36)
        models.append({"index": i, "mins": mn, "maxs": mx, "origin": org,
                       "headnode": hn, "firstface": ff, "numfaces": nf})
    return models


def read_texnames(blob):
    str_data = B.lump(blob, B.LUMP_TEXDATA_STRING_DATA)
    str_tbl = B.lump(blob, B.LUMP_TEXDATA_STRING_TABLE)
    nstr = len(str_tbl) // 4
    offs = struct.unpack_from("<%di" % nstr, str_tbl, 0) if nstr else ()
    strings = []
    for ofs in offs:
        if 0 <= ofs < len(str_data):
            end = str_data.find(b"\x00", ofs)
            strings.append(str_data[ofs:end if end >= 0 else len(str_data)].decode("ascii", "replace"))
        else:
            strings.append("")
    tex = B.lump(blob, B.LUMP_TEXDATA)
    names = []
    for k in range(len(tex) // 32):
        sid = struct.unpack_from("<i", tex, k * 32 + 12)[0]
        names.append(strings[sid].lower() if 0 <= sid < len(strings) else "")
    return names


def walk_faces(blob):
    """Same indirection as bsp_oracle.read_bsp, but keeps the face index and
    the material name so a polygon can be attributed to a dmodel."""
    td_names = read_texnames(blob)

    vraw = B.lump(blob, B.LUMP_VERTEXES)
    nverts = len(vraw) // 12
    verts = struct.unpack_from("<%df" % (nverts * 3), vraw, 0) if nverts else ()
    eraw = B.lump(blob, B.LUMP_EDGES)
    edges = struct.unpack_from("<%dH" % (len(eraw) // 2), eraw, 0) if eraw else ()
    sraw = B.lump(blob, B.LUMP_SURFEDGES)
    surf = struct.unpack_from("<%di" % (len(sraw) // 4), sraw, 0) if sraw else ()
    praw = B.lump(blob, B.LUMP_PLANES)
    # A dplane_t is 20 bytes = 4 floats (normal.xyz + dist), so the float COUNT is
    # len/4, not len/20. Writing len//20 here silently keeps only the first quarter
    # of the plane lump: every face whose planenum lands past that quarter is then
    # either skipped or read against the wrong normal, and the survivors look like a
    # perfectly plausible but different set of floors. That is exactly how this file
    # first reported "0 faces in the region" for a cluster of 90.
    nplanes = len(praw) // 20
    planes = struct.unpack_from("<%df" % (nplanes * 4), praw, 0) if praw else ()
    tiraw = B.lump(blob, B.LUMP_TEXINFO)
    nti = len(tiraw) // 72
    # texdata index is the LAST int of the 72-byte texinfo_t (+68), after int
    # flags at +64. This read +0 until 2026-11-15, i.e. the first textureVec
    # float reinterpreted as an int. bsp_oracle.read_bsp carries the same fix
    # and the comment there has the cp_process_f12 consequence; the product
    # reads +68 at native/src/bsp_map.cpp:777.
    ti_tex = [struct.unpack_from("<i", tiraw, k * 72 + 68)[0] for k in range(nti)]
    fraw = B.lump(blob, B.LUMP_FACES)
    nfaces = len(fraw) // 56

    out = []
    for fi in range(nfaces):
        base = fi * 56
        planenum = struct.unpack_from("<H", fraw, base)[0]
        side = fraw[base + 2]
        firstedge = struct.unpack_from("<i", fraw, base + 4)[0]
        numedges = struct.unpack_from("<h", fraw, base + 8)[0]
        texinfo = struct.unpack_from("<h", fraw, base + 10)[0]
        if planenum >= len(planes) // 4 or numedges < 3 or firstedge < 0 or firstedge + numedges > len(surf):
            continue
        pb = planenum * 4
        nx, ny, nz = planes[pb], planes[pb + 1], planes[pb + 2]
        if side:
            nx, ny, nz = -nx, -ny, -nz
        if nz <= 0.7:
            continue
        ti = ti_tex[texinfo] if 0 <= texinfo < nti else -1
        name = td_names[ti] if 0 <= ti < len(td_names) else ""
        if name.startswith("tools/") or name.startswith("editor/"):
            continue
        if "skybox" in name:
            continue
        poly = []
        bad = False
        for k in range(numedges):
            se = surf[firstedge + k]
            vi = edges[se * 2] if se >= 0 else edges[(-se) * 2 + 1]
            if vi >= nverts:
                bad = True
                break
            poly.append((verts[vi * 3], verts[vi * 3 + 1], verts[vi * 3 + 2]))
        if bad:
            continue
        out.append({"face": fi, "material": name, "poly": poly})
    return out


def poly_area(poly):
    a = 0.0
    for i in range(len(poly)):
        x1, y1 = poly[i][0], poly[i][1]
        x2, y2 = poly[(i + 1) % len(poly)][0], poly[(i + 1) % len(poly)][1]
        a += x1 * y2 - x2 * y1
    return abs(a) * 0.5


def centroid(poly):
    n = float(len(poly))
    return (sum(p[0] for p in poly) / n, sum(p[1] for p in poly) / n)


def aabb_intersects(mn, mx, rect):
    x0, y0, x1, y1 = rect
    return not (mx[0] < x0 or mn[0] > x1 or mx[1] < y0 or mn[1] > y1)


def main():
    bsp_path = sys.argv[1] if len(sys.argv) > 1 else None
    out_path = sys.argv[2] if len(sys.argv) > 2 else None
    if not bsp_path:
        raise SystemExit("usage: n14_sunshine_cluster.py <bsp> [out.json]")

    blob = open(bsp_path, "rb").read()
    ents = B.parse_entities(B.lump(blob, B.LUMP_ENTITIES).decode("utf-8", "replace"))
    models = read_models(blob)
    faces = walk_faces(blob)

    model_owner = {}
    for e in ents:
        m = e.get("model", "")
        if m.startswith("*"):
            try:
                model_owner[int(m[1:])] = e.get("classname", "?")
            except ValueError:
                pass

    grown = (REGION[0] - GROW, REGION[1] - GROW, REGION[2] + GROW, REGION[3] + GROW)
    selected = []
    for f in faces:
        cx, cy = centroid(f["poly"])
        if grown[0] <= cx <= grown[2] and grown[1] <= cy <= grown[3]:
            selected.append(f)

    def owner_of(face_index):
        for m in models:
            if m["firstface"] <= face_index < m["firstface"] + m["numfaces"]:
                cls = model_owner.get(m["index"], "worldspawn" if m["index"] == 0 else "brush-entity?")
                return m["index"], cls
        return -1, "unattributed"

    by_owner = {}
    by_material = {}
    area_total = 0.0
    zlo, zhi = 1e30, -1e30
    for f in selected:
        a = poly_area(f["poly"])
        area_total += a
        for p in f["poly"]:
            zlo = min(zlo, p[2]); zhi = max(zhi, p[2])
        mi, cls = owner_of(f["face"])
        key = "%d:%s" % (mi, cls)
        rec = by_owner.setdefault(key, {"model": mi, "classname": cls, "faces": 0, "area": 0.0})
        rec["faces"] += 1
        rec["area"] += a
        mkey = f["material"] or "(none)"
        by_material[mkey] = by_material.get(mkey, 0) + 1

    ent_inside = {}
    ent_rows = []
    for e in ents:
        org = B.vec3(e.get("origin", ""))
        if org is None:
            continue
        if grown[0] <= org[0] <= grown[2] and grown[1] <= org[1] <= grown[3]:
            cls = e.get("classname", "?")
            ent_inside[cls] = ent_inside.get(cls, 0) + 1
            ent_rows.append({"classname": cls, "targetname": e.get("targetname", ""),
                             "origin": [round(v, 2) for v in org]})

    triggers = []
    for e in ents:
        cls = e.get("classname", "")
        if not cls.startswith("trigger_"):
            continue
        m = e.get("model", "")
        if not m.startswith("*"):
            continue
        try:
            mi = int(m[1:])
        except ValueError:
            continue
        if mi >= len(models):
            continue
        dm = models[mi]
        if aabb_intersects(dm["mins"], dm["maxs"], grown):
            triggers.append({
                "classname": cls,
                "targetname": e.get("targetname", ""),
                "model": mi,
                "mins": [round(v, 1) for v in dm["mins"]],
                "maxs": [round(v, 1) for v in dm["maxs"]],
                "covers_region": aabb_intersects(dm["mins"], dm["maxs"], REGION),
            })

    result = {
        "bsp": os.path.basename(bsp_path),
        "region": list(REGION),
        "grow": GROW,
        "selection_rule": "floor polygon (normal.z > 0.7, not sky, not tools/editor) whose XY centroid lies in region grown by GROW",
        "faces_selected": len(selected),
        "faces_total_floor": len(faces),
        "area_selected": round(area_total, 1),
        "z_range": [round(zlo, 1) if zlo < 1e29 else None, round(zhi, 1) if zhi > -1e29 else None],
        "by_owner": sorted(by_owner.values(), key=lambda r: -r["area"]),
        "by_material": dict(sorted(by_material.items(), key=lambda kv: -kv[1])[:15]),
        "entities_inside": dict(sorted(ent_inside.items(), key=lambda kv: -kv[1])),
        "entity_rows": ent_rows[:40],
        "triggers_intersecting": triggers,
        "dmodel_count": len(models),
    }

    text = json.dumps(result, indent=2, sort_keys=False)
    if out_path:
        tmp = out_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            fh.write(text)
        os.replace(tmp, out_path)
    print(text)


if __name__ == "__main__":
    main()
