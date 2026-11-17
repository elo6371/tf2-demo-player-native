#!/usr/bin/env python3
"""Displacement-grid parametrisation checker (T105 task-5).

Three readings of the same defect. The engine walks a patch with
    k = i * size + j    i along points[0]->points[1], j along points[0]->points[3]
and reads dispvert[k] from that same linear list; the native renderer used to walk
it transposed (k = y*size + x, strides swapped), so every sewn displacement seam
pulled apart.

  seam   - adjacent displacement faces: how far apart are the boundary vertices the
           author sewed? Measured on the triangles the product really emits
           (probe --face-triangles) plus a from-BSP engine oracle as the ceiling.
  parity - pl_upward_f12 face 13233 against the engine lattice from the BSP:
           per-vertex deviation and triangle index triples in engine index space.
  frames - world-disp-param-capture.sh bitmaps -> enclosed-clear and
           sky-through-crack pixel readings.

Read-only: parses a BSP and the probe's text output.

usage:
  world-disp-param-check.py seam   --mode product|engine [--map NAME|--all] [--probe EXE]
  world-disp-param-check.py parity [--face 13233] [--probe EXE] [--engine-table FILE]
  world-disp-param-check.py frames --dir DIR [--before DIR]
"""

import argparse
import json
import lzma
import math
import os
import re
import struct
import subprocess
import sys

CLEAR = (14, 18, 22)
DEPTH_FAR_SUM = 12        # a --world-depth pixel with no geometry sums to <= 12
SEAM_EXACT = 0.01         # "the same vertex" in map units
SEAM_ENDPOINT = 0.5       # an edge pair must share its endpoints this well

TF_ROOT_DEFAULT = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO_ROOT_DEFAULT = "E:/demo测试"
PROBE_DEFAULT = "native/build-nmake/world_geometry_probe.exe"

# Same nine maps and same bsp precedence as world-holes-census.sh:14-26.
MAPS = [
    ("pl_upward_f12", "maps/pl_upward_f12.bsp"),
    ("cp_process_f12", "maps/cp_process_f12.bsp"),
    ("cp_sunshine", "maps/cp_sunshine.bsp"),
    ("cp_sultry_b8a", "DEMO:cp_sultry_b8a/地图/cp_sultry_b8a.bsp"),
    ("cp_gullywash_f9", "maps/cp_gullywash_f9.bsp"),
    ("cp_reckoner", "maps/cp_reckoner.bsp"),
    ("koth_bagel_rc11", "DEMO:koth_bagel_rc11/地图/koth_bagel_rc11.bsp"),
    ("koth_ashville_final1", "maps/koth_ashville_final1.bsp"),
    ("koth_product_final", "maps/koth_product_final.bsp"),
]

LUMP_VERTS, LUMP_FACES, LUMP_EDGES = 3, 7, 12
LUMP_SURF, LUMP_DISPINFO, LUMP_DISPVERTS = 13, 26, 33
DISPINFO_SIZE = 176
DISPVERT_SIZE = 20
class Bsp:
    """Just the lumps this question needs: polygon, displacement, lattice.

    Field offsets are the ones native/src/bsp_map.cpp itself reads, so the
    oracle and the renderer cannot disagree about the file format:
      face (56 B): plane u16 +0, side u8 +2, firstEdge i32 +4, count i16 +8,
                   texinfo i16 +10, dispinfo i16 +12
      dispinfo (176 B): start vertex +0, vertStart i32 +12, power i32 +20,
                   mapFace u16 +36, edge neighbours at +48 (4 edges x 2 subs x 6 B)
      dispvert (20 B): normal xyz +0, dist +12, alpha +16
    """

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as handle:
            self.buf = handle.read()
        if len(self.buf) < 8 + 64 * 16:
            raise ValueError("not a bsp: %s" % path)
        self.lumps = []
        for i in range(64):
            self.lumps.append(struct.unpack_from("<ii", self.buf, 8 + i * 16))
        self.verts = self._lump(LUMP_VERTS)
        self.faces = self._lump(LUMP_FACES)
        self.edges = self._lump(LUMP_EDGES)
        self.surf = self._lump(LUMP_SURF)
        self.dispinfo = self._lump(LUMP_DISPINFO)
        self.dispverts = self._lump(LUMP_DISPVERTS)
        self.ndisp = len(self.dispinfo) // DISPINFO_SIZE
        self._polygon = {}

    def _lump(self, index):
        off, length = self.lumps[index]
        if off == 0 or length == 0:
            return b""
        if self.buf[off:off + 4] == b"LZMA":
            outsz = struct.unpack_from("<i", self.buf, off + 4)[0]
            props = self.buf[off + 12:off + 17]
            packed = self.buf[off + 17:off + length]
            return lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(
                props + struct.pack("<Q", outsz) + packed)
        return self.buf[off:off + length]

    def vec3(self, table, off):
        return list(struct.unpack_from("<3f", table, off))

    def polygon(self, face):
        if face in self._polygon:
            return self._polygon[face]
        base = face * 56
        first = struct.unpack_from("<i", self.faces, base + 4)[0]
        count = struct.unpack_from("<h", self.faces, base + 8)[0]
        poly = []
        if 0 <= first and count >= 3:
            for k in range(count):
                surf = struct.unpack_from("<i", self.surf, (first + k) * 4)[0]
                edge = -surf if surf < 0 else surf
                vi = struct.unpack_from("<H", self.edges, edge * 4 + (2 if surf < 0 else 0))[0]
                poly.append(self.vec3(self.verts, vi * 12))
        self._polygon[face] = poly
        return poly

    def d_start(self, di):
        return self.vec3(self.dispinfo, di * DISPINFO_SIZE)

    def d_power(self, di):
        return struct.unpack_from("<i", self.dispinfo, di * DISPINFO_SIZE + 20)[0]

    def d_mapface(self, di):
        return struct.unpack_from("<H", self.dispinfo, di * DISPINFO_SIZE + 36)[0]

    def d_vertstart(self, di):
        return struct.unpack_from("<i", self.dispinfo, di * DISPINFO_SIZE + 12)[0]

    def d_edges(self, di):
        base = di * DISPINFO_SIZE + 48
        out = []
        for e in range(4):
            subs = []
            for s in range(2):
                o = base + e * 12 + s * 6
                subs.append([struct.unpack_from("<H", self.dispinfo, o)[0],
                             self.dispinfo[o + 2], self.dispinfo[o + 3], self.dispinfo[o + 4]])
            out.append(subs)
        return out
def dist3(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def corner_of(poly, start):
    """Which polygon corner is the displacement origin. Same rule as the renderer:
    nearest corner to dispinfo.start, and only usable when it is within 1 unit."""
    best, best_d2 = 0, 1.0e30
    for i in range(4):
        d2 = sum((poly[i][k] - start[k]) ** 2 for k in range(3))
        if d2 < best_d2:
            best_d2, best = d2, i
    return best, best_d2


def quad_of(poly, corner):
    return [poly[(corner + i) % 4] for i in range(4)]


def grid_engine(bsp, di):
    """The engine's own walk, as published in the research notes (Q2b):

        index  k = vertStart + i*size + j
        i runs points[0] -> points[1] with stride size,
        j runs points[0] -> points[3] with stride 1,
        position = bilinear(u=i/(size-1), v=j/(size-1)) + dispvert[k].xyz*dist

    points[] is the face polygon rotated so points[0] is the corner nearest
    dispinfo.start -- the same rotation the renderer uses. Returned as
    grid[i][j] so it can be compared with a lattice rebuilt from triangles.
    """
    size = (1 << bsp.d_power(di)) + 1
    if size < 2:
        return None
    vertices = quad_of(bsp.polygon(bsp.d_mapface(di)), corner_of(bsp.polygon(bsp.d_mapface(di)), bsp.d_start(di))[0])
    vert_start = bsp.d_vertstart(di)
    step = 1.0 / float(size - 1)
    edge_i = [(vertices[1][k] - vertices[0][k]) * step for k in range(3)]
    edge_j = [(vertices[2][k] - vertices[3][k]) * step for k in range(3)]
    grid = []
    for i in range(size):
        a0 = [vertices[0][k] + edge_i[k] * i for k in range(3)]
        a1 = [vertices[3][k] + edge_j[k] * i for k in range(3)]
        along = [(a1[k] - a0[k]) * step for k in range(3)]
        row = []
        for j in range(size):
            flat = [a0[k] + along[k] * j for k in range(3)]
            off = (vert_start + i * size + j) * DISPVERT_SIZE
            vx, vy, vz, dist = struct.unpack_from("<4f", bsp.dispverts, off)
            row.append([flat[0] + vx * dist, flat[1] + vy * dist, flat[2] + vz * dist])
        grid.append(row)
    return grid


def sides_of(grid):
    """The four boundary polylines of a square lattice, as the research oracle
    defines them. Order is meaningless on purpose: the caller tries all pairs."""
    size = len(grid)
    return [
        [grid[i][0] for i in range(size)],
        [grid[size - 1][j] for j in range(size)],
        [grid[i][size - 1] for i in range(size)],
        [grid[0][j] for j in range(size)],
    ]


def seam_dist(a, b):
    """Max per-vertex gap between two boundary polylines that ought to be sewn.

    The two patches have opposite traversal order along a shared edge, so the
    direct pairing is tried first and the reversed one second; the pair whose
    endpoints agree better is the alignment. An edge whose endpoints are more
    than SEAM_ENDPOINT apart is not a pair at all -> None (the old metric would
    have called that a huge gap; this is the research oracle's definition)."""
    direct = max(dist3(a[0], b[0]), dist3(a[-1], b[-1]))
    reversed_ = max(dist3(a[0], b[-1]), dist3(a[-1], b[0]))
    if direct <= reversed_:
        target, ends = b, direct
    else:
        target, ends = list(reversed(b)), reversed_
    if ends > SEAM_ENDPOINT:
        return None
    return max(dist3(a[k], target[k]) for k in range(min(len(a), len(target))))
def sewn_pairs(bsp):
    """Deduped displacement-adjacency pairs, straight from the dispinfo edge
    neighbour table: 4 edges x 2 sub-edges, neighbour u16 at +0 of each 6-byte
    record. 0xFFFF is 'no neighbour'. This is the map's own statement about which
    patches were sewn together -- the thing the seam metric must honour, whether
    or not the renderer agrees with it yet."""
    seen, pairs = set(), []
    for di in range(bsp.ndisp):
        for edge in bsp.d_edges(di):
            for sub in edge:
                nb = sub[0]
                if nb == 0xFFFF or nb >= bsp.ndisp:
                    continue
                key = (min(di, nb), max(di, nb))
                if key in seen:
                    continue
                seen.add(key)
                pairs.append(key)
    return pairs


def disp_faces(bsp):
    """Face index -> dispinfo index for every displacement face this checker can
    reason about (4-corner polygon, power 2..4, origin corner within 1 unit)."""
    out = {}
    for di in range(bsp.ndisp):
        power = bsp.d_power(di)
        if power < 2 or power > 4:
            continue
        face = bsp.d_mapface(di)
        poly = bsp.polygon(face)
        if len(poly) != 4:
            continue
        _corner, corner_d2 = corner_of(poly, bsp.d_start(di))
        if corner_d2 > 1.0:
            continue
        out.setdefault(di, face)
    return out


def run_probe(probe, bsp_path, face_ids, tf_root=TF_ROOT_DEFAULT, timeout=900):
    """One probe run: face summary lines plus every emitted triangle of those
    faces. Read-only; the probe parses the same bsp file."""
    command = [probe, "--tf-root", tf_root, "--map", bsp_path, "--eye", "0,0,0",
               "--face-info", ",".join(str(f) for f in face_ids), "--face-triangles"]
    done = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    if done.returncode != 0:
        raise RuntimeError("probe failed rc=%d: %s" % (done.returncode, done.stderr.strip()[:400]))
    triangles, info = [], {}
    for line in done.stdout.splitlines():
        if line.startswith("geometry face-tri "):
            fields = {}
            for token in line.split():
                if "=" in token:
                    key, _sep, value = token.partition("=")
                    fields[key] = value
            pos = {}
            for corner in ("a", "b", "c"):
                start = line.find(" %s=(" % corner)
                if start < 0:
                    continue
                body = line[start + len(corner) + 3:line.find(")", start)]
                pos[corner] = [float(v) for v in body.split(",")]
            if len(pos) != 3:
                continue
            triangles.append({
                "id": int(fields.get("id", -1)),
                "face": int(fields.get("face", -1)),
                "disp": int(fields.get("disp", 0)),
                "flip": int(fields.get("flip", 0)),
                "pos": [pos["a"], pos["b"], pos["c"]],
            })
        elif line.startswith("geometry face-info "):
            fields = {}
            for token in line.split():
                if "=" in token:
                    key, _sep, value = token.partition("=")
                    fields[key] = value
            if "index" in fields:
                info[int(fields["index"])] = fields
    return triangles, info
def _key(point):
    return (round(point[0], 2), round(point[1], 2), round(point[2], 2))


def lattice_from_triangles(triangles, size):
    """Rebuild the size x size lattice from emitted triangles alone.

    Convention-free by construction: it never assumes where i or j points, only
    that the renderer emits, for every cell, a pair of triangles that share their
    first vertex (p00) and one further vertex (p11), leaving p10 in the first
    triangle and p01 in the second. pushTriangle may reverse b and c to orient the
    triangle (T11) but never a, so p00 survives any winding rule -- which is
    exactly why this measurement cannot be fooled by the defect it measures: if
    the lattice is transposed, the rebuilt grid is transposed with it.

    Returns (grid, None) with grid[i][j], or (None, reason)."""
    cells = size - 1
    if len(triangles) != 2 * cells * cells:
        return None, "triangle count %d != %d" % (len(triangles), 2 * cells * cells)
    position = {}
    cell = {}
    for index in range(cells * cells):
        first = [_key(p) for p in triangles[2 * index]["pos"]]
        second = [_key(p) for p in triangles[2 * index + 1]["pos"]]
        if first[0] != second[0]:
            return None, "cell %d: triangle pair does not share its first vertex" % index
        shared = [k for k in first[1:] if k in second[1:]]
        if len(shared) != 1:
            return None, "cell %d: %d shared vertices" % (index, len(shared))
        shared = shared[0]
        rest_first = [k for k in first[1:] if k != shared]
        rest_second = [k for k in second[1:] if k != shared]
        if len(rest_first) != 1 or len(rest_second) != 1:
            return None, "cell %d: degenerate triangle pair" % index
        for key, point in zip(first, triangles[2 * index]["pos"]):
            position.setdefault(key, point)
        for key, point in zip(second, triangles[2 * index + 1]["pos"]):
            position.setdefault(key, point)
        x, y = index % cells, index // cells
        wanted = {(x, y): first[0], (x + 1, y): rest_first[0],
                  (x + 1, y + 1): shared, (x, y + 1): rest_second[0]}
        for slot, key in wanted.items():
            if slot in cell and cell[slot] != key:
                return None, "cell %d: lattice point %s assigned twice differently" % (index, slot)
            cell[slot] = key
    grid = []
    for i in range(size):
        row = []
        for j in range(size):
            key = cell.get((i, j))
            if key is None or key not in position:
                return None, "lattice point (%d,%d) missing" % (i, j)
            row.append(list(position[key]))
        grid.append(row)
    return grid, None


def summarise(values):
    ok = [v for v in values if v is not None and v >= 0.0]
    return {
        "pairs": len(values),
        "nomatch": len(values) - len(ok),
        "exact": sum(1 for v in ok if v <= SEAM_EXACT),
        "lt_1": sum(1 for v in ok if v <= 1.0),
        "gt_1": sum(1 for v in ok if v > 1.0),
        "median": round(sorted(ok)[len(ok) // 2], 4) if ok else None,
        "max": round(max(ok), 3) if ok else None,
    }
def analyse_map(mode, probe, name, bsp_path, tf_root=TF_ROOT_DEFAULT, detail_disp=304):
    """Seam reading for one map. mode=engine uses the from-BSP oracle as the
    quality ceiling; mode=product uses the lattices rebuilt from the triangles
    the product really emits."""
    bsp = Bsp(bsp_path)
    usable = disp_faces(bsp)
    pairs = sewn_pairs(bsp)
    lattice = {}
    failures = []
    if mode == "engine":
        for di in usable:
            lattice[di] = grid_engine(bsp, di)
    else:
        triangles, _info = run_probe(probe, bsp_path, sorted(set(usable.values())), tf_root)
        by_face = {}
        for triangle in triangles:
            if triangle["disp"]:
                by_face.setdefault(triangle["face"], []).append(triangle)
        for di, face in usable.items():
            size = (1 << bsp.d_power(di)) + 1
            grid, why = lattice_from_triangles(by_face.get(face, []), size)
            if grid is None:
                failures.append([di, face, why])
            else:
                lattice[di] = grid
    values = []
    detail = []
    skipped_power = 0
    for first, second in pairs:
        if first not in usable or second not in usable:
            continue
        if bsp.d_power(first) != bsp.d_power(second):
            skipped_power += 1
            continue
        grid_a, grid_b = lattice.get(first), lattice.get(second)
        if grid_a is None or grid_b is None:
            values.append(None)
            continue
        best = None
        for side_a in sides_of(grid_a):
            for side_b in sides_of(grid_b):
                gap = seam_dist(side_a, side_b)
                if gap is not None and (best is None or gap < best):
                    best = gap
        values.append(best)
        if detail_disp in (first, second) and len(detail) < 12:
            detail.append([first, second, None if best is None else round(best, 4)])
    reading = summarise(values)
    reading["map"] = name
    reading["mode"] = mode
    reading["power_mismatch_pairs"] = skipped_power
    reading["unreconstructable_faces"] = len(failures)
    reading["failures"] = failures[:8]
    reading["detail304"] = detail
    return reading
def analyse_parity(probe, bsp_path, face, engine_table, tf_root=TF_ROOT_DEFAULT):
    """One named face against the engine lattice recomputed from the bsp.

    The product lattice is rebuilt from emitted triangles, so it carries no index
    convention of its own; each of its vertices is matched to the nearest engine
    lattice vertex and the resulting engine index is what gets reported. If the
    renderer still walked the grid transposed, the match would map onto the
    transpose of the engine index and the triples would disagree."""
    bsp = Bsp(bsp_path)
    disp_index = None
    for candidate in range(bsp.ndisp):
        if bsp.d_mapface(candidate) == face:
            disp_index = candidate
            break
    if disp_index is None:
        raise SystemExit("face %d carries no dispinfo in %s" % (face, bsp_path))
    engine = grid_engine(bsp, disp_index)
    size = len(engine)
    engine_point = {}
    for i in range(size):
        for j in range(size):
            engine_point[_key(engine[i][j])] = i * size + j
    triangles, _info = run_probe(probe, bsp_path, [face], tf_root)
    face_triangles = [t for t in triangles if t["face"] == face and t["disp"]]
    grid, why = lattice_from_triangles(face_triangles, size)
    report = {"face": face, "dispinfo": disp_index, "size": size,
              "corner": corner_of(bsp.polygon(face), bsp.d_start(disp_index))[0],
              "triangles": len(face_triangles), "flipped": sum(t["flip"] for t in face_triangles)}
    if grid is None:
        report["state"] = "unreconstructable"
        report["reason"] = why
        return report
    nearest = {}
    worst = 0.0
    per_vertex = []
    for i in range(size):
        for j in range(size):
            point = grid[i][j]
            best_k, best_d = None, 1.0e30
            for ii in range(size):
                for jj in range(size):
                    d = dist3(point, engine[ii][jj])
                    if d < best_d:
                        best_d, best_k = d, ii * size + jj
            nearest[_key(point)] = best_k
            worst = max(worst, best_d)
            per_vertex.append([i, j, best_k, round(best_d, 4)])
    report["state"] = "ok"
    report["max_vertex_deviation"] = round(worst, 4)
    report["vertices_within_0.01"] = sum(1 for row in per_vertex if row[3] <= SEAM_EXACT)
    report["vertex_count"] = len(per_vertex)
    report["per_vertex"] = per_vertex
    ours = []
    for triangle in face_triangles:
        ours.append([nearest.get(_key(p), -1) for p in triangle["pos"]])
    report["triples_engine_index"] = ours
    engine_table_set = set()
    if engine_table:
        with open(engine_table, "r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line.startswith("E"):
                    continue
                start = line.find("idx=(")
                if start < 0:
                    continue
                body = line[start + 5:line.find(")", start)]
                engine_table_set.add(frozenset(int(v) for v in body.replace(",", " ").split()))
    report["engine_table_triples"] = len(engine_table_set)
    ours_set = set(frozenset(t) for t in ours)
    report["triples_match_table"] = len(ours_set & engine_table_set) if engine_table_set else None
    report["triples_only_ours"] = sorted([sorted(t) for t in (ours_set - engine_table_set)]) if engine_table_set else None
    report["triples_only_table"] = sorted([sorted(t) for t in (engine_table_set - ours_set)]) if engine_table_set else None
    return report
POSES = ["up0", "up1800", "pr600", "koth0"]
KNOWN_PIXELS = [(997, 125), (998, 125)]


def load_bmp(path):
    from PIL import Image
    with Image.open(path) as image:
        return image.size[0], image.size[1], list(image.convert("RGB").getdata())


def enclosed_mask(width, height, geometry):
    """Non-geometry pixels that are not 4-connected to the frame border. The
    definition is the one the hole census already uses: a pixel with no world
    geometry behind it is 'empty', and an empty pixel the outside cannot reach is
    enclosed -- a gap in the world, or a crack the sky pass is painting over."""
    reached = bytearray(width * height)
    stack = []
    for x in range(width):
        for y in (0, height - 1):
            index = y * width + x
            if not geometry[index] and not reached[index]:
                reached[index] = 1
                stack.append(index)
    for y in range(height):
        for x in (0, width - 1):
            index = y * width + x
            if not geometry[index] and not reached[index]:
                reached[index] = 1
                stack.append(index)
    while stack:
        index = stack.pop()
        x, y = index % width, index // width
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if nx < 0 or ny < 0 or nx >= width or ny >= height:
                continue
            neighbour = ny * width + nx
            if geometry[neighbour] or reached[neighbour]:
                continue
            reached[neighbour] = 1
            stack.append(neighbour)
    return bytearray(1 if (not geometry[i] and not reached[i]) else 0 for i in range(width * height))


def pose_metrics(directory, stem):
    base_path = os.path.join(directory, stem + ".bmp")
    depth_path = os.path.join(directory, stem + "-depth.bmp")
    if not os.path.exists(base_path) or not os.path.exists(depth_path):
        return {"pose": stem, "state": "missing"}
    width, height, colour = load_bmp(base_path)
    dw, dh, depth = load_bmp(depth_path)
    if (dw, dh) != (width, height):
        return {"pose": stem, "state": "size mismatch"}
    geometry = bytearray(1 if (depth[i][0] + depth[i][1] + depth[i][2]) > DEPTH_FAR_SUM else 0
                         for i in range(width * height))
    enclosed = enclosed_mask(width, height, geometry)
    nosky_path = os.path.join(directory, stem + "-nosky.bmp")
    nosky = load_bmp(nosky_path)[2] if os.path.exists(nosky_path) else None
    metrics = {"pose": stem, "state": "ok", "size": [width, height],
               "geometry_pixels": sum(geometry),
               "empty_pixels": width * height - sum(geometry),
               "enclosed_pixels": sum(enclosed),
               "enclosed_clear": 0, "enclosed_holes": 0,
               "enclosed_clear_colour_only": 0, "sky_through_crack": 0,
               "crack_confirmed_by_nosky": 0, "crack_colours": {}}
    for index in range(width * height):
        if not enclosed[index]:
            continue
        rgb = colour[index]
        far = not geometry[index]
        if rgb == CLEAR:
            metrics["enclosed_clear_colour_only"] += 1
            if far:
                metrics["enclosed_clear"] += 1
                metrics["enclosed_holes"] += 1
            continue
        if far:
            metrics["sky_through_crack"] += 1
            key = "%d,%d,%d" % rgb
            metrics["crack_colours"][key] = metrics["crack_colours"].get(key, 0) + 1
            if nosky is not None and nosky[index] == CLEAR:
                metrics["crack_confirmed_by_nosky"] += 1
    known = []
    for x, y in KNOWN_PIXELS:
        if x >= width or y >= height:
            continue
        index = y * width + x
        known.append({"pixel": [x, y], "rgb": list(colour[index]),
                      "depth_sum": sum(depth[index]),
                      "enclosed": bool(enclosed[index]),
                      "nosky_rgb": list(nosky[index]) if nosky is not None else None})
    metrics["known"] = known
    metrics["crack_colours"] = dict(sorted(metrics["crack_colours"].items(), key=lambda kv: -kv[1])[:5])
    return metrics
def resolve_bsp(name, spec, tf_root, demo_root):
    if spec.startswith("DEMO:"):
        return os.path.join(demo_root, spec[5:])
    return os.path.join(tf_root, spec)


def seam_command(args):
    wanted = [pair for pair in MAPS if args.all or pair[0] == args.map]
    if not wanted:
        raise SystemExit("--map needs one of: " + ", ".join(name for name, _spec in MAPS))
    rows = []
    totals = {"pairs": 0, "nomatch": 0, "exact": 0, "lt_1": 0, "gt_1": 0}
    for name, spec in wanted:
        bsp_path = resolve_bsp(name, spec, args.tf_root, args.demo_root)
        if not os.path.exists(bsp_path):
            rows.append({"map": name, "state": "missing bsp", "path": bsp_path})
            continue
        reading = analyse_map(args.mode, args.probe, name, bsp_path, args.tf_root)
        rows.append(reading)
        for key in totals:
            totals[key] += reading.get(key, 0)
    if args.json:
        print(json.dumps({"mode": args.mode, "rows": rows, "totals": totals}, indent=1))
        return 0
    print("seam mode=%s (exact <= %.2f, no-pair gate %.1f)" % (args.mode, SEAM_EXACT, SEAM_ENDPOINT))
    print("%-22s %6s %8s %8s %6s %6s %9s %9s %6s"
          % ("map", "pairs", "exact", "within1", "over1", "nopair", "median", "max", "unrec"))
    for row in rows:
        if row.get("state") == "missing bsp":
            print("%-22s  MISSING %s" % (row["map"], row["path"]))
            continue
        print("%-22s %6d %8d %8d %6d %6d %9s %9s %6d"
              % (row["map"], row["pairs"], row["exact"], row["lt_1"], row["gt_1"],
                 row["nomatch"], row["median"], row["max"], row["unreconstructable_faces"]))
        if row.get("detail304"):
            print("    dispinfo 304 pairs: %s" % row["detail304"])
        for failure in row.get("failures", [])[:3]:
            print("    unreconstructable dispinfo %d face %d: %s" % (failure[0], failure[1], failure[2]))
    print("%-22s %6d %8d %8d %6d %6d" % ("TOTAL", totals["pairs"], totals["exact"],
                                          totals["lt_1"], totals["gt_1"], totals["nomatch"]))
    return 0


def parity_command(args):
    bsp_path = resolve_bsp(args.map, dict(MAPS)[args.map], args.tf_root, args.demo_root)
    report = analyse_parity(args.probe, bsp_path, args.face, args.engine_table, args.tf_root)
    if args.json:
        print(json.dumps(report, indent=1))
        return 0
    print("parity %s face %d dispinfo %d size %d corner %d"
          % (args.map, report["face"], report["dispinfo"], report["size"], report["corner"]))
    if report.get("state") != "ok":
        print("  state=%s reason=%s" % (report["state"], report.get("reason")))
        return 1
    print("  triangles=%d flipped=%d max per-vertex deviation=%.4f vertices<=%.2f: %d/%d"
          % (report["triangles"], report["flipped"], report["max_vertex_deviation"],
             SEAM_EXACT, report["vertices_within_0.01"], report["vertex_count"]))
    if report["engine_table_triples"]:
        print("  triples matching %s: %d/%d" % (os.path.basename(args.engine_table),
             report["triples_match_table"], len(report["triples_engine_index"])))
        if report["triples_only_ours"]:
            print("  only ours: %s" % report["triples_only_ours"])
        if report["triples_only_table"]:
            print("  only table: %s" % report["triples_only_table"])
    print("  triples in engine index space:")
    for index, triple in enumerate(report["triples_engine_index"]):
        print("   %02d %s" % (index, triple))
    return 0


def frames_command(args):
    rows = []
    for stem in POSES:
        reading = pose_metrics(args.dir, stem)
        row = {"after": reading}
        if args.before:
            row["before"] = pose_metrics(args.before, stem)
        rows.append(row)
    if args.json:
        print(json.dumps(rows, indent=1))
        return 0
    keys = [("enclosed_pixels", "enclosed"), ("enclosed_clear", "holes"),
            ("enclosed_clear_colour_only", "clearCol"), ("sky_through_crack", "crack"),
            ("crack_confirmed_by_nosky", "crackNoSky")]
    print("%-8s %9s %7s %8s %6s %10s%s" % ("pose", "enclosed", "holes", "clearCol", "crack", "crackNoSky",
                                          "  before: enclosed/holes/clearCol/crack"))
    for row in rows:
        reading = row["after"]
        if reading.get("state") != "ok":
            print("%-8s  %s" % (reading["pose"], reading.get("state")))
            continue
        line = "%-8s %9d" % (reading["pose"], reading["enclosed_pixels"])
        for key, _label in keys[1:]:
            line += " %7d" % reading[key]
        before = row.get("before")
        if before and before.get("state") == "ok":
            line += "   %d/%d/%d/%d" % (before["enclosed_pixels"], before["enclosed_clear"],
                                        before["enclosed_clear_colour_only"], before["sky_through_crack"])
        print(line)
        for entry in reading.get("known", []):
            print("    known %s rgb=%s depth=%d enclosed=%s nosky=%s"
                  % (entry["pixel"], entry["rgb"], entry["depth_sum"], entry["enclosed"], entry["nosky_rgb"]))
        if reading["crack_colours"]:
            print("    crack colours: %s" % reading["crack_colours"])
    return 0


def probe_counters(probe, bsp_path, tf_root):
    command = [probe, "--tf-root", tf_root, "--map", bsp_path]
    try:
        finished = subprocess.run(command, capture_output=True, text=True, timeout=900)
    except OSError as error:
        return {"state": "probe failed to start", "reason": str(error)}
    if finished.returncode != 0:
        return {"state": "probe rc=%d" % finished.returncode, "reason": finished.stderr.strip()[:200]}
    out = {"state": "ok", "triangles": 0, "faces": 0, "displaced": 0, "disp_faces": 0,
           "disp_triangles": 0, "disp_emitted": 0, "disp_fallback": 0, "flipped": 0,
           "misfaced": 0, "disp_misfaced": 0, "agree": 0, "side_faces": 0, "side_of": 0,
           "fallback_face": "", "fallback_material": ""}
    for line in finished.stdout.splitlines():
        if line.startswith("geometry status="):
            for key, pattern in (("triangles", r"triangles=(\d+)"), ("faces", r"\bfaces=(\d+)"),
                                 ("displaced", r"displaced=(\d+)")):
                found = re.search(pattern, line)
                if found:
                    out[key] = int(found.group(1))
        elif line.startswith("geometry displacement faces="):
            for key, pattern in (("disp_faces", r"faces=(\d+)"), ("disp_triangles", r"triangles=(\d+)")):
                found = re.search(pattern, line)
                if found:
                    out[key] = int(found.group(1))
        elif line.startswith("geometry displacement coverage"):
            found = re.search(r"emitted=(\d+) fallback=(\d+)", line)
            if found:
                out["disp_emitted"] = int(found.group(1))
                out["disp_fallback"] = int(found.group(2))
        elif line.startswith("geometry displacement fallback-face"):
            found = re.search(r"index=(\d+) material=(\S+)", line)
            if found:
                out["fallback_face"] = found.group(1)
                out["fallback_material"] = found.group(2)
        elif line.startswith("geometry winding flippedTriangles="):
            found = re.search(r"flippedTriangles=(\d+) of (\d+)", line)
            if found:
                out["flipped"] = int(found.group(1))
        elif line.startswith("geometry winding sideAware"):
            found = re.search(r"misfaced=(\d+) displacedMisfaced=(\d+) sideOneFaces=(\d+) of (\d+)", line)
            if found:
                out["misfaced"] = int(found.group(1))
                out["disp_misfaced"] = int(found.group(2))
                out["side_faces"] = int(found.group(3))
                out["side_of"] = int(found.group(4))
        elif line.startswith("geometry winding agree="):
            found = re.search(r"agree=(\d+)", line)
            if found:
                out["agree"] = int(found.group(1))
    return out


def counters_command(args):
    wanted = [pair for pair in MAPS if args.all or pair[0] == args.map]
    if not wanted:
        raise SystemExit("--map needs one of: " + ", ".join(name for name, _spec in MAPS))
    rows = []
    for name, spec in wanted:
        bsp_path = resolve_bsp(name, spec, args.tf_root, args.demo_root)
        if not os.path.exists(bsp_path):
            rows.append({"map": name, "state": "missing bsp", "path": bsp_path})
            continue
        reading = probe_counters(args.probe, bsp_path, args.tf_root)
        reading["map"] = name
        rows.append(reading)
    if args.json:
        print(json.dumps(rows, indent=1))
        return 0
    print("counters probe=%s" % args.probe)
    print("%-22s %9s %7s %9s %9s %8s %8s %9s %9s %9s %8s" % ("map", "triangles", "faces", "displaced",
          "dispFaces", "dispTri", "emitted", "flipped", "misfaced", "dispMisf", "agree"))
    totals = {"triangles": 0, "displaced": 0, "disp_faces": 0, "disp_triangles": 0, "flipped": 0,
              "misfaced": 0, "disp_misfaced": 0, "agree": 0}
    for row in rows:
        if row.get("state") == "missing bsp":
            print("%-22s  MISSING %s" % (row["map"], row["path"]))
            continue
        if row.get("state") != "ok":
            print("%-22s  %s %s" % (row["map"], row.get("state"), row.get("reason", "")))
            continue
        print("%-22s %9d %7d %9d %9d %8d %8d %9d %9d %9d %8d"
              % (row["map"], row["triangles"], row["faces"], row["displaced"],
                 row["disp_faces"], row["disp_triangles"], row["disp_emitted"], row["flipped"],
                 row["misfaced"], row["disp_misfaced"], row["agree"]))
        for key in totals:
            totals[key] += row[key]
    print("%-22s %9d %7s %9d %9d %8d %8s %9d %9d %9d %8d" % ("TOTAL", totals["triangles"], "-",
          totals["displaced"], totals["disp_faces"], totals["disp_triangles"], "-", totals["flipped"],
          totals["misfaced"], totals["disp_misfaced"], totals["agree"]))
    print("triangles/displaced/dispFaces/dispTri must be identical between the before and after arms;")
    print("flipped is the T11 reorient patch firing count, expected to move because the lattice moved.")
    return 0

def main(argv):
    parser = argparse.ArgumentParser(description="displacement grid parametrisation checker")
    sub = parser.add_subparsers(dest="command")
    sub.required = True
    seam = sub.add_parser("seam", help="seam continuity across adjacent displacement faces")
    seam.add_argument("--mode", default="product", choices=["product", "engine"])
    seam.add_argument("--map")
    seam.add_argument("--all", action="store_true")
    seam.add_argument("--probe", default=PROBE_DEFAULT)
    seam.add_argument("--tf-root", default=TF_ROOT_DEFAULT)
    seam.add_argument("--demo-root", default=DEMO_ROOT_DEFAULT)
    seam.add_argument("--json", action="store_true")
    seam.set_defaults(run=seam_command)
    parity = sub.add_parser("parity", help="named face against the engine lattice")
    parity.add_argument("--map", default="pl_upward_f12")
    parity.add_argument("--face", type=int, default=13233)
    parity.add_argument("--probe", default=PROBE_DEFAULT)
    parity.add_argument("--engine-table", default="D:/_shared/research/tf2-source-notes/exp/face13233_orient.txt")
    parity.add_argument("--tf-root", default=TF_ROOT_DEFAULT)
    parity.add_argument("--demo-root", default=DEMO_ROOT_DEFAULT)
    parity.add_argument("--json", action="store_true")
    parity.set_defaults(run=parity_command)
    frames = sub.add_parser("frames", help="pixel readings from captured frames")
    frames.add_argument("--dir", required=True)
    frames.add_argument("--before")
    frames.add_argument("--json", action="store_true")
    frames.set_defaults(run=frames_command)
    counters = sub.add_parser("counters", help="per-map displacement/winding counters from the probe")
    counters.add_argument("--map")
    counters.add_argument("--all", action="store_true")
    counters.add_argument("--probe", default=PROBE_DEFAULT)
    counters.add_argument("--tf-root", default=TF_ROOT_DEFAULT)
    counters.add_argument("--demo-root", default=DEMO_ROOT_DEFAULT)
    counters.add_argument("--json", action="store_true")
    counters.set_defaults(run=counters_command)
    args = parser.parse_args(argv)
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
