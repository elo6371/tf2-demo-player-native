#!/usr/bin/env python3
"""world-geometry-hole-account.py -- T26: is an empty pixel a hole or the sky?

WHY THIS FILE EXISTS
--------------------
T23 measured that 38,328 of the 860,784 pixels in the reference frame have no
world triangle along them, even with the back-face cull and the pvs clip both
off, and that all but 2,313 of those belong to one component that touches the
top edge. What it could not say is whether the empty pixels are *the map's sky*
or *geometry that is missing*: the sky box fills every pixel the world pass does
not, so the finished frame is sky-coloured either way, and a colour test cannot
separate the two.

The separation is a different question asked of the same camera:

    --world-geometry sky --world-flat --no-world-cull

draws the BSP's sky-brush polygons -- the map's own statement of where sky is
meant to be -- in the same flat colour the world pass uses, through the same
projection and the same world transform. A missing pixel that is flat in that
frame has a sky brush along it: the map intended sky there. A missing pixel that
is not has no world triangle and no sky brush, and that is the only population
this task is allowed to call a hole.

WHAT IT DOES NOT PROVE
----------------------
Coverage is a fill test, not a depth test. A sky brush *behind* a drawn surface
and a sky brush *in front* of one are the same flat pixel here; the `--sky-depth`
frame and the world depth frame are the pair that separates those, and this tool
reports the count of drawn pixels the sky mask also covers so the pair can be
read. `sky_on_drawn` above zero is not automatically a defect -- it is a
question that needs the depth control, and the gate names it as one.

Usage:
  world-geometry-hole-account.py --dir evidence/t26-world-holes/captures \
      [--out evidence/t26-world-holes/analysis] [--dark 16] [--points 12]

Exit: 0 = the report was produced; 2 = an input frame is missing or malformed.
"""
import collections
import os
import struct
import sys

EXIT_OK, EXIT_ERROR = 0, 2
FRAME_SUFFIXES = ("base", "novis", "nocull", "novis-nocull")
# The four regions the pinned visual baseline judges. They are repeated here so
# a region that gains an unexplained hole is named by the same rectangle the
# other gates use.
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
        self.pixels = pixels


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


def mode_colour(pixels):
    counts = collections.Counter()
    for at in range(0, len(pixels), 3 * 7):
        counts[(pixels[at], pixels[at + 1], pixels[at + 2])] += 1
    return counts.most_common(1)[0][0]


def match_mask(frame, colour):
    """1 where the pixel is exactly `colour`."""
    r, g, b = colour
    out = bytearray(frame.width * frame.height)
    pixels = frame.pixels
    for index in range(len(out)):
        at = index * 3
        if pixels[at] == r and pixels[at + 1] == g and pixels[at + 2] == b:
            out[index] = 1
    return out


def luminance(pixels, at):
    return 0.299 * pixels[at] + 0.587 * pixels[at + 1] + 0.114 * pixels[at + 2]


def components(mask, width, height):
    """Connected components of a 1-mask, 4-neighbour, top-down. Keeps every
    member so a representative pixel can be named for each one."""
    seen = bytearray(len(mask))
    result = []
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        queue = collections.deque([start])
        seen[start] = 1
        members = []
        min_x = width
        max_x = 0
        min_y = height
        max_y = 0
        while queue:
            index = queue.popleft()
            members.append(index)
            y = index // width
            x = index - y * width
            if x < min_x:
                min_x = x
            if x > max_x:
                max_x = x
            if y < min_y:
                min_y = y
            if y > max_y:
                max_y = y
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if nx < 0 or ny < 0 or nx >= width or ny >= height:
                    continue
                nindex = ny * width + nx
                if mask[nindex] and not seen[nindex]:
                    seen[nindex] = 1
                    queue.append(nindex)
        result.append({
            "area": len(members), "min_x": min_x, "max_x": max_x,
            "min_y": min_y, "max_y": max_y, "members": members,
        })
    result.sort(key=lambda item: item["area"], reverse=True)
    return result


def representative_points(members, width, height, limit):
    """Up to `limit` pixels inside a component, spread along its scan order.

    A centroid can land outside a concave blob; taking the median of the
    component's own pixels cannot. Three interior points rather than one give
    the point trace a chance to disagree with itself, which is the only way a
    single sampled pixel can be trusted.
    """
    if not members:
        return []
    picks = []
    for fraction in (0.25, 0.5, 0.75):
        index = members[min(len(members) - 1, int(len(members) * fraction))]
        y = index // width
        x = index - y * width
        if (x, y) not in picks:
            picks.append((x, y))
        if len(picks) >= limit:
            break
    return picks


def main(argv):
    args = argv[1:]
    directory = "evidence/t26-world-holes/captures"
    out_dir = "evidence/t26-world-holes/analysis"
    dark_limit = 16
    point_budget = 12
    index = 0
    while index < len(args):
        token = args[index]
        if token == "--dir" and index + 1 < len(args):
            directory = args[index + 1]; index += 2
        elif token == "--out" and index + 1 < len(args):
            out_dir = args[index + 1]; index += 2
        elif token == "--dark" and index + 1 < len(args):
            dark_limit = int(args[index + 1]); index += 2
        elif token == "--points" and index + 1 < len(args):
            point_budget = int(args[index + 1]); index += 2
        else:
            print("HOLE status=ERROR reason=unknown argument %s" % token)
            return EXIT_ERROR
    if not os.path.isdir(directory):
        print("HOLE status=ERROR reason=%s is not a directory" % directory)
        return EXIT_ERROR
    os.makedirs(out_dir, exist_ok=True)

    names = ["textured", "none", "sky-faces"]
    names += ["flat-%s" % suffix for suffix in FRAME_SUFFIXES]
    frames = {}
    try:
        for name in names:
            frames[name] = read_bmp(os.path.join(directory, name + ".bmp"))
    except ValueError as error:
        print("HOLE status=ERROR reason=%s" % error)
        return EXIT_ERROR
    sky_depth = None
    sky_depth_path = os.path.join(directory, "sky-depth.bmp")
    if os.path.exists(sky_depth_path):
        try:
            sky_depth = read_bmp(sky_depth_path)
        except ValueError as error:
            print("HOLE status=ERROR reason=%s" % error)
            return EXIT_ERROR
    world_ids = None
    world_ids_path = os.path.join(directory, "world-ids.bmp")
    if os.path.exists(world_ids_path):
        try:
            world_ids = read_bmp(world_ids_path)
        except ValueError as error:
            print("HOLE status=ERROR reason=%s" % error)
            return EXIT_ERROR
    # The cull-off id frame. A sliver is frequently bounded on one side by a
    # surface the product culls, and that surface has no id in the cull-on frame
    # (the skybox colour is there instead, which decodes to a face index in the
    # millions). Naming it needs this frame.
    world_ids_nocull = None
    world_ids_nocull_path = os.path.join(directory, "world-ids-nocull.bmp")
    if os.path.exists(world_ids_nocull_path):
        try:
            world_ids_nocull = read_bmp(world_ids_nocull_path)
        except ValueError as error:
            print("HOLE status=ERROR reason=%s" % error)
            return EXIT_ERROR

    base = frames["flat-base"]
    width, height = base.width, base.height
    for name, frame in frames.items():
        if (frame.width, frame.height) != (width, height):
            print("HOLE status=ERROR reason=%s is %dx%d, expected %dx%d"
                  % (name, frame.width, frame.height, width, height))
            return EXIT_ERROR

    flat_colour = mode_colour(base.pixels)
    print("HOLE status=ok dir=%s size=%dx%d flat_colour=(%d,%d,%d)"
          % (directory, width, height, flat_colour[0], flat_colour[1], flat_colour[2]))

    present_all_off = match_mask(frames["flat-novis-nocull"], flat_colour)
    present_base = match_mask(frames["flat-base"], flat_colour)
    sky_mask = match_mask(frames["sky-faces"], flat_colour)
    total = width * height

    # T23's 38,328 excludes the overlay: the `none` frame is the frame with no
    # world pass at all, so every pixel that is not its clear colour is the HUD
    # or an entity, and no world triangle was ever going to cover it. Counting
    # those as missing would add ~24k invented holes and the two tasks would be
    # reporting different populations under the same word.
    clear_colour = mode_colour(frames["none"].pixels)
    overlay = bytearray(total)
    none_pixels = frames["none"].pixels
    for index in range(total):
        at = index * 3
        if (none_pixels[at] != clear_colour[0] or none_pixels[at + 1] != clear_colour[1]
                or none_pixels[at + 2] != clear_colour[2]):
            overlay[index] = 1
    print("HOLE background clear_colour=(%d,%d,%d) overlay=%d"
          % (clear_colour[0], clear_colour[1], clear_colour[2], sum(overlay)))

    missing = bytearray(1 if (not present_all_off[index]) and not overlay[index] else 0
                        for index in range(total))
    missing_count = sum(missing)
    # The sky mask is a coverage question, and the sky pass draws under neither
    # the overlay nor the pvs clip: restrict it to the pixels the world could
    # have drawn at all, or the HUD rectangle reads as "sky behind the HUD".
    for index in range(total):
        if overlay[index]:
            sky_mask[index] = 0
    sky_count = sum(sky_mask)
    print("HOLE masks pixels=%d missing=%d skyFaceCoverage=%d"
          % (total, missing_count, sky_count))

    # The sky pass is drawn with the cull off and no pvs clip, so it can cover a
    # pixel the world pass also fills. That population is not a hole and not
    # automatically a defect -- it is the pair the depth control has to explain.
    sky_on_missing = 0
    sky_on_drawn = 0
    unexplained = 0
    for index in range(total):
        if sky_mask[index]:
            if present_base[index]:
                sky_on_drawn += 1
            elif missing[index]:
                sky_on_missing += 1
    for index in range(total):
        if missing[index] and not sky_mask[index]:
            unexplained += 1
    print("HOLE totals missing=%d sky=%d unexplained=%d skyOnDrawn=%d skyOnDrawnUnmeasured=%d"
          % (missing_count, sky_on_missing, unexplained, sky_on_drawn, sky_on_drawn))

    # The mask's own reach has to be stated before its verdict is quoted. A
    # sealed Source map puts sky brushes around the whole play space, so from
    # inside it every direction ends on a sky face and the mask covers the
    # frame: that is a real property of the map, but it also means the mask
    # cannot separate "a crack between two brushes" from "the open sky above
    # the map". Both are sky-backed. The number is printed, the two cases are
    # told apart by shape and by the bounding faces instead.
    coverage_share = sky_count / float(max(1, total))
    degenerate = 1 if coverage_share >= 0.99 else 0
    print("HOLE maskReach coverage=%d share=%.5f degenerate=%d"
          % (sky_count, coverage_share, degenerate))

    # The depth control for the population coverage cannot settle. Two limits
    # were measured on this build and both are reported rather than hidden:
    # the debug depth view is quantised to 8 bits, and with this camera's
    # near/far ratio every surface past a few units lands on 255, so the frame
    # is a constant and comparisons in it mean nothing. `saturated` says so.
    sky_behind = sky_in_front = sky_tie = 0
    saturated = 0
    if sky_depth is not None:
        levels = set()
        for index in range(0, total, 7):
            levels.add(sky_depth.pixels[index * 3])
            if len(levels) > 2:
                break
        saturated = 1 if len(levels) <= 2 else 0
        base_depth_path = os.path.join(directory, "depth-base.bmp")
        if os.path.exists(base_depth_path):
            try:
                base_depth = read_bmp(base_depth_path)
            except ValueError as error:
                print("HOLE status=ERROR reason=%s" % error)
                return EXIT_ERROR
            for index in range(total):
                if not (present_base[index] and sky_mask[index]):
                    continue
                at = index * 3
                world_level = base_depth.pixels[at]
                sky_level = sky_depth.pixels[at]
                if abs(sky_level - world_level) <= 2:
                    sky_tie += 1
                elif sky_level > world_level:
                    sky_behind += 1
                else:
                    sky_in_front += 1
    print("HOLE depthControl drawnCovered=%d skyBehind=%d skyInFront=%d tie=%d measured=%d saturated=%d"
          % (sky_on_drawn, sky_behind, sky_in_front, sky_tie,
             1 if sky_depth is not None else 0, saturated))

    # Component account. The task's number to isolate is the enclosed 2,282-pixel
    # population against the 36,015-pixel top opening, so the split that matters
    # is edge-touching versus enclosed, and sky versus unexplained inside each.
    comps = components(missing, width, height)
    enclosed_area = 0
    enclosed_unexplained = 0
    open_area = 0
    points = []
    for rank, item in enumerate(comps):
        members = item["members"]
        sky_area = sum(1 for index in members if sky_mask[index])
        unexplained_area = item["area"] - sky_area
        touches_border = (item["min_x"] == 0 or item["min_y"] == 0
                          or item["max_x"] == width - 1 or item["max_y"] == height - 1)
        box = (item["max_x"] - item["min_x"] + 1) * (item["max_y"] - item["min_y"] + 1)
        if item["area"] >= 4:
            print("HOLE component rank=%d area=%d sky=%d unexplained=%d bbox=(x=%d..%d,y=%d..%d)"
                  " edge=%d fill=%.3f"
                  % (rank, item["area"], sky_area, unexplained_area, item["min_x"], item["max_x"],
                     item["min_y"], item["max_y"], 1 if touches_border else 0,
                     item["area"] / max(1, box)))
        if touches_border:
            open_area += item["area"]
        else:
            enclosed_area += item["area"]
            enclosed_unexplained += unexplained_area
        if item["area"] >= 200:
            for x, y in representative_points(members, width, height, 3):
                if len(points) < point_budget:
                    points.append((x, y, rank, item["area"]))

    print("HOLE split open=%d enclosed=%d enclosedUnexplained=%d components=%d"
          % (open_area, enclosed_area, enclosed_unexplained, len(comps)))
    for x, y, rank, area in points:
        print("HOLE point=%d,%d component=%d area=%d" % (x, y, rank, area))
    print("HOLE points=%s" % ";".join("%d,%d" % (x, y) for x, y, _, _ in points))

    # ---- which faces bound each enclosed component ------------------------
    # The shape test says "sliver"; it cannot say between WHAT. The world
    # face-id frame names the surfaces on the other side of each sliver, and the
    # probe turns a face index into a material, a plane and a displacement flag.
    # That is the step from "there is a crack" to "the crack runs between these
    # two faces of the map", which is what the task asked for.
    seam_faces = set()
    culled_neighbours = 0
    if world_ids is not None:
        def face_at(frame, x, y):
            at = (y * frame.width + x) * 3
            # The file holds B,G,R and the shader wrote r = id&0xFF; see the
            # decode note in hole-point-verify.py.
            return (frame.pixels[at + 2] | (frame.pixels[at + 1] << 8)
                    | (frame.pixels[at] << 16))

        def scan(present, x, y, dx, dy, limit):
            for step in range(1, limit + 1):
                nx, ny = x + dx * step, y + dy * step
                if nx < 0 or ny < 0 or nx >= width or ny >= height:
                    return None
                if present[ny * width + nx]:
                    return step, nx, ny
            return None

        def neighbour(x, y, dx, dy, limit):
            """The first pixel drawn by the PRODUCT (front-facing), else the first
            pixel drawn only once the cull is off. The second case is the one
            that matters: it names a surface the product removes."""
            hit = scan(present_base, x, y, dx, dy, limit)
            if hit is not None:
                step, nx, ny = hit
                return "%d:%d" % (face_at(world_ids, nx, ny), step)
            if world_ids_nocull is None:
                return "-"
            hit = scan(present_all_off, x, y, dx, dy, limit)
            if hit is None:
                return "-"
            step, nx, ny = hit
            return "^%d:%d" % (face_at(world_ids_nocull, nx, ny), step)

        for rank, item in enumerate(comps):
            if item["area"] < 200:
                continue
            if (item["min_x"] == 0 or item["min_y"] == 0
                    or item["max_x"] == width - 1 or item["max_y"] == height - 1):
                continue
            for x, y in representative_points(item["members"], width, height, 2):
                parts = []
                for label, dx, dy in (("left", -1, 0), ("right", 1, 0),
                                      ("up", 0, -1), ("down", 0, 1)):
                    text = neighbour(x, y, dx, dy, 64)
                    parts.append("%s=%s" % (label, text))
                    if text[0].isdigit():
                        seam_faces.add(int(text.split(":")[0]))
                    elif text.startswith("^"):
                        seam_faces.add(int(text[1:].split(":")[0]))
                        culled_neighbours += 1
                print("HOLE seam rank=%d point=%d,%d %s" % (rank, x, y, " ".join(parts)))
    print("HOLE seamFaces count=%d ids=%s"
          % (len(seam_faces), ",".join(str(value) for value in sorted(seam_faces))))
    # A direction that only the cull-off frame could name is a surface the
    # product removes: the sliver is its silhouette, and the empty pixel survives
    # the cull being off, so culling is not what made the pixel empty.
    print("HOLE seamCulled directions=%d" % culled_neighbours)

    # Region account: the pinned rectangles, so a hole inside W1-W4 is named by
    # the rectangle the visual baseline already judges.
    for label, rx, ry, rw, rh in REGIONS:
        rx = max(0, rx); ry = max(0, ry)
        rw = min(rw, width - rx); rh = min(rh, height - ry)
        region_missing = region_sky = region_unexplained = region_drawn = 0
        for yy in range(ry, ry + rh):
            row = yy * width
            for xx in range(rx, rx + rw):
                at = row + xx
                if present_base[at]:
                    region_drawn += 1
                if missing[at]:
                    region_missing += 1
                    if sky_mask[at]:
                        region_sky += 1
                    else:
                        region_unexplained += 1
        print("HOLE region %s x=%d,y=%d,w=%d,h=%d drawn=%d missing=%d sky=%d unexplained=%d"
              % (label, rx, ry, rw, rh, region_drawn, region_missing, region_sky, region_unexplained))

    # How dark is what the frame actually draws, restated here because T26 must
    # not report a dark surface as a hole: the population is drawn, not empty.
    textured = frames["textured"]
    drawn_dark = 0
    for index in range(total):
        if present_base[index] and luminance(textured.pixels, index * 3) < dark_limit:
            drawn_dark += 1
    print("HOLE dark drawn=%d dark=%d limit=%d" % (sum(present_base), drawn_dark, dark_limit))

    # Cause map: the missing population split by its attribution, the drawn
    # population in green for context, and the enclosed unexplained pixels in
    # white -- the only pixels in this frame that are a hole in the strict sense.
    palette = [
        (210, 40, 40),    # 0 unexplained missing (a hole)
        (30, 150, 60),    # 1 drawn
        (150, 150, 150),  # 2 other (not drawn, not missing: back-face/visibility)
        (40, 170, 220),   # 3 missing covered by a sky face (sky by design)
        (255, 255, 255),  # 4 enclosed unexplained (a hole, framed by geometry)
        (70, 70, 110),    # 5 overlay / not accounted
    ]
    image = bytearray(total)
    for index in range(total):
        if overlay[index]:
            image[index] = 5
        elif present_base[index]:
            image[index] = 1
        elif missing[index]:
            image[index] = 3 if sky_mask[index] else 0
        else:
            image[index] = 2
    for item in comps:
        if item["area"] < 200:
            continue
        touches_border = (item["min_x"] == 0 or item["min_y"] == 0
                          or item["max_x"] == width - 1 or item["max_y"] == height - 1)
        if touches_border:
            continue
        for index in item["members"]:
            if missing[index] and not sky_mask[index]:
                image[index] = 4
    write_bmp(os.path.join(out_dir, "hole-mask.bmp"), width, height, image, palette)
    print("HOLE mask=%s" % os.path.join(out_dir, "hole-mask.bmp"))
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv))
