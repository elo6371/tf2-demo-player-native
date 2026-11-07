#!/usr/bin/env python3
"""world-geometry-budget-pixels.py -- T88: attribute a picture change to the faces that caused it.

WHY THIS FILE EXISTS
  Restoring the func_illusionary faces and lifting the world triangle budget changes
  what the product draws, so the pinned frames move. "The picture changed, re-pinned"
  is not an attribution: it does not say which faces appeared, how many pixels they
  cover, or whether anything else moved with them.

  The product can draw world geometry with the BSP face index encoded into the colour
  (--world-face-id, native/src/native_renderer_world.cpp:27-37: r = id & 0xFF,
  g = (id >> 8) & 0xFF, b = (id >> 16) & 0xFF). Two runs of the same camera and tick,
  one on the fixed build and one with the pre-fix behaviour put back, therefore give
  two sets of drawn face ids and two textured frames.

WHY "PIXELS ONLY INCREASE" IS THE WRONG ASSERTION HERE (T88, measured)
  The first version of this file asserted outside == 0: every changed pixel must belong
  to a face id that did not exist before, because adding opaque geometry can only
  replace what a pixel showed. The fixed build failed that with 2516 pixels out of
  15722 changed, and the failure is real geometry behaviour, not a regression:

    * the pre-fix build refuses a displacement GRID when the remaining budget is too
      small, but still pushes the face's flat fallback fan (native/src/bsp_map.cpp:1007-1043,
      emittingFallbackQuad), so faces past firstDroppedFace can be on screen as FLAT
      quads until the budget is truly full;
    * the fixed build draws the real (curved) grid for those faces instead, which covers
      a different set of pixels -- so pixels leave as well as arrive, and a wall pixel
      can become sky because the curved surface no longer covers it.

  So the loss is the signature of the fix in both directions, not only the gain. What
  survives as an assertion is a PARTITION with a named owner for every pixel and a
  nonzero gap failing:
    1. every changed pixel either belongs to an added id, or to a face that was drawn
       before and after with a channel delta <= --same-face-delta (sampling drift);
    2. every other changed pixel ("lost") was owned before by a deformation face whose
       grid the pre-fix build refused (before id in --displacement-faces and
       >= --before-first-dropped-face) -- i.e. it is a flat-quad-versus-grid
       substitution, not an unexplained move;
    3. the faces the whitelist restored own pixels in the after frame and none in the
       before frame (--restored-faces) -- the positive signature that the fix reached
       the screen at all.

  If the face lists are not supplied, the tool keeps the original strict outside == 0
  rule. Nothing is silently weakened: the partition is only accepted when the map data
  that names the owner is on the command line.

WHAT IT STILL DOES NOT PROVE
  It cannot prove the reverse containment (that every added face owns pixels): a face
  can be edge-on, behind a nearer surface, or outside the view. Zero-pixel added ids are
  reported as a reading, not asserted.

Usage:
  world-geometry-budget-pixels.py --before-textured a.bmp --after-textured b.bmp \
      --before-ids c.bmp --after-ids d.bmp [--out report.txt] [--top 10] \
      [--displacement-faces f] [--before-first-dropped-face n] [--restored-faces f] \
      [--same-face-delta 8]

Exit: 0 the partition held; 1 it did not; 2 an input is missing or unreadable.

Written by T88 (map axis).
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
    """BGR bytes -> face index, per native_renderer_world.cpp:27-37."""
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


def load_faces(path):
    """One face index per line (world-geometry-budget-faces.py writes this shape)."""
    if not path:
        return None
    try:
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError as error:
        raise SystemExit("cannot read %s: %s" % (path, error))
    faces = np.array([int(tok) for tok in text.split()], dtype=np.int64)
    if faces.size == 0:
        raise SystemExit("%s names no faces" % path)
    return faces


def top_ids(ids, mask, limit):
    if not mask.any():
        return ""
    values, counts = np.unique(ids[mask], return_counts=True)
    order = np.argsort(-counts)[:limit]
    return ",".join("%d:%d" % (int(values[i]), int(counts[i])) for i in order)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--before-textured", required=True)
    parser.add_argument("--after-textured", required=True)
    parser.add_argument("--before-ids", required=True)
    parser.add_argument("--after-ids", required=True)
    parser.add_argument("--out")
    parser.add_argument("--top", type=int, default=8)
    parser.add_argument("--displacement-faces")
    parser.add_argument("--restored-faces")
    parser.add_argument("--before-first-dropped-face", type=int)
    parser.add_argument("--same-face-delta", type=int, default=8)
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
    disp_faces = load_faces(args.displacement_faces)
    restored_faces = load_faces(args.restored_faces)

    delta = np.abs(after - before).max(axis=2)
    changed = delta > 0
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

    # The partition of the pixels that did NOT come from an added id.
    same_face = outside & (ids_before == ids_after)
    lost = outside & ~same_face
    same_face_pixels = int(same_face.sum())
    lost_pixels = int(lost.sum())
    lines.append("outside_partition same_face=%d lost=%d" % (same_face_pixels, lost_pixels))
    # A pixel whose own face did not change can still move because a neighbour moved:
    # where the silhouette of a face shifts, the antialiased edge pixel of the SAME face
    # takes a different blend. Those are allowed above the bulk limit only when the pixel
    # touches a pixel whose id changed; interior drift above the limit is a shading
    # change and fails.
    id_edge = np.zeros_like(changed)
    id_edge[:, 1:] |= ids_after[:, 1:] != ids_after[:, :-1]
    id_edge[:, :-1] |= ids_after[:, 1:] != ids_after[:, :-1]
    id_edge[1:, :] |= ids_after[1:, :] != ids_after[:-1, :]
    id_edge[:-1, :] |= ids_after[1:, :] != ids_after[:-1, :]
    over = same_face & (delta > args.same_face_delta)
    over_fringe = over & id_edge
    over_interior = over & ~id_edge
    if same_face_pixels:
        lines.append("same_face_delta max=%d limit=%d over=%d on_id_edge=%d interior=%d "
                     "top_before_ids=%s" % (
                     int(delta[same_face].max()), args.same_face_delta, int(over.sum()),
                     int(over_fringe.sum()), int(over_interior.sum()),
                     top_ids(ids_before, same_face, args.top)))

    explained = None
    if lost_pixels:
        if disp_faces is None or args.before_first_dropped_face is None:
            explained = np.zeros_like(lost)
        else:
            before_at_lost = ids_before[lost]
            is_disp = np.isin(before_at_lost, disp_faces)
            was_refused = before_at_lost >= args.before_first_dropped_face
            ok_lost = is_disp & was_refused
            explained = np.zeros_like(lost)
            explained[lost] = ok_lost
            lines.append("lost_grid_substitution=%d top_before_ids=%s" % (
                         int(ok_lost.sum()), top_ids(ids_before, lost & explained, args.top)))
        unattributed = lost & ~explained
        if int(unattributed.sum()):
            lines.append("unattributed_lost=%d top_before_ids=%s top_after_ids=%s" % (
                         int(unattributed.sum()), top_ids(ids_before, unattributed, args.top),
                         top_ids(ids_after, unattributed, args.top)))

    if restored_faces is not None:
        after_owned = int(np.isin(ids_after, restored_faces).sum())
        before_owned = int(np.isin(ids_before, restored_faces).sum())
        lines.append("restored_face_pixels before=%d after=%d" % (before_owned, after_owned))

    ok = True
    if added_ids.size == 0:
        lines.append("FAIL no face id appeared: the frame did not change for the claimed reason")
        ok = False
    if int(over_interior.sum()):
        lines.append("FAIL %d pixels changed on faces that were drawn before and after, "
                     "away from any id edge (max channel delta %d > %d): that is a "
                     "shading change, not a coverage change"
                     % (int(over_interior.sum()), int(delta[over_interior].max()),
                        args.same_face_delta))
        ok = False
    if lost_pixels:
        if explained is None:
            lines.append("FAIL %d changed pixels belong to faces that were already drawn: "
                         "the change is not contained in the restored geometry "
                         "(pass --displacement-faces and --before-first-dropped-face to "
                         "allow the grid-versus-flat-fallback substitution)" % lost_pixels)
            ok = False
        elif int((lost & ~explained).sum()):
            lines.append("FAIL %d lost pixels are not a displacement-grid substitution: "
                         "the loss has no owner in this map's geometry"
                         % int((lost & ~explained).sum()))
            ok = False
    if restored_faces is not None:
        if after_owned == 0:
            lines.append("FAIL the restored faces own no pixel in the after frame: the "
                         "whitelist fix did not reach the screen")
            ok = False
        if before_owned != 0:
            lines.append("FAIL the restored faces already owned %d pixels in the before "
                         "frame: the pre-fix build drew them too" % before_owned)
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
