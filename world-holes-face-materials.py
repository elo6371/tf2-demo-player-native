#!/usr/bin/env python3
"""world-holes-face-materials.py -- which materials the refused brush-model faces use.
Reads lumps 0/2/6/7/14/43/44 (LZMA-aware). Prints, per refused class, the material
histogram of its faces and a visible/invisible summary. Usage: <bsp>"""
import struct, sys, lzma, collections

STATIC = {"func_brush", "func_wall", "func_lod", "func_illusionary"}
L_TEXDATA, L_TEXINFO, L_FACES, L_MODELS = 2, 6, 7, 14
L_STRDATA, L_STRTABLE = 43, 44

def lzma_lump(data, base):
    actual, size = struct.unpack_from("<II", data, base+4)
    props = data[base+12:base+17]
    b = props[0]; lc = b % 9; b //= 9; lp = b % 5; pb = b // 5
    ds = struct.unpack_from("<I", props, 1)[0]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW,
        filters=[{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": ds}])
    out = dec.decompress(data[base+17:], max_length=actual)
    if len(out) != actual: raise ValueError("lump size mismatch")
    return out

def load(path):
    data = open(path,"rb").read()
    tbl = [(struct.unpack_from("<ii", data, 8+i*16)) for i in range(64)]
    def get(i):
        off,ln = tbl[i]
        if ln<=0: return b""
        return lzma_lump(data, off) if data[off:off+4]==b"LZMA" else data[off:off+ln]
    return get

def ents(text):
    out=[]; cur={}; i=0; n=len(text); pending=None
    while i<n:
        c=text[i]
        if c=='{': cur={}; pending=None; i+=1
        elif c=='}':
            if cur: out.append(cur)
            cur={}; pending=None; i+=1
        elif c=='"':
            j=text.find('"', i+1)
            if j<0: break
            tok=text[i+1:j]
            if pending is None: pending=tok
            else: cur[pending]=tok; pending=None
            i=j+1
        else: i+=1
    return out

def main():
    get = load(sys.argv[1])
    ent_list = ents(get(0).decode("latin-1"))
    models = get(L_MODELS)
    texdata = get(L_TEXDATA); texinfo = get(L_TEXINFO); faces = get(L_FACES)
    sdata = get(L_STRDATA); stable = get(L_STRTABLE)
    names=[]
    for i in range(len(stable)//4):
        off = struct.unpack_from("<i", stable, i*4)[0]
        end = sdata.find(b"\x00", off)
        names.append(sdata[off:end].decode("latin-1"))
    def mat_of_face(fi):
        ti = struct.unpack_from("<h", faces, fi*56+10)[0]
        if ti < 0 or (ti+1)*72 > len(texinfo): return "?"
        tdi = struct.unpack_from("<i", texinfo, ti*72+68)[0]
        if tdi < 0 or (tdi+1)*32 > len(texdata): return "?"
        sid = struct.unpack_from("<i", texdata, tdi*32+12)[0]
        return names[sid] if 0 <= sid < len(names) else "?"
    by_model={}
    for e in ent_list:
        mv=e.get("model","")
        if not mv.startswith("*"): continue
        try: idx=int(mv[1:])
        except ValueError: continue
        if idx>0: by_model[idx]=e.get("classname","")
    count=len(models)//48
    per=collections.defaultdict(collections.Counter)
    for idx in range(1,count):
        first,nf = struct.unpack_from("<ii", models, idx*48+40)
        if nf<=0: continue
        cls=by_model.get(idx,"") or "(no-entity)"
        if cls in STATIC: continue
        for k in range(nf):
            per[cls][mat_of_face(first+k)] += 1
    for cls in sorted(per, key=lambda c:-sum(per[c].values())):
        tot=sum(per[cls].values())
        vis=sum(n for m,n in per[cls].items() if not any(t in m.lower() for t in ("nodraw","trigger","invisible","skip","skybox","tools/toolsskybox")))
        print("== %s faces=%d visible-looking=%d" % (cls, tot, vis))
        for m,n in per[cls].most_common(6):
            print("     %-46s %d" % (m,n))

if __name__=="__main__":
    main()
