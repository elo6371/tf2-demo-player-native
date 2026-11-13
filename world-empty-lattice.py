"""T62 -- the empty population of a captured map frame, as a set algebra.

WHY THIS FILE EXISTS
  The geometry axis of the map matrix used to be judged by asking whether the
  empty pixels of a flat frame were covered by the map own BSP sky brushes. That
  question cannot be answered on a map whose sky is a six-face cubemap drawn
  behind a 3D skybox: the brushes are one small volume, and the measured
  coverage of the empty population swings from 100% (odyssey) to 0.4%
  (sunshine) on the same instrument. A share cap on that number measures the
  instrument, not the frame.

  What this file does instead is the algebra the gate already has the frames
  for. Every frame is captured at one pose with one flat world colour, so
  "drawn" is the set of pixels that carry that flat colour -- the world pass
  painted them -- and each of the two restrictors (backface culling, the PVS
  visibility clip) can be turned off independently. "Empty" is therefore "no
  world-pass triangle painted this pixel", which is what a hole would be.

  Drawn is deliberately not "anything that is not the clear colour". These
  captures keep the sky pass on, and the sky is a six-face cubemap drawn behind
  the level, so the geometry-free frame is a single clear colour while a flat
  world frame is ~5000 colours: the clear colour appears in zero pixels and
  "not clear" degenerates to "the whole frame" on all five maps. The LATTICE
  colours lines print that census next to the counts so a reader can see which
  regime the capture is in. The cubemap pixels are then the interesting part of
  the empty population -- they are what a removed triangle uncovers -- and G8's
  point oracle is what decides whether each of them is open sky or a triangle a
  restrictor refused. That gives four drawn sets whose nesting is an invariant
  of the renderer, not of the map:

    drawn_base          both restrictors on       (fewest pixels drawn)
    drawn_novis         cull on, vis clip off
    drawn_nocull        cull off, vis clip on
    drawn_alloff        both off                  (most pixels drawn)

    drawn_base subset drawn_novis subset drawn_alloff
    drawn_base subset drawn_nocull subset drawn_alloff

  and the pixels a restrictor removes are drawn again when both are off, which
  is what makes "the frame is empty here because a restrictor removed it" a
  statement the frames can prove rather than a story. A renderer that silently
  drops a triangle it parsed breaks the nesting or the attribution: the pixel
  would be empty in a frame whose restrictors are off, and no restrictor could
  be named for it.

WHAT IT PRINTS
  LATTICE status=ok             / LATTICE status=error reason=...
  LATTICE frame=WxH flat=r,g,b drawn_base=.. drawn_novis=.. drawn_nocull=..
          drawn_alloff=.. empty_base=.. empty_alloff=..
  LATTICE monotone base_not_in_novis=.. base_not_in_nocull=..
          novis_not_in_all=.. nocull_not_in_all=..
  LATTICE restrict cull_only=.. pvs_only=.. overlap=.. union=.. both=..
  LATTICE light albedo_dark=.. lit_dark=.. albedo_not_lit=..
  SAMPLE x y base=<empty|drawn> alloff=<empty|drawn>
  LATTICE points=<n> empty=<n> drawn=<n>

  cull_only  pixels drawn with both off and with the vis clip off (so only the
             cull is on) -- the pixels the cull removes.
  pvs_only   pixels drawn with both off and with the cull off -- the pixels the
             vis clip removes.
  both       pixels the two restrictors remove together.
  overlap    pixels each restrictor alone would have kept, yet both together
             remove (a depth-order interaction), so union = cull_only +
             pvs_only - overlap is the population that both-removed set must
             contain.

  The SAMPLE lines are the points the point oracle is asked about: the centroids
  of the largest empty and drawn components, snapped to a pixel of the same
  class, with overlay pixels excluded so the HUD cannot vote. They are chosen
  here so a later run asks the same question of the same pixels.

Usage: python world-empty-lattice.py --dir <capture directory> [--samples N]
Exit: 0 = the algebra held, 1 = it did not, 2 = an input is missing.
"""

import argparse
import collections
import os
import struct
import sys

FRAMES = ("flat-base", "flat-novis", "flat-nocull", "flat-novis-nocull")
EXTRA = ("none", "lmoff", "textured")
DARK = 16.0
MIN_COMPONENT = 400


def read_bmp(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 54 or data[0:2] != b"BM":
        raise ValueError("%s is not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if bits != 24 or compression != 0:
        raise ValueError("%s is not an uncompressed 24-bit BMP" % path)
    flip = height < 0
    height = abs(height)
    stride = ((width * 3) + 3) & ~3
    # A BMP stores BGR, and every comparison in this file (the flat colour, the
    # luminance weights) is written in RGB, so the swap happens once here. Doing
    # it lazily per call site is how a swapped-weight luminance silently counted
    # 187036 dark pixels where the lighting account counted 179168.
    out = bytearray(width * height * 3)
    for y in range(height):
        source = y if flip else (height - 1 - y)
        start = offset + source * stride
        row = data[start:start + width * 3]
        out[y * width * 3:(y + 1) * width * 3] = bytes(
            [channel for at in range(0, len(row), 3)
             for channel in (row[at + 2], row[at + 1], row[at])])
    return width, height, out


def mode_colour(pixels):
    counts = collections.Counter()
    for at in range(0, len(pixels), 3 * 7):
        counts[(pixels[at], pixels[at + 1], pixels[at + 2])] += 1
    return counts.most_common(1)[0][0]


def match(pixels, colour):
    r, g, b = colour
    out = bytearray(len(pixels) // 3)
    for index in range(len(out)):
        at = index * 3
        if pixels[at] == r and pixels[at + 1] == g and pixels[at + 2] == b:
            out[index] = 1
    return out


def luminance(pixels, index):
    at = index * 3
    return 0.299 * pixels[at] + 0.587 * pixels[at + 1] + 0.114 * pixels[at + 2]


def components(mask, width, height):
    """4-neighbour components of a 1-mask, largest first."""
    seen = bytearray(width * height)
    found = []
    for seed in range(width * height):
        if not mask[seed] or seen[seed]:
            continue
        stack = [seed]
        seen[seed] = 1
        cells = []
        while stack:
            at = stack.pop()
            cells.append(at)
            y, x = divmod(at, width)
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < height and 0 <= nx < width:
                    other = ny * width + nx
                    if mask[other] and not seen[other]:
                        seen[other] = 1
                        stack.append(other)
        found.append(cells)
    found.sort(key=len, reverse=True)
    return found


def snap(cells, width):
    """The cell of the component nearest its centroid."""
    total_x = total_y = 0
    for at in cells:
        y, x = divmod(at, width)
        total_x += x
        total_y += y
    cx = total_x / float(len(cells))
    cy = total_y / float(len(cells))
    best = cells[0]
    best_d = None
    for at in cells:
        y, x = divmod(at, width)
        d = (x - cx) ** 2 + (y - cy) ** 2
        if best_d is None or d < best_d:
            best_d = d
            best = at
    y, x = divmod(best, width)
    return x, y


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", required=True)
    parser.add_argument("--samples", type=int, default=8)
    args = parser.parse_args(argv)

    frames = {}
    for name in FRAMES + EXTRA:
        path = os.path.join(args.dir, name + ".bmp")
        if not os.path.exists(path):
            print("LATTICE status=error reason=missing %s" % path)
            return 2
        frames[name] = read_bmp(path)

    width, height, all_off_pixels = frames["flat-novis-nocull"]
    count = width * height
    flat = mode_colour(all_off_pixels)
    for name in FRAMES:
        if frames[name][0] != width or frames[name][1] != height:
            print("LATTICE status=error reason=%s is not %dx%d" % (name, frames[name][0], frames[name][1]))
            return 1

    # The clear colour and the overlay come from the geometry-free frame: every
    # pixel that is not its dominant colour is the HUD or an entity, and those
    # pixels say nothing about world geometry either way.
    clear = collections.Counter()
    none_pixels = frames["none"][2]
    for at in range(0, len(none_pixels), 3):
        clear[(none_pixels[at], none_pixels[at + 1], none_pixels[at + 2])] += 1
    background = clear.most_common(1)[0][0]
    overlay = bytearray(count)
    for index in range(count):
        at = index * 3
        if (none_pixels[at], none_pixels[at + 1], none_pixels[at + 2]) != background:
            overlay[index] = 1

    # Drawn means "the world pass painted this pixel with the flat colour". The
    # sky cubemap is not the world pass: it paints the pixels the world pass
    # leaves, and on this capture set it covers all of them, so an "is it the
    # clear colour" test would mark the whole frame drawn on every map. The
    # LATTICE colours line above prints flat/clear/other so that stays visible.
    def drawn_mask(pixels):
        mask = bytearray(count)
        for index in range(count):
            if overlay[index]:
                continue
            at = index * 3
            if (pixels[at], pixels[at + 1], pixels[at + 2]) == flat:
                mask[index] = 1
        return mask

    drawn = dict((name, drawn_mask(frames[name][2])) for name in FRAMES)
    base = drawn["flat-base"]
    novis = drawn["flat-novis"]
    nocull = drawn["flat-nocull"]
    alloff = drawn["flat-novis-nocull"]

    base_not_in_novis = sum(1 for i in range(count) if base[i] and not novis[i])
    base_not_in_nocull = sum(1 for i in range(count) if base[i] and not nocull[i])
    novis_not_in_all = sum(1 for i in range(count) if novis[i] and not alloff[i])
    nocull_not_in_all = sum(1 for i in range(count) if nocull[i] and not alloff[i])

    cull_only = sum(1 for i in range(count) if alloff[i] and not novis[i])
    pvs_only = sum(1 for i in range(count) if alloff[i] and not nocull[i])
    overlap = sum(1 for i in range(count) if alloff[i] and not novis[i] and not nocull[i])
    union = cull_only + pvs_only - overlap
    both = sum(1 for i in range(count) if alloff[i] and not base[i])

    # The albedo-only frame (--world-lightmap off) against the lit frame: a
    # pixel whose albedo is dark must be dark in the lit frame too. The reverse
    # does not hold -- bright albedo times dark light is a dark lit pixel -- and
    # that asymmetry is the whole point of the check, so only one direction is
    # asserted.
    lit = frames["lmoff"][2]
    textured = frames["textured"][2]
    albedo_dark = lit_dark = albedo_not_lit = 0
    for index in range(count):
        if luminance(lit, index) < DARK:
            albedo_dark += 1
            if luminance(textured, index) >= DARK:
                albedo_not_lit += 1
        if luminance(textured, index) < DARK:
            lit_dark += 1

    print("LATTICE status=ok dir=%s" % args.dir)
    print("LATTICE frame=%dx%d flat=%d,%d,%d drawn_base=%d drawn_novis=%d drawn_nocull=%d drawn_alloff=%d empty_base=%d empty_alloff=%d"
          % (width, height, flat[0], flat[1], flat[2],
             sum(base), sum(novis), sum(nocull), sum(alloff),
             count - sum(base), count - sum(alloff)))
    # The colour census is the reading that keeps the definition above honest:
    # flat = the world flat colour, clear = the geometry-free frame colour, and
    # other = the world pass painting something else, which on koth_bagel_rc11
    # is the sky brush. A map whose other count is 0 needs no explanation; a map
    # whose other count is large is the case that used to read as holes.
    for name in FRAMES:
        pixels = frames[name][2]
        flat_pixels = clear_pixels = 0
        for index in range(count):
            at = index * 3
            colour = (pixels[at], pixels[at + 1], pixels[at + 2])
            if colour == flat:
                flat_pixels += 1
            elif colour == background:
                clear_pixels += 1
        print("LATTICE colours %s flat=%d clear=%d other=%d"
              % (name, flat_pixels, clear_pixels, count - flat_pixels - clear_pixels))
    print("LATTICE monotone base_not_in_novis=%d base_not_in_nocull=%d novis_not_in_all=%d nocull_not_in_all=%d"
          % (base_not_in_novis, base_not_in_nocull, novis_not_in_all, nocull_not_in_all))
    print("LATTICE restrict cull_only=%d pvs_only=%d overlap=%d union=%d both=%d"
          % (cull_only, pvs_only, overlap, union, both))
    print("LATTICE light albedo_dark=%d lit_dark=%d albedo_not_lit=%d"
          % (albedo_dark, lit_dark, albedo_not_lit))

    # Sample points: the largest empty components of the frame the matrix is
    # looked at, and the largest drawn components, so the oracle is asked about
    # the pixels a hole would actually be made of.
    empty_mask = bytearray(count)
    drawn_mask = bytearray(count)
    for i in range(count):
        if not overlay[i]:
            if base[i]:
                drawn_mask[i] = 1
            else:
                empty_mask[i] = 1

    points = []
    for cells in components(empty_mask, width, height)[:args.samples]:
        if len(cells) < MIN_COMPONENT:
            continue
        x, y = snap(cells, width)
        points.append((x, y, "empty"))
    for cells in components(drawn_mask, width, height)[:args.samples]:
        if len(cells) < MIN_COMPONENT:
            continue
        x, y = snap(cells, width)
        points.append((x, y, "drawn"))

    kept = []
    seen = set()
    for x, y, label in points:
        if (x, y) in seen:
            continue
        seen.add((x, y))
        kept.append((x, y, label))

    for x, y, label in kept:
        index = y * width + x
        alloff_label = "drawn" if alloff[index] else "empty"
        print("SAMPLE %d %d base=%s alloff=%s" % (x, y, label, alloff_label))
    print("LATTICE points=%d empty=%d drawn=%d"
          % (len(kept), sum(1 for p in kept if p[2] == "empty"),
             sum(1 for p in kept if p[2] == "drawn")))

    for label, value in (
            ("base drawn outside the vis-off draw", base_not_in_novis),
            ("base drawn outside the cull-off draw", base_not_in_nocull),
            ("vis-off draw outside the both-off draw", novis_not_in_all),
            ("cull-off draw outside the both-off draw", nocull_not_in_all),
            ("drawn pixels whose albedo is dark but the frame is not", albedo_not_lit)):
        if value != 0:
            print("LATTICE violated %s = %d, expected 0" % (label, value))
            return 1
    if union > both:
        print("LATTICE violated the restrictor union = %d, larger than the pixels both remove = %d"
              % (union, both))
        return 1
    if not kept:
        print("LATTICE violated no sample point survived component selection")
        return 1
    print("LATTICE verdict=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
