import json
from PIL import Image
base = "D:/TF2_Native_Test/evidence/t65-minimap/"
d = json.load(open(base+"data/projection_ledger.json"))
ok = True
for name, v in d.items():
    img = Image.open(base+"data/%s.preview.png" % name).convert("RGB")
    for cp in v["controlPoints"]:
        x, y = cp["px"]
        pix = img.getpixel((int(round(x)), int(round(y))))
        good = pix == (255, 215, 0)
        ok = ok and good
        print(name, cp["name"], "px=(%.2f,%.2f)" % (x, y), "pixel=", pix, "yellow" if good else "MISMATCH")
    for t, sp in v["teamSpawnCentroids"].items():
        x, y = sp["px"]
        pix = img.getpixel((int(round(x)), int(round(y))))
        want = (225, 70, 70) if t == "2" else (80, 140, 255)
        good = pix == want
        ok = ok and good
        print(name, "team", t, "centroid px=(%.2f,%.2f)" % (x, y), "pixel=", pix, "ok" if good else "MISMATCH want=%s" % (want,))
    for e, ev in v.get("extra", {}).items():
        if "px" in ev:
            x, y = ev["px"]
            pix = img.getpixel((int(round(x)), int(round(y))))
            print(name, "extra", e, "px=(%.2f,%.2f)" % (x, y), "pixel=", pix)
print("ALL_MARKERS_OK" if ok else "MARKER_MISMATCH")
