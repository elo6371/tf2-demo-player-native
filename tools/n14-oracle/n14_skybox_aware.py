
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
def clip(poly,rect):
    x0,y0,x1,y1=rect
    def cl(pts,inside,inter):
        out=[]
        for i in range(len(pts)):
            a=pts[i]; b=pts[(i+1)%len(pts)]
            ia=inside(a); ib=inside(b)
            if ia:
                out.append(a)
                if not ib: out.append(inter(a,b))
            elif ib: out.append(inter(a,b))
        return out
    def vert(p,q,x):
        t=(x-p[0])/(q[0]-p[0]); return (x,p[1]+t*(q[1]-p[1]))
    def horiz(p,q,y):
        t=(y-p[1])/(q[1]-p[1]); return (p[0]+t*(q[0]-p[0]),y)
    p=[(a[0],a[1]) for a in poly]
    for f,g,h in ((lambda a:a[0]>=x0,lambda a,b:vert(a,b,x0),None),
                  (lambda a:a[0]<=x1,lambda a,b:vert(a,b,x1),None),
                  (lambda a:a[1]>=y0,lambda a,b:horiz(a,b,y0),None),
                  (lambda a:a[1]<=y1,lambda a,b:horiz(a,b,y1),None)):
        p=cl(p,f,g)
        if len(p)<3: return 0.0
    return area2(p)
rep={}
for m,rect in RECT.items():
    p=find(m); bm=bo.read_bsp(p)
    ents=bo.lump(open(p,'rb').read(), bo.LUMP_ENTITIES).decode('utf-8','replace')
    sky=None
    mm=re.search(r'\{[^{}]*"classname"\s+"sky_camera"[^{}]*\}', ents)
    if mm:
        o=re.search(r'"origin"\s+"([-\d. ]+)"', mm.group(0))
        if o: sky=[float(t) for t in o.group(1).split()][:2]
    polys=[[(v[0],v[1]) for v in q] for q in bm['floors']]
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
    for i in range(len(polys)): groups.setdefault(f(i),[]).append(i)
    cl=[]
    for root,idxs in groups.items():
        gp=[polys[i] for i in idxs]
        xs=[v[0] for q in gp for v in q]; ys=[v[1] for q in gp for v in q]
        ar=sum(area2(q) for q in gp); ia=sum(clip(q,rect) for q in gp)
        has_sky=bool(sky and min(xs)<=sky[0]<=max(xs) and min(ys)<=sky[1]<=max(ys))
        cl.append({'area':ar,'inside':ia,'bbox':[min(xs),min(ys),max(xs),max(ys)],
                   'sky':has_sky,'polys':len(idxs)})
    tot=sum(c['area'] for c in cl)
    gameplay=[c for c in cl if not c['sky']]
    g_tot=sum(c['area'] for c in gameplay); g_in=sum(c['inside'] for c in gameplay)
    sky_tot=sum(c['area'] for c in cl if c['sky'])
    sky_in=sum(c['inside'] for c in cl if c['sky'])
    rep[m]={'rect':list(rect),'sky_camera':sky,'floor_area_total':round(tot),
            'skybox_area':round(sky_tot),'skybox_inside_rect':round(sky_in),
            'gameplay_area':round(g_tot),'gameplay_inside_rect':round(g_in),
            'gameplay_outside_rect':round(g_tot-g_in),
            'gameplay_pct_outside':round(100*(g_tot-g_in)/g_tot,2) if g_tot else 0,
            'clusters':[{'area':round(c['area']),'inside':round(c['inside']),
                         'bbox':[round(v) for v in c['bbox']],'sky':c['sky'],'polys':c['polys']}
                        for c in sorted(cl,key=lambda d:-d['area'])[:6]]}
    print('%-18s total=%11.0f sky=%11.0f gameplay=%11.0f  outside_rect=%9.0f (%4.1f%%)' % (
        m, tot, sky_tot, g_tot, g_tot-g_in, rep[m]['gameplay_pct_outside']))
    for c in rep[m]['clusters'][:4]:
        print('     area=%10d inside=%10d sky=%-3s bbox=%s' % (c['area'],c['inside'],'YES' if c['sky'] else 'no',c['bbox']))
json.dump(rep, open(r'D:/_shared/EXEC-2026-11-15/n14/skybox-aware.json','w'), indent=1)
