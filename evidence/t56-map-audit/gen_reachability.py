import json,os,hashlib,glob,struct
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''): h.update(b)
    return h.hexdigest()
hdr={}
for r in map(json.loads,open('demo_headers_tfdemos.jsonl',encoding='utf-8')):
    hdr.setdefault(r['map'],[]).append(r)
td=[]
for p in glob.glob("D:/TF2_Demo_Player/testdata/demos/*.dem"):
    b=open(p,'rb').read(1072)
    def s(o): return b[o:o+260].split(b'\x00',1)[0].decode('utf-8','replace')
    playback,ticks,frames,signon=struct.unpack_from('<fiii',b,8+8+260*4)
    td.append({"path":p,"bytes":os.path.getsize(p),"map":s(8+8+520),"client":s(8+8+260),"ticks":ticks,"frames":frames,"playbackSec":round(playback,3)})
print("testdata demos:",[(os.path.basename(x["path"]),x["map"],x["client"],x["ticks"]) for x in td])
def bsp_for(m):
    for rel in ("maps","download/maps"):
        p=os.path.join(TF,rel,m+".bsp")
        if os.path.exists(p): return p,rel
    return None,None
sel=[("cp_snakewater_final1","D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem","SourceTV"),
     ("cp_snakewater_final1",os.path.join(TF,"demos/autorecord_2026-05-15_23-37-45.dem"),"POV"),
     ("pl_odyssey",os.path.join(TF,"demos/autorecord_2026-07-02_13-26-46.dem"),"POV"),
     ("cp_sunshine",os.path.join(TF,"demos/SUNSHINE.dem"),"SourceTV"),
     ("cp_reckoner",os.path.join(TF,"demos/review12.dem"),"SourceTV"),
     ("koth_bagel_rc12",os.path.join(TF,"demos/autorecord_2026-06-24_21-46-16.dem"),"POV")]
out=[]
for m,demo,rec in sel:
    bp,store=bsp_for(m)
    if not os.path.exists(demo):
        print("MISSING DEMO",demo); continue
    info={}
    for x in td:
        if os.path.basename(x["path"])==os.path.basename(demo): info=x
    for x in hdr.get(m,[]):
        if os.path.basename(x["path"])==os.path.basename(demo): info=x
    n=len(hdr.get(m,[])); stv=sum(1 for x in hdr.get(m,[]) if 'SourceTV' in (x.get('client') or ''))
    row=[m,bp or "MISSING",os.path.getsize(bp) if bp else 0,sha(bp) if bp else "MISSING",store or "MISSING",demo,os.path.getsize(demo),sha(demo),rec,info.get("ticks",""),info.get("frames",""),info.get("playbackSec",""),n,stv,"maps/ then download/maps/ then vpk (native/src/map_lookup.cpp:75-77)"]
    out.append(row); print("\t".join(str(x) for x in row))
open('reachability.tsv','w',encoding='utf-8',newline='').write("map\tbsp_path\tbsp_bytes\tbsp_sha256\tbsp_store\tdemo_path\tdemo_bytes\tdemo_sha256\trecording\tticks\tframes\tplaybackSec\tdemos_on_map\tstv_on_map\tmap_lookup_candidates\n"+"\n".join("\t".join(str(x) for x in r) for r in out)+"\n")
print("wrote reachability.tsv", len(out), "rows")
print("bagel_rc13 bsp:",bsp_for("koth_bagel_rc13"))
