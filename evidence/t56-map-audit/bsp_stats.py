#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Static BSP census for T56 (no product, no probe, no launch, no capture).

Parses a Source BSP v20 lump directory directly from the file bytes. Handles
per-lump LZMA compression (lump magic "LZMA": actualSize, lzmaSize, props[5]).

Reports per map: face count, displacement faces, materials used (texdata ->
TEXDATA_STRING_TABLE/DATA), lightmap lump bytes + luxels, entity counts,
models, brushes, cubemap lump records, and the sha256 of the BSP file.

Usage: python bsp_stats.py <bsp> [<bsp> ...]
"""
import hashlib, json, lzma, os, struct, sys

LUMP_ENTITIES = 0
LUMP_TEXDATA = 2
LUMP_TEXINFO = 6
LUMP_FACES = 7
LUMP_LIGHTING = 8
LUMP_MODELS = 14
LUMP_BRUSHES = 18
LUMP_DISPINFO = 26
LUMP_CUBEMAPS = 42
LUMP_TEXDATA_STRING_DATA = 43
LUMP_TEXDATA_STRING_TABLE = 44

def lzma_decompress_lump(data, fileofs):
    if data[fileofs:fileofs + 4] != b"LZMA":
        raise ValueError("not an LZMA lump at %d" % fileofs)
    actual, lzma_size = struct.unpack_from("<II", data, fileofs + 4)
    props = data[fileofs + 12:fileofs + 17]
    b = props[0]
    lc = b % 9
    b //= 9
    lp = b % 5
    pb = b // 5
    dict_size = struct.unpack_from("<I", props, 1)[0]
    raw = data[fileofs + 17:]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW,
                                filters=[{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp,
                                          "pb": pb, "dict_size": dict_size}])
    out = dec.decompress(raw, max_length=actual)
    if len(out) != actual:
        raise ValueError("lump decompressed to %d bytes, header says %d" % (len(out), actual))
    return out

def census(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"VBSP":
        raise ValueError("not a VBSP file: %r" % data[:4])
    version = struct.unpack_from("<i", data, 4)[0]
    lumps = []
    for i in range(64):
        fileofs, filelen, lversion, fourcc = struct.unpack_from("<iiii", data, 8 + i * 16)
        lumps.append((fileofs, filelen, lversion, fourcc))

    def lump(i):
        fileofs, filelen, _lv, _fc = lumps[i]
        if filelen <= 0:
            return b""
        if data[fileofs:fileofs + 4] == b"LZMA":
            return lzma_decompress_lump(data, fileofs)
        return data[fileofs:fileofs + filelen]

    sd = lump(LUMP_TEXDATA_STRING_DATA)
    st = lump(LUMP_TEXDATA_STRING_TABLE)
    names = []
    for i in range(len(st) // 4):
        o = struct.unpack_from("<i", st, i * 4)[0]
        end = sd.find(b"\x00", o)
        if end < 0:
            end = len(sd)
        names.append(sd[o:end].decode("latin-1"))

    texdata = lump(LUMP_TEXDATA)
    texinfo = lump(LUMP_TEXINFO)
    faces = lump(LUMP_FACES)
    disp = lump(LUMP_DISPINFO)
    light = lump(LUMP_LIGHTING)
    ents = lump(LUMP_ENTITIES).decode("latin-1", "replace")

    n_texdata = len(texdata) // 32
    n_texinfo = len(texinfo) // 72
    n_faces = len(faces) // 56
    n_disp = len(disp) // 176

    texinfo_texdata = [struct.unpack_from("<i", texinfo, i * 72 + 68)[0] for i in range(n_texinfo)]

    mat_faces = {}
    face_disp = 0
    light_faces = 0
    luxels = 0
    bad_ti = 0
    for i in range(n_faces):
        base = i * 56
        ti = struct.unpack_from("<h", faces, base + 10)[0]
        di = struct.unpack_from("<h", faces, base + 12)[0]
        lofs = struct.unpack_from("<i", faces, base + 24)[0]
        lm_w = struct.unpack_from("<i", faces, base + 36)[0]
        lm_h = struct.unpack_from("<i", faces, base + 40)[0]
        if di != -1:
            face_disp += 1
        if lofs >= 0:
            light_faces += 1
            if lm_w > 0 and lm_h > 0:
                luxels += lm_w * lm_h
        if ti < 0 or ti >= n_texinfo:
            bad_ti += 1
            continue
        td = texinfo_texdata[ti]
        name = names[td] if 0 <= td < len(names) else "<bad-texdata-%d>" % td
        mat_faces[name] = mat_faces.get(name, 0) + 1

    top = sorted(mat_faces.items(), key=lambda kv: -kv[1])
    total = sum(mat_faces.values()) or 1
    return {
        "path": path.replace("\\", "/"),
        "bytes": len(data),
        "bsp_version": version,
        "sha256": hashlib.sha256(data).hexdigest(),
        "lumps_lzma": sum(1 for f, l, _v, _c in lumps if l > 0 and data[f:f + 4] == b"LZMA"),
        "faces": n_faces,
        "faces_with_dispinfo": face_disp,
        "dispinfo_records": n_disp,
        "texdata": n_texdata,
        "texinfo": n_texinfo,
        "materials_used": len(mat_faces),
        "bad_texinfo_index": bad_ti,
        "lighting_lump_bytes": len(light),
        "faces_with_lightofs": light_faces,
        "lightmap_luxels": luxels,
        "models": len(lump(LUMP_MODELS)) // 64,
        "brushes": len(lump(LUMP_BRUSHES)) // 12,
        "entities": ents.count('"classname"'),
        "worldspawn": ents.count('"classname" "worldspawn"'),
        "light_environment": ents.count('"classname" "light_environment"'),
        "cubemap_lump_records": len(lump(LUMP_CUBEMAPS)) // 16,
        "top_material_share": round(top[0][1] / total, 4) if top else 0,
        "top_materials": top[:10],
    }

def main():
    for p in sys.argv[1:]:
        try:
            print(json.dumps(census(p), ensure_ascii=False))
        except Exception as e:
            print(json.dumps({"path": p, "error": "%s: %s" % (type(e).__name__, e)}, ensure_ascii=False))

if __name__ == "__main__":
    main()
