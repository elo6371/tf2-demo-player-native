#!/usr/bin/env python3
"""hole-point-verify.py -- T26: does the rasterizer agree with the point oracle?

WHY THIS FILE EXISTS
--------------------
`--world-hole-trace` projects the map's triangles itself and reports which face
owns a screen point. That is a CPU claim about a GPU picture, and this project
has one rule about such claims: they are not accepted until a second, independent
instrument says the same thing. The second instrument is the face-id frame
(`--world-geometry sky --world-face-id`), where the pixel's colour IS the face
index written by the rasterizer.

Three statements are checked per point, and each can fail on its own:

  drawn     the point trace must report no drawn world triangle -- otherwise the
            point was never empty and the accounting's missing mask is wrong;
  id        the face-id frame at that pixel must decode to the same sky face the
            trace names. A mismatch means the projection, the transform or the
            buffer differs between the two instruments;
  empty     the captured depth buffer at that pixel must be the far plane. A
            point the oracle calls empty and the depth buffer calls occupied is
            a reading that contradicts itself.

The face id is written by the shader as r = id&0xFF, g = (id>>8)&0xFF,
b = (id>>16)&0xFF, and the BMP the capture writes holds the target's bytes in
B,G,R order -- measured on the flat frames, where the shader's (0.35,0.62,0.38)
lands in the file as (97,158,89). So the id is rebuilt from the THIRD byte
down: `pixels[at+2] | pixels[at+1]<<8 | pixels[at]<<16`. Decoding it in file
order instead produces a number in the millions for a map with 18,052 faces,
which is how the first run of this tool announced the mistake.

`--depth` is accepted for a caller that has a frame able to answer the
question, but the gate does not pass it: this build's depth debug view returns
`position.z / position.w`, which the 8-bit target saturates to 255 for every
surface beyond a few units (measured: `sky-depth` is one value, 255, for all
860,783 covered pixels). A comparison against a saturated frame is a
comparison of two constants, so the far-plane test above is used instead.

The id frame is drawn with the cull off and no pvs clip on purpose: the sky
buffer is a closed hull seen from inside, so half of it faces away, and the mask
must not depend on a winding rule that does not apply to a sky brush.

Usage:
  hole-point-verify.py --trace <trace> --ids <sky-ids.bmp> [--depth <sky-depth.bmp>]
      [--max-depth-delta 0.005]

Exit: 0 = every point agreed with both frames; 1 = at least one did not; 2 = a
malformed input.
"""
import re
import struct
import sys

EXIT_OK, EXIT_ERROR, EXIT_MISMATCH = 0, 2, 1
TRACE_PATTERN = re.compile(
    r"gpu: world hole-trace point=(\d+),(\d+) drawn=(\S+) any=(\S+) sky=(.*?) frameDepth=([-\d.]+)")
SKY_PATTERN = re.compile(r"face=(-?\d+) material=(\S+) tri=(\d+) depth=([-\d.]+)")


class Frame:
    __slots__ = ("width", "height", "pixels")

    def __init__(self, width, height, pixels):
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
    flip = height > 0
    height = abs(height)
    stride = ((width * 3) + 3) & ~3
    out = bytearray(width * height * 3)
    for y in range(height):
        source = (height - 1 - y) if flip else y
        start = offset + source * stride
        out[y * width * 3:(y + 1) * width * 3] = data[start:start + width * 3]
    return Frame(width, height, out)


def main(argv):
    args = argv[1:]
    trace_path = None
    ids_path = None
    depth_path = None
    max_depth_delta = 0.005
    index = 0
    while index < len(args):
        token = args[index]
        if token == "--trace" and index + 1 < len(args):
            trace_path = args[index + 1]; index += 2
        elif token == "--ids" and index + 1 < len(args):
            ids_path = args[index + 1]; index += 2
        elif token == "--depth" and index + 1 < len(args):
            depth_path = args[index + 1]; index += 2
        elif token == "--max-depth-delta" and index + 1 < len(args):
            max_depth_delta = float(args[index + 1]); index += 2
        else:
            print("HOLE-POINT status=ERROR reason=unknown argument %s" % token)
            return EXIT_ERROR
    if trace_path is None or ids_path is None:
        print("HOLE-POINT status=ERROR reason=--trace and --ids are required")
        return EXIT_ERROR
    try:
        ids = read_bmp(ids_path)
        depth = read_bmp(depth_path) if depth_path else None
    except ValueError as error:
        print("HOLE-POINT status=ERROR reason=%s" % error)
        return EXIT_ERROR

    points = []
    try:
        with open(trace_path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                match = TRACE_PATTERN.search(line.strip())
                if match:
                    points.append({
                        "x": int(match.group(1)), "y": int(match.group(2)),
                        "drawn": match.group(3), "sky": match.group(5).strip(),
                        "frame_depth": float(match.group(6)),
                    })
    except OSError as error:
        print("HOLE-POINT status=ERROR reason=%s" % error)
        return EXIT_ERROR
    if not points:
        print("HOLE-POINT status=ERROR reason=the trace carries no hole-trace points")
        return EXIT_ERROR

    failures = 0
    for point in points:
        x, y = point["x"], point["y"]
        if x < 0 or y < 0 or x >= ids.width or y >= ids.height:
            print("HOLE-POINT point=%d,%d status=OUT-OF-FRAME" % (x, y))
            failures += 1
            continue
        at = (y * ids.width + x) * 3
        # The file holds B,G,R (see the header); the shader wrote r = id&0xFF.
        id_face = ids.pixels[at + 2] | (ids.pixels[at + 1] << 8) | (ids.pixels[at] << 16)
        sky_match = SKY_PATTERN.search(point["sky"]) if point["sky"] != "-" else None
        if sky_match is None:
            print("HOLE-POINT point=%d,%d status=NO-SKY face=%d drawn=%s"
                  % (x, y, id_face, point["drawn"]))
            failures += 1
            continue
        trace_face = int(sky_match.group(1))
        trace_depth = float(sky_match.group(4))
        drawn_ok = point["drawn"] == "-"
        id_ok = id_face == trace_face
        depth_ok = True
        depth_text = "-"
        if depth is not None:
            dat = (y * depth.width + x) * 3
            frame_depth = depth.pixels[dat] / 255.0
            depth_text = "%.4f" % frame_depth
            depth_ok = abs(frame_depth - trace_depth) <= max_depth_delta
        empty_ok = abs(point["frame_depth"] - 1.0) <= 1.0e-4
        status = "OK" if (drawn_ok and id_ok and depth_ok and empty_ok) else "MISMATCH"
        print("HOLE-POINT point=%d,%d status=%s drawn=%s skyFace=%d idFace=%d idMatch=%d"
              " skyDepth=%.4f depthFrame=%s depthMatch=%d frameDepth=%.4f empty=%d"
              % (x, y, status, point["drawn"], trace_face, id_face, 1 if id_ok else 0,
                 trace_depth, depth_text, 1 if depth_ok else 0, point["frame_depth"],
                 1 if empty_ok else 0))
        if status != "OK":
            failures += 1
    print("HOLE-POINT-SUMMARY points=%d failures=%d ids=%dx%d depth=%s"
          % (len(points), failures, ids.width, ids.height, depth_path or "-"))
    return EXIT_OK if failures == 0 else EXIT_MISMATCH


if __name__ == "__main__":
    sys.exit(main(sys.argv))
