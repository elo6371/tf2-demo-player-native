import json
base = "D:/TF2_Native_Test/evidence/t65-minimap/data/"
d = json.load(open(base+"projection_ledger.json"))
for k,v in d.items():
    print("====",k)
    for key in ["errorBudget","extra","teamSpawnCentroids","identity","geometry"]:
        if key in v:
            print(" ",key,"=",json.dumps(v[key])[:600])
    print()
