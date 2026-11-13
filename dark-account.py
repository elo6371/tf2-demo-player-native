#!/usr/bin/env python3
"""dark-account.py -- T36: attribute every dark pixel of the reference frame.

WHY THIS FILE EXISTS
-------------------
T29 measured 208,630 black pixels (24.24%) in the fixed reference frame and
attributed 98.7% of them to "lightmap-driven" without being able to split the
population by face side, because it had no face-id frame. T36 takes that
split, and asks the one question that separates a product defect from a map
property: does the dark pixel sit on lightmap DATA THAT IS DARK?

THE FRAMES IT NEEDS (one camera, one tick, one build)
----------------------------------------------------
  textured    the product frame (what is dark)
  ids         --world-face-id                        (which dface_t owns the pixel)
  ids-nocull  --world-face-id --no-world-cull        (the same, back faces included)
  lmoff       --world-lightmap off                   (albedo only, no lightmap term)
  flat        --world-flat                           (is there a world triangle at all)

THE ACCOUNT
------------
Every dark pixel (luminance < 16) lands in exactly one bucket. Two orthogonal
questions are asked of each:

  Q1  is the ALBEDO itself dark?   lmoff removes ONLY the lightmap term, so a
      pixel that is dark there too has a dark material, not a dark lightmap.

  Q2  what does the face own lightmap rectangle hold?   read straight from the
      BSP lighting lump with the product own RGBExp32 decode (bsp_map.cpp
      writeLuxel), bucketed by the rectangle mean: all-zero, <8, <32, <96, or
      bright. A face seen from its BACK side is named too, because the map
      bakes light for the side the face was authored as front.

A pixel is only attributable to a PRODUCT lightmap defect when the albedo is
bright (so light would make it visible) AND the rectangle it samples is not
dark. Everything else is the map own lighting, drawn faithfully.

Each bucket is written as a MASK (one byte per pixel) so a later run can
intersect populations pixel by pixel instead of by count, and the counts go
to a JSON ledger.

Usage: python dark-account.py [--frames DIR] [--out DIR] [--bsp PATH]
                               [--eye X,Y,Z] [--json PATH]
Exit:  0 = the account was written; 1 = a required frame is missing.
"""
import struct, lzma, json, os, sys, argparse
from collections import Counter

FLAT_COLOUR = (89, 158, 97)   # --world-flat writes (0.35, 0.62, 0.38)


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


def load_bsp(path):
    d = open(path, "rb").read()
    return {"planes": read_lump(d, 1), "faces": read_lump(d, 7),
            "lighting": read_lump(d, 8), "models": read_lump(d, 14)}


def bmp(path):
    """The product 24-bit BMP, rows in file order (top-down for our captures)."""
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    flip = h < 0
    h = abs(h)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24:
        raise SystemExit("%s: %d bpp is not the product 24-bit BMP" % (path, bpp))
    stride = (w * 3 + 3) // 4 * 4
    rows = []
    for y in range(h):
        src = y if flip else h - 1 - y
        base = off + src * stride
        rows.append([(d[base + x * 3 + 2], d[base + x * 3 + 1], d[base + x * 3])
                     for x in range(w)])
    return w, h, rows


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
    ap.add_argument("--out", default=".scratch/t36/account")
    ap.add_argument("--bsp",
                    default="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_odyssey.bsp")
    ap.add_argument("--eye", default="-2423.6,5566.0,-1280.0")
    ap.add_argument("--json", default=None)
    ap.add_argument("--dark-threshold", type=float, default=16.0)
    ap.add_argument("--albedo-threshold", type=float, default=16.0)
    args = ap.parse_args()

    eye = tuple(float(v) for v in args.eye.split(","))
    os.makedirs(args.out, exist_ok=True)

    names = ("textured", "ids", "ids-nocull", "lmoff", "flat")
    frames = {}
    for n in names:
        p = os.path.join(args.frames, n + ".bmp")
        if not os.path.exists(p):
            print("MISSING %s" % p, file=sys.stderr)
            return 1
        frames[n] = bmp(p)
    w, h, tex = frames["textured"]

    bsp = load_bsp(args.bsp)
    faces, planes, lighting = bsp["faces"], bsp["planes"], bsp["lighting"]
    firstFace, faceCount = struct.unpack_from("<ii", bsp["models"], 40)
    lastFace = firstFace + faceCount

    cache = {}

    def face_info(fi):
        """(rectangle mean, face side) for one dface_t, both read from the BSP."""
        if fi in cache:
            return cache[fi]
        base = fi * 56
        lo = struct.unpack_from("<i", faces, base + 20)[0]
        sw = struct.unpack_from("<i", faces, base + 36)[0] + 1
        sh = struct.unpack_from("<i", faces, base + 40)[0] + 1
        styles = faces[base + 16]
        rect = None
        if (styles != 255 and lo >= 0 and sw > 0 and sh > 0 and sw <= 512
                and sh <= 512 and lo + sw * sh * 4 <= len(lighting)):
            rect = [decode_luxel(lighting[lo + i * 4: lo + i * 4 + 4])
                    for i in range(sw * sh)]
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
        cache[fi] = (rect, st)
        return cache[fi]

    counts = Counter()
    masks = {}

    def put(bucket, x, y):
        counts[bucket] += 1
        if bucket not in masks:
            masks[bucket] = bytearray(w * h)
        masks[bucket][y * w + x] = 1

    for y in range(h):
        for x in range(w):
            if lum(tex[y][x]) >= args.dark_threshold:
                continue
            # Q0: is there a world triangle at all?
            fp = frames["flat"][2][y][x]
            if (abs(fp[0] - FLAT_COLOUR[0]) + abs(fp[1] - FLAT_COLOUR[1])
                    + abs(fp[2] - FLAT_COLOUR[2])) > 6:
                put("A_no_world_triangle", x, y)
                continue
            p = frames["ids"][2][y][x]
            if p[2] != 0:
                put("A_no_face_id", x, y)
                continue
            fi = p[0] | (p[1] << 8)
            if not (firstFace <= fi < lastFace):
                p2 = frames["ids-nocull"][2][y][x]
                fi2 = p2[0] | (p2[1] << 8) if p2[2] == 0 else -1
                if firstFace <= fi2 < lastFace:
                    put("B_cull_only_nocull", x, y)
                else:
                    put("A_no_face_id", x, y)
                continue
            rect, st = face_info(fi)
            albedo_dark = lum(frames["lmoff"][2][y][x]) < args.albedo_threshold
            if rect is None:
                put("B0_no_rect_albedoDark" if albedo_dark else
                    "B0_no_rect_albedoBright", x, y)
                continue
            mean = sum(max(v) for v in rect) / float(len(rect))
            if mean < 1.0:
                tone = "rect_all_zero"
            elif mean < 8.0:
                tone = "rect_lt_8"
            elif mean < 32.0:
                tone = "rect_lt_32"
            elif mean < 96.0:
                tone = "rect_lt_96"
            else:
                tone = "rect_bright"
            put("%s_%s_albedo%s" % (tone, st, "Dark" if albedo_dark else "Bright"),
                x, y)

    dark = sum(counts.values())
    print("frame %dx%d  dark(lum<%.0f) = %d of %d (%.2f%%)"
          % (w, h, args.dark_threshold, dark, w * h, 100.0 * dark / (w * h)))
    for k in sorted(counts):
        print("  %-38s %7d (%6.2f%%)" % (k, counts[k], 100.0 * counts[k] / dark))
    print("")
    print("masks (1 = in the population):")
    for k in sorted(masks):
        path = os.path.join(args.out, k + ".mask")
        open(path, "wb").write(bytes(masks[k]))
        print("  %-38s %7d px -> %s" % (k, sum(masks[k]), path))
    ledger = {"width": w, "height": h, "dark": dark,
              "darkThreshold": args.dark_threshold,
              "albedoThreshold": args.albedo_threshold,
              "eyeMap": list(eye), "bsp": args.bsp,
              "counts": dict(counts),
              "productAttributable": sum(
                  v for k, v in counts.items()
                  if "albedoBright" in k and "rect_bright" in k)}
    jp = args.json or os.path.join(args.out, "account.json")
    json.dump(ledger, open(jp, "w"), indent=1)
    print("")
    print("ledger -> %s" % jp)
    print("product-attributable (albedo bright AND rectangle bright): %d px (%.4f%% of dark)"
          % (ledger["productAttributable"],
             100.0 * ledger["productAttributable"] / dark))
    return 0


if __name__ == "__main__":
    sys.exit(main())
