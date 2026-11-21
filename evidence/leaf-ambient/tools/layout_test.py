import sys, os, struct, json, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import leaf_ambient_survey as S

def grid_score(pos, n):
    # count distinct values per axis; a regular grid wants few
    xs = set(p[0] for p in pos); ys = set(p[1] for p in pos); zs = set(p[2] for p in pos)
    return len(xs), len(ys), len(zs)

def analyse(path):
    blob = open(path,'rb').read()
    lumps = [struct.unpack_from('<ii', blob, 8+i*16) for i in range(64)]
    d = S.unpack_lump(blob, *lumps[56]); idx = S.unpack_lump(blob, *lumps[52]); lf = S.unpack_lump(blob, *lumps[10])
    nl = len(lf)//32
    statsA = {"x":{}, "y":{}, "z":{}}
    insideA = 0; checked = 0
    expA = {}
    # Layout A: cube 0..23, position 24..26
    for k in range(0, nl, 7):
        cnt, first = struct.unpack_from('<HH', idx, k*4)
        if cnt == 0 or cnt > 16: continue
        mins = struct.unpack_from('<3h', lf, k*32+8); maxs = struct.unpack_from('<3h', lf, k*32+14)
        if any(maxs[i] <= mins[i] for i in range(3)): continue
        checked += 1
        ok = True
        for s in range(cnt):
            at = (first+s)*28
            px, py, pz = d[at+24], d[at+25], d[at+26]
            for (m, M, v, ax) in ((mins[0],maxs[0],px,'x'), (mins[1],maxs[1],py,'y'), (mins[2],maxs[2],pz,'z')):
                world = m + (M-m)*(v/255.0)
                if not (m-1 <= world <= M+1): ok = False
            statsA['x'][px>>6] = statsA['x'].get(px>>6,0)+1
        if ok: insideA += 1
        cnt0, _ = cnt, 0
        for s in range(cnt):
            at = (first+s)*28
            e = struct.unpack_from('<b', d, at+3)[0]
            expA[e] = expA.get(e,0)+1
    return {"map": os.path.basename(path), "leavesChecked": checked, "positionsInsideBbox_A": insideA,
            "expTop_A": dict(sorted(expA.items(), key=lambda kv:-kv[1])[:5]),
            "posByteTopBits_A": dict(sorted(statsA['x'].items(), key=lambda kv:-kv[1])[:5])}

for p in sys.argv[1:]:
    print(json.dumps(analyse(p)))
