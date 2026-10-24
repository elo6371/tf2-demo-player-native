import json,sys,struct
sys.path.insert(0,'D:/TF2_Native_Test/evidence/t63-sourcetv')
from packet_clock import walk_commands
rows=[json.loads(l) for l in open('D:/TF2_Native_Test/evidence/t56-map-audit/demo_headers_tfdemos.jsonl',encoding='utf-8')]
maps=json.load(open('D:/TF2_Native_Test/evidence/t63-sourcetv/map_summary.json'))['maps']
bsp={m for m,v in maps.items() if v.get('bsp_present')}
pov=[r for r in rows if r.get('client')!='SourceTV Demo' and r.get('map') in bsp and (r.get('ticks') or 0)>5000 and (r.get('bytes') or 0)<60000000]
pov.sort(key=lambda r: r.get('bytes') or 0)
print('candidates',len(pov))
tested=0
for r in pov:
    p=r['path'].replace(chr(92),'/')
    try:
        b=open(p,'rb').read(12000000)
        hdr,packets,commands=walk_commands(b)
    except Exception:
        continue
    tm={t:(off,size) for t,off,size in packets}
    for want in (600,601,602):
        if want in tm:
            off,size=tm[want]
            raw=b[off-88:off-12]
            nz=sum(1 for x in raw if x)
            if nz>0:
                fl=struct.unpack('<19f',raw)
                print('GOOD',r['map'],'ticks',r['ticks'],'bytes',r['bytes'],p)
                print('  tick',want,'nonzero',nz,'floats',[round(x,2) for x in fl[:9]])
                tested+=1
            break
    if tested>=3: break
print('done tested',tested)
