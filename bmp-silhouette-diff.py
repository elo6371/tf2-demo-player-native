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

  INSPECTION MODE (T48): --inspect-pixel X,Y prints the raw neighbourhood of one
  sample instead of claiming anything -- the pixel and its four neighbours in
  both frames, the per-channel deltas, the 4-neighbour gradient and mask
  membership, and a value grid of the window -- because a pair that differs by
  one sample raises "which sample, on what edge, between which colours?", and
  that question must be answerable from the frames rather than from a re-run.
  An inspection-only run checks no property and so passes no bound; the rule
  output is unchanged and still requires one.

Usage:
  bmp-silhouette-diff.py A.bmp B.bmp --edge-frame F.bmp
      [--edge-threshold N] [--max-interior-delta N] [--max-changed N]
      [--max-global-delta N] [--min-mask N] [--max-mask N]
      [--inspect-pixel X,Y] [--inspect-window N]

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


DIRECTIONS = (("left", -1, 0), ("right", 1, 0), ("up", 0, -1), ("down", 0, 1))


def pixel_rgb(pixels, width, height, bpp, x, y, top_down):
    """Return (rgb, byte offset, stored row) for a display pixel (x, y).

    The rule above compares STORED bytes; this mode is the one place where the
    display coordinate is needed, so the bottom-up flip happens here and nowhere
    else. pixel_rgb is only meaningful when every frame shares an orientation,
    which main() checks before it calls in.
    """
    stored = y if top_down else height - 1 - y
    offset = stored * width * bpp + x * bpp
    return (pixels[offset + 2], pixels[offset + 1], pixels[offset]), offset, stored


def neighbour_gradient(pixels, width, height, bpp, x, y, top_down):
    """The same 4-neighbour maximum per-channel difference the mask uses."""
    here = pixel_rgb(pixels, width, height, bpp, x, y, top_down)[0]
    best = 0
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        nx, ny = x + dx, y + dy
        if nx < 0 or ny < 0 or nx >= width or ny >= height:
            continue
        other = pixel_rgb(pixels, width, height, bpp, nx, ny, top_down)[0]
        for channel in range(3):
            delta = here[channel] - other[channel]
            if delta < 0:
                delta = -delta
            if delta > best:
                best = delta
    return best


def inspect_sample(first, second, reference, width, height, bpp,
                   first_top_down, second_top_down, reference_top_down, mask,
                   threshold, x, y, window, label_a, label_b, label_edge):
    """Print the raw neighbourhood of one sample. It asserts nothing."""
    print("BMP-INSPECT pixel=%d,%d window=%d extent=%dx%dx%d frame_pixels=%d"
          % (x, y, window, width, height, bpp * 8, width * height))
    print("BMP-INSPECT A=%s B=%s edge=%s" % (label_a, label_b, label_edge))
    value_a, offset_a, stored_a = pixel_rgb(first, width, height, bpp, x, y, first_top_down)
    value_b, offset_b, stored_b = pixel_rgb(second, width, height, bpp, x, y, second_top_down)
    value_r, _, stored_r = pixel_rgb(reference, width, height, bpp, x, y, reference_top_down)
    delta = tuple(abs(p - q) for p, q in zip(value_a, value_b))
    print("BMP-INSPECT stored: A_row=%d A_data_offsets=%d..%d B_row=%d B_data_offsets=%d..%d edge_row=%d"
          % (stored_a, offset_a, offset_a + bpp - 1, stored_b, offset_b, offset_b + bpp - 1,
             stored_r))
    print("BMP-INSPECT values: A=%s B=%s edge=%s delta=%s max_channel=%d"
          % (value_a, value_b, value_r, delta, max(delta)))
    for name, dx, dy in DIRECTIONS:
        nx, ny = x + dx, y + dy
        if nx < 0 or ny < 0 or nx >= width or ny >= height:
            print("BMP-INSPECT neighbour %-5s outside the frame" % name)
            continue
        neighbour_a = pixel_rgb(first, width, height, bpp, nx, ny, first_top_down)[0]
        neighbour_b = pixel_rgb(second, width, height, bpp, nx, ny, second_top_down)[0]
        neighbour_delta = tuple(abs(p - q) for p, q in zip(neighbour_a, neighbour_b))
        print("BMP-INSPECT neighbour %-5s A=%s B=%s delta=%s"
              % (name, neighbour_a, neighbour_b, neighbour_delta))
    print("BMP-INSPECT gradient: A=%d B=%d edge=%d edge_threshold=%d mask_edge=%d"
          % (neighbour_gradient(first, width, height, bpp, x, y, first_top_down),
             neighbour_gradient(second, width, height, bpp, x, y, second_top_down),
             neighbour_gradient(reference, width, height, bpp, x, y, reference_top_down),
             threshold, mask[stored_r * width + x]))
    half = window // 2
    x0, x1 = max(0, x - half), min(width - 1, x + half)
    y0, y1 = max(0, y - half), min(height - 1, y + half)
    print("BMP-INSPECT grid_A_r: R channel of %s, rows y=%d..%d, columns x=%d..%d, '*' marks the sample"
          % (label_a, y0, y1, x0, x1))
    print("BMP-INSPECT grid_A_r header %s"
          % " ".join("%3d" % gx for gx in range(x0, x1 + 1)))
    differing = []
    histogram = {}
    for gy in range(y0, y1 + 1):
        cells = []
        for gx in range(x0, x1 + 1):
            here = pixel_rgb(first, width, height, bpp, gx, gy, first_top_down)[0]
            other = pixel_rgb(second, width, height, bpp, gx, gy, second_top_down)[0]
            if here != other:
                differing.append((gx, gy, here, other))
            histogram[here] = histogram.get(here, 0) + 1
            cells.append("*%2d" % here[0] if (gx == x and gy == y) else "%3d" % here[0])
        print("BMP-INSPECT grid_A_r y=%3d %s" % (gy, " ".join(cells)))
    print("BMP-INSPECT differing_in_window: %d of %d"
          % (len(differing), (x1 - x0 + 1) * (y1 - y0 + 1)))
    for gx, gy, here, other in differing[:12]:
        print("BMP-INSPECT differs (%d,%d) A=%s B=%s delta=%s"
              % (gx, gy, here, other, tuple(abs(p - q) for p, q in zip(here, other))))
    if len(differing) > 12:
        print("BMP-INSPECT differs ... %d more" % (len(differing) - 12))
    ranked = sorted(histogram.items(), key=lambda item: (-item[1], item[0]))
    print("BMP-INSPECT window_A_values: %s"
          % " ".join("%s x%d" % (value, count) for value, count in ranked[:8]))
    print("BMP-INSPECT status=OK")


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
    inspect_pixel = None
    inspect_window = 9
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
        if token == "--inspect-pixel":
            if index + 1 >= len(args):
                print("BMP-SILHOUETTE status=ERROR reason=--inspect-pixel needs X,Y")
                return EXIT_ERROR
            parts = args[index + 1].split(",")
            if len(parts) != 2:
                print("BMP-SILHOUETTE status=ERROR reason=--inspect-pixel wants X,Y")
                return EXIT_ERROR
            try:
                inspect_pixel = (int(parts[0]), int(parts[1]))
            except ValueError:
                print("BMP-SILHOUETTE status=ERROR reason=--inspect-pixel wants two integers")
                return EXIT_ERROR
            index += 2
            continue
        if token == "--inspect-window":
            if index + 1 >= len(args):
                print("BMP-SILHOUETTE status=ERROR reason=--inspect-window needs a value")
                return EXIT_ERROR
            try:
                inspect_window = int(args[index + 1])
            except ValueError:
                print("BMP-SILHOUETTE status=ERROR reason=--inspect-window is not an integer")
                return EXIT_ERROR
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
    has_bounds = any(bounds[key] is not None for key in bounds if key != "edge_threshold")
    if not has_bounds and inspect_pixel is None:
        print("BMP-SILHOUETTE status=ERROR reason=no bound given; nothing would be checked")
        return EXIT_ERROR
    if inspect_window < 3 or inspect_window % 2 == 0:
        print("BMP-SILHOUETTE status=ERROR reason=--inspect-window must be an odd number >= 3")
        return EXIT_ERROR
    threshold = bounds["edge_threshold"] if bounds["edge_threshold"] is not None else 32

    try:
        first, width, height, bpp, first_top_down = read_frame(positional[0])
        second, sw, sh, sbpp, second_top_down = read_frame(positional[1])
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
    if inspect_pixel is not None:
        if not (0 <= inspect_pixel[0] < width and 0 <= inspect_pixel[1] < height):
            print("BMP-SILHOUETTE status=ERROR reason=--inspect-pixel %d,%d is outside %dx%d"
                  % (inspect_pixel[0], inspect_pixel[1], width, height))
            return EXIT_ERROR
        if first_top_down != second_top_down or first_top_down != reference_top_down:
            print("BMP-SILHOUETTE status=ERROR reason=--inspect-pixel needs frames of one orientation")
            return EXIT_ERROR

    try:
        mask = edge_mask(reference, width, height, bpp, threshold)
    except ValueError as error:
        print("BMP-SILHOUETTE status=ERROR reason=%s" % error)
        return EXIT_ERROR

    if inspect_pixel is not None:
        inspect_sample(first, second, reference, width, height, bpp,
                       first_top_down, second_top_down, reference_top_down, mask,
                       threshold, inspect_pixel[0], inspect_pixel[1], inspect_window,
                       positional[0], positional[1], edge_frame)
        if not has_bounds:
            print("BMP-INSPECT inspection-only=1 (no bound given: nothing was checked)")
            return EXIT_PASS

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
