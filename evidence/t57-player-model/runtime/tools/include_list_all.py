import struct, sys
sys.path.insert(0, r"D:/TF2_Native_Test/evidence/t57-player-model/tools")
from vpk_inventory import Vpk
TF = r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
vpk = Vpk(TF + "/tf2_misc_dir.vpk")
def i32(b,o): return struct.unpack_from("<i", b, o)[0]
def cstr(b,o):
    if o<0 or o>=len(b): return ""
    e=b.find(b"\x00",o)
    return b[o:e].decode("utf-8","replace") if e>=0 else ""
names = ["scout","sniper","soldier","demo","medic","heavy","pyro","spy","engineer"]
print("class\tfirstAnimInclude\tincludeCount")
for n in names:
    m = "models/player/%s.mdl" % n
    b = vpk.read(m)
    cnt = i32(b,336); idx = i32(b,340)
    incs = []
    for i in range(max(0,cnt)):
        off = idx + i*8
        labelIdx = i32(b, off); nameIdx = i32(b, off+4)
        nm = cstr(b, off+nameIdx)
        if ".mdl" not in nm: nm = cstr(b, off+labelIdx)
        incs.append(nm)
    first = next((x for x in incs if "_animations.mdl" in x), "")
    print("%s\t%s\t%d\t%s" % (n, first, cnt, " | ".join(incs)))
