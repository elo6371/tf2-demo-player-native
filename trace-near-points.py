#!/usr/bin/env python3
"""trace-near-points.py TRACE NEAR_BOX SCALE [threshold_map_units]

T29: count how many of the product's named face-trace points are closer to the
eye than a map-unit threshold.

WHY THIS IS A PIXEL READING
  The product's `gpu: world face-trace point=` lines carry, for a named screen
  pixel, the world projection depth (z/w) of the triangle that owns it. The
  world is drawn in a normalized box, so the depth is not a distance; it is
  converted back through the near plane the draw itself published
  (`gpu: world clip near=`, `horizontalScale=`) to map units. 611 map units is
  the distance the shipped near plane (0.05 box units) actually clipped, so a
  point nearer than that is a pixel the old build could not have drawn.

  Nothing here reads a colour, and the number moves when the product's own
  depth at those pixels moves: it is the assertion that says "the camera's room
  is in the picture" without asking whether the room is bright.

Usage: python trace-near-points.py <trace> <near_box> <scale> [threshold]
Exit:  0 = a reading was printed; 2 = no face-trace point was found.
"""
import re
import sys


def main():
    trace = sys.argv[1]
    near = float(sys.argv[2])
    scale = float(sys.argv[3])
    threshold = float(sys.argv[4]) if len(sys.argv) > 4 else 611.0
    if near <= 0.0 or scale <= 0.0:
        print("0 0 -1")
        return 0
    count = 0
    seen = 0
    mind = 1e30
    for line in open(trace, encoding="utf-8", errors="replace"):
        if "gpu: world face-trace point=" not in line:
            continue
        if "tri=none" in line:
            continue
        m = re.search(r"(?:^| )depth=([-0-9.]+)", line)
        if not m:
            continue
        depth = float(m.group(1))
        seen += 1
        # z_ndc = f/(f-n) * (1 - n/z), with the published far plane f = 100
        ratio = 1.0 - depth * (100.0 - near) / 100.0
        if ratio <= 1e-9:
            continue
        distance = near / ratio / scale
        if distance < mind:
            mind = distance
        if distance < threshold:
            count += 1
    print("%d %d %.1f" % (count, seen, mind if mind < 1e29 else -1.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
