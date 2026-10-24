import json, gzip, csv
from PIL import Image, ImageDraw
CELL=32.0
BG=(18,18,22)
def load_cells(name):
    rows=[]
    with gzip.open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.cells.csv.gz"%name,"rt") as f:
        for x in csv.DictReader(f):
            rows.append((int(x["col"]),int(x["row"]),float(x["minz"]),float(x["maxz"]),int(x["levels"])))
    return rows
for name in ("cp_snakewater_final1","pl_badwater"):
    d=json.load(open("D:/TF2_Native_Test/evidence/t65-minimap/data/%s.bsp.geometry.json"%name))
    p=d["projection"]; s=p["scalePxPerUnit"]; minX=p["minX"]; maxY=p["maxY"]
    W,H=p["width"],p["height"]
    img=Image.new("RGB",(W,H),BG); dr=ImageDraw.Draw(img)
    cells=load_cells(name)
    zmin=min(c[2] for c in cells); zmax=max(c[3] for c in cells)
    for col,row,z0,z1,lv in cells:
        x0=(col*CELL-minX)*s; y0=(maxY-(row+1)*CELL)*s
        x1=((col+1)*CELL-minX)*s; y1=(maxY-row*CELL)*s
        if x1<0 or y1<0 or x0>W or y0>H: continue
        v=(z0-zmin)/max(1e-9,(zmax-zmin))
        g=int(40+160*v)
        # higher levels slightly warmer so multi-level reads at a glance
        col3=(g+ (25 if lv>1 else 0), g, g)
        col3=tuple(min(255,c) for c in col3)
        dr.rectangle([x0,y0,x1,y1],fill=col3)
    # frame border
    fx0,fx1,fy0,fy1=p["frameUnits"]
    dr.rectangle([(fx0-minX)*s,(maxY-fy1)*s,(fx1-minX)*s,(maxY-fy0)*s],outline=(120,200,255),width=2)
    def P(x,y): return ((x-minX)*s,(maxY-y)*s)
    # payload path first, so control-point squares drawn later stay on top
    if d["entities"].get("path_track"):
        for e in d["entities"]["path_track"]:
            o=e["origin"]; X,Y=P(o[0],o[1])
            dr.ellipse([X-2,Y-2,X+2,Y+2],fill=(255,255,255))
    # control points
    cps=d["entities"].get("team_control_point",[])
    for cp in cps:
        o=cp["origin"]; X,Y=P(o[0],o[1])
        r=7; dr.rectangle([X-r,Y-r,X+r,Y+r],fill=(255,215,0),outline=(0,0,0))
        dr.text((X+9,Y-6),str(cp.get("targetname")),fill=(255,255,180))
    # spawn centroids
    spawns=d["entities"].get("info_player_teamspawn",[])
    band=d["zFilter"]; zlo,zhi=band["zlo"],band["zhi"]
    teams={}
    for sp in spawns:
        o=sp["origin"]
        if not (zlo-512<=o[2]<=zhi+512): continue
        teams.setdefault(sp.get("team","?"),[]).append(o)
    colmap={"2":(225,70,70),"3":(80,140,255)}
    for t,pts in teams.items():
        n=len(pts); cx=sum(q[0] for q in pts)/n; cy=sum(q[1] for q in pts)/n
        X,Y=P(cx,cy); c=colmap.get(t,(200,200,200)); r=9
        dr.ellipse([X-r,Y-r,X+r,Y+r],fill=c,outline=(0,0,0))
        dr.text((X+11,Y-6),"team %s spawns (n=%d)"%(t,n),fill=c)
    # title + legend
    dr.rectangle([0,0,W,26],fill=(0,0,0))
    dr.text((8,7),"%s   %dx%d   %.4f units/px   cells=%d   multi-level=%d"%(name,W,H,p["unitsPerPixel"],d["cellCount"],d["multiLevelCells"]),fill=(230,230,230))
    dr.rectangle([6,H-74,470,H-6],fill=(0,0,0))
    dr.text((12,H-68),"yellow square = control point   red/blue circle = team spawn centroid",fill=(220,220,220))
    dr.text((12,H-52),"white dot = payload path_track (pl_badwater only)   cyan box = projection frame",fill=(220,220,220))
    dr.text((12,H-36),"gray shade = floor height (bright = high), +25 red tint where >=2 z layers",fill=(220,220,220))
    out="D:/TF2_Native_Test/evidence/t65-minimap/data/%s.preview.png"%name
    img.save(out)
    print(out)
    # verification: sample marker pixels
    for cp in cps:
        o=cp["origin"]; X,Y=P(o[0],o[1]); px=img.getpixel((int(X),int(Y)))
        print("  cp",cp.get("targetname"),"px",(round(X,1),round(Y,1)),"sample",px,"ok" if px==(255,215,0) else "MISMATCH")
    for t,pts in teams.items():
        n=len(pts); cx=sum(q[0] for q in pts)/n; cy=sum(q[1] for q in pts)/n
        X,Y=P(cx,cy); px=img.getpixel((int(X),int(Y)))
        print("  team",t,"centroid px",(round(X,1),round(Y,1)),"sample",px,"ok" if px==colmap.get(t) else "MISMATCH")
