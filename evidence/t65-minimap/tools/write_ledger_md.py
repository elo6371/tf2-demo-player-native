import json, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
data = json.load(open(os.path.join(ROOT, "data", "projection_ledger.json")))
out = []
def w(s=""): out.append(s)

w("# T65 — projection ledger (world → minimap pixel)")
w()
w("Generated from data/projection_ledger.json by tools/write_ledger_md.py.")
w("Do not hand-edit numbers; regenerate instead. Canvas is 1024x1024, equal aspect, 2% padding.")
w()
w("## 1. Formula")
w()
w("    px = (x - minX) * scale")
w("    py = (maxY - y) * scale          # y is flipped: +Y world points up on screen")
w("    x  = minX + px / scale           # inverse")
w("    y  = maxY - py / scale")
w()
w("The frame is derived, not hard-coded: frame = intersect(gameplay-entity bbox + 1536 units margin, BSP floor extent).")
w("Raw worldModel mins/maxs are NOT usable (they include the 3D skybox; see REPORT.md).")
w()
w("## 2. Constants")
w()
w("| map | minX | maxX | minY | maxY | scale (px/unit) | units/px | frame x | frame y | cells outside frame | cells outside canvas |")
w("|---|---|---|---|---|---|---|---|---|---|---|")
for name, m in data.items():
    p = m["projection"]
    w("| %s | %.3f | %.3f | %.3f | %.3f | %.10f | %.4f | [%s, %s] | [%s, %s] | %d | %d |" % (
        name, p["minX"], p["maxX"], p["minY"], p["maxY"], p["scalePxPerUnit"], p["unitsPerPixel"],
        ("%g" % p["frameUnits"][0]), ("%g" % p["frameUnits"][1]),
        ("%g" % p["frameUnits"][2]), ("%g" % p["frameUnits"][3]),
        p["outsideFrameCells"], p["outsideCanvasCells"]))
w()
for name, m in data.items():
    w("## 3. %s — control points" % name)
    w()
    w("BSP: tf/maps/%s.bsp  bytes=%d  sha256=%s" % (name, m["identity"]["bytes"], m["identity"]["sha256"]))
    w()
    w("| name | world x | world y | world z | px | py |")
    w("|---|---|---|---|---|---|")
    for cp in m["controlPoints"]:
        o = cp["world"]
        w("| %s | %.3f | %.3f | %.3f | %.2f | %.2f |" % (cp["name"], o[0], o[1], o[2], cp["px"][0], cp["px"][1]))
    w()
    w("## 4. %s — team spawn centroids (in-band only)" % name)
    w()
    w("| team | n | centroid world | px | bbox world (xmin,xmax,ymin,ymax) |")
    w("|---|---|---|---|---|")
    for team in sorted(m["teamSpawnCentroids"]):
        t = m["teamSpawnCentroids"][team]
        c = t["centroid"]; b = t["bbox"]
        w("| %s | %d | (%.2f, %.2f, %.2f) | (%.2f, %.2f) | (%.0f, %.0f, %.0f, %.0f) |" % (
            team, t["count"], c[0], c[1], c[2], t["px"][0], t["px"][1], b[0], b[1], b[2], b[3]))
    w()
    extra = m.get("extra") or {}
    if extra:
        w("Extra entities:")
        w()
        for key, val in extra.items():
            w("- %s: %s" % (key, json.dumps(val, ensure_ascii=False)))
        w()
    w("## 5. %s — error budget" % name)
    w()
    w("| item | value |")
    w("|---|---|")
    w("| round-trip max abs error (control points, float64) | %.3e world units |" % m["errorBudget"]["roundTripMaxAbsErrorWorldUnits"])
    w("| half-pixel quantization at 1024 canvas | %.4f world units |" % m["errorBudget"]["halfPixelWorldUnits"])
    w("| pixel rounding | +/- 0.5 px |")
    w()
    g = m["geometry"]
    w("## 6. %s — geometry summary" % name)
    w()
    w("| item | value |")
    w("|---|---|")
    w("| faceCount / floorFaces / vertexCount | %d / %d / %d |" % (g["faceCount"], g["floorFaces"], g["vertexCount"]))
    w("| entityCount | %d |" % g["entityCount"])
    pb = g["playableBounds"]
    w("| playableBounds (floor cell extent) | x[%g, %g] y[%g, %g] |" % (pb["minX"], pb["maxX"], pb["minY"], pb["maxY"]))
    w("| zRange | [%g, %g] |" % tuple(g["zRange"]))
    w("| zBands (2/20/40/60/80/98%%) | %s |" % json.dumps([round(v, 2) for v in g["zBands"]]))
    w("| cellCount / multiLevelCells | %d / %d |" % (g["cellCount"], g["multiLevelCells"]))
    w("| levelHistogram | %s |" % json.dumps(g["levelHistogram"]))
    w("| worldModel mins / maxs (NOT for framing) | %s / %s |" % (g["worldModel"]["mins"], g["worldModel"]["maxs"]))
    w("| zFilter band / inband points / dropped | %s / %d of %d / %s |" % (
        g["zFilter"]["band"], g["zFilter"]["inbandPoints"], g["zFilter"]["totalPoints"],
        json.dumps(g["zFilter"]["dropped"], ensure_ascii=False)))
    w()
w("## 7. Clipped cells and the off-map rule")
w()
w("Cells counted in outsideFrameCells/outsideCanvasCells are floor cells that fall outside the playable frame;")
w("they are not drawn. If a player ever projects outside [0,1024], the UI must clamp the dot to the canvas edge")
w("and show an off-map indicator — never draw a dot at a wrapped or guessed position.")
w()
w("## 8. Multi-level policy")
w()
w("Both maps have large multi-level fractions (snakewater 16789/57812 = 29.0%, badwater 10621/43601 = 24.4%).")
w("A dot must be assigned to the layer nearest the player's rendered z; when a cell has >=2 layers the UI shows a")
w("stack-count badge (e.g. '2') instead of silently picking one. Never draw two players at the same pixel without")
w("indicating overlap.")
w()
w("## 9. Runtime contract (for the implementer)")
w()
w("1. Derive the frame at map load from the same rule (gameplay entity bbox + 1536 margin, intersect floor extent).")
w("2. Print the derived minX/maxX/minY/maxY/scale into the log and compare with this ledger; a mismatch means a")
w("   different entity set or map version and must be recorded as a new identity, not ignored.")
w("3. Use double precision internally; round only at draw time.")
w("4. Do not use BspMap worldModel mins/maxs for framing (skybox).")
w("5. Clamp out-of-canvas projections to the edge with an indicator (section 7).")
open(os.path.join(ROOT, "projection_ledger.md"), "w", encoding="utf-8").write("\n".join(out) + "\n")
print("wrote projection_ledger.md lines=%d" % len(out))
