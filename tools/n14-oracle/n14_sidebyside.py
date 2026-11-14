#!/usr/bin/env python3
"""Side-by-side: the PRODUCT panel as captured, next to proposed linework drawn at
the same 160 px through the same projection.

Why this exists: the N14 acceptance criterion is "the player can recognise the map
and their relative position". The pixel gate answers "is the panel that rectangle";
the panel audit answers "is there anything in it besides markers". Neither answers
"would a human recognise this". This sheet is the artefact a human looks at.

The product half is CROPPED OUT OF THE REAL CAPTURED FRAME at the panel rect the
trace printed -- not re-rendered from product code, so it cannot inherit a drawing
bug. The proposal half is drawn from BSP floor polygons by the independent reader.
Both use the same camera and the same declared rect, so the halves line up.

Output: <out> (default D:/_shared/EXEC-2026-11-15/n14/side-by-side.png)
"""
import argparse
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bsp_oracle as oracle
import n14_linework as lw

PANEL = 160
SCALE = 3
GUTTER = 8

BG = (18, 20, 24)
FILL = (46, 50, 58)
INK = (234, 232, 226)
RED = (230, 51, 46)
BLU = (51, 107, 242)
NEUTRAL = (235, 230, 217)
VIEWER = (26, 255, 77)
PAPER = (10, 10, 12)

MAPS_DIRS = [
    "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps",
    "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/download/maps",
]
DEFAULT_MAPS = ["cp_process_f12", "cp_sunshine", "cp_gullywash_f9", "cp_sultry_b8a",
                "koth_product_final", "koth_bagel_rc11", "pl_upward_f12"]


def read_bmp24(path):
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        raise SystemExit("not a BMP: %s" % path)
    ofs = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24:
        raise SystemExit("need 24bpp, got %d" % bpp)
    flip = h > 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        base = ofs + (h - 1 - y if flip else y) * stride
        rows.append(d[base:base + w * 3])
    return w, h, rows


def crop_rgb(rows, w, h, x, y, cw, ch):
    out = []
    for j in range(ch):
        yy = y + j
        src = rows[yy] if 0 <= yy < h else b"\x00" * (3 * w)
        line = []
        for i in range(cw):
            xx = x + i
            if 0 <= xx < w:
                line.append((src[3 * xx + 2], src[3 * xx + 1], src[3 * xx]))
            else:
                line.append((0, 0, 0))
        out.append(line)
    return out


def parse_kv(line):
    f = {}
    for tok in line.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            f[k] = v
    return f


def read_trace(path):
    st, cps = None, []
    for line in open(path, encoding="utf-8", errors="replace"):
        if "minimap: hud state=" in line:
            st = parse_kv(line)
        elif "minimap: cp name=" in line:
            f = parse_kv(line)
            # The cp line packs two numbers into each token, so a plain
            # key=value split leaves f["px"] == "23.99,py=100.25" and f["py"]
            # absent. Skipping on that exception silently dropped EVERY cp line
            # (cps=0 on all seven maps) -- the sheet still rendered, minus its
            # markers. Parse the packed form explicitly.
            try:
                wx, wy = f["world"].rstrip(",").split(",")
                px_s, py_s = f["px"].split(",py=") if ",py=" in f["px"] else (f["px"], f["py"])
                cps.append({"name": f["name"], "team": int(f["team"]),
                            "x": float(wx), "y": float(wy),
                            "px": float(px_s), "py": float(py_s)})
            except Exception as exc:
                print("WARN cp line unparsed: %s (%s)" % (line.strip(), exc))
    return st or {}, cps


def proposal_canvas(rect, floors, cps, viewer):
    """160x160 flat list of RGB tuples: filled footprint, 1-px edge, markers."""
    canvas = [BG] * (PANEL * PANEL)
    mask, _s, _ox, _oy = lw.footprint(floors, rect, PANEL, 64.0)
    for i, on in enumerate(mask):
        if on:
            canvas[i] = FILL
    edge = lw.edge_of(mask, PANEL)
    for i, on in enumerate(edge):
        if on:
            canvas[i] = INK
    for cp in cps:
        col = RED if cp["team"] == 2 else (BLU if cp["team"] == 3 else NEUTRAL)
        lw.blob(canvas, PANEL, cp["px"], cp["py"], 4, col)
    if viewer[0] == viewer[0] and viewer[1] == viewer[1]:
        lw.blob(canvas, PANEL, viewer[0], viewer[1], 1, VIEWER)
    return canvas


def flat_to_rows(canvas, size):
    return [canvas[y * size:(y + 1) * size] for y in range(size)]


def write_png(path, rows, scale):
    """rows = list of RGB tuple rows, all the same length; upscaled by scale."""
    H = len(rows)
    W = len(rows[0])
    out = []
    for row in rows:
        big = bytearray()
        for c in row:
            big += bytes(c) * scale
        for _ in range(scale):
            out.append(bytes(big))
    raw = b"".join(b"\x00" + r for r in out)

    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    # The IHDR height must be the row count of the SCALED image (out already
    # repeats each row scale times). Writing the unscaled count squashed the
    # first sheet 3x vertically, and nothing errored.
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, len(out), 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def load_rects(header):
    """Parse the DECLARED rect table only -- no projection constants -- so this
    script cannot inherit a compose bug."""
    txt = open(header, encoding="utf-8", errors="replace").read()
    rects = {}
    pat = re.compile(r'\{\s*"([a-z][a-z0-9_]+)"\s*,\s*(-?[0-9.]+)f?\s*,'
                     r'\s*(-?[0-9.]+)f?\s*,\s*(-?[0-9.]+)f?\s*,\s*(-?[0-9.]+)f?')
    for m in pat.finditer(txt):
        rects[m.group(1)] = tuple(float(m.group(i)) for i in (2, 3, 4, 5))
    return rects


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", default="native/include/minimap.h")
    ap.add_argument("--captures", default="D:/_shared/EXEC-2026-11-15/n14/captures")
    ap.add_argument("--out", default="D:/_shared/EXEC-2026-11-15/n14/side-by-side.png")
    ap.add_argument("--map", action="append")
    a = ap.parse_args()

    rects = load_rects(a.header)
    names = a.map or DEFAULT_MAPS
    width = PANEL * 2 + GUTTER
    canvas = []
    drawn = 0
    for name in names:
        rect = rects.get(name)
        bmp = os.path.join(a.captures, name + ".bmp")
        trc = os.path.join(a.captures, name + ".trace")
        if rect is None or not (os.path.exists(bmp) and os.path.exists(trc)):
            print("SKIP %-20s rect=%s bmp=%s" % (name, bool(rect), os.path.exists(bmp)))
            continue
        st, cps = read_trace(trc)
        panel = [int(v) for v in st.get("panel", "0,0,0,0").split(",")]
        w, h, bm = read_bmp24(bmp)
        prod = crop_rgb(bm, w, h, panel[0], panel[1], PANEL, PANEL)
        try:
            viewer = (float(st["viewerPx"]), float(st["viewerPy"]))
        except Exception:
            viewer = (float("nan"), float("nan"))
        bsp = next((os.path.join(r, name + ".bsp") for r in MAPS_DIRS
                    if os.path.exists(os.path.join(r, name + ".bsp"))), None)
        if bsp is None:
            print("SKIP %-20s (no bsp)" % name)
            continue
        model = oracle.read_bsp(bsp)
        prop = flat_to_rows(proposal_canvas(rect, model["floors"], cps, viewer), PANEL)
        gutter = [[PAPER] * GUTTER for _ in range(PANEL)]
        for y in range(PANEL):
            canvas.append(prod[y] + gutter[y] + prop[y])
        if drawn:
            pass
        for _ in range(4):
            canvas.append([PAPER] * width)
        drawn += 1
        print("ROW %-20s floors=%d cps=%d viewer=%.2f,%.2f panel=%s"
              % (name, len(model["floors"]), len(cps), viewer[0], viewer[1], panel))

    if not drawn:
        raise SystemExit("nothing to draw")
    write_png(a.out, canvas, SCALE)
    print("wrote %s %dx%d bytes=%d" % (a.out, width * SCALE, len(canvas) * SCALE,
                                       os.path.getsize(a.out)))


if __name__ == "__main__":
    main()
