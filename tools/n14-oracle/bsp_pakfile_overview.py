
import os,struct
MAPDIRS = [r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps',
           r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/download/maps']
maps = ['cp_process_f12','cp_sunshine','cp_gullywash_f9','cp_sultry_b8a','koth_product_final','koth_bagel_rc11','pl_upward_f12']
def find(m):
    for d in MAPDIRS:
        p = os.path.join(d, m + '.bsp')
        if os.path.exists(p): return p
    return None
for m in maps:
    p = find(m)
    if not p:
        print('%-20s MISSING' % m); continue
    d = open(p,'rb').read()
    ident, ver = struct.unpack_from('<ii', d, 0)
    n = 64
    lumps = struct.unpack_from('<%di' % (n*2), d, 8)
    ofs, ln = lumps[40*2], lumps[40*2+1]
    blob = d[ofs:ofs+ln] if ln else b''
    hits = [t for t in (b'overviews', b'overview', b'_radar', b'materials/vgui') if t in blob]
    print('%-20s pakfile=%7d B  overviewHits=%s  entries=%d' % (m, ln, [h.decode() for h in hits], blob.count(b'PK\x03\x04') if blob[:2]==b'PK' else -1))
