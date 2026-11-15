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
from pathlib import Path

# Reuse the independent reader's Source LZMA wrapper, not a product decoder.
sys.path.insert(0, str(Path(__file__).resolve().parent / "n14-oracle"))
from bsp_oracle import lump as decode_lump

LUMP_ENTITIES = 0
LUMP_PLANES = 1
LUMP_TEXDATA = 2
LUMP_TEXINFO = 6
LUMP_FACES = 7
LUMP_DISPINFO = 26
LUMP_TEXDATA_STRING_DATA = 43
LUMP_TEXDATA_STRING_TABLE = 44

SURF_SKY = 0x4
SURF_NODRAW = 0x80
SURF_TRIGGER = 0x40
SURF_NOLIGHT = 0x400


def lumps(data):
    # VBSP header: magic 'VBSP' at 0, version at 4, 64 lump directory entries from 8.
    if len(data) < 1036:
        raise ValueError("truncated BSP header")
    if data[:4] != b"VBSP":
        raise SystemExit("not a Source BSP: magic=%r" % data[:4])
    version = struct.unpack_from("<i", data, 4)[0]
    out = {}
    for i, stride in ((LUMP_PLANES, 20), (LUMP_TEXDATA, 32),
                      (LUMP_TEXINFO, 72), (LUMP_FACES, 56),
                      (LUMP_TEXDATA_STRING_DATA, 1),
                      (LUMP_TEXDATA_STRING_TABLE, 4)):
        off, length = struct.unpack_from("<ii", data, 8 + i * 16)
        if off < 0 or length < 0 or off > len(data) or length > len(data) - off:
            raise ValueError("lump %d lies outside BSP" % i)
        raw = data[off:off + length]
        expected = length
        if raw[:4] == b"LZMA":
            if len(raw) < 17:
                raise ValueError("lump %d has a truncated LZMA header" % i)
            expected, packed = struct.unpack_from("<II", raw, 4)
            dictionary = struct.unpack_from("<I", raw, 13)[0]
            if packed != length - 17 or not 0 < expected <= 128 * 1024 * 1024:
                raise ValueError("lump %d has invalid LZMA sizes" % i)
            if dictionary > 128 * 1024 * 1024:
                raise ValueError("lump %d has an excessive LZMA dictionary" % i)
        payload = decode_lump(data, i)
        if len(payload) != expected or len(payload) % stride:
            raise ValueError("lump %d has invalid decoded size" % i)
        out[i] = payload
    return version, out


def cstr(buf, at):
    end = buf.find(b"\x00", at)
    return buf[at:end if end >= 0 else len(buf)].decode("latin-1", "replace")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    path = sys.argv[1]
    data = Path(path).read_bytes()
    version, lump = lumps(data)
    print("map=%s version=%d size=%d" % (path, version, len(data)))

    # texture names: string table gives offsets into the string data block
    strings = lump[LUMP_TEXDATA_STRING_TABLE]
    strtab = [value[0] for value in struct.iter_unpack("<i", strings)]
    strdata = lump[LUMP_TEXDATA_STRING_DATA]

    # texdata: 3 floats reflectivity, int nameId, 4 ints dims
    textures = lump[LUMP_TEXDATA]
    texdata = []
    for base in range(0, len(textures), 32):
        name_id = struct.unpack_from("<i", textures, base + 12)[0]
        w, h = struct.unpack_from("<ii", textures, base + 16)
        name = cstr(strdata, strtab[name_id]) if 0 <= name_id < len(strtab) else "?"
        texdata.append((name, w, h))

    # texinfo: 2 x (4 floats texture vecs) + 2 x (4 floats lightmap vecs) + flags + texdata
    infos = lump[LUMP_TEXINFO]
    texinfo = []
    for base in range(0, len(infos), 72):
        flags, td = struct.unpack_from("<ii", infos, base + 64)
        texinfo.append((flags, td))

    planes = lump[LUMP_PLANES]
    faces = lump[LUMP_FACES]
    face_size = 56
    face_count = len(faces) // face_size
    print("decoded_faces=%d decoded_face_bytes=%d" % (face_count, len(faces)))

    if sys.argv[2] == "--materials":
        # N07: every material the map names, one per line, ready for vpk_lookup.
        for name, w, h in texdata:
            print("materials/%s.vmt" % name)
        return 0

    for arg in sys.argv[2:]:
        face = int(arg)
        if not (0 <= face < face_count):
            raise ValueError("face %d: out of range (0..%d)" % (face, face_count - 1))
        base = face * face_size
        plane, side, on_node = struct.unpack_from("<HBB", faces, base)
        firstedge = struct.unpack_from("<i", faces, base + 4)[0]
        numedges = struct.unpack_from("<h", faces, base + 8)[0]
        texinf, dispinfo = struct.unpack_from("<hh", faces, base + 10)
        styles = faces[base + 16:base + 20]
        flags, td = texinfo[texinf] if 0 <= texinf < len(texinfo) else (0, -1)
        name = texdata[td][0] if 0 <= td < len(texdata) else "?"
        size = texdata[td][1:] if 0 <= td < len(texdata) else ("?", "?")
        pbase = plane * 20
        if pbase + 20 > len(planes):
            raise ValueError("face %d references an invalid plane" % face)
        normal = struct.unpack_from("<fff", planes, pbase)
        dist = struct.unpack_from("<f", planes, pbase + 12)[0]
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
    import lzma
    try:
        sys.exit(main())
    except (OSError, ValueError, struct.error, lzma.LZMAError) as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        sys.exit(2)
