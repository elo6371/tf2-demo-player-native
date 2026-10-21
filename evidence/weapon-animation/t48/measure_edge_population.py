#!/usr/bin/env python3
"""measure_edge_population.py -- the population behind the T48 bound.

The identity-skinning pair differs by ONE sample. That sample can only change
ownership if it sits within the vertex displacement's reach of a raster edge, so
the bound needs three measured population numbers, not a hand count:

  footprint   where the model is, measured as the pixels the pose change
              (identity frame vs animated frame) moves; the world is excluded by
              construction because a still world does not move with the pose
  scale       the model's normalised box is 1.2 units; the footprint's diagonal
              therefore gives an upper bound on pixels-per-unit
  boundaries  how many visible boundary samples the model has: model-shaded
              pixels whose 4-neighbour maximum per-channel gradient reaches a
              stated threshold, halved because a boundary has two sides

Then the displacement of the skinning branch is bounded by the shader's own
float expression (identity matrices make mul(p, I) exactly p, so the difference
is only the normalised-weight sum's rounding, <= 1e-6 of a unit), and the
expected number of one-sample ties is 2 * delta_px * boundaries. That number is
what decides whether "one sample flipped" is an anomaly or the expected
deterministic outcome.

Usage: measure_edge_population.py IDENTITY.bmp ANIMATED.bmp [--scale-units U]
Exit:  0 (it prints measurements; it makes no pass/fail claim)
"""
import struct
import sys

BOUND_UNITS = 1e-6  # conservative bound on |skinned - passthrough| for identity matrices


def read_frame(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[0:2] != b"BM":
        raise ValueError("%s is not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    return data[offset:], width, abs(height), abs(width) * 3


def main(argv):
    scale_units = 1.2
    if "--scale-units" in argv:
        index = argv.index("--scale-units")
        scale_units = float(argv[index + 1])
        del argv[index:index + 2]
    if len(argv) != 3:
        print("usage: measure_edge_population.py IDENTITY.bmp ANIMATED.bmp [--scale-units U]")
        return 2
    identity, iw, ih, istride = read_frame(argv[1])
    animated, aw, ah, astride = read_frame(argv[2])
    if (iw, ih) != (aw, ah) or len(identity) != len(animated):
        print("ERROR: frames are not the same geometry")
        return 2

    # Footprint: pixels the pose change moves.
    xs, ys = [], []
    for y in range(ih):
        base_i, base_a = y * istride, y * astride
        for x in range(iw):
            oi, oa = base_i + x * 3, base_a + x * 3
            if identity[oi:oi + 3] != animated[oa:oa + 3]:
                xs.append(x)
                ys.append(y)
    left, right, top, bottom = min(xs), max(xs), min(ys), max(ys)
    width, height = right - left + 1, bottom - top + 1
    diagonal = (width * width + height * height) ** 0.5
    scale = diagonal / scale_units
    # The loops walk STORED rows; the printed box is in display rows (top-left
    # origin) because that is the coordinate the report and the gate use.
    print("footprint pixels=%d bbox=x%d..%d y%d..%d extent=%dx%d diagonal=%.1fpx"
          % (len(xs), left, right, ih - 1 - bottom, ih - 1 - top, width, height, diagonal))
    print("scale upper bound=%.1f px/unit (the %.1f-unit box projected onto the %.1f px footprint diagonal)"
          % (scale, scale_units, diagonal))

    # Model shading: the product forces vertex colour (0.72, 0.72, 0.76), so a
    # model pixel has R == G and B == R * 0.76 / 0.72 up to 8-bit rounding. World
    # pixels in the same box are dark (measured: R ~ 27..30) and fail the R floor.
    def model_like(r, g, b):
        if r < 100 or r != g:
            return False
        return abs(b - r * 0.76 / 0.72) <= 1.5

    def pixel(x, y):
        offset = y * istride + x * 3
        return identity[offset + 2], identity[offset + 1], identity[offset]

    model_pixels = 0
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            if model_like(*pixel(x, y)):
                model_pixels += 1
    print("model-shaded pixels=%d of %d in the footprint box (%.1f%%)"
          % (model_pixels, width * height, 100.0 * model_pixels / (width * height)))

    # Boundary samples: model pixels with a contrasty neighbour, both sides
    # counted, so the number of boundaries is half the population.
    boundaries_by_threshold = {}
    for threshold in (32, 48):
        samples = 0
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
                here = pixel(x, y)
                if not model_like(*here):
                    continue
                best = 0
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if nx < left or ny < top or nx > right or ny > bottom:
                        continue
                    other = pixel(nx, ny)
                    for channel in range(3):
                        delta = abs(here[channel] - other[channel])
                        if delta > best:
                            best = delta
                if best >= threshold:
                    samples += 1
        boundaries_by_threshold[threshold] = samples // 2
        delta_px = BOUND_UNITS * scale
        expected = 2.0 * delta_px * (samples / 2.0)
        print("boundary threshold=%d samples=%d boundaries=%d shift_bound=%.6fpx expected_ties=%.3f"
              % (threshold, samples, samples // 2, delta_px, expected))
    # The same arithmetic at a realistic rather than worst-case rounding error:
    # the sum of three normalised weights is wrong by a few ulp, not by the
    # 1e-6-of-a-unit bound above. Printed so the bound is not read as a point.
    realistic_px = 1.5e-7 * scale
    print("realistic_shift(1.5e-7 units)=%.6fpx expected_ties_at_threshold_32=%.3f"
          % (realistic_px, 2.0 * realistic_px * boundaries_by_threshold[32]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
