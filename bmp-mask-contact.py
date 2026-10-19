#!/usr/bin/env python3
"""bmp-mask-contact.py -- where a mask is, and whether it is still touching another.

WHY THIS EXISTS
  T22's claim is about placement, not about counters: "the posed hand is still on
  the weapon". A bounding box cannot say that. The hand can move inside a box that
  contains the whole ViewModel and the box reports nothing; the same box can also
  cover the weapon, so "the hand is inside the box" is true of a hand that has
  left the grip entirely. T18 recorded exactly this shape of mistake: a healthy
  pose box around a mesh the shader had thrown across the screen.

  So this tool answers three questions about a MASK, where the mask is a pair of
  frames that differ because one material was forced onto one range:

    maskPixels      how many pixels the mask owns
    bbox, centroid  where those pixels are, in top-left origin coordinates
    nearFraction    what share of them lie within R pixels of a second mask

  The second mask is the weapon body's coverage -- the pixels where the body is
  drawn, measured as the difference between a body-on and a body-off frame of the
  same tick. `nearFraction` is then the picture's own statement of "the hand is
  in contact with the weapon": a hand that leaves the grip keeps its pixel count
  and loses its contact, and this is the number that moves when it does.

  Distances are Chebyshev (a square of side 2R+1), which is the same neighbourhood
  a 2-D screen-space contact test uses. The dilation is done with bytearray
  operations rather than per-pixel Python, because a 1264x681 frame is 861k
  pixels and a per-pixel BFS costs more than the capture did.

Usage:
  bmp-mask-contact.py --mask A.bmp B.bmp [--body C.bmp D.bmp] [--radius N]
                      [--region x,y,w,h]

  --mask    two frames; the mask is every pixel where they differ
  --body    two frames; the reference mask (optional; without it near=none)
  --radius  contact distance in pixels (default 2)
  --region  restrict both masks to this rectangle (top-left origin)

Exit codes:
  0  the reading was produced
  1  a mask came out empty (the frame pair differs nowhere -- not a reading)
  2  the input could not be read (missing file, not a 24/32-bit BI_RGB BMP)
"""
import struct
import sys

EXIT_OK, EXIT_EMPTY, EXIT_ERROR = 0, 1, 2


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
        raise ValueError("%s uses compression %d, not BI_RGB" % (compression, compression))
    if width <= 0 or height <= 0:
        raise ValueError("%s has a non-positive extent %dx%d" % (path, width, height))
    if offset > len(data):
        raise ValueError("%s declares a pixel offset past the end of the file" % path)
    # BMP stores rows bottom-up unless the height is negative. The caller only
    # needs a top-left origin, so `top_down` says whether the first stored row is
    # the top one; every row slice below is taken in STORED order and mapped back.
    return data[offset:], width, height, bits // 8, height < 0


class Frame:
    def __init__(self, pixels, width, height, bpp, top_down):
        self.pixels = pixels
        self.width = width
        self.height = height
        self.bpp = bpp
        self.top_down = top_down
        self.stride = width * bpp

    def row(self, y):
        """Row y in TOP-LEFT origin coordinates."""
        stored = y if self.top_down else self.height - 1 - y
        base = stored * self.stride
        return self.pixels[base:base + self.stride]

    def pixel(self, row, x):
        return row[x * self.bpp:(x + 1) * self.bpp]


def load(path):
    return Frame(*read_frame(path))


def mask_of(left, right, region):
    if (left.width, left.height, left.bpp) != (right.width, right.height, right.bpp):
        raise ValueError("geometry differs between %s and %s" % (left_name, right_name))
    x0, y0, w, h = region
    mask = []
    for y in range(y0, y0 + h):
        a = left.row(y)
        b = right.row(y)
        out = bytearray(w)
        for x in range(x0, x0 + w):
            if left.pixel(a, x) != right.pixel(b, x):
                out[x - x0] = 1
        mask.append(out)
    return mask


def stats(mask, width, height):
    count = 0
    sum_x = 0
    sum_y = 0
    min_x, min_y, max_x, max_y = width, height, -1, -1
    for y in range(height):
        row = mask[y]
        row_count = 0
        for x in range(width):
            if row[x]:
                row_count += 1
                sum_x += x
                sum_y += y
                if x < min_x:
                    min_x = x
                if x > max_x:
                    max_x = x
                if y < min_y:
                    min_y = y
                if y > max_y:
                    max_y = y
        count += row_count
    if count == 0:
        return 0, (0, 0, -1, -1), (0.0, 0.0)
    return count, (min_x, min_y, max_x, max_y), (sum_x / count, sum_y / count)


def dilate(mask, width, height, radius):
    """Square (Chebyshev) dilation by `radius`, in the mask's own coordinates."""
    current = mask
    for _ in range(radius):
        spread = []
        for y in range(height):
            row = current[y]
            out = bytearray(row)
            for x in range(width):
                if not row[x] and ((x > 0 and row[x - 1]) or (x + 1 < width and row[x + 1])):
                    out[x] = 1
            spread.append(out)
        grown = []
        for y in range(height):
            out = bytearray(spread[y])
            if y > 0:
                above = spread[y - 1]
                for x in range(width):
                    if above[x]:
                        out[x] = 1
            if y + 1 < height:
                below = spread[y + 1]
                for x in range(width):
                    if below[x]:
                        out[x] = 1
            grown.append(out)
        current = grown
    return current


def crop(mask, region, box):
    """Cut `box` (in region coordinates) out of `mask`; box is x0,y0,x1,y1."""
    x0, y0, x1, y1 = box
    out = []
    for y in range(y0, y1):
        out.append(mask[y][x0:x1])
    return out


def contact_distance(mask, body_mask, width, height, limit):
    """Smallest Chebyshev radius at which the mask meets the body, or -1.

    0 means the two masks already share a pixel (one is drawn over the other);
    1 means they touch along an edge. A hand that has left the grip does not
    produce a small number -- it produces none at all within `limit`, which is
    the reading this exists to be able to give.
    """
    mask_box = stats(mask, width, height)[1]
    body_box = stats(body_mask, width, height)[1]
    if mask_box[2] < 0 or body_box[2] < 0:
        return -1
    x0 = max(0, min(mask_box[0], body_box[0]) - limit)
    y0 = max(0, min(mask_box[1], body_box[1]) - limit)
    x1 = min(width, max(mask_box[2], body_box[2]) + limit + 1)
    y1 = min(height, max(mask_box[3], body_box[3]) + limit + 1)
    local_mask = crop(mask, None, (x0, y0, x1, y1))
    local_body = crop(body_mask, None, (x0, y0, x1, y1))
    lw, lh = x1 - x0, y1 - y0
    for radius in range(0, limit + 1):
        grown = local_body if radius == 0 else dilate(local_body, lw, lh, 1)
        local_body = grown
        for y in range(lh):
            row = local_mask[y]
            grow_row = grown[y]
            for x in range(lw):
                if row[x] and grow_row[x]:
                    return radius
    return -1


def main(argv):
    args = argv[1:]
    mask_pair = None
    body_pair = None
    radius = 2
    max_distance = 8
    region = None
    index = 0
    while index < len(args):
        token = args[index]
        if token in ("--mask", "--body"):
            if index + 2 >= len(args):
                print("BMP-CONTACT status=ERROR reason=%s wants two frame paths" % token)
                return EXIT_ERROR
            pair = (args[index + 1], args[index + 2])
            if token == "--mask":
                mask_pair = pair
            else:
                body_pair = pair
            index += 3
            continue
        if token in ("--radius", "--region", "--max-distance"):
            if index + 1 >= len(args):
                print("BMP-CONTACT status=ERROR reason=%s needs a value" % token)
                return EXIT_ERROR
            try:
                if token == "--radius":
                    radius = int(args[index + 1])
                elif token == "--max-distance":
                    max_distance = int(args[index + 1])
                else:
                    parts = [int(v) for v in args[index + 1].split(",")]
                    if len(parts) != 4:
                        raise ValueError
                    region = tuple(parts)
            except ValueError:
                print("BMP-CONTACT status=ERROR reason=%s has a malformed value" % token)
                return EXIT_ERROR
            index += 2
            continue
        print("BMP-CONTACT status=ERROR reason=unexpected argument %s" % token)
        return EXIT_ERROR

    if mask_pair is None:
        print("BMP-CONTACT status=ERROR reason=--mask A.bmp B.bmp is required")
        return EXIT_ERROR

    global left_name, right_name
    try:
        left = load(mask_pair[0])
        right = load(mask_pair[1])
        left_name, right_name = mask_pair
        if region is None:
            region = (0, 0, left.width, left.height)
        if region[0] < 0 or region[1] < 0 or region[0] + region[2] > left.width \
                or region[1] + region[3] > left.height:
            raise ValueError("region %s is outside the %dx%d frame"
                             % (region, left.width, left.height))
        mask = mask_of(left, right, region)
        body_mask = None
        if body_pair is not None:
            body_left = load(body_pair[0])
            body_right = load(body_pair[1])
            if (body_left.width, body_left.height, body_left.bpp) != (left.width, left.height, left.bpp):
                raise ValueError("the body pair's geometry differs from the mask pair's")
            left_name, right_name = body_pair
            body_mask = mask_of(body_left, body_right, region)
    except ValueError as error:
        print("BMP-CONTACT status=ERROR reason=%s" % error)
        return EXIT_ERROR

    width, height = region[2], region[3]
    count, box, centroid = stats(mask, width, height)
    if count == 0:
        print("BMP-CONTACT maskPixels=0 status=EMPTY reason=the frame pair differs nowhere")
        return EXIT_EMPTY

    body_count = 0
    body_box = (0, 0, -1, -1)
    near = 0
    distance = -1
    if body_mask is not None:
        body_count, body_box, _ = stats(body_mask, width, height)
        if body_count:
            grown = dilate(body_mask, width, height, radius)
            for y in range(height):
                row = mask[y]
                grown_row = grown[y]
                for x in range(width):
                    if row[x] and grown_row[x]:
                        near += 1
            distance = contact_distance(mask, body_mask, width, height, max_distance)

    fraction = near / count if body_count else 0.0
    print(
        "BMP-CONTACT maskPixels=%d maskBbox=(x=%d..%d, y=%d..%d) w=%d h=%d "
        "centroid=(%.2f,%.2f) bodyPixels=%d bodyBbox=(x=%d..%d, y=%d..%d) "
        "near=%d nearFraction=%.4f outsideBody=%d contactDistance=%d radius=%d "
        "region=%d,%d,%d,%d status=OK"
        % (count, box[0] + region[0], box[2] + region[0], box[1] + region[1], box[3] + region[1],
           box[2] - box[0] + 1, box[3] - box[1] + 1,
           centroid[0] + region[0], centroid[1] + region[1],
           body_count, body_box[0] + region[0], body_box[2] + region[0],
           body_box[1] + region[1], body_box[3] + region[1],
           near, fraction, count - near, distance, radius,
           region[0], region[1], region[2], region[3]))
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv))
