import sys, os
sys.path.insert(0, "evidence/t56-map-audit")
import vpk_materials as V
v = V.VPK(os.path.join(V.TF, "tf2_misc_dir.vpk"))
for pat in ["hazardstrip","glasswindow008","measuregeneric","reflectivity_5","metalrail013","metalgrate013","water_2fort_beneath"]:
    hits = [k for k in v.entries if pat in k]
    print(pat, "->", hits[:6])
# also check the download tree
import glob
for pat in ["hazardstrip","glasswindow008","measuregeneric","metalrail013","metalgrate013"]:
    hits = glob.glob(os.path.join(V.TF,"download","materials","**","*"+pat+"*"), recursive=True)
    print("dl", pat, "->", [h.replace("\\","/").split("materials/")[-1] for h in hits[:4]])
