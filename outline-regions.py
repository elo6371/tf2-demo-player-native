#!/usr/bin/env python3
"""outline-regions.py -- turn the product's own overlay reading into pixel boxes.

WHY THIS EXISTS
  "The red player's region must be red" is only a measurement if the region is
  the entity's and not the script's opinion. The pass publishes, per drawn
  silhouette, the entity index, the team, and the screen box its own geometry
  projects into. This script reads those rows out of the trace and prints them --
  no search of the frame, no "wherever it changed".

MODES
  --csv            every drawn item's box, joined with ";" (the regions the
                   non-interference assertion may differ inside)
  --team           the largest RED box and the largest BLUE box as
                   "red=x0,y0,x1,y1 blue=x0,y0,x1,y1"
  --occluded       a rectangle EVERY pixel of which the frame's own depth buffer
                   says is behind world geometry. Empty when there is none.

  --occluded needs --depth-pgm (preferred) or --depth, the same tick's capture.
  --depth-pgm is the 16-bit PGM the renderer writes with --capture-depth: the
  depth buffer's own value, 1/65535 apart. --depth is the 8-bit BMP written by
  --world-depth, whose diagnostic view SATURATES past a few world units (T26
  asserts that saturation, so it has to stay), which left "is this pixel behind a
  wall 900 units away when the player is 1200 away" with 8 levels of answer --
  everything reads 255, and no occlusion can be proved. The 16-bit image is the
  same 0..1 value with 256x the resolution, so a wall and a player behind it stay
  distinguishable.

  --pgm-stats      facts about a --capture-depth image (bytes, size, distinct
                   values, min/max), so "the depth map is really 16-bit" is an
                   assertion and not a hope.

WHY THE OCCLUDED REGION IS A RECTANGLE AND NOT A BOUNDING BOX
  The depth-awareness assertion is "the depth-tested frame draws NOTHING here and
  is byte-identical to the overlay-off frame here". A bounding box of the
  occluded pixels of two players also contains pixels that are NOT occluded, so
  the assertion would fail on a correct build and pass for the wrong reason once
  the box happened to fit. The region printed here is the largest axis-aligned
  rectangle that the depth buffer proves is occluded, so the claim is about a
  region that is occluded by construction.
"""
import os
import re
import struct
import sys

# How much nearer than the silhouette's own projected depth the world surface has
# to be before the pixel counts as occluded. 0.004 is about one 8-bit depth step
# (1/255) plus a margin, and far smaller than the separation between a player and
# a wall in front of it.
DEPTH_EPSILON = 0.004


def parse_args(argv):
    args = {"mode": None, "trace": None, "depth": None, "depthpgm": None,
            "on": None, "off": None, "frame": "captured"}
    pending = None
    for item in argv[1:]:
        if pending == "--frame":
            args["frame"] = item
            pending = None
            continue
        if pending == "--trace":
            args["trace"] = item
            pending = None
            continue
        if pending == "--depth":
            args["depth"] = item
            pending = None
            continue
        if pending == "--depth-pgm":
            args["depthpgm"] = item
            pending = None
            continue
        if pending == "--on":
            args["on"] = item
            pending = None
            continue
        if pending == "--off":
            args["off"] = item
            pending = None
            continue
        if item == "--csv":
            args["mode"] = "csv"
        elif item == "--team":
            args["mode"] = "team"
        elif item == "--occluded":
            args["mode"] = "occluded"
        elif item == "--depth-stats":
            args["mode"] = "depthstats"
        elif item == "--pgm-stats":
            args["mode"] = "pgmstats"
        elif item == "--attribute":
            args["mode"] = "attribute"
        elif item == "--extent":
            args["mode"] = "extent"
        elif item == "--expand-tolerance":
            args["mode"] = "expand-tolerance"
        elif item == "--expand-band":
            args["mode"] = "expand-band"
        elif item == "--trace":
            pending = "--trace"
        elif item == "--depth":
            pending = "--depth"
        elif item == "--depth-pgm":
            pending = "--depth-pgm"
        elif item == "--on":
            pending = "--on"
        elif item == "--off":
            pending = "--off"
        elif item.startswith("--trace="):
            args["trace"] = item.split("=", 1)[1]
        elif item == "--frame":
            pending = "--frame"
        elif item.startswith("--depth="):
            args["depth"] = item.split("=", 1)[1]
        elif item.startswith("--depth-pgm="):
            args["depthpgm"] = item.split("=", 1)[1]
        elif item.startswith("--frame="):
            args["frame"] = item.split("=", 1)[1]
        else:
            raise SystemExit("unknown argument: " + item)
    if pending is not None:
        raise SystemExit(pending + " needs a value")
    return args


# One row per drawn silhouette. The format is fixed by the product:
#   outline: item entity=12 team=red depth=xray x0=.. y0=.. x1=.. y1=.. depth=0.53 onScreen=1
#   T52 (2026-10-23): the line gained `extent=` (1 = the region is the projected
#   vertices of the mesh the draw consumes, 0 = the entity AABB, 2 = vertices asked
#   for and none in front of the eye) and `aabb=`, the old rule's box. Optional in
#   the pattern so an older trace still parses, but the gate asserts extent=1 rather
#   than assuming it.
ITEM = re.compile(
    r"outline: item entity=(\d+) team=(\w+) depth=(\w+) "
    r"x0=(-?\d+) y0=(-?\d+) x1=(-?\d+) y1=(-?\d+) depth=([0-9.]+) onScreen=([01])"
    r"(?: extent=(\d+) expand=([0-9.]+) aabb=(-?\d+),(-?\d+),(-?\d+),(-?\d+))?")


DRAW = re.compile(r"outline: draw ")
CAPTURE = re.compile(r"capture: armed ")


def read_items(path, frame="captured"):
    """The item rows of the frame the BMP was captured from.

    A trace holds one 'outline: draw' line per frame followed by that frame's
    item rows, and a capture is one frame -- but NOT the last one. The product
    arms the capture with 'capture: armed requested=N frame_tick=T frames=F' and
    then draws; the recording keeps running for a few more frames before the
    process exits. So 'the last frame's items' describes a camera the BMP does
    not show, and the gate compared a picture against another frame's boxes:
    measured 2026-10-22, 'differing pixels outside every drawn region = 297'
    with the differences reaching 23 px away -- not an outline halo, a player
    who had moved.

    Three modes:
      captured (default) the group after the last 'capture: armed' line
      last     the last group in the file (what this reader did before)
      all      every group joined (the original default, 4701 boxes)
    """
    groups = []
    capture_group = None
    armed = False
    with open(path, "r", errors="replace") as handle:
        for line in handle:
            if CAPTURE.search(line):
                armed = True
                continue
            if DRAW.search(line):
                groups.append([])
                if armed:
                    capture_group = len(groups) - 1
                    armed = False
                continue
            match = ITEM.search(line)
            if not match:
                continue
            if not groups:
                groups.append([])
            groups[-1].append({
                "entity": int(match.group(1)),
                "team": match.group(2),
                "mode": match.group(3),
                "x0": int(match.group(4)),
                "y0": int(match.group(5)),
                "x1": int(match.group(6)),
                "y1": int(match.group(7)),
                "depth": float(match.group(8)),
                "onScreen": match.group(9) == "1",
                # -1 when the trace predates the field: "unknown" is not "0".
                "extent": int(match.group(10)) if match.group(10) is not None else -1,
                # -1.0 when the trace predates the field: "unknown" is not "0", and a
                # gate that read a missing tolerance as a zero tolerance would call a
                # correct silhouette a defect.
                "expand": float(match.group(11)) if match.group(11) is not None else -1.0,
                "aabb": (int(match.group(12)), int(match.group(13)),
                         int(match.group(14)), int(match.group(15)))
                        if match.group(12) is not None else None,
            })
    if not groups:
        return []
    if frame == "all":
        return [item for group in groups for item in group]
    if frame == "last" or capture_group is None:
        return groups[-1]
    return groups[capture_group]


def area(item):
    return max(0, item["x1"] - item["x0"] + 1) * max(0, item["y1"] - item["y0"] + 1)


def read_depth_bmp(path):
    """The --world-depth capture: every byte is the interpolated depth as grey.

    The world pixel shader's diagnostic mode 5 writes position.z / position.w
    into all four channels (native_renderer.cpp, the envFeatures.x > 4.5 branch),
    i.e. the same 0..1 value the depth buffer holds, so a pixel's brightness IS
    its depth scaled by 255. Nearer is darker; the comparison below says so.
    """
    data = open(path, "rb").read()
    if data[0:2] != b"BM":
        raise SystemExit("not a BMP: " + path)
    off = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0] // 8
    if bpp < 3:
        raise SystemExit("want at least 24bpp, got %d" % (bpp * 8))
    flip = height < 0
    height = abs(height)
    stride = width * bpp
    return data[off:], width, height, bpp, flip, stride


def read_depth_pgm(path):
    """The --capture-depth image: a 16-bit P5 PGM of the world depth buffer.

    Same meaning as read_depth_bmp -- 0 is the near plane, 1 is far -- but taken
    from the depth buffer itself (D24_UNORM_S8_UINT, quantised to 16 bits on the
    way out) instead of the 8-bit diagnostic view, which saturates. Rows are
    written top-down and there is no row padding, so flip is False.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[0:2] != b"P5":
        raise SystemExit("not a P5 PGM: " + path)
    cursor = 2
    fields = []
    while len(fields) < 3:
        while cursor < len(data) and chr(data[cursor]).isspace():
            cursor += 1
        if data[cursor:cursor + 1] == b"#":
            while cursor < len(data) and data[cursor:cursor + 1] != b"\n":
                cursor += 1
            continue
        start = cursor
        while cursor < len(data) and not chr(data[cursor]).isspace():
            cursor += 1
        fields.append(int(data[start:cursor]))
    cursor += 1  # the single whitespace byte that ends the header
    width, height, maxval = fields
    if maxval != 65535:
        raise SystemExit("want 16-bit P5 (maxval 65535), got %d in %s" % (maxval, path))
    pixels = data[cursor:]
    if len(pixels) < width * height * 2:
        raise SystemExit("short PGM: %d bytes for %dx%d" % (len(pixels), width, height))
    return pixels, width, height, 2, False, width * 2


def depth_at(frame, x, y):
    pixels, width, height, bpp, flip, stride = frame
    if x < 0 or y < 0 or x >= width or y >= height:
        return None
    row = y if flip else (height - 1 - y)
    offset = row * stride + x * bpp
    if bpp == 2:
        # read_depth_pgm: big-endian 16-bit samples, 256x the 8-bit view's steps.
        return ((pixels[offset] << 8) | pixels[offset + 1]) / 65535.0
    return pixels[offset] / 255.0


def occluded_pixels(item, frame):
    """The pixels of one item's box whose world depth is NEARER than the silhouette.

    A silhouette pixel is occluded when the frame's own depth at that pixel is
    strictly nearer than the silhouette's projected depth: that is the wall
    standing between the camera and the player. The item's depth is the nearest
    of its eight projected corners, so this can only under-report occlusion,
    never invent it.
    """
    pixels = set()
    for y in range(item["y0"], item["y1"] + 1):
        for x in range(item["x0"], item["x1"] + 1):
            world = depth_at(frame, x, y)
            if world is None:
                continue
            if world < item["depth"] - DEPTH_EPSILON:
                pixels.add((x, y))
    return pixels


def largest_all_occluded_rectangle(pixels, item):
    """The largest axis-aligned rectangle inside the item box made only of occluded pixels.

    Histogram-per-row, the classic largest-rectangle-in-a-histogram scan. The
    result is a region the depth buffer proves is occluded at EVERY pixel, which
    is what makes "the depth-tested frame draws nothing here" a statement about
    the wall rather than about the player's box.
    """
    x0, y0, x1, y1 = item["x0"], item["y0"], item["x1"], item["y1"]
    width = x1 - x0 + 1
    if width <= 0:
        return None
    heights = [0] * width
    best = None  # (area, x0, y0, x1, y1)
    for y in range(y0, y1 + 1):
        for index in range(width):
            if (x0 + index, y) in pixels:
                heights[index] += 1
            else:
                heights[index] = 0
        stack = []  # (startIndex, height)
        for index in range(width + 1):
            height = heights[index] if index < width else 0
            start = index
            while stack and stack[-1][1] > height:
                column, columnHeight = stack.pop()
                candidate = columnHeight * (index - column)
                if columnHeight > 0 and (best is None or candidate > best[0]):
                    best = (candidate, x0 + column, y - columnHeight + 1, x0 + index - 1, y)
                start = column
            stack.append((start, height))
    return best


def occluded_rectangle(items, frame):
    """The largest fully-occluded rectangle across every drawn silhouette."""
    best = None
    for item in items:
        pixels = occluded_pixels(item, frame)
        if not pixels:
            continue
        rectangle = largest_all_occluded_rectangle(pixels, item)
        if rectangle is None:
            continue
        if best is None or rectangle[0] > best[0]:
            best = rectangle + (item["entity"], item["team"], len(pixels))
    return best


def depth_stats(path):
    """Facts about a --world-depth capture that a colour frame cannot fake.

    THE FAILURE THIS EXISTS FOR: on 2026-10-23 the depth capture ran under a 600 s
    timeout, never reached the requested tick, wrote no BMP, the occluded region
    came back empty and section 5 fell to NOT VERIFIED -- so mutation m2 was
    unproven for a reason the log did not name. The file has to be proved to
    EXIST, to be a DEPTH image, and to have GEOMETRY in it before one pixel of it
    is used as evidence.

    nonGreyPct: the world shader writes the same scalar into R, G and B
    (read_depth_bmp documents why), so a real depth capture is grey by
    construction. A colour frame of the same scene is ~17% non-grey (measured
    2026-10-23), which is what makes "feed the colour frame as the depth map" a
    detectable substitution instead of a silent one.
    nonzeroPct: an all-black BMP (nothing rendered, or a truncated write) is 0.
    uniqueSampled: a blank or single-value image has 1.
    """
    try:
        data = open(path, "rb").read()
    except OSError:
        return {"bytes": 0, "nonGreyPct": -1.0, "nonzeroPct": -1.0, "uniqueSampled": 0}
    if len(data) < 54 or data[0:2] != b"BM":
        return {"bytes": len(data), "nonGreyPct": -1.0, "nonzeroPct": -1.0, "uniqueSampled": 0}
    off = struct.unpack_from("<I", data, 10)[0]
    bpp = struct.unpack_from("<H", data, 28)[0] // 8
    if bpp < 3:
        return {"bytes": len(data), "nonGreyPct": -1.0, "nonzeroPct": -1.0, "uniqueSampled": 0}
    pixels = data[off:]
    count = len(pixels) // bpp
    step = max(1, count // 20000)
    samples = non_grey = nonzero = 0
    seen = set()
    for i in range(0, count, step):
        b = pixels[i * bpp]
        g = pixels[i * bpp + 1]
        r = pixels[i * bpp + 2]
        samples += 1
        if not (r == g == b):
            non_grey += 1
        if b or g or r:
            nonzero += 1
        seen.add(b)
    return {
        "bytes": len(data),
        "nonGreyPct": round(100.0 * non_grey / max(1, samples), 2),
        "nonzeroPct": round(100.0 * nonzero / max(1, samples), 2),
        "uniqueSampled": len(seen),
    }


def pgm_stats(path):
    """Facts about a --capture-depth image that an 8-bit capture cannot fake.

    distinct is the assertion with teeth: the diagnostic view can only hold 256
    values, so a depth map with thousands of distinct samples came from the
    buffer and not from the saturated view. min/max bound the scene: a capture
    that never drew anything is all 65535 (the clear), one that drew only the sky
    is a single value.
    """
    try:
        pixels, width, height, bpp, flip, stride = read_depth_pgm(path)
        size = os.stat(path).st_size
    except (OSError, SystemExit) as error:
        return {"bytes": 0, "width": 0, "height": 0, "distinct": 0, "min": -1, "max": -1,
                "error": str(error)}
    count = min(len(pixels) // 2, width * height)
    step = max(1, count // 200000)
    seen = {}
    low = 65536
    high = -1
    for index in range(0, count, step):
        offset = index * 2
        value = (pixels[offset] << 8) | pixels[offset + 1]
        seen[value] = seen.get(value, 0) + 1
        if value < low:
            low = value
        if value > high:
            high = value
    return {
        "bytes": size,
        "width": width,
        "height": height,
        "distinct": len(seen),
        "min": low if high >= 0 else -1,
        "max": high,
        "samples": count,
    }


def attribute_rows(trace, on_path, off_path):
    """One row per drawn item: which entity, which box, where its paint is.

    D1 (2026-10-23) is "the boxes and the silhouettes are not the same objects".
    Four painted blobs, fourteen published boxes, 60 of 484 differing pixels inside
    any box -- that says the two disagree, not which side is wrong. This row is the
    attribution: for every item the product PUBLISHED, the centroid of the painted
    blob nearest to its box, and whether that centroid falls inside the box.

      centroid inside the box      -> the box follows the paint, so a silhouette
                                      drawn outside it is a projection problem
      centroid outside the box     -> the box does not follow the paint, so the
                                      world-to-screen step that derives it is wrong
    """
    items = [i for i in read_items(trace, "captured") if i["onScreen"]]
    a = read_depth_bmp(on_path)
    b = read_depth_bmp(off_path)
    width, height = a[1], a[2]
    def rgb(frame, x, y):
        pixels, w, h, bpp, flip, stride = frame
        row = y if flip else (h - 1 - y)
        off = row * stride + x * bpp
        return pixels[off], pixels[off + 1], pixels[off + 2]
    differing = set()
    for y in range(height):
        for x in range(width):
            if rgb(a, x, y) != rgb(b, x, y):
                differing.add((x, y))
    seen = set()
    blobs = []
    for point in differing:
        if point in seen:
            continue
        stack = [point]
        seen.add(point)
        blob = []
        while stack:
            cx, cy = stack.pop()
            blob.append((cx, cy))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    nxt = (cx + dx, cy + dy)
                    if nxt in differing and nxt not in seen:
                        seen.add(nxt)
                        stack.append(nxt)
        blobs.append(blob)
    rows = []
    for index, item in enumerate(items):
        bx = (item["x0"] + item["x1"]) / 2.0
        by = (item["y0"] + item["y1"]) / 2.0
        best = None
        bestd = None
        for blob in blobs:
            cx = sum(p[0] for p in blob) / float(len(blob))
            cy = sum(p[1] for p in blob) / float(len(blob))
            d = (cx - bx) ** 2 + (cy - by) ** 2
            if bestd is None or d < bestd:
                bestd = d
                best = (cx, cy, len(blob))
        if best is None:
            continue
        cx, cy, size = best
        inside = 1 if (item["x0"] <= cx <= item["x1"] and item["y0"] <= cy <= item["y1"]) else 0
        rows.append("attr i=%d entity=%d box=%d,%d,%d,%d centroid=%.1f,%.1f blobPx=%d inside=%d dist=%.1f"
                    % (index, item["entity"], item["x0"], item["y0"], item["x1"], item["y1"],
                       cx, cy, size, inside, bestd ** 0.5))
    return rows

def main(argv):
    args = parse_args(argv)
    if args.get("mode") == "depthstats":
        if not args["depth"]:
            raise SystemExit("--depth-stats needs --depth <world-depth bmp>")
        stats = depth_stats(args["depth"])
        print("bytes=%d nonGreyPct=%.2f nonzeroPct=%.2f uniqueSampled=%d"
              % (stats["bytes"], stats["nonGreyPct"], stats["nonzeroPct"],
                 stats["uniqueSampled"]))
        return 0
    if args.get("mode") == "pgmstats":
        if not args["depthpgm"]:
            raise SystemExit("--pgm-stats needs --depth-pgm <capture-depth pgm>")
        stats = pgm_stats(args["depthpgm"])
        if stats.get("error"):
            print("error=%s" % stats["error"])
            return 1
        print("bytes=%d width=%d height=%d distinct=%d min=%d max=%d"
              % (stats["bytes"], stats["width"], stats["height"], stats["distinct"],
                 stats["min"], stats["max"]))
        return 0
    if args.get("mode") == "expand-band":
        # The band the shell thickness has to sit in, read off the PRODUCT's own
        # number. A 1-2 px shell reads as an outline on a player who is ~4 px tall;
        # past ~4 px it stops reading as an edge and starts reading as a halo, which
        # is a worse thing to ship. Asserting the band makes a change to the constant
        # that nobody thought about show up as a red line, not as a picture nobody
        # measured.
        rows = [i for i in read_items(args["trace"], args.get("frame", "captured")) if i["onScreen"]]
        values = [row["expand"] for row in rows if row["expand"] >= 0.0]
        missing = len(rows) - len(values)
        below = sum(1 for value in values if value <= 0.0)
        above = sum(1 for value in values if value > 4.0)
        ordered = sorted(values)
        median = ordered[len(ordered) // 2] if ordered else 0.0
        print("items=%d published=%d missing=%d below=%d above=%d min=%.3f max=%.3f median=%.3f"
              % (len(rows), len(values), missing, below, above,
                 ordered[0] if ordered else 0.0, ordered[-1] if ordered else 0.0, median))
        return 0
    if args.get("mode") == "expand-tolerance":
        # The tolerance for "no painted pixel outside a region", and it is the
        # PRODUCT'S number, not the gate's: each silhouette publishes how many
        # pixels its own shell is allowed to stick out (expand=), computed in the
        # renderer from the expansion constant and that instance's model->world
        # scale. The allowed total is the sum of (perimeter x expand) over the
        # items -- a pixel budget, not a guessed threshold. `missing=` counts
        # items that did not publish the number at all: that is a failure of the
        # product to speak, not a zero tolerance.
        rows = [i for i in read_items(args["trace"], args.get("frame", "captured")) if i["onScreen"]]
        tolerance = 0.0
        missing = 0
        published = 0
        for row in rows:
            if row["expand"] < 0.0:
                missing += 1
                continue
            published += 1
            width = max(0, row["x1"] - row["x0"] + 1)
            height = max(0, row["y1"] - row["y0"] + 1)
            tolerance += 2.0 * (width + height) * max(0.0, row["expand"])
        print("tolerance=%.3f items=%d published=%d missing=%d"
              % (tolerance, len(rows), published, missing))
        return 0
    if args.get("mode") == "extent":
        rows = [i for i in read_items(args["trace"], args.get("frame", "captured")) if i["onScreen"]]
        counts = {1: 0, 0: 0, 2: 0, -1: 0}
        for row in rows:
            counts[row["extent"] if row["extent"] in counts else -1] += 1
        # The box height travels with the count so "the box is 1-3 px tall while the
        # paint is 10-14 px tall" is a reading in the gate's own output, not a
        # deduction someone has to redo from the attribution table.
        heights = [row["y1"] - row["y0"] + 1 for row in rows]
        hmin = min(heights) if heights else 0
        hmax = max(heights) if heights else 0
        print("items=%d extent1=%d extent0=%d extent2=%d extentMissing=%d hmin=%d hmax=%d"
              % (len(rows), counts[1], counts[0], counts[2], counts[-1], hmin, hmax))
        return 0
    if args.get("mode") == "attribute":
        if not (args["trace"] and args["on"] and args["off"]):
            raise SystemExit("--attribute needs --trace --on --off")
        for row in attribute_rows(args["trace"], args["on"], args["off"]):
            print(row)
        return 0
    if not args["mode"] or not args["trace"]:
        raise SystemExit(__doc__)
    items = read_items(args["trace"], args.get("frame", "captured"))
    on_screen = [item for item in items if item["onScreen"]]

    if args["mode"] == "csv":
        print(";".join("%d,%d,%d,%d" % (i["x0"], i["y0"], i["x1"], i["y1"])
                       for i in on_screen))
        return 0

    if args["mode"] == "team":
        best = {"red": None, "blue": None}
        for item in on_screen:
            team = item["team"]
            if team not in best:
                continue
            if best[team] is None or area(item) > area(best[team]):
                best[team] = item
        out = []
        for team in ("red", "blue"):
            item = best[team]
            if item is not None:
                out.append("%s=%d,%d,%d,%d" % (team, item["x0"], item["y0"], item["x1"], item["y1"]))
        print(" ".join(out))
        return 0

    if args["mode"] == "occluded":
        if args.get("depthpgm"):
            frame = read_depth_pgm(args["depthpgm"])
        elif args["depth"]:
            frame = read_depth_bmp(args["depth"])
        else:
            raise SystemExit("--occluded needs --depth-pgm <capture-depth pgm> "
                             "or --depth <world-depth bmp>")
        best = occluded_rectangle(on_screen, frame)
        if best is None:
            sys.stderr.write("occluded: no fully-occluded pixel in %d drawn item(s)\n"
                             % len(on_screen))
            return 0
        rectArea, rx0, ry0, rx1, ry1, entity, team, count = best
        sys.stderr.write(
            "occluded: entity=%d team=%s occludedPixels=%d rect=%d,%d,%d,%d (%d px)\n"
            % (entity, team, count, rx0, ry0, rx1, ry1, rectArea))
        print("%d,%d,%d,%d" % (rx0, ry0, rx1, ry1))
        return 0

    raise SystemExit("no mode selected")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
