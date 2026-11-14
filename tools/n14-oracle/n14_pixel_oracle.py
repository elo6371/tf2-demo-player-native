#!/usr/bin/env python3
"""N14 PIXEL ORACLE: convention -> pixel, with no product number in the path.

Why this file exists. n14_verify_trace.py tier 1 checks the product PRINTED
arithmetic against the convention -- good, but that can only prove the trace is
self-consistent with a formula. It cannot prove the pixels are where the trace says,
and if the compose code and the trace code shared a bug the two would agree on a
wrong picture. This script closes that gap: it takes ONLY the camera (eye, yaw), the
panel rectangle and the declared world rectangle, derives the projection from the
stated convention, and then LOOKS AT THE FRAME to see whether a control point team
colour is painted where the convention says it must be.

What is taken from the product and what is not:
  taken        the panel rect (x,y,w,h -- also re-derived by the gate A2/A3), the
               camera eye/yaw, the map name, the declared world rect
  not taken    scale, offsetX, offsetY, the product px/py per control point, the
               product inside/drawn flags, or any product helper
The declared world rect is product data by definition; whether that rect is a fair
crop of the map is a DIFFERENT question answered against BSP floor geometry by
n14_skybox_aware.py / n14_crop_tight.py, not here. Stated so nobody reads this file
as evidence about the rect.

usage: n14_pixel_oracle.py --header native/include/minimap.h --captures DIR
                           [--out out.json] [--selfcheck]
"""
import json
import math
import os
import re
import struct
import sys

TOL_PX = 0.5
WINDOW = 2
MIN_HITS = 3

PALETTE = {
    "background": (0.05, 0.06, 0.08),
    "border":     (0.62, 0.64, 0.66),
    "red":        (0.90, 0.20, 0.18),
    "blu":        (0.20, 0.42, 0.95),
    "neutral":    (0.92, 0.90, 0.85),
    "viewer":     (0.10, 1.00, 0.30),
}
COLOUR_TOL = 30.0
TEAM_COLOUR = {2: "red", 3: "blu", 0: "neutral"}


def palette255():
    return {k: tuple(int(round(c * 255.0)) for c in v) for k, v in PALETTE.items()}


def read_bmp(path):
    blob = open(path, "rb").read()
    if blob[:2] != b"BM":
        raise ValueError("not a BMP: " + path)
    data_ofs = struct.unpack_from("<I", blob, 10)[0]
    w, h = struct.unpack_from("<ii", blob, 18)
    bottom_up = h > 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        base = data_ofs + y * stride
        row = blob[base:base + w * 3]
        rows.append([(row[i * 3 + 2], row[i * 3 + 1], row[i * 3]) for i in range(w)])
    if bottom_up:
        rows.reverse()
    return w, h, rows


def classify(px, pal):
    best, bestd = None, 1e9
    for name, ref in pal.items():
        d = max(abs(px[0] - ref[0]), abs(px[1] - ref[1]), abs(px[2] - ref[2]))
        if d < bestd:
            best, bestd = name, d
    return best if bestd <= COLOUR_TOL else None


MAP_ROW_RE = re.compile(
    r'\{"([^"]+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,')
STATE_RE = re.compile(r"minimap: hud state=(\S+) map=(\S+) reason=(\S+) panel=(\d+),(\d+),(\d+),(\d+) "
                      r"scale=([\d.eE+-]+) offsetX=([\d.eE+-]+) offsetY=([\d.eE+-]+) angle=([\d.eE+-]+) "
                      r"yaw=([\d.eE+-]+) eye=([\d.eE+-]+),([\d.eE+-]+) viewerPx=([\d.eE+-]+) "
                      r"viewerPy=([\d.eE+-]+) viewerInside=(\d+)")
CP_RE = re.compile(r"minimap: cp name=(\S+) index=(\d+) team=(-?\d+) world=([\d.eE+-]+),([\d.eE+-]+) "
                   r"px=([\d.eE+-]+),py=([\d.eE+-]+) inside=(\d) drawn=(\d)")


def projection(rect, pw, ph):
    """The stated convention and nothing else: equal aspect, centred, world +y down."""
    minx, miny, maxx, maxy = rect
    spanx, spany = maxx - minx, maxy - miny
    if spanx <= 0 or spany <= 0 or pw <= 0 or ph <= 0:
        return None
    scale = min(pw / spanx, ph / spany)
    return {"scale": scale,
            "offsetX": (pw - spanx * scale) * 0.5 - minx * scale,
            "offsetY": (ph - spany * scale) * 0.5 + maxy * scale,
            "scaleX": pw / spanx, "scaleY": ph / spany}


def to_panel(proj, x, y):
    return proj["offsetX"] + x * proj["scale"], proj["offsetY"] - y * proj["scale"]


def count_colour(rows, cx, cy, want, pal):
    hits = 0
    for yy in range(int(round(cy)) - WINDOW, int(round(cy)) + WINDOW + 1):
        for xx in range(int(round(cx)) - WINDOW, int(round(cx)) + WINDOW + 1):
            if 0 <= yy < len(rows) and 0 <= xx < len(rows[0]):
                if classify(rows[yy][xx], pal) == want:
                    hits += 1
    return hits


def audit_map(mapname, rect, bmp, trace):
    w, h, rows = read_bmp(bmp)
    state = None
    cps = []
    for raw in open(trace, encoding="utf-8", errors="replace"):
        m = STATE_RE.search(raw)
        if m:
            state = {"panel": [int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7))],
                     "eyeX": float(m.group(13)), "eyeY": float(m.group(14)),
                     "yaw": float(m.group(12)), "viewerInsideFlag": int(m.group(17)),
                     "product_scale": float(m.group(8)),
                     "product_offsetX": float(m.group(9)),
                     "product_offsetY": float(m.group(10))}
            continue
        m = CP_RE.search(raw)
        if m:
            cps.append({"name": m.group(1), "team": int(m.group(3)),
                        "worldX": float(m.group(4)), "worldY": float(m.group(5)),
                        "product_px": float(m.group(6)), "product_py": float(m.group(7)),
                        "inside": int(m.group(8)), "drawn": int(m.group(9))})
    out = {"map": mapname, "bmp": os.path.basename(bmp), "checks": [], "cps": []}
    if state is None:
        out["checks"].append({"check": "state line present", "verdict": "FAIL"})
        out["verdict"] = "FAIL"
        return out
    pxx, pyy, pw, ph = state["panel"]
    proj = projection(rect, pw, ph)
    out["panel"] = state["panel"]
    out["oracle"] = {"scale": round(proj["scale"], 8), "offsetX": round(proj["offsetX"], 6),
                     "offsetY": round(proj["offsetY"], 6),
                     "equalAspect": round(proj["scaleX"] / proj["scaleY"], 9)}
    out["checks"].append({"check": "product scale == oracle scale",
                          "got": state["product_scale"], "want": round(proj["scale"], 8),
                          "verdict": "PASS" if abs(state["product_scale"] - proj["scale"]) <= proj["scale"] * 1e-4 else "FAIL"})
    out["checks"].append({"check": "product offsetX == oracle offsetX",
                          "got": state["product_offsetX"], "want": round(proj["offsetX"], 4),
                          "verdict": "PASS" if abs(state["product_offsetX"] - proj["offsetX"]) <= TOL_PX else "FAIL"})
    out["checks"].append({"check": "product offsetY == oracle offsetY",
                          "got": state["product_offsetY"], "want": round(proj["offsetY"], 4),
                          "verdict": "PASS" if abs(state["product_offsetY"] - proj["offsetY"]) <= TOL_PX else "FAIL"})
    # NOTE ON THE ASPECT CHECK. The panel is SQUARE (160x160) and the world rect is
    # not, so pw/spanX != ph/spanY by construction -- comparing those two is a test
    # of my own min() call, which always passes for the wrong reason and, if the
    # product stretched one axis, would still pass. The invariant that actually
    # matters is that ONE scale is used for both axes in the OUTPUT, so it is tested
    # against the product's printed pixels below (pairwise world->pixel ratio).
    pal = palette255()
    for cp in cps:
        lx, ly = to_panel(proj, cp["worldX"], cp["worldY"])
        inside_oracle = (0.0 <= lx <= pw) and (0.0 <= ly <= ph)
        want = TEAM_COLOUR.get(cp["team"], "neutral")
        hits = 0
        if inside_oracle:
            hits = count_colour(rows, pxx + lx, pyy + ly, want, pal)
        in_prod = (0 <= cp["product_px"] <= pw and 0 <= cp["product_py"] <= ph)
        phits = count_colour(rows, pxx + cp["product_px"], pyy + cp["product_py"], want, pal) if in_prod else 0
        agree = abs(lx - cp["product_px"]) <= TOL_PX and abs(ly - cp["product_py"]) <= TOL_PX
        rec = {"cp": cp["name"], "team": cp["team"], "wantColour": want,
               "oraclePx": [round(lx, 3), round(ly, 3)],
               "productPx": [cp["product_px"], cp["product_py"]],
               "insideOracle": inside_oracle, "pixelsAtOracle": hits,
               "pixelsAtProduct": phits, "oracleAgreesWithProduct": agree}
        rec["verdict"] = ("PASS" if inside_oracle and hits >= MIN_HITS and agree
                          else ("OUTSIDE" if not inside_oracle else "FAIL"))
        out["cps"].append(rec)
    # --- uniform scale, measured on the product's own pixels -------------------
    # For every pair of control points: pixel distance / world distance must be the
    # same constant. An axis-stretched or non-uniform projection makes these ratios
    # differ, and nothing in this computation uses my formula.
    pairs = []
    for i in range(len(cps)):
        for j in range(i + 1, len(cps)):
            a, b = cps[i], cps[j]
            wd = math.hypot(a["worldX"] - b["worldX"], a["worldY"] - b["worldY"])
            pd = math.hypot(a["product_px"] - b["product_px"], a["product_py"] - b["product_py"])
            if wd > 1.0:
                pairs.append({"a": a["name"], "b": b["name"], "worldDist": round(wd, 3),
                              "pxDist": round(pd, 3), "ratio": round(pd / wd, 9)})
    if pairs:
        rs = [p["ratio"] for p in pairs]
        spread = max(rs) - min(rs)
        # 2-decimal printing on px/py for points up to 160 px away allows a few 1e-4
        rel = spread / max(rs)
        out["checks"].append({"check": "one uniform scale across all control-point pairs",
                              "got": round(rel, 6), "want": "<0.01 relative spread",
                              "pairs": len(pairs),
                              "verdict": "PASS" if rel < 0.01 else "FAIL"})
        out["pairRatios"] = pairs
    else:
        out["checks"].append({"check": "one uniform scale across all control-point pairs",
                              "got": "n/a", "want": ">=1 pair",
                              "verdict": "UNSUPPORTED"})

    vx, vy = to_panel(proj, state["eyeX"], state["eyeY"])
    v_inside = (0.0 <= vx <= pw) and (0.0 <= vy <= ph)
    vhits = count_colour(rows, pxx + vx, pyy + vy, "viewer", pal) if v_inside else 0
    out["viewer"] = {"oraclePx": [round(vx, 3), round(vy, 3)], "insideOracle": v_inside,
                     "pixels": vhits, "productInsideFlag": state["viewerInsideFlag"]}
    out["checks"].append({"check": "viewer dot painted where the convention puts it",
                          "got": vhits, "want": ">=1",
                          "verdict": "PASS" if (not v_inside or vhits >= 1) else "FAIL"})
    out["checks"].append({"check": "product viewerInside flag == oracle containment",
                          "got": state["viewerInsideFlag"], "want": 1 if v_inside else 0,
                          "verdict": "PASS" if state["viewerInsideFlag"] == (1 if v_inside else 0) else "FAIL"})
    fails = [c for c in out["checks"] if c["verdict"] == "FAIL"]
    fails += [c for c in out["cps"] if c["verdict"] == "FAIL"]
    out["verdict"] = "PASS" if not fails else "FAIL"
    return out


def write_bmp(path, rows, w, h):
    stride = (w * 3 + 3) & ~3
    body = bytearray()
    for y in range(h - 1, -1, -1):
        line = bytearray()
        for x in range(w):
            r, g, b = rows[y][x]
            line += bytes((b, g, r))
        body += line + bytes(stride - w * 3)
    hdr = bytearray(54)
    hdr[0:2] = b"BM"
    struct.pack_into("<I", hdr, 2, 54 + len(body))
    struct.pack_into("<I", hdr, 10, 54)
    struct.pack_into("<i", hdr, 14, 40)
    struct.pack_into("<i", hdr, 18, w)
    struct.pack_into("<i", hdr, 22, h)
    struct.pack_into("<H", hdr, 26, 1)
    struct.pack_into("<H", hdr, 28, 24)
    open(path, "wb").write(bytes(hdr) + bytes(body))


def selfcheck():
    """A deliberately MIRRORED picture must be rejected. If the oracle cannot go
    red on a mirrored frame it cannot support any claim about the real one."""
    pal = palette255()
    w = h = 200
    pw = ph = 160
    pxx, pyy = 20, 20
    rect = (-1000.0, -1000.0, 1000.0, 1000.0)
    proj = projection(rect, pw, ph)
    cx, cy = 700.0, -400.0
    lx, ly = to_panel(proj, cx, cy)
    results = []
    for label, draw_at in (("correct", (pxx + lx, pyy + ly)),
                           ("mirrored", (pxx + lx, pyy + (ph - ly)))):
        rows = [[pal["background"]] * w for _ in range(h)]
        for x in range(pw):
            rows[pyy][pxx + x] = pal["border"]
            rows[pyy + ph - 1][pxx + x] = pal["border"]
        for y in range(ph):
            rows[pyy + y][pxx] = pal["border"]
            rows[pyy + y][pxx + pw - 1] = pal["border"]
        # the viewer dot lives at the projected camera (eye 0,0 in this fixture) and
        # must be drawn too, otherwise the viewer check -- correctly -- stays red
        evx, evy = to_panel(proj, 0.0, 0.0)
        for oy in range(-1, 2):
            for ox in range(-1, 2):
                rows[pyy + int(round(evy)) + oy][pxx + int(round(evx)) + ox] = pal["viewer"]
        dx, dy = int(round(draw_at[0])), int(round(draw_at[1]))
        # FILLED disc, radius 4 -- that is what the product paints: the HUD shader
        # discards the outside half of the bounding box, so a control point is a
        # solid dot ~50 px, not an annulus. A hollow ring here would make the 5x5
        # sampling window sit in the hole and every real capture would read 0.
        for oy in range(-4, 5):
            for ox in range(-4, 5):
                if math.hypot(ox, oy) <= 4.2:
                    rows[dy + oy][dx + ox] = pal["blu"]
        bmp = os.path.join(os.environ.get("TEMP", "."), "n14_oracle_" + label + ".bmp")
        write_bmp(bmp, rows, w, h)
        trc = bmp.replace(".bmp", ".trace")
        with open(trc, "w", encoding="utf-8") as fh:
            fh.write("minimap: hud state=ready map=test reason=ok panel=%d,%d,%d,%d scale=%.6f "
                     "offsetX=%.6f offsetY=%.6f angle=57.00 yaw=33.00 eye=0.0,0.0 viewerPx=1.00 "
                     "viewerPy=1.00 viewerInside=1 arrowDots=3 cps=1 drawn=1 shapes=6 tick=0"
                     % (pxx, pyy, pw, ph, proj["scale"], proj["offsetX"], proj["offsetY"]) + chr(10))
            fh.write("minimap: cp name=cp_1 index=0 team=3 world=%.1f,%.1f px=%.2f,py=%.2f inside=1 drawn=1" % (cx, cy, lx, ly) + chr(10))
        r = audit_map("test", rect, bmp, trc)
        results.append({"label": label, "verdict": r["verdict"],
                        "pixelsAtOracle": r["cps"][0]["pixelsAtOracle"]})
    ok = results[0]["verdict"] == "PASS" and results[1]["verdict"] == "FAIL"
    print(json.dumps({"selfcheck": "PASS" if ok else "FAIL", "cases": results}, indent=1))
    return 0 if ok else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    header = captures = out_path = None
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--header":
            header = args[i + 1]; i += 2
        elif args[i] == "--captures":
            captures = args[i + 1]; i += 2
        elif args[i] == "--out":
            out_path = args[i + 1]; i += 2
        else:
            i += 1
    text = open(header, encoding="utf-8").read()
    table = {m.group(1): [float(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(5))]
             for m in MAP_ROW_RE.finditer(text)}
    report = {"header": header, "captures": captures, "maps": [], "summary": {}}
    for mapname in sorted(table):
        bmp = os.path.join(captures, mapname + ".bmp")
        trc = os.path.join(captures, mapname + ".trace")
        if not (os.path.exists(bmp) and os.path.exists(trc)):
            report["maps"].append({"map": mapname, "verdict": "UNSUPPORTED",
                                   "why": "no capture for this map"})
            continue
        try:
            r = audit_map(mapname, table[mapname], bmp, trc)
        except Exception as exc:
            report["maps"].append({"map": mapname, "verdict": "ERROR", "why": str(exc)})
            continue
        report["maps"].append(r)
    passed = [m for m in report["maps"] if m.get("verdict") == "PASS"]
    failed = [m for m in report["maps"] if m.get("verdict") == "FAIL"]
    unsup = [m for m in report["maps"] if m.get("verdict") in ("UNSUPPORTED", "ERROR")]
    report["summary"] = {"maps": len(report["maps"]), "pass": len(passed),
                         "fail": len(failed), "unsupported": len(unsup),
                         "verdict": "PASS" if passed and not failed and not unsup else ("PARTIAL" if passed and not failed else "FAIL")}
    if out_path:
        json.dump(report, open(out_path, "w", encoding="utf-8"), indent=1)
    for m in report["maps"]:
        print("== %-20s %-8s %s" % (m["map"], m.get("verdict"), m.get("why", "")))
        if "checks" in m:
            for c in m["checks"]:
                print("   %-4s %-52s got=%s want=%s" % (c["verdict"], c["check"], c.get("got"), c.get("want")))
            for c in m["cps"]:
                print("   %-4s cp %-12s team=%d oracle=(%s) product=%s px@oracle=%d agree=%s"
                      % (c["verdict"], c["cp"], c["team"], c["oraclePx"], c["productPx"],
                         c["pixelsAtOracle"], c["oracleAgreesWithProduct"]))
            v = m["viewer"]
            print("   viewer oracle=%s inPanel=%s px=%d flag=%d" % (v["oraclePx"], v["insideOracle"], v["pixels"], v["productInsideFlag"]))
    print(json.dumps(report["summary"]))
    return 0 if report["summary"]["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
