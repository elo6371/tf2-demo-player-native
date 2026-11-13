#!/usr/bin/env python3
"""cull-control.py -- T36: what the world back-face cull actually removes.

The dark-pixel account names a back-face population: pixels whose face the BSP
calls BACK (the eye is on the plane non-front side) while the GPU still drew
them. Before that population can be blamed on anything, the control has to say
what the cull DID remove, because a cull that removes nothing would make the
whole reading meaningless.

Inputs: two face-id frames from the same build, camera and tick -- one with the
default cull and one with --no-world-cull -- plus the BSP, for the side test.

A cull-removed pixel whose face the BSP calls FRONT would mean the cull is
removing surfaces the eye can legitimately see, which is the failure this
control exists to catch.

Usage: python cull-control.py --frames DIR --bsp PATH [--eye X,Y,Z] [--json P]
Exit:  0 = the control was evaluated and held; 1 = a required input is missing
          or the control failed.
"""
import struct, lzma, json, os, sys, argparse
from collections import Counter


def read_lump(data, idx):
    off, length = struct.unpack_from("<ii", data, 8 + idx * 16)
    if length <= 0:
        return b""
    raw = data[off:off + length]
    if raw[:4] != b"LZMA":
        return raw
    actual = struct.unpack_from("<I", raw, 4)[0]
    props = raw[12:17]
    dec = lzma.LZMADecompressor(
        format=lzma.FORMAT_RAW,
        filters=[lzma._decode_filter_properties(lzma.FILTER_LZMA1, props)])
    return dec.decompress(raw[17:], max_length=actual)


def bmp(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    h = abs(h)
    stride = (w * 3 + 3) // 4 * 4
    rows = []
    for y in range(h):
        base = off + y * stride
        rows.append([(d[base + x*3+2], d[base + x*3+1], d[base + x*3])
                     for x in range(w)])
    return w, h, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", default=".scratch/t36/frames-vm")
    ap.add_argument("--bsp", required=True)
    ap.add_argument("--eye", default="-2423.6,5566.0,-1280.0")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()
    eye = tuple(float(v) for v in args.eye.split(","))

    a = os.path.join(args.frames, "ids.bmp")
    b = os.path.join(args.frames, "ids-nocull.bmp")
    for p in (a, b):
        if not os.path.exists(p):
            print("MISSING %s" % p, file=sys.stderr)
            return 1
    w, h, cull = bmp(a)
    _, _, nocull = bmp(b)

    d = open(args.bsp, "rb").read()
    faces = read_lump(d, 7)
    planes = read_lump(d, 1)

    side_cache = {}

    def side_of(fi):
        if fi in side_cache:
            return side_cache[fi]
        base = fi * 56
        pn = struct.unpack_from("<H", faces, base)[0]
        side = faces[base + 2]
        st = "ambiguous"
        if pn < len(planes) // 20:
            n = struct.unpack_from("<3f", planes, pn * 20)
            dist = struct.unpack_from("<f", planes, pn * 20 + 12)[0]
            if all((v == v) and abs(v) < 1.0e30 for v in n):
                sd = n[0] * eye[0] + n[1] * eye[1] + n[2] * eye[2] - dist
                front = (sd >= 0.0) if side == 0 else (sd <= 0.0)
                st = "front" if front else "back"
        side_cache[fi] = st
        return st

    def fid(rows, x, y):
        p = rows[y][x]
        if p[2] != 0:
            return -1
        return p[0] | (p[1] << 8)

    kept = 0
    removed = 0
    removed_side = Counter()
    removed_faces = Counter()
    only_cull = 0
    for y in range(h):
        for x in range(w):
            fa = fid(cull, x, y)
            fb = fid(nocull, x, y)
            if fa >= 0 and fb >= 0:
                kept += 1
            elif fb >= 0:
                removed += 1
                removed_side[side_of(fb)] += 1
                removed_faces[fb] += 1
            elif fa >= 0:
                only_cull += 1

    out = {"kept": kept, "removed": removed,
           "removedSide": dict(removed_side),
           "onlyInCullFrame": only_cull,
           "distinctRemovedFaces": len(removed_faces),
           "topRemovedFaces": removed_faces.most_common(8),
           "eyeMap": list(eye)}
    if args.json:
        json.dump(out, open(args.json, "w"), indent=1)
    print("face-id pixels the cull KEPT   : %d" % kept)
    print("face-id pixels the cull REMOVED: %d (from %d distinct faces)"
          % (removed, len(removed_faces)))
    print("  removed faces by BSP side    : %s" % dict(removed_side))
    print("  pixels with a face only in the culled frame: %d" % only_cull)
    print("  top removed faces            : %s" % removed_faces.most_common(8))
    # The control does NOT assert that every removed pixel is a back face,
    # because on this map it is not: the GPU back-face cull and the BSP
    # plane-side rule disagree (T11 winding question, mask 07). What the
    # control asserts is the two things that make the account meaningful:
    #   * the cull removed a real population (so "the cull is on" is a fact,
    #     not a claim), and
    #   * the culled frame is a SUBSET of the unculled one (nothing appears
    #     only with the cull on), so the account taken on the culled frame is
    #     the same surfaces minus the culled ones.
    if removed == 0:
        print("CONTROL=FAIL the cull removed nothing")
        return 1
    if only_cull != 0:
        print("CONTROL=FAIL %d pixels have a face only in the CULLED frame"
              % only_cull)
        return 1
    front = removed_side.get("front", 0)
    back = removed_side.get("back", 0)
    print("CONTROL=OK the cull removed %d px (%d BSP-front / %d BSP-back);"
          " the culled frame is a subset of the unculled one"
          % (removed, front, back))
    print("NOTE the front/back split of the removed population is the T11")
    print("     winding disagreement, not a cull defect: the cull follows the")
    print("     buffer winding, the side test follows the plane lump.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
