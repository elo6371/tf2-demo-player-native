
import importlib.util, os, json, re
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
    return None
report={}
for m,(x0,y0,x1,y1) in RECT.items():
    p=find(m); bm=bo.read_bsp(p)
    blob=open(p,'rb').read()
    ents=bo.lump(blob, bo.LUMP_ENTITIES).decode('utf-8','replace')
    # sky_camera?
    sky=[a for a in bm['anchors'] if a['classname']=='sky_camera']
    # raw entity text lookup for the far cluster
    hits=re.findall(r'\{[^{}]*-14[0-9]{3}[^{}]*\}', ents)
    # far floor polys: those whose bbox centre is > 1.4x rect span from rect centre
    cx,cy=(x0+x1)/2.0,(y0+y1)/2.0
    span=max(x1-x0,y1-y0)
    far=[]
    for poly in bm['floors']:
        px=sum(v[0] for v in poly)/len(poly); py=sum(v[1] for v in poly)/len(poly)
        if ((px-cx)**2+(py-cy)**2)**0.5 > 0.9*span:
            far.append((round(px,0),round(py,0)))
    fb=None
    if far:
        fb=[min(f[0] for f in far),min(f[1] for f in far),max(f[0] for f in far),max(f[1] for f in far)]
    report[m]={'sky_camera':[(s0.get('targetname',''),[round(v,1) for v in s0['origin'][:3]]) for s0 in sky],
               'far_floor_polys':len(far),'far_floor_bbox':fb,
               'raw_entity_hits_14xxxx':len(hits),'sample':[h[:180] for h in hits[:2]]}
    print('%-20s sky_camera=%s far_floor_polys=%d far_bbox=%s raw_hits=%d' %
          (m, report[m]['sky_camera'], len(far), fb, len(hits)))
json.dump(report, open(r'D:/_shared/EXEC-2026-11-15/n14/skybox-regions.json','w'), indent=1)
