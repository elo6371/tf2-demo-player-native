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
    args = {"mode": None, "trace": None, "depth": None, "frame": "captured"}
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
        if item == "--csv":
            args["mode"] = "csv"
        elif item == "--team":
            args["mode"] = "team"
        elif item == "--occluded":
            args["mode"] = "occluded"
        elif item == "--trace":
            pending = "--trace"
        elif item == "--depth":
            pending = "--depth"
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
ITEM = re.compile(
    r"outline: item entity=(\d+) team=(\w+) depth=(\w+) "
    r"x0=(-?\d+) y0=(-?\d+) x1=(-?\d+) y1=(-?\d+) depth=([0-9.]+) onScreen=([01])")


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


def main(argv):
    args = parse_args(argv)
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
