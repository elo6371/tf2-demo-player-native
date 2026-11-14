
import os
root = r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf'
tot=0
for f in sorted(os.listdir(root)):
    if not f.endswith('_dir.vpk'): continue
    d = open(os.path.join(root,f),'rb').read()
    idx=0; n=0
    while True:
        i = d.find(b'overviews', idx)
        if i < 0: break
        n+=1; idx=i+1
        if n<=6:
            print('%-24s @%9d %r' % (f, i, d[max(0,i-60):i+180]))
    if n:
        tot+=n
        print('%-24s total occurrences %d' % (f,n))
print('TOTAL', tot)
