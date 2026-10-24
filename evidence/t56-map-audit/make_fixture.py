src="D:/TF2_Native_Test/evidence/world-material-face/trace-albedo.txt"
lines=open(src,encoding="utf-8",errors="replace").read().splitlines()
keep=[l for l in lines if "map: lookup" in l or "capture: armed" in l]
keep.append([l for l in lines if "scene: camera" in l][0])
keep.append([l for l in lines if "gpu: world vis" in l][0])
keep.extend([l for l in lines if "gpu: world face-trace" in l][:3])
open("D:/TF2_Native_Test/evidence/t56-map-audit/fixtures/positive_odyssey_trace.txt","w",encoding="utf-8").write("\n".join(keep)+"\n")
print("fixture lines:",len(keep))
