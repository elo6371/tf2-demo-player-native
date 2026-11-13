#!/usr/bin/env python3
"""world-geometry-budget-pixels-selftest.py -- prove the T88 partition assertions can fail.

WHY THIS FILE EXISTS
  world-geometry-budget-pixels.py stopped asserting "pixels only increase" (that is
  unsatisfiable when a fix replaces a flat fallback quad with a curved displacement grid)
  and now asserts a partition with a named owner for every changed pixel. A partition
  assertion is only worth having if a wrong owner or a shading change still turns it red,
  so this file builds two negative cases out of a real capture and checks the exact
  message each one produces:

    A  one interior pixel of a face that was drawn before and after, moved by +40 in
       blue -> the tool must fail with "away from any id edge" (a shading change);
    B  one pixel whose before id is a refused displacement face rewritten to a face
       that is not in the map's dispinfo list -> the tool must fail with "not a
       displacement-grid substitution" (a loss with no owner).

  Both cases edit a COPY of the captured frames inside --work-dir; the capture itself is
  never modified.

Usage:
  world-geometry-budget-pixels-selftest.py --frames-dir FRAMES \
      --displacement-faces FILE --restored-faces FILE \
      [--first-dropped-face 12204] [--work-dir .scratch/selftest]

  FRAMES must hold before-textured.bmp, after-textured.bmp, before-ids.bmp,
  after-ids.bmp (what world-geometry-budget-check.sh --pixel-attr leaves in
  .scratch/t88-budget/pix/<map>/).

Exit: 0 both cases went red for the right reason; 1 one did not; 2 a file is missing.

Written by T88 (map axis). Read-only on the capture.
"""
import argparse
import importlib.util
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "world-geometry-budget-pixels.py")


def load_tool():
    spec = importlib.util.spec_from_file_location("budget_pixels", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def save_bgr(path, frame):
    h, w, _ = frame.shape
    stride = ((w * 24 + 31) // 32) * 4
    header = bytearray(54)
    header[0:2] = b"BM"
    struct.pack_into("<I", header, 2, 54 + stride * h)
    struct.pack_into("<I", header, 10, 54)
    struct.pack_into("<i", header, 14, 40)
    struct.pack_into("<ii", header, 18, w, h)
    struct.pack_into("<H", header, 26, 1)
    struct.pack_into("<H", header, 28, 24)
    struct.pack_into("<I", header, 34, stride * h)
    body = bytearray()
    for y in range(h - 1, -1, -1):
        row = np.clip(frame[y], 0, 255).astype(np.uint8).tobytes()
        body += row + b"\x00" * (stride - len(row))
    with open(path, "wb") as handle:
        handle.write(bytes(header) + bytes(body))


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--frames-dir", required=True)
    parser.add_argument("--displacement-faces", required=True)
    parser.add_argument("--restored-faces", required=True)
    parser.add_argument("--first-dropped-face", type=int, default=12204)
    parser.add_argument("--work-dir", default=".scratch/selftest")
    args = parser.parse_args(argv)
    px = load_tool()
    frames = args.frames_dir.rstrip("/\\") + "/"
    for name in ("before-textured.bmp", "after-textured.bmp", "before-ids.bmp", "after-ids.bmp"):
        if not os.path.isfile(frames + name):
            print("missing frame: " + frames + name)
            return 2
    os.makedirs(args.work_dir, exist_ok=True)

    after = px.read_bmp(frames + "after-textured.bmp")
    before = px.read_bmp(frames + "before-textured.bmp")
    ids_before = px.face_ids(px.read_bmp(frames + "before-ids.bmp"))
    ids_after = px.face_ids(px.read_bmp(frames + "after-ids.bmp"))
    delta = np.abs(after - before).max(axis=2)

    id_edge = np.zeros_like(ids_after, dtype=bool)
    id_edge[:, 1:] |= ids_after[:, 1:] != ids_after[:, :-1]
    id_edge[:, :-1] |= ids_after[:, 1:] != ids_after[:, :-1]
    id_edge[1:, :] |= ids_after[1:, :] != ids_after[:-1, :]
    id_edge[:-1, :] |= ids_after[1:, :] != ids_after[:-1, :]

    added = np.setdiff1d(np.unique(ids_after), np.unique(ids_before))
    outside = (delta > 0) & ~np.isin(ids_after, added)
    same = outside & (delta <= 8) & (ids_before == ids_after) & ~id_edge
    lost = outside & (ids_before != ids_after)
    if not same.any() or not lost.any():
        print("this capture has no same-face interior pixel or no lost pixel; nothing to test")
        return 2
    ys, xs = np.nonzero(same)
    y, x = int(ys[0]), int(xs[0])
    ys, xs = np.nonzero(lost)
    y2, x2 = int(ys[0]), int(xs[0])
    print("case A: same-face interior pixel (%d,%d) id=%d delta=%d"
          % (x, y, int(ids_after[y, x]), int(delta[y, x])))
    print("case B: lost pixel (%d,%d) before id=%d after id=%d"
          % (x2, y2, int(ids_before[y2, x2]), int(ids_after[y2, x2])))

    disp = set(int(tok) for tok in open(args.displacement_faces, encoding="utf-8").read().split())
    candidates = [int(i) for i in np.unique(ids_before) if int(i) not in disp]
    if not candidates:
        print("no non-displacement face id in the before frame; cannot build case B")
        return 2
    print("case B replacement before id (not a displacement face): %d" % candidates[0])

    base = [sys.executable, TOOL,
            "--before-ids", frames + "before-ids.bmp",
            "--after-ids", frames + "after-ids.bmp",
            "--displacement-faces", args.displacement_faces,
            "--restored-faces", args.restored_faces,
            "--before-first-dropped-face", str(args.first_dropped_face)]
    fails = 0

    a_bad = after.copy()
    a_bad[y, x, 0] = min(255, int(a_bad[y, x, 0]) + 40)
    a_path = os.path.join(args.work_dir, "A-after.bmp")
    save_bgr(a_path, a_bad)
    run_a = subprocess.run(base + ["--before-textured", frames + "before-textured.bmp",
                                   "--after-textured", a_path],
                           capture_output=True, text=True)
    ok_a = run_a.returncode == 1 and "away from any id edge" in run_a.stdout
    print("case A rc=%d red-as-expected=%s" % (run_a.returncode, ok_a))
    if not ok_a:
        print(run_a.stdout)
        fails += 1

    b_ids = px.read_bmp(frames + "before-ids.bmp").copy()
    ptr = candidates[0]
    b_ids[y2, x2] = (ptr & 0xFF, (ptr >> 8) & 0xFF, (ptr >> 16) & 0xFF)
    b_path = os.path.join(args.work_dir, "B-before-ids.bmp")
    save_bgr(b_path, b_ids)
    run_b = subprocess.run(base + ["--before-textured", frames + "before-textured.bmp",
                                   "--after-textured", frames + "after-textured.bmp",
                                   "--before-ids", b_path],
                           capture_output=True, text=True)
    ok_b = run_b.returncode == 1 and "not a displacement-grid substitution" in run_b.stdout
    print("case B rc=%d red-as-expected=%s" % (run_b.returncode, ok_b))
    if not ok_b:
        print(run_b.stdout)
        fails += 1

    print("SELFTEST=%s" % ("PASS" if fails == 0 else "FAIL"))
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
