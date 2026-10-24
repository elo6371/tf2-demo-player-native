import json, collections
p="D:/TF2_Native_Test/evidence/t65-minimap/data/pl_badwater.bsp.geometry.json"
d=json.load(open(p))
print("components:", json.dumps(d["components"], indent=1)[:1500])
print("cells", d["cellCount"], "multi", d["multiLevelCells"])
