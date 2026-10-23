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

  --occluded needs --depth (the same tick's capture). The depth it compares
  against is read out of the BMP the renderer wrote with --world-depth, so a
  wall that is not where the trace thinks it is cannot be assumed away.

WHY THE OCCLUDED REGION IS A RECTANGLE AND NOT A BOUNDING BOX
  The depth-awareness assertion is "the depth-tested frame draws NOTHING here and
  is byte-identical to the overlay-off frame here". A bounding box of the
  occluded pixels of two players also contains pixels that are NOT occluded, so
  the assertion would fail on a correct build and pass for the wrong reason once
  the box happened to fit. The region printed here is the largest axis-aligned
  rectangle that the depth buffer proves is occluded, so the claim is about a
  region that is occluded by construction.
"""
import re
import struct
import sys

# How much nearer than the silhouette's own projected depth the world surface has
# to be before the pixel counts as occluded. 0.004 is about one 8-bit depth step
# (1/255) plus a margin, and far smaller than the separation between a player and
# a wall in front of it.
DEPTH_EPSILON = 0.004


def parse_args(argv):
    args = {"mode": None, "trace": None, "depth": None, "on": None, "off": None,
            "frame": "captured"}
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
        elif item == "--attribute":
            args["mode"] = "attribute"
        elif item == "--extent":
            args["mode"] = "extent"
        elif item == "--trace":
            pending = "--trace"
        elif item == "--depth":
            pending = "--depth"
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
    r"(?: extent=(\d+) aabb=(-?\d+),(-?\d+),(-?\d+),(-?\d+))?")


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
                "aabb": (int(match.group(11)), int(match.group(12)),
                         int(match.group(13)), int(match.group(14)))
                        if match.group(11) is not None else None,
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


def depth_at(frame, x, y):
    pixels, width, height, bpp, flip, stride = frame
    if x < 0 or y < 0 or x >= width or y >= height:
        return None
    row = y if flip else (height - 1 - y)
    offset = row * stride + x * bpp
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
        if not args["depth"]:
            raise SystemExit("--occluded needs --depth <world-depth bmp>")
        frame = read_depth_bmp(args["depth"])
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
