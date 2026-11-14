
import importlib.util, os, json, math, re
here = r'D:/TF2_Native_Worktrees/t105-e5/tools/n14-oracle'
spec = importlib.util.spec_from_file_location('bo', os.path.join(here,'bsp_oracle.py'))
bo = importlib.util.module_from_spec(spec); spec.loader.exec_module(bo)
MAPDIRS=[r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps',
         r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/download/maps']
RECT={'cp_process_f12':(-5056,-4608,5056,4608),'cp_sunshine':(-11008,128,-256,10368),
 'cp_gullywash_f9':(-5202,-3990,5202,3990),'cp_sultry_b8a':(-4832,-6016,4768,5984),
 'koth_product_final':(-5426,-4704,2304,4704),'koth_bagel_rc11':(-4256,-2816,4256,3072),
 'pl_upward_f12':(-3072,-4096,4096,3072)}
def find(m):
    for d in MAPDIRS:
        c=os.path.join(d,m+'.bsp')
        if os.path.exists(c): return c
def area2(p):
    a=0.0
    for k in range(len(p)):
        xa,ya=p[k][0],p[k][1]; xb,yb=p[(k+1)%len(p)][0],p[(k+1)%len(p)][1]
        a+=xa*yb-xb*ya
    return abs(a)*0.5
def clip(poly, rect):
    x0,y0,x1,y1=rect
    def cl(pts, inside, inter):
        out=[]
        for i in range(len(pts)):
            a=pts[i]; b=pts[(i+1)%len(pts)]
            ia=inside(a); ib=inside(b)
            if ia:
                out.append(a)
                if not ib: out.append(inter(a,b))
            elif ib:
                out.append(inter(a,b))
        return out
    def vertical(p,q,x):
        t=(x-p[0])/(q[0]-p[0]); return (x,p[1]+t*(q[1]-p[1]))
    def horizontal(p,q,y):
        t=(y-p[1])/(q[1]-p[1]); return (p[0]+t*(q[0]-p[0]),y)
    p=[(a[0],a[1]) for a in poly]
    p=cl(p, lambda a: a[0]>=x0, lambda a,b: vertical(a,b,x0))
    if not p: return 0.0
    p=cl(p, lambda a: a[0]<=x1, lambda a,b: vertical(a,b,x1))
    if not p: return 0.0
    p=cl(p, lambda a: a[1]>=y0, lambda a,b: horizontal(a,b,y0))
    if not p: return 0.0
    p=cl(p, lambda a: a[1]<=y1, lambda a,b: horizontal(a,b,y1))
    return area2(p) if len(p)>=3 else 0.0
report={}
for m,rect in RECT.items():
    p=find(m); bm=bo.read_bsp(p)
    ents=bo.lump(open(p,'rb').read(), bo.LUMP_ENTITIES).decode('utf-8','replace')
    sky=None
    mm=re.search(r'\{[^{}]*"classname"\s+"sky_camera"[^{}]*\}', ents)
    if mm:
        o=re.search(r'"origin"\s+"([-\d. ]+)"', mm.group(0))
        if o: sky=[float(t) for t in o.group(1).split()][:2]
    polys=[[(v[0],v[1]) for v in poly] for poly in bm['floors']]
    tot=sum(area2(q) for q in polys)
    ins=sum(clip(q,rect) for q in polys)
    # cluster by 128-unit grid cells
    parent=list(range(len(polys)))
    def f(a):
        while parent[a]!=a: parent[a]=parent[parent[a]]; a=parent[a]
        return a
    def u(a,b):
        ra,rb=f(a),f(b)
        if ra!=rb: parent[rb]=ra
    cell={}
    for i,q in enumerate(polys):
        xs=[v[0] for v in q]; ys=[v[1] for v in q]
        for gx in range(int(min(xs)//128), int(max(xs)//128)+1):
            for gy in range(int(min(ys)//128), int(max(ys)//128)+1):
                k=(gx,gy)
                if k in cell: u(cell[k],i)
                else: cell[k]=i
    groups={}
    for i,q in enumerate(polys):
        groups.setdefault(f(i),[]).append(i)
    cl=[]
    for root,idxs in groups.items():
        gpolys=[polys[i] for i in idxs]
        xs=[v[0] for q in gpolys for v in q]; ys=[v[1] for q in gpolys for v in q]
        ar=sum(area2(q) for q in gpolys)
        cx=(min(xs)+max(xs))/2; cy=(min(ys)+max(ys))/2
        has_sky = bool(sky and min(xs)<=sky[0]<=max(xs) and min(ys)<=sky[1]<=max(ys))
        cl.append({'polys':len(idxs),'area':round(ar),'bbox':[round(min(xs)),round(min(ys)),round(max(xs)),round(max(ys))],
                   'centre':[round(cx),round(cy)],'contains_sky_camera':has_sky,
                   'inside_rect_area':round(sum(clip(q,rect) for q in gpolys))})
    cl.sort(key=lambda d:-d['area'])
    report[m]={'sky_camera':sky,'floor_area_total':round(tot),'inside_rect_area':round(ins),
               'pct_inside':round(100*ins/tot,2) if tot else 0,
               'clusters':cl[:6],'cluster_count':len(cl)}
    print('%-18s total=%11.0f inside=%5.1f%% clusters=%d sky_cam=%s' % (m,tot,report[m]['pct_inside'],len(cl),sky))
    for c in cl[:5]:
        print('     area=%10d bbox=%-28s centre=%-16s sky=%s inside=%5.1f%% polys=%d' % (
            c['area'], str(c['bbox']), str(c['centre']), 'YES' if c['contains_sky_camera'] else 'no',
            100*c['inside_rect_area']/c['area'] if c['area'] else 0, c['polys']))
json.dump(report, open(r'D:/_shared/EXEC-2026-11-15/n14/rect-clip.json','w'), indent=1)
