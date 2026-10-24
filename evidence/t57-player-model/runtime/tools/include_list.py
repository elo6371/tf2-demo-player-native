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
for m in ["models/player/scout.mdl","models/player/soldier.mdl","models/player/medic.mdl"]:
    b = vpk.read(m)
    cnt = i32(b,336); idx = i32(b,340)
    print(m, "includeCount=", cnt, "includeIndex=", idx)
    for i in range(max(0,cnt)):
        off = idx + i*8
        labelIdx = i32(b, off); nameIdx = i32(b, off+4)
        name = cstr(b, off+nameIdx)
        if ".mdl" not in name: name = cstr(b, off+labelIdx)
        print("   include[%d] off=%d labelIdx=%d nameIdx=%d -> %r present=%s" % (i, off, labelIdx, nameIdx, name, bool(vpk.read(name))))
