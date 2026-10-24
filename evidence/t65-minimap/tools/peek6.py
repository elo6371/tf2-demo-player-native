import json
for n in ("cp_snakewater_final1","pl_badwater"):
    d=json.load(open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.geometry.json"%n))
    print("==",n)
    for k in ("file","bytes","sha256","bspVersion","worldModel","faceCount","floorFaces","vertexCount","skipped","entityCount","playableBounds","zRange","zBands","cellCount","multiLevelCells","levelHistogram"):
        if k in d: print(" ",k,"=",json.dumps(d[k])[:300])
    print(" projection keys:",sorted(d["projection"].keys()))
    print(" outsideFrame",d["projection"].get("outsideFrameCells"),"outsideCanvas",d["projection"].get("outsideCanvasCells"))
