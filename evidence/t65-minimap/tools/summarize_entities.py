import json, collections
base="D:/TF2_Native_Test/evidence/t65-minimap/data/"
for name in ["cp_snakewater_final1.bsp","pl_badwater.bsp"]:
    d=json.load(open(base+name+".geometry.json"))
    print("=== ",name)
    e=d["entities"]
    for c in ("team_control_point","team_train_watcher","path_track","item_teamflag","func_capturezone"):
        v=e.get(c) or []
        print(c, len(v))
        if c in ("team_control_point","team_train_watcher"):
            for x in v: print("   ", x.get("targetname"), x.get("origin"), "team",x.get("team"), "target",x.get("target"))
        if c=="path_track" and v:
            xs=[x["origin"] for x in v if x.get("origin")]
            print("    x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]"%(min(p[0] for p in xs),max(p[0] for p in xs),min(p[1] for p in xs),max(p[1] for p in xs),min(p[2] for p in xs),max(p[2] for p in xs)))
    sp=e.get("info_player_teamspawn") or []
    print("spawns",len(sp), collections.Counter(str(x.get("team")) for x in sp))
    for t in ("2","3"):
        pts=[x["origin"] for x in sp if str(x.get("team"))==t and x.get("origin")]
        if pts:
            print("  team",t,"n",len(pts),"x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]"%(min(p[0] for p in pts),max(p[0] for p in pts),min(p[1] for p in pts),max(p[1] for p in pts),min(p[2] for p in pts),max(p[2] for p in pts)))
    print("proj", json.dumps(d["projection"]))
