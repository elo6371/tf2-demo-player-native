#!/usr/bin/env python3
"""bmp-diff-mask.py A.bmp B.bmp --mask C.bmp D.bmp [--mask E.bmp F.bmp]... [--max-outside N] [--min-changed N]

WHY THIS EXISTS
  "The material change is limited to the weapon body" is not a statement about a
  bounding box. A box large enough to contain the weapon also contains parts of
  the arms and the world behind them, so a difference inside the box may be a
  hand changing colour. What the claim needs is the weapon's own COVERAGE as the
  mask: every pixel where the weapon body is drawn.

  That mask is measurable from two frames taken at the same tick from the same
  executable -- the body textured, and the body switched off. A pixel where those
  two differ is a pixel the body covers (at least one of the two draws put
  something there that the other did not). The flat-colour frame gets its own
  pair (`--mask` may be given more than once; the masks are unioned), because a
  pixel where the textured body happens to match the frame behind it is still a
  body pixel the flat control can change. With both pairs the mask covers the
  union of the two weapon draws, which is what makes the containment test
  one-sided-safe.

  So the report is:
    changed        pixels where A and B differ (the material claim)
    mask           pixels covered by C/D (the body's own coverage)
    outside        changed pixels the mask does NOT cover -- must be 0
  `outside=0` is the statement that no hand, sleeve, or world pixel moved.

Both frames must have identical geometry. Exit codes:
  0  every given bound held
  1  a bound failed
  2  the input could not be read (missing file, not a BMP, different geometry)

Usage:
  bmp-diff-mask.py A.bmp B.bmp --mask C.bmp D.bmp --mask E.bmp F.bmp
                                [--max-outside N] [--min-changed N]
"""
import struct
import sys

EXIT_PASS, EXIT_FAIL, EXIT_ERROR = 0, 1, 2


def read_frame(path):
    """Return (pixel_bytes, width, height, bytes_per_pixel, bottom_up)."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as error:
        raise ValueError("cannot read %s: %s" % (path, error))
    if len(data) < 54:
        raise ValueError("%s is shorter than a BMP header" % path)
    if data[0:2] != b"BM":
        raise ValueError("%s does not start with a BMP signature" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if bits not in (24, 32):
        raise ValueError("%s is %d bits per pixel, not 24 or 32" % (path, bits))
    if compression != 0:
        raise ValueError("%s uses compression %d, not BI_RGB" % (path, compression))
    if width <= 0 or height <= 0:
        raise ValueError("%s has a non-positive extent %dx%d" % (path, width, height))
    if offset > len(data):
        raise ValueError("%s declares a pixel offset past the end of the file" % path)
    return data[offset:], width, height, bits // 8, height > 0


def main(argv):
    args = argv[1:]
    max_outside = 0
    min_changed = None
    positional = []
    mask_pairs = []
    index = 0
    while index < len(args):
        token = args[index]
        if token == "--mask":
            if index + 2 >= len(args):
                print("BMP-MASK status=ERROR reason=--mask wants two frame paths")
                return EXIT_ERROR
            mask_pairs.append((args[index + 1], args[index + 2]))
            index += 3
            continue
        if token in ("--max-outside", "--min-changed"):
            if index + 1 >= len(args):
                print("BMP-MASK status=ERROR reason=%s needs a value" % token)
                return EXIT_ERROR
            try:
                value = int(args[index + 1])
            except ValueError:
                print("BMP-MASK status=ERROR reason=%s is not an integer" % token)
                return EXIT_ERROR
            if token == "--max-outside":
                max_outside = value
            else:
                min_changed = value
            index += 2
            continue
        positional.append(token)
        index += 1

    if len(positional) != 2 or not mask_pairs:
        print("BMP-MASK status=ERROR reason=expected A.bmp B.bmp --mask C.bmp D.bmp")
        return EXIT_ERROR

    try:
        first, width, height, bpp, first_flip = read_frame(positional[0])
        second, sw, sh, sbpp, _ = read_frame(positional[1])
        mask_frames = [(read_frame(a), read_frame(b)) for a, b in mask_pairs]
    except ValueError as error:
        print("BMP-MASK status=ERROR reason=%s" % error)
        return EXIT_ERROR

    if (width, height, bpp) != (sw, sh, sbpp):
        print("BMP-MASK status=ERROR reason=geometry differs between A and B")
        return EXIT_ERROR
    for (left, right) in mask_frames:
        (lpixels, mw, mh, mbpp, _), (rpixels, nw, nh, nbpp, _) = left, right
        if (width, height, bpp) != (mw, mh, mbpp) or (width, height, bpp) != (nw, nh, nbpp):
            print("BMP-MASK status=ERROR reason=geometry differs between the pair and the mask")
            return EXIT_ERROR
        if len(first) != len(lpixels) or len(first) != len(rpixels):
            print("BMP-MASK status=ERROR reason=pixel data length differs")
            return EXIT_ERROR
    if len(first) != len(second):
        print("BMP-MASK status=ERROR reason=pixel data length differs")
        return EXIT_ERROR

    stride = width * bpp
    changed = 0
    inside = 0
    outside = 0
    mask_pixels = 0
    min_x, min_y, max_x, max_y = width, height, -1, -1
    sample = bpp  # compare whole pixels, not channels

    for row in range(height):
        # Row `row` is in top-left origin coordinates, which is what the mask
        # and the bbox report use. BMP stores bottom-up unless the height is
        # negative, so the byte row is mirrored for the usual case.
        source_row = (height - 1 - row) if first_flip else row
        start = source_row * stride
        row_a = first[start:start + stride]
        row_b = second[start:start + stride]
        mask_rows = []
        any_mask = False
        for (left, right) in mask_frames:
            left_row = left[0][start:start + stride]
            right_row = right[0][start:start + stride]
            differs = left_row != right_row
            mask_rows.append((left_row, right_row, differs))
            if differs:
                any_mask = True
        if row_a == row_b and not any_mask:
            continue
        for column in range(0, stride, sample):
            end = column + sample
            a = row_a[column:end]
            b = row_b[column:end]
            is_changed = a != b
            in_mask = False
            for (left_row, right_row, differs) in mask_rows:
                if differs and left_row[column:end] != right_row[column:end]:
                    in_mask = True
                    break
            if in_mask:
                mask_pixels += 1
            if not is_changed:
                continue
            changed += 1
            if in_mask:
                inside += 1
            else:
                outside += 1
            x = column // bpp
            if x < min_x:
                min_x = x
            if x > max_x:
                max_x = x
            if row < min_y:
                min_y = row
            if row > max_y:
                max_y = row

    if changed:
        bbox = "bbox=(x=%d..%d, y=%d..%d) w=%d h=%d" % (
            min_x, max_x, min_y, max_y, max_x - min_x + 1, max_y - min_y + 1)
    else:
        bbox = "bbox=none"

    reasons = []
    if outside > max_outside:
        reasons.append("outside=%d exceeds %d" % (outside, max_outside))
    if min_changed is not None and changed < min_changed:
        reasons.append("changed=%d is below %d" % (changed, min_changed))
    verdict = "FAIL" if reasons else "PASS"
    suffix = (" status=%s reason=%s" % (verdict, "; ".join(reasons))) if reasons else " status=%s" % verdict
    print("BMP-MASK changed=%d inside=%d outside=%d mask_pixels=%d %s%s" % (
        changed, inside, outside, mask_pixels, bbox, suffix))
    return EXIT_PASS if not reasons else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main(sys.argv))
