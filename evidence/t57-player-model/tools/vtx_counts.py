#!/usr/bin/env python3
"""Replicate native/src/model_loader.cpp:244-311 (parseVtxHierarchyWithStrides) in Python
to count the render indices buildBindPoseMeshLod0 would see, per LOD, for the nine player models."""
import struct, os, json, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vpk_inventory as V

def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def u16(b, o): return struct.unpack_from("<H", b, o)[0]

def fits(off, count, stride, size):
    return off >= 0 and count >= 0 and off + count * stride <= size

def walk(b, vvd_vertices):
    bodyPartCount = u32(b, 28); bodyPartOffset = u32(b, 32)
    descs = []
    out_of_bounds = 0
    bad = 0
    if not fits(bodyPartOffset, bodyPartCount, 8, len(b)):
        return {"error": "bodypart table out of bounds"}
    for bp in range(bodyPartCount):
        bpOff = bodyPartOffset + bp * 8
        modelCount = u32(b, bpOff); modelRel = u32(b, bpOff + 4)
        if modelCount > 4096 or not fits(bpOff + modelRel, modelCount, 8, len(b)):
            continue
        for m in range(modelCount):
            modelOff = bpOff + modelRel + m * 8
            lodCount = u32(b, modelOff); lodRel = u32(b, modelOff + 4)
            if lodCount > 32 or not fits(modelOff + lodRel, lodCount, 12, len(b)):
                continue
            for lod in range(lodCount):
                lodOff = modelOff + lodRel + lod * 12
                meshCount = u32(b, lodOff); meshRel = u32(b, lodOff + 4)
                if meshCount > 4096 or not fits(lodOff + meshRel, meshCount, 9, len(b)):
                    continue
                for mesh in range(meshCount):
                    meshOff = lodOff + meshRel + mesh * 9
                    groupCount = u32(b, meshOff); groupRel = u32(b, meshOff + 4)
                    if groupCount > 4096 or not fits(meshOff + groupRel, groupCount, 25, len(b)):
                        continue
                    for g in range(groupCount):
                        groupOff = meshOff + groupRel + g * 25
                        vertexCount = u32(b, groupOff); vertexRel = u32(b, groupOff + 4)
                        indexCount = u32(b, groupOff + 8); indexRel = u32(b, groupOff + 12)
                        if (vertexCount == 0 or indexCount < 3 or vertexCount > 4000000 or indexCount > 12000000
                                or not fits(groupOff + vertexRel, vertexCount, 9, len(b))
                                or not fits(groupOff + indexRel, indexCount, 2, len(b))):
                            out_of_bounds += 1
                            continue
                        ok = True
                        for i in range(indexCount):
                            local = u16(b, groupOff + indexRel + i * 2)
                            if local >= vertexCount:
                                out_of_bounds += 1; ok = False; break
                            orig = u16(b, groupOff + vertexRel + local * 9 + 4)
                            if orig >= vvd_vertices:
                                out_of_bounds += 1; ok = False; break
                        if not ok:
                            bad += 1
                            continue
                        descs.append({"bp": bp, "model": m, "lod": lod, "mesh": mesh, "group": g,
                                      "indexCount": indexCount, "vertexCount": vertexCount})
    return {"descriptors": descs, "outOfBounds": out_of_bounds, "rejectedGroups": bad}

def main():
    misc = V.Vpk(os.path.join(V.TF, "tf2_misc_dir.vpk"))
    rows = []
    for tf_class, stem, disp in V.CLASSES:
        mdl = misc.read("models/player/%s.mdl" % stem)
        vvd_raw = misc.read("models/player/%s.vvd" % stem)
        vtx_raw = misc.read("models/player/%s.dx90.vtx" % stem)
        if not (mdl and vvd_raw and vtx_raw):
            rows.append({"class": disp, "error": "missing resource"}); continue
        vvd = V.parse_vvd(vvd_raw)
        walkres = walk(vtx_raw, vvd["lod0Vertices"])
        descs = walkres.get("descriptors", [])
        by_lod = {}
        for d in descs:
            by_lod[d["lod"]] = by_lod.get(d["lod"], 0) + d["indexCount"]
        lod0 = by_lod.get(0, 0)
        rows.append({"class": disp, "tfClass": tf_class, "stem": stem,
                     "vvdLod0Vertices": vvd["lod0Vertices"], "vvdNumLods": vvd["numLODs"],
                     "vtxLod0RenderIndices": lod0,
                     "vtxTotalRenderIndices": sum(by_lod.values()),
                     "descriptorsPerLod": by_lod,
                     "descriptorCount": len(descs),
                     "truncatedBy12000Cap": lod0 > 12000,
                     "lostIndicesToCap": max(0, lod0 - 12000),
                     "outOfBounds": walkres.get("outOfBounds"), "rejectedGroups": walkres.get("rejectedGroups")})
    print("%-9s %-8s %-6s %-8s %-8s %-6s %-7s %s" % ("class", "vvdLOD0", "lods", "lod0Idx", "allIdx", "descs", "capCut", "perLOD"))
    for r in rows:
        if "error" in r:
            print("%-9s ERROR %s" % (r["class"], r["error"])); continue
        print("%-9s %-8d %-6d %-8d %-8d %-6d %-7d %s" % (
            r["class"], r["vvdLod0Vertices"], r["vvdNumLods"], r["vtxLod0RenderIndices"],
            r["vtxTotalRenderIndices"], r["descriptorCount"], r["lostIndicesToCap"], r["descriptorsPerLod"]))
    with open(os.path.join(V.OUT, "raw", "vtx_index_counts.json"), "w", encoding="utf-8") as f:
        json.dump(rows, f, indent=1)

if __name__ == "__main__":
    main()
