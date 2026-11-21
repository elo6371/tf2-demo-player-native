import sys, os, struct, json, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import leaf_ambient_survey as S

def cube(buf, at):
    out = []
    for c in range(6):
        b = at + c*4
        e = struct.unpack_from('<b', buf, b+3)[0]
        scale = math.ldexp(1.0, e)/255.0
        out.append([buf[b+k]*scale for k in range(3)])
    return out

def lum(c): return 0.2126*c[0]+0.7152*c[1]+0.0722*c[2]

path = sys.argv[1]
blob = open(path,'rb').read()
lumps = [struct.unpack_from('<ii', blob, 8+i*16) for i in range(64)]
ldr = S.unpack_lump(blob, *lumps[56]); hdr = S.unpack_lump(blob, *lumps[55])
i52 = S.unpack_lump(blob, *lumps[52]); i51 = S.unpack_lump(blob, *lumps[51])
nl = len(i52)//4
same = differ = onlyLdr = onlyHdr = countMismatch = 0
maxAbs = 0.0; sumRel = 0.0; nRel = 0
for k in range(nl):
    cA, fA = struct.unpack_from('<HH', i52, k*4)
    cB, fB = struct.unpack_from('<HH', i51, k*4)
    if cA == 0 and cB == 0: continue
    if cA == 0: onlyHdr += 1; continue
    if cB == 0: onlyLdr += 1; continue
    if cA != cB: countMismatch += 1; continue
    leafSame = True
    for s in range(cA):
        a = cube(ldr, (fA+s)*28); b = cube(hdr, (fB+s)*28)
        for i in range(6):
            la, lb = lum(a[i]), lum(b[i])
            d = abs(la-lb)
            maxAbs = max(maxAbs, d)
            if d > 0:
                leafSame = False
                if max(la, lb) > 1e-9: sumRel += d/max(la, lb); nRel += 1
    if leafSame: same += 1
    else: differ += 1
print(json.dumps({"map": os.path.basename(path), "leaves": nl, "identicalLeaves": same, "differingLeaves": differ,
   "onlyLdr": onlyLdr, "onlyHdr": onlyHdr, "countMismatch": countMismatch,
   "maxAbsLumDiff": round(maxAbs, 9), "meanRelDiff": round(sumRel/max(1,nRel), 6), "nPairs": nRel}))
