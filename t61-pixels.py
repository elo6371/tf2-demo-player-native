#!/usr/bin/env python3
"""t61_pixels.py -- T61 pixel assertions on the PRODUCT frame.

Why this exists: every other T61 number is a counter or a trace line, and a
counter can be green while the picture is black (that is exactly how the
black-silhouette defect survived T60). The claim this tool checks is about
pixels the user actually sees:

  MASK   the entity pass's own coverage, measured by --world-entity-coverage:
         the entity pass returns exactly (255,0,255) and every other pass
         returns something else. An empty or tiny mask is a failure, not a
         vacuous pass.
  BODY   over that mask, the product frame is not a flat silhouette: it has a
         spread of colours and a non-trivial mean luminance.
  POSE   the animation drive changes those pixels: the same tick with and
         without --player-animation, and two different ticks, must each move a
         measurable share of the masked pixels.

Usage: t61_pixels.py <coverage.bmp> <animated.bmp> [--off <bmp>] [--later <bmp>]
Exit: 0 all bounds held, 1 a bound failed, 2 input unreadable.
"""
import struct
import sys


def load(path):
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise SystemExit("not a BMP: " + path)
    off = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit("want 24 or 32 bpp, got %d" % bpp)
    flip = height < 0
    height = abs(height)
    nbytes = bpp // 8
    stride = ((width * nbytes) + 3) & ~3
    px = []
    for y in range(height):
        row = data[off + y * stride: off + y * stride + width * nbytes]
        if not flip:
            row = row
        px.append([tuple(row[x * nbytes:x * nbytes + 3][::-1]) for x in range(width)])
    if not flip:
        px.reverse()
    return width, height, px


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {}
    i = 1
    while i < len(argv):
        if argv[i] in ("--off", "--later"):
            flags[argv[i][2:]] = argv[i + 1]
            i += 2
        else:
            i += 1
    cov_path, anim_path = args[0], args[1]
    w, h, cov = load(cov_path)
    w2, h2, anim = load(anim_path)
    if (w, h) != (w2, h2):
        raise SystemExit("size mismatch %dx%d vs %dx%d" % (w, h, w2, h2))

    mask = [(x, y) for y in range(h) for x in range(w) if cov[y][x] == (255, 0, 255)]
    others = {}
    for y in range(h):
        for x in range(w):
            p = cov[y][x]
            if p != (255, 0, 255):
                others[p] = others.get(p, 0) + 1
    total = w * h
    print("MASK pixels=%d of %d (%.2f%%) sentinel=(255,0,255)" % (len(mask), total, 100.0 * len(mask) / total))
    top = sorted(others.items(), key=lambda kv: -kv[1])[:3]
    print("MASK non-sentinel top=%s" % (top,))

    fail = 0
    if not (2000 <= len(mask) <= total // 4):
        print("FAIL mask size %d outside [2000, %d]; an empty mask would make" % (len(mask), total // 4))
        print("     every pixel assertion below vacuous")
        fail = 1
    else:
        print("OK   mask size %d in [2000, %d]" % (len(mask), total // 4))

    def stats(img, name):
        cols = {}
        lum = 0.0
        black = 0
        for (x, y) in mask:
            p = img[y][x]
            cols[p] = cols.get(p, 0) + 1
            l = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]
            lum += l
            if max(p) <= 8:
                black += 1
        n = max(len(mask), 1)
        mean = lum / n
        blackf = float(black) / n
        top3 = sorted(cols.items(), key=lambda kv: -kv[1])[:3]
        print("%s over mask: distinct=%d mean_lum=%.1f pure_black=%.3f top=%s"
              % (name, len(cols), mean, blackf, top3))
        return len(cols), mean, blackf

    distinct, mean, blackf = stats(anim, "BODY")
    if distinct >= 200:
        print("OK   body distinct colours %d >= 200 (not a flat fill)" % distinct)
    else:
        print("FAIL body distinct colours %d < 200 (flat silhouette)" % distinct); fail = 1
    if mean >= 12.0:
        print("OK   body mean luminance %.1f >= 12.0" % mean)
    else:
        print("FAIL body mean luminance %.1f < 12.0" % mean); fail = 1
    if blackf <= 0.35:
        print("OK   body pure-black share %.3f <= 0.35" % blackf)
    else:
        print("FAIL body pure-black share %.3f > 0.35 (silhouette again)" % blackf); fail = 1

    def moved(other_path, label, need):
        _, _, other = load(other_path)
        diff = 0
        big = 0
        for (x, y) in mask:
            a, b = anim[y][x], other[y][x]
            if a != b:
                diff += 1
                if max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) >= 24:
                    big += 1
        n = max(len(mask), 1)
        print("%s vs animated over mask: differ=%d (%.2f%%) differ_by_>=24=%d (%.2f%%)"
              % (label, diff, 100.0 * diff / n, big, 100.0 * big / n))
        if 100.0 * diff / n >= need:
            print("OK   %s moved %.2f%% >= %.1f%% of body pixels" % (label, 100.0 * diff / n, need))
        else:
            print("FAIL %s moved %.2f%% < %.1f%% of body pixels" % (label, 100.0 * diff / n, need))
            return 1
        return 0

    if "off" in flags:
        fail |= moved(flags["off"], "animation-off", 5.0)
    if "later" in flags:
        fail |= moved(flags["later"], "later-tick", 5.0)

    print("PIXEL-CHECK=%s" % ("FAIL" if fail else "PASS"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
