#!/usr/bin/env python3
"""install-readiness-fixtures.py -- T32 (2026-10-20): build the clean-room tf roots
that install-readiness-check.sh drives the installed product against.

WHY THIS FILE EXISTS
  The store matrix has to run against an install tree the product did NOT find on
  this machine: the developer's D:/SteamLibrary install is a real root, so a run
  that used it could pass without ever exercising the precedence rules. This
  script builds small synthetic tf roots whose ONLY job is to make one store
  answer and the others not, plus one demo whose 260-byte header map field names
  the fixture map. The product sees a normal root and reports its own store
  reading; nothing here is a second implementation of the lookup.

WHAT EACH ROOT PROVES
  precedence     loose maps/t32_marker.bsp is a real BSP; pak01_dir.vpk carries
                 the SAME name with bytes that are not a BSP. The store the
                 product reports is therefore the precedence reading: loose must
                 answer first, and the title's BSP segment must read a positive
                 triangle count. A product that read the VPK first would report
                 store=vpk and an invalid BSP.
  vpk-only       no loose map; the same valid BSP is an inline VPK entry.
  download-maps  the map exists only under download/maps; maps/ holds another
                 name so the root is structurally valid but the requested map is
                 not in the first store.
  missing-map    no store has the name; the product must report store=none.
  vpk-corrupt    pak01_dir.vpk is present and its directory tree names an inline
                 entry whose offset is 0xFFFFFFF0: > 4 GiB past the tree end, so
                 a 32-bit narrowing wraps and accepts it. The forward product
                 must refuse the archive by name ("VPK entry range outside
                 directory archive"); the T32 corruption mutation restores the
                 32-bit narrowing and the gate then observes the acceptance.
  vpk-empty      0-byte pak01_dir.vpk -> "empty VPK file".
  vpk-truncated  8-byte header (magic+version, no tree size) -> "truncated VPK
                 header".

The fixtures are rebuilt from the pinned demo and the pinned map every run, so
they cannot drift from the real inputs they depend on. They are machine products:
they go under .scratch/, and only their SHA-256 list is committed.

USAGE
  python install-readiness-fixtures.py --out <dir> --demo <real.dem> --bsp <map.bsp>
Exit 0 when every fixture and the demo rewrite were written and hashed.
"""

import argparse
import hashlib
import os
import shutil
import struct
import sys

SIGNATURE = 0x55AA1234
DIRECTORY_ARCHIVE = 0x7FFF
MAP_NAME_FIELD_OFFSET = 536
MAP_NAME_FIELD_LENGTH = 260
FIXTURE_MAP = "t32_marker"
CORRUPT_PAYLOAD = b"NOT-A-BSP-T32-CORRUPT-VPK-ENTRY"


def put32(buffer, value):
    buffer.extend(struct.pack("<I", value & 0xFFFFFFFF))


def put16(buffer, value):
    buffer.extend(struct.pack("<H", value & 0xFFFF))


def put_string(buffer, value):
    buffer.extend(value.encode("ascii"))
    buffer.append(0)


def build_tree(files):
    """A v1 directory tree: extension, path, filename, 18 fixed bytes, terminator."""
    tree = bytearray()
    for spec in files:
        put_string(tree, spec["extension"])
        put_string(tree, spec["path"])
        put_string(tree, spec["filename"])
        put32(tree, 0)                                    # crc
        put16(tree, spec.get("preload", 0))               # preload bytes
        put16(tree, spec.get("archive_index", DIRECTORY_ARCHIVE))
        put32(tree, spec["offset"])
        put32(tree, spec["length"])
        put16(tree, 0xFFFF)                               # entry terminator
        tree.append(0)                                    # closes filename loop
        tree.append(0)                                    # closes path loop
    tree.append(0)                                        # closes extension loop
    return bytes(tree)


def build_archive(files, trailing=b""):
    tree = build_tree(files)
    out = bytearray()
    put32(out, SIGNATURE)
    put32(out, 1)
    put32(out, len(tree))
    out.extend(tree)
    out.extend(trailing)
    return bytes(out)


def inline_archive(entries):
    """entries: list of (path, filename, payload). Payload is appended after the tree."""
    files = []
    data = bytearray()
    for path, filename, payload in entries:
        files.append({"extension": "bsp", "path": path, "filename": filename,
                      "offset": len(data), "length": len(payload)})
        data.extend(payload)
    return build_archive(files, trailing=bytes(data))


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)


def write_markers(root):
    write(os.path.join(root, "materials", "t32", "marker.vmt"),
          b'"UnlitGeneric"\n{\n\t"$basetexture" "t32/marker"\n}\n')
    write(os.path.join(root, "models", "t32", "marker.mdl"), b"T32-INSTALL-READINESS-MODEL-MARKER")
    write(os.path.join(root, "materials", "t32", "marker.vtf"), b"T32-INSTALL-READINESS-TEXTURE-MARKER")


def rewrite_demo(source, destination, map_name):
    with open(source, "rb") as handle:
        data = bytearray(handle.read())
    if len(data) < MAP_NAME_FIELD_OFFSET + MAP_NAME_FIELD_LENGTH:
        raise SystemExit("demo is shorter than an HL2DEMO header: %s" % source)
    name = map_name.encode("ascii")
    if len(name) >= MAP_NAME_FIELD_LENGTH:
        raise SystemExit("fixture map name does not fit the header field")
    data[MAP_NAME_FIELD_OFFSET:MAP_NAME_FIELD_OFFSET + MAP_NAME_FIELD_LENGTH] = \
        name + b"\x00" * (MAP_NAME_FIELD_LENGTH - len(name))
    write(destination, bytes(data))


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--demo", required=True)
    parser.add_argument("--bsp", required=True)
    args = parser.parse_args(argv)
    out = os.path.abspath(args.out)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)

    with open(args.bsp, "rb") as handle:
        bsp = handle.read()
    if len(bsp) < 8 or bsp[:4] != b"VBSP":
        raise SystemExit("input map is not a VBSP file: %s" % args.bsp)

    demo = os.path.join(out, "demo", FIXTURE_MAP + ".dem")
    rewrite_demo(args.demo, demo, FIXTURE_MAP)

    roots = os.path.join(out, "roots")
    map_relative = os.path.join("maps", FIXTURE_MAP + ".bsp")
    other_map_relative = os.path.join("maps", "t32_other.bsp")

    # 1. precedence: loose BSP valid, same name inside the VPK is not a BSP.
    root = os.path.join(roots, "precedence")
    write_markers(root)
    write(os.path.join(root, map_relative), bsp)
    write(os.path.join(root, "pak01_dir.vpk"),
          inline_archive([("maps", FIXTURE_MAP, CORRUPT_PAYLOAD)]))

    # 2. vpk-only: the valid BSP exists only as an inline VPK entry.
    root = os.path.join(roots, "vpk-only")
    write_markers(root)
    write(os.path.join(root, "pak01_dir.vpk"),
          inline_archive([("maps", FIXTURE_MAP, bsp)]))

    # 3. download-maps: the requested map exists only under download/maps.
    root = os.path.join(roots, "download-maps")
    write_markers(root)
    write(os.path.join(root, other_map_relative), bsp)
    write(os.path.join(root, "download", map_relative), bsp)

    # 4. missing-map: structurally valid root, the requested name in no store.
    root = os.path.join(roots, "missing-map")
    write_markers(root)
    write(os.path.join(root, other_map_relative), bsp)

    # 5. corrupt: a tree that names an inline entry at 0xFFFFFFF0 (>4 GiB out).
    root = os.path.join(roots, "vpk-corrupt")
    write_markers(root)
    write(os.path.join(root, "pak01_dir.vpk"), build_archive([
        {"extension": "bsp", "path": "maps", "filename": FIXTURE_MAP,
         "offset": 0xFFFFFFF0, "length": 4},
    ]))

    # 6. empty: a zero-byte archive the product must name as empty, not as absent.
    root = os.path.join(roots, "vpk-empty")
    write_markers(root)
    write(os.path.join(root, "pak01_dir.vpk"), b"")

    # 7. truncated: magic and version present, no tree-size field.
    root = os.path.join(roots, "vpk-truncated")
    write_markers(root)
    header = bytearray()
    put32(header, SIGNATURE)
    put32(header, 1)
    write(os.path.join(root, "pak01_dir.vpk"), bytes(header))

    # 8. empty: three store directories and not one file in them. A path that
    #    exists but names no resources is the "half-installed" case, and the
    #    product must refuse it exactly like a path that does not exist.
    root = os.path.join(roots, "empty")
    for directory in ("materials", "models", "maps"):
        os.makedirs(os.path.join(root, directory), exist_ok=True)

    hashes = []
    for directory, _subdirs, filenames in os.walk(out):
        for filename in sorted(filenames):
            path = os.path.join(directory, filename)
            relative = os.path.relpath(path, out).replace("\\", "/")
            hashes.append((relative, os.path.getsize(path), sha256_of(path)))
    hashes.sort()
    with open(os.path.join(out, "inputs.sha256"), "w", encoding="ascii", newline="\n") as handle:
        for relative, size, digest in hashes:
            handle.write("%s  %d  %s\n" % (digest, size, relative))

    print("fixtures_root=%s" % out)
    print("fixture_demo=%s" % demo)
    print("fixture_roots=%d" % len(os.listdir(roots)))
    print("fixture_files=%d" % len(hashes))
    for relative, size, digest in hashes:
        if relative.startswith("roots/") and relative.endswith("_dir.vpk"):
            print("fixture_vpk=%s bytes=%d sha256=%s" % (relative, size, digest))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
