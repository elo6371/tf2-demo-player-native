import json, os
d = os.path.dirname(os.path.abspath("evidence/t56-map-audit/x"))
s = json.load(open("evidence/t56-map-audit/map_material_summary.json", encoding="utf-8"))
for m, v in s.items():
    print("==", m, "materials=%d" % v["materials"])
    print("  missing_vmt:", v["missing_vmt"])
    print("  missing_vtf:", v["missing_vtf"])
    print("  no_basetexture_direct=%d with_include=%d only_in_pak=%d" % (len(v["no_basetexture_direct"]), v["materials_with_include"], v["only_in_pak"]))
    print("  vmt_source:", dict(v["vmt_source"]), " vtf_source:", dict(v["vtf_source"]))
    print("  no_base sample:", v["no_basetexture_direct"][:6])
