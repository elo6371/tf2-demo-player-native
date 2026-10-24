#!/usr/bin/env python3
"""T65 static BSP geometry probe: bounds, walkable-floor raster, z levels, preview PNG.

Reads a BSP (LZMA-compressed lumps supported) and writes JSON + PNG under
evidence/t65-minimap/. No product/probe launch, no capture.

Face layout (Source BSP v20, 56 bytes) verified against cp_snakewater_final1:
  planenum u16@0, side u8@2, firstedge i32@4, numedges i16@8, texinfo i16@10,
  dispinfo i16@12, surfaceFogVolumeID i16@14, styles[4]@16, lightofs i32@20,
  area f32@24, lightmap mins[2]@28, size[2]@36, origFace i32@44,
  numPrims u16@48, firstPrimID u16@50, smoothingGroups u32@52.
"""
import json, lzma, math, os, struct, sys, hashlib
from collections import Counter, defaultdict

from PIL import Image, ImageDraw

LUMP_ENTITIES, LUMP_PLANES, LUMP_TEXDATA, LUMP_VERTEXES = 0, 1, 2, 3
LUMP_TEXINFO, LUMP_FACES, LUMP_EDGES = 6, 7, 12
LUMP_SURFEDGES, LUMP_MODELS = 13, 14
LUMP_TEXDATA_STRING_DATA, LUMP_TEXDATA_STRING_TABLE = 43, 44

def lzma_lump(data, fileofs):
    actual, lzsize = struct.unpack_from("<II", data, fileofs + 4)
    props = data[fileofs + 12:fileofs + 17]
    raw = data[fileofs + 17:fileofs + 17 + lzsize]
    p0 = props[0]
    lc, lp, pb = p0 % 9, (p0 // 9) % 5, p0 // 45
    dict_size = struct.unpack_from("<I", props, 1)[0]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW,
        filters=[{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": dict_size}])
    return dec.decompress(raw, max_length=actual)

class Bsp:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        d = self.data
        self.ident = d[:4]
        self.version = struct.unpack_from("<i", d, 4)[0]
        self.lumps = [struct.unpack_from("<iiii", d, 8 + i * 16) for i in range(64)]
        self._cache = {}
    def lump(self, i):
        if i in self._cache: return self._cache[i]
        fo, fl, _v, _c = self.lumps[i]
        if fl <= 0: out = b""
        elif self.data[fo:fo+4] == b"LZMA": out = lzma_lump(self.data, fo)
        else: out = self.data[fo:fo+fl]
        self._cache[i] = out
        return out
    def entities(self):
        raw = self.lump(LUMP_ENTITIES).decode("latin-1")
        ents, cur = [], None
        for line in raw.replace("\r", "").split("\n"):
            t = line.strip()
            if t == "{": cur = {}; continue
            if t == "}":
                if cur is not None: ents.append(cur)
                cur = None; continue
            if cur is None: continue
            if t.startswith('"'):
                parts = t.split('"')
                if len(parts) >= 4: cur[parts[1]] = parts[3]
        return ents
    def models(self):
        b = self.lump(LUMP_MODELS); out = []
        for o in range(0, len(b) - 47, 48):
            out.append({"mins": list(struct.unpack_from("<3f", b, o)),
                        "maxs": list(struct.unpack_from("<3f", b, o + 12)),
                        "origin": list(struct.unpack_from("<3f", b, o + 24)),
                        "headnode": struct.unpack_from("<i", b, o + 36)[0],
                        "firstface": struct.unpack_from("<i", b, o + 40)[0],
                        "numfaces": struct.unpack_from("<i", b, o + 44)[0]})
        return out
    def planes(self):
        b = self.lump(LUMP_PLANES); out = []
        for o in range(0, len(b) - 19, 20):
            out.append((struct.unpack_from("<3f", b, o), struct.unpack_from("<f", b, o + 12)[0]))
        return out
    def vertexes(self):
        b = self.lump(LUMP_VERTEXES)
        return [struct.unpack_from("<3f", b, o) for o in range(0, len(b) - 11, 12)]
    def edges(self):
        b = self.lump(LUMP_EDGES)
        return [struct.unpack_from("<2H", b, o) for o in range(0, len(b) - 3, 4)]
    def surfedges(self):
        b = self.lump(LUMP_SURFEDGES)
        return list(struct.unpack_from("<%di" % (len(b) // 4), b, 0))
    def faces(self):
        b = self.lump(LUMP_FACES); stride = 56; out = []
        for o in range(0, len(b) - stride + 1, stride):
            out.append({"planenum": struct.unpack_from("<H", b, o)[0], "side": b[o + 2],
                        "firstedge": struct.unpack_from("<i", b, o + 4)[0],
                        "numedges": struct.unpack_from("<h", b, o + 8)[0],
                        "texinfo": struct.unpack_from("<h", b, o + 10)[0],
                        "dispinfo": struct.unpack_from("<h", b, o + 12)[0]})
        return out
    def texinfo(self):
        b = self.lump(LUMP_TEXINFO); out = []
        for o in range(0, len(b) - 71, 72):
            out.append({"flags": struct.unpack_from("<i", b, o + 64)[0],
                        "texdata": struct.unpack_from("<i", b, o + 68)[0]})
        return out
    def texdata_names(self):
        td = self.lump(LUMP_TEXDATA); st = self.lump(LUMP_TEXDATA_STRING_TABLE)
        sd = self.lump(LUMP_TEXDATA_STRING_DATA); names = []
        for o in range(0, len(td) - 31, 32):
            sid = struct.unpack_from("<i", td, o + 12)[0]
            if 0 <= sid < len(st) // 4:
                so = struct.unpack_from("<i", st, sid * 4)[0]
                e = sd.find(b"\x00", so)
                names.append(sd[so:e if e >= 0 else len(sd)].decode("latin-1"))
            else:
                names.append("<bad>")
        return names

def origin_of(e):
    v = e.get("origin", "").strip().split()
    if len(v) == 3:
        try: return [float(x) for x in v]
        except ValueError: return None
    return None

def z_at(tri, x, y):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = tri
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(den) < 1e-9: return None
    u = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den
    v = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den
    if u < -0.001 or v < -0.001 or u + v > 1.001: return None
    return az + u * (bz - az) + v * (cz - az)

def main():
    path, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    bsp = Bsp(path)
    ents = bsp.entities(); models = bsp.models(); planes = bsp.planes()
    verts = bsp.vertexes(); edges = bsp.edges(); surf = bsp.surfedges()
    faces = bsp.faces(); texinfo = bsp.texinfo(); texnames = bsp.texdata_names()
    world = models[0] if models else None

    classes = Counter(e.get("classname", "?") for e in ents)
    def collect(cls):
        return [{"origin": origin_of(e), "team": e.get("team") or e.get("TeamNum"),
                 "targetname": e.get("targetname"), "target": e.get("target"),
                 "model": e.get("model"), "angles": e.get("angles")}
                for e in ents if e.get("classname") == cls]
    key_classes = ["team_control_point", "team_control_point_master", "info_player_teamspawn",
                   "team_train_watcher", "path_track", "func_capturezone", "item_teamflag",
                   "info_player_start", "info_observer_point", "func_respawnroom"]
    ent_out = {c: collect(c) for c in key_classes if classes.get(c)}

    CELL = 32.0
    cells = defaultdict(lambda: [1e30, -1e30, 0.0, "", []])  # minz maxz area material zsamples
    skipped = Counter(); floor_faces = 0
    for f in faces:
        ti = f["texinfo"]
        if ti < 0 or ti >= len(texinfo):
            skipped["bad_texinfo"] += 1; continue
        td = texinfo[ti]["texdata"]
        mat = texnames[td] if 0 <= td < len(texnames) else ""
        low = mat.lower()
        if ("sky" in low) or ("water" in low) or ("tools/" in low) or low.startswith("dev/"):
            skipped["special:" + ("sky" if "sky" in low else "water" if "water" in low else "tools")] += 1
            continue
        if f["planenum"] < 0 or f["planenum"] >= len(planes):
            skipped["bad_plane"] += 1; continue
        n, _dist = planes[f["planenum"]]
        if f["side"]: n = (-n[0], -n[1], -n[2])
        if n[2] < 0.7: skipped["not_floor"] += 1; continue
        if f["numedges"] < 3 or f["firstedge"] < 0 or f["firstedge"] + f["numedges"] > len(surf):
            skipped["bad_edges"] += 1; continue
        poly = []; ok = True
        for i in range(f["numedges"]):
            se = surf[f["firstedge"] + i]
            vi = edges[se][0] if se >= 0 else edges[-se][1]
            if vi >= len(verts): ok = False; break
            poly.append(verts[vi])
        if not ok or len(poly) < 3:
            skipped["bad_vertex"] += 1; continue
        floor_faces += 1
        p0 = poly[0]
        for k in range(1, len(poly) - 1):
            tri = [p0, poly[k], poly[k + 1]]
            ys = [p[1] for p in tri]; y0, y1 = min(ys), max(ys)
            r0 = int(math.floor(y0 / CELL)); r1 = int(math.floor(y1 / CELL))
            for row in range(r0, r1 + 1):
                yc = (row + 0.5) * CELL
                xs = []
                for a, bb in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
                    if (a[1] <= yc < bb[1]) or (bb[1] <= yc < a[1]):
                        t = (yc - a[1]) / (bb[1] - a[1])
                        xs.append(a[0] + t * (bb[0] - a[0]))
                if len(xs) < 2: continue
                x0, x1 = min(xs), max(xs)
                c0 = int(math.floor(x0 / CELL)); c1 = int(math.floor(x1 / CELL))
                for col in range(c0, c1 + 1):
                    xc = (col + 0.5) * CELL
                    zc = z_at(tri, xc, yc)
                    if zc is None: continue
                    c = cells[(col, row)]
                    if zc < c[0]: c[0] = zc
                    if zc > c[1]: c[1] = zc
                    c[2] += CELL * CELL
                    if not c[3]: c[3] = mat
                    c[4].append(zc)

    # --- playable mask ------------------------------------------------------
    # Two corrections, both derived from the map's own gameplay entities rather
    # than hand-picked constants:
    # 1. Vertical outlier rejection. pl_badwater ships a 6841-cell ground plane
    #    (NATURE/BLENDGROUNDTOGRAVEL005) at z=-2452, i.e. 2580 units below every
    #    spawn/CP/train point. Keeping it would stretch the canvas and push the
    #    real map into a corner. Keep cells whose centre z lies inside
    #    [minEntityZ-512, maxEntityZ+1024]; by construction no gameplay point
    #    can be excluded, and roofs up to 1024 units above the highest gameplay
    #    entity are still kept.
    # 2. Component filter. Keep 8-connected raster components that intersect the
    #    gameplay bbox (entity origins +/-1024 units); anything else is
    #    out-of-play detail, and it is reported rather than silently dropped.
    pts = []
    for cls in ("info_player_teamspawn", "team_control_point", "team_train_watcher",
                "path_track", "item_teamflag", "func_capturezone"):
        for e in ent_out.get(cls, []):
            if e.get("origin"): pts.append(e["origin"])
    # Both targets carry 3D-skybox COPIES of their spawns at z ~ -14600, so a
    # plain min/max over entity z puts the band in the skybox and the filter
    # becomes a no-op. Derive the main vertical band instead: histogram gameplay
    # z in 256-unit bins and take the fullest bin. Cells are then kept inside
    # [bandLo-512, bandHi+1024]; by construction every in-band gameplay point
    # survives, and roofs up to 1024 units above the band stay.
    if pts:
        bins = Counter(int(math.floor(p[2] / 256.0)) for p in pts)
        main_bin, main_count = bins.most_common(1)[0]
        band_lo, band_hi = main_bin * 256.0, (main_bin + 1) * 256.0
        inband = [p for p in pts if band_lo - 512.0 <= p[2] <= band_hi + 512.0]
        core_minx = min(p[0] for p in inband) - 1024.0; core_maxx = max(p[0] for p in inband) + 1024.0
        core_miny = min(p[1] for p in inband) - 1024.0; core_maxy = max(p[1] for p in inband) + 1024.0
        ent_minz = min(p[2] for p in inband); ent_maxz = max(p[2] for p in inband)
    else:
        core_minx = core_maxx = core_miny = core_maxy = 0.0
        ent_minz = ent_maxz = 0.0
        main_bin = main_count = 0; band_lo = band_hi = 0.0; inband = []
    zlo, zhi = band_lo - 512.0, band_hi + 1024.0
    z_dropped = [k for k, c in cells.items() if not (zlo <= (c[0] + c[1]) * 0.5 <= zhi)]
    z_drop_report = {}
    if z_dropped:
        z_drop_report = {
            "cells": len(z_dropped),
            "zRange": [min(cells[k][0] for k in z_dropped), max(cells[k][1] for k in z_dropped)],
            "bbox": [min(k[0] for k in z_dropped) * CELL, max(k[0] for k in z_dropped) * CELL + CELL,
                     min(k[1] for k in z_dropped) * CELL, max(k[1] for k in z_dropped) * CELL + CELL],
            "materials": Counter(cells[k][3] for k in z_dropped).most_common(5),
        }
        for k in z_dropped: cells.pop(k, None)
    comp_of = {}
    comps = []
    for start in cells:
        if start in comp_of: continue
        stack = [start]; comp_of[start] = len(comps); members = []
        while stack:
            cur = stack.pop(); members.append(cur)
            cx0, cy0 = cur
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if dx == 0 and dy == 0: continue
                    nb = (cx0 + dx, cy0 + dy)
                    if nb in cells and nb not in comp_of:
                        comp_of[nb] = len(comps); stack.append(nb)
        comps.append(members)
    keep_ids = set(); comp_report = []
    for ci, members in enumerate(comps):
        xs = [m[0] * CELL for m in members]; ys = [m[1] * CELL for m in members]
        bx0, bx1 = min(xs), max(xs) + CELL; by0, by1 = min(ys), max(ys) + CELL
        zs = []
        for m in members: zs.extend(cells[m][4])
        overlaps = not (bx1 < core_minx or bx0 > core_maxx or by1 < core_miny or by0 > core_maxy)
        if overlaps: keep_ids.add(ci)
        comp_report.append({"cells": len(members), "bbox": [bx0, bx1, by0, by1],
                            "zRange": [min(zs), max(zs)] if zs else [0, 0], "kept": overlaps})
    dropped = [c for c in comp_report if not c["kept"]]
    cells = {k: v for k, v in cells.items() if comp_of[k] in keep_ids}

    def levels_of(zs):
        zs = sorted(zs); lv = 1
        for i in range(1, len(zs)):
            if zs[i] - zs[i - 1] > 128.0: lv += 1
        return lv

    if cells:
        xs = [k[0] * CELL for k in cells]; ys = [k[1] * CELL for k in cells]
        play_minx, play_maxx = min(xs), max(xs) + CELL
        play_miny, play_maxy = min(ys), max(ys) + CELL
    else:
        play_minx = play_maxx = play_miny = play_maxy = 0.0

    zvals = []; multi = 0; level_hist = Counter()
    for c in cells.values():
        zc = (c[0] + c[1]) * 0.5; zvals.append(zc)
        lv = levels_of(c[4]); level_hist[lv] += 1
        if lv >= 2: multi += 1
    zvals.sort()
    def pct(p): return zvals[min(len(zvals) - 1, int(len(zvals) * p))] if zvals else 0.0
    zbands = [pct(0.02), pct(0.2), pct(0.4), pct(0.6), pct(0.8), pct(0.98)]
    area_units = sum(c[2] for c in cells.values())

    W = H = 1024
    # Frame = gameplay extent (every in-band spawn/CP/train/path point) plus a
    # 1536-unit margin, clamped to the BSP floor extent. Why not the raw floor
    # min/max: pl_badwater's floor reaches 2176 units below its lowest gameplay
    # point (out-of-play slope), which would leave a fifth of the canvas empty.
    # Why not reachable cells alone: the raster flood fill misses thin ramps and
    # would clip legitimately playable edges. Clipped cells are counted, not
    # silently discarded.
    if inband:
        fx0 = min(p[0] for p in inband) - 1536.0; fx1 = max(p[0] for p in inband) + 1536.0
        fy0 = min(p[1] for p in inband) - 1536.0; fy1 = max(p[1] for p in inband) + 1536.0
    else:
        fx0, fx1, fy0, fy1 = play_minx, play_maxx, play_miny, play_maxy
    fx0 = max(fx0, play_minx); fx1 = min(fx1, play_maxx)
    fy0 = max(fy0, play_miny); fy1 = min(fy1, play_maxy)
    spanx = fx1 - fx0; spany = fy1 - fy0
    span = max(spanx, spany, 1.0); pad = span * 0.02
    cx = (fx0 + fx1) * 0.5; cy = (fy0 + fy1) * 0.5
    half = span * 0.5 + pad
    minx, maxx = cx - half, cx + half; miny, maxy = cy - half, cy + half
    outside_frame = sum(1 for k in cells
                        if not (fx0 <= (k[0] + 0.5) * CELL <= fx1 and fy0 <= (k[1] + 0.5) * CELL <= fy1))
    outside_canvas = sum(1 for k in cells
                         if not (minx <= (k[0] + 0.5) * CELL <= maxx and miny <= (k[1] + 0.5) * CELL <= maxy))
    scale = W / (maxx - minx)
    def to_px(x, y): return ((x - minx) * scale, (maxy - y) * scale)

    img = Image.new("RGB", (W, H), (12, 14, 18)); dr = ImageDraw.Draw(img)
    band_colors = [(38, 48, 64), (52, 68, 92), (74, 92, 118), (98, 118, 142), (126, 146, 168), (156, 176, 198)]
    for (col, row), c in cells.items():
        x0, y0 = col * CELL, row * CELL; x1, y1 = x0 + CELL, y0 + CELL
        px0, py1 = to_px(x0, y0); px1, py0 = to_px(x1, y1)
        zc = (c[0] + c[1]) * 0.5
        bi = 0
        for i in range(1, len(zbands)):
            if zc >= zbands[i]: bi = i
        dr.rectangle([px0, py0, px1, py1], fill=band_colors[min(bi, len(band_colors) - 1)])
        if levels_of(c[4]) >= 2:
            dr.line([px0, py0, px1, py1], fill=(230, 120, 60), width=1)
            dr.line([px0, py1, px1, py0], fill=(230, 120, 60), width=1)

    gx = math.ceil(minx / 1024.0) * 1024.0
    while gx < maxx:
        px, _ = to_px(gx, miny)
        dr.line([px, 0, px, H], fill=(40, 44, 52), width=1)
        dr.text((px + 3, H - 16), "%d" % gx, fill=(120, 126, 136)); gx += 1024.0
    gy = math.ceil(miny / 1024.0) * 1024.0
    while gy < maxy:
        _, py = to_px(minx, gy)
        dr.line([0, py, W, py], fill=(40, 44, 52), width=1)
        dr.text((4, py - 12), "%d" % gy, fill=(120, 126, 136)); gy += 1024.0

    for e in ent_out.get("info_player_teamspawn", []):
        o = e["origin"]
        if not o: continue
        px, py = to_px(o[0], o[1])
        colr = (220, 70, 70) if str(e.get("team")) == "2" else (90, 140, 230)
        dr.ellipse([px - 3, py - 3, px + 3, py + 3], outline=colr, width=1)
    for i, e in enumerate(ent_out.get("team_control_point", [])):
        o = e["origin"]
        if not o: continue
        px, py = to_px(o[0], o[1])
        dr.ellipse([px - 8, py - 8, px + 8, py + 8], outline=(255, 220, 90), width=2)
        dr.text((px + 10, py - 6), e.get("targetname") or ("CP%d" % (i + 1)), fill=(255, 220, 90))
    for e in ent_out.get("team_train_watcher", []):
        o = e["origin"]
        if not o: continue
        px, py = to_px(o[0], o[1])
        dr.rectangle([px - 6, py - 6, px + 6, py + 6], outline=(120, 255, 160), width=2)

    dr.rectangle([0, 0, W - 1, H - 1], outline=(90, 96, 108))
    dr.text((8, 8), "%s  playable[%.0f,%.0f]x[%.0f,%.0f]  %.4f px/unit (%.2f units/px)  +Y up / y-flip" %
            (os.path.basename(path), play_minx, play_maxx, play_miny, play_maxy, scale, 1.0 / scale), fill=(220, 226, 236))
    dr.text((8, 24), "derived from BSP faces normal.z>=0.7, cell=%.0f units; orange X = >=2 z levels (gap>128)" % CELL,
            fill=(150, 156, 166))
    cells_csv = os.path.join(outdir, os.path.basename(path) + ".cells.csv.gz")
    import gzip, csv
    with gzip.open(cells_csv, "wt", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["col", "row", "minz", "maxz", "levels", "material"])
        for (col, row), c in sorted(cells.items()):
            w.writerow([col, row, "%.3f" % c[0], "%.3f" % c[1], levels_of(c[4]), c[3]])

    png = os.path.join(outdir, os.path.basename(path) + ".floorplan.png")
    img.save(png)

    out = {
        "bsp": path, "ident": bsp.ident.decode("latin-1"), "version": bsp.version,
        "bytes": os.path.getsize(path),
        "sha256": hashlib.sha256(open(path, "rb").read()).hexdigest(),
        "entityCount": len(ents), "classCounts": dict(classes.most_common(30)),
        "worldModel": world, "faceCount": len(faces), "floorFaces": floor_faces,
        "skipped": dict(skipped), "vertexCount": len(verts),
        "playableBounds": {"minX": play_minx, "maxX": play_maxx, "minY": play_miny, "maxY": play_maxy},
        "playableAreaUnits2": area_units,
        "zRange": [zvals[0], zvals[-1]] if zvals else [0, 0], "zBands": zbands,
        "cellCount": len(cells), "multiLevelCells": multi, "levelHistogram": dict(level_hist),
        "zFilter": {"zlo": zlo, "zhi": zhi, "mainBin": main_bin, "mainBinCount": main_count,
                     "band": [band_lo, band_hi], "inbandPoints": len(inband), "totalPoints": len(pts),
                     "inbandZRange": [ent_minz, ent_maxz], "dropped": z_drop_report},
        "components": {"count": len(comps), "kept": len(keep_ids), "droppedCount": len(dropped),
                        "list": sorted(comp_report, key=lambda c: -c["cells"])[:10],
                        "dropped": sorted(dropped, key=lambda c: -c["cells"])[:20]},
        "projection": {"width": W, "height": H, "minX": minx, "maxX": maxx, "minY": miny, "maxY": maxy,
                        "frameUnits": [fx0, fx1, fy0, fy1], "frameMarginUnits": 1536.0,
                        "frameSource": "gameplay entity bbox + margin, clamped to BSP floor extent",
                        "outsideFrameCells": outside_frame, "outsideCanvasCells": outside_canvas,
                        "scalePxPerUnit": scale, "unitsPerPixel": 1.0 / scale, "yFlip": True,
                        "formulaPx": "px=(x-minX)*scale", "formulaPy": "py=(maxY-y)*scale"},
        "entities": ent_out, "png": png, "cellsCsv": cells_csv,
    }
    with open(os.path.join(outdir, os.path.basename(path) + ".geometry.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    print(json.dumps({k: out[k] for k in ("bsp", "bytes", "sha256", "entityCount", "worldModel",
        "playableBounds", "playableAreaUnits2", "zRange", "zBands", "cellCount", "multiLevelCells",
        "levelHistogram", "projection", "skipped", "floorFaces", "faceCount")}, indent=2))
    for c in ("team_control_point", "team_train_watcher", "path_track"):
        if ent_out.get(c):
            print(c, "count", len(ent_out[c]))
            for e in ent_out[c][:8]: print("   ", json.dumps(e))
    if ent_out.get("info_player_teamspawn"):
        sp = ent_out["info_player_teamspawn"]
        print("spawns", len(sp), "teams", Counter(str(x.get("team")) for x in sp))
        for t in ("2", "3"):
            pts = [x["origin"] for x in sp if str(x.get("team")) == t and x["origin"]]
            if pts:
                xs = [p[0] for p in pts]; ys = [p[1] for p in pts]; zs = [p[2] for p in pts]
                print("team", t, "n", len(pts), "x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]" % (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))

if __name__ == "__main__":
    main()
