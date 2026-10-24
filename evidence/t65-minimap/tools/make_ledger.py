import json, math, gzip, csv
CELL=32.0
MAPS=["cp_snakewater_final1","pl_badwater"]
out={}
for name in MAPS:
    d=json.load(open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.geometry.json"%name))
    p=d["projection"]; s=p["scalePxPerUnit"]; minX=p["minX"]; maxY=p["maxY"]
    ents=d["entities"]
    def px(x,y): return (x-minX)*s, (maxY-y)*s
    zlo,zhi=d["zFilter"]["zlo"],d["zFilter"]["zhi"]
    def inband(o): return zlo-512 <= o[2] <= zhi+512
    cps=[]
    for cp in ents.get("team_control_point",[]):
        o=cp["origin"]; X,Y=px(o[0],o[1])
        cps.append({"name":cp.get("targetname"),"world":[o[0],o[1],o[2]],"px":[round(X,3),round(Y,3)]})
    teams={}
    for sp in ents.get("info_player_teamspawn",[]):
        o=sp["origin"]
        if not inband(o): continue
        teams.setdefault(sp.get("team","?"),[]).append(o)
    tc={}
    for t,pts in teams.items():
        n=len(pts); cx=sum(q[0] for q in pts)/n; cy=sum(q[1] for q in pts)/n; cz=sum(q[2] for q in pts)/n
        X,Y=px(cx,cy)
        tc[t]={"count":n,"centroid":[round(cx,3),round(cy,3),round(cz,3)],"px":[round(X,3),round(Y,3)],
               "bbox":[min(q[0] for q in pts),max(q[0] for q in pts),min(q[1] for q in pts),max(q[1] for q in pts)]}
    rt=[]
    for cp in ents.get("team_control_point",[]):
        o=cp["origin"]; X,Y=px(o[0],o[1]); x2=minX+X/s; y2=maxY-Y/s
        rt.append(abs(x2-o[0])+abs(y2-o[1]))
    extra={}
    if ents.get("team_train_watcher"):
        o=ents["team_train_watcher"][0]["origin"]; X,Y=px(o[0],o[1])
        extra["train_watcher"]={"world":o,"px":[round(X,3),round(Y,3)]}
    if ents.get("path_track"):
        pts=[e["origin"] for e in ents["path_track"]]
        extra["path_track"]={"count":len(pts),"bbox":[min(q[0] for q in pts),max(q[0] for q in pts),min(q[1] for q in pts),max(q[1] for q in pts)]}
    out[name]={
      "identity":{"file":"tf/maps/%s.bsp"%name,"bytes":d["bytes"],"sha256":d["sha256"]},
      "projection":{k:p[k] for k in ("width","height","minX","maxX","minY","maxY","scalePxPerUnit","unitsPerPixel","frameUnits","frameMarginUnits","frameSource","formulaPx","formulaPy","yFlip","outsideFrameCells","outsideCanvasCells")},
      "geometry":{"faceCount":d["faceCount"],"floorFaces":d["floorFaces"],"vertexCount":d["vertexCount"],"skipped":d["skipped"],"entityCount":d["entityCount"],"playableBounds":d["playableBounds"],"zRange":d["zRange"],"zBands":d["zBands"],"cellCount":d["cellCount"],"multiLevelCells":d["multiLevelCells"],"levelHistogram":d["levelHistogram"],"worldModel":d["worldModel"],"zFilter":d["zFilter"]},
      "controlPoints":cps,"teamSpawnCentroids":tc,"extra":extra,
      "errorBudget":{"roundTripMaxAbsErrorWorldUnits":max(rt) if rt else None,"halfPixelWorldUnits":0.5/s,"pixelQuantization":"world error <= 0.5 px * unitsPerPixel at 1024x1024"}
    }
json.dump(out,open("D:/TF2_Native_Test/evidence/t65-minimap/data/projection_ledger.json","w"),indent=1,ensure_ascii=False)
print("wrote projection_ledger.json")
for n,v in out.items():
    print(n,"cps",len(v["controlPoints"]),"teams",{k:x["count"] for k,x in v["teamSpawnCentroids"].items()},"halfpx",round(v["errorBudget"]["halfPixelWorldUnits"],3))
