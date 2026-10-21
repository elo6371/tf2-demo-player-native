#!/usr/bin/env python3
"""bmp-silhouette-diff.py -- a frame pair may differ ONLY on its measured edges.

WHY THIS EXISTS
  The identity-skinning pair (weapon-animation-check.sh section 4) is one product
  invocation with --model-skinning 1 against one with --model-skinning 0. The
  claim is "enabling the skinning branch with identity bone matrices must not
  deform the bind pose". It is NOT a bit-exact claim, and the gate's own comment
  has always said so: the skinning shader evaluates sum(w_i * M_i * v) with
  normalized weights, a different float expression than the passthrough v, so a
  sample that sits exactly on a triangle boundary can be owned by the other
  triangle (or lose a depth tie) and take the neighbouring surface's colour. That
  is a raster edge; it is not a deformation. A whole-frame "max channel delta
  <= 1" cannot distinguish the two: an edge flip has the two surface colours'
  difference by construction (measured: 59 of 255 on this frame).

  So the claim is measured where it can be measured, and the measurement is
  reported in full:

    mask            pixels the rule below calls edges, from the reference frame
    changed         pixels where the pair differs
    inside          changed pixels the mask covers            -- must be all
    outside         changed pixels the mask does not cover    -- must be 0
    interior_max    max channel delta outside the mask        -- must be <= bound
    global_max      max channel delta anywhere                -- must be <= bound
    mask_share      mask / frame, printed so the mask is a population and not a
                    filter chosen to make the number green

  THE MASK RULE (derived from the frames, no hard-coded pixel list): a pixel is
  an edge pixel when its 4-neighbour maximum per-channel difference in the
  --edge-frame is at least --edge-threshold. On the weapon gate's control frame
  the 4-neighbour gradient population measures 823,210 px at 0, 2,813 at 1,
  18,446 at 32 or more and up to 222 at the strongest face boundary; the measured
  residual sits on a 59 gradient. The threshold is a stated parameter, not a
  natural break, and the population it selects is printed on every run so the
  mask is checkable rather than assumed. What the containment test buys is not
  that 32 is "the" edge threshold: it is that the only permitted change is a
  pixel the reference frame itself shows as locally contrasty, and that the
  count, interior strictness and global ceiling below bound how many and how
  large such changes may be.

  THE INTERIOR BOUND is the historic rounding allowance: a handful of bytes may
  round differently (<= 1), but nothing may move. It is strict and it is where a
  deformation would show, because an interior pixel cannot change because of a
  raster tie.

  THE COUNT BOUND is what separates "one sample decided differently" from "the
  model moved": a rigid move of even a fraction of a pixel changes hundreds or
  thousands of edge pixels, and a pose change changes interior pixels as well.
  The bound is a small measured number (see the gate for the reading and why).

  THE GLOBAL CEILING is deliberately above the measured edge delta and well
  below the channel range: 59 of 255 is a surface boundary, while a change
  involving a bright surface or a different material lands far above it. It
  exists so a large move cannot hide inside the mask.

  THE MASK BOUNDS keep the containment test from being vacuous: an empty mask
  would pass every pair, a whole-frame mask would pass every pair too. The
  measured population is printed with the bounds that were checked.

Usage:
  bmp-silhouette-diff.py A.bmp B.bmp --edge-frame F.bmp
      [--edge-threshold N] [--max-interior-delta N] [--max-changed N]
      [--max-global-delta N] [--min-mask N] [--max-mask N]

  At least one bound must be given: a comparison with no bound is an unchecked
  property, and printing PASS for it would be a lie (the same rule
  bmp-pixel-diff.py follows). Exit codes:
    0  every given bound held
    1  a bound failed
    2  the input could not be read as a comparable frame
"""
import struct
import sys

EXIT_PASS, EXIT_FAIL, EXIT_ERROR = 0, 1, 2


def read_frame(path):
    """Return (pixel_bytes, width, height, bytes_per_pixel, top_down)."""
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
    return data[offset:], width, abs(height), bits // 8, height < 0


def edge_mask(pixels, width, height, bpp, threshold):
    """Pixels whose 4-neighbour maximum per-channel difference >= threshold.

    The mask is indexed by STORED row order, which is the order every reader of
    this tool walks the pixel bytes in; a BMP's bottom-up storage is a display
    coordinate question and adjacency is unchanged by it. The neighbourhood is
    the four edge-sharing neighbours, so a diagonal boundary that crosses a
    single sample marks that sample, which is exactly the shape this rule is for.
    """
    stride = width * bpp
    mask = bytearray(width * height)
    for stored in range(height):
        base = stored * stride
        row = pixels[base:base + stride]
        for x in range(width):
            offset = x * bpp
            here = row[offset:offset + bpp]
            best = 0
            if x > 0:
                left = row[offset - bpp:offset]
                for channel in range(bpp):
                    delta = here[channel] - left[channel]
                    if delta < 0:
                        delta = -delta
                    if delta > best:
                        best = delta
            if x + 1 < width:
                right = row[offset + bpp:offset + 2 * bpp]
                for channel in range(bpp):
                    delta = here[channel] - right[channel]
                    if delta < 0:
                        delta = -delta
                    if delta > best:
                        best = delta
            if best < threshold:
                for dy in (-1, 1):
                    adjacent = stored + dy
                    if 0 <= adjacent < height:
                        other = pixels[adjacent * stride + offset:adjacent * stride + offset + bpp]
                        for channel in range(bpp):
                            delta = here[channel] - other[channel]
                            if delta < 0:
                                delta = -delta
                            if delta > best:
                                best = delta
            if best >= threshold:
                mask[stored * width + x] = 1
    return mask


def main(argv):
    args = argv[1:]
    bounds = {
        "edge_threshold": None,
        "max_interior_delta": None,
        "max_changed": None,
        "max_global_delta": None,
        "min_mask": None,
        "max_mask": None,
    }
    edge_frame = None
    positional = []
    index = 0
    while index < len(args):
        token = args[index]
        if token == "--edge-frame":
            if index + 1 >= len(args):
                print("BMP-SILHOUETTE status=ERROR reason=--edge-frame needs a value")
                return EXIT_ERROR
            edge_frame = args[index + 1]
            index += 2
            continue
        key = token[2:].replace("-", "_") if token.startswith("--") else None
        if key in bounds:
            if index + 1 >= len(args):
                print("BMP-SILHOUETTE status=ERROR reason=%s needs a value" % token)
                return EXIT_ERROR
            try:
                bounds[key] = int(args[index + 1])
            except ValueError:
                print("BMP-SILHOUETTE status=ERROR reason=%s is not an integer" % token)
                return EXIT_ERROR
            index += 2
            continue
        positional.append(token)
        index += 1

    if len(positional) != 2:
        print("BMP-SILHOUETTE status=ERROR reason=expected exactly two frame paths")
        return EXIT_ERROR
    if edge_frame is None:
        print("BMP-SILHOUETTE status=ERROR reason=--edge-frame is required; the mask must be derived from a stated frame")
        return EXIT_ERROR
    if all(bounds[key] is None for key in bounds if key != "edge_threshold"):
        print("BMP-SILHOUETTE status=ERROR reason=no bound given; nothing would be checked")
        return EXIT_ERROR
    threshold = bounds["edge_threshold"] if bounds["edge_threshold"] is not None else 32

    try:
        first, width, height, bpp, first_top_down = read_frame(positional[0])
        second, sw, sh, sbpp, _ = read_frame(positional[1])
        reference, rw, rh, rbpp, reference_top_down = read_frame(edge_frame)
    except ValueError as error:
        print("BMP-SILHOUETTE status=ERROR reason=%s" % error)
        return EXIT_ERROR

    if (width, height, bpp) != (sw, sh, sbpp) or (width, height, bpp) != (rw, rh, rbpp):
        print("BMP-SILHOUETTE status=ERROR reason=geometry differs: %dx%dx%d vs %dx%dx%d vs %dx%dx%d"
              % (width, height, bpp, sw, sh, sbpp, rw, rh, rbpp))
        return EXIT_ERROR
    if len(first) != len(second) or len(first) != len(reference):
        print("BMP-SILHOUETTE status=ERROR reason=pixel data length differs")
        return EXIT_ERROR

    try:
        mask = edge_mask(reference, width, height, bpp, threshold)
    except ValueError as error:
        print("BMP-SILHOUETTE status=ERROR reason=%s" % error)
        return EXIT_ERROR

    stride = width * bpp
    total_pixels = width * height
    mask_pixels = 0
    changed = 0
    changed_bytes = 0
    inside = 0
    outside = 0
    interior_max = 0
    global_max = 0
    for y in range(height):
        base = y * stride
        row_a = first[base:base + stride]
        row_b = second[base:base + stride]
        for x in range(width):
            offset = x * bpp
            in_mask = mask[y * width + x]
            if in_mask:
                mask_pixels += 1
            pixel_delta = 0
            for channel in range(bpp):
                delta = row_a[offset + channel] - row_b[offset + channel]
                if delta < 0:
                    delta = -delta
                if delta:
                    changed_bytes += 1
                if delta > pixel_delta:
                    pixel_delta = delta
            if pixel_delta:
                changed += 1
                if pixel_delta > global_max:
                    global_max = pixel_delta
                if in_mask:
                    inside += 1
                else:
                    outside += 1
            if not in_mask and pixel_delta > interior_max:
                interior_max = pixel_delta

    mask_share = 100.0 * mask_pixels / total_pixels if total_pixels else 0.0
    reasons = []
    if bounds["max_interior_delta"] is not None and interior_max > bounds["max_interior_delta"]:
        reasons.append("interior_max=%d exceeds %d" % (interior_max, bounds["max_interior_delta"]))
    if bounds["max_changed"] is not None and changed > bounds["max_changed"]:
        reasons.append("changed=%d exceeds %d" % (changed, bounds["max_changed"]))
    if bounds["max_global_delta"] is not None and global_max > bounds["max_global_delta"]:
        reasons.append("global_max=%d exceeds %d" % (global_max, bounds["max_global_delta"]))
    if bounds["min_mask"] is not None and mask_pixels < bounds["min_mask"]:
        reasons.append("mask_pixels=%d is below %d" % (mask_pixels, bounds["min_mask"]))
    if bounds["max_mask"] is not None and mask_pixels > bounds["max_mask"]:
        reasons.append("mask_pixels=%d exceeds %d" % (mask_pixels, bounds["max_mask"]))
    # Containment is a bound too: no changed pixel may sit outside the mask. It is
    # asserted by default rather than opted into, because "inside" without
    # "outside == 0" would let one uncontained change ride along.
    if outside != 0:
        reasons.append("outside=%d changed pixels are not on a measured edge" % outside)
    verdict = "FAIL" if reasons else "PASS"
    suffix = (" status=%s reason=%s" % (verdict, "; ".join(reasons))) if reasons else " status=%s" % verdict
    print("BMP-SILHOUETTE changed=%d changed_bytes=%d inside=%d outside=%d mask_pixels=%d "
          "mask_share=%.4f interior_pixels=%d interior_max=%d global_max=%d edge_threshold=%d "
          "extent=%dx%dx%d frame_pixels=%d%s"
          % (changed, changed_bytes, inside, outside, mask_pixels, mask_share,
             total_pixels - mask_pixels, interior_max, global_max, threshold,
             width, height, bpp * 8, total_pixels, suffix))
    return EXIT_PASS if not reasons else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main(sys.argv))
