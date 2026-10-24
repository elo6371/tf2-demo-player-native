import gzip, csv, json, math
def load(name):
    cells=[]
    with gzip.open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.cells.csv.gz"%name,"rt") as f:
        for x in csv.DictReader(f):
            cells.append((int(x["col"]),int(x["row"]),(float(x["minz"])+float(x["maxz"]))/2.0,int(x["levels"])))
    return cells
CELL=32.0
for name in ("cp_snakewater_final1","pl_badwater"):
    d=json.load(open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.geometry.json"%name))
    p=d["projection"]; s=p["scalePxPerUnit"]
    cells=load(name)
    W=64; H=32
    grid=[[0]*W for _ in range(H)]
    inside=0
    for c,r,z,lv in cells:
        x=(c+0.5)*CELL; y=(r+0.5)*CELL
        px=(x-p["minX"])*s; py=(p["maxY"]-y)*s
        if 0<=px<p["width"] and 0<=py<p["height"]:
            inside+=1
            gx=min(W-1,int(px/p["width"]*W)); gy=min(H-1,int(py/p["height"]*H))
            grid[gy][gx]+=1
    print("==",name,"insideFrame",inside,"%.1f%%"%(100*inside/len(cells)))
    chars=" .:-=+*#%@"
    for row in grid:
        print("  |"+"".join(chars[min(9,int(v/8))] if v else " " for v in row)+"|")
