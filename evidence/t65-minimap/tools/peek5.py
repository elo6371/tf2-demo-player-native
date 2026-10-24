import json
for n in ("cp_snakewater_final1","pl_badwater"):
    d=json.load(open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.geometry.json"%n))
    p=d["projection"]
    print("==",n)
    print(" proj",{k:p[k] for k in ("minX","maxX","minY","maxY","scalePxPerUnit","unitsPerPixel","frameUnits","clippedCells","paddingPx") if k in p})
    print(" cellCount",d["cellCount"],"png",d.get("png"))
