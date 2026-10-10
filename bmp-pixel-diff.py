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
Any bound that is given is checked; every bound must hold.

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
    positional = []
    index = 0
    while index < len(args):
        token = args[index]
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
    percent = 100.0 * differing / total if total else 0.0

    reasons = []
    if bounds["max_delta"] is not None and max_delta > bounds["max_delta"]:
        reasons.append("max_delta=%d exceeds %.0f" % (max_delta, bounds["max_delta"]))
    if bounds["max_diff_percent"] is not None and percent > bounds["max_diff_percent"]:
        reasons.append("diff_percent=%.4f exceeds %.4f" % (percent, bounds["max_diff_percent"]))
    if bounds["min_diff_percent"] is not None and percent < bounds["min_diff_percent"]:
        reasons.append("diff_percent=%.4f is below %.4f" % (percent, bounds["min_diff_percent"]))
    if bounds["min_delta"] is not None and max_delta < bounds["min_delta"]:
        reasons.append("max_delta=%d is below %.0f" % (max_delta, bounds["min_delta"]))

    print("BMP-DIFF differing=%d total=%d percent=%.4f max_delta=%d extent=%dx%dx%d status=%s%s" % (
        differing, total, percent, max_delta, first_width, first_height, first_bits,
        "FAIL" if reasons else "PASS",
        (" reason=" + "; ".join(reasons)) if reasons else ""))
    return EXIT_FAIL if reasons else EXIT_PASS


if __name__ == "__main__":
    sys.exit(main(sys.argv))
