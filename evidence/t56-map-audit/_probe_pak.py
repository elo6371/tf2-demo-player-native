import sys, os, struct, io, zipfile, collections
sys.path.insert(0, "evidence/t56-map-audit")
import vpk_materials as V

def lump_bytes(path, idx):
    data = open(path,"rb").read()
    fo, fl, _lv, _fc = struct.unpack_from("<iiii", data, 8 + idx*16)
    if fl <= 0: return b""
    if data[fo:fo+4] == b"LZMA":
        return V.lzma_lump(data, fo)
    return data[fo:fo+fl]

for m,p in [("snakewater", os.path.join(V.TF,"maps/cp_snakewater_final1.bsp")),
            ("odyssey", os.path.join(V.TF,"maps/pl_odyssey.bsp")),
            ("bagel12", os.path.join(V.TF,"download/maps/koth_bagel_rc12.bsp"))]:
    b = lump_bytes(p, 40)
    print("\n%s pakfile bytes=%d magic=%r" % (m, len(b), b[:4]))
    if not b: continue
    z = zipfile.ZipFile(io.BytesIO(b))
    names = z.namelist()
    print("  entries=%d" % len(names))
    c = collections.Counter(n.rsplit(".",1)[-1].lower() for n in names)
    print("  ext:", c.most_common(6))
    print("  sample:", names[:6])
