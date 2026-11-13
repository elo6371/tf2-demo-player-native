#!/usr/bin/env python3
"""world-geometry-accounting.py -- T23: why is this pixel not showing a world surface?

WHY THIS FILE EXISTS
--------------------
"The world has holes" is not a diagnosis. A pixel with no world surface can be
produced by four different mechanisms, and the fix for one is a regression in
another:

  missing     no triangle exists along the pixel, even with the pvs clip and the
              back-face cull both off. Nothing in the world pass can draw here.
  backface    a nearer triangle exists but its emitted winding faces away, so
              the cull removes it. The pixel either shows a farther surface
              (swapped) or nothing at all (empty).
  visibility  a nearer triangle exists and faces the eye but its cluster is not
              in the camera's pvs, so the clip removes it. Same two outcomes.
  both        both switches remove the nearer surface.
  drawn       the nearest triangle the all-off frame can draw is the one the
              product draws. If it reads dark in the textured frame, that is
              shading (material/lightmap), not geometry.

The instrument is the three switches the product already has, captured at the
same demo tick and camera:

  flat-*    --world-flat   is ANY world triangle drawn at this pixel?
  depth-*   --world-depth  WHICH surface is drawn (a nearer triangle has a
                           different depth; the flat colour does not)
  none      --world-geometry none  the frame's own background (sky/clear)

Four flat frames (clip on/off x cull on/off) give the fill-level account; four
depth frames give the surface-identity account. Comparing the product frame's
depth against the all-off frame's depth says whether the surface the product
draws is the nearest one; comparing against the cull-off and clip-off depths
says which switch moved it when it is not.

This is a measurement, not an acceptance: it names where each missing pixel
came from and how large the enclosed empty regions are. `drawn` does not mean
"correct material" -- that is what the textured/lightmap comparisons and the
probe's lightmap-tone reading are for.

Usage:
  world-geometry-accounting.py --dir evidence/t23-world-geometry/captures \
      [--out evidence/t23-world-geometry/analysis] [--dark 16]

Exit: 0 = the report was produced; 2 = an input frame is missing or malformed.
"""
import collections
import os
import struct
import sys

EXIT_OK, EXIT_ERROR = 0, 2
FRAME_SUFFIXES = ("base", "novis", "nocull", "novis-nocull")
REGIONS = [
    ("W1", 0, 0, 420, 200),
    ("W2", 0, 560, 420, 121),
    ("W3", 1100, 100, 164, 200),
    ("W4", 600, 0, 64, 60),
]


class Frame:
    __slots__ = ("path", "width", "height", "pixels")

    def __init__(self, path, width, height, pixels):
        self.path = path
        self.width = width
        self.height = height
        self.pixels = pixels  # top-down, RGB triplets, no padding


def read_bmp(path):
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as error:
        raise ValueError("cannot read %s: %s" % (path, error))
    if len(data) < 54 or data[0:2] != b"BM":
        raise ValueError("%s is not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if bits != 24 or compression != 0:
        raise ValueError("%s is not an uncompressed 24-bit BMP (%d bpp, compression %d)"
                         % (path, bits, compression))
    if width <= 0 or height == 0:
        raise ValueError("%s has an invalid extent %dx%d" % (path, width, height))
    flip = height > 0
    height = abs(height)
    stride = ((width * 3) + 3) & ~3
    if offset + stride * height > len(data):
        raise ValueError("%s is shorter than its declared pixel data" % path)
    out = bytearray(width * height * 3)
    for y in range(height):
        source = (height - 1 - y) if flip else y
        start = offset + source * stride
        out[y * width * 3:(y + 1) * width * 3] = data[start:start + width * 3]
    return Frame(path, width, height, out)


def write_bmp(path, width, height, pixels, palette):
    """palette: list of 256 RGB triples; pixels is a bytearray of indices."""
    stride = ((width * 3) + 3) & ~3
    image = bytearray(stride * height)
    for y in range(height):
        row = (height - 1 - y) * stride
        base = y * width
        for x in range(width):
            r, g, b = palette[pixels[base + x]]
            at = row + x * 3
            image[at] = b
            image[at + 1] = g
            image[at + 2] = r
    header = bytearray(54)
    header[0:2] = b"BM"
    struct.pack_into("<I", header, 2, 54 + len(image))
    struct.pack_into("<I", header, 10, 54)
    struct.pack_into("<I", header, 14, 40)
    struct.pack_into("<i", header, 18, width)
    struct.pack_into("<i", header, 22, height)
    struct.pack_into("<H", header, 26, 1)
    struct.pack_into("<H", header, 28, 24)
    struct.pack_into("<I", header, 34, len(image))
    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(image)


def luminance(pixels, at):
    return 0.299 * pixels[at] + 0.587 * pixels[at + 1] + 0.114 * pixels[at + 2]


def mode_colour(pixels):
    counts = collections.Counter()
    step = 3
    # Every 7th pixel is enough for a mode and keeps 1.2M-pixel frames cheap.
    for at in range(0, len(pixels), step * 7):
        counts[(pixels[at], pixels[at + 1], pixels[at + 2])] += 1
    return counts.most_common(1)[0][0]


def flat_mask(frame, colour):
    """1 where the pixel is exactly the flat world colour."""
    r, g, b = colour
    out = bytearray(frame.width * frame.height)
    pixels = frame.pixels
    for index in range(len(out)):
        at = index * 3
        if pixels[at] == r and pixels[at + 1] == g and pixels[at + 2] == b:
            out[index] = 1
    return out


def region_stats(label, x, y, w, h, width, height, classes, labels, textured, lightmap_off, dark_limit):
    x = max(0, x); y = max(0, y)
    w = min(w, width - x); h = min(h, height - y)
    counts = collections.Counter()
    dark = 0
    lightmap_driven = 0
    albedo_driven = 0
    for yy in range(y, y + h):
        row = yy * width
        for xx in range(x, x + w):
            index = row + xx
            kind = labels[classes[index]]
            counts[kind] += 1
            if kind == "drawn" and luminance(textured.pixels, index * 3) < dark_limit:
                dark += 1
                if lightmap_off is not None and luminance(lightmap_off.pixels, index * 3) >= dark_limit + 16:
                    lightmap_driven += 1
                else:
                    albedo_driven += 1
    total = max(1, w * h)
    return ("region %s x=%d,y=%d,w=%d,h=%d pixels=%d drawn=%d backface=%d visibility=%d both=%d "
            "missing=%d other=%d overlay=%d drawn_share=%.4f missing_share=%.4f dark=%d dark_share=%.4f "
            "dark_lightmap=%d dark_albedo=%d" % (
                label, x, y, w, h, w * h, counts["drawn"], counts["backface"],
                counts["visibility"], counts["both"], counts["missing"], counts["other"],
                counts["overlay"], counts["drawn"] / total, counts["missing"] / total, dark,
                dark / total, lightmap_driven, albedo_driven))


def components(mask, width, height, limit):
    """Connected components of a 1-mask, 4-neighbour, top-down."""
    seen = bytearray(len(mask))
    result = []
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        queue = collections.deque([start])
        seen[start] = 1
        area = 0
        members = []
        min_x = width; max_x = 0; min_y = height; max_y = 0
        border = 0
        while queue:
            index = queue.popleft()
            area += 1
            members.append(index)
            y = index // width
            x = index - y * width
            if x < min_x: min_x = x
            if x > max_x: max_x = x
            if y < min_y: min_y = y
            if y > max_y: max_y = y
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if nx < 0 or ny < 0 or nx >= width or ny >= height:
                    border += 1
                    continue
                nindex = ny * width + nx
                if mask[nindex]:
                    if not seen[nindex]:
                        seen[nindex] = 1
                        queue.append(nindex)
                else:
                    border += 1
        result.append({
            "area": area, "min_x": min_x, "max_x": max_x,
            "min_y": min_y, "max_y": max_y, "border": border,
            "members": members if area >= 200 else (),
        })
    result.sort(key=lambda item: item["area"], reverse=True)
    return result[:limit]


def main(argv):
    args = argv[1:]
    directory = "evidence/t23-world-geometry/captures"
    out_dir = "evidence/t23-world-geometry/analysis"
    dark_limit = 16
    index = 0
    while index < len(args):
        token = args[index]
        if token == "--dir" and index + 1 < len(args):
            directory = args[index + 1]; index += 2
        elif token == "--out" and index + 1 < len(args):
            out_dir = args[index + 1]; index += 2
        elif token == "--dark" and index + 1 < len(args):
            dark_limit = int(args[index + 1]); index += 2
        else:
            print("ACCOUNTING status=ERROR reason=unknown argument %s" % token)
            return EXIT_ERROR
    if not os.path.isdir(directory):
        print("ACCOUNTING status=ERROR reason=%s is not a directory" % directory)
        return EXIT_ERROR
    os.makedirs(out_dir, exist_ok=True)

    names = ["textured", "none", "terrain", "lightmap-off"]
    names += ["flat-%s" % suffix for suffix in FRAME_SUFFIXES]
    names += ["depth-%s" % suffix for suffix in FRAME_SUFFIXES]
    frames = {}
    try:
        for name in names:
            path = os.path.join(directory, name + ".bmp")
            frames[name] = read_bmp(path)
    except ValueError as error:
        print("ACCOUNTING status=ERROR reason=%s" % error)
        return EXIT_ERROR

    base = frames["flat-base"]
    width, height = base.width, base.height
    for name, frame in frames.items():
        if (frame.width, frame.height) != (width, height):
            print("ACCOUNTING status=ERROR reason=%s is %dx%d, expected %dx%d"
                  % (name, frame.width, frame.height, width, height))
            return EXIT_ERROR

    flat_colour = mode_colour(base.pixels)
    print("ACCOUNTING status=ok dir=%s size=%dx%d flat_colour=(%d,%d,%d)"
          % (directory, width, height, flat_colour[0], flat_colour[1], flat_colour[2]))

    present = {}
    for suffix in FRAME_SUFFIXES:
        present[suffix] = flat_mask(frames["flat-%s" % suffix], flat_colour)
    depth = {suffix: frames["depth-%s" % suffix].pixels for suffix in FRAME_SUFFIXES}
    textured = frames["textured"]
    lightmap_off = frames["lightmap-off"]
    # The `none` frame is the frame with no world pass: sky off, entities/HUD
    # still drawn. Its mode colour is the clear colour, and every pixel that is
    # not it is an overlay the world accounting must not blame on geometry. The
    # 24k such pixels on the reference frame are the HUD/entities; counting them
    # as "missing world" would invent a hole in the middle of the screen.
    clear_colour = mode_colour(frames["none"].pixels)
    overlay = bytearray(width * height)
    none_pixels = frames["none"].pixels
    for index in range(width * height):
        at = index * 3
        if (none_pixels[at] != clear_colour[0] or none_pixels[at + 1] != clear_colour[1]
                or none_pixels[at + 2] != clear_colour[2]):
            overlay[index] = 1
    print("ACCOUNTING background clear_colour=(%d,%d,%d) overlay_pixels=%d"
          % (clear_colour[0], clear_colour[1], clear_colour[2], sum(overlay)))

    classes = bytearray(width * height)  # 0 missing, 1 drawn, 2 backface, 3 visibility, 4 both, 5 other, 6 overlay
    labels = ("missing", "drawn", "backface", "visibility", "both", "other", "overlay")
    totals = collections.Counter()
    backface_empty = backface_swapped = 0
    visibility_empty = visibility_swapped = 0
    dark = 0
    dark_lightmap = 0
    dark_albedo = 0
    for index in range(width * height):
        if overlay[index]:
            kind = 6
            classes[index] = kind
            totals[labels[kind]] += 1
            continue
        p00 = present["base"][index]
        p10 = present["novis"][index]
        p01 = present["nocull"][index]
        p11 = present["novis-nocull"][index]
        if not p11:
            kind = 0
        elif p00:
            # Attribute by the switch's OWN effect, not by the all-off depth:
            # the all-off frame can be nearer because the cull removed a nearer
            # back-facing triangle, and blaming the visibility clip for that
            # (its depth is identical to the base) was a real mislabel in the
            # first run of this tool.
            cull_changed = (not p01) or abs(depth["base"][index * 3] - depth["nocull"][index * 3]) > 1
            vis_changed = (not p10) or abs(depth["base"][index * 3] - depth["novis"][index * 3]) > 1
            if not cull_changed and not vis_changed:
                kind = 1
            elif cull_changed and not vis_changed:
                kind = 2
            elif vis_changed and not cull_changed:
                kind = 3
            else:
                kind = 4
        elif p10 and not p01:
            kind = 3
        elif p01 and not p10:
            kind = 2
        elif p10 and p01:
            kind = 4
        else:
            kind = 5
        classes[index] = kind
        totals[labels[kind]] += 1
        if kind == 2:
            if p00: backface_swapped += 1
            else: backface_empty += 1
        elif kind == 3:
            if p00: visibility_swapped += 1
            else: visibility_empty += 1
        elif kind == 1 and luminance(textured.pixels, index * 3) < dark_limit:
            dark += 1
            if luminance(lightmap_off.pixels, index * 3) >= dark_limit + 16:
                dark_lightmap += 1
            else:
                dark_albedo += 1

    total = width * height
    print("ACCOUNTING totals pixels=%d drawn=%d backface=%d visibility=%d both=%d missing=%d other=%d overlay=%d"
          % (total, totals["drawn"], totals["backface"], totals["visibility"], totals["both"],
             totals["missing"], totals["other"], totals["overlay"]))
    print("ACCOUNTING shares drawn=%.4f backface=%.4f visibility=%.4f both=%.4f missing=%.4f other=%.4f overlay=%.4f"
          % (totals["drawn"] / total, totals["backface"] / total, totals["visibility"] / total,
             totals["both"] / total, totals["missing"] / total, totals["other"] / total,
             totals["overlay"] / total))
    print("ACCOUNTING rejected backface_empty=%d backface_swapped=%d visibility_empty=%d visibility_swapped=%d"
          % (backface_empty, backface_swapped, visibility_empty, visibility_swapped))
    print("ACCOUNTING dark drawn_dark=%d of %d (%.4f) lightmap_driven=%d albedo_driven=%d limit=%d"
          % (dark, totals["drawn"], dark / max(1, totals["drawn"]), dark_lightmap, dark_albedo, dark_limit))
    for label, x, y, w, h in REGIONS:
        print(region_stats(label, x, y, w, h, width, height, classes, labels,
                           textured, lightmap_off, dark_limit).replace("region ", "ACCOUNTING region "))

    # The suspicious-empty-region account: components of pixels no world triangle
    # reaches even with both switches off. A component that touches the frame
    # edge is sky/open view; a component enclosed by drawn pixels and larger than
    # a few hundred pixels is a hole.
    missing_mask = bytearray(1 if value == 0 else 0 for value in classes)
    missing_components = components(missing_mask, width, height, 24)
    for rank, item in enumerate(missing_components):
        touches_border = (item["min_x"] == 0 or item["min_y"] == 0
                          or item["max_x"] == width - 1 or item["max_y"] == height - 1)
        box = (item["max_x"] - item["min_x"] + 1) * (item["max_y"] - item["min_y"] + 1)
        print("ACCOUNTING missing-component rank=%d area=%d bbox=(x=%d..%d,y=%d..%d) "
              "edge=%d fill=%.3f"
              % (rank, item["area"], item["min_x"], item["max_x"], item["min_y"], item["max_y"],
                 1 if touches_border else 0, item["area"] / max(1, box)))

    # Cause map: one colour per class, plus the enclosed/open split for missing.
    palette = [
        (210, 40, 40),    # 0 missing (open sky or hole; recoloured below)
        (30, 150, 60),    # 1 drawn
        (40, 90, 220),    # 2 backface
        (220, 40, 220),   # 3 visibility
        (240, 150, 30),   # 4 both
        (120, 120, 120),  # 5 other
        (70, 70, 110),    # 6 overlay (HUD/entities, excluded from the account)
        (255, 255, 255),  # 7 enclosed missing (a hole)
        (20, 20, 20),     # 8 drawn but dark
    ]
    image = bytearray(width * height)
    for index in range(total):
        kind = classes[index]
        if kind == 0:
            image[index] = 0
        elif kind == 1 and luminance(textured.pixels, index * 3) < dark_limit:
            image[index] = 8
        else:
            image[index] = kind
    # Recolour the missing pixels of enclosed components white so a hole is
    # visible at a glance instead of being lost in the sky.
    for item in missing_components:
        if item["area"] < 200:
            continue
        touches_border = (item["min_x"] == 0 or item["min_y"] == 0
                          or item["max_x"] == width - 1 or item["max_y"] == height - 1)
        if touches_border:
            continue
        for index in item["members"]:
            if classes[index] == 0:
                image[index] = 7
    write_bmp(os.path.join(out_dir, "accounting-mask.bmp"), width, height, image, palette)
    print("ACCOUNTING mask=%s" % os.path.join(out_dir, "accounting-mask.bmp"))
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv))
