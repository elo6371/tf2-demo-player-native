#!/usr/bin/env python3
"""bmp-pixel-diff.py -- compare two captured frames and apply a threshold.

Why this exists
---------------
`render-output-hashable-frame-grab` gives the product a way to hand out a frame,
but a frame *hash* can only answer "are these two bytes the same?". Two questions
this project needs cannot be asked that way:

  * "does enabling the skinning branch with identity matrices leave the bind pose
    alone?" -- the answer is yes, but NOT bit-exactly: the shader's weighted sum
    is a different float expression than the passthrough, so a handful of bytes
    differ by 1. A hash calls that a difference; it is not a deformation.
  * "did the pose actually move?" -- the answer must be a large, quantified
    difference, not merely "not identical".

So this compares pixel data and reports the size of the difference, and the
caller states the bound it wants:
  --max-delta N           no channel may differ by more than N
  --max-diff-percent P    at most P% of bytes may differ
  --min-diff-percent P    at least P% of bytes must differ
  --min-delta N           at least one channel must differ by N or more
  --region x,y,w,h        compare only that rectangle, in TOP-LEFT origin
                          pixels (a rect may be given more than once; each is
                          compared and reported on its own line)
Any bound that is given is checked; every bound must hold.

`--region` exists because "the picture changed" is not the claim the ViewModel
material work makes. The claim is "the region the ViewModel covers changed, and
the rest of the frame did not" -- and a whole-frame diff cannot tell those
apart. Without it, a run whose *world* was nondeterministic would pass a
whole-frame bound and the pass would be credited with a difference it did not
cause. Every region is reported with the share of the frame's differences that
falls inside it, so "the difference is where the ViewModel is" is a number and
not an impression.

`--min-delta` is the one that separates "the pose moved" from "a few bytes
rounded differently": a large single-channel difference cannot come from a
float expression being reassociated, only from geometry landing somewhere else.

The bounds are the caller's claim, not this tool's opinion: a missing bound is
not a pass, it is an unchecked property. Exit codes:
  0  every given bound held
  1  a bound failed
  2  the input could not be read as a comparable frame (missing file, not a BMP,
     unsupported bit depth, or different pixel geometry) -- a failure, never a
     silent skip.

Usage:
  bmp-pixel-diff.py A.bmp B.bmp [--max-delta N] [--max-diff-percent P]
                                [--min-diff-percent P] [--min-delta N]
"""
import struct
import sys

EXIT_PASS, EXIT_FAIL, EXIT_ERROR = 0, 1, 2


def read_frame(path):
    """Return (bytes, width, height, bits_per_pixel) or raise ValueError."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as error:
        raise ValueError("cannot read %s: %s" % (path, error))
    if len(data) < 54:
        raise ValueError("%s is shorter than a BMP header" % path)
    if data[0:2] != b"BM":
        raise ValueError("%s does not start with a BMP signature" % path)
    data_offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if bits not in (24, 32):
        raise ValueError("%s is %d bits per pixel, not 24 or 32" % (path, bits))
    if compression != 0:
        raise ValueError("%s uses compression %d, not BI_RGB" % (path, compression))
    if width <= 0 or height <= 0:
        raise ValueError("%s has a non-positive extent %dx%d" % (path, width, height))
    if data_offset > len(data):
        raise ValueError("%s declares a pixel offset past the end of the file" % path)
    return data[data_offset:], width, abs(height), bits


def main(argv):
    args = argv[1:]
    bounds = {"max_delta": None, "max_diff_percent": None, "min_diff_percent": None,
              "min_delta": None}
    regions = []
    positional = []
    index = 0
    while index < len(args):
        token = args[index]
        if token == "--region":
            if index + 1 >= len(args):
                print("BMP-DIFF status=ERROR reason=--region needs a value")
                return EXIT_ERROR
            parts = args[index + 1].split(",")
            if len(parts) != 4:
                print("BMP-DIFF status=ERROR reason=--region wants x,y,w,h")
                return EXIT_ERROR
            try:
                region = tuple(int(part) for part in parts)
            except ValueError:
                print("BMP-DIFF status=ERROR reason=--region %s is not four integers"
                      % args[index + 1])
                return EXIT_ERROR
            if region[2] <= 0 or region[3] <= 0:
                print("BMP-DIFF status=ERROR reason=--region %s has a non-positive extent"
                      % args[index + 1])
                return EXIT_ERROR
            regions.append(region)
            index += 2
            continue
        if token in ("--max-delta", "--max-diff-percent", "--min-diff-percent", "--min-delta"):
            if index + 1 >= len(args):
                print("BMP-DIFF status=ERROR reason=%s needs a value" % token)
                return EXIT_ERROR
            key = token[2:].replace("-", "_")
            try:
                bounds[key] = float(args[index + 1])
            except ValueError:
                print("BMP-DIFF status=ERROR reason=%s is not a number" % token)
                return EXIT_ERROR
            index += 2
            continue
        positional.append(token)
        index += 1

    if len(positional) != 2:
        print("BMP-DIFF status=ERROR reason=expected exactly two frame paths")
        return EXIT_ERROR
    if all(bounds[key] is None for key in bounds):
        # No bound means nothing was checked. Saying PASS here would let a caller
        # believe a property was verified when it never was.
        print("BMP-DIFF status=ERROR reason=no bound given; nothing would be checked")
        return EXIT_ERROR

    try:
        first, first_width, first_height, first_bits = read_frame(positional[0])
        second, second_width, second_height, second_bits = read_frame(positional[1])
    except ValueError as error:
        print("BMP-DIFF status=ERROR reason=%s" % error)
        return EXIT_ERROR

    if (first_width, first_height, first_bits) != (second_width, second_height, second_bits):
        print("BMP-DIFF status=ERROR reason=geometry differs: %dx%dx%d vs %dx%dx%d" % (
            first_width, first_height, first_bits, second_width, second_height, second_bits))
        return EXIT_ERROR
    if len(first) != len(second):
        print("BMP-DIFF status=ERROR reason=pixel data length differs: %d vs %d" % (
            len(first), len(second)))
        return EXIT_ERROR

    total = len(first)
    differing = 0
    max_delta = 0
    for index in range(total):
        delta = abs(first[index] - second[index])
        if delta:
            differing += 1
            if delta > max_delta:
                max_delta = delta

    # Every region is measured against the SAME bounds, and each reports its own
    # share of the frame-wide difference count. The share is what turns "the
    # difference is inside the ViewModel's screen rect" into something a caller
    # can assert, and the complement -- "this region has 0 differences" -- is the
    # control that a nondeterministic world would redden.
    examined = [(None, differing, total, max_delta)] if not regions else []
    if regions:
        stride = first_width * (first_bits // 8)
        for x, y, w, h in regions:
            if x < 0 or y < 0 or x + w > first_width or y + h > first_height:
                print("BMP-DIFF status=ERROR reason=region %d,%d,%d,%d is outside the %dx%d frame"
                      % (x, y, w, h, first_width, first_height))
                return EXIT_ERROR
            region_differing = 0
            region_max_delta = 0
            for row in range(y, y + h):
                # BMP rows are bottom-up; the caller writes top-left origin.
                bottom_up = first_height - 1 - row
                start = bottom_up * stride + x * (first_bits // 8)
                for offset in range(start, start + w * (first_bits // 8)):
                    delta = abs(first[offset] - second[offset])
                    if delta:
                        region_differing += 1
                        if delta > region_max_delta:
                            region_max_delta = delta
            examined.append(((x, y, w, h), region_differing,
                             w * h * (first_bits // 8), region_max_delta))

    reasons = []
    lines = []
    if not regions:
        # The whole-frame form is kept byte-identical to what it was before
        # --region existed: the gates that already call this tool print its line
        # verbatim, and a format change would have shown up as their diff, not as
        # this tool's.
        lines.append("differing=%d total=%d percent=%.4f max_delta=%d extent=%dx%dx%d"
                     % (differing, total, 100.0 * differing / total if total else 0.0,
                        max_delta, first_width, first_height, first_bits))
    else:
        lines.append("whole differing=%d total=%d percent=%.4f max_delta=%d extent=%dx%dx%d"
                     % (differing, total, 100.0 * differing / total if total else 0.0,
                        max_delta, first_width, first_height, first_bits))
    for region, region_differing, region_total, region_max_delta in examined:
        percent = 100.0 * region_differing / region_total if region_total else 0.0
        if region is None:
            # The whole frame is a region too, and it must be checked like one.
            # Until 2026-10-12 it was not: this branch skipped every bound, so a
            # whole-frame call with --min-diff-percent 0.5 returned status=PASS
            # on a byte-identical pair. The mutation run that paired a
            # zero-difference capture with that bound went unchecked because of
            # this skip, not because the mutation could not move the reading --
            # a bound that cannot go red is not a bound.
            label = "whole frame"
        else:
            share = 100.0 * region_differing / differing if differing else 0.0
            lines.append("region=%d,%d,%d,%d differing=%d total=%d percent=%.4f max_delta=%d share=%.4f"
                         % (region + (region_differing, region_total, percent, region_max_delta, share)))
            label = "region %d,%d,%d,%d" % region
        if bounds["max_delta"] is not None and region_max_delta > bounds["max_delta"]:
            reasons.append("%s max_delta=%d exceeds %.0f"
                           % (label, region_max_delta, bounds["max_delta"]))
        if bounds["max_diff_percent"] is not None and percent > bounds["max_diff_percent"]:
            reasons.append("%s percent=%.4f exceeds %.4f"
                           % (label, percent, bounds["max_diff_percent"]))
        if bounds["min_diff_percent"] is not None and percent < bounds["min_diff_percent"]:
            reasons.append("%s percent=%.4f is below %.4f"
                           % (label, percent, bounds["min_diff_percent"]))
        if bounds["min_delta"] is not None and region_max_delta < bounds["min_delta"]:
            reasons.append("%s max_delta=%d is below %.0f"
                           % (label, region_max_delta, bounds["min_delta"]))

    suffix = (" status=%s%s" % ("FAIL" if reasons else "PASS",
                                (" reason=" + "; ".join(reasons)) if reasons else ""))
    if not regions:
        print("BMP-DIFF " + lines[0] + suffix)
    else:
        print("BMP-DIFF " + lines[0])
        for line in lines[1:]:
            print("BMP-DIFF " + line)
        print("BMP-DIFF" + suffix)
    return EXIT_FAIL if reasons else EXIT_PASS


if __name__ == "__main__":
    sys.exit(main(sys.argv))
