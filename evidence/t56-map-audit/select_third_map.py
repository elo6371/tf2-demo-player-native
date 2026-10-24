#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Static selection aid for T56's third map.
Reads only files already on disk (demo headers jsonl + BSP crossref tsv).
No product, no probe, no launch, no capture."""
import json, os, sys, collections

EV = os.path.dirname(os.path.abspath(__file__))
J = os.path.join(EV, "demo_headers_tfdemos.jsonl")
T = os.path.join(EV, "map_demo_crossref.tsv")

def load():
    demos = []
    with open(J, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                demos.append(json.loads(line))
    cross = {}
    with open(T, encoding="utf-8") as f:
        hdr = f.readline().rstrip("\n").split("\t")
        for line in f:
            p = line.rstrip("\n").split("\t")
            cross[p[0]] = dict(zip(hdr, p))
    return demos, cross

def is_pov(d):
    c = (d.get("client") or "").lower()
    return not ("sourcetv" in c or "gotv" in c or c.startswith("tv"))

def main():
    demos, cross = load()
    by_map = collections.defaultdict(list)
    for d in demos:
        by_map[d["map"]].append(d)
    rows = []
    for m, ds in by_map.items():
        c = cross.get(m)
        if not c:
            continue
        pov = [d for d in ds if is_pov(d)]
        rows.append({
            "map": m,
            "demos": len(ds),
            "pov": len(pov),
            "bsp_bytes": int(c["bsp_bytes"]),
            "bsp_count": int(c["bsp_count"]),
            "max_ticks": max(d["ticks"] for d in ds),
            "pov_max_ticks": max((d["ticks"] for d in pov), default=0),
            "pov_demo": (max(pov, key=lambda d: d["ticks"])["path"] if pov else ""),
        })
    rows.sort(key=lambda r: (-r["pov"], -r["demos"]))
    print("map\tdemos\tpov_demos\tbsp_bytes\tmax_ticks\tpov_max_ticks\tpov_demo")
    for r in rows:
        print("%s\t%d\t%d\t%d\t%d\t%d\t%s" % (r["map"], r["demos"], r["pov"], r["bsp_bytes"],
                                              r["max_ticks"], r["pov_max_ticks"], r["pov_demo"]))

if __name__ == "__main__":
    main()
