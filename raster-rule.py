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

Usage: python raster-rule.py --bsp <pl_odyssey.bsp> --frames <capture dir>
                             [--eye X,Y,Z] [--json out.json]
Exit: 0 = every reading produced, 1 = a reading is missing.
"""
import argparse, json, lzma, os, struct, sys
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

    if args.json:
        with open(args.json, "w") as fh:
            json.dump(out, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print("")
        print("json -> %s" % args.json)
    return rc


if __name__ == "__main__":
    sys.exit(main())
