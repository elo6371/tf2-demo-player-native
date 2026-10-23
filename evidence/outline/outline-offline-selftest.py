
#!/usr/bin/env python3
"""outline-offline-selftest.py -- prove the gate's pixel plumbing can go red.

WHY THIS EXISTS
  Every assertion in outline-check.sh that reads a NUMBER out of
  outline-frame-check.py or outline-regions.py is only worth the failure it can
  report. A reader that always prints the same number, or a region parser that
  silently returns an empty list, turns a gate green without measuring anything
  -- and the previous round of this task already paid for that lesson once (a v()
  helper that was never defined, so every reading compared "" with "").

  So this script builds the frames and traces the readers are handed, in the
  shape the product writes them, and requires every reader to answer BOTH ways:
  the correct input passes, and the defect the corresponding mutation
  reintroduces makes the same reading fail. It needs no GPU, no demo and no
  host lock, which is the point: it can run while the machine is busy.

  What it does NOT prove: that the product produces such frames. That is the
  gate's live section, and this script must never be read as covering it.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FRAME = os.path.join(REPO, "outline-frame-check.py")
REGIONS = os.path.join(REPO, "outline-regions.py")

GREY = (100, 100, 100)
RED = (220, 40, 35)
BLUE = (40, 110, 240)
WHITE = (255, 255, 255)

RED_BOX = (8, 8, 23, 23)
BLUE_BOX = (40, 40, 55, 55)
STRAY = (60, 60)


def write_bmp(path, width, height, pixel):
    """32bpp top-down BMP. pixel(x, y) returns (r, g, b, a)."""
    stride = width * 4
    body = bytearray()
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixel(x, y)
            body += bytes((b, g, r, a))
    header = b"BM" + struct.pack("<IHHI", 14 + 40 + len(body), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, len(body), 2835, 2835, 0, 0)
    with open(path, "wb") as handle:
        handle.write(header + info + bytes(body))


def in_box(box, x, y):
    return box[0] <= x <= box[2] and box[1] <= y <= box[3]


def frame_with_rects(rects, stray=False):
    def pixel(x, y):
        for box, colour in rects:
            if in_box(box, x, y):
                return colour + (255,)
        if stray and (x, y) == STRAY:
            return WHITE + (255,)
        return GREY + (255,)
    return pixel


def run(script, args):
    result = subprocess.run([sys.executable, script] + args,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.returncode, result.stdout.decode("utf-8", "replace"), result.stderr.decode("utf-8", "replace")


FAILURES = []


def expect(label, ok, detail):
    print(("  ok    " if ok else "  FAIL  ") + label + " -- " + detail)
    if not ok:
        FAILURES.append(label)


def reading(text, key):
    for token in text.split():
        if token.startswith(key + "="):
            return token.split("=", 1)[1]
    return None


def main():
    work = tempfile.mkdtemp(prefix="t52-offline-")
    off = os.path.join(work, "off.bmp")
    on = os.path.join(work, "on.bmp")
    on_stray = os.path.join(work, "on-stray.bmp")
    swapped = os.path.join(work, "swapped.bmp")
    depth = os.path.join(work, "world-depth.bmp")
    trace = os.path.join(work, "on.trace")
    trace_empty = os.path.join(work, "empty.trace")

    good = [(RED_BOX, RED), (BLUE_BOX, BLUE)]
    write_bmp(off, 64, 64, frame_with_rects([]))
    write_bmp(on, 64, 64, frame_with_rects(good))
    write_bmp(on_stray, 64, 64, frame_with_rects(good, stray=True))
    # The colour exchange of mutation m1, as a frame: the red player's box holds
    # blue and the blue player's box holds red.
    write_bmp(swapped, 64, 64, frame_with_rects([(RED_BOX, BLUE), (BLUE_BOX, RED)]))

    # --world-depth writes depth*255 into every channel; a wall 0.39 in front of
    # a player whose projected depth is 0.60 covers the red box and nothing else.
    def depth_pixel(x, y):
        value = 100 if in_box(RED_BOX, x, y) else 200
        return (value, value, value, 255)
    write_bmp(depth, 64, 64, depth_pixel)

    draw_line = ("outline: draw enabled=1 depth=xray depthTest=0 depthWrite=0 drew=1 "
                 "ordinal=6 vmOrdinal=7 orderOk=1 silhouettes=2 lines=0 markers=0 "
                 "vertexCount=24 drawCalls=2 submitted=2 red=1 blue=1 unknown=0 noMesh=0 "
                 "noOrigin=0 disabled=0 samples=2 sampledTicks=2 presses=0 stateChanges=1 "
                 "refused=0 owner=outline\n")
    items = [
        "outline: item entity=12 team=red depth=xray x0=%d y0=%d x1=%d y1=%d depth=0.6000 onScreen=1\n" % RED_BOX,
        "outline: item entity=13 team=blue depth=xray x0=%d y0=%d x1=%d y1=%d depth=0.6000 onScreen=1\n" % BLUE_BOX,
    ]
    with open(trace, "w") as handle:
        handle.write(draw_line)
        handle.writelines(items)
    with open(trace_empty, "w") as handle:
        handle.write(draw_line)

    print("outline-frame-check.py noninterference")
    rc, out, _ = run(FRAME, ["noninterference", "on=" + on_stray, "off=" + off,
                             "regions=%d,%d,%d,%d;%d,%d,%d,%d" % (RED_BOX + BLUE_BOX)])
    expect("a difference outside every region fails", rc != 0 and reading(out, "diff_outside") == "1",
           out.strip().replace("\n", " | "))
    rc, out, _ = run(FRAME, ["noninterference", "on=" + on, "off=" + off,
                             "regions=%d,%d,%d,%d;%d,%d,%d,%d" % (RED_BOX + BLUE_BOX)])
    expect("differences confined to the regions pass", rc == 0 and reading(out, "diff_outside") == "0"
           and int(reading(out, "diff_inside")) > 0, out.strip().replace("\n", " | "))
    rc, out, _ = run(FRAME, ["noninterference", "on=" + off, "off=" + off, "require_inside=0"])
    expect("two identical frames pass the determinism form", rc == 0 and reading(out, "diff_outside") == "0",
           out.strip().replace("\n", " | "))

    print("outline-frame-check.py team")
    rc, out, _ = run(FRAME, ["team", "frame=" + on,
                             "red_region=%d,%d,%d,%d" % RED_BOX, "blue_region=%d,%d,%d,%d" % BLUE_BOX])
    expect("each team's own region holds its colour", rc == 0
           and int(reading(out, "red_inside")) >= 8 and int(reading(out, "blue_inside")) >= 8,
           out.strip().replace("\n", " | "))
    rc, out, _ = run(FRAME, ["team", "frame=" + swapped,
                             "red_region=%d,%d,%d,%d" % RED_BOX, "blue_region=%d,%d,%d,%d" % BLUE_BOX])
    wrong = int(reading(out, "red_wrong_team") or 0) + int(reading(out, "blue_wrong_team") or 0)
    expect("the exchanged-colour frame fails the same assertion", rc != 0 and wrong > 0,
           out.strip().replace("\n", " | "))

    print("outline-frame-check.py teamcount")
    rc, out, _ = run(FRAME, ["teamcount", "frame=" + on, "min_total=8"])
    expect("an overlay-on frame counts team pixels", rc == 0
           and int(reading(out, "red_total")) > 0 and int(reading(out, "blue_total")) > 0,
           out.strip().replace("\n", " | "))
    rc, out, _ = run(FRAME, ["teamcount", "frame=" + off])
    expect("an overlay-off frame has none", rc == 0 and reading(out, "red_total") == "0"
           and reading(out, "blue_total") == "0", out.strip().replace("\n", " | "))
    rc, out, _ = run(FRAME, ["teamcount", "frame=" + off, "min_total=8"])
    expect("the leak m3 creates would fail min_total", rc != 0, out.strip().replace("\n", " | "))

    print("outline-frame-check.py depth")
    rc, out, _ = run(FRAME, ["depth", "region=%d,%d,%d,%d" % RED_BOX, "xray=" + on, "tested=" + off,
                             "off=" + off])
    expect("x-ray draws through the wall, the control does not", rc == 0
           and int(reading(out, "occluded_xray")) >= 8 and reading(out, "occluded_tested") == "0"
           and reading(out, "occluded_tested_diff") == "0", out.strip().replace("\n", " | "))
    rc, out, _ = run(FRAME, ["depth", "region=%d,%d,%d,%d" % RED_BOX, "xray=" + on, "tested=" + on,
                             "off=" + off])
    expect("a depth-tested frame that still draws there fails", rc != 0
           and int(reading(out, "occluded_tested")) > 0, out.strip().replace("\n", " | "))

    print("outline-regions.py")
    rc, out, _ = run(REGIONS, ["--trace", trace, "--csv"])
    expect("--csv returns every drawn box", out.strip() ==
           "%d,%d,%d,%d;%d,%d,%d,%d" % (RED_BOX + BLUE_BOX), out.strip() or "<empty>")
    rc, out, _ = run(REGIONS, ["--trace", trace, "--team"])
    expect("--team returns the largest box per team", out.strip() ==
           "red=%d,%d,%d,%d blue=%d,%d,%d,%d" % (RED_BOX + BLUE_BOX), out.strip() or "<empty>")
    rc, out, err = run(REGIONS, ["--trace", trace, "--occluded", "--depth", depth])
    expect("--occluded finds the rectangle the wall covers", out.strip() == "%d,%d,%d,%d" % RED_BOX,
           (out.strip() or "<empty>") + " | " + err.strip())
    # The depth-aware assertions are the ones most likely to be measured on air,
    # so the empty case has to be a quiet, recognisable reading rather than a
    # crash or a silently huge rectangle.
    write_bmp(depth, 64, 64, lambda x, y: (200, 200, 200, 255))
    rc, out, err = run(REGIONS, ["--trace", trace, "--occluded", "--depth", depth])
    expect("--occluded is empty when nothing is behind a wall", rc == 0 and out.strip() == "",
           (out.strip() or "<empty>") + " | " + err.strip())
    rc, out, _ = run(REGIONS, ["--trace", trace_empty, "--csv"])
    expect("--csv is empty when nothing drew", rc == 0 and out.strip() == "",
           out.strip() or "<empty>")

    print()
    if FAILURES:
        print("OFFLINE-SELFTEST=FAIL (%d case(s): %s)" % (len(FAILURES), "; ".join(FAILURES)))
        return 1
    print("OFFLINE-SELFTEST=PASS (the readers answer both ways)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
