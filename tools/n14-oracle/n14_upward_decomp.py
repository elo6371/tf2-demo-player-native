
import importlib.util, os, json
here = r'D:/TF2_Native_Worktrees/t105-e5/tools/n14-oracle'
spec = importlib.util.spec_from_file_location('bo', os.path.join(here,'bsp_oracle.py'))
bo = importlib.util.module_from_spec(spec); spec.loader.exec_module(bo)
P=r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_upward_f12.bsp'
x0,y0,x1,y1=-3072,-4096,4096,3072
bm=bo.read_bsp(P)
blob=open(P,'rb').read()
ents=bo.lump(blob, bo.LUMP_ENTITIES).decode('utf-8','replace')
print('sky_camera in text:', 'sky_camera' in ents)
import re
for mm in re.finditer(r'"sky_camera"', ents):
    print('  ctx:', ents[max(0,mm.start()-300):mm.start()+300].replace('\n',' ')[:520])
# area decomposition of floor polys outside the rect
def area(poly):
    a=0.0
    for k in range(len(poly)):
        xa,ya=poly[k][0],poly[k][1]; xb,yb=poly[(k+1)%len(poly)][0],poly[(k+1)%len(poly)][1]
        a+=xa*yb-xb*ya
    return abs(a)/2.0
tot=0.0; buckets={}
for poly in bm['floors']:
    A=area(poly); tot+=A
    px=sum(v[0] for v in poly)/len(poly); py=sum(v[1] for v in poly)/len(poly)
    if x0<=px<=x1 and y0<=py<=y1:
        k='inside'
    else:
        # distance beyond the rect on the dominant axis
        dx=max(x0-px, px-x1, 0.0); dy=max(y0-py, py-y1, 0.0)
        d=max(dx,dy)
        k='outside_<1024' if d<1024 else ('outside_1024_2048' if d<2048 else 'outside_>2048')
    buckets[k]=buckets.get(k,0.0)+A
print('total floor area %.0f' % tot)
for k in sorted(buckets): print('  %-20s %12.0f  %5.1f%%' % (k,buckets[k],100*buckets[k]/tot))
json.dump({'map':'pl_upward_f12','rect':[x0,y0,x1,y1],'total_area':tot,'buckets':buckets},
          open(r'D:/_shared/EXEC-2026-11-15/n14/upward-outside-decomp.json','w'), indent=1)
