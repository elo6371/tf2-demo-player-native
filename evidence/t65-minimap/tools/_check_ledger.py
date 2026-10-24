import json, hashlib
base = "D:/TF2_Native_Test/evidence/t65-minimap/"
d = json.load(open(base + "data/projection_ledger.json"))
for k, v in d.items():
    pr = v["projection"]
    print(k, "projection:", {kk: pr.get(kk) for kk in ("minX","maxX","minY","maxY","scalePxPerUnit","unitsPerPixel")})
    print(k, "frame:", pr.get("frameUnits"), "outsideFrame:", pr.get("outsideFrameCells"), "outsideCanvas:", pr.get("outsideCanvasCells"))
    cp = v.get("controlPoints")
    print(k, "cps type:", type(cp).__name__, "sample:", json.dumps(cp)[:400] if cp else None)
    print()
for f in ["data/cp_snakewater_final1.bsp.geometry.json","data/pl_badwater.bsp.geometry.json",
          "data/cp_snakewater_final1.bsp.floorplan.png","data/pl_badwater.bsp.floorplan.png",
          "data/cp_snakewater_final1.preview.png","data/pl_badwater.preview.png",
          "data/projection_ledger.json"]:
    h = hashlib.sha256(open(base+f,"rb").read()).hexdigest()
    print(h, f)
