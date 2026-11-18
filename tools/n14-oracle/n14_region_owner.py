#!/usr/bin/env python3
"""N14: classify a rectangle of BSP brush geometry by OWNER ENTITY.

Why this file exists: the earlier one-off classifiers in .scratch answered the
lead's two questions, but they were throwaway scripts. This is the same answer
from a checked-in tool, and it deliberately carries its own face walk so that a
break in bsp_oracle.read_bsp cannot silently change the verdict -- both read
texdata at +68 (native/src/bsp_map.cpp:777), and bsp_oracle.read_bsp is used
only for parse_entities/lump.

It answers, for one XY rectangle:
  (a) which entity owns the brush faces there -- by dmodel -> entity, with the
      owning model index and the entity's classname and targetname;
  (b) which trigger_* brush models intersect that rectangle in XY.

Selection rule, stated so the number can be argued with: a face is "in" when the
XY centroid of its vertex loop is inside RECT. Area is the XY projection.

Usage: n14_region_owner.py <bsp> <minX> <minY> <maxX> <maxY>
"""
import struct
import sys

LUMP_PLANES, LUMP_TEXDATA, LUMP_VERTEXES = 1, 2, 3
LUMP_TEXINFO, LUMP_FACES, LUMP_EDGES, LUMP_SURFEDGES = 6, 7, 12, 13
LUMP_MODELS, LUMP_ENTITIES = 14, 0
DMODEL_BYTES = 48


def lump(blob, index):
    # A VBSP lump can live LZMA-compressed inside the pakfile, in which case the
    # 16-byte directory entry carries size 0 and the real stream is at
    # (offset>>8, offset&0xFF). Returning b"" for those silently produced an
    # empty entity table and an empty face list here -- the first run of this
    # tool reported "0 faces, 0 triggers" on a map that has 31 triggers. The
    # decompressor is bsp_oracle.lump, which already does this; only the texinfo
    # arithmetic below is deliberately re-stated.
    import os as _os
    import sys as _sys
    _here = _os.path.dirname(_os.path.abspath(__file__))
    if _here not in _sys.path:
        _sys.path.insert(0, _here)
    import bsp_oracle as _B
    return _B.lump(blob, index)


def parse_entities(text):
    ents, cur, key = [], {}, None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("{"):
            cur = {}
            continue
        if s.startswith("}"):
            ents.append(cur)
            cur = None
            continue
        if cur is None:
            continue
        if s.startswith('"'):
            parts = s.split('"')
            if len(parts) >= 4:
                cur[parts[1].lower()] = parts[3]
    return ents


def texnames(blob):
    sd = lump(blob, 43)
    st = lump(blob, 44)
    n = len(st) // 4
    offs = struct.unpack_from("<%di" % n, st, 0) if n else ()
    strings = []
    for o in offs:
        if 0 <= o < len(sd):
            e = sd.find(b"\x00", o)
            strings.append(sd[o:e if e >= 0 else len(sd)].decode("ascii", "replace"))
        else:
            strings.append("")
    tex = lump(blob, LUMP_TEXDATA)
    out = []
    for k in range(len(tex) // 32):
        sid = struct.unpack_from("<i", tex, k * 32 + 12)[0]
        out.append(strings[sid].lower() if 0 <= sid < len(strings) else "")
    return out


def read_models(blob):
    raw = lump(blob, LUMP_MODELS)
    out = []
    for i in range(len(raw) // DMODEL_BYTES):
        b = i * DMODEL_BYTES
        mn = struct.unpack_from("<3f", raw, b)
        mx = struct.unpack_from("<3f", raw, b + 12)
        hn, ff, nf = struct.unpack_from("<3i", raw, b + 36)
        out.append({"index": i, "mins": mn, "maxs": mx,
                    "headnode": hn, "firstface": ff, "numfaces": nf})
    return out


def walk(blob):
    names = texnames(blob)
    vraw = lump(blob, LUMP_VERTEXES)
    nv = len(vraw) // 12
    verts = struct.unpack_from("<%df" % (nv * 3), vraw, 0) if nv else ()
    eraw = lump(blob, LUMP_EDGES)
    edges = struct.unpack_from("<%dH" % (len(eraw) // 2), eraw, 0) if eraw else ()
    sraw = lump(blob, LUMP_SURFEDGES)
    surf = struct.unpack_from("<%di" % (len(sraw) // 4), sraw, 0) if sraw else ()
    praw = lump(blob, LUMP_PLANES)
    npl = len(praw) // 20
    planes = struct.unpack_from("<%df" % (npl * 4), praw, 0) if npl else ()
    tiraw = lump(blob, LUMP_TEXINFO)
    nti = len(tiraw) // 72
    # texdata index is the LAST int of the 72-byte texinfo_t: +68, after
    # int flags at +64. Reading +0 yields the first textureVec float as an int.
    ti_tex = [struct.unpack_from("<i", tiraw, k * 72 + 68)[0] for k in range(nti)]
    fraw = lump(blob, LUMP_FACES)
    nf = len(fraw) // 56
    out = []
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
        pts, ok = [], True
        for k in range(ne):
            se = surf[fe + k]
            vi = edges[se * 2] if se >= 0 else edges[(-se) * 2 + 1]
            if vi >= nv:
                ok = False
                break
            pts.append((verts[vi * 3], verts[vi * 3 + 1], verts[vi * 3 + 2]))
        if not ok or len(pts) < 3:
            continue
        t = ti_tex[ti] if 0 <= ti < nti else -1
        mat = names[t] if 0 <= t < len(names) else ""
        out.append({"face": fi, "poly": pts, "nz": nz, "material": mat})
    return out


def area_xy(pts):
    a = 0.0
    n = len(pts)
    for k in range(n):
        x0, y0, _ = pts[k]
        x1, y1, _ = pts[(k + 1) % n]
        a += x0 * y1 - x1 * y0
    return abs(a) * 0.5


def owner_of_face(models, fi):
    for m in models:
        if m["numfaces"] > 0 and m["firstface"] <= fi < m["firstface"] + m["numfaces"]:
            return m
    return None


def main(argv):
    bsp = argv[1]
    rect = tuple(float(x) for x in argv[2:6])
    blob = open(bsp, "rb").read()
    ents = parse_entities(lump(blob, LUMP_ENTITIES).decode("utf-8", "replace"))
    models = read_models(blob)
    # entity by model index: worldspawn owns model 0; a brush entity names "*N"
    ent_of_model = {}
    for e in ents:
        mdl = e.get("model", "")
        if mdl.startswith("*") and mdl[1:].isdigit():
            ent_of_model[int(mdl[1:])] = e
    ent_of_model.setdefault(0, next((e for e in ents if e.get("classname") == "worldspawn"), {}))

    faces = walk(blob)
    sel = []
    for f in faces:
        cx = sum(p[0] for p in f["poly"]) / len(f["poly"])
        cy = sum(p[1] for p in f["poly"]) / len(f["poly"])
        if rect[0] <= cx <= rect[2] and rect[1] <= cy <= rect[3]:
            sel.append(f)

    print("BSP       : %s" % bsp)
    print("RECT      : x %.0f..%.0f  y %.0f..%.0f" % (rect[0], rect[2], rect[1], rect[3]))
    print("(a) faces whose XY centroid is inside the rect: %d" % len(sel))

    by_mat, by_owner, up_owner = {}, {}, {}
    for f in sel:
        m = owner_of_face(models, f["face"])
        mi = m["index"] if m else -1
        e = ent_of_model.get(mi, {})
        cls = e.get("classname", "<no-entity>")
        tgt = e.get("targetname", "")
        key = "%s %s (dmodel %d)" % (cls, ("[%s]" % tgt) if tgt else "", mi)
        by_mat[f["material"]] = by_mat.get(f["material"], 0) + 1
        rec = by_owner.setdefault(key, [0, 0.0])
        rec[0] += 1
        rec[1] += area_xy(f["poly"])
        if f["nz"] > 0.7:
            r2 = up_owner.setdefault(key, [0, 0.0])
            r2[0] += 1
            r2[1] += area_xy(f["poly"])

    print("  by material:")
    for k, v in sorted(by_mat.items(), key=lambda kv: -kv[1]):
        print("    %-40s %4d" % (k or "<none>", v))
    print("  by OWNER ENTITY (all faces / XY area u^2):")
    for k, v in sorted(by_owner.items(), key=lambda kv: -kv[1][1]):
        print("    %-46s %4d  %14.1f" % (k, v[0], v[1]))
    print("  by OWNER ENTITY, upward faces only (nz>0.7):")
    for k, v in sorted(up_owner.items(), key=lambda kv: -kv[1][1]):
        print("    %-46s %4d  %14.1f" % (k, v[0], v[1]))

    print()
    print("(b) trigger_* brush models intersecting the rect in XY")
    n_trig = 0
    for mi, e in sorted(ent_of_model.items()):
        if not e.get("classname", "").startswith("trigger_"):
            continue
        n_trig += 1
        m = models[mi] if mi < len(models) else None
        if m is None:
            continue
        mn, mx = m["mins"], m["maxs"]
        if mx[0] < rect[0] or mn[0] > rect[2] or mx[1] < rect[1] or mn[1] > rect[3]:
            continue
        print("    HIT %-24s [%s] dmodel %d bbox x %.0f..%.0f y %.0f..%.0f" % (
            e.get("classname"), e.get("targetname", ""), mi, mn[0], mx[0], mn[1], mx[1]))
    print("    trigger_* brush-model entities total: %d; intersecting this rect: see HIT lines" % n_trig)

    print()
    print("(c) entities whose origin is inside the rect")
    by_cls = {}
    for e in ents:
        o = e.get("origin", "")
        if not o:
            continue
        try:
            x, y, z = (float(v) for v in o.split()[:3])
        except ValueError:
            continue
        if rect[0] <= x <= rect[2] and rect[1] <= y <= rect[3]:
            by_cls[e.get("classname", "")] = by_cls.get(e.get("classname", ""), 0) + 1
    for k, v in sorted(by_cls.items(), key=lambda kv: -kv[1]):
        print("    %-32s %3d" % (k, v))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
