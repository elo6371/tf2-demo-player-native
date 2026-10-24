import sys
sys.path.insert(0,"D:/TF2_Native_Test/evidence/t65-minimap/tools")
from bsp_geometry import Bsp
for name in ("cp_snakewater_final1","pl_badwater"):
    b=Bsp("D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/%s.bsp"%name)
    ents=b.entities()
    bad=[]
    for e in ents:
        o=e.get("origin","").split()
        if len(o)==3:
            try: z=float(o[2])
            except: continue
            if z < -1000 or z > 2000:
                bad.append((e.get("classname"), e.get("origin"), e.get("targetname")))
    print("==",name,len(bad))
    for x in bad[:15]: print("  ",x)
