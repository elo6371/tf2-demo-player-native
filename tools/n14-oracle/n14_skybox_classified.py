
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
def area(poly):
    a=0.0
    for k in range(len(poly)):
        xa,ya=poly[k][0],poly[k][1]; xb,yb=poly[(k+1)%len(poly)][0],poly[(k+1)%len(poly)][1]
        a+=xa*yb-xb*ya
    return abs(a)/2.0
rep={}
for m,(x0,y0,x1,y1) in RECT.items():
    p=find(m); bm=bo.read_bsp(p)
    blob=open(p,'rb').read()
    ents=bo.lump(blob, bo.LUMP_ENTITIES).decode('utf-8','replace')
    sky=None
    mm=re.search(r'\{[^{}]*"classname"\s+"sky_camera"[^{}]*\}', ents)
    if mm:
        blk=mm.group(0)
        o=re.search(r'"origin"\s+"([-\d. ]+)"', blk)
        sc=re.search(r'"scale"\s+"([-\d.]+)"', blk)
        if o:
            v=[float(t) for t in o.group(1).split()]
            sky={'origin':v[:3],'scale':float(sc.group(1)) if sc else None}
    tot=0.0; inside=0.0; skybox=0.0; other=0.0; other_pts=[]
    for poly in bm['floors']:
        A=area(poly); tot+=A
        px=sum(v[0] for v in poly)/len(poly); py=sum(v[1] for v in poly)/len(poly)
        if x0<=px<=x1 and y0<=py<=y1:
            inside+=A; continue
        if sky:
            # 3D-skybox geometry is the playable map scaled DOWN around sky_camera.
            # A skybox copy point maps back to world: origin + (p-origin)*scale
            r=math.hypot(px-sky['origin'][0], py-sky['origin'][1])
            if sky['scale'] and r*sky['scale'] <= max(x1-x0,y1-y0):
                skybox+=A; continue
        other+=A; other_pts.append((round(px,0),round(py,0)))
    rep[m]={'sky_camera':sky,'rect':[x0,y0,x1,y1],
            'floor_area_total':round(tot),'inside':round(inside),'skybox':round(skybox),
            'other_outside':round(other),
            'pct_inside':round(100*inside/tot,1) if tot else 0,
            'pct_skybox':round(100*skybox/tot,1) if tot else 0,
            'pct_other':round(100*other/tot,1) if tot else 0,
            'other_sample':other_pts[:8]}
    print('%-20s sky=%-42s inside=%5.1f%% skybox=%5.1f%% other=%5.1f%%' % (m, sky, rep[m]['pct_inside'], rep[m]['pct_skybox'], rep[m]['pct_other']))
json.dump(rep, open(r'D:/_shared/EXEC-2026-11-15/n14/skybox-classified.json','w'), indent=1)
