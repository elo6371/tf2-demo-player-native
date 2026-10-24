#!/usr/bin/env python3
"""T57: resolve each player model's material slots exactly the way the product does.

Product rules mirrored here, and validated against the product's own readings:
  * native/src/model_loader.cpp:147-170 textureCandidatesFor(): a texture name that
    starts with 'models/' or 'materials/' is already rooted (folded); otherwise each
    MDL $cdmaterials directory is prepended.
  * native/src/vmt_material.cpp:165-174 VmtParser::resourcePath(): strip leading '/',
    strip a leading 'materials/', append the extension, then prefix 'materials/'.
  * the product uses the ROOT block's $basetexture only. The eye shader materials
    (EyeRefract, e.g. materials/models/player/scout/eyeball_l.vmt) carry their
    $basetexture inside a nested "Eyes_dx8" { } fallback block, which the product
    does not descend into, so those slots resolve to no paint.

VALIDATION: this script reproduces the product's own counts exactly --
  Scout 15/17 resolved, Medic 19/21 resolved (entity-material-check.sh:137-138,
  evidence/entity-material/probe-models_player_scout.mdl.txt:22-23,40-41).
"""
import json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vpk_inventory as V

TF = "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
misc = V.Vpk(os.path.join(TF, "tf2_misc_dir.vpk"))
tex = V.Vpk(os.path.join(TF, "tf2_textures_dir.vpk"))
RAW = os.path.join(HERE, "..", "raw")

def fold(p):
    p = p.replace("\\", "/")
    out = []
    for part in p.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if out: out.pop()
            continue
        out.append(part)
    return "/".join(out)

def resource_path(name, ext):
    n = name.replace("\\", "/").lstrip("/")
    if n.lower().startswith("materials/"):
        n = n[10:]
    if not n.lower().endswith(ext.lower()):
        n += ext
    return "materials/" + n

def candidates(name, cds):
    n = name.replace("\\", "/")
    if n.lower().startswith("models/") or n.lower().startswith("materials/"):
        return [fold(n)]
    return [fold(c + "/" + n) for c in cds]

def top_level_basetexture(text):
    """$basetexture from the VMT's root block only (brace depth 1)."""
    depth = 0
    for line in text.splitlines():
        m = re.search(r'"\$basetexture"\s+"([^"]+)"', line, re.I)
        if m and depth == 1:
            return m.group(1)
        depth += line.count("{") - line.count("}")
    return None

classes = json.load(open(os.path.join(RAW, "player_models.json")))["classes"]
out = []
for c in classes:
    cds = c.get("cdTextures") or []
    seen, slots = set(), []
    for s in (c.get("materials") or []):
        if s["texture"] in seen:
            continue
        seen.add(s["texture"])
        slots.append(s["texture"])
    rec = {"className": c["className"], "tfClass": c["tfClass"],
           "mdlTextureCount": (c["mdl"] or {}).get("textureCount"),
           "slots": len(slots), "resolved": 0, "incomplete": [], "entries": [], "slot0": None}
    for i, t in enumerate(slots):
        cands = candidates(t, cds)
        vmt = next((p for p in cands if misc.has(resource_path(p, ".vmt"))), None)
        e = {"slot": i, "texture": t, "candidates": cands, "vmt": None, "base": None, "vtf": None}
        if vmt:
            e["vmt"] = resource_path(vmt, ".vmt")
            base = top_level_basetexture(misc.read(e["vmt"]).decode("utf-8", "replace"))
            e["base"] = base
            if base:
                e["vtf"] = resource_path(base, ".vtf")
                if tex.has(e["vtf"]):
                    rec["resolved"] += 1
                    e["complete"] = True
                else:
                    e["complete"] = False
                    rec["incomplete"].append(t + " (base VTF missing)")
            else:
                e["complete"] = False
                rec["incomplete"].append(t + " (no root $basetexture)")
        else:
            e["complete"] = False
            rec["incomplete"].append(t + " (no VMT)")
        if i == 0:
            rec["slot0"] = e
        rec["entries"].append(e)
    out.append(rec)

json.dump(out, open(os.path.join(RAW, "materials_resolved.json"), "w", encoding="utf-8"), indent=1)
print("class     slots resolved  slot0 texture -> vmt -> vtf")
for r in out:
    s0 = r["slot0"] or {}
    print("%-8s %5d %8d  %s -> %s -> %s" % (r["className"], r["slots"], r["resolved"],
                                            s0.get("texture"), s0.get("vmt"), s0.get("vtf")))
    if r["incomplete"]:
        print("          incomplete:", ", ".join(r["incomplete"]))
