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
  --occluded       the part of those boxes that the frame's own depth says is
                   BEHIND world geometry, as one box; empty when nothing is

  --occluded needs --depth (the same tick's capture). The depth it compares
  against is read out of the BMP the renderer wrote with --world-depth, so a
  wall that is not where the trace thinks it is cannot be assumed away.
"""
import re
import struct
import sys


def parse_args(argv):
    args = {"mode": None, "trace": None, "depth": None}
    for item in argv[1:]:
        if item == "--csv":
            args["mode"] = "csv"
        elif item == "--team":
            args["mode"] = "team"
        elif item == "--occluded":
            args["mode"] = "occluded"
        elif item.startswith("--trace="):
            args["trace"] = item.split("=", 1)[1]
        elif item.startswith("--depth="):
            args["depth"] = item.split("=", 1)[1]
        else:
            raise SystemExit("unknown argument: " + item)
    return args


# One row per drawn silhouette. The format is fixed by the product:
#   outline: item entity=12 team=red depth=xray x0=.. y0=.. x1=.. y1=.. depth=0.53 onScreen=1
ITEM = re.compile(
    r"outline: item entity=(\d+) team=(\w+) depth=(\w+) "
    r"x0=(-?\d+) y0=(-?\d+) x1=(-?\d+) y1=(-?\d+) depth=([0-9.]+) onScreen=([01])")


def read_items(path):
    items = []
    with open(path, "r", errors="replace") as handle:
        for line in handle:
            match = ITEM.search(line)
            if not match:
                continue
            items.append({
                "entity": int(match.group(1)),
                "team": match.group(2),
                "mode": match.group(3),
                "x0": int(match.group(4)),
                "y0": int(match.group(5)),
                "x1": int(match.group(6)),
                "y1": int(match.group(7)),
                "depth": float(match.group(8)),
                "onScreen": match.group(9) == "1",
            });
    return items


def area(item):
    return max(0, item["x1"] - item["x0"] + 1) * max(0, item["y1"] - item["y0"] + 1)


def read_depth_bmp(path):
    """The --world-depth capture is greyscale: near is bright, far is dark.

    The renderer writes depth as an 8-bit value; what the occluded-region
    derivation needs is only a COMPARISON with the overlay item's own projected
    depth, so the reading is the pixel value normalised to 0..1 the same way the
    renderer packed it.
    """
    data = open(path, "rb").read()
    if data[0:2] != b"BM":
        raise SystemExit("not a BMP: " + path)
    off = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0] // 8
    flip = height < 0
    height = abs(height)
    stride = width * bpp
    return data[off:], width, height, bpp, flip, stride


def depth_at(frame, x, y):
    pixels, width, height, bpp, flip, stride = frame
    row = y if flip else (height - 1 - y)
    offset = row * stride + x * bpp
    return pixels[offset] / 255.0


def occluded_box(items, frame):
    """The part of the players' boxes that the world depth says is IN FRONT.

    A silhouette pixel is occluded when the frame's own depth at that pixel is
    strictly nearer than the silhouette's projected depth: that is the wall
    standing between the camera and the player. The box returned bounds exactly
    those pixels, so the depth-tested control is judged on the region a wall
    actually covers rather than on a neighbourhood around it.
    """
    minx, miny, maxx, maxy = 10 ** 9, 10 ** 9, -1, -1
    for item in items:
        for y in range(item["y0"], item["y1"] + 1):
            for x in range(item["x0"], item["x1"] + 1):
                world = depth_at(frame, x, y)
                # 0.004 is comfortably larger than the depth write's own
                # quantisation (8 bits over the 0..1 range) and far smaller than
                # the separation between a player and the wall in front of it.
                if world < item["depth"] - 0.004:
                    minx = min(minx, x)
                    maxx = max(maxx, x)
                    miny = min(miny, y)
                    maxy = max(maxy, y)
    if maxx < 0:
        return ""
    return "%d,%d,%d,%d" % (minx, miny, maxx, maxy)


def main(argv):
    args = parse_args(argv)
    if not args["mode"] or not args["trace"]:
        raise SystemExit(__doc__)
    items = read_items(args["trace"])
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
        box = occluded_box(on_screen, frame)
        if box:
            print(box)
        return 0

    raise SystemExit("no mode selected")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
