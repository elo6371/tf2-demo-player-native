
import importlib.util, os, json
here = r'D:/TF2_Native_Worktrees/t105-e5/tools/n14-oracle'
spec = importlib.util.spec_from_file_location('bo', os.path.join(here,'bsp_oracle.py'))
bo = importlib.util.module_from_spec(spec); spec.loader.exec_module(bo)
MAPDIRS=[r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps',
         r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/download/maps']
RECT={'cp_process_f12':(-5056,-4608,5056,4608),'cp_sunshine':(-11008,128,-256,10368),
 'cp_gullywash_f9':(-5202,-3990,5202,3990),'cp_sultry_b8a':(-4832,-6016,4768,5984),
 'koth_product_final':(-5426,-4704,2304,4704),'koth_bagel_rc11':(-4256,-2816,4256,3072),
 'pl_upward_f12':(-3072,-4096,4096,3072)}

def inside(px, py, poly):
    n=len(poly); c=False
    for k in range(n):
        xa,ya=poly[k][0],poly[k][1]; xb,yb=poly[(k+1)%n][0],poly[(k+1)%n][1]
        if (ya>py)!=(yb>py):
            xc = xa + (py-ya)*(xb-xa)/(yb-ya)
            if px < xc: c = not c
    return c

out={}
for m,(x0,y0,x1,y1) in RECT.items():
    p=None
    for d in MAPDIRS:
        c=os.path.join(d,m+'.bsp')
        if os.path.exists(c): p=c; break
    bm=bo.read_bsp(p)
    floors=bm['floors']
    def on_floor(px,py):
        for poly in floors:
            xs=[v[0] for v in poly]; ys=[v[1] for v in poly]
            if px<min(xs) or px>max(xs) or py<min(ys) or py>max(ys): continue
            if inside(px,py,poly): return True
        return False
    rows=[]
    no_origin=0
    for a in bm['anchors']:
        if not a['origin'] or len(a['origin'])<2:
            no_origin+=1; continue
        x,y=a['origin'][0],a['origin'][1]
        if x0<=x<=x1 and y0<=y<=y1: continue
        rows.append({'cls':a['classname'],'name':a.get('targetname',''),
                     'team':a.get('team',''),'xy':[round(x,1),round(y,1)],
                     'on_floor':on_floor(x,y)})
    # how many rows are only outside on one axis?
    out[m]={'outside':len(rows),'on_floor':sum(1 for r in rows if r['on_floor']),'no_origin':no_origin,
            'classes':{}, 'detail':rows[:20]}
    for r in rows: out[m]['classes'][r['cls']]=out[m]['classes'].get(r['cls'],0)+1
    print('%-20s outside=%d on_floor=%d classes=%s' % (m,len(rows),out[m]['on_floor'],out[m]['classes']))
    for r in rows[:8]:
        print('    %-22s %-16s team=%-2s %-22s on_floor=%s' % (r['cls'],r['name'],r['team'],r['xy'],r['on_floor']))
json.dump(out, open(r'D:/_shared/EXEC-2026-11-15/n14/anchors-outside.json','w'), indent=1)
print('WROTE anchors-outside.json')
