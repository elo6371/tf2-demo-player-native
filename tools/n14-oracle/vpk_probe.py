
import os
root = r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf'
for arch in ['tf2_misc_dir.vpk','tf2_textures_dir.vpk']:
    d = open(os.path.join(root,arch),'rb').read()
    i = d.find(b'materials/overviews')
    print('==',arch,'at',i)
    if i<0: continue
    seg = d[max(0,i-40): i+400]
    print(repr(seg))
