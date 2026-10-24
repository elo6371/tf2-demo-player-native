import gzip, csv, collections
def load(p):
    rows=[]
    with gzip.open(p,"rt") as f:
        r=csv.DictReader(f)
        for x in r:
            rows.append((int(x["col"]), int(x["row"]), float(x["minz"]), float(x["maxz"]), int(x["levels"]), x["material"]))
    return rows
for name in ("cp_snakewater_final1","pl_badwater"):
    rows=load("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.cells.csv.gz"%name)
    print("==",name,"cells",len(rows))
    zs=sorted((r[2]+r[3])/2 for r in rows)
    print(" z pct 0/1/5/50/95/99/100: %.0f %.0f %.0f %.0f %.0f %.0f %.0f"%(zs[0],zs[len(zs)//100],zs[len(zs)//20],zs[len(zs)//2],zs[-len(zs)//20],zs[-len(zs)//100],zs[-1]))
    low=[r for r in rows if (r[2]+r[3])/2 < -500]
    print(" cells z<-500:",len(low))
    if low:
        print("  bbox x[%.0f,%.0f] y[%.0f,%.0f]"%(min(r[0] for r in low)*32,max(r[0] for r in low)*32,min(r[1] for r in low)*32,max(r[1] for r in low)*32))
        print("  mats",collections.Counter(r[5] for r in low).most_common(5))
    # occupancy per 1024 block
    blocks=collections.Counter((r[0]*32//1024, r[1]*32//1024) for r in rows)
    print(" blocks>50 cells:", sorted([(k,v) for k,v in blocks.items() if v>50])[:40])
    print(" total blocks",len(blocks))
