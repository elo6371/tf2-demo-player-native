import gzip, csv, json, collections, math
CELL=32.0
def load(name):
    rows={}
    with gzip.open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.cells.csv.gz"%name,"rt") as f:
        for x in csv.DictReader(f):
            rows[(int(x["col"]),int(x["row"]))]=((float(x["minz"])+float(x["maxz"]))/2.0,int(x["levels"]),x["material"])
    return rows
for name in ("cp_snakewater_final1","pl_badwater"):
    cells=load(name)
    geo=json.load(open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.geometry.json"%name))
    pts=[]
    for cls in ("info_player_teamspawn","team_control_point","team_train_watcher","path_track","item_teamflag","func_capturezone"):
        for e in geo["entities"].get(cls,[]):
            if e.get("origin"): pts.append(e["origin"])
    # anchors = cells within 64 units of any gameplay point
    anchors=set()
    for p in pts:
        c=int(math.floor(p[0]/CELL)); r=int(math.floor(p[1]/CELL))
        for dc in (-1,0,1):
            for dr in (-1,0,1):
                k=(c+dc,r+dr)
                if k in cells and abs(cells[k][0]-p[2])<=128: anchors.add(k)
    seen=set(anchors); q=list(anchors)
    while q:
        cur=q.pop(); zc=cells[cur][0]
        for dc in (-1,0,1):
            for dr in (-1,0,1):
                if dc==0 and dr==0: continue
                nb=(cur[0]+dc,cur[1]+dr)
                if nb in cells and nb not in seen and abs(cells[nb][0]-zc)<=64.0:
                    seen.add(nb); q.append(nb)
    def bbox(keys):
        if not keys: return None
        xs=[k[0]*CELL for k in keys]; ys=[k[1]*CELL for k in keys]
        return [min(xs),max(xs)+CELL,min(ys),max(ys)+CELL]
    print("==",name,"cells",len(cells),"anchors",len(anchors),"reachable",len(seen),"%.1f%%"%(100*len(seen)/len(cells)))
    print("  reachable bbox",bbox(seen))
    un=[k for k in cells if k not in seen]
    print("  unreachable",len(un),"bbox",bbox(un))
    zs=[cells[k][0] for k in seen]
    print("  reachable z[%.0f,%.0f]"%(min(zs),max(zs)))
    # unreachable clusters
    comp_of={}; comps=[]
    for s in un:
        if s in comp_of: continue
        st=[s]; comp_of[s]=len(comps); mem=[]
        while st:
            cur=st.pop(); mem.append(cur)
            for dc in (-1,0,1):
                for dr in (-1,0,1):
                    nb=(cur[0]+dc,cur[1]+dr)
                    if nb in cells and nb not in seen and nb not in comp_of:
                        comp_of[nb]=len(comps); st.append(nb)
        comps.append(mem)
    comps.sort(key=len,reverse=True)
    print("  unreachable clusters",len(comps),[(len(c),bbox(c)) for c in comps[:5]])
