#!/usr/bin/env python3
"""reconcile_lighting.py -- turn an observed sample colour into shader values.

The standalone --model path draws with one untextured shader (native/src/
native_renderer.cpp): the bind-pose upload forces every vertex colour to
(0.72, 0.72, 0.76), and the pixel shader returns colour.rgb * lighting with

    lighting = 0.62 + 0.38 * saturate(dot(normal, normalize(float3(0.35, 0.55, 0.75))))

An 8-bit UNORM store keeps the nearest of 255 levels, so one observed channel
value bounds the lighting factor to a 1/255-wide interval; three channels agree
on an interval only if the observed pixel really is one vertex colour under one
lighting factor. This script prints those intervals, so "the two candidates are
the same vertex colour under two different normals" is a measurement rather than
an eyeball comparison.

Usage: reconcile_lighting.py R,G,B [R,G,B ...]
Exit:  0 (it reports intervals; it makes no pass/fail claim)
"""
import sys

VERTEX_COLOUR = (0.72, 0.72, 0.76)


def interval(value, colour):
    low = (value - 0.5) / 255.0 / colour
    high = (value + 0.5) / 255.0 / colour
    return low, high


def main(argv):
    if len(argv) < 2:
        print("usage: reconcile_lighting.py R,G,B [R,G,B ...]")
        return 2
    for spec in argv[1:]:
        try:
            values = [int(part) for part in spec.split(",")]
        except ValueError:
            print("ERROR: %r is not R,G,B" % spec)
            return 2
        if len(values) != 3:
            print("ERROR: %r is not R,G,B" % spec)
            return 2
        low, high = 0.0, 1.0
        for value, colour in zip(values, VERTEX_COLOUR):
            channel_low, channel_high = interval(value, colour)
            low = max(low, channel_low)
            high = min(high, channel_high)
        if low >= high:
            print("colour=(%d,%d,%d) lighting=inconsistent (no single factor explains the three channels)"
                  % tuple(values))
            continue
        diffuse_low = (low - 0.62) / 0.38
        diffuse_high = (high - 0.62) / 0.38
        print("colour=(%d,%d,%d) lighting=%.6f..%.6f diffuse=%.6f..%.6f%s"
              % (values[0], values[1], values[2], low, high, diffuse_low, diffuse_high,
                 " (saturated at 0: the surface faces away from the light)" if diffuse_low < 0 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
