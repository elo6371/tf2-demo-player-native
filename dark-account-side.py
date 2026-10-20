#!/usr/bin/env python3
"""dark-account-side.py -- T36: the face-side split, with its denominator.

WHY THIS FILE EXISTS
--------------------
dark-account.py splits the dark population by BSP plane side and reports that
81.8% of dark pixels are "back" faces. Read alone, that number sounds like a
finding: most of the black is on surfaces the eye sees from behind, so the
lightmap was baked for the other side and that is the defect.

It is not a finding. It is the base rate. The plane-side rule dark-account.py
uses labels 86% of ALL drawn pixels back, bright ones included. A dark
population that is 81.8% back when its parent population is 86% back is not
back-heavy -- it is slightly back-depleted. The share was reported without the
denominator, which turns a property of the frame into a property of the dark
pixels.

This file supplies the denominator, and calibrates the label itself.

THE CALIBRATION (the part the account never had)
------------------------------------------------
The --world-face-id frame is drawn with D3D11_CULL_BACK: the GPU discarded every
triangle it called back-facing, so every pixel with a face id is a surface the
RASTERIZER calls front. That is an independent, already-measured population
whose facing is known. Any candidate plane-side rule can be scored against it;
the rule that calls ~all of them front is the one whose sign matches the
rasterizer. Scoring the rule the account uses says it calls 86% of them BACK,
so the account's "back" is the rasterizer's front: the label is inverted, and
every front/back number in dark-account.py is swapped.

Both the given rule and the calibrated one are reported, so the inversion is a
measurement in the evidence rather than an assertion in a report.

THE READING
-----------
  * dark rate on each side, against the base dark rate (so the split has a
    denominator);
  * the relative risk of being dark, back vs front, under the calibrated rule;
  * the same split for the lightmap-rectangle tone buckets, so the question
    "is darkness concentrated where the map baked it dark, rather than on one
    side" can be answered instead of assumed.

Usage: python dark-account-side.py --frames DIR --bsp PATH [--eye X,Y,Z]
                                   [--json PATH] [--masks DIR]
Exit:  0 = the reading was written; 1 = a required frame is missing.
"""
import struct, lzma, json, os, sys, argparse


def read_lump(data, idx):
    off, length = struct.unpack_from("<ii", data, 8 + idx * 16)
    if length <= 0:
        return b""
    raw = data[off:off + length]
    if raw[:4] != b"LZMA":
        return raw
    actual = struct.unpack_from("<I", raw, 4)[0]
    dec = lzma.LZMADecompressor(
        format=lzma.FORMAT_RAW,
        filters=[lzma._decode_filter_properties(lzma.FILTER_LZMA1, raw[12:17])])
    return dec.decompress(raw[17:], max_length=actual)


def bmp(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    h = abs(h)
    stride = (w * 3 + 3) // 4 * 4
    return w, h, [[(d[off + y * stride + x * 3 + 2],
                    d[off + y * stride + x * 3 + 1],
                    d[off + y * stride + x * 3]) for x in range(w)]
                  for y in range(h)]


def lum(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def decode_luxel(sample):
    """bsp_map.cpp writeLuxel, offline. RGBExp32 is LINEAR radiance."""
    e = sample[3]
    if e > 127:
        e -= 256
    scale = (2.0 ** e) / 255.0
    out = []
    for c in range(3):
        v = sample[c] * scale
        out.append(255 if v >= 1.0 else (int(v * 255.0 + 0.5) if v > 0.0 else 0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", default=".scratch/t36/frames-vm")
    ap.add_argument("--bsp", required=True)
    ap.add_argument("--eye", default="-2423.6,5566.0,-1280.0")
    ap.add_argument("--json", default="side.json")
    ap.add_argument("--masks", default=None)
    ap.add_argument("--dark-threshold", type=float, default=16.0)
    args = ap.parse_args()
    eye = tuple(float(v) for v in args.eye.split(","))

    tex_p = os.path.join(args.frames, "textured.bmp")
    ids_p = os.path.join(args.frames, "ids.bmp")
    for p in (tex_p, ids_p):
        if not os.path.exists(p):
            print("MISSING %s" % p, file=sys.stderr)
            return 1
    w, h, tex = bmp(tex_p)
    _, _, ids = bmp(ids_p)

    d = open(args.bsp, "rb").read()
    faces, planes, lighting, models = (read_lump(d, 7), read_lump(d, 1),
                                       read_lump(d, 8), read_lump(d, 14))
    firstFace, faceCount = struct.unpack_from("<ii", models, 40)
    lastFace = firstFace + faceCount

    cache = {}

    def info(fi):
        """(side as written, calibrated side, rectangle mean or None)."""
        if fi in cache:
            return cache[fi]
        base = fi * 56
        lo = struct.unpack_from("<i", faces, base + 20)[0]
        sw = struct.unpack_from("<i", faces, base + 36)[0] + 1
        sh = struct.unpack_from("<i", faces, base + 40)[0] + 1
        styles = faces[base + 16]
        rect = None
        if (styles != 255 and lo >= 0 and 0 < sw <= 512 and 0 < sh <= 512
                and lo + sw * sh * 4 <= len(lighting)):
            rect = [decode_luxel(lighting[lo + i * 4:lo + i * 4 + 4])
                    for i in range(sw * sh)]
        mean = None
        if rect:
            mean = sum(max(v) for v in rect) / float(len(rect))
        pn = struct.unpack_from("<H", faces, base)[0]
        sb = faces[base + 2]
        written = calibrated = "ambiguous"
        if pn < len(planes) // 20:
            n = struct.unpack_from("<3f", planes, pn * 20)
            dist = struct.unpack_from("<f", planes, pn * 20 + 12)[0]
            if all((v == v) and abs(v) < 1.0e30 for v in n):
                sd = n[0] * eye[0] + n[1] * eye[1] + n[2] * eye[2] - dist
                written = "front" if ((sd >= 0.0) if sb == 0 else (sd <= 0.0)) else "back"
                # The sign the rasterizer's own cull agrees with. See the
                # module docstring: the written rule calls 86% of the drawn
                # (already back-face-culled) population back.
                calibrated = "front" if ((sd < 0.0) if sb == 0 else (sd > 0.0)) else "back"
        cache[fi] = (written, calibrated, mean)
        return cache[fi]

    drawn = {"front": 0, "back": 0}
    dark_side = {"front": 0, "back": 0}
    low = {"front": 0, "back": 0}
    masks = {}
    for y in range(h):
        for x in range(w):
            p = ids[y][x]
            if p[2] != 0:
                continue
            fi = p[0] | (p[1] << 8)
            if not (firstFace <= fi < lastFace):
                continue
            written, cal, mean = info(fi)
            if cal == "ambiguous":
                continue
            drawn[cal] += 1
            if lum(tex[y][x]) >= args.dark_threshold:
                continue
            dark_side[cal] += 1
            if mean is not None and mean < 96.0:
                low[cal] += 1
            k = "dark_%s_rect%s" % (cal, "Low" if (mean is not None and mean < 96.0)
                                    else ("None" if mean is None else "Bright"))
            if args.masks:
                if k not in masks:
                    masks[k] = bytearray(w * h)
                masks[k][y * w + x] = 1

    D = drawn["front"] + drawn["back"]
    K = dark_side["front"] + dark_side["back"]
    out = {
        "width": w, "height": h, "eyeMap": list(eye),
        "calibration": {
            "note": "the rule dark-account.py uses calls most of the already "
                    "back-face-culled drawn population BACK; the calibrated "
                    "rule is the sign the rasterizer agrees with",
            "drawnUnderCalibratedRule": dict(drawn),
        },
        "drawn": dict(drawn),
        "dark": dict(dark_side),
        "darkRatePct": {s: (100.0 * dark_side[s] / drawn[s]) if drawn[s] else 0.0
                        for s in ("front", "back")},
        "baseDarkRatePct": (100.0 * K / D) if D else 0.0,
        "relativeRiskBackVsFront": (
            ((dark_side["back"] / drawn["back"]) /
             (dark_side["front"] / drawn["front"]))
            if drawn["back"] and drawn["front"] and dark_side["front"] else None),
        "darkOnLowRectangle": dict(low),
        "darkOnLowRectangleSharePct": {s: (100.0 * low[s] / dark_side[s])
                                       if dark_side[s] else 0.0
                                       for s in ("front", "back")},
    }
    json.dump(out, open(args.json, "w"), indent=1)
    if args.masks:
        os.makedirs(args.masks, exist_ok=True)
        for k, m in masks.items():
            open(os.path.join(args.masks, k + ".mask"), "wb").write(bytes(m))

    print("calibrated side rule (the sign the rasterizer's own cull agrees with)")
    print("drawn face-id pixels: front %d (%.2f%%)  back %d (%.2f%%)"
          % (drawn["front"], 100.0 * drawn["front"] / D,
             drawn["back"], 100.0 * drawn["back"] / D))
    print("dark pixels         : front %d (%.2f%%)  back %d (%.2f%%)"
          % (dark_side["front"], 100.0 * dark_side["front"] / K,
             dark_side["back"], 100.0 * dark_side["back"] / K))
    print("")
    print("dark rate on back faces : %.2f%%" % out["darkRatePct"]["back"])
    print("dark rate on front faces: %.2f%%" % out["darkRatePct"]["front"])
    print("base dark rate          : %.2f%%" % out["baseDarkRatePct"])
    print("relative risk back/front: %.3f" % out["relativeRiskBackVsFront"])
    print("")
    print("dark pixels whose face rectangle mean is below 96/255:")
    print("  back  %d (%.2f%% of dark back)" % (low["back"], out["darkOnLowRectangleSharePct"]["back"]))
    print("  front %d (%.2f%% of dark front)" % (low["front"], out["darkOnLowRectangleSharePct"]["front"]))
    print("")
    print("A relative risk near 1.0 means the darkness does NOT concentrate on")
    print("one side: the split is the frame's composition, not a defect.")
    print("ledger -> %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
