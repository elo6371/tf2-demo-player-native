#!/usr/bin/env python3
"""N14 independent PANEL AUDIT: what is actually inside the minimap rectangle?

Why this file exists. The gate (minimap-hud-check.sh) proves the panel is where it
says and that the control-point rings and the viewer dot are painted. It cannot
answer the N14 question -- "can the player recognise the map?" -- because it never
asks how much of the panel is MAP and how much is MARKER. The task card calls that
failure by name: "a dot on a sign cannot replace a readable basemap".

So this script reads the captured frame itself and partitions the panel into:
    background   the declared panel colour (nothing drawn)
    border       the declared frame colour
    marker       pixels inside a small disc around a projected marker
                 (control points from the trace, viewer, heading dots)
    structure    everything else -- i.e. pixels that are NOT decoration and NOT a
                 marker. Today this is the number that should be ~0, because the
                 product draws no map geometry. When linework lands, this is the
                 number the gate must see rise, and the same script measures it.

Independence rules followed here:
  * the BMP is decoded by hand (no product code, no gate helper, no PIL)
  * colours come from the PRODUCT'S DECLARED palette (constants are data, not
    arithmetic) but each pixel is binned by nearest declared colour with a
    tolerance, and anything that fits no declared colour is reported as
    "unexplained" rather than being silently folded into a category
  * positions come from the product's own trace (px=,py=) and are checked
    against the pixels; the projection is NOT re-derived here -- that is
    n14_verify_trace.py's job, and duplicating it would hide a disagreement
    instead of exposing it

usage: n14_panel_audit.py <frame.bmp> <trace> [--out audit.json] [--label NAME]
       n14_panel_audit.py --selfcheck
"""
import json
import math
import os
import re
import struct
import sys

# --- the product's declared palette (native/src/main_hud.cpp:842-854) ---------
PALETTE = {
    "background": (0.05, 0.06, 0.08),
    "border":     (0.62, 0.64, 0.66),
    "red":        (0.90, 0.20, 0.18),
    "blu":        (0.20, 0.42, 0.95),
    "neutral":    (0.92, 0.90, 0.85),
    "viewer":     (0.10, 1.00, 0.30),
}
COLOUR_TOL = 26.0        # per-channel distance that still counts as "that colour"
MARKER_DISC = 7          # px radius treated as "this pixel belongs to a marker"
BORDER_BAND = 3          # px band inside the rect edge treated as frame, not map


def _palette255():
    return {k: tuple(int(round(c * 255.0)) for c in v) for k, v in PALETTE.items()}


def read_bmp(path):
    """24-bit uncompressed BMP -> (w, h, rows) with rows[0] = TOP row."""
    blob = open(path, "rb").read()
    if blob[:2] != b"BM":
        raise ValueError("not a BMP: %s" % path)
    data_ofs = struct.unpack_from("<I", blob, 10)[0]
    w, h = struct.unpack_from("<ii", blob, 18)
    bpp = struct.unpack_from("<H", blob, 28)[0]
    comp = struct.unpack_from("<I", blob, 30)[0]
    if bpp != 24 or comp != 0:
        raise ValueError("expected 24-bit uncompressed, got bpp=%d comp=%d" % (bpp, comp))
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


STATE_RE = re.compile(r"minimap: hud state=(\S+) map=(\S+) reason=(\S+) panel=(\d+),(\d+),(\d+),(\d+) "
                      r"scale=([\d.eE+-]+) offsetX=([\d.eE+-]+) offsetY=([\d.eE+-]+) angle=([\d.eE+-]+) "
                      r"yaw=([\d.eE+-]+) eye=([\d.eE+-]+),([\d.eE+-]+) viewerPx=([\d.eE+-]+) "
                      r"viewerPy=([\d.eE+-]+) viewerInside=(\d+)")
CP_RE = re.compile(r"minimap: cp name=(\S+) index=(\d+) team=(-?\d+) world=([\d.eE+-]+),([\d.eE+-]+) "
                   r"px=([\d.eE+-]+),py=([\d.eE+-]+) inside=(\d) drawn=(\d)")


def parse_trace(path):
    state, cps = None, []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            m = STATE_RE.search(raw)
            if m:
                state = {"state": m.group(1), "map": m.group(2), "reason": m.group(3),
                         "x": int(m.group(4)), "y": int(m.group(5)),
                         "w": int(m.group(6)), "h": int(m.group(7)),
                         "scale": float(m.group(8)), "offsetX": float(m.group(9)),
                         "offsetY": float(m.group(10)), "angle": float(m.group(11)),
                         "yaw": float(m.group(12)), "eyeX": float(m.group(13)),
                         "eyeY": float(m.group(14)), "viewerPx": float(m.group(15)),
                         "viewerPy": float(m.group(16)), "viewerInside": int(m.group(17))}
                continue
            m = CP_RE.search(raw)
            if m:
                cps.append({"name": m.group(1), "index": int(m.group(2)), "team": int(m.group(3)),
                            "worldX": float(m.group(4)), "worldY": float(m.group(5)),
                            "px": float(m.group(6)), "py": float(m.group(7)),
                            "inside": int(m.group(8)), "drawn": int(m.group(9))})
    return state, cps


def classify(pixel, pal):
    """Nearest declared colour within COLOUR_TOL, else None (unexplained)."""
    best, bestd = None, 1e9
    for name, ref in pal.items():
        d = max(abs(pixel[0] - ref[0]), abs(pixel[1] - ref[1]), abs(pixel[2] - ref[2]))
        if d < bestd:
            best, bestd = name, d
    return best if bestd <= COLOUR_TOL else None


def audit(frame, trace, label=None):
    w, h, rows = read_bmp(frame)
    state, cps = parse_trace(trace)
    out = {"label": label or os.path.basename(frame), "frame": frame, "frameSize": [w, h],
           "trace": trace, "verdict": "UNSUPPORTED", "reasons": []}
    if state is None:
        out["reasons"].append("trace carries no 'minimap: hud state=' line")
        return out
    out["state"] = state
    if state["state"] != "ready":
        out["reasons"].append("panel not ready in this frame: state=%s reason=%s"
                              % (state["state"], state["reason"]))
        return out
    px, py, pw, ph = state["x"], state["y"], state["w"], state["h"]
    if px < 0 or py < 0 or px + pw > w or py + ph > h:
        out["reasons"].append("panel rect %d,%d,%d,%d outside frame %dx%d" % (px, py, pw, ph, w, h))
        return out

    pal = _palette255()
    markers = [(c["px"] + px, c["py"] + py, "cp:" + c["name"]) for c in cps if c["drawn"]]
    markers.append((state["viewerPx"] + px, state["viewerPy"] + py, "viewer"))
    # heading dots run from the viewer along the reported screen angle
    a = math.radians(state["angle"])
    for k in range(3):
        d = 4.0 + k * 1.0
        markers.append((state["viewerPx"] + px + math.cos(a) * d,
                        state["viewerPy"] + py - math.sin(a) * d, "arrow%d" % k))

    counts = {k: 0 for k in pal}
    counts["unexplained"] = 0
    structure = []
    for yy in range(ph):
        for xx in range(pw):
            p = rows[py + yy][px + xx]
            cls = classify(p, pal)
            counts[cls if cls else "unexplained"] += 1
            edge = (xx < BORDER_BAND or yy < BORDER_BAND or
                    xx >= pw - BORDER_BAND or yy >= ph - BORDER_BAND)
            if edge or cls in ("border", "background"):
                continue
            near = False
            for mx, my, _ in markers:
                if abs((px + xx) - mx) <= MARKER_DISC and abs((py + yy) - my) <= MARKER_DISC:
                    near = True
                    break
            if not near:
                structure.append((xx, yy, p, cls))

    total = pw * ph
    out["panel"] = {"x": px, "y": py, "w": pw, "h": ph, "pixels": total}
    out["counts"] = counts
    out["structure_pixels"] = len(structure)
    out["structure_fraction"] = round(len(structure) / float(total), 6)
    out["panel_colour_composition"] = {k: round(v / float(total), 6) for k, v in counts.items()}

    # marker presence: a ring/dot must exist at each projected position
    present = []
    for mx, my, name in markers:
        lx, ly = mx - px, my - py
        if lx < 0 or ly < 0 or lx >= pw or ly >= ph:
            present.append({"marker": name, "inPanel": False})
            continue
        hit = 0
        for yy in range(max(0, int(ly) - 5), min(ph, int(ly) + 6)):
            for xx in range(max(0, int(lx) - 5), min(pw, int(lx) + 6)):
                if classify(rows[py + yy][px + xx], pal) in ("red", "blu", "neutral", "viewer"):
                    hit += 1
        present.append({"marker": name, "inPanel": True, "paintedPx": hit})
    out["markers"] = present

    # is there anything that could be called a map? structure is the answer.
    out["verdict"] = "PASS" if len(structure) > 0 else "MARKERS-ONLY"
    if len(structure) == 0:
        out["reasons"].append("panel contains no geometry: every non-background pixel is border, "
                              "a marker, or within %d px of one" % MARKER_DISC)
    return out


def selfcheck():
    """Synthesise two tiny panels: one markers-only, one with a 'map' streak.
    The audit must call the first MARKERS-ONLY and the second PASS, and must
    count the streak pixels as structure. A checker that cannot tell them apart
    cannot support any claim about linework."""
    pal = _palette255()
    pw = ph = 40
    w, h = 60, 60
    def build(streak):
        rows = [[(0, 0, 0)] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                rows[y][x] = pal["background"]
        for x in range(pw):
            rows[0][x] = pal["border"]; rows[ph - 1][x] = pal["border"]
        for y in range(ph):
            rows[y][0] = pal["border"]; rows[y][pw - 1] = pal["border"]
        # markers are drawn in FRAME coordinates: the trace puts the panel at
        # (10,10) and gives panel-LOCAL px/py, so frame = 10 + local. Drawing the
        # ring at local coords was the first version of this selfcheck and the
        # audit correctly called the misplaced ring "structure" -- keep the
        # offset explicit so that mistake cannot come back.
        cx, cy = 10 + 20, 10 + 20
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                if 3 <= math.hypot(dx, dy) <= 4.6 and 0 <= cy + dy < h and 0 <= cx + dx < w:
                    rows[cy + dy][cx + dx] = pal["neutral"]
        rows[cy][cx] = pal["viewer"]
        if streak:
            for x in range(6, pw - 6):
                rows[10 + 30][x] = pal["neutral"]
        return rows
    def fake_trace(rows):
        return ("minimap: hud state=ready map=test reason=ok panel=10,10,%d,%d scale=1.0 "
                "offsetX=0.0 offsetY=0.0 angle=90.00 yaw=0.00 eye=0.0,0.0 viewerPx=20.00 "
                "viewerPy=20.00 viewerInside=1 arrowDots=3 cps=1 drawn=1 shapes=5 tick=0\n"
                "minimap: cp name=cp_1 index=0 team=0 world=0.0,0.0 px=20.00,py=20.00 inside=1 drawn=1\n"
                % (pw, ph))
    import tempfile
    results = []
    for label, streak in (("markers-only", False), ("with-streak", True)):
        rows = build(streak)
        bmp = os.path.join(tempfile.gettempdir(), "n14_self_%s.bmp" % label)
        stride = (w * 3 + 3) & ~3
        body = bytearray()
        for y in range(h - 1, -1, -1):
            line = bytearray()
            for x in range(w):
                r, g, b = rows[y][x]
                line += bytes((b, g, r))
            line += b"\0" * (stride - w * 3)
            body += line
        hdr = bytearray(54)
        hdr[0:2] = b"BM"
        struct.pack_into("<I", hdr, 2, 54 + len(body))
        struct.pack_into("<I", hdr, 10, 54)
        struct.pack_into("<i", hdr, 14, 40)
        struct.pack_into("<i", hdr, 18, w)
        struct.pack_into("<i", hdr, 22, h)
        struct.pack_into("<H", hdr, 26, 1)
        struct.pack_into("<H", hdr, 28, 24)
        open(bmp, "wb").write(bytes(hdr) + bytes(body))
        trc = bmp.replace(".bmp", ".trace")
        open(trc, "w", encoding="utf-8").write(fake_trace(rows))
        r = audit(bmp, trc, label)
        results.append({"label": label, "verdict": r["verdict"],
                        "structure": r["structure_pixels"]})
    ok = (results[0]["verdict"] == "MARKERS-ONLY" and results[1]["verdict"] == "PASS"
          and results[1]["structure"] > 0 and results[0]["structure"] == 0)
    print(json.dumps({"selfcheck": "PASS" if ok else "FAIL", "cases": results}, indent=1))
    return 0 if ok else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    frame, trace = sys.argv[1], sys.argv[2]
    out_path = label = None
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--out":
            out_path = sys.argv[i + 1]; i += 2
        elif sys.argv[i] == "--label":
            label = sys.argv[i + 1]; i += 2
        else:
            i += 1
    r = audit(frame, trace, label)
    if out_path:
        json.dump(r, open(out_path, "w", encoding="utf-8"), indent=1)
    print(json.dumps({k: r.get(k) for k in ("label", "verdict", "structure_pixels",
                                            "structure_fraction", "counts", "reasons")}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
