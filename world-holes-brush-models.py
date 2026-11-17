#!/usr/bin/env python3
"""world-holes-brush-models.py -- refused brush models by class, with face ranges.
Reads lumps 0 (entities) and 14 (models) of a BSP, LZMA-aware. Prints one row per
refused model: classname model firstFace faceCount. Usage: <bsp> [--faces]"""
import struct, sys, lzma, collections

STATIC = {"func_brush", "func_wall", "func_lod", "func_illusionary"}

def lzma_lump(data, base):
    if data[base:base+4] != b"LZMA":
        raise ValueError("not LZMA")
    actual, size = struct.unpack_from("<II", data, base+4)
    props = data[base+12:base+17]
    b = props[0]; lc = b % 9; b //= 9; lp = b % 5; pb = b // 5
    dict_size = struct.unpack_from("<I", props, 1)[0]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW,
        filters=[{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": dict_size}])
    out = dec.decompress(data[base+17:], max_length=actual)
    if len(out) != actual: raise ValueError("lump size mismatch")
    return out

def lumps_of(path):
    data = open(path,"rb").read()
    if data[:4] != b"VBSP": raise SystemExit("not VBSP")
    tbl=[]
    for i in range(64):
        off,ln = struct.unpack_from("<ii", data, 8+i*16)
        tbl.append((off,ln))
    def get(i):
        off,ln = tbl[i]
        if ln<=0: return b""
        return lzma_lump(data, off) if data[off:off+4]==b"LZMA" else data[off:off+ln]
    return data, get

def entities(text):
    out=[]; cur={}; key=None
    i=0
    while i < len(text):
        c = text[i]
        if c == '{':
            cur={}; i+=1
        elif c == '}':
            if cur: out.append(cur)
            cur={}; i+=1
        elif c == '"':
            j=text.find('"', i+1)
            if j<0: break
            k=text.find('"', j+1)
            if k<0: break
            name=text[i+1:j]; val=text[k+1:k+1]
            # simple key/value: "key" "value"
            if k+1 < len(text):
                # find next quote pair after k
                pass
            cur[name]=None
            i=k+1
        else:
            i+=1
    return out

def entities2(text):
    """Parse key/value pairs properly: tokens alternate key, value inside braces."""
    out=[]; cur={}; i=0; n=len(text); pending=None
    while i<n:
        c=text[i]
        if c=='{':
            cur={}; i+=1; pending=None
        elif c=='}':
            if cur: out.append(cur)
            cur={}; pending=None; i+=1
        elif c=='"':
            j=text.find('"', i+1)
            if j<0: break
            tok=text[i+1:j]
            if pending is None: pending=tok
            else:
                cur[pending]=tok; pending=None
            i=j+1
        else:
            i+=1
    return out

def main():
    path=sys.argv[1]
    faces_mode = "--faces" in sys.argv
    data, get = lumps_of(path)
    ents = entities2(get(0).decode("latin-1"))
    models = get(14)
    count = len(models)//48
    by_model={}
    for ent in ents:
        mv=ent.get("model","")
        if not mv.startswith("*"): continue
        try: idx=int(mv[1:])
        except ValueError: continue
        if 0 < idx < count: by_model[idx]=ent.get("classname","")
    rows=[]
    for idx in range(1,count):
        first, nf = struct.unpack_from("<ii", models, idx*48+40)
        if nf<=0: continue
        cls=by_model.get(idx,"")
        if cls in STATIC: continue
        rows.append((cls if cls else "(no-entity)", idx, first, nf))
    per=collections.Counter(); faces=collections.Counter()
    for cls,idx,first,nf in rows:
        per[cls]+=1; faces[cls]+=nf
    print("== %s models=%d refusedModels=%d refusedFaces=%d" % (path, count-1, len(rows), sum(faces.values())))
    for cls,n in faces.most_common():
        print("   refused class=%-26s faces=%-6d models=%d" % (cls,n,per[cls]))
    if faces_mode:
        for cls,idx,first,nf in sorted(rows, key=lambda r:(r[0],r[2])):
            print("   FACE %-26s model=%-4d first=%-6d count=%d ids=%s" % (cls,idx,first,nf,
                ",".join(str(first+k) for k in range(0,nf,max(1,nf//4)))))

if __name__=="__main__":
    main()
