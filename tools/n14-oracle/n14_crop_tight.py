
import importlib.util, os, json, re, struct
here = r'D:/TF2_Native_Worktrees/t105-e5/tools/n14-oracle'
spec = importlib.util.spec_from_file_location('bo', os.path.join(here,'bsp_oracle.py'))
bo = importlib.util.module_from_spec(spec); spec.loader.exec_module(bo)
TF=r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf'
MAPS=[('cp_process_f12','maps/cp_process_f12.bsp'),('cp_sunshine','maps/cp_sunshine.bsp'),
      ('cp_gullywash_f9','maps/cp_gullywash_f9.bsp'),('cp_sultry_b8a','download/maps/cp_sultry_b8a.bsp'),
      ('koth_product_final','maps/koth_product_final.bsp'),('koth_bagel_rc11','download/maps/koth_bagel_rc11.bsp'),
      ('pl_upward_f12','maps/pl_upward_f12.bsp')]
RECT={'cp_process_f12':(-5056,-4608,5056,4608),'cp_sunshine':(-11008,128,-256,10368),
      'cp_gullywash_f9':(-5202,-3990,5202,3990),'cp_sultry_b8a':(-4832,-6016,4768,5984),
      'koth_product_final':(-5426,-4704,2304,4704),'koth_bagel_rc11':(-4256,-2816,4256,3072),
      'pl_upward_f12':(-3072,-4096,4096,3072)}
out={}
for m,rel in MAPS:
    p=os.path.join(TF,rel)
    blob=open(p,'rb').read()
    bm=bo.read_bsp(p)
    ents=bo.parse_entities(bo.lump(blob,bo.LUMP_ENTITIES).decode('utf-8','replace'))
    polys=[[(v[0],v[1]) for v in q] for q in bm['floors']]
    def bb(q):
        xs=[v[0] for v in q]; ys=[v[1] for v in q]; return (min(xs),min(ys),max(xs),max(ys))
    parent=list(range(len(polys)))
    def f(a):
        while parent[a]!=a: parent[a]=parent[parent[a]]; a=parent[a]
        return a
    def u(a,b):
        ra,rb=f(a),f(b)
        if ra!=rb: parent[rb]=ra
    cell={}
    for i,q in enumerate(polys):
        x0,y0,x1,y1=bb(q)
        for gx in range(int(x0//128), int(x1//128)+1):
            for gy in range(int(y0//128), int(y1//128)+1):
                k=(gx,gy)
                if k in cell: u(cell[k],i)
                else: cell[k]=i
    groups={}
    for i in range(len(polys)): groups.setdefault(f(i),[]).append(i)
    sky=bm.get('sky_camera') or {}
    skyxy=(sky.get('x'),sky.get('y')) if isinstance(sky,dict) else (None,None)
    if skyxy==(None,None):
        mm=re.search(r'\{[^{}]*"classname"\s+"sky_camera"[^{}]*\}', bo.lump(blob,bo.LUMP_ENTITIES).decode('utf-8','replace'))
        if mm:
            o=re.search(r'"origin"\s+"([-\d. ]+)"', mm.group(0))
            v=[float(t) for t in o.group(1).split()]; skyxy=(v[0],v[1])
    r=RECT[m]
    clusters=[]
    for root,idxs in groups.items():
        gp=[polys[i] for i in idxs]
        x0=min(bb(q)[0] for q in gp); y0=min(bb(q)[1] for q in gp)
        x1=max(bb(q)[2] for q in gp); y1=max(bb(q)[3] for q in gp)
        is_sky = (skyxy[0] is not None and x0<=skyxy[0]<=x1 and y0<=skyxy[1]<=y1)
        clusters.append({'area':sum(1 for _ in idxs),'npolys':len(idxs),'bbox':[x0,y0,x1,y1],'sky':is_sky,
                         'polys':gp})
    # gameplay bbox from non-sky clusters, weighted by polygon count as a size proxy
    gp=[c for c in clusters if not c['sky']]
    gx0=min(c['bbox'][0] for c in gp); gy0=min(c['bbox'][1] for c in gp)
    gx1=max(c['bbox'][2] for c in gp); gy1=max(c['bbox'][3] for c in gp)
    rec={'map':m,'declared_rect':list(r),'sky_camera':skyxy,
         'gameplay_floor_bbox':[gx0,gy0,gx1,gy1],
         'slack_left':gx0-r[0],'slack_bottom':gy0-r[1],'clip_right':gx1-r[2],'clip_top':gy1-r[3],
         'side_notes':{},'clusters_outside':[]}
    rec['rect_span']=[r[2]-r[0], r[3]-r[1]]
    rec['gameplay_span']=[gx1-gx0, gy1-gy0]
    rec['tight_fill_px']=None
    for c in clusters:
        b=c['bbox']
        inside_w=max(0,min(b[2],r[2])-max(b[0],r[0]))
        inside_h=max(0,min(b[3],r[3])-max(b[1],r[1]))
        if inside_w<=0 or inside_h<=0:
            cen=[e for e in ents if 'origin' in e]
            def org(e):
                try: return [float(t) for t in e['origin'].split()][:2]
                except Exception: return None
            hits=[]
            for e in cen:
                o=org(e)
                if o and b[0]-64<=o[0]<=b[2]+64 and b[1]-64<=o[1]<=b[3]+64:
                    hits.append({'cls':e.get('classname',''),'name':e.get('targetname',''),'team':e.get('team',''),'xy':o})
            rec['clusters_outside'].append({'bbox':[round(v) for v in b],'npolys':c['npolys'],'sky':c['sky'],
                'anchors':len(hits),'classes':sorted({h['cls'] for h in hits}),
                'sample':hits[:6]})
    out[m]=rec
json.dump(out, open(r'D:/_shared/EXEC-2026-11-15/n14/crop-tight.json','w'), indent=1)
print('%-20s %-30s %-26s %s' % ('map','declared rect','gameplay floor bbox','slack L/B  clip R/T'))
for m in [x[0] for x in MAPS]:
    r=out[m]
    print('%-20s %-30s %-26s L%-6.0f B%-6.0f R%-6.0f T%-6.0f' % (m,
        '[%.0f,%.0f,%.0f,%.0f]'%tuple(r['declared_rect']),
        '[%.0f,%.0f,%.0f,%.0f]'%tuple(r['gameplay_floor_bbox']),
        r['slack_left'],r['slack_bottom'],r['clip_right'],r['clip_top']))
print()
for m in [x[0] for x in MAPS]:
    r=out[m]
    rs=r['rect_span']; gs=r['gameplay_span']
    print('%-20s rect %5.0fx%-5.0f gameplay %5.0fx%-5.0f  fill_of_rect_x=%.2f y=%.2f  outside_clusters=%d' % (
        m, rs[0],rs[1],gs[0],gs[1], gs[0]/rs[0], gs[1]/rs[1], len(r['clusters_outside'])))
    for c in r['clusters_outside']:
        print('     outside bbox=%s polys=%d sky=%s anchors=%d classes=%s' % (c['bbox'],c['npolys'],c['sky'],c['anchors'],c['classes']))
