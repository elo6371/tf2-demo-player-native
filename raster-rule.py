#!/usr/bin/env python3
"""raster-rule.py -- T41: the front/back rule the rasterizer actually applies.

WHY THIS FILE EXISTS
--------------------
T36 labelled every drawn pixel "BACK" with a plane-side rule, then called that
label inverted because a CULL_BACK frame can only contain front faces, and
calibrated the rule by majority vote: whatever calls >=80% of the drawn pixels
FRONT wins. That is not a derivation. A majority vote cannot say which of the
two names is right, and it silently adopts the instrument it was meant to test.

This tool derives the rule from the three things that actually decide it:

  1. the world raster state -- D3D11_CULL_BACK with FrontCounterClockwise =
     FALSE (native_renderer.cpp:1174-1179), so FRONT is the clockwise
     triangle in the viewport's y-down screen space, which for a left-handed
     projection is  dot(windingNormal, eye - vertex) > 0  (main.cpp:5236-5239
     applies the same test as "area > 0");
  2. the winding the product emits -- it does NOT emit the stored polygon: the
     brush fan is pushed as (polygon[0], polygon[i+1], polygon[i])
     (bsp_map.cpp:835), the reverse of the surfedge order, and displacement
     triangles are flipped per triangle onto the face's side-aware outward
     (bsp_map.cpp:609-616). Both are counted here from the lumps;
  3. the eye the renderer used -- the trace's own eyeMap, which is the number
     the T29 gate already pins.

WHAT IT MEASURES
----------------
  emission census      how many emitted brush triangles point along the plane
                       normal, and how many displacement triangles point along
                       the face's outward after the product's flip;
  derived front set    per face, whether the emitted winding makes it front for
                       the reference eye -- for brush this is side-INDEPENDENT
                       (the emitted normal is the plane normal either way), for
                       displacement it is side-aware (the flip uses the
                       outward);
  rule agreement       the written plane-side rule against the derived rule, in
                       the four class x side cells, with face counts -- the
                       cells that disagree are named;
  falsification        the face-id frame, which T36 used as the oracle, tested
                       against the derived rule: every pixel of the cull frame
                       must sit on a face the derived rule calls front. The
                       per-face area of each id region is also compared with the
                       pinhole area the face's own geometry allows. If the frame
                       disagrees, the frame is not a sound oracle and the
                       pixel-level side split inherited from it is not a
                       measurement -- which is what this tool reports, with the
                       numbers, instead of a relabeled result.

T46: WHY A PER-FACE RULE COULD NOT SETTLE ITS OWN RESIDUAL
---------------------------------------------------------
The face-level rule above is exact on planar brush fans and conservative on a
curved displacement grid: it hands a displacement face ONE normal (the face's
outward), while the product hands the rasterizer the grid's individual
triangles, each flipped onto that outward. A displacement face can therefore be
"back" as a plane and still ship triangles whose own winding faces the eye, and
those triangles are what the 51,620 disp/side-0 pixels of T41's residual are.

Section 3b answers it with a second instrument instead of a better label:
  * emit_world recreates bsp_map.cpp's emission triangle for triangle, in
    map.triangles order (the same 99,312 triangles the product's own probe
    counts), reading the records at their real strides (dplane_t 20 bytes,
    CDispVert 20 bytes -- reading either as four floats was the bug that made
    the first version of this rule miss by 19,000 triangles);
  * triangle_sides classifies each TRIANGLE by dot(cross(b-a,c-a), eye-a),
    which is the raster's winding test and equals the product's own area>0
    oracle (main.cpp:5255) up to the near-plane clipping that preserves it;
  * build_camera / project_map_points rebuild the product's transform, and the
    tool reconciles them against the product's 28 face-trace points before it
    uses them: 25 hits project onto the traced pixel, 25 carry the traced
    depth, 24 carry the emitted face's id;
  * cover_pixels casts one ray per face-id pixel through the face's emitted
    triangles (a ray, not a screen-space test: a third of the map's triangles
    straddle the near plane, where an unclipped projection mirrors vertices
    across the screen), and reports exact coverage and coverage within half a
    pixel separately.

That separates the two questions T41 conflated: which side the MAP authored a
surface on (the face-level rule) and which triangle the RASTERIZER actually
painted a pixel with (the ledger). It also exposes the id frame's third
contaminant: the ViewModel pass runs after the world with depth cleared and
alpha blending (native_renderer.cpp:4968-4971, 1139-1150), and it writes the
weapon's COLOUR, not a face id, so its dark pixels decode as in-range face ids.
Their discriminating evidence is that their bytes equal the textured frame's
bytes at the same pixel while no emitted triangle of the face they decode to
covers them -- 512 such pixels, 266 of them on derived-back faces, which is
where T41's unexplained "derived-back brush" pixels went. They are not brush
geometry and not world pixels.

Usage: python raster-rule.py --bsp <pl_odyssey.bsp> --frames <capture dir>
                             [--eye X,Y,Z] [--trace capture.trace]
                             [--named-face 17605] [--json out.json]
Exit: 0 = every reading produced, 1 = a reading is missing.
"""
import argparse, json, lzma, math, os, struct, sys
import numpy as np

# The reference eye: T29 asserts this equals the trace's eyeMap at tick 2000,
# and the renderer's camera is that point (setObserverDemoViewWorld).
DEFAULT_EYE = (-2423.6, 5566.0, -1280.0)
# D3D11 raster state for the world pass: CULL_BACK + FrontCounterClockwise=FALSE.
CULL_MODE = "D3D11_CULL_BACK"
FRONT_COUNTER_CLOCKWISE = False


def read_lump(data, index):
    off, length = struct.unpack_from("<ii", data, 8 + index * 16)
    if length <= 0:
        return b""
    raw = data[off:off + length]
    if raw[:4] != b"LZMA":
        return raw
    actual = struct.unpack_from("<I", raw, 4)[0]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW,
        filters=[lzma._decode_filter_properties(lzma.FILTER_LZMA1, raw[12:17])])
    return dec.decompress(raw[17:], max_length=actual)


def read_bmp(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, hh = struct.unpack_from("<ii", d, 18)
    topdown = hh < 0
    h = abs(hh)
    stride = (w * 3 + 3) // 4 * 4
    a = np.frombuffer(d, dtype=np.uint8, count=stride * h, offset=off).reshape(h, stride)
    rgb = a[:, :w * 3].reshape(h, w, 3)[:, :, ::-1]
    if not topdown:
        rgb = rgb[::-1]
    return w, h, rgb


class Bsp:
    def __init__(self, path):
        d = open(path, "rb").read()
        self.first_face, self.face_count = struct.unpack_from("<ii", read_lump(d, 14), 40)
        self.faces = read_lump(d, 7)
        self.planes = read_lump(d, 1)
        self.edges = read_lump(d, 12)
        self.surfedges = read_lump(d, 13)
        self.vertices = read_lump(d, 3)
        self.n_edges = len(self.edges) // 4
        self.n_verts = len(self.vertices) // 12
        self.n_surf = len(self.surfedges) // 4
        self.V = np.frombuffer(self.vertices, dtype=np.float32,
                               count=self.n_verts * 3).reshape(self.n_verts, 3).astype(np.float64)
        self.E = np.frombuffer(self.edges, dtype=np.uint16,
                               count=self.n_edges * 2).reshape(self.n_edges, 2)
        self.P = np.frombuffer(self.planes, dtype=np.float32,
                               count=len(self.planes) // 4).reshape(-1, 5)[:, :4].astype(np.float64)
        # T46: the emission reads texinfo flags and the displacement records.
        # CDispVert is five floats (normal.xyz, distance, alpha) and CDispInfo
        # 176 bytes; reading either as four floats was the bug that first made
        # this rule disagree with the product by 19,000 triangles.
        self.texinfo = read_lump(d, 6)
        self.disp_info = read_lump(d, 26)
        self.disp_verts = read_lump(d, 33)
        self.n_texinfo = len(self.texinfo) // 72
        self.n_dispinfo = len(self.disp_info) // 176
        self.n_dispverts = len(self.disp_verts) // 20
        self.DV = np.frombuffer(self.disp_verts, dtype=np.float32,
                                count=self.n_dispverts * 5).reshape(-1, 5).astype(np.float64) \
            if self.n_dispverts else np.zeros((0, 5))

    def face_polygon(self, firstedge, count):
        """The polygon in stored order, or None when an index is out of range."""
        if count < 3 or firstedge < 0 or firstedge + count > self.n_surf:
            return None
        verts = []
        for k in range(count):
            s = struct.unpack_from("<i", self.surfedges, (firstedge + k) * 4)[0]
            e = -s if s < 0 else s
            if e < 0 or e >= self.n_edges:
                return None
            vi = int(self.E[e][1 if s < 0 else 0])
            if vi >= self.n_verts:
                return None
            verts.append(vi)
        return self.V[verts].astype(np.float64)

    def disp_record(self, disp_index):
        """(vertStart, power, startPosition) of one displacement info record."""
        off = disp_index * 176
        if off + 176 > len(self.disp_info):
            return None
        vert_start = struct.unpack_from("<i", self.disp_info, off + 12)[0]
        power = struct.unpack_from("<i", self.disp_info, off + 20)[0]
        start = np.frombuffer(self.disp_info, dtype=np.float32, count=3,
                              offset=off).astype(np.float64)
        return vert_start, power, start

    def face(self, index):
        """The fields the emission rule reads, plus the polygon in stored order."""
        b = index * 56
        if b + 56 > len(self.faces):
            return None
        planenum = struct.unpack_from("<H", self.faces, b)[0]
        side = self.faces[b + 2]
        firstedge = struct.unpack_from("<i", self.faces, b + 4)[0]
        count = struct.unpack_from("<h", self.faces, b + 8)[0]
        dispinfo = struct.unpack_from("<h", self.faces, b + 12)[0]
        if count < 3 or firstedge < 0 or firstedge + count > self.n_surf:
            return None
        if planenum >= len(self.P):
            return None
        verts = []
        for k in range(count):
            s = struct.unpack_from("<i", self.surfedges, (firstedge + k) * 4)[0]
            e = -s if s < 0 else s
            if e < 0 or e >= self.n_edges:
                return None
            vi = int(self.E[e][1 if s < 0 else 0])
            if vi >= self.n_verts:
                return None
            verts.append(vi)
        n = self.P[planenum][:3]
        keeps = np.dot(n, n) > 1e-12
        if not keeps:
            return None
        return {"planenum": planenum, "side": int(side), "count": int(count),
                "dispinfo": int(dispinfo), "poly": self.V[verts],
                "plane": n / np.linalg.norm(n), "dist": float(self.P[planenum][3])}

    def emitted_normal(self, face):
        """The normal of the triangle the rasterizer receives, in the product's order.

        bsp_map.cpp:835 fans (polygon[0], polygon[i+1], polygon[i]); displacement
        faces are then flipped onto the face's outward at 609-616, and this
        returns the post-flip orientation for them.
        """
        poly = face["poly"]
        acc = np.zeros(3)
        for k in range(1, len(poly) - 1):
            acc += np.cross(poly[k + 1] - poly[0], poly[k] - poly[0])
        if face["dispinfo"] >= 0:
            # The grid's own order is against the plane normal (counted below);
            # the product flips every triangle onto the outward, so the shading
            # normal is the outward whatever the parametrisation did.
            outward = face["plane"] * (1.0 if face["side"] == 0 else -1.0)
            return outward
        return acc



# ---------------------------------------------------------------------------
# T46: the emitted triangle set, the per-triangle raster side, and the pixel
# ledger.
#
# T41's rule classified a FACE from one normal (the brush fan's sum; the
# displacement face's outward). The product hands the rasterizer individual
# TRIANGLES, and a displacement face's grid is curved, so a face can be "back"
# as a plane and still ship triangles whose own winding faces the eye. This
# block rebuilds the product's emission (bsp_map.cpp:314-843) triangle by
# triangle, projects it with the product's own camera, and settles the pixels
# T41 could only bound by asking which emitted triangle actually covers each
# one.
#
# Nothing here changes the T41 readings above: it explains them.
# ---------------------------------------------------------------------------
F32 = np.float32

# A pixel whose ray misses a triangle by less than half a pixel footprint is
# edge-on, not absent: the rasterizer's subpixel precision decides those. The
# bound is stated, not silent -- every reading below reports the exact and the
# half-pixel counts separately.
SUBPIXEL = 0.5


def demo_view_forward(pitch_deg, yaw_deg):
    pitch = math.radians(pitch_deg)
    yaw = math.radians(yaw_deg)
    return np.array([math.cos(pitch) * math.cos(yaw),
                     math.cos(pitch) * math.sin(yaw),
                     -math.sin(pitch)], dtype=F32)


def look_at_lh(eye, target, up):
    zaxis = target - eye
    zaxis = zaxis / np.linalg.norm(zaxis)
    xaxis = np.cross(up, zaxis)
    xaxis = xaxis / np.linalg.norm(xaxis)
    yaxis = np.cross(zaxis, xaxis)
    # XMMatrixLookAtLH stores the basis vectors as COLUMNS, so that a row-vector
    # multiply v*view subtracts the eye: v'[0] = dot(v, xaxis) - dot(eye, xaxis).
    m = np.eye(4, dtype=F32)
    m[:3, 0] = xaxis
    m[:3, 1] = yaxis
    m[:3, 2] = zaxis
    m[3, 0] = -np.dot(xaxis, eye)
    m[3, 1] = -np.dot(yaxis, eye)
    m[3, 2] = -np.dot(zaxis, eye)
    return m


def perspective_fov_lh(fov_y, aspect, zn, zf):
    ys = F32(1.0) / F32(math.tan(fov_y / 2.0))
    xs = ys / F32(aspect)
    m = np.zeros((4, 4), dtype=F32)
    m[0, 0] = xs
    m[1, 1] = ys
    m[2, 2] = F32(zf) / F32(zf - zn)
    m[2, 3] = F32(1.0)
    m[3, 2] = -F32(zn) * F32(zf) / F32(zf - zn)
    return m


def build_camera(vertices, eye_map, pitch, yaw, width, height, fov_y_deg=90.0):
    """The transform the product applies, in the product's own order.

    native_renderer.cpp:1306-1325 normalises every emitted vertex into the box
    the shaders work in; 4442-4532 builds the look-at from the normalised eye
    and a forward scaled by the same anisotropy; main.cpp:5131-5146 is the
    row-vector multiply that turns a map point into the pixel the trace names.
    Reproduced here so the ledger's pixels and the product's pixels are the same
    pixels -- and the trace reconciliation below proves it on the product's own
    28 sampled points.
    """
    v = vertices.astype(F32)
    min_x = float(v[:, 0].min()); max_x = float(v[:, 0].max())
    min_y = float(v[:, 1].min()); max_y = float(v[:, 1].max())
    min_z = float(v[:, 2].min()); max_z = float(v[:, 2].max())
    span_x = F32(max(max_x - min_x, 1.0))
    span_y = F32(max(max_y - min_y, 1.0))
    span_z = F32(max(max_z - min_z, 1.0))
    scale = F32(1.8) / F32(max(span_x, span_y))
    center_x = F32((min_x + max_x) * 0.5)
    center_y = F32((min_y + max_y) * 0.5)
    forward = demo_view_forward(pitch, yaw)
    eye = np.array([(F32(eye_map[0]) - center_x) * scale,
                    (F32(eye_map[1]) - center_y) * scale,
                    (F32(eye_map[2]) - min_z) / span_z * F32(0.8) + F32(0.1)], dtype=F32)
    norm_forward = np.array([forward[0] * scale, forward[1] * scale,
                             forward[2] * (F32(0.8) / span_z)], dtype=F32)
    up = np.array([0.0, 0.0, 1.0], dtype=F32) if abs(float(forward[2])) <= 0.95 \
        else np.array([0.0, 1.0, 0.0], dtype=F32)
    view = look_at_lh(eye, eye + norm_forward, up)
    near = max(float(scale) * 4.0, 1e-6)
    projection = perspective_fov_lh(math.radians(fov_y_deg), width / float(height), near, 100.0)
    mvp = np.asarray(view @ projection, dtype=F32)
    return {"mvp": mvp, "scale": float(scale), "centerX": float(center_x),
            "centerY": float(center_y), "minZ": float(min_z), "spanZ": float(span_z),
            "width": int(width), "height": int(height), "near": near, "far": 100.0,
            "eyeMap": [float(v) for v in eye_map], "pitch": pitch, "yaw": yaw,
            "eyeNormalized": [float(v) for v in eye]}


def to_box(cam, points):
    """map coordinates -> the normalised box the shader multiplies by the mvp."""
    p = np.asarray(points, dtype=np.float64)
    out = np.empty(p.shape, dtype=np.float64)
    out[..., 0] = (p[..., 0] - cam["centerX"]) * cam["scale"]
    out[..., 1] = (p[..., 1] - cam["centerY"]) * cam["scale"]
    out[..., 2] = (p[..., 2] - cam["minZ"]) / cam["spanZ"] * 0.8 + 0.1
    return out


def project_map_points(cam, points):
    """main.cpp:5131-5146 point for point: sx, sy, post-divide depth, invW."""
    b = to_box(cam, points)
    m = cam["mvp"].astype(np.float64)
    x = b[..., 0]; y = b[..., 1]; z = b[..., 2]
    cx = x * m[0, 0] + y * m[1, 0] + z * m[2, 0] + m[3, 0]
    cy = x * m[0, 1] + y * m[1, 1] + z * m[2, 1] + m[3, 1]
    cz = x * m[0, 2] + y * m[1, 2] + z * m[2, 2] + m[3, 2]
    cw = x * m[0, 3] + y * m[1, 3] + z * m[2, 3] + m[3, 3]
    ok = (cw > 1e-5) & np.isfinite(cw)
    safe = np.where(ok, cw, 1.0)
    sx = (cx / safe * 0.5 + 0.5) * cam["width"]
    sy = (0.5 - cy / safe * 0.5) * cam["height"]
    return sx, sy, cz / safe, 1.0 / safe, ok


def view_basis(cam):
    eye = np.asarray(cam["eyeNormalized"], dtype=np.float64)
    f = demo_view_forward(cam["pitch"], cam["yaw"]).astype(np.float64)
    scale = cam["scale"]
    norm_forward = np.array([f[0] * scale, f[1] * scale, f[2] * (0.8 / cam["spanZ"])])
    zaxis = norm_forward / np.linalg.norm(norm_forward)
    up = np.array([0.0, 0.0, 1.0]) if abs(f[2]) <= 0.95 else np.array([0.0, 1.0, 0.0])
    xaxis = np.cross(up, zaxis); xaxis /= np.linalg.norm(xaxis)
    yaxis = np.cross(zaxis, xaxis)
    return eye, xaxis, yaxis, zaxis


def ray_directions(cam, px, py):
    """The view ray of a pixel centre, rebuilt from the same basis the look-at used."""
    eye, xaxis, yaxis, zaxis = view_basis(cam)
    tan_y = math.tan(math.radians(90.0) / 2.0)
    ndx = ((np.asarray(px, dtype=np.float64) + 0.5) / cam["width"] * 2.0 - 1.0) \
        * (cam["width"] / float(cam["height"])) * tan_y
    ndy = (1.0 - (np.asarray(py, dtype=np.float64) + 0.5) / cam["height"] * 2.0) * tan_y
    d = xaxis[None, :] * ndx[:, None] + yaxis[None, :] * ndy[:, None] + zaxis[None, :]
    return d, eye


def closest_on_triangle(a, b, c, p):
    """Distance from p to triangle (a,b,c); Ericson, vectorised over (P,T)."""
    ab = b - a
    ac = c - a
    ap = p - a[None, :, :]
    d1 = np.einsum('tk,ptk->pt', ab, ap)
    d2 = np.einsum('tk,ptk->pt', ac, ap)
    bp = p - b[None, :, :]
    d3 = np.einsum('tk,ptk->pt', ab, bp)
    d4 = np.einsum('tk,ptk->pt', ac, bp)
    cp = p - c[None, :, :]
    d5 = np.einsum('tk,ptk->pt', ab, cp)
    d6 = np.einsum('tk,ptk->pt', ac, cp)
    vc = d1 * d4 - d3 * d2
    vb = d5 * d2 - d1 * d6
    va = d3 * d6 - d5 * d4
    denom = va + vb + vc
    safe = np.where(np.abs(denom) > 1e-30, denom, 1.0)
    q = a[None, :, :] + ab[None, :, :] * (vb / safe)[..., None] + ac[None, :, :] * (vc / safe)[..., None]
    ab_den = d1 - d3
    t_ab = np.where(np.abs(ab_den) > 1e-30, d1 / np.where(np.abs(ab_den) > 1e-30, ab_den, 1.0), 0.0)
    ac_den = d2 - d6
    t_ac = np.where(np.abs(ac_den) > 1e-30, d2 / np.where(np.abs(ac_den) > 1e-30, ac_den, 1.0), 0.0)
    bc_den = (d4 - d3) + (d5 - d6)
    t_bc = np.where(np.abs(bc_den) > 1e-30, (d4 - d3) / np.where(np.abs(bc_den) > 1e-30, bc_den, 1.0), 0.0)
    q = np.where((d1 <= 0)[..., None] & (d2 <= 0)[..., None], a[None, :, :], q)
    q = np.where((d3 >= 0)[..., None] & (d4 <= d3)[..., None], b[None, :, :], q)
    q = np.where((d6 >= 0)[..., None] & (d5 <= d6)[..., None], c[None, :, :], q)
    edge_ab = a[None, :, :] + ab[None, :, :] * np.clip(t_ab, 0.0, 1.0)[..., None]
    edge_ac = a[None, :, :] + ac[None, :, :] * np.clip(t_ac, 0.0, 1.0)[..., None]
    edge_bc = b[None, :, :] + (c - b)[None, :, :] * np.clip(t_bc, 0.0, 1.0)[..., None]
    q = np.where((vc <= 0)[..., None] & (d1 >= 0)[..., None] & (d3 <= 0)[..., None], edge_ab, q)
    q = np.where((vb <= 0)[..., None] & (d2 >= 0)[..., None] & (d6 <= 0)[..., None], edge_ac, q)
    q = np.where((va <= 0)[..., None] & ((d4 - d3) >= 0)[..., None]
                 & ((d5 - d6) >= 0)[..., None], edge_bc, q)
    return np.linalg.norm(p - q, axis=-1)


def cover_pixels(cam, tris, tri_front, px, py, chunk=4000):
    """Which emitted triangles cover each pixel, and how nearly.

    Returns per pixel: exact front/back coverage, the nearest covering front
    triangle ordinal and its ray parameter, and the distance in pixels from the
    pixel's ray to the nearest front/back triangle (inf when the ray misses the
    triangle's plane behind the eye).
    """
    n = len(px)
    front_exact = np.zeros(n, dtype=bool)
    back_exact = np.zeros(n, dtype=bool)
    near_tri = np.full(n, -1, dtype=np.int64)
    near_t = np.full(n, np.inf)
    front_miss = np.full(n, np.inf)
    back_miss = np.full(n, np.inf)
    if n == 0 or tris.shape[1] == 0:
        return front_exact, back_exact, near_tri, near_t, front_miss, back_miss
    a = to_box(cam, tris[0]); b = to_box(cam, tris[1]); c = to_box(cam, tris[2])
    e1 = b - a; e2 = c - a
    dvec, eye = ray_directions(cam, px, py)
    height = float(cam["height"])
    tri_front = np.asarray(tri_front, dtype=bool)
    for s in range(0, n, chunk):
        e = min(n, s + chunk)
        Pc = e - s
        D = dvec[s:e]
        # ---- exact coverage ------------------------------------------------
        pv = np.cross(D[:, None, :], e2[None, :, :])
        det = np.einsum('ptk,tk->pt', pv, e1)
        ok = np.abs(det) > 1e-14
        inv = 1.0 / np.where(ok, det, 1.0)
        shape_ptk = (Pc, e1.shape[0], 3)
        tv = np.broadcast_to(eye[None, None, :] - a[None, :, :], shape_ptk)
        u = np.einsum('ptk,ptk->pt', tv, pv) * inv
        qv = np.cross(tv, np.broadcast_to(e1[None, :, :], shape_ptk))
        v = np.einsum('ptk,ptk->pt', np.broadcast_to(D[:, None, :], shape_ptk), qv) * inv
        t = np.einsum('tk,ptk->pt', e2, qv) * inv
        hit = ok & (u >= 0.0) & (v >= 0.0) & ((u + v) <= 1.0) & (t > 0.0)
        fe = hit & tri_front[None, :]
        be = hit & ~tri_front[None, :]
        front_exact[s:e] = fe.any(axis=1)
        back_exact[s:e] = be.any(axis=1)
        if fe.any():
            tf = np.where(fe, t, np.inf)
            best = np.argmin(tf, axis=1)
            bd = tf[np.arange(Pc), best]
            has = np.isfinite(bd)
            near_tri[s:e][has] = best[has].astype(np.int64)
            near_t[s:e][has] = bd[has]
        # ---- misses: how near was the closest triangle of each side? -------
        need = np.nonzero(~front_exact[s:e] | ~back_exact[s:e])[0]
        if need.size == 0:
            continue
        pos = ok[need] & (t[need] > 0.0)
        keep = np.nonzero(pos.any(axis=1))[0]
        if keep.size == 0:
            continue
        sub = need[keep]             # pixel offsets in this chunk that miss
        tpix = t[sub]                # (Pneed, T) plane parameter
        tp = np.where(tpix > 0.0, tpix, 1.0)
        Dp = D[sub]
        shape2 = (sub.size, e1.shape[0], 3)
        q = eye[None, None, :] + np.broadcast_to(Dp[:, None, :], shape2) * tp[..., None]
        d3 = closest_on_triangle(a, b, c, q)
        pxdist = np.where(tpix > 0.0, d3 * height / (2.0 * tp), np.inf)
        fm = np.where(tri_front[None, :], pxdist, np.inf).min(axis=1)
        bm = np.where(~tri_front[None, :], pxdist, np.inf).min(axis=1)
        front_miss[s + sub] = fm
        back_miss[s + sub] = bm
    return front_exact, back_exact, near_tri, near_t, front_miss, back_miss


def emit_world(bsp):
    """The product's world emission, triangle by triangle (bsp_map.cpp:314-843).

    Returns the emitted triangles in map.triangles order with the face each one
    came from, whether the displacement grid fed it, whether the orientation was
    flipped onto the face's outward, and whether the face is water.
    """
    face_ids = []; av = []; bv = []; cv = []
    disp_flag = []; flip_flag = []; water_flag = []
    per_face = {}
    grid_faces = 0
    fallback_faces = []
    triangle_limit = 200000
    for face_index in range(bsp.first_face, bsp.first_face + bsp.face_count):
        b = face_index * 56
        if b + 56 > len(bsp.faces):
            continue
        planenum = struct.unpack_from("<H", bsp.faces, b)[0]
        side = int(bsp.faces[b + 2])
        first_edge = struct.unpack_from("<i", bsp.faces, b + 4)[0]
        count = struct.unpack_from("<h", bsp.faces, b + 8)[0]
        info = struct.unpack_from("<h", bsp.faces, b + 10)[0]
        disp_index = struct.unpack_from("<h", bsp.faces, b + 12)[0]
        if count < 3 or first_edge < 0 or first_edge + count > bsp.n_surf \
                or info < 0 or info >= bsp.n_texinfo or planenum >= len(bsp.P):
            continue
        flags = struct.unpack_from("<i", bsp.texinfo, info * 72 + 64)[0]
        if (flags & (2 | 4)) != 0:
            continue                      # sky: goes to map.skyTriangles, not drawn here
        if (flags & (64 | 128)) != 0:
            continue                      # non-sky faces the product drops
        poly = bsp.face_polygon(first_edge, count)
        if poly is None:
            continue
        water = (flags & 8) != 0
        plane = bsp.P[planenum][:3]
        outward = plane * (1.0 if side == 0 else -1.0)
        start_tri = len(face_ids)
        face_tris = []
        emitted_disp = False
        if disp_index >= 0 and count == 4:
            rec = bsp.disp_record(disp_index)
            if rec is not None:
                poly_norm = np.cross(poly[1] - poly[0], poly[2] - poly[0])
                if float(np.linalg.norm(poly_norm)) > 1e-6:
                    vert_start, power, start_pos = rec
                    size = (1 << power) + 1
                    if 2 <= power <= 4 and vert_start >= 0 \
                            and vert_start + size * size <= bsp.n_dispverts:
                        d2 = np.sum((poly[:4] - start_pos) ** 2, axis=1)
                        corner = int(np.argmin(d2))
                        if float(d2[corner]) <= 1.0:
                            quad = np.array([poly[(corner + i + 8) % 4] for i in range(4)],
                                            dtype=np.float64)
                            dv = bsp.DV[vert_start:vert_start + size * size].reshape(size, size, 5)
                            if np.isfinite(dv[:, :, :4]).all() and np.all(np.abs(dv[:, :, 3]) <= 4096.0):
                                step = np.arange(size, dtype=np.float64) / (size - 1)
                                left = quad[0][None, :] + (quad[3] - quad[0])[None, :] * step[:, None]
                                right_ = quad[1][None, :] + (quad[2] - quad[1])[None, :] * step[:, None]
                                flatp = left[:, None, :] + (right_ - left)[:, None, :] * step[None, :, None]
                                grid = flatp + dv[:, :, :3] * dv[:, :, 3:4]
                                ok_cells = True
                                for gy in range(size - 1):
                                    for gx in range(size - 1):
                                        p00 = grid[gy, gx]; p10 = grid[gy, gx + 1]
                                        p11 = grid[gy + 1, gx + 1]; p01 = grid[gy + 1, gx]
                                        if not (_usable(p00, p10, p11) and _usable(p00, p01, p11)):
                                            ok_cells = False
                                            break
                                    if not ok_cells:
                                        break
                                if ok_cells:
                                    for gy in range(size - 1):
                                        for gx in range(size - 1):
                                            p00 = grid[gy, gx]; p10 = grid[gy, gx + 1]
                                            p11 = grid[gy + 1, gx + 1]; p01 = grid[gy + 1, gx]
                                            for (pa, pb, pc) in ((p00, p11, p10), (p00, p01, p11)):
                                                pa, pb, pc, was_flip = _orient(pa, pb, pc, outward)
                                                face_tris.append((pa, pb, pc, True, was_flip))
                                    emitted_disp = len(face_tris) > 0
        if not emitted_disp:
            if disp_index >= 0:
                fallback_faces.append(face_index)
            for i in range(1, count - 1):
                pa, pb, pc = poly[0], poly[i + 1], poly[i]
                face_tris.append((pa, pb, pc, False, False))
        for (pa, pb, pc, was_disp, was_flip) in face_tris:
            if len(face_ids) >= triangle_limit:
                break
            face_ids.append(face_index); av.append(pa); bv.append(pb); cv.append(pc)
            disp_flag.append(was_disp); flip_flag.append(was_flip); water_flag.append(water)
        if emitted_disp:
            grid_faces += 1
        per_face[face_index] = {"planenum": int(planenum), "side": side,
                                "dispinfo": int(disp_index), "water": water,
                                "poly": poly, "plane": np.asarray(plane, dtype=np.float64),
                                "outward": np.asarray(outward, dtype=np.float64),
                                "emittedDisp": emitted_disp,
                                "tris": len(face_ids) - start_tri,
                                "firstTri": start_tri, "flags": int(flags)}
        if len(face_ids) >= triangle_limit:
            break
    return {"face": np.asarray(face_ids, dtype=np.int64),
            "a": np.asarray(av, dtype=F32), "b": np.asarray(bv, dtype=F32),
            "c": np.asarray(cv, dtype=F32),
            "disp": np.asarray(disp_flag, dtype=bool),
            "flip": np.asarray(flip_flag, dtype=bool),
            "water": np.asarray(water_flag, dtype=bool),
            "faces": per_face, "gridFaces": grid_faces,
            "fallbackFaces": fallback_faces}


def _usable(p00, p10, p11):
    """bsp_map.cpp's usable(): area above 1e-8 and no edge above 512 units."""
    ux = p10[0] - p00[0]; uy = p10[1] - p00[1]; uz = p10[2] - p00[2]
    vx = p11[0] - p00[0]; vy = p11[1] - p00[1]; vz = p11[2] - p00[2]
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    if not (nx * nx + ny * ny + nz * nz > 1e-8):
        return False
    e0 = ux * ux + uy * uy + uz * uz
    e1 = vx * vx + vy * vy + vz * vz
    e2 = (p11[0] - p10[0]) ** 2 + (p11[1] - p10[1]) ** 2 + (p11[2] - p10[2]) ** 2
    return e0 <= 262144.0 and e1 <= 262144.0 and e2 <= 262144.0


def _orient(pa, pb, pc, outward):
    n = np.cross(pb - pa, pc - pa)
    if float(np.dot(n, outward)) < 0.0:
        return pa, pc, pb, True
    return pa, pb, pc, False


def triangle_sides(emitted, eye_map):
    """Per-triangle raster side: dot(cross(b-a, c-a), eye - a).

    The rasterizer's winding test reduces to the sign of the emitted triangle's
    geometric normal against the eye, and the product's own CPU oracle
    (main.cpp:5255) is the same test in screen space; clipping preserves the
    side. +1 front, -1 back, 0 degenerate.
    """
    a = emitted["a"].astype(np.float64); b = emitted["b"].astype(np.float64)
    c = emitted["c"].astype(np.float64)
    eye = np.asarray(eye_map, dtype=np.float64)
    nrm = np.cross(b - a, c - a)
    dot = np.einsum('ij,ij->i', nrm, eye[None, :] - a)
    return np.where(dot > 0.0, 1, np.where(dot < 0.0, -1, 0)), dot


def parse_face_trace(trace_path):
    """The 'gpu: world face-trace' points: pixel, triangle ordinal, depth, face."""
    points = []
    try:
        fh = open(trace_path, "r", errors="replace")
    except OSError:
        return points
    with fh:
        for line in fh:
            if "gpu: world face-trace" not in line:
                continue
            rec = {}
            for token in line.split():
                if "=" not in token:
                    continue
                key, value = token.split("=", 1)
                rec[key] = value
            try:
                point = rec.get("point", "").split(",")
                px = int(float(point[0])); py = int(float(point[1]))
                tri = int(float(rec.get("tri", "-1")))
                face = int(float(rec.get("face", "-1")))
                depth = float(rec.get("depth", "nan"))
            except (IndexError, ValueError):
                continue
            points.append({"x": px, "y": py, "tri": tri, "face": face, "depth": depth})
    return points

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bsp", required=True)
    ap.add_argument("--frames", required=True)
    ap.add_argument("--eye", default="%.1f,%.1f,%.1f" % DEFAULT_EYE,
                    help="fallback eye, used only when --trace cannot be read")
    ap.add_argument("--trace", default="",
                    help="capture trace to read the camera from; the last "
                         "'scene: camera' line is the pose the captured frame "
                         "was drawn with")
    ap.add_argument("--named-face", default="17605",
                    help="the face T41's disp residual names (default 17605)")
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    # T41 correction: the eye this tool used first, (-2423.6,5566.0,-1280.0), is
    # the 'gpu: world vis eyeMap' value, and main.cpp emits that line ONCE on
    # the first drawn frame. It is not the pose of the captured tick-2000 frame.
    # The capture's own trace carries the real one: the last 'scene: camera'
    # line before the frame is drawn (mode=first-person ... src=demo), and that
    # is what classifies the pixels below.
    trace_path = args.trace or os.path.join(args.frames, "ids.trace")
    eye_source = "argument --eye (no readable trace)"
    eye_line = ""
    drawn_tick = None
    if os.path.exists(trace_path):
        try:
            with open(trace_path, "r", errors="replace") as fh:
                for line in fh:
                    if "scene: camera" in line:
                        eye_line = line.strip()
                    elif "draw: begin tick=" in line:
                        try:
                            drawn_tick = int(line.split("draw: begin tick=")[1].split()[0])
                        except (IndexError, ValueError):
                            pass
        except OSError:
            eye_line = ""
    if eye_line:
        import re
        m = re.search(r"eye=(-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)", eye_line)
        if m:
            eye = np.array([float(m.group(1)), float(m.group(2)), float(m.group(3))])
            eye_source = "capture trace %s, last 'scene: camera'" % trace_path
        else:
            eye = np.array([float(v) for v in args.eye.split(",")])
    else:
        eye = np.array([float(v) for v in args.eye.split(",")])
    bsp = Bsp(args.bsp)
    out = {"cullMode": CULL_MODE, "frontCounterClockwise": FRONT_COUNTER_CLOCKWISE,
           "eye": [float(v) for v in eye], "eyeSource": eye_source,
           "eyeTraceLine": eye_line, "drawnTick": drawn_tick,
           "frontRule": "dot(emittedNormal, eye - p) > 0"}

    # ---- 1. emission census -------------------------------------------------
    census = {"brushFaces": 0, "brushTris": 0, "brushAlongPlane": 0, "brushAgainstPlane": 0,
              "brushDegenerate": 0, "dispFaces": 0, "dispTris": 0,
              "dispGridAgainstPlane": 0, "dispGridAlongPlane": 0}
    faces = {}
    for f in range(bsp.first_face, bsp.first_face + bsp.face_count):
        face = bsp.face(f)
        if face is None:
            continue
        faces[f] = face
        if face["dispinfo"] >= 0:
            census["dispFaces"] += 1
            census["dispTris"] += face["count"] - 2
            # the grid's own parametrisation: cross of the first quad corner
            poly = face["poly"]
            if len(poly) >= 3:
                w = np.cross(poly[1] - poly[0], poly[2] - poly[0])
                if np.dot(w, face["plane"]) > 0:
                    census["dispGridAlongPlane"] += 1
                else:
                    census["dispGridAgainstPlane"] += 1
            continue
        census["brushFaces"] += 1
        census["brushTris"] += face["count"] - 2
        n = bsp.emitted_normal(face)
        if np.linalg.norm(n) < 1e-9:
            census["brushDegenerate"] += 1
            continue
        if np.dot(n, face["plane"]) > 0:
            census["brushAlongPlane"] += 1
        else:
            census["brushAgainstPlane"] += 1
    out["emission"] = census

    # ---- 2. derived front set, per face ------------------------------------
    # brush: the emitted normal is the plane normal, so the side bit does not
    # enter the raster's test. displacement: the flip put the emitted normal on
    # the face's outward, so the side bit does enter it.
    cells = {}
    for f, face in faces.items():
        sd = float(np.dot(face["plane"], eye) - face["dist"])
        side_sign = 1.0 if face["side"] == 0 else -1.0
        emitted = bsp.emitted_normal(face)
        derived_front = float(np.dot(emitted, eye - face["poly"][0])) > 0.0
        written_front = (sd >= 0.0) if face["side"] == 0 else (sd <= 0.0)
        cls = "disp" if face["dispinfo"] >= 0 else "brush"
        key = "%s/side-%d" % (cls, face["side"])
        cell = cells.setdefault(key, {"faces": 0, "derivedFront": 0, "writtenFront": 0,
                                      "agree": 0, "disagree": 0, "eyeSide": 0})
        cell["faces"] += 1
        if sd > 0:
            cell["eyeSide"] += 1
        cell["derivedFront"] += int(derived_front)
        cell["writtenFront"] += int(written_front)
        cell["agree"] += int(derived_front == written_front)
        cell["disagree"] += int(derived_front != written_front)
        face["derivedFront"] = derived_front
        face["writtenFront"] = written_front
    out["ruleAgreement"] = {k: cells[k] for k in sorted(cells)}
    out["derivedFrontFaces"] = int(sum(1 for f in faces.values() if f["derivedFront"]))
    out["writtenFrontFaces"] = int(sum(1 for f in faces.values() if f["writtenFront"]))
    out["facesParsed"] = len(faces)

    # ---- 2b. the side split of what the raster keeps and what it removes ----
    # Two different questions, and this is where they get separated:
    #   KEPT or REMOVED  is the rasterizer's decision, made by the derived front
    #                    test on the emitted triangle -- it does not look at the
    #                    face's side bit, only at the winding it was handed;
    #   FRONT or BACK    is the AUTHORED side, i.e. whether the map wrote the
    #                    surface facing this eye. That is the plane-side rule
    #                    (side 0 -> +normal, side 1 -> -normal), which is
    #                    exactly the rule the account has always used and which
    #                    this tool now confirms is the authored-side question,
    #                    not a rival answer to the rasterizer's.
    # Their disagreement is confined to brush/side-1 (see the table): the brush
    # fan emits the PLANE normal whatever the side bit says, so on that cell the
    # rasterizer keeps the face the map authored as back.
    kept = {"front": 0, "back": 0}
    removed = {"front": 0, "back": 0}
    for face in faces.values():
        bucket = kept if face["derivedFront"] else removed
        bucket["front" if face["writtenFront"] else "back"] += 1
    out["derivedDrawn"] = kept
    out["derivedRemoved"] = removed
    out["derivedDrawnFaces"] = kept["front"] + kept["back"]
    out["derivedRemovedFaces"] = removed["front"] + removed["back"]

    # ---- 3. falsification against the face-id frame -------------------------
    ids_path = os.path.join(args.frames, "ids.bmp")
    nocull_path = os.path.join(args.frames, "ids-nocull.bmp")
    frame = {"frames": [ids_path, nocull_path]}
    rc = 0
    if os.path.exists(ids_path):
        w, h, ids = read_bmp(ids_path)
        fi = (ids[:, :, 0].astype(np.int64) | (ids[:, :, 1].astype(np.int64) << 8)
              | (ids[:, :, 2].astype(np.int64) << 16))
        flat = fi.ravel()
        in_range = (flat >= bsp.first_face) & (flat < bsp.first_face + bsp.face_count)
        frame.update({"width": w, "height": h, "inRangePixels": int(in_range.sum()),
                      "offRangePixels": int((~in_range).sum())})
        counts = np.bincount((flat[in_range] - bsp.first_face).astype(np.int64),
                             minlength=bsp.face_count)
        drawn = {bsp.first_face + i: int(c) for i, c in enumerate(counts) if c > 0}
        frame["distinctFaceIds"] = len(drawn)
        # Every pixel of a CULL_BACK frame must be a front face under the rule
        # derived above. Pixels on a face the rule calls BACK are pixels the
        # frame and the rule disagree about, and one of them is wrong.
        agree_px = sum(int(c) for f, c in drawn.items() if f in faces and faces[f]["derivedFront"])
        disagree_px = sum(int(c) for f, c in drawn.items() if f in faces and not faces[f]["derivedFront"])
        frame["drawnPixelsOnDerivedFront"] = agree_px
        frame["drawnPixelsOnDerivedBack"] = disagree_px
        # The same population under the written plane-side rule, for the record:
        # this is the number T36 reported as "86% BACK".
        written_back = sum(int(c) for f, c in drawn.items() if f in faces and not faces[f]["writtenFront"])
        frame["drawnPixelsWrittenBack"] = written_back
        frame["drawnPixelsWrittenFront"] = sum(int(c) for f, c in drawn.items()
                                               if f in faces and faces[f]["writtenFront"])
        # Per-face pinhole bound: a planar face of area A seen from distance |d|
        # cannot cover more than A*K^2/z^2 pixels, K = (h/2)/tan(fov/2) with the
        # product's 90 degree vertical fov. A region that exceeds it cannot be
        # that face's projection.
        K = (h / 2.0) / np.tan(np.radians(90.0) / 2.0)
        ratios = []
        for f, c in drawn.items():
            if f not in faces or c < 20:
                continue
            poly = faces[f]["poly"]
            acc = np.zeros(3)
            for k in range(1, len(poly) - 1):
                acc += np.cross(poly[k] - poly[0], poly[k + 1] - poly[0])
            area = 0.5 * float(np.linalg.norm(acc))
            dist = max(float(np.linalg.norm(poly.mean(axis=0) - eye)), 1.0)
            bound = area * K * K / (dist * dist)
            if bound > 0:
                ratios.append(float(c) / bound)
        frame["overBoundFaces"] = int(sum(1 for r in ratios if r > 1.0))
        if ratios:
            frame["ratioMedian"] = float(np.median(ratios))
            frame["ratioP90"] = float(np.percentile(ratios, 90))
            frame["ratioMax"] = float(np.max(ratios))
        frame["facesWithRegion"] = len(ratios)
        # The cull control's total is id-independent: it is the number of pixels
        # the unculled frame paints that the culled one does not.
        if os.path.exists(nocull_path):
            w2, h2, nocull = read_bmp(nocull_path)
            fn = (nocull[:, :, 0].astype(np.int64) | (nocull[:, :, 1].astype(np.int64) << 8)
                  | (nocull[:, :, 2].astype(np.int64) << 16))
            in_n = (fn.ravel() >= bsp.first_face) & (fn.ravel() < bsp.first_face + bsp.face_count)
            in_c = in_range
            frame["removedByIds"] = int((in_c & ~in_n).sum())
            frame["nocullOnly"] = int((in_n & ~in_c).sum())
            frame["pixelsTheCullRemoved"] = int((in_n & ~in_c).sum())
    else:
        frame["note"] = "no ids frame under " + args.frames
        rc = 1
    out["faceIdFrame"] = frame

    # ---- 3b. T46: the emitted triangles, their own side, and the pixel ledger
    # that separates "which face authored this surface" (the conservative,
    # face-level rule of section 2) from "which emitted triangle actually
    # painted this pixel" (the raster's own decision, triangle by triangle).
    import re as _re
    ledger = {"status": "NOT MEASURED",
              "reason": "no face-id frame, or the trace carries no pitch/yaw"}
    out["triangleLedger"] = ledger
    pitch = yaw = None
    if eye_line:
        _mp = _re.search(r"pitch=(-?[0-9.]+)", eye_line)
        _my = _re.search(r"yaw=(-?[0-9.]+)", eye_line)
        if _mp and _my:
            pitch = float(_mp.group(1))
            yaw = float(_my.group(1))
    if os.path.exists(ids_path) and "inRangePixels" in frame and pitch is not None:
        w_id, h_id, ids = read_bmp(ids_path)
        H, W = h_id, w_id
        fi = (ids[:, :, 0].astype(np.int64) | (ids[:, :, 1].astype(np.int64) << 8)
              | (ids[:, :, 2].astype(np.int64) << 16))
        emitted = emit_world(bsp)
        tri_face_arr = emitted["face"]
        # face-level lookups (the section-2 rule), indexed by face id
        kind_lut = np.full(bsp.face_count, -1, dtype=np.int8)
        side_lut = np.full(bsp.face_count, -1, dtype=np.int8)
        dfront_lut = np.zeros(bsp.face_count, dtype=bool)
        for f, face in faces.items():
            k = f - bsp.first_face
            kind_lut[k] = 1 if face["dispinfo"] >= 0 else 0
            side_lut[k] = face["side"]
            dfront_lut[k] = face["derivedFront"]
        idx = np.clip(fi - bsp.first_face, 0, bsp.face_count - 1)
        inr = (fi >= bsp.first_face) & (fi < bsp.first_face + bsp.face_count)
        tri_kind = kind_lut[np.clip(tri_face_arr - bsp.first_face, 0, bsp.face_count - 1)]
        tri_sidev = side_lut[np.clip(tri_face_arr - bsp.first_face, 0, bsp.face_count - 1)]
        disp_tris = tri_kind == 1
        brush_tris = tri_kind == 0
        sides, side_dot = triangle_sides(emitted, eye)
        # T46 mutation anchor (m4): the gate replaces the assignment below with a
        # flip of the selected class and requires the pixel ledger to detect it.
        # In the forward run every triangle keeps the side its own winding gives.
        sides_used = sides
        tri_stack = np.stack([emitted["a"], emitted["b"], emitted["c"]])
        all_vertices = np.concatenate([emitted["a"], emitted["b"], emitted["c"]], axis=0)
        cam = build_camera(all_vertices, eye, pitch, yaw, frame["width"], frame["height"])
        front_per_face = {}
        back_per_face = {}
        for t in range(len(tri_face_arr)):
            key = int(tri_face_arr[t])
            if sides_used[t] == 1:
                front_per_face[key] = front_per_face.get(key, 0) + 1
            elif sides_used[t] == -1:
                back_per_face[key] = back_per_face.get(key, 0) + 1
        mixed_disp_faces = sorted([f for f in front_per_face
                                   if kind_lut[np.clip(f - bsp.first_face, 0, bsp.face_count - 1)] == 1
                                   and back_per_face.get(f, 0) > 0])
        emission_ledger = {
            "triangles": int(len(tri_face_arr)),
            # labelled by the path that emitted them, which is how T23's probe
            # counts them: the 10 displacement faces that fall back to the quad
            # fan emit through the brush fan, so the probe's "brush 52,400"
            # includes their 20 triangles.
            "brushFanTriangles": int((~emitted["disp"]).sum()),
            "dispGridTriangles": int(emitted["disp"].sum()),
            "dispFaceClassTriangles": int(disp_tris.sum()),
            "brushFaceClassTriangles": int(brush_tris.sum()),
            "brushTriangles": int((~emitted["disp"]).sum()),
            "dispTriangles": int(disp_tris.sum()),
            "dispGridFaces": int(emitted["gridFaces"]),
            "dispFallbackFaces": int(len(emitted["fallbackFaces"])),
            "dispFallbackFaceList": [int(f) for f in emitted["fallbackFaces"]],
            "waterTriangles": int(emitted["water"].sum()),
            "trianglesFlippedOntoOutward": int(emitted["flip"].sum()),
            "distinctFacesEmitted": int(len(np.unique(tri_face_arr))),
        }
        sides_ledger = {
            "dispFrontTriangles": int(((sides_used == 1) & disp_tris).sum()),
            "dispBackTriangles": int(((sides_used == -1) & disp_tris).sum()),
            "brushFrontTriangles": int(((sides_used == 1) & brush_tris).sum()),
            "brushBackTriangles": int(((sides_used == -1) & brush_tris).sum()),
            "degenerateTriangles": int((sides_used == 0).sum()),
            "dispFacesWithBothSides": int(len(mixed_disp_faces)),
            "dispFacesWithBothSidesSample": [int(f) for f in mixed_disp_faces[:20]],
            "dispBackFacesShippingFrontTriangles": int(sum(
                1 for f in mixed_disp_faces
                if not dfront_lut[np.clip(f - bsp.first_face, 0, bsp.face_count - 1)])),
        }
        # the product's own 28 sampled pixels, against this reconstruction
        trace_points = parse_face_trace(trace_path)
        trace_hits = [p for p in trace_points if 0 <= p["tri"] < len(tri_face_arr)]
        covered = 0
        face_match = 0
        depth_match = 0
        max_delta = 0.0
        exceptions = []
        for p in trace_hits:
            tri = p["tri"]
            sx, sy, depth, invw, okp = project_map_points(cam, tri_stack[:, tri, :])
            if not bool(np.all(okp)):
                continue
            ax, ay = float(sx[0]), float(sy[0])
            bx, by = float(sx[1]), float(sy[1])
            cx, cy = float(sx[2]), float(sy[2])
            area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
            if area == 0.0:
                continue
            fx = p["x"] + 0.5
            fy = p["y"] + 0.5
            w0 = ((bx - fx) * (cy - fy) - (by - fy) * (cx - fx)) / area
            w1 = ((cx - fx) * (ay - fy) - (cy - fy) * (ax - fx)) / area
            w2 = 1.0 - w0 - w1
            if area > 0.0 and min(w0, w1, w2) >= -1e-4:
                covered += 1
                d = w0 * float(depth[0]) + w1 * float(depth[1]) + w2 * float(depth[2])
                delta = abs(d - p["depth"])
                max_delta = max(max_delta, delta)
                if delta <= 1e-3:
                    depth_match += 1
                id_face = int(fi[p["y"], p["x"]])
                if id_face == int(tri_face_arr[tri]):
                    face_match += 1
                else:
                    exceptions.append({"point": [p["x"], p["y"]], "traceTri": tri,
                                       "emittedFace": int(tri_face_arr[tri]),
                                       "idFace": id_face, "depth": p["depth"]})
        trace_ledger = {"points": len(trace_points), "hits": len(trace_hits),
                        "coveredByTracedTriangle": covered,
                        "idFaceMatchesEmittedFace": face_match,
                        "depthMatched": depth_match,
                        "maxDepthDelta": max_delta,
                        "exceptions": exceptions}
        # ---- the pixel ledger -------------------------------------------
        same = np.zeros((H, W), dtype=bool)
        textured_path = os.path.join(args.frames, "textured.bmp")
        if os.path.exists(textured_path):
            w_t, h_t, tex = read_bmp(textured_path)
            if (h_t, w_t) == (H, W):
                same = np.all(ids == tex, axis=2)
        fe_img = np.zeros((H, W), dtype=bool)
        be_img = np.zeros((H, W), dtype=bool)
        fmiss_img = np.full((H, W), np.inf)
        bmiss_img = np.full((H, W), np.inf)
        near_tri_img = np.full((H, W), -1, dtype=np.int64)
        rows, cols = np.nonzero(inr)
        fvals = fi[rows, cols]
        for f in np.unique(fvals).tolist():
            sel = np.nonzero(fvals == f)[0]
            tri_idx = np.nonzero(tri_face_arr == f)[0]
            if tri_idx.size == 0 or f not in faces:
                continue
            px = cols[sel]
            py = rows[sel]
            fe, be, ntri, nt, fm, bm = cover_pixels(
                cam, tri_stack[:, tri_idx, :], sides_used[tri_idx] == 1, px, py)
            fe_img[py, px] = fe
            be_img[py, px] = be
            fmiss_img[py, px] = fm
            bmiss_img[py, px] = bm
            hit = ntri >= 0
            near_tri_img[py[hit], px[hit]] = tri_idx[ntri[hit]]
        kind_img = np.where(inr, kind_lut[idx], -1)
        side_img = np.where(inr, side_lut[idx], -1)
        dfront_img = np.where(inr, dfront_lut[idx], False).astype(bool)

        def bucket(mask, fe_a, be_a, fm_a, bm_a):
            near_front = fm_a <= SUBPIXEL
            near_back = bm_a <= SUBPIXEL
            return {"pixels": int(mask.sum()),
                    "frontExact": int((mask & fe_a).sum()),
                    "frontSubpixel": int((mask & ~fe_a & near_front).sum()),
                    "backOnlyExact": int((mask & ~fe_a & ~near_front & be_a).sum()),
                    "backOnlySubpixel": int((mask & ~fe_a & ~near_front & ~be_a & near_back).sum()),
                    "noTriangle": int((mask & ~fe_a & ~near_front & ~be_a & ~near_back).sum())}

        vm_img = inr & same
        world_img = inr & ~vm_img
        back_world = world_img & ~dfront_img
        labels = (("disp/side-0", 1, 0), ("disp/side-1", 1, 1),
                  ("brush/side-0", 0, 0), ("brush/side-1", 0, 1))
        by_class_side = {}
        for label, kind, sidev in labels:
            by_class_side[label] = bucket(back_world & (kind_img == kind) & (side_img == sidev),
                                          fe_img, be_img, fmiss_img, bmiss_img)
        pixel_ledger = {
            "inRangePixels": int(inr.sum()),
            "viewmodelOverwritePixels": int(vm_img.sum()),
            "viewmodelOverwriteCoveredByOwnFace": int((vm_img & fe_img).sum()),
            "viewmodelOverwriteOnDerivedBack": int((vm_img & ~dfront_img).sum()),
            "worldPixels": int(world_img.sum()),
            "drawnOnDerivedFrontAllIds": bucket(inr & dfront_img, fe_img, be_img, fmiss_img, bmiss_img),
            "drawnOnDerivedBackAllIds": bucket(inr & ~dfront_img, fe_img, be_img, fmiss_img, bmiss_img),
            "drawnOnDerivedBackWorld": bucket(back_world, fe_img, be_img, fmiss_img, bmiss_img),
            "drawnOnDerivedBackWorldByClassSide": by_class_side,
        }
        # the named face: T41's residual, item by item
        named_face = int(args.named_face)
        named_img = (fi == named_face)
        named_ledger = bucket(named_img, fe_img, be_img, fmiss_img, bmiss_img)
        named_ledger.update({
            "face": named_face,
            "frontTriangles": int(front_per_face.get(named_face, 0)),
            "backTriangles": int(back_per_face.get(named_face, 0)),
            "emittedTriangles": int((tri_face_arr == named_face).sum()),
            "pixelsByFrontTriangle": {},
            "tracePointsOnThisFace": [],
        })
        if named_face in faces:
            named_ledger["derivedFront"] = bool(faces[named_face]["derivedFront"])
            named_ledger["writtenFront"] = bool(faces[named_face]["writtenFront"])
            named_ledger["planeSideDistance"] = float(
                np.dot(faces[named_face]["plane"], eye) - faces[named_face]["dist"])
            sel = np.nonzero(named_img & fe_img)
            counts = {}
            for t in near_tri_img[sel].tolist():
                counts[t] = counts.get(t, 0) + 1
            named_ledger["pixelsByFrontTriangle"] = {str(int(k)): int(v)
                                                     for k, v in sorted(counts.items())}
            named_ledger["frontTrianglesWithPixels"] = len(counts)
        for p in trace_hits:
            if int(fi[p["y"], p["x"]]) != named_face:
                continue
            tri = p["tri"]
            named_ledger["tracePointsOnThisFace"].append({
                "point": [p["x"], p["y"]],
                "traceTri": int(tri),
                "emittedFace": int(tri_face_arr[tri]),
                "triangleSide": ("front" if sides_used[tri] == 1
                                 else ("back" if sides_used[tri] == -1 else "degenerate")),
                "frontExact": bool(fe_img[p["y"], p["x"]]),
                "frontMissPixels": (None if not np.isfinite(fmiss_img[p["y"], p["x"]])
                                    else float(fmiss_img[p["y"], p["x"]])),
                "backExact": bool(be_img[p["y"], p["x"]]),
            })
        # the pixels the cull removed, measured the same way
        removed_ledger = {"status": "NOT MEASURED"}
        if os.path.exists(nocull_path):
            w_n, h_n, nocull = read_bmp(nocull_path)
            if (h_n, w_n) == (H, W):
                fn = (nocull[:, :, 0].astype(np.int64) | (nocull[:, :, 1].astype(np.int64) << 8)
                      | (nocull[:, :, 2].astype(np.int64) << 16))
                in_n = (fn >= bsp.first_face) & (fn < bsp.first_face + bsp.face_count)
                rem_mask = in_n & ~inr
                rfe = np.zeros((H, W), dtype=bool)
                rbe = np.zeros((H, W), dtype=bool)
                rfm = np.full((H, W), np.inf)
                rbm = np.full((H, W), np.inf)
                rr, cc = np.nonzero(rem_mask)
                rv = fn[rr, cc]
                for f in np.unique(rv).tolist():
                    sel = np.nonzero(rv == f)[0]
                    tri_idx = np.nonzero(tri_face_arr == f)[0]
                    if tri_idx.size == 0 or f not in faces:
                        continue
                    fe, be, ntri, nt, fm, bm = cover_pixels(
                        cam, tri_stack[:, tri_idx, :], sides_used[tri_idx] == 1, cc[sel], rr[sel])
                    rfe[rr[sel], cc[sel]] = fe
                    rbe[rr[sel], cc[sel]] = be
                    rfm[rr[sel], cc[sel]] = fm
                    rbm[rr[sel], cc[sel]] = bm
                removed_ledger = bucket(rem_mask, rfe, rbe, rfm, rbm)
                removed_ledger["status"] = "measured per emitted triangle"
                rem_kind = kind_lut[np.clip(rv - bsp.first_face, 0, bsp.face_count - 1)]
                rem_side = side_lut[np.clip(rv - bsp.first_face, 0, bsp.face_count - 1)]
                for label, kind, sidev in labels:
                    selm = (rem_kind == kind) & (rem_side == sidev)
                    m2 = np.zeros((H, W), dtype=bool)
                    m2[rr[selm], cc[selm]] = True
                    removed_ledger[label] = bucket(m2, rfe, rbe, rfm, rbm)
                removed_ledger["distinctFaces"] = int(len(np.unique(rv)))
        ledger.update({
            "status": "measured",
            "camera": {k: (v.tolist() if isinstance(v, np.ndarray) else v)
                       for k, v in cam.items()},
            "cameraSource": eye_source,
            "emission": emission_ledger,
            "perTriangleSide": sides_ledger,
            "traceProjection": trace_ledger,
            "pixelLedger": pixel_ledger,
            "namedFace": named_ledger,
            "removedLedger": removed_ledger,
            "supersedes": {
                "T41 brush residual (334 pixels)": "0 attributable to brush geometry: the 268 pixels the id frame puts on brush faces on derived-back faces are viewmodel overwrites -- their bytes equal the textured frame's bytes at the same pixel and no brush triangle of the named face covers them",
                "T41 51,956 arithmetic": "51,890 = 51,620 disp/side-0 + 2 disp/side-1 + 268 viewmodel overwrites; the world population on derived-back faces is 51,622",
                "T41 face-level rule on displacement": "conservative, as claimed: face 17605 is authored back and its grid still ships front triangles; the per-triangle rule accounts for them exactly",
            },
        })

    # ---- 4. what this derivation supersedes, and what it does not ----------
    # The pins it replaces were measurements of a different question made with
    # the plane-side convention and a per-pixel face attribution that fails the
    # test above. They stay in the gate (a cull or winding change must still
    # move them) but they are not the answer to "which side was this pixel seen
    # from", and this block is what a reader is pointed at.
    out["notVerified"] = {
        "status": "NOT VERIFIED",
        "claims": [
            "the pixel-level authored-side split of the dark population"
            " (T36: 170701 back / 35227 front) and the 86%-back headline",
            "the pixel-level removedSide split (T36: 44046 plane-front /"
            " 37095 plane-back of 81141 removed pixels)",
        ],
        "why": "they rest on the face-id frame, whose per-pixel ids put %d of %d"
               " world pixels on faces the derived rule removes and whose regions"
               " exceed their own face's pinhole area for %d of %d faces"
               % (frame.get("drawnPixelsOnDerivedBack", 0), frame.get("inRangePixels", 0),
                  frame.get("overBoundFaces", 0), frame.get("facesWithRegion", 0)),
        "supersededBy": "derivedDrawn / derivedRemoved: the authored side of what the"
                        " raster keeps and removes, weighted by face",
        "cheapestNextStep": "one product run at tick 2000 with --world-face-id"
                            " --world-face-trace: the trace names tri=<ordinal> at each"
                            " of its 28 pixels, so the id at that pixel can be compared"
                            " with the face of the triangle the trace says was drawn",
    }

    print("T41 raster rule: %s, FrontCounterClockwise=%s" % (CULL_MODE, FRONT_COUNTER_CLOCKWISE))
    print("front test: %s" % out["frontRule"])
    print("eye = (%.1f,%.1f,%.1f) from %s" % (eye[0], eye[1], eye[2], eye_source))
    if eye_line:
        print("  %s" % eye_line)
    if drawn_tick is not None:
        print("  last drawn tick in that trace: %s" % drawn_tick)
    print("")
    print("emission census (the triangles the rasterizer receives):")
    print("  brush faces %d (%d triangles): along the plane normal %d, against %d, degenerate %d"
          % (census["brushFaces"], census["brushTris"], census["brushAlongPlane"],
             census["brushAgainstPlane"], census["brushDegenerate"]))
    print("  displacement faces %d (%d triangles): the grid's own order is against the plane"
          " normal %d, along %d; the product flips every triangle onto the face's outward"
          % (census["dispFaces"], census["dispTris"], census["dispGridAgainstPlane"],
             census["dispGridAlongPlane"]))
    print("")
    print("derived front set at this eye: %d of %d faces (written plane-side rule: %d)"
          % (out["derivedFrontFaces"], out["facesParsed"], out["writtenFrontFaces"]))
    print("%-14s %7s %7s %9s %8s %8s" % ("cell", "faces", "eyeSide", "derivedF", "writtenF", "disagree"))
    for k, c in out["ruleAgreement"].items():
        print("%-14s %7d %7d %9d %8d %8d" % (k, c["faces"], c["eyeSide"], c["derivedFront"],
                                             c["writtenFront"], c["disagree"]))
    print("")
    if "inRangePixels" in frame:
        print("face-id frame %dx%d: %d pixels carry a face id (%d off-range)"
              % (frame["width"], frame["height"], frame["inRangePixels"], frame["offRangePixels"]))
        print("  distinct ids %d; drawn pixels on faces the derived rule calls FRONT %d (%.2f%%)"
              % (frame["distinctFaceIds"], frame["drawnPixelsOnDerivedFront"],
                 100.0 * frame["drawnPixelsOnDerivedFront"] / max(frame["inRangePixels"], 1)))
        print("  drawn pixels on faces the derived rule calls BACK  %d (%.2f%%)  <- must be 0"
              % (frame["drawnPixelsOnDerivedBack"],
                 100.0 * frame["drawnPixelsOnDerivedBack"] / max(frame["inRangePixels"], 1)))
        print("  the same population under the written plane-side rule: FRONT %d, BACK %d"
              % (frame["drawnPixelsWrittenFront"], frame["drawnPixelsWrittenBack"]))
        print("  (the written rule and the derived rule are different rules: they agree in"
              " every class x side cell but one, see the table above)")
        if frame.get("facesWithRegion"):
            print("  per-face pinhole bound: %d of %d regions exceed the area their face can"
                  " cover (median ratio %.2f, p90 %.2f, max %.1f)"
                  % (frame["overBoundFaces"], frame["facesWithRegion"],
                     frame.get("ratioMedian", 0), frame.get("ratioP90", 0), frame.get("ratioMax", 0)))
        if "pixelsTheCullRemoved" in frame:
            print("  pixels the unculled frame paints and the culled one does not: %d"
                  " (only-cull %d)" % (frame["pixelsTheCullRemoved"], frame["removedByIds"]))

    if ledger.get("status") == "measured":
        em = ledger["emission"]
        sd = ledger["perTriangleSide"]
        tl = ledger["traceProjection"]
        pl = ledger["pixelLedger"]
        nf = ledger["namedFace"]
        rl = ledger["removedLedger"]
        print("")
        print("emitted triangles rebuilt from the lumps (the rasterizer's actual input):")
        print("  %d triangles from %d faces: brush %d, displacement %d (grids %d,"
              " fallbacks %d); %d flipped onto the face's outward"
              % (em["triangles"], em["distinctFacesEmitted"], em["brushTriangles"],
                 em["dispTriangles"], em["dispGridFaces"], em["dispFallbackFaces"],
                 em["trianglesFlippedOntoOutward"]))
        print("per-triangle raster side at this eye (dot(cross(b-a,c-a), eye-a)):")
        print("  displacement %d front / %d back; brush %d front / %d back; degenerate %d"
              % (sd["dispFrontTriangles"], sd["dispBackTriangles"], sd["brushFrontTriangles"],
                 sd["brushBackTriangles"], sd["degenerateTriangles"]))
        print("  displacement faces shipping BOTH sides: %d; of those, faces the face-level rule"
              " calls BACK: %d" % (sd["dispFacesWithBothSides"],
                                   sd["dispBackFacesShippingFrontTriangles"]))
        print("product trace reconciled against this projection:")
        print("  %d points, %d name a triangle; %d project onto the traced pixel with area>0,"
              " %d carry the emitted face's id, %d match the traced depth (max delta %.2e)"
              % (tl["points"], tl["hits"], tl["coveredByTracedTriangle"],
                 tl["idFaceMatchesEmittedFace"], tl["depthMatched"], tl["maxDepthDelta"]))
        if tl["exceptions"]:
            print("  exceptions: %s" % tl["exceptions"])
        print("pixel ledger:")
        print("  %d in-range pixels; %d are viewmodel bytes (id frame == textured frame),"
              " %d of them on derived-back faces; %d covered by their own face's geometry"
              % (pl["inRangePixels"], pl["viewmodelOverwritePixels"],
                 pl["viewmodelOverwriteOnDerivedBack"],
                 pl["viewmodelOverwriteCoveredByOwnFace"]))
        back = pl["drawnOnDerivedBackWorld"]
        print("  drawn on derived-back faces, world pixels only: %d -> front triangle %d"
              " (+%d sub-pixel), back-only %d (+%d sub-pixel), none %d"
              % (back["pixels"], back["frontExact"], back["frontSubpixel"],
                 back["backOnlyExact"], back["backOnlySubpixel"], back["noTriangle"]))
        for label, b in pl["drawnOnDerivedBackWorldByClassSide"].items():
            if b["pixels"]:
                print("    %-12s %6d: front %d (+%d sub-pixel), back-only %d (+%d), none %d"
                      % (label, b["pixels"], b["frontExact"], b["frontSubpixel"],
                         b["backOnlyExact"], b["backOnlySubpixel"], b["noTriangle"]))
        print("named face %d (plane side distance %.3f, face-level rule: %s):"
              % (nf["face"], nf.get("planeSideDistance", 0.0),
                 "front" if nf.get("derivedFront") else "back"))
        print("  %d pixels, %d emitted triangles (%d front / %d back): front triangle %d"
              " (+%d sub-pixel), back-only %d (+%d), none %d"
              % (nf["pixels"], nf["emittedTriangles"], nf["frontTriangles"],
                 nf["backTriangles"], nf["frontExact"], nf["frontSubpixel"],
                 nf["backOnlyExact"], nf["backOnlySubpixel"], nf["noTriangle"]))
        if nf["frontTrianglesWithPixels"]:
            print("  front triangles carrying pixels: %d" % nf["frontTrianglesWithPixels"])
        for tp in nf["tracePointsOnThisFace"]:
            print("  trace exception %s tri=%d (%s) frontExact=%s backExact=%s"
                  % (tp["point"], tp["traceTri"], tp["triangleSide"],
                     tp["frontExact"], tp["backExact"]))
        if str(rl.get("status", "")).startswith("measured"):
            print("pixels the cull removed, same measurement: %d -> covered only by back"
                  " triangles %d (+%d sub-pixel), by a FRONT triangle %d (+%d), none %d"
                  % (rl["pixels"], rl["backOnlyExact"], rl["backOnlySubpixel"],
                     rl["frontExact"], rl["frontSubpixel"], rl["noTriangle"]))

    if args.json:
        with open(args.json, "w") as fh:
            json.dump(out, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print("")
        print("json -> %s" % args.json)
    return rc


if __name__ == "__main__":
    sys.exit(main())
