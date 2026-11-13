#!/usr/bin/env python3
"""world-empty-oracle.py -- T62: explain the empty-pixel classes with the point oracle.

WHY THIS FILE EXISTS
  world-empty-lattice.py names every empty pixel of a captured frame by set
  algebra: either the frame draws nothing along that ray even with both
  restrictors off (open sky), or one of the restrictors took a triangle away
  (a removal). That classification never looks at a triangle. The product own
  point oracle, --world-hole-trace, does: for a screen point it reports the
  world triangle the ray hits with the cull and the PVS clip applied (drawn=),
  the triangle it would hit with both off (any=), and the BSP sky triangle
  (sky=).

  This file joins the two accounts. A sampled open-sky pixel must have no drawn
  triangle. A sampled restrictor removal must have no drawn triangle but must
  have an any= triangle -- that triangle is the thing the restrictor took away.
  Either class holding the other shape is a contradiction inside the product own
  account, which is a defect the set algebra alone cannot see.

WHY THE FRAME IS NOT RE-READ
  The oracle and the pixels come from the same product run: --world-hole-trace
  does not touch the frame (proved by capturing the same pose with and without
  it and comparing the BMP hashes, 0c748941...d6f5 both). The gate asserts that
  byte identity itself, so this file only has to read the two text accounts.

WHAT IT DOES NOT CLAIM
  It explains the sampled pixels, not the population: the oracle costs about a
  second per point, so the gate samples a handful of component centroids rather
  than every empty pixel. A verdict of ok on N points says the classes mean what
  the algebra says they mean; it is not a per-pixel hole detector.

Usage: python world-empty-oracle.py --lattice <empty-lattice.txt> --trace <oracle.trace>
Exit:  0 = every sampled pixel was explained; 1 = one was not; 2 = an input is missing.
"""

import argparse
import sys


def read_points(path):
    """The oracle verdicts of a trace, keyed by screen point.

    The product prints one line per point:
      <time> gpu: world hole-trace point=480,120 drawn=- any=- sky=face=13388 ...
    Token splitting is used rather than one regex because the describe() tail is
    free text ("material=TOOLS/TOOLSSKYBOX tri=823 depth=0.9980") and a pattern
    spanning it is one edit away from silently matching nothing.
    """
    points = {}
    with open(path, "rb") as handle:
        for raw in handle:
            line = raw.decode("utf-8", "replace")
            if "hole-trace point=" not in line:
                continue
            tokens = line.split("hole-trace point=", 1)[1].split()
            if not tokens:
                continue
            where = tokens[0].split(",")
            if len(where) != 2 or not where[0].isdigit() or not where[1].isdigit():
                continue
            fields = {}
            for token in tokens[1:]:
                key, _, value = token.partition("=")
                if key in ("drawn", "any", "sky") and key not in fields:
                    fields[key] = value
            if len(fields) != 3:
                continue
            points[(int(where[0]), int(where[1]))] = (
                fields["drawn"], fields["any"], fields["sky"])
    return points


def read_samples(path):
    """The SAMPLE lines of an empty-lattice run: (x, y, base, alloff)."""
    samples = []
    with open(path, "r") as handle:
        for line in handle:
            fields = line.split()
            if len(fields) < 5 or fields[0] != "SAMPLE":
                continue
            try:
                x = int(fields[1])
                y = int(fields[2])
            except ValueError:
                continue
            base = fields[3].split("=", 1)[-1]
            alloff = fields[4].split("=", 1)[-1]
            samples.append((x, y, base, alloff))
    return samples


def classify(base, alloff):
    """The class the algebra put this pixel in, or None when it is not empty."""
    if base != "empty":
        return None
    return "open" if alloff == "empty" else "removed"


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--lattice", required=True)
    parser.add_argument("--trace", required=True)
    args = parser.parse_args(argv)

    try:
        samples = read_samples(args.lattice)
        points = read_points(args.trace)
    except OSError as error:
        print("ORACLE status=missing %s" % error)
        return 2

    judged = 0
    failed = 0
    empty = 0
    for x, y, base, alloff in samples:
        kind = classify(base, alloff)
        if kind is None:
            continue
        empty += 1
        seen = points.get((x, y))
        if seen is None:
            print("ORACLE point=%d,%d class=%s verdict=missing reason=no-oracle-line"
                  % (x, y, kind))
            failed += 1
            continue
        drawn, anyface, sky = seen
        if kind == "open":
            # Nothing may be on the ray at all. The pixel is empty even in the
            # frame with both restrictors off, so if any triangle covered it the
            # rasteriser would have drawn it -- there is no nearer pixel to hide
            # behind (empty means no triangle drew in this pixel).
            want = drawn == "-" and anyface == "-"
            reason = "an open-sky pixel must have no world triangle along the ray"
        else:
            # The pixel is empty with both restrictors on and drawn with them
            # off, so the oracle must find the triangle that was there.
            # The triangle a restrictor took away may be a world triangle or a
            # BSP sky triangle: with --world-flat --no-world-sky a sky brush
            # paints a flat colour of its own on some maps (koth_bagel_rc11
            # paints it over the whole left edge), so a restrictor can take a
            # sky-brush pixel away exactly as it takes a world one. Sky is not
            # constrained in the open class for the mirror reason: a sky
            # triangle can paint nothing at all on a map whose sky brush is
            # never drawn in this mode.
            want = drawn == "-" and (anyface != "-" or sky != "-")
            reason = "a removed pixel must have a triangle no restrictor draws"
        verdict = "ok" if want else "contradiction"
        print("ORACLE point=%d,%d class=%s drawn=%s any=%s sky=%s verdict=%s"
              % (x, y, kind, drawn, anyface, sky, verdict))
        judged += 1
        if not want:
            print("ORACLE failed point=%d,%d %s" % (x, y, reason))
            failed += 1

    print("ORACLE points=%d emptySamples=%d judged=%d failed=%d"
          % (len(samples), empty, judged, failed))
    if empty == 0:
        print("ORACLE verdict=vacuous no empty pixel was sampled")
        return 1
    if failed:
        print("ORACLE verdict=fail")
        return 1
    print("ORACLE verdict=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
