import json
for n in ("cp_snakewater_final1","pl_badwater"):
    d=json.load(open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.geometry.json"%n))
    print("==",n,"cells",d["cellCount"],"multi",d["multiLevelCells"])
    print(" zFilter",json.dumps(d.get("zFilter"))[:400])
    print(" bounds",d["playableBounds"],"proj",d["projection"]["minX"],d["projection"]["maxY"],d["projection"]["scalePxPerUnit"])
    print(" comps",json.dumps(d.get("components"))[:400])
