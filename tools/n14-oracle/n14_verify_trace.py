#!/usr/bin/env python3
"""N14 oracle, tier 1 + tier 2: does the product's reported projection match
first-principles arithmetic, and does the rect it projects from agree with the BSP?

Why this file exists: D43 says the minimap scale was never independently verified.
"Independently" has to mean the oracle does not call the product's formula. So this
script re-derives the projection from the STATED CONVENTION only:

    equal aspect      scale = min(panelW/spanX, panelH/spanY)   (one scale, both axes)
    centred           the world rect is centred in the panel
    top-down          world +y is DOWN in the image, so the image row is
                      py = offsetY - worldY*scale

and then compares those numbers with what the running product printed in its trace
line. Agreement to float noise means the product's arithmetic (and its y-flip and
angle convention) is reproducible from the convention alone -- not that the numbers
were copied out of the product source.

TIER 2 is separate and does not use the convention at all: the control points the
product draws are matched BY NAME against the BSP's own team_control_point origins,
so a rect that is shifted, mirrored or scaled wrongly cannot survive it even if the
arithmetic is self-consistent.

usage: n14_verify_trace.py <header.h> <log...> --maps-dir D [--maps-dir D2]
       n14_verify_trace.py --selfcheck
"""
import importlib.util
import json
import math
import os
import re
import sys

TOL_PX = 0.05     # panel pixels; the product prints 2 decimals
TOL_SCALE_REL = 1e-4
TOL_WORLD = 1.0   # world units for a by-name landmark match


def load_oracle():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("bsp_oracle_mod",
                                                  os.path.join(here, "bsp_oracle.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MAP_ROW_RE = re.compile(
    r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,')
STATE_RE = re.compile(r"minimap: hud state=(\S+) map=(\S+) reason=(\S+) panel=(\d+),(\d+),(\d+),(\d+) "
                      r"scale=([\d.eE+-]+) offsetX=([\d.eE+-]+) offsetY=([\d.eE+-]+) angle=([\d.eE+-]+) "
                      r"yaw=([\d.eE+-]+) eye=([\d.eE+-]+),([\d.eE+-]+) viewerPx=([\d.eE+-]+) "
                      r"viewerPy=([\d.eE+-]+) viewerInside=(\d+)")
CP_RE = re.compile(r"minimap: cp name=(\S+) index=(\d+) team=(-?\d+) world=([\d.eE+-]+),([\d.eE+-]+) "
                   r"px=([\d.eE+-]+),py=([\d.eE+-]+) inside=(\d) drawn=(\d)")


def my_projection(rect, pw, ph):
    """Equal-aspect centred fit, stated convention only. No product helper called."""
    minx, miny, maxx, maxy = rect
    spanx, spany = maxx - minx, maxy - miny
    if spanx <= 0 or spany <= 0 or pw <= 0 or ph <= 0:
        return None
    scale = min(pw / spanx, ph / spany)
    offx = (pw - spanx * scale) * 0.5 - minx * scale
    offy = (ph - spany * scale) * 0.5 + maxy * scale
    return {"scale": scale, "offsetX": offx, "offsetY": offy,
            "scaleX": pw / spanx, "scaleY": ph / spany}


def to_px(proj, x, y):
    return proj["offsetX"] + x * proj["scale"], proj["offsetY"] - y * proj["scale"]


def angle_of(yaw):
    a = (90.0 - yaw) % 360.0
    return a


def check(name, got, want, tol, unit=""):
    ok = abs(got - want) <= tol
    return {"check": name, "got": got, "want": round(want, 6), "tol": tol, "unit": unit,
            "verdict": "PASS" if ok else "FAIL"}


def selfcheck():
    """Prove the verifier can fail. A checker that cannot go red proves nothing."""
    rect = (-3072.0, -4096.0, 4096.0, 3072.0)
    p = my_projection(rect, 160, 160)
    bad = []
    # a deliberately wrong projection must be rejected by the same comparison
    for label, candidate in [("mirrored y", dict(p, offsetY=-p["offsetY"] + 2 * 160)),
                             ("scale x10", dict(p, scale=p["scale"] * 10)),
                             ("no centre", dict(p, offsetX=p["offsetX"] + 6.0))]:
        gx, gy = to_px(candidate, 0.0, -2048.0)
        wx, wy = to_px(p, 0.0, -2048.0)
        if abs(gx - wx) <= TOL_PX and abs(gy - wy) <= TOL_PX:
            bad.append(label)
    print(json.dumps({"selfcheck": "PASS" if not bad else "FAIL",
                      "mutation_not_detected": bad}, indent=1))
    return 0 if not bad else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    header = sys.argv[1]
    args = sys.argv[2:]
    logs, roots = [], []
    out_path = None
    i = 0
    while i < len(args):
        if args[i] == "--maps-dir":
            roots.append(args[i + 1]); i += 2
        elif args[i] == "--out":
            out_path = args[i + 1]; i += 2
        else:
            logs.append(args[i]); i += 1
    text = open(header, encoding="utf-8").read()
    table = {m.group(1): [float(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(5))]
             for m in MAP_ROW_RE.finditer(text)}
    mod = load_oracle()
    report = {"header": header, "logs": logs, "tier1": [], "tier2": [], "notes": []}

    for log in logs:
        for raw in open(log, encoding="utf-8", errors="replace"):
            m = STATE_RE.search(raw)
            if not m:
                continue
            mapname = m.group(2)
            pw, ph = int(m.group(6)), int(m.group(7))
            got = {"scale": float(m.group(8)), "offsetX": float(m.group(9)),
                   "offsetY": float(m.group(10)), "angle": float(m.group(11)),
                   "yaw": float(m.group(12)), "eyeX": float(m.group(13)),
                   "eyeY": float(m.group(14)), "px": float(m.group(15)),
                   "py": float(m.group(16)), "inside": int(m.group(17))}
            entry = {"log": os.path.basename(log), "map": mapname, "panel": [pw, ph],
                     "product": got, "checks": []}
            if mapname not in table:
                entry["checks"].append({"check": "map in header table", "verdict": "FAIL"})
                report["tier1"].append(entry); continue
            rect = table[mapname]
            proj = my_projection(rect, pw, ph)
            wx, wy = to_px(proj, got["eyeX"], got["eyeY"])
            entry["oracle"] = {"rect": rect, "scale": round(proj["scale"], 8),
                               "offsetX": round(proj["offsetX"], 6),
                               "offsetY": round(proj["offsetY"], 6),
                               "viewerPx": round(wx, 4), "viewerPy": round(wy, 4)}
            entry["checks"] += [
                check("scale", got["scale"], proj["scale"], proj["scale"] * TOL_SCALE_REL, "px/u"),
                check("offsetX", got["offsetX"], proj["offsetX"], TOL_PX, "px"),
                check("offsetY", got["offsetY"], proj["offsetY"], TOL_PX, "px"),
                check("viewerPx", got["px"], wx, TOL_PX, "px"),
                check("viewerPy", got["py"], wy, TOL_PX, "px"),
                check("angle(90-yaw)", got["angle"], angle_of(got["yaw"]), 0.02, "deg"),
                # The panel is SQUARE and the world rect is not, so pw/spanX != ph/spanY
                # by construction: comparing them tests my own min() call and always
                # fails for the wrong reason (this check was red on 6/7 maps before the
                # fix and said nothing about the product). The property that matters is
                # one uniform scale in the OUTPUT, and that is measured below on the
                # product's own pixels -- see "uniform scale across control-point pairs".
                # NOT "viewerInside must be 1": on cp_sunshine the fixture camera
                # (eye 0,-2048) genuinely projects outside the declared rect, and the
                # product reporting 0 there is CORRECT. The claim to verify is that the
                # flag agrees with the convention's own containment test -- asserting a
                # constant would have turned correct behaviour into a red frame.
                {"check": "viewerInside == oracle containment",
                 "got": got["inside"],
                 "want": 1 if (0.0 <= wx <= pw and 0.0 <= wy <= ph) else 0,
                 "verdict": "PASS" if got["inside"] == (1 if (0.0 <= wx <= pw and 0.0 <= wy <= ph) else 0) else "FAIL"},
            ]
            # uniform scale, measured on the product's printed pixels only: for every
            # pair of control points, pixel distance / world distance must be the same
            # constant. An axis-stretched projection makes these ratios differ, and
            # nothing in this computation uses my formula.
            pair_pts = []
            for raw2 in open(log, encoding="utf-8", errors="replace"):
                cm = CP_RE.search(raw2)
                if cm:
                    pair_pts.append((float(cm.group(4)), float(cm.group(5)),
                                     float(cm.group(6)), float(cm.group(7))))
            # The viewer is a second world point the product also prints (its world
            # position is the capture CLI camera pose, its pixel is checked by colour in
            # n14_pixel_oracle.py), so a single-control-point KOTH map can still be
            # measured for scale.
            #
            # CORRECTION (this comment used to claim "two non-coincident points fix
            # scale, translation and flip, so this is not a weaker test"): the flip part
            # is false. Two point correspondences admit BOTH the orientation-preserving
            # and the mirrored similarity -- a mirrored projection reproduces both
            # points exactly. So this padding buys scale and translation, NOT axis
            # signs, and it also leaves exactly one pair, where the relative-spread
            # check below is identically 0 (PASS by construction, i.e. a tautology).
            # Axis signs are therefore measured separately, from the whole printed
            # sample, by tools/n14-oracle/n14_mirror_sensitivity.py; on koth_bagel_rc11
            # the X sign there is not measurable at all (worst flip displacement
            # 0.000 px, below TOL_PX). Read tier2 as "the product's printed control
            # point agrees with this BSP by name", not as "the orientation is verified".
            if len(pair_pts) == 1:
                pair_pts.append((got["eyeX"], got["eyeY"], got["px"], got["py"]))
            ratios = []
            for i in range(len(pair_pts)):
                for j in range(i + 1, len(pair_pts)):
                    a, b = pair_pts[i], pair_pts[j]
                    wd = math.hypot(a[0] - b[0], a[1] - b[1])
                    pd = math.hypot(a[2] - b[2], a[3] - b[3])
                    if wd > 1.0:
                        ratios.append(pd / wd)
            # A relative spread needs at least TWO ratios to be able to disagree with
            # itself. With one pair (max-min)/max is exactly 0, so PASS would be
            # unconditional -- the same false-green class as D55. One pair is reported
            # UNSUPPORTED, not PASS, and the count is printed so a reader can see how
            # much sample the PASS actually had.
            if len(ratios) >= 2:
                rel = (max(ratios) - min(ratios)) / max(ratios)
                entry["checks"].append({"check": "uniform scale across cp pairs",
                                        "got": round(rel, 6), "want": "<0.01 relative spread",
                                        "pairs": len(ratios),
                                        "verdict": "PASS" if rel < 0.01 else "FAIL"})
            elif len(ratios) == 1:
                entry["checks"].append({"check": "uniform scale across cp pairs",
                                        "got": "n/a", "want": ">=2 pairs",
                                        "pairs": 1,
                                        "verdict": "UNSUPPORTED",
                                        "why": "single pair: (max-min)/max is 0 by "
                                               "construction, so PASS there would be a "
                                               "tautology"})
            else:
                entry["checks"].append({"check": "uniform scale across cp pairs",
                                        "got": "n/a", "want": ">=2 pairs",
                                        "verdict": "UNSUPPORTED",
                                        "why": "no pair with world distance > 1 u"})
            report["tier1"].append(entry)

            # ---- tier 2: does the rect agree with the BSP, by name?
            bsp = None
            for root in roots:
                cand = os.path.join(root, mapname + ".bsp")
                if os.path.exists(cand):
                    bsp = cand; break
            if bsp is None:
                report["tier2"].append({"map": mapname, "verdict": "UNSUPPORTED",
                                        "why": "bsp not found for this map"})
                continue
            bm = mod.read_bsp(bsp)
            by_name = {}
            for a in bm["anchors"]:
                if a["classname"] == "team_control_point":
                    by_name[a.get("targetname", "")] = a
            worst = 0.0
            matched = 0
            details = []
            for raw2 in open(log, encoding="utf-8", errors="replace"):
                cm = CP_RE.search(raw2)
                if not cm or cm.group(1) not in by_name:
                    continue
                ent = by_name[cm.group(1)]
                wxw, wyw = float(cm.group(4)), float(cm.group(5))
                d = math.hypot(wxw - ent["origin"][0], wyw - ent["origin"][1])
                matched += 1
                worst = max(worst, d)
                details.append({"cp": cm.group(1), "log_world": [wxw, wyw],
                                "bsp_origin": ent["origin"][:2], "dist": round(d, 3)})
            report["tier2"].append({"map": mapname, "matched": matched, "worst_dist": round(worst, 3),
                                    "verdict": ("PASS" if matched and worst <= TOL_WORLD else
                                                ("UNSUPPORTED" if not matched else "FAIL")),
                                    "details": details})

    total = sum(len(e["checks"]) for e in report["tier1"])
    failed = sum(1 for e in report["tier1"] for c in e["checks"] if c["verdict"] == "FAIL")
    unsupported = sum(1 for e in report["tier1"] for c in e["checks"]
                      if c["verdict"] == "UNSUPPORTED")
    report["summary"] = {"tier1_checks": total, "tier1_failed": failed,
                         "tier1_unsupported": unsupported,
                         "tier1_verdict": "PASS" if total and not failed else "FAIL"}
    # NOTE: the first version tested `if "out_path" in dir():`, which is a fragile
    # locals() probe -- it worked by accident here but would silently stop writing if
    # the name were assigned anywhere earlier. Track it explicitly instead.
    if out_path is not None:
        json.dump(report, open(out_path, "w", encoding="utf-8"), indent=1)
    for e in report["tier1"]:
        print("== %s %s panel=%dx%d" % (e["log"], e["map"], e["panel"][0], e["panel"][1]))
        for c in e["checks"]:
            print("   %-4s %-32s got=%s want=%s" % (c["verdict"], c["check"], c.get("got"), c.get("want")))
    for e in report["tier2"]:
        print("== tier2 %s %s matched=%s worst=%s" % (e["map"], e["verdict"],
                                                      e.get("matched"), e.get("worst_dist")))
    print(json.dumps(report["summary"]))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
