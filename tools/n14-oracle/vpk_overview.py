
import os, struct, sys
root = r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf'
maps = ['cp_process_f12','cp_sunshine','cp_gullywash_f9','cp_sultry_b8a',
        'koth_product_final','koth_bagel_rc11','pl_upward_f12',
        'cp_process_final','pl_upward','cp_gullywash_final','cp_sultry_b8','koth_bagel_rc12','koth_product_rc8']
def entries(path):
    d = open(path,'rb').read()
    sig, ver, tree = struct.unpack_from('<III', d, 0)
    assert sig == 0x55aa1234, hex(sig)
    off = 12
    if ver == 2:
        fsz, asz, tsz, hsz = struct.unpack_from('<IIII', d, 0)
        off = 28
    out = []
    p = off
    while True:
        def cstr(p):
            e = d.index(b'\0', p); return d[p:e].decode('ascii','replace'), e+1
        ext, p = cstr(p)
        if ext == '': break
        pathdir, p = cstr(p)
        while True:
            name, p = cstr(p)
            if name == '' and pathdir == '':
                break
            crc, pre, arch, eoff, elen, term = struct.unpack_from('<IHHIIH', d, p); p += 18
            if name == '':
                break
            out.append((pathdir, name, ext, arch, eoff, elen, pre))
    return out
for arch in ['tf2_misc_dir.vpk','tf2_textures_dir.vpk','tf2_misc_000.vpk']:
    fp = os.path.join(root, arch)
    if not os.path.exists(fp): continue
    try: ents = entries(fp)
    except Exception as e:
        print(arch, 'ERR', e); continue
    ov = [e for e in ents if 'overviews' in e[0]]
    print('==', arch, 'entries', len(ents), 'overview dirs', len(ov))
    for e in ov[:60]:
        print('   ', e[0]+'/'+e[1]+'.'+e[2], 'arch', e[3], 'len', e[5])
