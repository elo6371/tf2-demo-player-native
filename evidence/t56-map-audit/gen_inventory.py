#!/usr/bin/env python3
"""T56 inventory: installed BSP list, selected demo list, sha256 input manifest."""
import os, sys, json, hashlib, csv, datetime

TF = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
REPO = "D:/TF2_Native_Test"
OUT = os.path.dirname(os.path.abspath(__file__))

def sha(p, bs=1 << 20):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        while True:
            b = f.read(bs)
            if not b:
                break
            h.update(b)
    return h.hexdigest()

# demos per map from the earlier crossref
demos = {}
with open(os.path.join(OUT, "map_demo_crossref.tsv"), encoding="utf-8") as f:
    for row in csv.DictReader(f, delimiter="\t"):
        demos[row["map"]] = int(row["demos"])

SELECTED = {"cp_snakewater_final1", "pl_odyssey", "koth_bagel_rc12"}
rows = []
for root in (os.path.join(TF, "maps"), os.path.join(TF, "download", "maps")):
    if not os.path.isdir(root):
        continue
    for fn in sorted(os.listdir(root)):
        if not fn.lower().endswith(".bsp"):
            continue
        p = os.path.join(root, fn)
        st = os.stat(p)
        m = fn[:-4]
        rows.append([m, p.replace("\\", "/"), st.st_size,
                     datetime.datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%dT%H:%M:%S"),
                     demos.get(m, 0),
                     sha(p) if m in SELECTED else ""])
with open(os.path.join(OUT, "bsp_inventory.tsv"), "w", encoding="utf-8", newline="") as f:
    w = csv.writer(f, delimiter="\t", lineterminator="\n")
    w.writerow(["map", "path", "bytes", "mtime", "demos_in_tf_demos", "sha256_selected"])
    w.writerows(rows)
print("bsp_inventory.tsv rows=%d selected=%d" % (len(rows), sum(1 for r in rows if r[5])))

SEL_DEMOS = [
    ("cp_snakewater_final1", "D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem", "world-material-check.sh:61, material-coverage-check.sh:67"),
    ("pl_odyssey", os.path.join(TF, "demos/autorecord_2026-07-02_13-26-46.dem"), "visual-baseline:75, world-material-face:64, hole-check:57, lighting-semantics:81"),
    ("koth_bagel_rc12", os.path.join(TF, "demos/autorecord_2026-06-24_21-46-16.dem"), "candidate third map (not used by any gate)"),
]
seen = set()
with open(os.path.join(OUT, "demos_selected.tsv"), "w", encoding="utf-8", newline="") as f:
    w = csv.writer(f, delimiter="\t", lineterminator="\n")
    w.writerow(["map", "path", "bytes", "sha256", "note"])
    for m, p, note in SEL_DEMOS:
        if p in seen:
            continue
        seen.add(p)
        w.writerow([m, p.replace("\\", "/"), os.path.getsize(p), sha(p), note])
print("demos_selected.tsv rows=%d" % len(seen))

# input hash manifest
lines = []
for m, p, note in SEL_DEMOS:
    if p in seen:
        pass
for _, p, _n in SEL_DEMOS:
    pass
hashed = []
for m, p, _n in SEL_DEMOS:
    if p not in [h[0] for h in hashed]:
        hashed.append((p, sha(p)))
for p, h in hashed:
    lines.append("%s  %s" % (h, p.replace("\\", "/")))
for r in rows:
    if r[5]:
        lines.append("%s  %s" % (r[5], r[1]))
# materials from the resolver
with open(os.path.join(OUT, "map_material_reachability.tsv"), encoding="utf-8") as f:
    for row in csv.DictReader(f, delimiter="\t"):
        if row["vmt_sha256"]:
            lines.append("%s  materials/%s.vmt [%s]" % (row["vmt_sha256"], row["material"], row["map"]))
        if row["vtf_sha256"]:
            lines.append("%s  materials/%s.vtf [%s]" % (row["vtf_sha256"], row["basetexture"], row["map"]))
for g in ("visual-baseline-check.sh", "world-material-check.sh", "material-coverage-check.sh",
          "world-material-face-check.sh", "world-geometry-hole-check.sh", "lighting-semantics-check.sh"):
    p = os.path.join(REPO, g)
    if os.path.exists(p):
        lines.append("%s  %s" % (sha(p), g))
for e in ("native/build-nmake/tf2_demo_native.exe", "native/build-nmake/world_geometry_probe.exe",
          "native/build-nmake/world_material_probe.exe", "native/build-nmake/material_coverage_probe.exe",
          "native/build-nmake/lighting_semantics_probe.exe"):
    p = os.path.join(REPO, e)
    if os.path.exists(p):
        lines.append("%s  %s" % (sha(p), e))
    else:
        lines.append("%s  %s" % ("-" * 64, e + " (ABSENT)"))
with open(os.path.join(OUT, "input_hashes.sha256"), "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print("input_hashes.sha256 lines=%d" % len(lines))
