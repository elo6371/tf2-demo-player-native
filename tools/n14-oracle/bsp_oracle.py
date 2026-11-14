#!/usr/bin/env python3
"""N14 independent BSP reader for the TF2 minimap -- no product code, no product formula.

WHY EACH NUMBER IS A DIFFERENT CLAIM
  world bbox     every brush-face vertex. Includes skybox and out-of-bounds
                 terrain, so a world rect LARGER than the playable area is not
                 an error -- it is what a Source map looks like.
  floor polys   upward-facing (nz > 0.7), non-tool, non-sky faces: the walkable
                 surface as geometry, not as a declared rectangle.
  anchors       entities with gameplay positions, so orientation can be checked
                 at points a single control point cannot provide.

Use: bsp_oracle.py <map.bsp> <out.json> [--png out.png] [--size 512]
Import: read_bsp(path) -> dict
"""
import json
import lzma
import math
import struct
import sys

LUMP_ENTITIES, LUMP_PLANES, LUMP_TEXDATA, LUMP_VERTEXES = 0, 1, 2, 3
LUMP_TEXINFO, LUMP_FACES, LUMP_EDGES, LUMP_SURFEDGES = 6, 7, 12, 13
LUMP_TEXDATA_STRING_DATA, LUMP_TEXDATA_STRING_TABLE = 43, 44

ANCHOR_CLASSES = (
    "team_control_point", "team_train_watcher", "item_teamflag",
    "trigger_capture_area", "func_capturezone", "func_respawnroom",
    "info_player_start", "info_player_teamspawn", "prop_dynamic",
    # payload rail: the cart corridor is playable space, so its extent is a
    # reachability proxy that does not depend on guessing which terrain is
    # out-of-bounds. Added for N14 crop checking (pl_upward_f12).
    "path_track",
)


def lump_pair(blob, index):
    return struct.unpack_from("<ii", blob, 8 + index * 16)


def lump(blob, index):
    """Lump payload, handling Source's per-lump LZMA wrapper.

    Wrapper: b"LZMA", u32 actualSize, u32 lzmaSize, u8 packed lc/lp/pb
    (lc = p % 9; rest = p // 9; lp = rest % 5; pb = rest // 5), u32 dictSize,
    then a RAW LZMA1 stream of lzmaSize bytes at offset 17. NOT LZMA-alone --
    lzma.decompress(FORMAT_ALONE) raises "Corrupt input data" on this wrapper.
    """
    off, size = lump_pair(blob, index)
    if size <= 0:
        return b""
    raw = blob[off:off + size]
    if raw[:4] != b"LZMA":
        return raw
    actual, lz = struct.unpack_from("<II", raw, 4)
    prop = raw[12]
    lc, rest = prop % 9, prop // 9
    lp, pb = rest % 5, rest // 5
    dict_size = struct.unpack_from("<I", raw, 13)[0]
    filters = [{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb,
                "dict_size": dict_size}]
    return lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=filters).decompress(
        raw[17:17 + lz], max_length=actual)


def parse_entities(text):
    ents, cur = [], None
    for raw in text.split("\n"):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("{"):
            cur = {}
        elif line.startswith("}"):
            if cur is not None:
                ents.append(cur)
            cur = None
        elif line.startswith('"') and cur is not None:
            parts = line.split('"')
            if len(parts) >= 3:
                cur[parts[1]] = parts[3] if len(parts) > 3 else ""
    return ents


def vec3(s):
    try:
        a, b, c = (s or "").split()[:3]
        return [float(a), float(b), float(c)]
    except Exception:
        return None


def read_bsp(path):
    blob = open(path, "rb").read()
    ident, bsp_version = struct.unpack_from("<ii", blob, 0)
    if ident != 0x50534256:
        raise SystemExit("not a VBSP file: %s" % hex(ident))

    ents = parse_entities(lump(blob, LUMP_ENTITIES).decode("utf-8", "replace"))

    str_data = lump(blob, LUMP_TEXDATA_STRING_DATA)
    str_tbl = lump(blob, LUMP_TEXDATA_STRING_TABLE)
    nstr = len(str_tbl) // 4
    offs = struct.unpack_from("<%di" % nstr, str_tbl, 0) if nstr else ()
    strings = []
    for ofs in offs:
        if 0 <= ofs < len(str_data):
            end = str_data.find(b"\x00", ofs)
            strings.append(str_data[ofs:end if end >= 0 else len(str_data)].decode("ascii", "replace"))
        else:
            strings.append("")

    tex = lump(blob, LUMP_TEXDATA)
    td_names = []
    for k in range(len(tex) // 32):
        sid = struct.unpack_from("<i", tex, k * 32 + 12)[0]
        td_names.append(strings[sid].lower() if 0 <= sid < len(strings) else "")

    vraw = lump(blob, LUMP_VERTEXES)
    nverts = len(vraw) // 12
    verts = struct.unpack_from("<%df" % (nverts * 3), vraw, 0) if nverts else ()
    eraw = lump(blob, LUMP_EDGES)
    edges = struct.unpack_from("<%dH" % (len(eraw) // 2), eraw, 0) if eraw else ()
    sraw = lump(blob, LUMP_SURFEDGES)
    surf = struct.unpack_from("<%di" % (len(sraw) // 4), sraw, 0) if sraw else ()
    praw = lump(blob, LUMP_PLANES)
    nplanes = len(praw) // 20
    planes = struct.unpack_from("<%df" % (nplanes * 4), praw, 0) if nplanes else ()
    tiraw = lump(blob, LUMP_TEXINFO)
    nti = len(tiraw) // 72
    # texdata index is the LAST field of Source's texinfo_t, not the first:
    # 2 textureVecs float[4] (32 B) + 2 lightmapVecs float[4] (32 B) = 64,
    # then int flags at +64, int texdata at +68. Reading +0 instead returns the
    # first textureVec float as an int -- for cp_process_f12 that lands on
    # texdata[0] == tools/toolsnodraw and silently classifies 1185 real upward
    # faces as tool geometry, while 141 genuine tool faces survive as floor.
    # The product reads +68 (native/src/bsp_map.cpp:777); a checker that reads
    # a different field than the product is measuring a different map.
    ti_tex = [struct.unpack_from("<i", tiraw, k * 72 + 68)[0] for k in range(nti)]
    fraw = lump(blob, LUMP_FACES)
    nfaces = len(fraw) // 56

    lo = [1e30, 1e30, 1e30]
    hi = [-1e30, -1e30, -1e30]
    floors = []
    n_tool = n_sky = n_used = n_bad = 0
    for fi in range(nfaces):
        base = fi * 56
        planenum = struct.unpack_from("<H", fraw, base)[0]
        side = fraw[base + 2]
        firstedge = struct.unpack_from("<i", fraw, base + 4)[0]
        numedges = struct.unpack_from("<h", fraw, base + 8)[0]
        texinfo = struct.unpack_from("<h", fraw, base + 10)[0]
        if planenum >= nplanes or numedges < 3 or firstedge < 0 or firstedge + numedges > len(surf):
            n_bad += 1
            continue
        pb = planenum * 4
        nx, ny, nz = planes[pb], planes[pb + 1], planes[pb + 2]
        if side:
            nx, ny, nz = -nx, -ny, -nz
        ti = ti_tex[texinfo] if 0 <= texinfo < nti else -1
        name = td_names[ti] if 0 <= ti < len(td_names) else ""
        if name.startswith("tools/") or name.startswith("editor/"):
            n_tool += 1
            continue
        poly, ok = [], True
        for k in range(numedges):
            se = surf[firstedge + k]
            vi = edges[se * 2] if se >= 0 else edges[(-se) * 2 + 1]
            if vi >= nverts:
                ok = False
                break
            poly.append((verts[vi * 3], verts[vi * 3 + 1], verts[vi * 3 + 2]))
        if not ok:
            n_bad += 1
            continue
        n_used += 1
        is_sky = "skybox" in name
        if is_sky:
            n_sky += 1
        for (x, y, z) in poly:
            if x < lo[0]: lo[0] = x
            if y < lo[1]: lo[1] = y
            if z < lo[2]: lo[2] = z
            if x > hi[0]: hi[0] = x
            if y > hi[1]: hi[1] = y
            if z > hi[2]: hi[2] = z
        if nz > 0.7 and not is_sky:
            floors.append(poly)

    anchors = []
    counts = {}
    for e in ents:
        cls = e.get("classname", "")
        counts[cls] = counts.get(cls, 0) + 1
        if cls in ANCHOR_CLASSES:
            anchors.append({
                "classname": cls,
                "targetname": e.get("targetname", ""),
                "origin": vec3(e.get("origin", "")),
                "team": e.get("team", ""),
                # info_player_teamspawn carries TeamNum/StartDisabled, not team
                "teamnum": e.get("TeamNum", e.get("teamnum", "")),
                "startdisabled": e.get("StartDisabled", e.get("startdisabled", "")),
                "target": e.get("target", ""),
                "model": e.get("model", ""),
            })
    wents = next((e for e in ents if e.get("classname") == "worldspawn"), {})
    return {
        "bsp_version": bsp_version, "faces_total": nfaces, "faces_used": n_used,
        "faces_skipped_tool": n_tool, "faces_sky_material": n_sky, "faces_bad": n_bad,
        "world_bbox_min": lo, "world_bbox_max": hi, "floors": floors, "anchors": anchors,
        "entity_total": len(ents), "entity_counts": counts,
        "worldspawn_mins": vec3(wents.get("world_mins", "")),
        "worldspawn_maxs": vec3(wents.get("world_maxs", "")),
        "skyname": wents.get("skyname", ""),
    }


def floor_bbox(floors):
    if not floors:
        return None
    x0 = y0 = 1e30
    x1 = y1 = -1e30
    for poly in floors:
        for (x, y, _z) in poly:
            if x < x0: x0 = x
            if y < y0: y0 = y
            if x > x1: x1 = x
            if y > y1: y1 = y
    return [x0, y0, x1, y1]


def rasterise(floors, lo, hi, scale, w, h):
    """Even-odd scanline fill of upward faces, world -> image rows top-down."""
    grid = bytearray(w * h)
    covered = 0
    for poly in floors:
        pts = [((x - lo[0]) * scale, (hi[1] - y) * scale) for (x, y, _z) in poly]
        ys = [p[1] for p in pts]
        y0 = max(0, int(math.floor(min(ys))))
        y1 = min(h - 1, int(math.ceil(max(ys))))
        n = len(pts)
        for yy in range(y0, y1 + 1):
            cy = yy + 0.5
            xs = []
            for k in range(n):
                xa, ya = pts[k]
                xb, yb = pts[(k + 1) % n]
                if (ya > cy) != (yb > cy):
                    xs.append(xa + (cy - ya) * (xb - xa) / (yb - ya))
            xs.sort()
            for k in range(0, len(xs) - 1, 2):
                a = max(0, int(math.ceil(xs[k] - 0.5)))
                b = min(w - 1, int(math.floor(xs[k + 1] - 0.5)))
                for xx in range(a, b + 1):
                    idx = yy * w + xx
                    if not grid[idx]:
                        grid[idx] = 255
                        covered += 1
    return grid, covered


def write_png(path, w, h, gray):
    import zlib
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(gray[y * w:(y + 1) * w])

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += chunk(b"IEND", b"")
    open(path, "wb").write(out)


def collinear_triples(points, min_area=256.0):
    n = good = 0
    n = len(points)
    for i in range(n):
        for j in range(i + 1, n):
            for k in range(j + 1, n):
                ax, ay = points[j][0] - points[i][0], points[j][1] - points[i][1]
                bx, by = points[k][0] - points[i][0], points[k][1] - points[i][1]
                if abs(ax * by - ay * bx) * 0.5 > min_area:
                    good += 1
    return good


def main():
    path, out_json = sys.argv[1], sys.argv[2]
    png, size = None, 512
    args, i = sys.argv[3:], 0
    while i < len(args):
        if args[i] == "--png":
            png = args[i + 1]; i += 2
        elif args[i] == "--size":
            size = int(args[i + 1]); i += 2
        else:
            i += 1

    m = read_bsp(path)
    fb = floor_bbox(m["floors"])
    res = {
        "map_file": path, "bsp_version": m["bsp_version"],
        "faces_total": m["faces_total"], "faces_used": m["faces_used"],
        "faces_skipped_tool": m["faces_skipped_tool"],
        "faces_sky_material": m["faces_sky_material"], "faces_bad": m["faces_bad"],
        "world_bbox_min": [round(v, 1) for v in m["world_bbox_min"]],
        "world_bbox_max": [round(v, 1) for v in m["world_bbox_max"]],
        "worldspawn_mins": m["worldspawn_mins"], "worldspawn_maxs": m["worldspawn_maxs"],
        "skyname": m["skyname"], "floor_face_count": len(m["floors"]),
        "floor_bbox": [round(v, 1) for v in fb] if fb else None,
        "entity_total": m["entity_total"],
        "entity_counts": dict(sorted(m["entity_counts"].items(), key=lambda kv: -kv[1])[:30]),
        "anchors": m["anchors"],
        "landmarks": {
            "control_points": sum(1 for a in m["anchors"] if a["classname"] == "team_control_point"),
            "control_point_noncollinear_triples": collinear_triples(
                [[a["origin"][0], a["origin"][1]] for a in m["anchors"]
                 if a["classname"] == "team_control_point" and a["origin"]]),
            "spawn_points": sum(1 for a in m["anchors"] if a["classname"] == "info_player_teamspawn"),
            "noncollinear_triples_all_anchors": collinear_triples(
                [[a["origin"][0], a["origin"][1]] for a in m["anchors"] if a["origin"]]),
        },
    }
    if fb:
        spanx, spany = fb[2] - fb[0], fb[3] - fb[1]
        scale = (size - 1) / max(spanx, spany)
        w, h = max(1, int(spanx * scale) + 1), max(1, int(spany * scale) + 1)
        grid, covered = rasterise(m["floors"], [fb[0], fb[1]], [fb[2], fb[3]], scale, w, h)
        res["raster"] = {"width": w, "height": h, "scale_px_per_unit": scale,
                         "covered_pixels": covered, "bbox_pixels": w * h,
                         "coverage_of_floor_bbox": round(covered / float(w * h), 4)}
        if png:
            write_png(png, w, h, grid)
            res["raster"]["path"] = png
    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump(res, fh, indent=1)
    print(json.dumps({
        "map": path.replace("\\", "/").rsplit("/", 1)[-1],
        "faces_used": m["faces_used"], "floor_faces": len(m["floors"]),
        "floor_bbox": [round(v, 1) for v in fb] if fb else None,
        "coverage_of_floor_bbox": res.get("raster", {}).get("coverage_of_floor_bbox"),
        "anchors": len(m["anchors"]),
    }))


if __name__ == "__main__":
    main()
