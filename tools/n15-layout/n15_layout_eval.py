#!/usr/bin/env python3
"""N15 layout-math evaluation: does the shipped layout rule hold at real window
sizes, and what exactly is the product's DPI story?

WHY A TRANSCRIPTION AND NOT A BUILD
  native/CMakeLists.txt is outside this agent's write domain, so a new probe target
  cannot be added, and any build needs the shared host lock which N12 currently
  holds. What CAN be done offline is: transcribe the three layout functions from
  source, then prove the transcription is faithful two independent ways.

FAITHFULNESS PROOF 1 (aggregate): run the SHIPPED probe's own matrix (6 dpi scales
  x 7 sizes = 42 cases) and reproduce its published readings
  layoutCases=42 layoutOverlap=0 layoutOutOfRange=0 layoutControls=19.

FAITHFULNESS PROOF 2 (per-pixel): two client sizes for which the SHIPPED BINARY
  already published a minimap panel rect must come out of the transcription
  unchanged --
    client 1264x681 -> panel 1092,12,160,160  (seven-map capture traces, 2026-11-15)
    client  784x561 -> panel  612,12,160,160  (--window-size 800x600, D08/G2)
  A transcription that reproduces a rect the binary printed is checkable; one that
  only reproduces its own arithmetic is not.

WHAT IT EVALUATES
  A containment / non-overlap over the matrix (the two things the shipped probe
    already asserts), reproduced independently.
  B the minimap panel rect at each size, plus the exact client size below which
    the panel is DROPPED rather than shrunk.
  C usability, which the shipped probe does NOT assert: a control clamped to a
    zero or sub-grabbable extent is inside the client and useless. Counted.
  D dpi behaviour of the two layers side by side: Win32 controls scale with dpi,
    the HUD minimap panel is pinned in back-buffer pixels and does not.

Sources transcribed (line numbers re-measured at HEAD 6dd853e; the first run of
this file carried main.cpp:1702-1705 from HEAD a901ece and the n12-camera commit
83657f1 shifted that block by +100 lines -- the stale number is fixed here):
  tf2::native::planUiLayout   native/src/native_ui.cpp:573-662
  tf2::native::planHudLayout  native/src/native_ui.cpp:671-714
  scaleDimension              native/src/native_ui.cpp:566-569
  hudLayoutPanels             native/include/hud_panel.h:56-123
  hudMinimapPanelSpec         native/include/hud_panel.h:139-151
  kHudPanelSafeInset          native/include/hud_panel.h:135
"""

import json
import math
import os
import sys

OUT_DIR = "D:/_shared/EXEC-2026-11-15/n15"

# --- transcribed constants ---------------------------------------------------
SAFE_INSET = 8
PANEL_PIXELS = 160
PANEL_MARGIN = 4

# Self-test hooks. Only the self-test sets these.
_DISABLE_CLAMP = False
# The mutation that ACTUALLY breaks containment is not the width clamp (room is
# already bounded by client_w - margin - cx) but the cursor cap that stops a
# squeezed row marching past the right edge.
_DISABLE_CONTAINMENT = False

UI_IDS = ["Status", "Open", "Cancel", "Confirm", "Play", "Stop", "Back",
          "Forward", "Reverse", "Timeline", "CameraMode", "FreeFly",
          "ResetToPlayer", "ResetView", "BookmarkSave", "BookmarkNameEdit",
          "BookmarkDelete", "BookmarkList", "KeybindReset"]
UI_COUNT = len(UI_IDS)
HUD_IDS = ["Import", "Preview", "Scale", "Reset", "Status"]


def scale_dimension(value, dpi):
    # C++ int(v * s + 0.5f) truncates toward zero; every input here is positive.
    return int(float(value) * dpi + 0.5)


def clamp(v, lo, hi):
    if lo > hi:
        lo = hi   # std::clamp is UB when lo > hi; callers never do this, guarded.
    return max(lo, min(v, hi))


def plan_ui_layout(client_w, client_h, dpi):
    """native/src/native_ui.cpp:573-662."""
    rects = {name: {"x": 0, "y": 0, "w": 0, "h": 0} for name in UI_IDS}
    if client_w <= 0 or client_h <= 0 or not (dpi > 0.0) or not math.isfinite(dpi):
        return {"valid": False, "rects": rects}

    margin = scale_dimension(16, dpi)
    gap = scale_dimension(6, dpi)
    usable = max(0, client_w - 2 * margin)

    rows = [
        (["Open", "Cancel", "Confirm"], 48, 30, [120, 90, 90]),
        (["Play", "Stop", "Back", "Forward", "Reverse"], 84, 30, [110, 80, 80, 80, 90]),
        (["Timeline"], 122, 30, [700]),
        (["FreeFly", "ResetToPlayer", "ResetView"], 158, 30, [90, 90, 90]),
        (["BookmarkSave", "BookmarkNameEdit", "BookmarkDelete", "BookmarkList",
          "KeybindReset"], 194, 28, [90, 240, 90, 110, 110]),
    ]

    # CameraMode label: end of the camera row (native_ui.cpp:611-623).
    x = margin
    for _ in range(3):
        x += scale_dimension(90, dpi) + gap
    mode = rects["CameraMode"]
    mode["x"] = min(x, client_w)
    mode["y"] = min(scale_dimension(158, dpi), client_h)
    mode["h"] = min(scale_dimension(30, dpi), max(0, client_h - mode["y"]))
    mode["w"] = max(0, min(scale_dimension(200, dpi),
                           max(0, client_w - margin - mode["x"])))

    for ids, base_y, base_h, widths in rows:
        y = scale_dimension(base_y, dpi)
        h = scale_dimension(base_h, dpi)
        if y > client_h:
            y = client_h
        x = margin
        for index, name in enumerate(ids):
            rect = rects[name]
            wanted = scale_dimension(widths[index], dpi)
            cx = x if _DISABLE_CONTAINMENT else min(x, client_w)
            room = max(0, client_w - margin - cx)
            rect["x"] = cx
            rect["y"] = y
            rect["h"] = min(h, max(0, client_h - y))
            rect["w"] = room if _DISABLE_CLAMP else clamp(wanted, min(24, room), room)
            x = cx + rect["w"] + gap

    status = rects["Status"]
    status["x"] = margin
    status["y"] = scale_dimension(12, dpi)
    if status["y"] > client_h:
        status["y"] = client_h
    status["h"] = min(scale_dimension(28, dpi), max(0, client_h - status["y"]))
    status["w"] = usable
    return {"valid": True, "rects": rects}


def plan_hud_layout(base, client_w, client_h, dpi):
    """native/src/native_ui.cpp:671-714."""
    rects = {name: {"x": 0, "y": 0, "w": 0, "h": 0} for name in HUD_IDS}
    if (not base["valid"] or client_w <= 0 or client_h <= 0
            or not (dpi > 0.0) or not math.isfinite(dpi)):
        return {"valid": False, "rects": rects}

    lowest = 0
    for rect in base["rects"].values():
        bottom = rect["y"] + rect["h"]
        if bottom > lowest:
            lowest = bottom
    margin = scale_dimension(16, dpi)
    gap = scale_dimension(6, dpi)
    y = lowest + gap
    h = scale_dimension(30, dpi)
    if y > client_h:
        y = client_h
    heights = min(h, max(0, client_h - y))
    widths = [scale_dimension(v, dpi) for v in (130, 120, 160, 140, 0)]
    x = margin
    for index, name in enumerate(HUD_IDS):
        rect = rects[name]
        cx = min(x, client_w)
        room = max(0, client_w - margin - cx)
        rect["x"] = cx
        rect["y"] = y
        rect["h"] = heights
        is_status = name == "Status"
        wanted = room if is_status else widths[index]
        rect["w"] = clamp(wanted, min(0 if is_status else 24, room), room)
        x = cx + rect["w"] + gap
    return {"valid": True, "rects": rects}


def hud_layout_panels(specs, client_w, client_h, safe_inset):
    """native/include/hud_panel.h:56-123. specs are (id, anchor, order, w, h, mx, my)."""
    if client_w <= 0 or client_h <= 0:
        return {"panels": [], "dropped": len(specs), "collapsed": 0}
    ordered = sorted(specs, key=lambda s: (s[2], s[0]))
    usable = max(0, client_w - 2 * safe_inset)
    top_left = safe_inset
    top_right = safe_inset
    bottom_left = client_h - safe_inset
    bottom_right = client_h - safe_inset
    out = []
    dropped = 0
    collapsed = 0
    for (pid, anchor, _order, w, h, mx, my) in ordered:
        width = int(w + 0.5)
        height = 0 if pid == "collapsed" else int(h + 0.5)
        width = max(0, width)
        height = max(0, height)
        if width > usable:
            width = usable
        margin_x = int(mx + 0.5)
        margin_y = int(my + 0.5)
        right = anchor in (1, 3)
        bottom = anchor in (2, 3)
        x = (client_w - safe_inset - margin_x - width) if right else (safe_inset + margin_x)
        if x < safe_inset:
            x = safe_inset
        if x + width > client_w - safe_inset:
            x = max(safe_inset, client_w - safe_inset - width)
        if bottom:
            cursor = bottom_right if anchor == 3 else bottom_left
            y = cursor - margin_y - height
            if anchor == 3:
                bottom_right = y
            else:
                bottom_left = y
        else:
            cursor = top_right if anchor == 1 else top_left
            y = cursor + margin_y
            if anchor == 1:
                top_right = y + height
            else:
                top_left = y + height
        if y < safe_inset or y + height > client_h - safe_inset:
            dropped += 1
            continue
        out.append({"id": pid, "x": x, "y": y, "w": width, "h": height})
    return {"panels": out, "dropped": dropped, "collapsed": collapsed}


def minimap_rect(client_w, client_h):
    """The panel the product actually draws: hudMinimapPanelSpec through
    hudLayoutPanels, exactly the call at native/src/main_hud.cpp:926-939."""
    specs = [("minimap", 1, 0, PANEL_PIXELS, PANEL_PIXELS, PANEL_MARGIN, PANEL_MARGIN)]
    layout = hud_layout_panels(specs, client_w, client_h, SAFE_INSET)
    if not layout["panels"]:
        return None
    return layout["panels"][0]


# --- checks ------------------------------------------------------------------
def overlaps(a, b):
    # native/src/native_ui.cpp:664-669 -- touching edges count as overlapping.
    return a["x"] < b["x"] + b["w"] and b["x"] < a["x"] + a["w"] \
        and a["y"] < b["y"] + b["h"] and b["y"] < a["y"] + a["h"]


def check_matrix():
    """The shipped probe's own 42-case matrix (tools/native_ui_probe.cpp:204-257)."""
    scales = [1.0, 1.25, 1.5, 2.0, 3.0, 5.0]
    sizes = [(320, 240), (640, 480), (800, 600), (1280, 720), (1920, 1080),
             (2560, 1440), (3840, 2160)]
    cases = out_of_range = overlap = status_clamped = 0
    for dpi in scales:
        for (w, h) in sizes:
            plan = plan_ui_layout(w, h, dpi)
            cases += 1
            if not plan["valid"]:
                out_of_range += 1
                continue
            names = UI_IDS
            for i, name in enumerate(names):
                rect = plan["rects"][name]
                if (rect["x"] < 0 or rect["y"] < 0
                        or rect["x"] + rect["w"] > w or rect["y"] + rect["h"] > h):
                    out_of_range += 1
                for other in names[i + 1:]:
                    if overlaps(rect, plan["rects"][other]):
                        overlap += 1
            status = plan["rects"]["Status"]
            margin = int(16.0 * dpi + 0.5)
            if status["w"] > max(0, w - 2 * margin):
                status_clamped += 1
    return {"layoutCases": cases, "layoutOverlap": overlap,
            "layoutOutOfRange": out_of_range, "statusClamped": status_clamped,
            "layoutControls": UI_COUNT}


def check_oracle_anchors():
    """Two rects the SHIPPED BINARY printed, recomputed from the transcription."""
    anchors = [
        (1264, 681, (1092, 12, 160, 160), "seven-map capture traces 2026-11-15"),
        (784, 561, (612, 12, 160, 160), "--window-size 800x600 (D08/G2)"),
    ]
    out = []
    for (cw, ch, want, source) in anchors:
        rect = minimap_rect(cw, ch)
        got = None if rect is None else (rect["x"], rect["y"], rect["w"], rect["h"])
        out.append({"client": [cw, ch], "want": list(want),
                    "got": None if got is None else list(got),
                    "match": got == want, "source": source})
    return out


def check_sizes():
    """Per-size evaluation with decidable readings."""
    sizes = [(320, 240), (640, 480), (800, 600), (1024, 640), (1280, 720),
             (1366, 768), (1920, 1080), (2560, 1440), (3840, 2160)]
    rows = []
    for (cw, ch) in sizes:
        for dpi in (1.0, 1.5, 2.0):
            plan = plan_ui_layout(cw, ch, dpi)
            hud = plan_hud_layout(plan, cw, ch, dpi)
            panel = minimap_rect(cw, ch)
            small = []
            for name in UI_IDS + HUD_IDS:
                src = plan["rects"] if name in UI_IDS else hud["rects"]
                rect = src[name]
                if rect["w"] < 24 or rect["h"] < 24:
                    small.append(name)
            contained = True
            for name in UI_IDS:
                r = plan["rects"][name]
                if (r["x"] < 0 or r["y"] < 0 or r["x"] + r["w"] > cw
                        or r["y"] + r["h"] > ch):
                    contained = False
            row = {
                "client": [cw, ch], "dpi": dpi,
                "panel": None if panel is None else
                         [panel["x"], panel["y"], panel["w"], panel["h"]],
                "panel_placed": panel is not None,
                "controls_contained": contained,
                "controls_small": small,
                "small_count": len(small),
                "hud_row_y": hud["rects"]["Import"]["y"],
                "hud_row_h": hud["rects"]["Import"]["h"],
                "timeline_w": plan["rects"]["Timeline"]["w"],
                "keybind_w": plan["rects"]["KeybindReset"]["w"],
                "status_w": plan["rects"]["Status"]["w"],
            }
            rows.append(row)
    return rows


def find_panel_drop_threshold():
    """Below what client size does the panel vanish instead of shrinking?"""
    # Search from 1: starting at 150 found 150 and reported the bound rather than
    # the threshold -- a search floor that can never lose is not a measurement.
    last_ok_w = None
    for cw in range(1, 400):
        if minimap_rect(cw, 720) is not None:
            last_ok_w = cw
            break
    last_ok_h = None
    for ch in range(1, 400):
        if minimap_rect(1280, ch) is not None:
            last_ok_h = ch
            break
    # FINDING, not a threshold: hudLayoutPanels drops a panel on the Y axis only
    # (hud_panel.h:114). On X it clamps x UP to safeInset, so a client narrower than
    # 2*safeInset gets a panel PLACED at x=8 with width 0 -- vertically fine,
    # horizontally outside the client, and never counted as dropped.
    narrow = []
    for cw in (1, 8, 12, 16, 17, 19, 20, 24, 40):
        rect = minimap_rect(cw, 720)
        narrow.append({
            "client_w": cw,
            "panel": None if rect is None else [rect["x"], rect["y"], rect["w"], rect["h"]],
            "placed": rect is not None,
            "x_outside_client": bool(rect and (rect["x"] < 0 or rect["x"] > cw)),
        })
    return {"min_client_width_placed": last_ok_w,
            "min_client_height_placed": last_ok_h,
            "drop_reason": "panel-not-placed (main_hud.cpp:970-973)",
            "behaviour": "the panel is DROPPED, never shrunk",
            "narrow_client_sweep": narrow,
            "narrow_client_note": "the drop rule tests y only, so below client_w "
                                  "2*safe_inset the panel is placed with width 0 at "
                                  "x=safe_inset: placed, useless, outside the client"}


def check_panel_vs_controls():
    """The question the layout report has to answer: does the 160-px minimap panel
    (drawn into the HUD pass) collide with a Win32 control (drawn on top of it)?

    The two live in different coordinate systems that happen to agree -- both are
    back-buffer pixels from the client's top-left -- so an overlap is a real
    occlusion, not a coordinate mistake. Windows are given as OUTER sizes because
    that is what --window-size takes; client = outer - 16 x, - 39 y, measured twice
    on this host (1280x720 -> 1264x681, 800x600 -> 784x561)."""
    outer_sizes = [(320, 240), (640, 480), (800, 600), (1024, 640), (1280, 720),
                   (1366, 768), (1920, 1080), (2560, 1440), (3840, 2160)]
    rows = []
    for (ow, oh) in outer_sizes:
        cw, ch = ow - 16, oh - 39
        for dpi in (1.0, 1.25, 1.5, 2.0):
            panel = minimap_rect(cw, ch)
            if panel is None:
                rows.append({"outer": [ow, oh], "client": [cw, ch], "dpi": dpi,
                             "panel": None, "overlaps": ["PANEL-NOT-PLACED"],
                             "panel_area_pct": 0.0})
                continue
            plan = plan_ui_layout(cw, ch, dpi)
            hud = plan_hud_layout(plan, cw, ch, dpi)
            hits = []
            for name in UI_IDS + HUD_IDS:
                src = plan["rects"] if name in UI_IDS else hud["rects"]
                rect = src[name]
                if overlaps(panel, rect):
                    hits.append(name)
            rows.append({
                "outer": [ow, oh], "client": [cw, ch], "dpi": dpi,
                "panel": [panel["x"], panel["y"], panel["w"], panel["h"]],
                "overlaps": hits,
                "panel_area_pct": round(100.0 * panel["w"] * panel["h"] / (cw * ch), 2),
            })
    return rows


def dpi_split():
    """How dpi actually reaches the product -- three separate questions."""
    return {
        "q1_awareness_declared": {
            "answer": "YES",
            "evidence": "native/src/main.cpp:1802-1805 SetProcessDpiAwarenessContext("
                        "DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) with a "
                        "SetProcessDPIAware() fallback",
            "note": "no .manifest file and no dpiAware manifest entry anywhere in the "
                    "tree (grep over *.txt and native/CMakeLists.txt: 0 hits), so the "
                    "process declares awareness by API call, not by manifest",
        },
        "q2_layout_scaled_by_dpi": {
            "answer": "YES for Win32 controls, NO for HUD primitives",
            "evidence_controls": "native/src/main_ui.cpp:41-47 passes "
                                 "GetDpiForWindow(window)/96 into planUiLayout; "
                                 "scaleDimension multiplies every base number "
                                 "(native/src/native_ui.cpp:566-569)",
            "evidence_hud": "native/include/hud_panel.h:131-134 pins the minimap panel "
                            "in BACK-BUFFER PIXELS on purpose so a dpi change cannot "
                            "move a rect a pixel gate hashes",
            "consequence": "at 200% dpi the panel keeps 160 px while every Win32 "
                           "control doubles its footprint",
        },
        "q3_testable_without_changing_display_settings": {
            "answer": "NO for the real WM_DPICHANGED path",
            "evidence": "no --dpi/--dpi-scale flag exists (grep over native/: 0 hits); "
                        "the dpi scale comes only from GetDpiForWindow",
            "what_IS_offline_testable": "planUiLayout(dpiScale) as a pure function, "
                                        "which is what this tool does",
        },
    }


def selfcheck():
    """Mutate the rules and require the checks to notice. A checker that cannot go
    red is not a checker."""
    global SAFE_INSET
    report = []

    anchors = check_oracle_anchors()
    report.append(("anchors_pass_unmutated", all(a["match"] for a in anchors)))

    saved = SAFE_INSET
    SAFE_INSET = 2
    mutated = check_oracle_anchors()
    report.append(("anchor_detects_inset_change", not all(a["match"] for a in mutated)))
    SAFE_INSET = saved

    global PANEL_PIXELS
    saved_px = PANEL_PIXELS
    PANEL_PIXELS = 200
    mutated = check_oracle_anchors()
    report.append(("anchor_detects_panel_size_change",
                   not all(a["match"] for a in mutated)))
    PANEL_PIXELS = saved_px

    # A containment break must be visible. The FIRST version of this check asserted
    # that an UNMUTATED plan at 320x240/500% already pokes outside the client, and it
    # failed -- correctly, because the planner clamps. Asserting a property of the
    # unmutated code is not a sensitivity test; it either passes for the wrong reason
    # or fails for one.
    global _DISABLE_CLAMP, _DISABLE_CONTAINMENT
    _DISABLE_CLAMP = True
    clamp_matrix = check_matrix()
    _DISABLE_CLAMP = False
    # FOUND WHILE FIXING THE ABOVE: removing the width clamp alone changes nothing,
    # because room is already bounded, so that mutation stayed green and looked like
    # a broken checker when the checker was fine. The cursor cap is what contains.
    _DISABLE_CONTAINMENT = True
    contain_matrix = check_matrix()
    _DISABLE_CONTAINMENT = False
    report.append(("matrix_detects_removed_cursor_cap",
                   contain_matrix["layoutOutOfRange"] > 0))

    ok = all(v for _n, v in report)
    return {
        "pass": ok,
        "checks": [{"name": n, "ok": v} for n, v in report],
        # Recorded, not required: this mutation does NOT break containment, and
        # saying so here is the point. It is listed so the next reader does not
        # "fix" the checker when the checker was right.
        "mutations_recorded_not_required": [{
            "mutation": "removed width clamp (rect.w = room)",
            "out_of_range_hits": clamp_matrix["layoutOutOfRange"],
            "reading": "0 = this mutation cannot break containment, because room is "
                       "already client_w - margin - cx. Keeping it as a required "
                       "check would have meant a permanent red that says nothing "
                       "about the product.",
        }],
    }


def main():
    result = {
        "source_transcribed_from": {
            "planUiLayout": "native/src/native_ui.cpp:573-662",
            "planHudLayout": "native/src/native_ui.cpp:671-714",
            "scaleDimension": "native/src/native_ui.cpp:566-569",
            "hudLayoutPanels": "native/include/hud_panel.h:56-123",
            "hudMinimapPanelSpec": "native/include/hud_panel.h:139-151",
        },
        "matrix_reproduction": check_matrix(),
        "oracle_anchors": check_oracle_anchors(),
        "panel_drop_threshold": find_panel_drop_threshold(),
        "dpi_split": dpi_split(),
        "sizes": check_sizes(),
        "selfcheck": selfcheck(),
    }
    m = result["matrix_reproduction"]
    result["matrix_verdict"] = (
        "PASS" if m["layoutCases"] == 42 and m["layoutOverlap"] == 0
        and m["layoutOutOfRange"] == 0 and m["statusClamped"] == 0
        and m["layoutControls"] == 19 else "FAIL")
    result["anchor_verdict"] = ("PASS" if all(a["match"] for a in result["oracle_anchors"])
                                else "FAIL")
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "n15-layout-eval.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print("matrix:", result["matrix_verdict"], m)
    print("anchors:", result["anchor_verdict"])
    for a in result["oracle_anchors"]:
        print("  client", a["client"], "want", a["want"], "got", a["got"], "->",
              "OK" if a["match"] else "MISMATCH")
    print("selfcheck:", "PASS" if result["selfcheck"]["pass"] else "FAIL",
          [c["name"] for c in result["selfcheck"]["checks"] if not c["ok"]])
    print("panel drop threshold:", result["panel_drop_threshold"])
    print("wrote", path)
    return 0 if (result["matrix_verdict"] == "PASS" and result["anchor_verdict"] == "PASS"
                 and result["selfcheck"]["pass"]) else 1


if __name__ == "__main__":
    sys.exit(main())
