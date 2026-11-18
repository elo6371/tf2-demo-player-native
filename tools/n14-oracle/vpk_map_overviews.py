
import os,re
root = r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf'
names = ['cp_process_f12','cp_sunshine','cp_gullywash_f9','cp_sultry_b8a','koth_product_final',
         'koth_bagel_rc11','pl_upward','pl_upward_f12','cp_process_final','koth_product_rc8']
print('== raw byte scan of *_dir.vpk for overview/<map> and resource/overviews')
found = {}
for f in sorted(os.listdir(root)):
    if not f.endswith('_dir.vpk'): continue
    p = os.path.join(root,f)
    d = open(p,'rb').read()
    for n in names:
        for pat in ('overviews/'+n, 'overviews\\'+n, n+'.txt', n+'_radar'):
            if pat.encode() in d:
                found.setdefault(f,set()).add(pat)
    if b'resource/overviews' in d:
        found.setdefault(f,set()).add('<resource/overviews dir present>')
for k,v in found.items(): print(' ',k,sorted(v))
if not found: print('  none')
