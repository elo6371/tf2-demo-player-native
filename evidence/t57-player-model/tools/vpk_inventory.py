#!/usr/bin/env python3
"""T57 static inventory: read real MDL/VVD/VTX headers for the nine TF2 player models
straight out of the retail VPKs. No product code, no build, no launch."""
import struct, os, json, sys

TF = r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
OUT = r"D:/TF2_Native_Test/evidence/t57-player-model"

class Vpk:
    def __init__(self, dirpath):
        self.dirpath = dirpath
        data = open(dirpath, "rb").read()
        magic, version = struct.unpack_from("<II", data, 0)
        if magic != 0x55AA1234:
            raise SystemExit("bad vpk magic %08x in %s" % (magic, dirpath))
        self.version = version
        tree_size = struct.unpack_from("<I", data, 8)[0]
        header = 28 if version == 2 else 12
        tree = data[header:header + tree_size]
        self.entries = {}
        self._parse(tree)
        self.base = os.path.dirname(dirpath)
        self.prefix = os.path.basename(dirpath).replace("_dir.vpk", "")

    def _parse(self, t):
        pos = 0
        n = len(t)
        def readstr():
            nonlocal pos
            end = t.index(b"\x00", pos)
            s = t[pos:end].decode("utf-8", "replace")
            pos = end + 1
            return s
        while pos < n:
            ext = readstr()
            if ext == "":
                break
            while True:
                path = readstr()
                if path == "":
                    break
                while True:
                    name = readstr()
                    if name == "":
                        break
                    crc, preload, arch, off, ln, term = struct.unpack_from("<IHHIIH", t, pos)
                    pos += 18
                    full = ((path + "/" + name) if path else name)
                    if ext:
                        full += "." + ext
                    key = full.lower()
                    self.entries[key] = (arch, off, ln, preload, crc, pos)
                    if preload:
                        pos += preload

    def read(self, path):
        key = path.lower()
        if key not in self.entries:
            return None
        arch, off, ln, preload, crc, preload_pos = self.entries[key]
        tree = None
        if arch == 0x7FFF:
            # single-file: data lives in the dir file right after the entry
            data = open(self.dirpath, "rb").read()
            header = 28 if self.version == 2 else 12
            tree_size = struct.unpack_from("<I", data, 8)[0]
            tree = data[header:header + tree_size]
            return tree[preload_pos:preload_pos + ln]
        chunk = os.path.join(self.base, "%s_%03d.vpk" % (self.prefix, arch))
        with open(chunk, "rb") as f:
            f.seek(off)
            return f.read(ln)

    def has(self, path):
        return path.lower() in self.entries

def cstr(b, off):
    if off < 0 or off >= len(b):
        return ""
    end = b.find(b"\x00", off)
    if end < 0:
        end = len(b)
    return b[off:end].decode("utf-8", "replace")

def parse_mdl(b):
    d = {}
    d["id"] = struct.unpack_from("<i", b, 0)[0]
    d["version"] = struct.unpack_from("<i", b, 4)[0]
    d["checksum"] = struct.unpack_from("<i", b, 8)[0]
    d["name"] = cstr(b, 12)
    d["length"] = struct.unpack_from("<i", b, 76)[0]
    d["flags"] = struct.unpack_from("<i", b, 152)[0]
    d["boneCount"] = struct.unpack_from("<i", b, 156)[0]
    d["boneIndex"] = struct.unpack_from("<i", b, 160)[0]
    d["sequenceCount"] = struct.unpack_from("<i", b, 188)[0]
    d["sequenceIndex"] = struct.unpack_from("<i", b, 192)[0]
    d["textureCount"] = struct.unpack_from("<i", b, 204)[0]
    d["textureIndex"] = struct.unpack_from("<i", b, 208)[0]
    d["cdTextureCount"] = struct.unpack_from("<i", b, 212)[0]
    d["cdTextureIndex"] = struct.unpack_from("<i", b, 216)[0]
    d["bodyPartCount"] = struct.unpack_from("<i", b, 232)[0]
    d["attachmentCount"] = struct.unpack_from("<i", b, 240)[0]
    d["bytesOnDisk"] = len(b)
    bones = []
    for i in range(d["boneCount"]):
        base = d["boneIndex"] + 216 * i
        if base + 216 > len(b):
            break
        bones.append(cstr(b, base + struct.unpack_from("<i", b, base)[0]))
    d["bones"] = bones
    seqs = []
    for i in range(d["sequenceCount"]):
        base = d["sequenceIndex"] + 212 * i
        if base + 212 > len(b):
            break
        label = cstr(b, base + struct.unpack_from("<i", b, base + 4)[0])
        activity = struct.unpack_from("<i", b, base + 16)[0]
        numblends = struct.unpack_from("<i", b, base + 56)[0]
        animidx = struct.unpack_from("<i", b, base + 60)[0]
        fps = 0.0
        frames = 0
        if numblends > 0 and animidx > 0 and animidx + 20 <= len(b):
            fps = struct.unpack_from("<f", b, animidx + 8)[0]
            frames = struct.unpack_from("<i", b, animidx + 16)[0]
        seqs.append({"label": label, "activity": activity, "numBlends": numblends, "fps": round(fps, 3), "frames": frames})
    d["sequences"] = seqs
    cds = []
    for i in range(d["cdTextureCount"]):
        off = struct.unpack_from("<i", b, d["cdTextureIndex"] + 4 * i)[0]
        cds.append(cstr(b, off))
    d["cdTextures"] = cds
    texs = []
    for i in range(d["textureCount"]):
        base = d["textureIndex"] + 64 * i
        if base + 64 > len(b):
            break
        texs.append(cstr(b, base + struct.unpack_from("<i", b, base)[0]))
    d["textureNames"] = texs
    return d

def parse_vvd(b):
    return {
        "id": struct.unpack_from("<i", b, 0)[0],
        "version": struct.unpack_from("<i", b, 4)[0],
        "checksum": struct.unpack_from("<i", b, 8)[0],
        "numLODs": struct.unpack_from("<i", b, 12)[0],
        "lod0Vertices": struct.unpack_from("<i", b, 16)[0],
        "numFixups": struct.unpack_from("<i", b, 48)[0],
        "bytesOnDisk": len(b),
    }

def parse_vtx(b):
    version = struct.unpack_from("<i", b, 0)[0]
    checksum = struct.unpack_from("<i", b, 16)[0]
    numLODs = struct.unpack_from("<i", b, 20)[0]
    lods = []
    for i in range(min(numLODs, 8)):
        base = 28 + 8 * i
        if base + 8 > len(b):
            break
        numLOD, off = struct.unpack_from("<ii", b, base)
        numMeshes = struct.unpack_from("<i", b, off)[0] if off + 4 <= len(b) else -1
        lods.append({"lod": numLOD, "numMeshes": numMeshes})
    return {"version": version, "checksum": checksum, "numLODs": numLODs, "lods": lods, "bytesOnDisk": len(b)}

CLASSES = [
    (1, "scout", "Scout"), (2, "sniper", "Sniper"), (3, "soldier", "Soldier"),
    (4, "demo", "Demoman"), (5, "medic", "Medic"), (6, "heavy", "Heavy"),
    (7, "pyro", "Pyro"), (8, "spy", "Spy"), (9, "engineer", "Engineer"),
]

def main():
    misc = Vpk(os.path.join(TF, "tf2_misc_dir.vpk"))
    tex = Vpk(os.path.join(TF, "tf2_textures_dir.vpk"))
    result = {"vpk": {"miscEntries": len(misc.entries), "textureEntries": len(tex.entries),
                      "miscVersion": misc.version, "textureVersion": tex.version},
              "playerMdlEntries": sorted([k for k in misc.entries if k.startswith("models/player/") and k.endswith(".mdl")]),
              "classes": []}
    for tf_class, stem, disp in CLASSES:
        mdl_path = "models/player/%s.mdl" % stem
        row = {"tfClass": tf_class, "className": disp, "stem": stem, "mdlPath": mdl_path,
               "companions": {}, "mdl": None, "vvd": None, "vtx": {}, "materials": []}
        for ext in (".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".phy", ".vtx"):
            p = "models/player/%s%s" % (stem, ext)
            row["companions"][ext] = misc.has(p)
        raw = misc.read(mdl_path)
        if raw is None:
            row["error"] = "MDL NOT FOUND IN tf2_misc VPK"
            result["classes"].append(row)
            continue
        m = parse_mdl(raw)
        row["mdl"] = {k: m[k] for k in ("id", "version", "checksum", "name", "length", "flags",
                                        "boneCount", "sequenceCount", "textureCount", "cdTextureCount",
                                        "bodyPartCount", "attachmentCount", "bytesOnDisk")}
        row["mdl"]["magic"] = struct.pack("<i", m["id"]).decode("latin-1")
        row["bones"] = m["bones"]
        row["sequences"] = m["sequences"]
        row["cdTextures"] = m["cdTextures"]
        row["textureNames"] = m["textureNames"]
        vvd_raw = misc.read("models/player/%s.vvd" % stem)
        if vvd_raw:
            row["vvd"] = parse_vvd(vvd_raw)
            row["vvd"]["magic"] = struct.pack("<i", row["vvd"]["id"]).decode("latin-1")
        for ext in (".dx90.vtx", ".dx80.vtx", ".sw.vtx"):
            rawv = misc.read("models/player/%s%s" % (stem, ext))
            if rawv:
                row["vtx"][ext] = parse_vtx(rawv)
        for cd in m["cdTextures"]:
            for tn in m["textureNames"]:
                vmt = ("materials/" + cd + tn + ".vmt").replace("//", "/")
                vtf = ("materials/" + cd + tn + ".vtf").replace("//", "/")
                row["materials"].append({"texture": tn, "vmt": vmt, "vmtExists": misc.has(vmt),
                                         "vtf": vtf, "vtfExists": tex.has(vtf)})
        result["classes"].append(row)
    os.makedirs(os.path.join(OUT, "raw"), exist_ok=True)
    with open(os.path.join(OUT, "raw", "player_models.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1)
    # compact table
    print("VPK: misc entries=%d (v%d) texture entries=%d (v%d)" % (
        result["vpk"]["miscEntries"], result["vpk"]["miscVersion"],
        result["vpk"]["textureEntries"], result["vpk"]["textureVersion"]))
    print("models/player/*.mdl in tf2_misc: %d" % len(result["playerMdlEntries"]))
    for k in result["playerMdlEntries"]:
        print("   ", k)
    print()
    hdr = "%-9s %-3s %-6s %-6s %-5s %-5s %-6s %-6s %-6s %-6s %-5s %s" % (
        "class", "id", "mdlVer", "cksum", "bones", "seqs", "tex", "cdtex", "vvdLOD", "vtxLOD", "meshes", "companions")
    print(hdr)
    for row in result["classes"]:
        if not row["mdl"]:
            print("%-9s ERROR %s" % (row["className"], row.get("error")))
            continue
        vtx = row["vtx"].get(".dx90.vtx") or {}
        mesh0 = (vtx.get("lods") or [{}])[0].get("numMeshes", "-")
        comp = "".join("%s=%d " % (k.strip("."), 1 if v else 0) for k, v in row["companions"].items())
        print("%-9s %-3d %-6d %-6d %-5d %-5d %-6d %-6d %-6s %-6s %-5s %s" % (
            row["className"], row["tfClass"], row["mdl"]["version"], row["mdl"]["checksum"] & 0xFFFFFFFF,
            row["mdl"]["boneCount"], row["mdl"]["sequenceCount"], row["mdl"]["textureCount"],
            row["mdl"]["cdTextureCount"], (row["vvd"] or {}).get("numLODs", "-"),
            vtx.get("numLODs", "-"), mesh0, comp))
    print()
    for row in result["classes"]:
        if not row["mdl"]:
            continue
        print("%s: cdTextures=%s" % (row["className"], row["cdTextures"]))
        for mat in row["materials"]:
            print("   tex=%-28s vmt=%-52s exists=%d vtf=%d" % (
                mat["texture"], mat["vmt"], 1 if mat["vmtExists"] else 0, 1 if mat["vtfExists"] else 0))

if __name__ == "__main__":
    main()
