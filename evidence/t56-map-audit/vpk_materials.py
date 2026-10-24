#!/usr/bin/env python3
"""T56 static inventory: VPK index + per-map VMT/VTF reachability across all five
resource sources, with sha256 of every resolved input.

Sources searched (in this order, first hit wins; all hits recorded in *_also):
  pak   - BSP lump 40 (embedded ZIP: the map's own materials/models)
  vpk   - tf2_misc_dir.vpk / tf2_textures_dir.vpk
  dl    - tf/download/materials (downloaded custom content)
  custom- tf/custom/*/materials (mods, e.g. crosshairs)
  loose - tf/materials

Static only: no product/probe launch, no build, no capture, no host lock.
"""
import struct, json, os, sys, hashlib, collections, re, io, zipfile

TF = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
OUT = os.path.dirname(os.path.abspath(__file__))
DIRS = ["tf2_misc_dir.vpk", "tf2_textures_dir.vpk", "tf2_sound_misc_dir.vpk",
        "tf2_sound_vo_english_dir.vpk"]
LUMP_TEXDATA, LUMP_TEXINFO, LUMP_FACES, LUMP_PAKFILE = 2, 6, 7, 40
LUMP_TEXDATA_STRING_DATA, LUMP_TEXDATA_STRING_TABLE = 43, 44


def cstr(d, p):
    e = d.find(b"\x00", p)
    return d[p:e].decode("latin-1"), e + 1


class VPK:
    def __init__(self, path):
        self.path = path
        d = open(path, "rb").read()
        sig, ver, tree = struct.unpack_from("<III", d, 0)
        if sig != 0x55AA1234:
            raise ValueError("bad sig %08x" % sig)
        self.version = ver
        off = 12
        self.fileDataSectionSize = 0
        if ver == 2:
            (self.fileDataSectionSize, self.archiveMD5SectionSize,
             self.otherMD5SectionSize, self.signatureSectionSize) = struct.unpack_from("<IIII", d, 12)
            off = 28
        self.treeStart, self.treeSize = off, tree
        self.dataStart = off + tree
        self.entries = {}
        p = off
        ext, p = cstr(d, p)
        while ext != "":
            ps, p = cstr(d, p)
            while ps != "":
                fn, p = cstr(d, p)
                while fn != "":
                    crc, preload, arch, eoff, elen, term = struct.unpack_from("<IHHIIH", d, p)
                    p += 18
                    if term != 0xFFFF:
                        raise ValueError("bad terminator")
                    pre = d[p:p + preload]
                    p += preload
                    self.entries[((ps + "/") if ps else "") + fn + "." + ext] = (arch, eoff, elen, pre)
                    fn, p = cstr(d, p)
                ps, p = cstr(d, p)
            ext, p = cstr(d, p)

    def read(self, name):
        key = name.lower()
        if key not in self.entries:
            return None
        arch, eoff, elen, pre = self.entries[key]
        out = bytearray(pre)
        if elen:
            if arch == 0x7FFF:
                with open(self.path, "rb") as f:
                    f.seek(self.dataStart + eoff)
                    out += f.read(elen)
            else:
                base = os.path.basename(self.path)
                vol = os.path.join(os.path.dirname(self.path), base.replace("_dir.vpk", "_%03d.vpk" % arch))
                with open(vol, "rb") as f:
                    f.seek(eoff)
                    out += f.read(elen)
        return bytes(out)


def lzma_lump(data, fileofs):
    import lzma
    actual, lzsize = struct.unpack_from("<II", data, fileofs + 4)
    props = data[fileofs + 12:fileofs + 17]
    raw = data[fileofs + 17:fileofs + 17 + lzsize]
    p0 = props[0]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW,
                                filters=[{"id": lzma.FILTER_LZMA1, "lc": p0 % 9, "lp": (p0 // 9) % 5,
                                          "pb": p0 // 45,
                                          "dict_size": struct.unpack_from("<I", props, 1)[0]}])
    return dec.decompress(raw, max_length=actual)


def bsp_open(path):
    data = open(path, "rb").read()
    lumps = [struct.unpack_from("<iiii", data, 8 + i * 16) for i in range(64)]

    def lump(i):
        fo, fl, _lv, _fc = lumps[i]
        if fl <= 0:
            return b""
        if data[fo:fo + 4] == b"LZMA":
            return lzma_lump(data, fo)
        return data[fo:fo + fl]
    return data, lump


def bsp_materials(lump):
    sd = lump(LUMP_TEXDATA_STRING_DATA)
    st = lump(LUMP_TEXDATA_STRING_TABLE)
    names = []
    for i in range(len(st) // 4):
        o = struct.unpack_from("<i", st, i * 4)[0]
        e = sd.find(b"\x00", o)
        names.append(sd[o:e if e >= 0 else len(sd)].decode("latin-1"))
    texdata, texinfo, faces = lump(LUMP_TEXDATA), lump(LUMP_TEXINFO), lump(LUMP_FACES)
    n_ti = len(texinfo) // 72
    ti_td = [struct.unpack_from("<i", texinfo, i * 72 + 68)[0] for i in range(n_ti)]
    counts = collections.Counter()
    for i in range(len(faces) // 56):
        ti = struct.unpack_from("<h", faces, i * 56 + 10)[0]
        if 0 <= ti < n_ti:
            td = ti_td[ti]
            counts[names[td] if 0 <= td < len(names) else "<bad>"] += 1
    return counts, len(texdata) // 32, len(names)


BT = re.compile(r'"\$basetexture"\s+"([^"]*)"', re.I)
INC = re.compile(r'"\$?include"\s+"([^"]*)"', re.I)


def sha(b):
    return hashlib.sha256(b).hexdigest() if b is not None else ""


def norm_mat(name):
    n = name.strip().replace("\\", "/")
    if n.lower().endswith(".vmt"):
        n = n[:-4]
    return n


def main():
    vpks = {}
    inv = {"_generated_by": "evidence/t56-map-audit/vpk_materials.py"}
    for n in DIRS:
        p = os.path.join(TF, n)
        v = VPK(p)
        vpks[n] = v
        by_ext = collections.Counter(k.rsplit(".", 1)[-1] for k in v.entries)
        inv[n] = {
            "version": v.version, "entries": len(v.entries), "tree_size": v.treeSize,
            "data_section_bytes": v.fileDataSectionSize, "file_bytes": os.path.getsize(p),
            "top_ext": by_ext.most_common(10),
            "materials_vmt": sum(1 for k in v.entries if k.startswith("materials/") and k.endswith(".vmt")),
            "materials_vtf": sum(1 for k in v.entries if k.startswith("materials/") and k.endswith(".vtf")),
        }
        print("%s v%d entries=%d vmt=%d vtf=%d" % (n, v.version, len(v.entries),
              inv[n]["materials_vmt"], inv[n]["materials_vtf"]), flush=True)

    def count_tree(root):
        vmt = vtf = 0
        for r, _d, fs in os.walk(root):
            for f in fs:
                if f.lower().endswith(".vmt"):
                    vmt += 1
                elif f.lower().endswith(".vtf"):
                    vtf += 1
        return vmt, vtf

    inv["_loose_materials"] = dict(zip(("vmt", "vtf"), count_tree(os.path.join(TF, "materials"))))
    inv["_download_materials"] = dict(zip(("vmt", "vtf"), count_tree(os.path.join(TF, "download", "materials"))))
    inv["_custom_materials"] = dict(zip(("vmt", "vtf"), count_tree(os.path.join(TF, "custom"))))
    print("loose=%s download=%s custom=%s" % (inv["_loose_materials"], inv["_download_materials"],
                                              inv["_custom_materials"]), flush=True)

    default_maps = ["cp_snakewater_final1", "pl_odyssey", "koth_bagel_rc12"]
    names = sys.argv[1:] or default_maps
    maps = []
    for name in names:
        for rel in ("maps", "download/maps"):
            candidate = os.path.join(TF, rel, name + ".bsp")
            if os.path.exists(candidate):
                maps.append((name, candidate))
                break
        else:
            print("SKIP %s: no BSP in maps/ or download/maps/" % name, flush=True)

    rows, summary, pakinfo, samples = [], {}, {}, []
    for mname, mpath in maps:
        data, lump = bsp_open(mpath)
        counts, ntexdata, nnames = bsp_materials(lump)
        pak = {}
        pb = lump(LUMP_PAKFILE)
        if pb:
            z = zipfile.ZipFile(io.BytesIO(pb))
            pak = {n.lower(): n for n in z.namelist()}
            ext = collections.Counter(n.rsplit(".", 1)[-1].lower() for n in z.namelist())
            pakinfo[mname] = {"bsp": mpath.replace("\\", "/"), "pakfile_bytes": len(pb),
                              "entries": len(pak), "ext": ext.most_common(8),
                              "materials_vmt": sum(1 for k in pak if k.startswith("materials/") and k.endswith(".vmt")),
                              "materials_vtf": sum(1 for k in pak if k.startswith("materials/") and k.endswith(".vtf"))}
            print("%s pakfile: %d entries %s" % (mname, len(pak), pakinfo[mname]["ext"][:5]), flush=True)
        else:
            pakinfo[mname] = {"bsp": mpath.replace("\\", "/"), "pakfile_bytes": 0, "entries": 0}

        def find(relpath):
            """relpath like 'materials/x/y.vmt' -> list of (source, bytes)"""
            hits = []
            k = relpath.lower()
            if k in pak:
                hits.append(("pak", zipfile.ZipFile(io.BytesIO(pb)).read(pak[k])))
            for an in ("tf2_misc_dir.vpk", "tf2_textures_dir.vpk"):
                b = vpks[an].read(k)
                if b is not None:
                    hits.append(("vpk:" + an.replace("_dir.vpk", ""), b))
                    break
            lp = os.path.join(TF, relpath)
            if os.path.exists(lp):
                hits.append(("download", open(lp, "rb").read()))
            for r, _d, fs in os.walk(os.path.join(TF, "custom")):
                cand = os.path.join(r, os.path.basename(relpath))
                if os.path.exists(cand) and "custom" in cand.lower():
                    hits.append(("custom", open(cand, "rb").read()))
                    break
            lp2 = os.path.join(TF, "materials", relpath[len("materials/"):])
            if os.path.exists(lp2):
                hits.append(("loose", open(lp2, "rb").read()))
            return hits

        st = {"bsp": mpath.replace("\\", "/"), "bsp_sha256": sha(data), "texdata": ntexdata,
              "materials": len(counts), "faces": sum(counts.values()),
              "missing_vmt": [], "no_basetexture_direct": [], "missing_vtf": [],
              "vmt_source": collections.Counter(), "vtf_source": collections.Counter(),
              "only_in_pak": 0, "materials_with_include": 0}
        for mat, nf in counts.most_common():
            base_name = norm_mat(mat)
            hits = find("materials/%s.vmt" % base_name)
            if not hits:
                st["missing_vmt"].append(mat)
                rows.append([mname, mat, nf, "MISSING", "", "", "", "", "", "", "", "", "", ""])
                continue
            src, vmt_bytes = hits[0]
            st["vmt_source"][src] += 1
            if len(hits) == 1 and src == "pak":
                st["only_in_pak"] += 1
            txt = vmt_bytes.decode("latin-1", "replace")
            m = BT.search(txt)
            base = m.group(1).strip().replace("\\", "/").lstrip("/") if m else ""
            inc = INC.search(txt)
            base_inc = ""
            if inc:
                st["materials_with_include"] += 1
                ipath = inc.group(1).strip().replace("\\", "/")
                if ipath.lower().startswith("materials/"):
                    ipath = ipath[len("materials/"):]
                if ipath.lower().endswith(".vmt"):
                    ipath = ipath[:-4]
                ih = find("materials/%s.vmt" % ipath)
                if ih:
                    mi = BT.search(ih[0][1].decode("latin-1", "replace"))
                    if mi:
                        base_inc = mi.group(1).strip().replace("\\", "/").lstrip("/")
                        st["base_via_include"] = st.get("base_via_include", 0) + 1
            if not base:
                st["no_basetexture_direct"].append(mat)
                if len(st["no_basetexture_direct"]) <= 5:
                    samples.append((mname, mat, src, txt[:600]))
            vsrc, vsha, vbytes, valso = "", "", 0, ""
            if base:
                vh = find("materials/%s.vtf" % base.lstrip("/"))
                if vh:
                    vsrc, vb = vh[0]
                    st["vtf_source"][vsrc] += 1
                    vsha, vbytes = sha(vb), len(vb)
                    valso = ",".join(s for s, _ in vh[1:])
                else:
                    st["missing_vtf"].append(mat + " -> " + base)
            rows.append([mname, mat, nf, src, ",".join(s for s, _ in hits[1:]),
                         sha(vmt_bytes), len(vmt_bytes), base, inc.group(1) if inc else "", base_inc,
                         vsrc, vsha, vbytes, valso])
        summary[mname] = st
        print("%s: materials=%d missing_vmt=%d no_base=%d missing_vtf=%d only_pak=%d vmt_src=%s"
              % (mname, st["materials"], len(st["missing_vmt"]), len(st["no_basetexture_direct"]),
                 len(st["missing_vtf"]), st["only_in_pak"], dict(st["vmt_source"])), flush=True)

    json.dump(inv, open(os.path.join(OUT, "vpk_inventory.json"), "w", encoding="utf-8"), indent=2)
    json.dump(pakinfo, open(os.path.join(OUT, "bsp_pakfile.json"), "w", encoding="utf-8"), indent=2)
    json.dump(summary, open(os.path.join(OUT, "map_material_summary.json"), "w", encoding="utf-8"), indent=2)
    with open(os.path.join(OUT, "map_material_reachability.tsv"), "w", encoding="utf-8", newline="") as f:
        f.write("map\tmaterial\tfaces\tvmt_source\tvmt_also\tvmt_sha256\tvmt_bytes\tbasetexture\tinclude\tbase_via_include\tvtf_source\tvtf_sha256\tvtf_bytes\tvtf_also\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")
    with open(os.path.join(OUT, "vmt_no_basetexture_samples.txt"), "w", encoding="utf-8") as f:
        for mname, mat, src, txt in samples:
            f.write("=== %s | %s | %s\n%s\n\n" % (mname, mat, src, txt))
    print("wrote vpk_inventory.json bsp_pakfile.json map_material_summary.json map_material_reachability.tsv (%d rows)" % len(rows))


if __name__ == "__main__":
    main()
