#!/usr/bin/env python3
"""N14 landmarks: what gameplay anchors exist, where, and which ones can prove orientation.

Purpose (N14 acceptance):
  * a control point is not enough to prove orientation -- KOTH has exactly one, so the
    three-non-collinear-landmark requirement must be satisfied from other anchors;
  * the pl_upward_f12 crop question (43% of floor mass outside the declared rect) can
    only be settled by finding out whether PLAYABLE space continues past the rect edge.
    The payload rail (path_track chain) is playable space by definition: the cart must
    reach the last point, and players must walk beside it. So the rail's extent is a
    reachability proxy that does not depend on guessing what out-of-bounds terrain is.

Output: per map -- per-class counts, per-team spawn centroids, control points,
cart rail extent vs the declared rect, and the non-collinear triples that exist.
"""
import importlib.util
import json
import math
import os
import re
import sys


def load_oracle():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("bsp_oracle_mod",
                                                  os.path.join(here, "bsp_oracle.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MAP_ROW_RE = re.compile(
    r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(\w+),\s*(\d+)\}')

LANDMARK_CLASSES = [
    "info_player_teamspawn", "info_player_start", "team_control_point",
    "team_train_watcher", "path_track", "item_teamflag", "func_capturezone",
    "trigger_capture_area", "func_respawnroom",
]


def centroid(pts):
    if not pts:
        return None
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))


def area2(a, b, c):
    return abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]))


def main():
    header, out_json = sys.argv[1], sys.argv[2]
    roots, i = [], 3
    while i < len(sys.argv):
        if sys.argv[i] == "--maps-dir":
            roots.append(sys.argv[i + 1]); i += 2
        else:
            i += 1

    text = open(header, encoding="utf-8").read()
    rows = [{"map": m.group(1), "rect": [float(m.group(2)), float(m.group(3)),
                                         float(m.group(4)), float(m.group(5))]}
            for m in MAP_ROW_RE.finditer(text)]
    mod = load_oracle()
    report = {"header": header, "maps": []}

    for row in rows:
        name, rect = row["map"], row["rect"]
        bsp = None
        for root in roots:
            cand = os.path.join(root, name + ".bsp")
            if os.path.exists(cand):
                bsp = cand
                break
        if bsp is None:
            report["maps"].append({"map": name, "error": "bsp not found"})
            continue
        m = mod.read_bsp(bsp)
        counts, by_class = {}, {}
        for a in m["anchors"]:
            cn = a["classname"]
            counts[cn] = counts.get(cn, 0) + 1
            by_class.setdefault(cn, []).append(a)

        # TeamNum, not team, is the key info_player_teamspawn uses (verified by
        # dumping an entity block: keys are classname/hammerid/origin/angles/
        # targetname/spawnflags/StartDisabled/TeamNum). StartDisabled spawns are
        # spawn sets for a game mode that is not running, so they are recorded
        # but not counted as reachable landmarks.
        spawns, disabled = {}, {}
        for a in by_class.get("info_player_teamspawn", []):
            if not a["origin"]:
                continue
            team = str(a.get("teamnum") or a.get("TeamNum") or "?")
            dis = str(a.get("startdisabled") or a.get("StartDisabled") or "0")
            bucket = disabled if dis == "1" else spawns
            bucket.setdefault(team, []).append(a["origin"][:2])
        spawn_cent = {t: centroid(v) for t, v in spawns.items()}
        dead_cent = {t: centroid(v) for t, v in disabled.items()}

        # control points, with the entity's own identity so a check can match by name
        cps = []
        for a in by_class.get("team_control_point", []):
            if a["origin"]:
                cps.append({"name": a.get("targetname"), "origin": a["origin"]})

        # payload rail: the cart path is playable corridor
        rail = [a["origin"][:2] for a in by_class.get("path_track", []) if a["origin"]]
        rail_box = None
        if rail:
            rail_box = [min(p[0] for p in rail), min(p[1] for p in rail),
                        max(p[0] for p in rail), max(p[1] for p in rail)]
        watchers = [a for a in by_class.get("team_train_watcher", []) if a["origin"]]

        # which landmark GROUPS are usable as independent landmarks?
        groups = {}
        for t, c in spawn_cent.items():
            if c:
                groups["spawn_team" + t] = c
        for cp in cps[:1]:
            groups["control_point"] = (cp["origin"][0], cp["origin"][1])
        if watchers:
            groups["train_watcher"] = (watchers[0]["origin"][0], watchers[0]["origin"][1])
        if rail_box:
            # far end of the rail is a distinct place from the watcher/start
            far = max(rail, key=lambda p: p[0] if abs(p[0]) > abs(p[1]) else p[1])
            groups["rail_far"] = far

        names = sorted(groups)
        triples, collinear = [], 0
        for a in range(len(names)):
            for b in range(a + 1, len(names)):
                for c in range(b + 1, len(names)):
                    pa, pb, pc = groups[names[a]], groups[names[b]], groups[names[c]]
                    if area2(pa, pb, pc) < 1.0:
                        collinear += 1
                    else:
                        triples.append([names[a], names[b], names[c],
                                        round(area2(pa, pb, pc) / 2.0, 1)])

        rail_out = None
        if rail_box:
            rail_out = {
                "rail_entities": len(rail),
                "rail_bbox": [round(v, 1) for v in rail_box],
                "rail_outside_rect_entities": sum(
                    1 for p in rail if not (rect[0] <= p[0] <= rect[2] and rect[1] <= p[1] <= rect[3])),
                "rail_overflow_units": {
                    "left": round(rect[0] - rail_box[0], 1),
                    "right": round(rail_box[2] - rect[2], 1),
                    "bottom": round(rect[1] - rail_box[1], 1),
                    "top": round(rail_box[3] - rect[3], 1)},
            }

        report["maps"].append({
            "map": name, "bsp": bsp.replace("\\", "/"), "declared_rect": rect,
            "counts": {k: counts.get(k, 0) for k in LANDMARK_CLASSES},
            "spawn_team_centroids": {t: [round(c[0], 1), round(c[1], 1)] for t, c in spawn_cent.items() if c},
            "spawn_counts_by_team": {t: len(v) for t, v in spawns.items()},
            "spawn_disabled_centroids": {t: [round(c[0], 1), round(c[1], 1)]
                                         for t, c in dead_cent.items() if c},
            "spawn_disabled_counts": {t: len(v) for t, v in disabled.items()},
            "control_points": [{"name": c["name"], "origin": [round(v, 1) for v in c["origin"]]} for c in cps],
            "rail": rail_out,
            "landmark_groups": {k: [round(v[0], 1), round(v[1], 1)] for k, v in groups.items()},
            "noncollinear_triples": triples,
            "collinear_triple_count": collinear,
        })

    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)

    print("%-20s %-6s %-6s %-6s %-6s %-5s %-8s %s" % (
        "map", "spawn", "cps", "rail", "flag", "nonc", "railOut", "groups"))
    for mp in report["maps"]:
        if "error" in mp:
            print("%-20s ERROR %s" % (mp["map"], mp["error"])); continue
        c = mp["counts"]
        ro = mp["rail"]
        print("%-20s %-6d %-6d %-6d %-6d %-5d %-8s %s" % (
            mp["map"], c["info_player_teamspawn"], c["team_control_point"],
            c["path_track"], c["item_teamflag"], len(mp["noncollinear_triples"]),
            "-" if not ro else str(ro["rail_outside_rect_entities"]),
            ",".join(sorted(mp["landmark_groups"]))))
    print()
    for mp in report["maps"]:
        if mp.get("rail"):
            print("%-20s rail overflow L %+.0f R %+.0f B %+.0f T %+.0f  (positive = rail sticks out)" % (
                mp["map"], mp["rail"]["rail_overflow_units"]["left"],
                mp["rail"]["rail_overflow_units"]["right"],
                mp["rail"]["rail_overflow_units"]["bottom"],
                mp["rail"]["rail_overflow_units"]["top"]))


if __name__ == "__main__":
    main()
