#!/usr/bin/env python3
"""outline-frame-check.py -- the pixel half of T52's acceptance.

WHY THIS EXISTS
  Three of T52's criteria are statements about PIXELS, and none of them can
  be made by counting differing bytes:

    (a) NON-INTERFERENCE. "The overlay does not change world geometry or
        ordinary visibility" means every pixel OUTSIDE the drawn silhouette is
        byte-identical to the same frame with the overlay off. A byte count
        cannot say that; a bound on WHERE the difference is can.
    (b) TEAM COLOUR, BOTH DIRECTIONS. A frame drawn with the colours swapped
        still "contains red and blue pixels". The predicate has to be directional
        AND tied to the entity, so the reading is "the region this RED player
        covers is red-dominant and the region the BLUE player covers is
        blue-dominant", not "red and blue both appear".
    (c) DEPTH AWARENESS. The depth-tested control is not "fewer pixels changed".
        The region a wall occludes must contain team-coloured pixels in the
        x-ray frame and NONE in the depth-tested frame, and the depth-tested
        region must be byte-identical to the overlay-off frame there.

  The regions come from the product, never from this script guessing: the trace
  prints the normalised-box bounds of what the overlay drew, and the gate
  projects those bounds with the same map-to-box transform the renderer uses.
  A script that searched the frame for "wherever it changed" would pass on a
  silhouette that had merely moved.

READINGS, ALL PRINTED, ALL ASSERTED BY THE GATE
  diff_outside          differing pixels outside every drawn region
  diff_inside           differing pixels inside them (must be > 0)
  red_inside            red-dominant pixels inside the red player's region
  blue_inside           blue-dominant pixels inside the blue player's region
  red_wrong_team        red-dominant pixels inside the BLUE player's region
  blue_wrong_team       blue-dominant pixels inside the RED player's region
  occluded_xray         team-coloured pixels in the occluded region, x-ray frame
  occluded_tested       same region, depth-tested frame
  occluded_tested_diff  differing pixels vs the overlay-off frame, same region

Exit: 0 = every requested assertion held, 1 = one did not, 2 = bad input.
"""
import struct
import sys


def read_frame(path):
    data = open(path, "rb").read()
    if data[0:2] != b"BM":
        raise SystemExit("not a BMP: " + path)
    off = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit("want 24 or 32 bpp, got %d" % bpp)
    flip = height < 0
    height = abs(height)
    return data[off:], width, height, bpp // 8, flip


class Frame(object):
    def __init__(self, path):
        self.pixels, self.width, self.height, self.bpp, self.flip = read_frame(path)
        self.stride = self.width * self.bpp

    def offset(self, x, y):
        row = y if self.flip else (self.height - 1 - y)
        return row * self.stride + x * self.bpp

    def rgb(self, x, y):
        offset = self.offset(x, y)
        b = self.pixels[offset + 0]
        g = self.pixels[offset + 1]
        r = self.pixels[offset + 2]
        return r, g, b

    def same(self, other, x, y):
        offset = self.offset(x, y)
        return self.pixels[offset:offset + self.bpp] == other.pixels[offset:offset + self.bpp]


# The colour predicate, mirroring entity_overlay.cpp exactly. Kept in two places
# on purpose: the C++ side classifies an ITEM's colour, this side classifies a
# PIXEL, and a gate that only had one of them could not tell a wrong colour from
# a wrong classification.
MARGIN = 48
FLOOR = 96


def is_red(r, g, b):
    return r >= g + MARGIN and r >= b + MARGIN and r >= FLOOR


def is_blue(r, g, b):
    return b >= r + MARGIN and b >= g + MARGIN and b >= FLOOR


def is_team_coloured(r, g, b):
    return is_red(r, g, b) or is_blue(r, g, b)


def parse_region(text):
    parts = [int(value) for value in text.split(",")]
    if len(parts) != 4:
        raise SystemExit("region wants x0,y0,x1,y1: " + text)
    return parts


def in_region(region, x, y):
    x0, y0, x1, y1 = region
    return x0 <= x <= x1 and y0 <= y <= y1


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    mode = argv[1]
    args = {}
    for item in argv[2:]:
        if "=" not in item:
            raise SystemExit("want key=value, got " + item)
        key, value = item.split("=", 1)
        args[key] = value

    if mode == "noninterference":
        on = Frame(args["on"])
        off = Frame(args["off"])
        # `regions_file=` exists because the region list grows with the number of
        # drawn silhouettes (14 on the tested tick) and passing them on argv hit
        # `Argument list too long` inside the Windows shim -- the assertion then
        # read `diff_outside=<none>` and looked like a product failure. A file has
        # no length limit, and the reader still takes the regions from the product,
        # never from a search of the frame.
        if "regions_file" in args:
            with open(args["regions_file"], "r", encoding="utf-8") as handle:
                raw = handle.read().strip()
        else:
            raw = args.get("regions", "")
        regions = [parse_region(value) for value in raw.split(";") if value]
        outside = 0
        inside = 0
        for y in range(on.height):
            for x in range(on.width):
                if on.same(off, x, y):
                    continue
                if any(in_region(region, x, y) for region in regions):
                    inside += 1
                else:
                    outside += 1
        print("diff_outside=%d diff_inside=%d regions=%d" % (outside, inside, len(regions)))
        # require_inside=0 is for the determinism call, which compares two
        # runs of the SAME state and therefore has no drawn region to find.
        want_inside = args.get("require_inside", "1") != "0"
        ok = outside == 0 and (inside > 0 or not want_inside)
        print("VERDICT %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    if mode == "team":
        frame = Frame(args["frame"])
        red_region = parse_region(args["red_region"])
        blue_region = parse_region(args["blue_region"])
        red_inside = blue_inside = red_wrong = blue_wrong = 0
        for y in range(frame.height):
            for x in range(frame.width):
                r, g, b = frame.rgb(x, y)
                red = is_red(r, g, b)
                blue = is_blue(r, g, b)
                if not red and not blue:
                    continue
                if in_region(red_region, x, y):
                    if red:
                        red_inside += 1
                    else:
                        blue_wrong += 1
                elif in_region(blue_region, x, y):
                    if blue:
                        blue_inside += 1
                    else:
                        red_wrong += 1
        print("red_inside=%d blue_inside=%d red_wrong_team=%d blue_wrong_team=%d" % (
            red_inside, blue_inside, red_wrong, blue_wrong))
        ok = red_inside >= int(args.get("min_red", "8")) and \
            blue_inside >= int(args.get("min_blue", "8")) and \
            red_wrong == 0 and blue_wrong == 0
        print("VERDICT %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    if mode == "teamcount":
        # A frame-wide count, because the per-region mode cannot say "this
        # frame has NO team-coloured pixel", which is exactly the toggle-off
        # reading mutation m3 has to move.
        frame = Frame(args["frame"])
        red_total = blue_total = 0
        for y in range(frame.height):
            for x in range(frame.width):
                r, g, b = frame.rgb(x, y)
                if is_red(r, g, b):
                    red_total += 1
                elif is_blue(r, g, b):
                    blue_total += 1
        print("red_total=%d blue_total=%d" % (red_total, blue_total))
        ok = red_total + blue_total >= int(args.get("min_total", "0"))
        print("VERDICT %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    if mode == "depth":
        xray = Frame(args["xray"])
        tested = Frame(args["tested"])
        off = Frame(args["off"])
        region = parse_region(args["region"])
        occluded_xray = occluded_tested = tested_diff = 0
        for y in range(region[1], region[3] + 1):
            for x in range(region[0], region[2] + 1):
                r, g, b = xray.rgb(x, y)
                if is_team_coloured(r, g, b):
                    occluded_xray += 1
                r, g, b = tested.rgb(x, y)
                if is_team_coloured(r, g, b):
                    occluded_tested += 1
                if not tested.same(off, x, y):
                    tested_diff += 1
        print("occluded_xray=%d occluded_tested=%d occluded_tested_diff=%d region=%dx%d" % (
            occluded_xray, occluded_tested, tested_diff,
            region[2] - region[0] + 1, region[3] - region[1] + 1))
        ok = occluded_xray >= int(args.get("min_xray", "8")) and \
            occluded_tested == 0 and tested_diff == 0
        print("VERDICT %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    raise SystemExit("unknown mode: " + mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
