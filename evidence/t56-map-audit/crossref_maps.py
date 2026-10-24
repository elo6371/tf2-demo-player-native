#!/usr/bin/env python3
"""Cross-reference demo header map names against installed BSPs.

Inputs (static reads only):
  evidence/t56-map-audit/demo_headers_tfdemos.jsonl   (scan of <tf>/demos)
  <tf>/maps/*.bsp  and  <tf>/download/maps/*.bsp
Outputs a TSV: map, demos, bsp_paths, bsp_bytes, reachable
"""
import json, os, sys, glob, collections

TF = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"

def bsp_index():
    idx = collections.defaultdict(list)
    for pat in ("maps/*.bsp", "download/maps/*.bsp"):
        for p in glob.glob(os.path.join(TF, pat)):
            name = os.path.basename(p)[:-4]
            idx[name].append(p)
    return idx

def main(jsonl, out):
    counts = collections.Counter()
    for line in open(jsonl, encoding="utf-8"):
        d = json.loads(line)
        if "error" in d:
            continue
        counts[d["map"]] += 1
    bsp = bsp_index()
    rows = []
    for m, n in counts.most_common():
        paths = bsp.get(m, [])
        size = sum(os.path.getsize(p) for p in paths)
        rows.append((m, n, len(paths), size, ";".join(paths)))
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("map\tdemos\tbsp_count\tbsp_bytes\tbsp_paths\n")
        for r in rows:
            f.write("%s\t%d\t%d\t%d\t%s\n" % r)
    print("maps_with_demos=%d  maps_with_demo_but_no_bsp=%d" % (
        len(rows), sum(1 for r in rows if r[2] == 0)))
    print("--- demo maps with NO installed BSP (top 40 by demo count) ---")
    for r in [r for r in rows if r[2] == 0][:40]:
        print("%5d  %s" % (r[1], r[0]))
    print("--- installed BSPs with no demo ---")
    nodemo = sorted(set(bsp) - set(counts))
    print("count=%d" % len(nodemo))
    print(", ".join(nodemo[:60]))
    print("--- target rows ---")
    for r in rows:
        if r[0] in ("cp_snakewater_final1", "pl_odyssey", "koth_bagel_rc13"):
            print(r)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
