#!/usr/bin/env python3
"""bsp_face.py -- read one dface out of a Source BSP without the product.

WHY THIS EXISTS
    N05's remaining question is what faces 6343 and 15566 ARE. The product's
    traces give ids and materials but not the two properties that decide the
    answer: whether the face is displaced, and which texture it names. Both are
    plain lump reads, so they do not need a product change, a build, or the lock.

USAGE
    python bsp_face.py <map.bsp> <face> [<face> ...]

WHAT IT DOES NOT DO
    It does not decide what the renderer does with the face. It reports the
    data: texture name, displacement presence, surface flags, and the plane.
"""
import struct
import sys

LUMP_ENTITIES = 0
LUMP_PLANES = 1
LUMP_TEXDATA = 2
LUMP_TEXINFO = 6
LUMP_FACES = 7
LUMP_DISPINFO = 26
LUMP_TEXDATA_STRING_TABLE = 43
LUMP_TEXDATA_STRING_DATA = 44

SURF_SKY = 0x4
SURF_NODRAW = 0x80
SURF_TRIGGER = 0x40
SURF_NOLIGHT = 0x400


def lumps(data):
    # VBSP header: magic 'VBSP' at 0, version at 4, 64 lump directory entries from 8.
    if data[:4] != b"VBSP":
        raise SystemExit("not a Source BSP: magic=%r" % data[:4])
    version = struct.unpack_from("<i", data, 4)[0]
    out = {}
    for i in range(64):
        off, length = struct.unpack_from("<ii", data, 8 + i * 16)
        out[i] = (off, length)
    return version, out


def cstr(buf, at):
    end = buf.find(b"\x00", at)
    return buf[at:end if end >= 0 else len(buf)].decode("latin-1", "replace")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    path = sys.argv[1]
    data = open(path, "rb").read()
    version, lump = lumps(data)
    print("map=%s version=%d size=%d" % (path, version, len(data)))

    # texture names: string table gives offsets into the string data block
    so, sl = lump[LUMP_TEXDATA_STRING_TABLE]
    do, dl = lump[LUMP_TEXDATA_STRING_DATA]
    strtab = [struct.unpack_from("<i", data, so + i * 4)[0] for i in range(sl // 4)]
    strdata = data[do:do + dl]

    # texdata: 3 floats reflectivity, int nameId, 4 ints dims
    to, tl = lump[LUMP_TEXDATA]
    texdata = []
    for i in range(tl // 32):
        base = to + i * 32
        name_id = struct.unpack_from("<i", data, base + 12)[0]
        w, h = struct.unpack_from("<ii", data, base + 16)
        name = cstr(strdata, strtab[name_id]) if 0 <= name_id < len(strtab) else "?"
        texdata.append((name, w, h))

    # texinfo: 2 x (4 floats texture vecs) + 2 x (4 floats lightmap vecs) + flags + texdata
    io, il = lump[LUMP_TEXINFO]
    texinfo = []
    for i in range(il // 72):
        base = io + i * 72
        flags, td = struct.unpack_from("<ii", data, base + 64)
        texinfo.append((flags, td))

    po, pl = lump[LUMP_PLANES]
    fo, fl = lump[LUMP_FACES]
    face_size = 56
    face_count = fl // face_size

    for arg in sys.argv[2:]:
        face = int(arg)
        if not (0 <= face < face_count):
            print("face %d: out of range (0..%d)" % (face, face_count - 1))
            continue
        base = fo + face * face_size
        plane, side, on_node = struct.unpack_from("<hbb", data, base)
        firstedge = struct.unpack_from("<i", data, base + 4)[0]
        numedges = struct.unpack_from("<h", data, base + 8)[0]
        texinf, dispinfo = struct.unpack_from("<hh", data, base + 10)
        styles = data[base + 14:base + 18]
        flags, td = texinfo[texinf] if 0 <= texinf < len(texinfo) else (0, -1)
        name = texdata[td][0] if 0 <= td < len(texdata) else "?"
        size = texdata[td][1:] if 0 <= td < len(texdata) else ("?", "?")
        pbase = po + plane * 20
        normal = struct.unpack_from("<fff", data, pbase)
        dist = struct.unpack_from("<f", data, pbase + 12)[0]
        surf = []
        for bit, label in ((SURF_SKY, "sky"), (SURF_NODRAW, "nodraw"),
                           (SURF_TRIGGER, "trigger"), (SURF_NOLIGHT, "nolight")):
            if flags & bit:
                surf.append(label)
        print("face %d: tex=%s %dx%d texinfo=%d flags=0x%x[%s] dispinfo=%d "
              "edges=%d first=%d side=%d plane=(%.3f,%.3f,%.3f)@%.2f styles=%s"
              % (face, name, size[0], size[1], texinf, flags, ",".join(surf) or "-",
                 dispinfo, numedges, firstedge, side, normal[0], normal[1], normal[2],
                 dist, list(styles)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
