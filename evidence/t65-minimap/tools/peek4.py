import json
d=json.load(open("D:/TF2_Native_Test/evidence/t65-minimap/data/pl_badwater.bsp.geometry.json"))
print(json.dumps(d["zFilter"],indent=1)[:800])
print(json.dumps(d["components"],indent=1)[:800])
