#!/usr/bin/env python3
"""T57: predict the product's sequence-name join (animation_decoder.cpp:474-480
reads labelIndex at sequenceOffset+4 and the string at sequenceOffset+labelIndex).
Read-only: parses two MDL headers straight out of the retail VPK."""
import struct, sys
sys.path.insert(0, r"D:/TF2_Native_Test/evidence/t57-player-model/tools")
from vpk_inventory import Vpk

TF = r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
vpk = Vpk(TF + "/tf2_misc_dir.vpk")

def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def i32(b, o): return struct.unpack_from("<i", b, o)[0]
def i16(b, o): return struct.unpack_from("<h", b, o)[0]
def cstr(b, o):
    if o < 0 or o >= len(b): return ""
    e = b.find(b"\x00", o)
    if e < 0: return ""
    return b[o:e].decode("utf-8", "replace")

def sequences(path):
    b = vpk.read(path)
    if b is None: raise SystemExit("missing " + path)
    seqCount = u32(b, 188); seqIndex = u32(b, 192)
    out = []
    for i in range(seqCount):
        off = seqIndex + i * 212
        labelIdx = i32(b, off + 4)
        label = cstr(b, off + labelIdx) if labelIdx >= 0 else ""
        blend = i32(b, off + 56)
        animDesc = -1
        if blend > 0:
            ptr = i32(b, off + 60)
            if ptr > 0:
                at = off + ptr
                if 0 <= at <= len(b) - 2:
                    animDesc = i16(b, at)
        out.append((label, animDesc))
    return out

for base_name in ["scout", "soldier"]:
    model = "models/player/%s.mdl" % base_name
    comp = "models/player/%s_animations.mdl" % base_name
    ms = sequences(model); cs = sequences(comp)
    compLabels = {}
    for label, anim in cs:
        if label: compLabels.setdefault(label, anim)
    nameBound = sum(1 for label, _ in ms if label in compLabels and compLabels[label] >= 0)
    baseBound = sum(1 for _, a in ms if a >= 0)
    print("%s modelSequences=%d modelBound=%d companionSequences=%d companionBound=%d nameBound=%d"
          % (model, len(ms), baseBound, len(cs), sum(1 for _, a in cs if a >= 0), nameBound))
    print("  model labels[:8]:", [l for l, _ in ms[:8]])
    print("  companion labels[:8]:", [l for l, _ in cs[:8]])
    print("  matched[:8]:", [l for l, _ in ms if l in compLabels][:8])
