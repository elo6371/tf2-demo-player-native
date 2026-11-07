#!/usr/bin/env python3
"""world-geometry-budget-faces.py -- name the faces the T88 pixel attribution talks about.

WHY THIS FILE EXISTS
  world-geometry-budget-pixels.py can only say "this changed pixel belongs to a face
  that was already drawn". That is enough to notice a surprise but not enough to
  explain one, and T88 learned the hard way that the explanation matters: the pre-fix
  build refuses a displacement GRID when the triangle budget is full, then still
  pushes the face's flat fallback fan (native/src/bsp_map.cpp:1007-1043), so faces past
  firstDroppedFace can still be on screen as flat quads. A later pixel can therefore
  legitimately move from one of those faces to the sky when the fixed build draws the
  real (curved) grid instead. To tell that apart from a regression, the pixel tool
  needs two face lists from the map itself:

    disp      every face that carries a dispinfo (BSP lump 26, mapFace at +36)
    restored  every face that belongs to a func_illusionary brush model -- the faces
              the whitelist fix put back into the world buffer (lump 0 entities,
              lump 14 dmodel_t firstface at +40 / numfaces at +44)

  Both are facts about the .bsp, not about a build, so one extraction serves any run.

Reading the header: the lump directory is 64 entries of 8 bytes starting at offset 8
(offset int32, length int32); entry i is at 8 + i*16.

Usage:
  world-geometry-budget-faces.py --bsp <map.bsp> [--disp-out f] [--restored-out f]
Exit: 0 written/listed; 2 the file is not a readable BSP.

Written by T88 (map axis). Read-only: it never touches the map.
"""
import argparse
import re
import struct
import sys

EXIT_OK, EXIT_ERROR = 0, 2

LUMP_ENTITIES = 0
LUMP_MODELS = 14
LUMP_DISPINFO = 26
DMODEL_BYTES = 48
DMODEL_FIRSTFACE = 40
DMODEL_NUMFACES = 44
DISPINFO_BYTES = 176
DISPINFO_MAPFACE = 36


def read_lump(data, index):
    if len(data) < 8 + 64 * 16:
        raise SystemExit("not a BSP: header shorter than the 64-entry lump directory")
    offset, length = struct.unpack_from("<ii", data, 8 + index * 16)
    if offset < 0 or length < 0 or offset + length > len(data):
        raise SystemExit("not a BSP: lump %d runs past the end of the file" % index)
    return data[offset:offset + length]


def displacement_faces(data):
    lump = read_lump(data, LUMP_DISPINFO)
    faces = set()
    for i in range(len(lump) // DISPINFO_BYTES):
        faces.add(struct.unpack_from("<H", lump, i * DISPINFO_BYTES + DISPINFO_MAPFACE)[0])
    return sorted(faces)


def illusionary_faces(data):
    text = read_lump(data, LUMP_ENTITIES).decode("latin-1")
    models = read_lump(data, LUMP_MODELS)
    count = len(models) // DMODEL_BYTES
    faces = set()
    for match in re.finditer(r"\{([^}]*)\}", text):
        pair = dict(re.findall(r'"([^"]+)"\s+"([^"]*)"', match.group(1)))
        if pair.get("classname") != "func_illusionary":
            continue
        model = pair.get("model", "")
        if not model.startswith("*"):
            continue
        index = int(model[1:])
        if index >= count:
            continue
        firstface = struct.unpack_from("<i", models, index * DMODEL_BYTES + DMODEL_FIRSTFACE)[0]
        numfaces = struct.unpack_from("<i", models, index * DMODEL_BYTES + DMODEL_NUMFACES)[0]
        faces.update(range(firstface, firstface + numfaces))
    return sorted(faces)


def emit(path, faces):
    if not path:
        return
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(str(i) for i in faces) + "\n")


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--bsp", required=True)
    parser.add_argument("--disp-out")
    parser.add_argument("--restored-out")
    args = parser.parse_args(argv)
    try:
        with open(args.bsp, "rb") as handle:
            data = handle.read()
    except OSError as error:
        raise SystemExit("cannot read %s: %s" % (args.bsp, error))
    if data[:4] != b"VBSP":
        raise SystemExit("%s is not a VBSP file" % args.bsp)
    disp = displacement_faces(data)
    restored = illusionary_faces(data)
    emit(args.disp_out, disp)
    emit(args.restored_out, restored)
    print("disp=%d restored=%d" % (len(disp), len(restored)))
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
