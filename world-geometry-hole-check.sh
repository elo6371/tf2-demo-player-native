#!/usr/bin/env bash
# world-geometry-hole-check.sh -- T26: is an empty pixel a hole or the sky?
#
# WHAT IT ASSERTS
#   1. The picture did not move. `textured` and `ref` are byte-identical to the
#      frames the same commit produces without the T26 instrumentation. The
#      instrument adds a buffer and a trace; it must not add a surface.
#   2. The missing population reproduces the account: 38,328 pixels, every one
#      covered by a BSP sky face, none without one.
#   3. The enclosed components are sky-backed too: the 2,313 pixels that looked
#      like closed holes are the map's sky seen through brush seams, not absent
#      geometry.
#   4. The point oracle and the rasterizer agree. Representative points of every
#      component >= 200 px are projected by the product itself, the nearest
#      world triangle and the nearest sky face are named, and the face-id frame
#      must decode to the same sky face the projection named.
#   5. The winding repair is refused by measurement. Flipping the brush/side-1
#      cell would fill back-facing pixels at this POV, and the first-hit oracle
#      says it would hide two thirds of the eye's first hits. The numbers are
#      pinned so a later "fix" has to argue with them.
#   6. The seam classification is not a cull artefact. Every measured seam
#      direction resolves, 0 of the 8 named seam faces are derived-back at the
#      capture's own camera, and a POSITIVE control shows the product cull still
#      removes a named non-seam face at the same pose. Mutation M6 disables the
#      cull and must turn the positive control red while the seam answer stays.
#
# WHAT IT DOES NOT CLAIM
#   `drawn` is not `correct`. A pixel with a triangle is not a pixel with the
#   right material, and the 28% dark population T23 measured is drawn geometry
#   with a dark lightmap, not a hole. T26 changes no geometry rule; it measures
#   which pixels have no geometry and what the map says about them.
#
# Usage: bash world-geometry-hole-check.sh [--mutation] [--reuse]
#   --mutation  run the forward gate, then prove each of six mutations turns it
#               red, restoring every source byte-for-byte.
#   --reuse     keep the frames already captured under evidence/ (the capture
#               script resumes). Without it the matrix is captured again.
# Exit:  0 = every assertion held (or, with --mutation, every mutation went red
#            and every source came back identical);
#        1 = an assertion failed, or a mutation did not turn the gate red;
#        2 = a required tool or input is missing.
set -uo pipefail
cd "$(dirname "$0")"

MODE="${1:-}"
OUT=evidence/t26-world-holes
CAP="$OUT/captures"
ANA="$OUT/analysis"
SCRATCH=".scratch/t26"
PYTHON="${PYTHON:-python}"
PRODUCT=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/world_geometry_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
TICK=2000
mkdir -p "$SCRATCH" "$ANA"

# ---- pins ----------------------------------------------------------------
# Measured on 36d68b0 with the T26 instrumentation built from the same commit.
# A pin that moves without a source change means the instrument changed the
# picture; a pin that moves with one must be explained in the delivery report,
# never copied from an older tree.
#
# T23 published 38,328 missing / 36,015 open / 2,313 enclosed / 44 components
# from 9591499, and the tree this gate was written on reproduced all four
# exactly -- once every frame in the matrix belongs to the SAME tick. The first
# run of this matrix did not: five frames were captured at tick 2001/2002
# because the arm fires on the first draw at or after the requested tick, and
# the account read 38,143 missing / 2,310 enclosed. That 185-pixel difference
# was the camera, not the code, and it is why capture-matrix frames carry their
# tick and this gate refuses a frame from the wrong one.
#
# T35 RE-BASELINE. T29 moved the world near plane from 611 map units to 4, and
# the camera's own room came back on screen. Every pin below that is derived
# from a FRAME moved; every pin derived from the BSP did not, and that split is
# the evidence that this was a projection change rather than a geometry-rule
# change. Measured on 9b4e734, same camera, same tick 2000, same switches:
#
#   reading                         pre-T29     T29 tree   moved?
#   missing                          38,328 ->     1,648   yes
#   missing covered by a sky face    38,328 ->     1,648   yes
#   missing with no sky face              0 ->         0   no
#   enclosed                          2,313 ->     1,648   yes
#   open                             36,015 ->         0   yes
#   components                           44 ->        34   yes
#   enclosed components >= 200 px         3 ->         3   no
#   back-face rejections (T23)      167,030 ->    80,917   yes
#   sky faces (probe, BSP read)         731 ->       731   no
#   sky triangles (probe, BSP read)   1,842 ->     1,842   no
#   buffer triangles (probe)         99,312 ->    99,312   no
#   textured frame sha              5e24802 ->  ac2cfe9   yes
#   ref frame sha                   f87f396 ->  6bb4d9a   yes
#
# The fall in missing is the fix: those pixels had geometry all along and the
# 611-unit clip was removing it. unexplained is 0 on both sides, so T29
# neither created a hole nor hid one behind the sky mask. The back-face count
# halving is the same fact read from T23's side.
#
# SEAM_CULLED MOVED 2 -> 0 (T44). The old assertion was
# "at least one sliver had a culled neighbour" (SEAM_CULLED >= 1); on this tree
# it is RED and it must not be called a pass: after the T29 near-plane fix none
# of the surviving slivers is bracketed by a surface the product culls. T41
# derived why and T44 measured it at the capture's own pose: all eight named
# seam faces are derived-FRONT under the D3D11 rule (CULL_BACK,
# FrontCounterClockwise=FALSE, front iff dot(emittedNormal, eye-p) > 0), so a
# correct cull CANNOT remove them and 0 cull-off-only directions is the correct
# reading. The old >=1 pin encoded a pre-T29 artefact. It is replaced -- not
# relaxed -- by four measurements that can fail independently:
#   * every one of the 24 measured direction probes resolves to a face
#     (6 seam points x 4 directions). The merged wave summary's "32/32" is a
#     documentation error: T40's own log (evidence/t40-pixel-pins/t26-hole.log,
#     lines 51-55) shows 6 seam lines and 13 face-info requests, and this gate
#     now asserts the measured total instead of copying the number;
#   * 0 of the 8 named faces are derived-back at the trace's camera;
#   * a POSITIVE control: the product cull removes 81,218 face-id px from 18
#     non-seam faces at the same pose, the largest being face 17605
#     (disp/side-0, derived-back, 33,167 px), so "the cull is on" is measured;
#   * mutation M6 disables the cull and must turn that control RED while the
#     derived-side classification stays at 0 derived-back, so the seam answer
#     cannot be an artefact of a disabled cull.
PIN_MISSING=1648
PIN_SKY_MISSING=1648
PIN_UNEXPLAINED=0
PIN_ENCLOSED=1648
PIN_ENCLOSED_MIN_COMPONENT=200
PIN_ENCLOSED_COMPONENTS=3
PIN_SKY_FACES=731
PIN_SKY_TRIANGLES=1842
PIN_WORLD_TRIANGLES=99312
PIN_BACKFACE=80917
PIN_TEXTURED_SHA=ac2cfe9b45bad37a210ad3a8a25b34c2e768fe3e4bd8c578046262bdf31c129d
PIN_REF_SHA=6bb4d9a712e82ef4cf58929651cea1070d11c1ac082bdb25a6e6d7097e2f974b

# T44 pins, measured on 6888e6d + the T44 assertion at the same camera/tick as
# the T35 re-baseline (trace: captures/traces/world-ids.trace, last
# 'scene: camera' before 'capture: armed ... frame_tick=2000', src=demo --
# NOT the once-per-run 'gpu: world vis eyeMap', which is the first drawn frame).
# The account traces 6 representative points (2 per enclosed sliver >= 200 px),
# 4 directions each = 24 probes, and all 24 resolve. The 8 named faces are all
# derived-front; the cull's positive control removes 18 non-seam faces.
PIN_CAPTURE_EYE="-2294.8,348.8,383.4"
PIN_SEAM_IDS="8555,15324,17217,17260,17262,17266,17268,17326"
PIN_SEAM_POINTS=6
PIN_SEAM_PROBES=24
PIN_SEAM_CULLED=0
PIN_SEAM_DERIVED_BACK=0
PIN_CULL_TOP_FACE=17605
PIN_CULL_TOP_DERIVED=back
PIN_CULL_TOP_PIXELS=33167

fail=0
assert_in() { # assert_in <file> <fixed string> <label>
  if grep -Fq -- "$2" "$1"; then
    echo "  PASS $3"
  else
    echo "  FAIL $3: '$2' is not in $1"
    fail=1
  fi
}
assert_eq() { # assert_eq <actual> <expected> <label>
  if [ "$1" = "$2" ]; then
    echo "  PASS $3 ($1)"
  else
    echo "  FAIL $3: $1 != $2"
    fail=1
  fi
}
field() { # field <text> <key> -> value after key=
  sed -n "s/.*[[:space:]]$2=\\([^[:space:]]*\\).*/\\1/p" <<<"$1" | head -1
}
# Which tick a captured frame belongs to; see world-geometry-hole-capture.sh.
# The gate re-runs this rather than trusting the capture script, because a
# mutation's focused frame is compared against numbers measured at TICK.
frame_tick() { # frame_tick <trace>
  local trace="$1" tick
  tick=$(grep -a "capture: armed " "$trace" 2>/dev/null | tr -d '\r' | tail -1 \
    | sed -n 's/.*frame_tick=\([-0-9]*\).*/\1/p')
  if [ -z "$tick" ]; then
    tick=$(grep -a "draw: begin tick=" "$trace" 2>/dev/null | tr -d '\r' \
      | sed -n 's/.*tick=\([0-9]*\).*/\1/p' | awk -v want="$TICK" '$1>=want{print; exit}')
  fi
  printf '%s' "$tick"
}
# The last line that says why a reload stopped, with the trace clock stripped.
refusal_line() { # refusal_line <trace>
  grep -a "reload-stage\|state=no world atlas was uploaded" "$1" 2>/dev/null | tr -d '\r' \
    | tail -1 | sed 's/^[0-9.]* //'
}
# A capture whose frame is not at TICK is a different camera, and comparing it
# against a tick-TICK account measures the camera, not the code. Retry rather
# than accept: the arm fires on the first draw at or after the tick, so a run
# that skips a tick captures late.
#
# T26 mutation M1 taught this function a second answer. Removing the sky
# polygon does not produce an empty mask: this product's rule (T12) is that an
# empty upload is a refusal, so the run ends before the first draw. That is a
# red the gate has to judge, and it is not "the frame landed on tick 2001" --
# four retries of a refusal printed four identical lines and then skipped the
# judgement, which is how a mutation can pass for the wrong reason. So: no draw
# at all plus a non-zero exit returns 2 and names the refusal.
run_capture_trace() { # run_capture_trace <bmp> <trace> [extra flags...]
  local bmp="$1" trace="$2"; shift 2
  local attempt rc
  LAST_PRODUCT_RC=0
  for attempt in 1 2 3 4; do
    rm -f "$trace" "$bmp"
    TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$trace")" timeout 180 "$PRODUCT" \
      --tf-root "$TF" --demo "$DEMO" --capture-tick "$TICK" \
      --capture-frame "$(cygpath -m "$PWD/$bmp")" \
      --audio-disabled --hidden-window --world-water off "$@" > "$SCRATCH/run.log" 2>&1
    rc=$?
    LAST_PRODUCT_RC=$rc
    if [ "$rc" -ne 0 ] && ! grep -aq "draw: begin tick=" "$trace" 2>/dev/null; then
      echo "  the product refused to draw (exit $rc): $(refusal_line "$trace")"
      return 2
    fi
    if [ "$rc" -eq 0 ] && [ -s "$bmp" ] && [ "$(frame_tick "$trace")" = "$TICK" ]; then
      return 0
    fi
    echo "  retry: the capture landed on tick $(frame_tick "$trace"), not $TICK"
  done
  return 1
}

# ---- T44 side instrument ------------------------------------------------
# One generated tool, three readings: the capture pose (eye), the derived side
# of the named seam faces (classify), and what the product cull removed (cull).
# It is generated into .scratch because it is this gate's instrument, not a
# repository tool: the gate is the only thing that should run it, and a scratch
# directory that is wiped must not break another gate. The BSP walk is T41's
# raster-rule.py parser reduced to the three lumps the emission rule reads.
cat > "$SCRATCH/seam-cull.py" <<'PYEOF'
#!/usr/bin/env python3
"""T44 side instrument for world-geometry-hole-check.sh -- generated by the gate.

Three readings, all from committed artifacts (no product launch):

  eye       the camera the captured frame was drawn with, read from the capture's
            own trace. The last 'scene: camera' line BEFORE the 'capture: armed
            ... frame_tick=TICK' line. The once-per-run 'gpu: world vis eyeMap'
            line is a different scene pose (the first drawn frame) and is NOT
            used; T41 established that at the T36/T41 pose and this gate must not
            repeat the mistake.
  classify  for each named seam face: the side the RASTERIZER's derived rule
            calls it at that eye. The rule (T41): D3D11_CULL_BACK with
            FrontCounterClockwise=FALSE, front iff dot(emittedNormal, eye-p) > 0.
            The brush fan emits the PLANE normal whatever the side bit says; a
            displacement face's triangles are flipped onto the face's outward
            (n if side==0 else -n). So brush is side-independent, disp is
            side-aware. This is a BSP+eye reading: the product cull cannot change
            it, which is what makes it a valid classification while the cull is
            under mutation.
  cull      the positive control: pixels that world-ids-nocull.bmp gives a face id
            and world-ids.bmp does not, grouped by face. That is what the
            product's cull removed at this pose, and the top NON-seam face is
            named with its derived side (it must be derived-back).

Usage:
  seam-cull.py eye      --trace T --tick 2000
  seam-cull.py classify --bsp B --eye=X,Y,Z --faces 1,2,3
  seam-cull.py cull     --bsp B --eye=X,Y,Z --seam 1,2,3 --ids A --ids-nocull B
Exit: 0 = readings produced; 2 = a required input is missing/unreadable.
"""
import argparse
import lzma
import os
import struct
import sys

import numpy as np


def read_lump(data, index):
    off, length = struct.unpack_from("<ii", data, 8 + index * 16)
    if length <= 0:
        return b""
    raw = data[off:off + length]
    if raw[:4] != b"LZMA":
        return raw
    actual = struct.unpack_from("<I", raw, 4)[0]
    dec = lzma.LZMADecompressor(
        format=lzma.FORMAT_RAW,
        filters=[lzma._decode_filter_properties(lzma.FILTER_LZMA1, raw[12:17])])
    return dec.decompress(raw[17:], max_length=actual)


class Bsp:
    """The three lumps the emission rule reads, plus the face->polygon walk.

    This is raster-rule.py's parser (T41) reduced to what classify/cull need;
    keeping the field offsets in one place is why it is copied rather than
    re-derived here.
    """

    def __init__(self, path):
        d = open(path, "rb").read()
        self.first_face, self.face_count = struct.unpack_from("<ii", read_lump(d, 14), 40)
        self.faces = read_lump(d, 7)
        self.planes = read_lump(d, 1)
        self.edges = read_lump(d, 12)
        self.surfedges = read_lump(d, 13)
        self.vertices = read_lump(d, 3)
        self.n_edges = len(self.edges) // 4
        self.n_verts = len(self.vertices) // 12
        self.n_surf = len(self.surfedges) // 4
        self.V = np.frombuffer(self.vertices, dtype=np.float32,
                               count=self.n_verts * 3).reshape(self.n_verts, 3).astype(np.float64)
        self.E = np.frombuffer(self.edges, dtype=np.uint16,
                               count=self.n_edges * 2).reshape(self.n_edges, 2)
        self.P = np.frombuffer(self.planes, dtype=np.float32,
                               count=len(self.planes) // 4).reshape(-1, 5)[:, :4].astype(np.float64)

    def face(self, index):
        b = index * 56
        if index < 0 or b + 56 > len(self.faces):
            return None
        planenum = struct.unpack_from("<H", self.faces, b)[0]
        side = self.faces[b + 2]
        firstedge = struct.unpack_from("<i", self.faces, b + 4)[0]
        count = struct.unpack_from("<h", self.faces, b + 8)[0]
        dispinfo = struct.unpack_from("<h", self.faces, b + 12)[0]
        if count < 3 or firstedge < 0 or firstedge + count > self.n_surf:
            return None
        if planenum >= len(self.P):
            return None
        verts = []
        for k in range(count):
            s = struct.unpack_from("<i", self.surfedges, (firstedge + k) * 4)[0]
            e = -s if s < 0 else s
            if e < 0 or e >= self.n_edges:
                return None
            vi = int(self.E[e][1 if s < 0 else 0])
            if vi >= self.n_verts:
                return None
            verts.append(vi)
        n = self.P[planenum][:3]
        if not np.dot(n, n) > 1e-12:
            return None
        return {"planenum": planenum, "side": int(side), "count": int(count),
                "dispinfo": int(dispinfo), "poly": self.V[verts],
                "plane": n / np.linalg.norm(n), "dist": float(self.P[planenum][3])}


def read_bmp(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    w, hh = struct.unpack_from("<ii", d, 18)
    topdown = hh < 0
    h = abs(hh)
    stride = (w * 3 + 3) // 4 * 4
    a = np.frombuffer(d, dtype=np.uint8, count=stride * h, offset=off).reshape(h, stride)
    rgb = a[:, :w * 3].reshape(h, w, 3)[:, :, ::-1]
    if not topdown:
        rgb = rgb[::-1]
    return w, h, rgb


def face_ids(path):
    w, h, rgb = read_bmp(path)
    ids = (rgb[:, :, 0].astype(np.int64) | (rgb[:, :, 1].astype(np.int64) << 8)
           | (rgb[:, :, 2].astype(np.int64) << 16))
    return w, h, ids


def derived_side(face, eye):
    """(derived side, signed plane distance, dot(emitted normal, eye-p0))."""
    sd = float(np.dot(face["plane"], eye) - face["dist"])
    if face["dispinfo"] >= 0:
        outward = face["plane"] * (1.0 if face["side"] == 0 else -1.0)
        emitted_dot = float(np.dot(outward, eye - face["poly"][0]))
    else:
        emitted_dot = float(np.dot(face["plane"], eye - face["poly"][0]))
    return ("front" if emitted_dot > 0.0 else "back"), sd, emitted_dot


def mode_eye(args):
    try:
        lines = open(args.trace, "r", errors="replace").read().splitlines()
    except OSError as exc:
        print("EYE state=missing trace=%s (%s)" % (args.trace, exc))
        return 2
    armed_index = None
    armed_line = ""
    for index, line in enumerate(lines):
        if "capture: armed " in line:
            tick = None
            for token in line.split():
                if token.startswith("frame_tick="):
                    try:
                        tick = int(token.split("=", 1)[1])
                    except ValueError:
                        tick = None
            if tick == args.tick:
                armed_index = index
                armed_line = line.strip()
    if armed_index is None:
        print("EYE state=missing reason=no 'capture: armed' line with frame_tick=%d" % args.tick)
        return 2
    eye_line = ""
    for index in range(armed_index - 1, -1, -1):
        if "scene: camera" in lines[index]:
            eye_line = lines[index].strip()
            break
    if not eye_line:
        print("EYE state=missing reason=no 'scene: camera' line before the capture")
        return 2
    values = {}
    for token in eye_line.split():
        if "=" in token:
            key, value = token.split("=", 1)
            values[key] = value
    eye = values.get("eye")
    src = values.get("src", "?")
    if not eye:
        print("EYE state=missing reason=the camera line has no eye= field")
        return 2
    # The once-per-run eyeMap line is a different scene pose; report it so a
    # reader can see it was not used rather than merely not mentioned.
    eye_map = ""
    for line in lines:
        if "gpu: world vis eyeMap" in line:
            eye_map = line.strip()
            break
    print("EYE eye=%s src=%s tick=%d armed=%s" % (eye, src, args.tick, armed_line))
    print("EYE camera-line %s" % eye_line)
    if eye_map:
        print("EYE superseded-eyeMap %s" % eye_map)
    else:
        print("EYE superseded-eyeMap absent")
    return 0


def mode_classify(args):
    bsp = Bsp(args.bsp)
    eye = np.array([float(v) for v in args.eye.split(",")])
    faces = [int(v) for v in args.faces.split(",") if v.strip()]
    if not faces:
        print("CLASS state=missing reason=no faces requested")
        return 2
    front = back = 0
    for f in faces:
        face = bsp.face(f)
        if face is None:
            print("CLASS face=%d state=unparsed" % f)
            continue
        side, sd, emitted_dot = derived_side(face, eye)
        kind = "disp" if face["dispinfo"] >= 0 else "brush"
        print("CLASS face=%d kind=%s side=%d disp=%d plane=%d planeSD=%.3f"
              " emittedDot=%.3f derived=%s"
              % (f, kind, face["side"], 1 if face["dispinfo"] >= 0 else 0,
                 face["planenum"], sd, emitted_dot, side))
        if side == "front":
            front += 1
        else:
            back += 1
    print("CLASS-SUMMARY faces=%d parsed=%d derivedFront=%d derivedBack=%d"
          % (len(faces), front + back, front, back))
    return 0 if front + back == len(faces) else 1


def mode_cull(args):
    bsp = Bsp(args.bsp)
    eye = np.array([float(v) for v in args.eye.split(",")])
    seam = set(int(v) for v in args.seam.split(",") if v.strip())
    w, h, cull_ids = face_ids(args.ids)
    w2, h2, nocull_ids = face_ids(args.ids_nocull)
    if (w, h) != (w2, h2):
        print("CULL state=missing reason=frames differ in size %dx%d vs %dx%d" % (w, h, w2, h2))
        return 2
    lo, hi = bsp.first_face, bsp.first_face + bsp.face_count
    in_cull = (cull_ids >= lo) & (cull_ids < hi)
    in_nocull = (nocull_ids >= lo) & (nocull_ids < hi)
    kept = int((in_cull & in_nocull).sum())
    removed_mask = in_nocull & ~in_cull
    removed = int(removed_mask.sum())
    only_cull = int((in_cull & ~in_nocull).sum())
    counts = {}
    flat = nocull_ids[removed_mask].tolist()
    for value in flat:
        counts[value] = counts.get(value, 0) + 1
    print("CULL state=ok frames=%dx%d kept=%d removed=%d onlyInCull=%d distinctRemoved=%d"
          % (w, h, kept, removed, only_cull, len(counts)))
    rows = []
    for face_id, pixels in counts.items():
        face = bsp.face(int(face_id))
        if face is None:
            rows.append((pixels, int(face_id), "unparsed", -1, "?", 0.0))
            continue
        side, sd, emitted_dot = derived_side(face, eye)
        kind = "disp" if face["dispinfo"] >= 0 else "brush"
        rows.append((pixels, int(face_id), kind, face["side"], side, sd))
    rows.sort(key=lambda row: (-row[0], row[1]))
    for pixels, face_id, kind, side_bit, side, sd in rows[:24]:
        print("CULL face=%d pixels=%d kind=%s side=%s derived=%s planeSD=%.3f seam=%d"
              % (face_id, pixels, kind, side_bit, side, sd, 1 if face_id in seam else 0))
    non_seam = [row for row in rows if row[1] not in seam]
    seam_removed = [row for row in rows if row[1] in seam]
    if non_seam:
        pixels, face_id, kind, side_bit, side, sd = non_seam[0]
        print("CULL-SUMMARY removed=%d onlyInCull=%d distinct=%d nonSeam=%d"
              " seamFacesRemoved=%d topNonSeam=%d topNonSeamPixels=%d"
              " topNonSeamKind=%s topNonSeamSide=%s topNonSeamDerived=%s"
              % (removed, only_cull, len(rows), len(non_seam), len(seam_removed),
                 face_id, pixels, kind, side_bit, side))
    else:
        print("CULL-SUMMARY removed=%d onlyInCull=%d distinct=%d nonSeam=0"
              " seamFacesRemoved=%d topNonSeam=none topNonSeamPixels=0"
              " topNonSeamKind=- topNonSeamSide=- topNonSeamDerived=-"
              % (removed, only_cull, len(rows), len(seam_removed)))
    for pixels, face_id, kind, side_bit, side, sd in seam_removed:
        print("CULL seam-removed face=%d pixels=%d kind=%s side=%s derived=%s"
              % (face_id, pixels, kind, side_bit, side))
    return 0


def main(argv):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)
    eye = sub.add_parser("eye")
    eye.add_argument("--trace", required=True)
    eye.add_argument("--tick", type=int, required=True)
    cls = sub.add_parser("classify")
    cls.add_argument("--bsp", required=True)
    cls.add_argument("--eye", required=True)
    cls.add_argument("--faces", required=True)
    cull = sub.add_parser("cull")
    cull.add_argument("--bsp", required=True)
    cull.add_argument("--eye", required=True)
    cull.add_argument("--seam", required=True)
    cull.add_argument("--ids", required=True)
    cull.add_argument("--ids-nocull", required=True)
    args = ap.parse_args(argv[1:])
    if args.mode == "eye":
        return mode_eye(args)
    if args.mode == "classify":
        return mode_classify(args)
    return mode_cull(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
PYEOF

echo "== 0. source and tool identity =="
HEAD_SHA=$(git rev-parse --short=12 HEAD)
echo "  HEAD=$HEAD_SHA branch=$(git branch --show-current)"
if [ -n "$(git status --porcelain native/ 2>/dev/null)" ]; then
  echo "  FAIL native/ has uncommitted changes; a gate must describe a committed tree"
  git status --short native/ | sed 's/^/       /'
  fail=1
else
  echo "  PASS native/ is clean"
fi
for tool in "$PYTHON" bash; do
  command -v "$tool" >/dev/null 2>&1 || { echo "  FAIL $tool is missing"; exit 2; }
done

echo "== 1. build =="
bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/build.txt" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL build"
  tail -20 "$SCRATCH/build.txt" | sed 's/^/       /'
  exit 1
fi
echo "  PASS build"
{
  echo "HEAD $HEAD_SHA"
  echo "tf2_demo_native.exe $(sha256sum "$PRODUCT" | cut -d' ' -f1)"
  echo "world_geometry_probe.exe $(sha256sum "$PROBE" | cut -d' ' -f1)"
  echo "native/src/bsp_map.cpp $(sha256sum native/src/bsp_map.cpp | cut -d' ' -f1)"
  echo "native/src/main.cpp $(sha256sum native/src/main.cpp | cut -d' ' -f1)"
  echo "native/src/native_renderer.cpp $(sha256sum native/src/native_renderer.cpp | cut -d' ' -f1)"
} | tee "$ANA/identity.txt"

echo "== 2. capture matrix =="
bash world-geometry-hole-capture.sh "$CAP" > "$SCRATCH/capture.txt" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL capture"
  tail -20 "$SCRATCH/capture.txt" | sed 's/^/       /'
  exit 1
fi
tail -1 "$SCRATCH/capture.txt" | sed 's/^/  /'
for frame in "$CAP"/*.bmp; do
  printf '%s  %s\n' "$(sha256sum "$frame" | cut -d' ' -f1)" "$(basename "$frame")"
done | sort -k2 > "$ANA/hashes.txt"
TEXTURED_SHA=$(sha256sum "$CAP/textured.bmp" | cut -d' ' -f1)
REF_SHA=$(sha256sum "$CAP/ref.bmp" | cut -d' ' -f1)

echo "== 3. the product picture did not move =="
assert_eq "$TEXTURED_SHA" "$PIN_TEXTURED_SHA" "stored textured frame identity"
assert_eq "$REF_SHA" "$PIN_REF_SHA" "stored ref frame identity"
# The stored frames were written by an earlier build of this worktree. Re-capture
# with the binary THIS run built: the instrument adds a trace line and a vertex
# colour the product path never fills, and the only way to say that neither
# moved a surface is a byte comparison of the same view from the new binary.
if run_capture_trace "$SCRATCH/verify-textured.bmp" "$SCRATCH/verify-textured.trace"; then
  if cmp -s "$SCRATCH/verify-textured.bmp" "$CAP/textured.bmp"; then
    echo "  PASS the binary this run built reproduces the pinned textured frame byte for byte"
  else
    echo "  FAIL the binary this run built draws a different textured frame"
    echo "       fresh $(sha256sum "$SCRATCH/verify-textured.bmp" | cut -d' ' -f1)"
    fail=1
  fi
else
  echo "  FAIL the verification capture did not land on tick $TICK"
  fail=1
fi

echo "== 4. probe: the sky as geometry =="
"$PROBE" --tf-root "$TF" --map maps/pl_odyssey.bsp > "$ANA/probe.txt" 2>&1
PROBE_RC=$?
if [ "$PROBE_RC" -ne 0 ]; then
  echo "  FAIL probe rc=$PROBE_RC"
  fail=1
fi
assert_in "$ANA/probe.txt" "geometry sky faces=$PIN_SKY_FACES triangles=$PIN_SKY_TRIANGLES bufferTriangles=$PIN_WORLD_TRIANGLES" "sky face census"
assert_in "$ANA/probe.txt" "geometry faces total=18052 rendered=17321 sky=731 skippedNoDraw=0 skippedInvalid=0" "face account unchanged"

echo "== 5. first-hit oracle: the winding candidate is refused =="
"$PROBE" --tf-root "$TF" --map maps/pl_odyssey.bsp \
  --ray-down 96 --ray-eye "-2423.6,5566.0,-1280.0" 24 > "$ANA/probe-oracle.txt" 2>&1
ORACLE_RC=$?
if [ "$ORACLE_RC" -ne 0 ]; then
  echo "  FAIL oracle rc=$ORACLE_RC"
  fail=1
fi
SHIPPED_EYE=$(sed -n 's/.*rule-table mask=07 .* eye=\([0-9.]*\).*/\1/p' "$ANA/probe-oracle.txt" | head -1)
FLIP_EYE=$(sed -n 's/.*rule-table mask=05 .* eye=\([0-9.]*\).*/\1/p' "$ANA/probe-oracle.txt" | head -1)
TOP_EYE=$(sed -n 's/.*rule-table mask=13 .* eye=\([0-9.]*\).*/\1/p' "$ANA/probe-oracle.txt" | head -1)
echo "  shipped mask07 eye=${SHIPPED_EYE:-none} flip mask05 eye=${FLIP_EYE:-none} top mask13 eye=${TOP_EYE:-none}"
if [ -z "$SHIPPED_EYE" ] || [ -z "$FLIP_EYE" ] || [ -z "$TOP_EYE" ]; then
  echo "  FAIL the rule table is missing"; fail=1
else
  "$PYTHON" - "$SHIPPED_EYE" "$FLIP_EYE" "$TOP_EYE" <<'PYEOF'
import sys
shipped, flip, top = (float(value) for value in sys.argv[1:4])
if shipped > flip + 0.2 and shipped > top + 0.2:
    print("  PASS the shipped rule keeps %.3f of the eye's first hits; the two cells a flip would win read %.3f and %.3f" % (shipped, flip, top))
else:
    print("  FAIL the shipped rule no longer dominates the flip candidates: %.3f vs %.3f / %.3f" % (shipped, flip, top))
    sys.exit(1)
PYEOF
  [ $? -ne 0 ] && fail=1
fi

echo "== 6. the empty-pixel account =="
"$PYTHON" world-geometry-hole-account.py --dir "$CAP" --out "$ANA" > "$ANA/hole-account.txt" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL accounting"
  cat "$ANA/hole-account.txt" | sed 's/^/       /'
  exit 1
fi
TOTALS=$(grep -F "HOLE totals " "$ANA/hole-account.txt")
SPLIT=$(grep -F "HOLE split " "$ANA/hole-account.txt")
echo "  $TOTALS"
echo "  $SPLIT"
assert_eq "$(field "$TOTALS" missing)" "$PIN_MISSING" "missing pixels"
assert_eq "$(field "$TOTALS" sky)" "$PIN_SKY_MISSING" "missing pixels covered by a sky face"
assert_eq "$(field "$TOTALS" unexplained)" "$PIN_UNEXPLAINED" "missing pixels with no sky face"
assert_eq "$(field "$SPLIT" enclosed)" "$PIN_ENCLOSED" "enclosed missing pixels"
assert_eq "$(field "$SPLIT" enclosedUnexplained)" "0" "enclosed pixels with no sky face"
# The mask's reach, printed before its verdict is quoted. On a sealed map the
# sky hull surrounds the play space, so the mask covers the frame; that is a
# property of the map and it is also the reason coverage alone cannot tell a
# brush seam from the open sky above the map. Both cases are sky-backed, so the
# number is asserted as a FINDING (`degenerate=1`) and the two are separated by
# the shape test below and by the bounding faces in section 8.
MASK_REACH=$(grep -F "HOLE maskReach " "$ANA/hole-account.txt")
echo "  $MASK_REACH"
assert_eq "$(field "$MASK_REACH" degenerate)" "1" "the sky hull is closed from this camera (the mask cannot discriminate)"
# The depth control cannot answer the behind/in-front question on this build:
# the debug depth view is 8-bit and saturates for everything past a few units.
# It is reported, not relied on -- `saturated=1` is the measured reason.
DEPTH_CONTROL=$(grep -F "HOLE depthControl " "$ANA/hole-account.txt")
echo "  $DEPTH_CONTROL"
assert_eq "$(field "$DEPTH_CONTROL" measured)" "1" "the depth control ran"
assert_eq "$(field "$DEPTH_CONTROL" saturated)" "1" "the depth view is saturated, so it is not used as evidence"

# The enclosed population, per component. Two things are asserted for each
# component of consequence: it is sky-backed, and it is needle-shaped. A missing
# FACE would be a compact blob; a seam between two surfaces is a sliver with a
# low fill ratio, and the distinction is what keeps this account from calling a
# crack a lost face.
enclosed_components=0
while IFS= read -r line; do
  area=$(field "$line" area)
  unexplained=$(field "$line" unexplained)
  edge=$(field "$line" edge)
  fill=$(field "$line" fill)
  bbox=$(sed -n 's/.*bbox=(x=\([0-9]*\)\.\.\([0-9]*\),y=\([0-9]*\)\.\.\([0-9]*\)).*/\1 \2 \3 \4/p' <<<"$line")
  [ "${area:-0}" -ge "$PIN_ENCLOSED_MIN_COMPONENT" ] || continue
  if [ "${edge:-1}" = "1" ]; then continue; fi
  enclosed_components=$((enclosed_components + 1))
  if [ "${unexplained:-1}" != "0" ]; then
    echo "  FAIL component ${line#HOLE component }"
    fail=1
  fi
  read -r x0 x1 y0 y1 <<<"$bbox"
  width=$((x1 - x0 + 1)); height=$((y1 - y0 + 1))
  thin=$((width < height ? width : height)); long=$((width < height ? height : width))
  if [ "$thin" -gt 32 ] || [ "$long" -lt 40 ]; then
    echo "  FAIL component ${line#HOLE component } is not a sliver (${width}x${height})"
    fail=1
  fi
  if [ -n "$fill" ] && "$PYTHON" -c "import sys; sys.exit(0 if float(sys.argv[1]) < 0.70 else 1)" "$fill"; then
    :
  else
    echo "  FAIL component ${line#HOLE component } fills its box (fill=$fill): a solid blob is a missing face, not a seam"
    fail=1
  fi
done < <(grep -F "HOLE component " "$ANA/hole-account.txt")
assert_eq "$enclosed_components" "$PIN_ENCLOSED_COMPONENTS" "enclosed components >= 200 px, each a sliver"
[ "$fail" = 0 ] && echo "  PASS every enclosed component is a thin sky-backed sliver"

echo "== 6b. the T23 account still reads the same =="
# The point of re-running T23's own tool on T26's frames is that the two tasks
# agree about the same pixels through two different programs. If T26's overlay
# rule had drifted from T23's, the `missing` totals would already differ here.
# --out keeps the tool's mask inside T26's own analysis directory: the tool
# defaults to evidence/t23-world-geometry/analysis, and writing there would edit
# another task's evidence from this one's gate (it did, and it was reverted).
"$PYTHON" world-geometry-accounting.py --dir "$CAP" --out "$ANA" \
  > "$ANA/t23-account.txt" 2>&1
if [ $? -ne 0 ]; then
  echo "  FAIL the T23 account refused the frame"
  tail -5 "$ANA/t23-account.txt" | sed 's/^/       /'
  fail=1
else
  T23=$(grep -F "ACCOUNTING totals " "$ANA/t23-account.txt")
  echo "  $T23"
  assert_eq "$(field "$T23" missing)" "$PIN_MISSING" "T23 and T26 agree on the missing population"
  assert_eq "$(field "$T23" backface)" "$PIN_BACKFACE" "back-face population (T11 winding unchanged)"
  assert_eq "$(field "$T23" visibility)" "0" "visibility population (T17 clip unchanged)"
fi

echo "== 7. the point oracle and the rasterizer =="
POINTS=$(grep -F "HOLE points=" "$ANA/hole-account.txt" | sed 's/^HOLE points=//')
if [ -z "$POINTS" ]; then
  echo "  FAIL the account named no representative points"; fail=1
else
  PT_TRACE="$SCRATCH/points.trace"
  if ! run_capture_trace "$SCRATCH/points.bmp" "$PT_TRACE" --world-hole-trace "$POINTS"; then
    echo "  FAIL the point trace run failed, or captured a tick other than $TICK"
    tail -10 "$SCRATCH/run.log" | sed 's/^/       /'
    fail=1
  else
    grep -a "gpu: world hole-trace" "$PT_TRACE" > "$ANA/point-trace.txt" || true
    SUMMARY=$(grep -a "gpu: world hole-trace-summary" "$PT_TRACE" | tail -1)
    echo "  ${SUMMARY:-no summary}"
    EMPTY_SKY=$(field "$SUMMARY" emptyWithSky)
    EMPTY_NONE=$(field "$SUMMARY" emptyWithoutSky)
    POINT_COUNT=$(grep -c "gpu: world hole-trace point=" "$ANA/point-trace.txt")
    assert_eq "$EMPTY_NONE" "0" "points with no sky face"
    if [ "${EMPTY_SKY:-0}" -ge 6 ] && [ "${EMPTY_SKY:-0}" -le "$POINT_COUNT" ]; then
      echo "  PASS $EMPTY_SKY of $POINT_COUNT traced points are empty with a sky face behind them"
    else
      echo "  FAIL emptyWithSky=$EMPTY_SKY of $POINT_COUNT points"
      fail=1
    fi
    # No --depth here on purpose: the 8-bit depth view saturates on this camera
    # (see section 6), so the tool's far-plane test is the depth statement it
    # makes, and the sky distance is checked by the oracle's own geometry.
    "$PYTHON" hole-point-verify.py --trace "$ANA/point-trace.txt" \
      --ids "$CAP/sky-ids.bmp" > "$ANA/point-verify.txt" 2>&1
    if [ $? -ne 0 ]; then
      echo "  FAIL the rasterizer disagreed with the point oracle"
      grep -F "MISMATCH" "$ANA/point-verify.txt" | head -5 | sed 's/^/       /'
      fail=1
    else
      echo "  PASS $(tail -1 "$ANA/point-verify.txt")"
    fi
    if cmp -s "$SCRATCH/points.bmp" "$CAP/textured.bmp"; then
      echo "  PASS the traced frame is the pinned textured frame (the trace changed no pixel)"
    else
      echo "  FAIL the traced frame differs from the textured frame"
      fail=1
    fi
  fi
fi

echo "== 8. what the empty pixels sit between =="
# The shape test says the enclosed population is slivers; it cannot say between
# what. Three instruments answer that from the map's own tables:
#   - the world face-id frame names the face on the other side of each sliver
#     (four directions, first drawn pixel within 64 px),
#   - world_geometry_probe --face-info turns an index into a material, a plane
#     and a displacement flag, and
#   - the derived raster rule (T41) says which side of each named face the eye is
#     on, from the BSP plane and the capture's own camera.
# The capture pose is the trace's LAST 'scene: camera' line before the frame was
# drawn -- NOT the once-per-run 'gpu: world vis eyeMap', which is the first
# drawn frame and a different scene pose. T41 made that mistake first; this gate
# now asserts the eye it classifies with against the trace line it came from.
BSP="$TF/maps/pl_odyssey.bsp"
SEAM_TRACE="$CAP/traces/world-ids.trace"
CAPTURE_EYE=""
if [ ! -s "$SEAM_TRACE" ]; then
  echo "  FAIL the world-ids capture left no trace at $SEAM_TRACE"
  fail=1
elif "$PYTHON" "$SCRATCH/seam-cull.py" eye --trace "$SEAM_TRACE" --tick "$TICK" \
    > "$ANA/seam-eye.txt" 2>&1; then
  sed 's/^/  /' "$ANA/seam-eye.txt"
  CAPTURE_EYE=$(sed -n 's/^EYE eye=\([^ ]*\) src=.*/\1/p' "$ANA/seam-eye.txt" | head -1)
  CAPTURE_SRC=$(sed -n 's/^EYE eye=[^ ]* src=\([^ ]*\) .*/\1/p' "$ANA/seam-eye.txt" | head -1)
  assert_eq "$CAPTURE_SRC" "demo" "the capture pose is the demo camera at tick $TICK"
  assert_eq "$CAPTURE_EYE" "$PIN_CAPTURE_EYE" "capture-pose eye from the trace's last scene camera"
else
  echo "  FAIL the world-ids trace does not name a pose at tick $TICK"
  sed 's/^/       /' "$ANA/seam-eye.txt"
  fail=1
fi

# The seam trace, counted rather than quoted. The old gate checked that ONE
# direction came from the cull-off frame; T44 replaces that with "every measured
# probe resolves" because a probe that resolves to nothing is an instrument
# failure the old assertion could not see, and because the count is a reading
# that has to travel with the verdict.
SEAM_ROWS=$(grep -E '^HOLE seam rank=' "$ANA/hole-account.txt" || true)
SEAM_POINTS=$(printf '%s\n' "$SEAM_ROWS" | grep -c '^HOLE seam rank=' || true)
PROBES=$(printf '%s\n' "$SEAM_ROWS" | grep -oE '(left|right|up|down)=[^[:space:]]+' | grep -c . || true)
RESOLVED=$(printf '%s\n' "$SEAM_ROWS" | grep -oE '(left|right|up|down)=[0-9^]' | grep -c . || true)
UNRESOLVED=$((PROBES - RESOLVED))
SEAM_CULLED=$(grep -F "HOLE seamCulled " "$ANA/hole-account.txt" | sed -n 's/.*directions=\([0-9]*\).*/\1/p')
SEAM_LINE=$(grep -F "HOLE seamFaces " "$ANA/hole-account.txt" || true)
SEAM_IDS=$(sed -n 's/.*[[:space:]]ids=//p' <<<"$SEAM_LINE")
echo "  seam points: ${SEAM_POINTS:-0}; direction probes: ${PROBES:-0}; resolved: ${RESOLVED:-0}; cull-off-only: ${SEAM_CULLED:-0}"
echo "  seam faces: ${SEAM_IDS:-none}"
assert_eq "$SEAM_POINTS" "$PIN_SEAM_POINTS" "seam representative points (four probes each)"
assert_eq "$PROBES" "$PIN_SEAM_PROBES" "seam direction probes measured at this pose"
assert_eq "$RESOLVED" "$PROBES" "every measured seam direction probe resolves to a face"
if [ "$UNRESOLVED" -ne 0 ]; then
  printf '%s\n' "$SEAM_ROWS" | awk '{bad=0; for (i=1;i<=NF;i++) if ($i ~ /^(left|right|up|down)=-$/) bad=1; if (bad) print "       " $0}'
fi
# The pre-T29 gate asserted that at least one sliver had a cull-off-only
# neighbour. After the near-plane fix no sliver at this pose is bracketed by a
# surface the product culls, and T41 derived why: all eight named seam faces are
# derived-FRONT, so a correct cull cannot remove them. The old >= 1 assertion
# encoded a pre-T29 artefact and is SUPERSEDED by the statement below; it is not
# relaxed in place. A seam face the cull wrongly removed is still caught here,
# because its neighbour would then resolve only in the all-off frame.
assert_eq "${SEAM_CULLED:-0}" "$PIN_SEAM_CULLED" "seam directions only the cull-off frame could name (a front seam neighbour was not culled)"
assert_eq "$SEAM_IDS" "$PIN_SEAM_IDS" "the eight named seam faces"

SKY_FACE_IDS=$(grep -o "sky=face=[0-9]*" "$ANA/point-trace.txt" 2>/dev/null \
  | sed 's/.*face=//' | sort -un | tr '\n' ',' | sed 's/,$//')
echo "  sky faces at the traced points: ${SKY_FACE_IDS:-none}"
if [ -z "$SEAM_IDS" ]; then
  echo "  FAIL no face bracketed an enclosed component (is world-ids.bmp missing?)"
  fail=1
else
  FACE_LIST="$SEAM_IDS"
  [ -n "$SKY_FACE_IDS" ] && FACE_LIST="$FACE_LIST,$SKY_FACE_IDS"
  "$PROBE" --tf-root "$TF" --map maps/pl_odyssey.bsp --face-info "$FACE_LIST" \
    > "$ANA/face-info.txt" 2>&1
  if [ $? -ne 0 ]; then
    echo "  FAIL the probe refused --face-info"
    tail -5 "$ANA/face-info.txt" | sed 's/^/       /'
    fail=1
  else
    REQUESTED=$(tr ',' '\n' <<<"$FACE_LIST" | grep -c .)
    RESOLVED_FACES=$(grep -c "geometry face-info index=" "$ANA/face-info.txt")
    ABSENT=$(grep -c "state=absent" "$ANA/face-info.txt")
    DISPLACED=$(grep "geometry face-info index=" "$ANA/face-info.txt" | grep -c "displacement=1" || true)
    SKY_KIND=$(grep "geometry face-info index=" "$ANA/face-info.txt" | grep -c "kind=sky" || true)
    assert_eq "$RESOLVED_FACES" "$REQUESTED" "every named face exists in the map's face table"
    assert_eq "$ABSENT" "0" "no face index was invented by a decode error"
    echo "  $DISPLACED of them are displacement faces, $SKY_KIND are sky faces"
    if [ "${DISPLACED:-0}" -ge 1 ]; then
      echo "  PASS at least one side of a sliver is a tessellated displacement cell"
    else
      echo "  FAIL no displacement face bounds a sliver: the seam account lost its subject"
      fail=1
    fi
    if [ "${SKY_KIND:-0}" -ge 1 ]; then
      echo "  PASS at least one traced empty pixel is backed by a sky face of the map"
    else
      echo "  FAIL no sky face was named at the traced points"
      fail=1
    fi
    grep "geometry face-info index=" "$ANA/face-info.txt" | head -12 | sed 's/^/       /'
  fi

  # ---- the derived side of the eight named faces -------------------------
  # The T41 rule applied per face at the capture's own eye, not per pixel:
  # D3D11_CULL_BACK with FrontCounterClockwise=FALSE, front iff
  # dot(emittedNormal, eye - p) > 0. A brush fan emits the plane normal whatever
  # the side bit says; a displacement face is flipped onto its outward. The rule
  # is a BSP+eye reading, so the cull switch cannot change it -- which is what
  # makes it a valid control while mutation M6 disables the cull.
  if [ -n "$CAPTURE_EYE" ]; then
    "$PYTHON" "$SCRATCH/seam-cull.py" classify --bsp "$BSP" --eye="$CAPTURE_EYE" \
      --faces "$SEAM_IDS" > "$ANA/seam-derived.txt" 2>&1
    if [ $? -ne 0 ]; then
      echo "  FAIL the derived-side classification refused (see $ANA/seam-derived.txt)"
      sed 's/^/       /' "$ANA/seam-derived.txt"
      fail=1
    else
      grep -E '^CLASS ' "$ANA/seam-derived.txt" | sed 's/^/       /'
      CLASS_SUMMARY=$(grep -F 'CLASS-SUMMARY ' "$ANA/seam-derived.txt")
      echo "  $CLASS_SUMMARY"
      DERIVED_BACK=$(field "$CLASS_SUMMARY" derivedBack)
      DERIVED_PARSED=$(field "$CLASS_SUMMARY" parsed)
      assert_eq "$DERIVED_BACK" "$PIN_SEAM_DERIVED_BACK" "named seam faces the derived raster rule calls back at this pose"
      assert_eq "$DERIVED_PARSED" "8" "every named seam face parsed from the BSP face table"
      if [ "${DERIVED_BACK:-1}" = "0" ]; then
        echo "  PASS all 8 named seam faces are derived-front: a correct cull leaves them drawn"
      fi
    fi
  else
    echo "  FAIL no capture eye, so the derived-side statement cannot be made"
    fail=1
  fi

  # ---- the positive control: the cull still removes a named non-seam face --
  # The seam statement above would also hold for a product that drew NOTHING,
  # so this control makes "the cull is on" a measurement. At the same pose it
  # names the non-seam face with the largest pixel population that has a face id
  # with the cull off and none with the cull on, records its derived side (back)
  # and refuses a culled frame that paints a pixel the unculled one does not.
  "$PYTHON" "$SCRATCH/seam-cull.py" cull --bsp "$BSP" --eye="$CAPTURE_EYE" \
    --seam "$SEAM_IDS" --ids "$CAP/world-ids.bmp" --ids-nocull "$CAP/world-ids-nocull.bmp" \
    > "$ANA/cull-control.txt" 2>&1
  if [ $? -ne 0 ]; then
    echo "  FAIL the cull positive control refused to read the frames"
    sed 's/^/       /' "$ANA/cull-control.txt"
    fail=1
  else
    grep -E '^CULL ' "$ANA/cull-control.txt" | head -24 | sed 's/^/       /'
    CULL_SUMMARY=$(grep -F 'CULL-SUMMARY ' "$ANA/cull-control.txt")
    echo "  $CULL_SUMMARY"
    CULL_REMOVED=$(field "$CULL_SUMMARY" removed)
    CULL_ONLY=$(field "$CULL_SUMMARY" onlyInCull)
    CULL_NONSEAM=$(field "$CULL_SUMMARY" nonSeam)
    CULL_TOP=$(field "$CULL_SUMMARY" topNonSeam)
    CULL_TOP_PIXELS=$(field "$CULL_SUMMARY" topNonSeamPixels)
    CULL_TOP_DERIVED=$(field "$CULL_SUMMARY" topNonSeamDerived)
    CULL_TOP_SIDE=$(field "$CULL_SUMMARY" topNonSeamSide)
    CULL_TOP_KIND=$(field "$CULL_SUMMARY" topNonSeamKind)
    assert_eq "$CULL_ONLY" "0" "the culled face-id frame is a subset of the unculled frame"
    if [ "${CULL_REMOVED:-0}" -ge 1 ]; then
      echo "  PASS the product cull removed $CULL_REMOVED face-id pixels at this pose"
    else
      echo "  FAIL the product cull removed no face-id pixel: the cull is not measurably on"
      fail=1
    fi
    if [ "${CULL_NONSEAM:-0}" -ge 1 ]; then
      echo "  PASS $CULL_NONSEAM non-seam face(s) lost pixels to the cull"
    else
      echo "  FAIL no non-seam face lost pixels to the cull"
      fail=1
    fi
    assert_eq "$CULL_TOP" "$PIN_CULL_TOP_FACE" "top non-seam face the product cull removed"
    assert_eq "$CULL_TOP_KIND" "disp" "the control face is a displacement face"
    assert_eq "$CULL_TOP_SIDE" "0" "the control face's BSP side bit"
    assert_eq "$CULL_TOP_DERIVED" "$PIN_CULL_TOP_DERIVED" "the control face's derived side (what a correct cull removes)"
    assert_eq "$CULL_TOP_PIXELS" "$PIN_CULL_TOP_PIXELS" "pixels the cull removed from the control face"
    case ",$SEAM_IDS," in
      *",$CULL_TOP,"*)
        echo "  FAIL the positive control face $CULL_TOP is one of the named seam faces"
        fail=1 ;;
      *)
        echo "  PASS the positive control face $CULL_TOP is not one of the 8 named seam faces" ;;
    esac
  fi
fi

if [ "$MODE" != "--mutation" ]; then
  echo "== verdict =="
  if [ "$fail" = 0 ]; then
    echo "PASS: every empty pixel in the reference frame is covered by the map's own sky brush; there is no missing geometry to fix, and the one winding repair the oracle can see is a measured regression."
    exit 0
  fi
  echo "FAIL: $fail assertion groups did not hold."
  exit 1
fi

# ---------------------------------------------------------------------------
# mutation mode
# ---------------------------------------------------------------------------
# Each mutation removes one rule the forward verdict stands on and the gate must
# go red for it. "Red" is not eyeballed: the focused run re-executes the SAME
# equality assertions the forward run made -- the pinned missing/sky/unexplained/
# enclosed numbers for the mask mutations, the per-point face decode for the
# face-id mutation -- and the mutation is caught only if one of them disagrees.
echo "== mutation mode =="
mfail=0
SRC_BSP=native/src/bsp_map.cpp
SRC_REN=native/src/native_renderer.cpp
SRC_MAIN=native/src/main.cpp
TOOL_ACCOUNT=world-geometry-hole-account.py
SAVE_BSP="$SCRATCH/mutation.bsp_map.cpp"
SAVE_REN="$SCRATCH/mutation.native_renderer.cpp"
SAVE_MAIN="$SCRATCH/mutation.main.cpp"
SAVE_ACCOUNT="$SCRATCH/mutation.world-geometry-hole-account.py"
cp "$SRC_BSP" "$SAVE_BSP"
cp "$SRC_REN" "$SAVE_REN"
cp "$SRC_MAIN" "$SAVE_MAIN"
cp "$TOOL_ACCOUNT" "$SAVE_ACCOUNT"
RESTORED=0
MUTATION_ACTIVE=0
restore_sources() {
  cp "$SAVE_BSP" "$SRC_BSP"
  cp "$SAVE_REN" "$SRC_REN"
  cp "$SAVE_MAIN" "$SRC_MAIN"
  cp "$SAVE_ACCOUNT" "$TOOL_ACCOUNT"
  MUTATION_ACTIVE=0
  if cmp -s "$SAVE_BSP" "$SRC_BSP" && cmp -s "$SAVE_REN" "$SRC_REN" \
      && cmp -s "$SAVE_MAIN" "$SRC_MAIN" && cmp -s "$SAVE_ACCOUNT" "$TOOL_ACCOUNT"; then
    echo "  RESTORE=OK (four files byte-identical)"
  else
    echo "  RESTORE=FAIL"; mfail=1
  fi
}
# A mutation left in the tree after an interrupt would be worse than a red gate.
# The binary is rebuilt at the end, so an interrupt can leave a mutant product
# behind; the restore of the source is what stops it from being committed.
trap 'if [ "$MUTATION_ACTIVE" = 1 ]; then echo "  aborted mid-mutation: restoring sources" >&2; restore_sources; fi' EXIT

build_mutated() { # build_mutated <label>
  if ! bash build-target.sh tf2_demo_native world_geometry_probe > "$SCRATCH/build-$1.txt" 2>&1; then
    echo "  FAIL $1 does not build"
    tail -6 "$SCRATCH/build-$1.txt" | sed 's/^/       /'
    mfail=1
    return 1
  fi
  return 0
}

# The frames the focused accounting needs that the mutation cannot change: the
# world pass is untouched by every mutation below except M5, and M5 mutates the
# account tool, not the picture.
stock_frames() { # stock_frames <dir>
  local dir="$1"
  rm -rf "$dir"; mkdir -p "$dir"
  local frame
  for frame in flat-base flat-novis flat-nocull flat-novis-nocull none textured; do
    cp "$CAP/$frame.bmp" "$dir/" || { echo "  FAIL $frame is missing from $CAP"; mfail=1; return 1; }
  done
}

capture_sky_mask() { # capture_sky_mask <dir> [extra flags...]
  local dir="$1"; shift
  run_capture_trace "$dir/sky-faces.bmp" "$dir/capture.trace" \
    --world-geometry sky --no-world-cull --world-flat "$@"
}

# Returns 0 when at least one pinned number moved -- i.e. when the forward gate
# would have failed. Prints which ones, so a mutation that only half-breaks the
# account is still visible in the log.
judge_account() { # judge_account <label> <dir>
  # Two statements, not one `local`: bash expands every word of the `local`
  # command before the builtin runs, so `report="$SCRATCH/account-$label.txt"`
  # in the same line reads `label` while it is still unset, and `set -u` kills
  # the run there -- which is what happened on the first M2 attempt.
  local label="$1"
  local dir="$2"
  local report="$SCRATCH/account-$label.txt"
  if ! "$PYTHON" world-geometry-hole-account.py --dir "$dir" --out "$SCRATCH/mask-$label" \
      > "$report" 2>&1; then
    echo "  PASS $label: the account refuses the frame -- $(head -1 "$report")"
    return 0
  fi
  local totals split broke=""
  totals=$(grep -F "HOLE totals " "$report")
  split=$(grep -F "HOLE split " "$report")
  [ "$(field "$totals" missing)" = "$PIN_MISSING" ] \
    || broke="$broke missing=$(field "$totals" missing)"
  [ "$(field "$totals" sky)" = "$PIN_SKY_MISSING" ] \
    || broke="$broke sky=$(field "$totals" sky)"
  [ "$(field "$totals" unexplained)" = "$PIN_UNEXPLAINED" ] \
    || broke="$broke unexplained=$(field "$totals" unexplained)"
  [ "$(field "$split" enclosed)" = "$PIN_ENCLOSED" ] \
    || broke="$broke enclosed=$(field "$split" enclosed)"
  [ "$(field "$split" enclosedUnexplained)" = "0" ] \
    || broke="$broke enclosedUnexplained=$(field "$split" enclosedUnexplained)"
  if [ -n "$broke" ]; then
    echo "  PASS $label: the gate goes red:$broke"
    return 0
  fi
  echo "  FAIL $label: every pinned number survived -- the mutation is not covered"
  mfail=1
  return 1
}

# M1: the parser still counts the sky faces and no longer emits their polygons.
# The census line the probe prints stays at 731, so the count cannot notice; what
# notices is everything downstream of the geometry. This mutation has exactly two
# acceptable reds and the gate names which one it got:
#   - an empty mask, caught by the account (sky=0, unexplained=38328), or
#   - a refusal to upload nothing at all, which is what this product does: the
#     uploader's own guard rejects an empty source, the reload fails, and the
#     process exits 14 before the first draw.
# The first attempt judged only the first red and, on the second, retried a
# refusal four times and then skipped the judgement. That is worse than a wrong
# assertion: it is a mutation that passes for the wrong reason.
echo "-- M1: the parser keeps the sky count and drops the polygons --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_BSP" <<'PATCH_M1'
        triangle.faceIndex = faceIndex;
        map.skyTriangles.push_back(std::move(triangle));
%%
        triangle.faceIndex = faceIndex;
        (void)triangle;  // T26 mutation M1: the census stays, the polygon is gone.
PATCH_M1
if [ $? -ne 0 ]; then
  echo "  FAIL M1 could not patch the parser"; mfail=1
elif build_mutated M1 && stock_frames "$SCRATCH/m1"; then
  capture_sky_mask "$SCRATCH/m1"; m1_status=$?
  M1_TRACE="$SCRATCH/m1/capture.trace"
  case "$m1_status" in
    0)
      judge_account M1 "$SCRATCH/m1" ;;
    2)
      m1_draws=$(grep -ac "draw: begin tick=" "$M1_TRACE" 2>/dev/null)
      m1_atlas=$(grep -ac "state=no world atlas was uploaded" "$M1_TRACE" 2>/dev/null)
      m1_frame=no; [ -e "$SCRATCH/m1/sky-faces.bmp" ] && m1_frame=yes
      if [ "$LAST_PRODUCT_RC" = 14 ] && [ "$m1_draws" = 0 ] && [ "$m1_atlas" -ge 1 ] \
          && [ "$m1_frame" = no ]; then
        echo "  PASS M1: no sky polygon means no upload -- exit 14, no draw, no atlas, no frame"
      else
        echo "  FAIL M1: the run stopped, but not as an empty sky buffer must (rc=$LAST_PRODUCT_RC draws=$m1_draws atlas=$m1_atlas frame=$m1_frame)"
        mfail=1
      fi
      ;;
    *)
      echo "  FAIL M1: the sky mask neither landed nor was refused (status $m1_status)"
      mfail=1
      ;;
  esac
fi
restore_sources

# M2: the sky buffer is filled from the world triangles. The mask is then the
# world's own coverage, so it covers every drawn pixel and none of the empty
# ones -- the exact answer the task had to rule out before it could blame
# geometry.
echo "-- M2: the sky pass uploads the world triangles --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_REN" <<'PATCH_M2'
  const std::vector<BspTriangle>& source = worldSkyFaceOnly_ ? map.skyTriangles : map.triangles;
%%
  const std::vector<BspTriangle>& source = map.triangles;  // T26 mutation M2
PATCH_M2
if [ $? -ne 0 ]; then
  echo "  FAIL M2 could not patch the uploader"; mfail=1
elif build_mutated M2 && stock_frames "$SCRATCH/m2"; then
  capture_sky_mask "$SCRATCH/m2"; m2_status=$?
  if [ "$m2_status" = 0 ]; then
    judge_account M2 "$SCRATCH/m2"
  else
    echo "  FAIL M2: the sky mask could not be captured (status $m2_status)"
    mfail=1
  fi
fi
restore_sources

# M3: the face-id view returns a constant. The point oracle's answer is
# unchanged (it is CPU), so this isolates the second instrument: an id frame
# that cannot name a face must be caught by the decode, not by the accounting.
echo "-- M3: the face-id view returns a constant --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_REN" <<'PATCH_M3'
    if (envFeatures.x > 5.5) return float4(colour.rgb, 1.0);
%%
    if (envFeatures.x > 5.5) return float4(0.25, 0.25, 0.25, 1.0);  // T26 mutation M3
PATCH_M3
if [ $? -ne 0 ]; then
  echo "  FAIL M3 could not patch the shader"; mfail=1
elif build_mutated M3; then
  mkdir -p "$SCRATCH/m3"
  if ! run_capture_trace "$SCRATCH/m3/sky-ids.bmp" "$SCRATCH/focused.trace" \
      --world-geometry sky --no-world-cull --world-face-id; then
    echo "  FAIL M3 capture"; mfail=1
  elif "$PYTHON" hole-point-verify.py --trace "$ANA/point-trace.txt" \
      --ids "$SCRATCH/m3/sky-ids.bmp" > "$SCRATCH/verify-M3.txt" 2>&1; then
    echo "  FAIL M3: the decoded ids still agreed with the point oracle"
    mfail=1
  else
    echo "  PASS M3: $(tail -1 "$SCRATCH/verify-M3.txt")"
  fi
fi
restore_sources

# M4: the sky buffer is drawn through its own bounds instead of the world's.
# Every sky pixel is still a sky pixel, but at the wrong scale and offset. The
# coverage account cannot see this -- the hull covers every direction, so a
# mask that is merely "somewhere near" still covers the frame, and that limit is
# stated in the delivery report. What can see it is the second instrument: the
# rasterizer's face id at a traced pixel has to be the face the CPU oracle
# projected with the frame's own matrix, and a moved buffer puts a different
# face (or none) there.
echo "-- M4: the sky mask gets its own transform --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_REN" <<'PATCH_M4'
  for (const auto& triangle : map.triangles) {
    for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
      minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
%%
  for (const auto& triangle : source) {  // T26 mutation M4: the mask's own bounds
    for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
      minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
PATCH_M4
if [ $? -ne 0 ]; then
  echo "  FAIL M4 could not patch the bounds"; mfail=1
elif build_mutated M4; then
  mkdir -p "$SCRATCH/m4"
  if ! run_capture_trace "$SCRATCH/m4/sky-ids.bmp" "$SCRATCH/focused.trace" \
      --world-geometry sky --no-world-cull --world-face-id; then
    echo "  FAIL M4 capture"; mfail=1
  elif "$PYTHON" hole-point-verify.py --trace "$ANA/point-trace.txt" \
      --ids "$SCRATCH/m4/sky-ids.bmp" > "$SCRATCH/verify-M4.txt" 2>&1; then
    echo "  FAIL M4: the ids still matched after the buffer moved"
    mfail=1
  else
    echo "  PASS M4: $(tail -1 "$SCRATCH/verify-M4.txt")"
  fi
fi
restore_sources

# M5: the account takes its mask from the product's own picture instead of the
# all-switches-off frame. This is the definition the whole task stands on: with
# the cull and the pvs clip off, "no triangle" means no triangle; without them it
# means "no triangle the product kept", and a back-face or a clipped surface
# would read as a hole. The frames are the forward frames, unmutated; only the
# frame the mask is taken from changes, and the pinned total must move.
#
# The first version of M5 dropped the overlay exclusion instead, and the gate
# refused it: measured on these frames `overlay=0` (tick 2000 draws no HUD), so
# the mutation changed no number. A mutation that cannot move a pin is not
# coverage, and this one is not claimed as such -- the overlay exclusion stays in
# the tool for frames that carry a HUD and is documented as inert here.
echo "-- M5: the account reads empty off the product's picture --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$TOOL_ACCOUNT" <<'PATCH_M5'
    missing = bytearray(1 if (not present_all_off[index]) and not overlay[index] else 0
                        for index in range(total))
%%
    missing = bytearray(1 if (not present_base[index]) and not overlay[index] else 0
                        for index in range(total))  # T26 mutation M5
PATCH_M5
if [ $? -ne 0 ]; then
  echo "  FAIL M5 could not patch the account"; mfail=1
else
  judge_account M5 "$CAP"
fi
restore_sources

# M6: the world back-face cull never runs. The forward verdict now stands on two
# independent statements: the seam classification (0 of 8 named faces are
# derived-back; a BSP+eye reading the product cannot change) and the positive
# control (the cull removes a named non-seam face). This mutation deletes the
# second: with the cull disabled the culled frame equals the unculled one, so the
# control finds nothing removed and must go red. The classification must NOT
# move -- if it did, the seam answer would be reading the cull instead of the
# geometry, and the mutation would be proving the wrong thing.
echo "-- M6: the world back-face cull never runs --"
MUTATION_ACTIVE=1
"$PYTHON" patch-source.py "$SRC_MAIN" <<'PATCH_M6'
        renderer.setWorldBackfaceCull(worldCullGiven ? worldCullRequested : true);
%%
        renderer.setWorldBackfaceCull(false);  // T44 mutation M6: the cull never runs
PATCH_M6
if [ $? -ne 0 ]; then
  echo "  FAIL M6 could not patch the cull switch"; mfail=1
elif build_mutated M6; then
  mkdir -p "$SCRATCH/m6"
  if ! run_capture_trace "$SCRATCH/m6/world-ids.bmp" "$SCRATCH/m6/world-ids.trace" \
      --world-face-id --world-water off; then
    echo "  FAIL M6 capture"; mfail=1
  else
    M6_EYE=$("$PYTHON" "$SCRATCH/seam-cull.py" eye --trace "$SCRATCH/m6/world-ids.trace" \
      --tick "$TICK" | sed -n 's/^EYE eye=\([^ ]*\).*/\1/p' | head -1)
    if [ "$M6_EYE" != "$CAPTURE_EYE" ]; then
      echo "  FAIL M6: the mutated build drew from a different pose ($M6_EYE, forward $CAPTURE_EYE)"
      mfail=1
    else
      "$PYTHON" "$SCRATCH/seam-cull.py" classify --bsp "$BSP" --eye="$M6_EYE" \
        --faces "$PIN_SEAM_IDS" > "$SCRATCH/m6/seam-derived.txt" 2>&1
      M6_CLASS=$(grep -F 'CLASS-SUMMARY ' "$SCRATCH/m6/seam-derived.txt")
      M6_DERIVED_BACK=$(field "$M6_CLASS" derivedBack)
      echo "  seam classification with the cull disabled: $M6_CLASS"
      "$PYTHON" "$SCRATCH/seam-cull.py" cull --bsp "$BSP" --eye="$M6_EYE" \
        --seam "$PIN_SEAM_IDS" --ids "$SCRATCH/m6/world-ids.bmp" \
        --ids-nocull "$CAP/world-ids-nocull.bmp" > "$SCRATCH/m6/cull-control.txt" 2>&1
      M6_CULL=$(grep -F 'CULL-SUMMARY ' "$SCRATCH/m6/cull-control.txt")
      echo "  $M6_CULL"
      M6_REMOVED=$(field "$M6_CULL" removed)
      M6_ONLY=$(field "$M6_CULL" onlyInCull)
      if [ "${M6_REMOVED:-0}" -ne 0 ]; then
        echo "  FAIL M6: the positive control still found $M6_REMOVED cull-removed pixels -- the mutation did not disable the cull"
        mfail=1
      elif [ "${M6_ONLY:-1}" -ne 0 ]; then
        echo "  FAIL M6: the disabled-cull frame is not a subset of the unculled one (onlyInCull=$M6_ONLY)"
        mfail=1
      elif [ "${M6_DERIVED_BACK:-1}" -ne 0 ]; then
        echo "  FAIL M6: the seam classification stopped being meaningful: $M6_CLASS"
        mfail=1
      else
        echo "  PASS M6: the cull removed 0 px (positive control RED) while 8 of 8 named seam faces are still derived-front -- the seam answer is geometry, not the cull"
      fi
    fi
  fi
fi
restore_sources

# The last mutation's build is a mutant binary. Rebuilding from the restored
# sources and re-capturing the pinned frame is the only statement that matters:
# byte-identical sources have to produce a byte-identical picture, or the gate
# has been judging a product that is not the one the tree describes.
echo "-- after restore: the pinned frame comes back --"
if build_mutated RESTORE; then
  RESTORED_CAPTURE="$SCRATCH/restored-textured.bmp"
  rm -f "$RESTORED_CAPTURE"
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$SCRATCH/restored.trace")" timeout 180 "$PRODUCT" \
    --tf-root "$TF" --demo "$DEMO" --capture-tick "$TICK" \
    --capture-frame "$(cygpath -m "$PWD/$RESTORED_CAPTURE")" \
    --audio-disabled --hidden-window --world-water off > "$SCRATCH/restored.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "  FAIL the restored build could not capture"; mfail=1
  elif cmp -s "$RESTORED_CAPTURE" "$CAP/textured.bmp"; then
    echo "  PASS the restored build reproduces the pinned textured frame byte for byte"
  else
    echo "  FAIL the restored build draws a different frame than the pin"
    echo "       restored $(sha256sum "$RESTORED_CAPTURE" | cut -d' ' -f1)"
    echo "       pinned   $(sha256sum "$CAP/textured.bmp" | cut -d' ' -f1)"
    mfail=1
  fi
fi

echo "== mutation verdict =="
if [ "$mfail" = 0 ]; then
  echo "PASS: all six mutations turned the gate red and every source came back byte-identical."
  exit 0
fi
echo "FAIL: a mutation was not caught, or a source was not restored. The tree may be dirty -- check 'git status native/' before continuing."
exit 1
