#!/usr/bin/env python3
"""N14: is a 0.228 panel difference actual evidence that two maps are distinguishable?

"Different by 22% of pixels" is only meaningful against a scale. The scale used here
is a NEGATIVE CONTROL: take one map, perturb it in ways a player would NOT call a
different map, and measure how big that difference is.

  jitter       the same panel shifted by one pixel in each of 8 directions. A player
               does not say "different map" because the picture moved one pixel.
  thicken      1-px outline dilation (an antialiasing/weight change on the same map).
  downsample   the panel reduced to 20x20 and re-expanded -- the detail a glance keeps.

One map is "distinguishable from another" only if the real pair difference is
comfortably above the largest of these. The ratio (min real pair / jitter ceiling) is
the margin; below 1.0 the silhouette would be a coin flip.

The second, independent signal is LANDMARKS: two maps can share a silhouette shape
and still be told apart by where their control points and spawn rooms sit. That is
measured as the fraction of landmark positions that disagree, using the same
clustering the linework renderer uses.

usage: n14_identity.py <header.h> <out.json> [--maps-dir D ...]
"""
import importlib.util
import json
import math
import os
import re
import sys


def load(path, modname):
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(modname, os.path.join(here, path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


PANEL = 160


def diff_frac(a, b):
    n = len(a)
    d = sum(1 for i in range(n) if a[i] != b[i])
    return d / n


def shifted(mask, dx, dy, size=PANEL):
    out = bytearray(size * size)
    for y in range(size):
        sy = y - dy
        if sy < 0 or sy >= size:
            continue
        for x in range(size):
            sx = x - dx
            if 0 <= sx < size and mask[sy * size + sx]:
                out[y * size + x] = 1
    return out


def dilate(mask, size=PANEL):
    out = bytearray(size * size)
    for y in range(size):
        for x in range(size):
            if mask[y * size + x]:
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < size and 0 <= xx < size:
                            out[yy * size + xx] = 1
    return out


def downsample(mask, cell, size=PANEL):
    """Majority-presence per cell, re-expanded -- what survives a glance."""
    n = size // cell
    cells = bytearray(n * n)
    for cy in range(n):
        for cx in range(n):
            hit = 0
            for y in range(cy * cell, (cy + 1) * cell):
                for x in range(cx * cell, (cx + 1) * cell):
                    hit += mask[y * size + x]
            cells[cy * n + cx] = 1 if hit * 2 >= cell * cell else 0
    out = bytearray(size * size)
    for y in range(size):
        for x in range(size):
            if cells[(y // cell) * n + (x // cell)]:
                out[y * size + x] = 1
    return out


def main():
    header, out_json = sys.argv[1], sys.argv[2]
    roots, i = [], 3
    while i < len(sys.argv):
        if sys.argv[i] == "--maps-dir":
            roots.append(sys.argv[i + 1]); i += 2
        else:
            i += 1
    text = open(header, encoding="utf-8").read()
    rows = [(m.group(1), [float(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(5))])
            for m in re.finditer(r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,', text)]
    lw = load("n14_linework.py", "lw")
    bo = load("bsp_oracle.py", "bo")
    MASKS = {}
    LANDMARKS = {}
    for name, rect in rows:
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand; break
        if bsp is None:
            continue
        m = bo.read_bsp(bsp)
        mask, _sc, _ox, _oy = lw.footprint(m["floors"], rect, PANEL, 1024.0)
        MASKS[name] = mask
        pts = []
        for c in lw.spawn_clusters(m["anchors"]):
            pts.append(("spawn", c["team"], round(c["x"] / 128.0), round(c["y"] / 128.0)))
        for a in m["anchors"]:
            if a["classname"] == "team_control_point":
                pts.append(("cp", a.get("teamnum"), round(a["origin"][0] / 128.0), round(a["origin"][1] / 128.0)))
        LANDMARKS[name] = pts

    names = sorted(MASKS)
    report = {"panel": PANEL, "control": {}, "pairs": [], "landmarks": {}}

    # ---- negative controls: perturbations that are NOT a different map ----------
    ceil_jitter = 0.0
    ceil_thicken = 0.0
    ceil_down = 0.0
    for name in names:
        m = MASKS[name]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            ceil_jitter = max(ceil_jitter, diff_frac(m, shifted(m, dx, dy)))
        ceil_thicken = max(ceil_thicken, diff_frac(m, dilate(m)))
        ceil_down = max(ceil_down, diff_frac(m, downsample(m, 8)))
    ceiling = max(ceil_jitter, ceil_thicken, ceil_down)
    report["control"] = {
        "jitter_1px_max": round(ceil_jitter, 4),
        "thicken_1px_max": round(ceil_thicken, 4),
        "downsample_8px_max": round(ceil_down, 4),
        "ceiling": round(ceiling, 4),
        "meaning": ("a difference at or below 'ceiling' is produced by a perturbation "
                    "that does not change which map this is"),
    }

    # ---- real pairs -----------------------------------------------------------
    for a in range(len(names)):
        for b in range(a + 1, len(names)):
            d = diff_frac(MASKS[names[a]], MASKS[names[b]])
            report["pairs"].append({"a": names[a], "b": names[b], "diff": round(d, 4),
                                    "margin_x": round(d / ceiling, 2) if ceiling else None,
                                    "verdict": "DISTINGUISHABLE" if d > ceiling else "NOT DISTINGUISHABLE"})
    report["pairs"].sort(key=lambda p: p["diff"])
    worst = report["pairs"][0]
    report["summary"] = {
        "pairs": len(report["pairs"]),
        "min_diff": worst["diff"], "min_pair": [worst["a"], worst["b"]],
        "max_diff": report["pairs"][-1]["diff"],
        "all_above_ceiling": all(p["diff"] > ceiling for p in report["pairs"]),
        "min_margin_x": worst["margin_x"],
        "criterion": ("a pair is distinguishable when its differing-pixel fraction exceeds "
                      "the largest fraction produced by 1-px jitter / 1-px thickening / "
                      "8-px downsampling of a single map"),
    }

    # ---- landmarks (independent of silhouette) --------------------------------
    for a in range(len(names)):
        for b in range(a + 1, len(names)):
            la, lb = set(LANDMARKS[names[a]]), set(LANDMARKS[names[b]])
            union = la | lb
            same = len(la & lb)
            report["landmarks"]["%s|%s" % (names[a], names[b])] = {
                "shared": same, "union": len(union),
                "mismatch_fraction": round(1.0 - same / len(union), 4) if union else 0.0,
            }
    json.dump(report, open(out_json, "w", encoding="utf-8"), indent=1)
    print(json.dumps(report["control"], indent=1))
    print(json.dumps(report["summary"], indent=1))
    print("%-22s %-22s %-7s %-8s %s" % ("a", "b", "diff", "margin", "verdict"))
    for p in report["pairs"][:6]:
        print("%-22s %-22s %-7.3f %-8.2f %s" % (p["a"], p["b"], p["diff"], p["margin_x"], p["verdict"]))


if __name__ == "__main__":
    main()
