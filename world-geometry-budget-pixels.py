#!/usr/bin/env python3
"""world-geometry-budget-pixels.py -- T88: attribute a picture change to the faces that caused it.

WHY THIS FILE EXISTS
  Restoring the func_illusionary faces and lifting the world triangle budget changes
  what the product draws, so the pinned frames move. "The picture changed, re-pinned"
  is not an attribution: it does not say which faces appeared, how many pixels they
  cover, or whether anything else moved with them.

  The product can draw world geometry with the BSP face index encoded into the colour
  (--world-face-id, native/src/native_renderer_world.cpp:31-37: r = id & 0xFF,
  g = (id >> 8) & 0xFF, b = (id >> 16) & 0xFF). Two runs of the same camera and tick,
  one on the fixed build and one on the pre-fix behaviour, therefore give two sets of
  drawn face ids and two textured frames. The difference between the id sets is the
  population that appeared; the textured difference is where the picture moved.

WHAT IT PROVES, AND WHAT IT DOES NOT
  outside=0 is the statement that every changed pixel belongs to a face that did not
  exist in the earlier frame: adding opaque geometry can only replace what a pixel
  showed, never reveal something new, so a changed pixel whose new id is an already
  drawn face would be an unexplained change. The tool fails on that.

  It cannot prove the reverse containment (that every added face also owns pixels):
  a face can be edge-on, behind a nearer surface, or outside the view. The added ids
  with zero pixels are reported as a reading, not asserted.

  removed ids are expected and not a defect: a newly drawn face in front of an old one
  takes over every pixel the old one owned, so an old id can vanish entirely.

Usage:
  world-geometry-budget-pixels.py --before-textured a.bmp --after-textured b.bmp \
      --before-ids c.bmp --after-ids d.bmp [--out report.txt] [--top 10]

Exit: 0 the containment held; 1 it did not; 2 an input is missing or unreadable.
"""
import argparse
import struct
import sys

import numpy as np

EXIT_OK, EXIT_FAIL, EXIT_ERROR = 0, 1, 2


def read_bmp(path):
    """Return (h, w, 3) int16 array of raw BGR bytes, top row first."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as error:
        raise SystemExit("cannot read %s: %s" % (path, error))
    if len(data) < 54 or data[:2] != b"BM":
        raise SystemExit("%s is not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit("%s is %d bpp; this tool reads 24/32" % (path, bpp))
    bottom_up = height > 0
    height = abs(height)
    stride = ((width * bpp + 31) // 32) * 4
    rows = np.frombuffer(data, dtype=np.uint8, count=stride * height, offset=offset)
    rows = rows.reshape(height, stride)[:, : width * bpp // 8]
    rows = rows.reshape(height, width, bpp // 8)
    if bottom_up:
        rows = rows[::-1]
    return rows[:, :, :3].astype(np.int16)


def face_ids(frame):
    """BGR bytes -> face index, per native_renderer_world.cpp:31-37."""
    b = frame[:, :, 0].astype(np.int32)
    g = frame[:, :, 1].astype(np.int32)
    r = frame[:, :, 2].astype(np.int32)
    return r | (g << 8) | (b << 16)


def bbox(mask):
    rows = np.flatnonzero(mask.any(axis=1))
    cols = np.flatnonzero(mask.any(axis=0))
    if rows.size == 0:
        return None
    return int(cols[0]), int(rows[0]), int(cols[-1]), int(rows[-1])


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--before-textured", required=True)
    parser.add_argument("--after-textured", required=True)
    parser.add_argument("--before-ids", required=True)
    parser.add_argument("--after-ids", required=True)
    parser.add_argument("--out")
    parser.add_argument("--top", type=int, default=8)
    args = parser.parse_args(argv)

    before = read_bmp(args.before_textured)
    after = read_bmp(args.after_textured)
    if before.shape != after.shape:
        print("FAIL picture size changed: %s vs %s" % (before.shape, after.shape))
        return EXIT_ERROR
    ids_before = face_ids(read_bmp(args.before_ids))
    ids_after = face_ids(read_bmp(args.after_ids))
    if ids_before.shape != ids_after.shape:
        print("FAIL id frame size changed")
        return EXIT_ERROR

    changed = np.any(before != after, axis=2)
    changed_pixels = int(changed.sum())
    added_ids = np.setdiff1d(np.unique(ids_after), np.unique(ids_before))
    removed_ids = np.setdiff1d(np.unique(ids_before), np.unique(ids_after))
    added_mask = np.isin(ids_after, added_ids)
    outside = changed & ~added_mask
    outside_pixels = int(outside.sum())

    lines = []
    lines.append("changed=%d bbox=%s" % (changed_pixels, bbox(changed)))
    lines.append("ids before=%d after=%d added=%d removed=%d" % (
                 int(np.unique(ids_before).size), int(np.unique(ids_after).size),
                 int(added_ids.size), int(removed_ids.size)))
    lines.append("added_pixels=%d outside=%d" % (int(added_mask.sum()), outside_pixels))

    counts = np.bincount(ids_after[added_mask].ravel()) if added_mask.any() else np.zeros(0, dtype=np.int64)
    nonzero = np.flatnonzero(counts) if counts.size else np.zeros(0, dtype=np.int64)
    visible = [int(i) for i in nonzero]
    missing = int(added_ids.size - len(visible))
    lines.append("added_visible=%d added_not_visible=%d" % (len(visible), missing))
    top = sorted(visible, key=lambda i: -int(counts[i]))[: args.top]
    lines.append("top_added=%s" % ",".join("%d:%d" % (i, int(counts[i])) for i in top))
    if added_ids.size:
        lines.append("added_id_range=%d..%d" % (int(added_ids.min()), int(added_ids.max())))
    if removed_ids.size:
        lines.append("removed_id_sample=%s" % ",".join(str(int(i)) for i in removed_ids[: args.top]))

    ok = True
    if added_ids.size == 0:
        lines.append("FAIL no face id appeared: the frame did not change for the claimed reason")
        ok = False
    if outside_pixels != 0:
        lines.append("FAIL %d changed pixels belong to faces that were already drawn: "
                     "the change is not contained in the restored geometry" % outside_pixels)
        ok = False
    if added_ids.size and len(visible) == 0:
        lines.append("FAIL %d face ids appeared but none owns a pixel" % int(added_ids.size))
        ok = False

    text = "\n".join(lines)
    print(text)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(text + "\n")
    return EXIT_OK if ok else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
