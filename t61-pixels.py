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
  BODY   is the mask restricted to bodies that can animate. Since 8fea2539
         (2026-11-12) the same pass also draws the map's static props
         (key=bsp-static:*), which carry no entity id and never move; on the
         snakewater pose they cover 34% of the frame against the animated
         body's 0.9%. Counting them in "how much did animation move" diluted a
         66.6% signal to 1.8% and made the bound fail for a static truss. Pass
         --identity to keep only the sentinel pixels whose identity-frame
         colour decodes to an entity id (g == 0 and b == 0 and r > 0, the
         t61-entityid.py test); the excluded pixels are reported as SCENERY and
         must themselves be still, which is a stronger claim than dropping
         them silently.
  BODY   over that mask, the product frame is not a flat silhouette: it has a
         spread of colours and a non-trivial mean luminance.
  POSE   the animation drive changes those pixels: the same tick with and
         without --player-animation, and two different ticks, must each move a
         measurable share of the masked pixels.

Usage: t61_pixels.py <coverage.bmp> <animated.bmp> [--identity <bmp>]
                       [--off <bmp>] [--later <bmp>]
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
        if argv[i] in ("--off", "--later", "--identity"):
            flags[argv[i][2:]] = argv[i + 1]
            i += 2
        else:
            i += 1
    cov_path, anim_path = args[0], args[1]
    w, h, cov = load(cov_path)
    w2, h2, anim = load(anim_path)
    if (w, h) != (w2, h2):
        raise SystemExit("size mismatch %dx%d vs %dx%d" % (w, h, w2, h2))

    sentinel = [(x, y) for y in range(h) for x in range(w) if cov[y][x] == (255, 0, 255)]
    others = {}
    for y in range(h):
        for x in range(w):
            p = cov[y][x]
            if p != (255, 0, 255):
                others[p] = others.get(p, 0) + 1
    total = w * h
    print("MASK pixels=%d of %d (%.2f%%) sentinel=(255,0,255)" % (len(sentinel), total, 100.0 * len(sentinel) / total))
    top = sorted(others.items(), key=lambda kv: -kv[1])[:3]
    print("MASK non-sentinel top=%s" % (top,))

    # Same pass, two kinds of body. bsp-static ranges are the map's own props:
    # no entity id, no animation, and on this pose 34% of the frame against the
    # animated body's 0.9%. They belong to the picture, not to the animation
    # claim, so the mask the pose assertions read must exclude them -- and the
    # exclusion has to be proved, not assumed, by requiring them to be still.
    scenery = []
    mask = sentinel
    identity_empty = False
    if "identity" in flags:
        _, _, ident = load(flags["identity"])
        mask = []
        for (x, y) in sentinel:
            r, g, b = ident[y][x]
            if g == 0 and b == 0 and r > 0:
                mask.append((x, y))
            else:
                scenery.append((x, y))
        print("MASK identity-coloured=%d scenery=%d (%.2f%% of the frame carries no"
              " entity id, so it cannot animate)"
              % (len(mask), len(scenery), 100.0 * len(scenery) / total))
        # 2026-11-22: no early return. A mutation that stops drawing bodies must
        # still reach the named BODY/POSE reasons below (the mutation suite greps
        # them), so an empty identity mask is one FAIL among the others, not the
        # only line printed.
        identity_empty = not mask
    else:
        mask = sentinel

    fail = 0
    if identity_empty:
        print("FAIL no sentinel pixel survives the identity filter: the"
              " coverage view and the id view disagree about what the"
              " entity pass drew, so a pose claim would be vacuous")
        fail = 1
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

    def moved(other_path, label, need, pixels=None):
        pixels = mask if pixels is None else pixels
        _, _, other = load(other_path)
        diff = 0
        big = 0
        for (x, y) in pixels:
            a, b = anim[y][x], other[y][x]
            if a != b:
                diff += 1
                if max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) >= 24:
                    big += 1
        n = max(len(pixels), 1)
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

    # The other half of the exclusion above. Dropping the scenery from the mask
    # is only honest if those pixels are what they are claimed to be: static map
    # geometry with no animation of its own. If they move when the animation flag
    # is switched off, the filter is hiding a body, not a prop, and the pose
    # claim above would have been diluted on purpose.
    # 2026-11-22: measured only against --off. Without an off frame nothing was
    # compared, and printing "OK scenery is still" for 0 of 0 compared pixels
    # would be the vacuous-pass shape this tool exists to refuse.
    if scenery and "off" not in flags:
        print("SCENERY not measured: no --off frame to compare the excluded pixels against")
    elif scenery:
        moved_scenery = 0
        _, _, other = load(flags["off"])
        for (x, y) in scenery:
            if anim[y][x] != other[y][x]:
                moved_scenery += 1
        ns = max(len(scenery), 1)
        share = 100.0 * moved_scenery / ns
        print("SCENERY vs animated over the excluded pixels: differ=%d (%.2f%%)"
              % (moved_scenery, share))
        if share <= 1.0:
            print("OK   scenery is still: %.2f%% <= 1.0%% of the excluded pixels"
                  " moved, so the exclusion removed props and not a body" % share)
        else:
            print("FAIL scenery moved %.2f%% > 1.0%%: the pixels the identity"
                  " filter dropped are animated, so the filter removed a body" % share)
            fail = 1

    print("PIXEL-CHECK=%s" % ("FAIL" if fail else "PASS"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
