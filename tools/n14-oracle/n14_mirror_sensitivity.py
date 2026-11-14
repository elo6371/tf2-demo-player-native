#!/usr/bin/env python3
"""N14: which axis signs do the seven captured traces actually pin?

WHY THIS EXISTS
  n14_verify_trace.py checks, per map, that the product's printed control-point
  pixels equal the pixels my own projection computes from the BSP origins. Where a
  map prints FIVE control points scattered over the map, an X- or Y-mirrored
  projection would move at least one of them far from its computed pixel, so the
  check pins both axis signs. Where a map prints ONE control point the verifier
  pads the sample with the viewer (n14_verify_trace.py:182-189) and its comment
  claims the padding is "not a weaker test" -- so this file measures exactly that
  claim instead of arguing about it.

METHOD (no product code called; only numbers the product printed)
  Under the stated convention (n14_verify_trace.py:71-72)
      px = offx + x * s        py = offy - y * s
  the rect centre maps to the panel centre, so an X-mirrored product would print
      px' = pw - px            =>  |px' - px| = 2 * |x - cx| * s
  and likewise for Y with (y, cy, ph). cx, cy are the rect centre from the
  compiled table, s the oracle scale, both already verified against the product's
  own printed scale/offset at TOL_PX=0.05.

  A mirrored axis is DETECTABLE from the printed sample iff some correspondence
  point moves further than that same 0.05-px tolerance. max_dx / max_dy below are
  those worst-case displacements in panel pixels.

READING
  detectable == false means: flip that axis and every number in the trace still
  agrees with the oracle within tolerance. That axis sign is NOT verified for that
  map, no matter how many checks came back PASS.
"""
import json
import os

OUT = "D:/_shared/EXEC-2026-11-15/n14/mirror-sensitivity.json"
SRC = "D:/_shared/EXEC-2026-11-15/n14/verify-trace-7.json"
TOL_PX = 0.05


def load():
    with open(SRC, encoding="utf-8") as handle:
        return json.load(handle)


def analyse(entry, tier2):
    oracle = entry["oracle"]
    rect = oracle["rect"]          # minx, miny, maxx, maxy
    scale = oracle["scale"]
    cx = (rect[0] + rect[2]) * 0.5
    cy = (rect[1] + rect[3]) * 0.5
    points = []
    for detail in tier2.get("details", []):
        x, y = detail["log_world"]
        points.append(("cp:" + detail["cp"], x, y))
    points.append(("viewer", entry["product"]["eyeX"], entry["product"]["eyeY"]))
    rows = []
    for (label, x, y) in points:
        rows.append({"point": label,
                     "dx_px": round(2.0 * abs(x - cx) * scale, 3),
                     "dy_px": round(2.0 * abs(y - cy) * scale, 3)})
    cps = [r for r in rows if r["point"].startswith("cp:")]
    max_dx = max(r["dx_px"] for r in rows)
    max_dy = max(r["dy_px"] for r in rows)
    cp_dx = max((r["dx_px"] for r in cps), default=0.0)
    cp_dy = max((r["dy_px"] for r in cps), default=0.0)
    return {
        "map": entry["map"],
        "rect": rect,
        "rect_centre": [round(cx, 3), round(cy, 3)],
        "scale": scale,
        "sample_points": len(rows),
        "cp_only": len(cps),
        "max_dx_px": max_dx, "max_dy_px": max_dy,
        "cp_only_max_dx_px": cp_dx, "cp_only_max_dy_px": cp_dy,
        "x_sign_detectable": max_dx > TOL_PX,
        "y_sign_detectable": max_dy > TOL_PX,
        "x_sign_detectable_without_viewer": cp_dx > TOL_PX,
        "y_sign_detectable_without_viewer": cp_dy > TOL_PX,
        "points": rows,
    }


def main():
    report = load()
    by_map = {e["map"]: e for e in report["tier1"]}
    t2 = {e["map"]: e for e in report["tier2"]}
    out = []
    for mapname in sorted(by_map):
        entry = by_map[mapname]
        if "oracle" not in entry or "product" not in entry:
            continue
        out.append(analyse(entry, t2.get(mapname, {})))
    blind = [r["map"] + ":" + ("X" if not r["x_sign_detectable"] else "")
             + ("Y" if not r["y_sign_detectable"] else "") for r in out
             if not (r["x_sign_detectable"] and r["y_sign_detectable"])]
    result = {"source": SRC, "tol_px": TOL_PX, "maps": out,
              "axis_sign_blind": blind,
              "note": "axis_sign_blind lists map:axis whose flip no printed number "
                      "in the capture could reveal"}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print("%-18s %2s %2s %8s %8s %8s %8s  %s" % ("map", "n", "cp", "max_dx",
                                                 "max_dy", "cp_dx", "cp_dy", "verdict"))
    for r in out:
        print("%-18s %2d %2d %8.3f %8.3f %8.3f %8.3f  X:%s Y:%s%s" % (
            r["map"], r["sample_points"], r["cp_only"], r["max_dx_px"], r["max_dy_px"],
            r["cp_only_max_dx_px"], r["cp_only_max_dy_px"],
            "pinned" if r["x_sign_detectable"] else "BLIND",
            "pinned" if r["y_sign_detectable"] else "BLIND",
            "" if r["x_sign_detectable_without_viewer"] else " (viewer is the only X witness)"))
    print("axis_sign_blind:", blind or "none")
    print("wrote", OUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
