#!/usr/bin/env python3
"""T57 static inventory: nine TF2 class player models + their separate animation
models, read from the real VPK file headers (no game launch, no build).

Inputs : tf2_misc_dir.vpk (via tools/vpk_inventory.py Vpk/parse_mdl)
Outputs: raw/nine_class_table.tsv, raw/anim_models.json
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vpk_inventory as V

TF = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
MISC = os.path.join(TF, "tf2_misc_dir.vpk")
RAW = os.path.join(HERE, "..", "raw")

classes = json.load(open(os.path.join(RAW, "player_models.json")))["classes"]
vtx = {r["tfClass"]: r for r in json.load(open(os.path.join(RAW, "vtx_index_counts.json")))}
misc = V.Vpk(MISC)

rows = []
for c in classes:
    m = c["mdl"]; v = c["vvd"] or {}
    t = vtx.get(c["tfClass"], {})
    lods = t.get("descriptorsPerLod", {})
    rows.append({
        "tfClass": c["tfClass"], "className": c["className"], "mdlPath": c["mdlPath"],
        "checksum": m["checksum"] & 0xFFFFFFFF, "bones": m["boneCount"], "sequences": m["sequenceCount"],
        "textures": m["textureCount"], "cdTextures": m["cdTextureCount"],
        "bodyParts": m["bodyPartCount"], "attachments": m["attachmentCount"], "mdlBytes": m["bytesOnDisk"],
        "mdlVersion": m["version"], "mdlMagic": m["magic"],
        "vvdNumLods": v.get("numLODs"), "vvdLod0Vertices": v.get("lod0Vertices"), "vvdBytes": v.get("bytesOnDisk"),
        "vtxVersion": (c["vtx"].get(".dx90.vtx") or {}).get("version"),
        "vtxNumLods": (c["vtx"].get(".dx90.vtx") or {}).get("numLODs"),
        "lods": lods, "lod0Indices": t.get("vtxLod0RenderIndices"),
        "totalIndices": t.get("vtxTotalRenderIndices"),
        "truncated12000": t.get("truncatedBy12000Cap"), "lostToCap": t.get("lostIndicesToCap"),
        "companions": c["companions"],
        "slot0Texture": (c["materials"][0]["texture"] if c.get("materials") else None),
        "slot0Vmt": (c["materials"][0]["vmt"] if c.get("materials") else None),
        "slot0VmtExists": (c["materials"][0]["vmtExists"] if c.get("materials") else None),
        "materialsWithVmt": sum(1 for x in c.get("materials", []) if x["vmtExists"]),
        "materialsWithVtf": sum(1 for x in c.get("materials", []) if x["vtfExists"]),
    })

hdr = ["tfClass","className","mdlPath","checksum","bones","sequences","textures","cdTextures","bodyParts",
       "attachments","mdlBytes","mdlVersion","vvdNumLods","vvdLod0Vertices","vtxVersion","vtxNumLods",
       "lod0Indices","totalIndices","truncated12000","lostToCap","slot0Texture","materialsWithVmt","materialsWithVtf"]
with open(os.path.join(RAW, "nine_class_table.tsv"), "w", encoding="utf-8") as f:
    f.write("\t".join(hdr) + "\n")
    for r in rows:
        f.write("\t".join(str(r[k]) for k in hdr) + "\n")

print("class    bones seqs tex cd body attach lod0idx  cap  lost  slot0Texture")
for r in rows:
    print("%-8s %4d %4d %3d %2d %4d %5d %7d %4s %5d  %s" % (
        r["className"], r["bones"], r["sequences"], r["textures"], r["cdTextures"], r["bodyParts"],
        r["attachments"], r["lod0Indices"], "YES" if r["truncated12000"] else "no", r["lostToCap"],
        r["slot0Texture"]))

# --- separate animation models (the pose source for players) -----------------
anim = []
for c in classes:
    stem = c["stem"]
    for suffix in ("_animations.mdl", "_user_animations.mdl"):
        p = "models/player/%s%s" % (stem, suffix)
        b = misc.read(p)
        rec = {"path": p, "exists": b is not None}
        if b:
            m = V.parse_mdl(b)
            rec.update({k: m.get(k) for k in ("version","checksum","name","length","flags","boneCount",
                                              "sequenceCount","textureCount","bodyPartCount","attachmentCount")})
            rec["checksum"] &= 0xFFFFFFFF
            rec["bytes"] = len(b)
        anim.append(rec)

json.dump({"classes": rows, "animationModels": anim}, open(os.path.join(RAW, "anim_models.json"), "w", encoding="utf-8"), indent=1)
print()
print("animation model                                    bones seqs tex  bytes")
for a in anim:
    if a["exists"]:
        print("%-48s %5d %4d %3d %7d" % (a["path"], a["boneCount"], a["sequenceCount"], a["textureCount"], a["bytes"]))
    else:
        print("%-48s MISSING" % a["path"])
