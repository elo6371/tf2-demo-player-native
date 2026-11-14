
import os, struct
root = r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf'
def entries(path):
    d = open(path,'rb').read()
    sig, ver, treeSize = struct.unpack_from('<III', d, 0)
    assert sig == 0x55aa1234
    p = 12 if ver == 1 else 28
    out=[]
    def cstr(p):
        e = d.index(b'\0', p); return d[p:e].decode('ascii','replace'), e+1
    while True:
        ext, p = cstr(p)
        if ext == '': break
        pathdir, p = cstr(p)
        while True:
            name, p = cstr(p)
            crc, pre, arch, eoff, elen, term = struct.unpack_from('<IHHIIH', d, p); p += 18
            if name == '': break
            out.append((pathdir, name, ext, arch, eoff, elen))
    return out
allents = {}
for arch in ['tf2_misc_dir.vpk','tf2_textures_dir.vpk']:
    ents = entries(os.path.join(root,arch))
    allents[arch] = ents
    print(arch, 'entries', len(ents))
    dirs = sorted({e[0] for e in ents})
    print('  dirs with "overview":', [x for x in dirs if 'overview' in x][:10])
    hits = [e for e in ents if 'overview' in e[0]]
    print('  overview entries', len(hits))
    for h in hits[:80]: print('    ', h)
