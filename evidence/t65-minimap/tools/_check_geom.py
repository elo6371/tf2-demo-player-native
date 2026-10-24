import json
base = "D:/TF2_Native_Test/evidence/t65-minimap/data/"
for name in ["cp_snakewater_final1.bsp.geometry.json","pl_badwater.bsp.geometry.json"]:
    g = json.load(open(base+name))
    print("====", name, "keys:", list(g.keys()))
    for k in ["map","faceCount","floorFaces","vertexCount","skipped","entityCount","playableBounds","zRange","zBands","cellCount","multiLevelCells","levelHistogram","components","droppedComponents","zFilter","worldModel"]:
        if k in g:
            v = g[k]
            s = json.dumps(v)
            print(" ", k, "=", s[:300])
    print()
