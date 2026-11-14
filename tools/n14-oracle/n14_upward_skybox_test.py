
import importlib.util, os, json, math, re
here = r'D:/TF2_Native_Worktrees/t105-e5/tools/n14-oracle'
spec = importlib.util.spec_from_file_location('bo', os.path.join(here,'bsp_oracle.py'))
bo = importlib.util.module_from_spec(spec); spec.loader.exec_module(bo)
P=r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_upward_f12.bsp'
bm=bo.read_bsp(P)
ents=bo.lump(open(P,'rb').read(), bo.LUMP_ENTITIES).decode('utf-8','replace')
mm=re.search(r'\{[^{}]*"classname"\s+"sky_camera"[^{}]*\}', ents)
o=re.search(r'"origin"\s+"([-\d. ]+)"', mm.group(0)); sky=[float(t) for t in o.group(1).split()][:3]
sc=re.search(r'"scale"\s+"([-\d.]+)"', mm.group(0)); scale=float(sc.group(1))
print('sky_camera origin=%s scale=%s' % (sky, scale))
def area2(p):
    a=0.0
    for k in range(len(p)):
        xa,ya=p[k][0],p[k][1]; xb,yb=p[(k+1)%len(p)][0],p[(k+1)%len(p)][1]
        a+=xa*yb-xb*ya
    return abs(a)*0.5
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
    cl.append({'area':sum(area2(q) for q in gp),'polys':len(idxs),
               'bbox':[min(xs),min(ys),max(xs),max(ys)]})
cl.sort(key=lambda d:-d['area'])
main=cl[0]
print('main cluster area=%.0f bbox=%s' % (main['area'], main['bbox']))
print('expected skybox copy: area~%.0f bbox~%.0fx%.0f centred near sky_cam' % (
    main['area']/(scale*scale), (main['bbox'][2]-main['bbox'][0])/scale, (main['bbox'][3]-main['bbox'][1])/scale))
print()
print('all clusters (area, polys, bbox, area/main/256):')
for c in cl:
    ratio=c['area']/(main['area']/(scale*scale)) if main['area'] else 0
    print('  area=%10.0f polys=%4d bbox=%-30s norm_by_scale_area=%.3f' % (
        c['area'], c['polys'], str([round(v) for v in c['bbox']]), ratio))
print()
print('clusters whose bbox contains sky_camera:')
for c in cl:
    b=c['bbox']
    if b[0]<=sky[0]<=b[2] and b[1]<=sky[1]<=b[3]:
        print('  area=%10.0f bbox=%s size=%.0fx%.0f' % (c['area'], [round(v) for v in b], b[2]-b[0], b[3]-b[1]))
