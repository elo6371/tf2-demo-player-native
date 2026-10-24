#!/usr/bin/env python3
"""Turn a product face-trace into per-item ledger rows, and refuse bad samples.

Static analysis only: reads trace files, never launches anything. The rejection
rules are the point: a trace that is missing, names a different map, carries no
world camera eye, or has no face-trace rows must not produce ledger rows.

Reality check on the trace format (evidence/world-material-face/trace-albedo.txt):
  - 'map: lookup map=<m> store=<s> selected=<p> bytes=<n> reason=found'
  - 'capture: armed requested=<t> frame_tick=<t> frames=<n>'
  - 'scene: camera mode=first-person target=1 follows=<n> holds=0 freeze=0'
      -> mode/target/follows/holds/freeze ONLY. No pitch/yaw anywhere.
  - 'gpu: world vis cameraCluster=<c> ... eyeMap=(x,y,z) ...'
      -> the only world-space camera eye in the trace.
  - 'gpu: world face-trace point=<x>,<y> tri=<id|none> material=...'
Usage:
  python face_trace_to_ledger.py --trace T --map NAME [--tick N] [--out TSV]
  python face_trace_to_ledger.py --selftest
"""
import argparse, json, os, re, sys

COLS = [
        "map_identity","demo_path","capture_tick","trace_sha256","frame_sha256",
        "camera_eye_x","camera_eye_y","camera_eye_z","camera_mode","camera_freeze",
        "camera_pitch","camera_yaw","point_x","point_y","region_id",
        "face_id","material","vmt_path","vmt_store","vmt_sha256",
        "vtf_path","vtf_store","vtf_sha256","uv_size","uv",
        "uv_inside","atlas_tile","atlas_tile_size","atlas_tile_rect","atlas_size",
        "texel","blend","lightmap_uv","lightmap_page","light_color",
        "albedo","albedo_tile","expected","refusal_reason","beyond_old_cap",
        "old_order","depth","frame_depth","final_depth","depth_match",
        "visibility_state","cull_state","delta_vs_reference","issue_class","owner",
        "next_step",
]

FIELDS = {"tri":"face_id","material":"material","vmt":"vmt_path","vmtStore":"vmt_store",
          "vtf":"vtf_path","vtfStore":"vtf_store","src":"uv_size","uv":"uv","uvInside":"uv_inside",
          "tile":"atlas_tile","size":"atlas_tile_size","rect":"atlas_tile_rect","atlas":"atlas_size",
          "texel":"texel","blend":"blend","luv":"lightmap_uv","light":"light_color",
          "albedo":"albedo","albedoTile":"albedo_tile","expected":"expected",
          "reason":"refusal_reason","beyondOldCap":"beyond_old_cap","oldOrder":"old_order",
          "depth":"depth","frameDepth":"frame_depth","finalDepth":"final_depth","depthMatch":"depth_match"}

def parse_face_lines(text):
    rows = []
    for line in text.splitlines():
        if "gpu: world face-trace" not in line: continue
        rec = {}
        for tok in line.split("gpu: world face-trace",1)[1].split():
            if "=" not in tok: continue
            k,v = tok.split("=",1)
            if k=="point" and "," in v:
                rec["point_x"],rec["point_y"] = v.split(",",1); continue
            if k in FIELDS: rec[FIELDS[k]] = v
        rows.append(rec)
    return rows

def sha256(path):
    import hashlib
    h=hashlib.sha256()
    with open(path,"rb") as f:
        for b in iter(lambda:f.read(1<<20),b""): h.update(b)
    return h.hexdigest()

def region_of(x,y):
    try: return "r%dx%d" % (int(x)//100, int(y)//100)
    except (TypeError,ValueError): return "unknown"

def identity(text, expected_map, expected_tick):
    reasons=[]; facts={}
    m = re.search(r"map: lookup map=(\S+) store=(\S+) selected=(\S+) bytes=(\d+) reason=(\S+)", text)
    if not m:
        reasons.append("missing map-lookup line (cannot prove which map was loaded)")
    else:
        facts.update(map=m.group(1),store=m.group(2),bsp=m.group(3),bsp_bytes=int(m.group(4)))
        if m.group(1)!=expected_map: reasons.append("wrong map: loaded %s expected %s"%(m.group(1),expected_map))
        if m.group(5)!="found": reasons.append("map not found: reason=%s"%m.group(5))
    a = re.search(r"capture: armed requested=(-?\d+) frame_tick=(-?\d+)", text)
    if not a: reasons.append("missing capture-armed line (no proof the requested frame was reached)")
    else:
        facts["requested_tick"],facts["frame_tick"]=int(a.group(1)),int(a.group(2))
        if expected_tick is not None and facts["frame_tick"]!=expected_tick:
            reasons.append("wrong tick: frame_tick=%d expected %d"%(facts["frame_tick"],expected_tick))
    c = re.search(r"scene: camera mode=(\S+) target=(\S+) follows=(\d+) holds=(\d+) freeze=(\d+)", text)
    if not c: reasons.append("missing scene-camera line (no follow/freeze state)")
    else: facts.update(camera_mode=c.group(1),camera_freeze=int(c.group(5)))
    e = re.search(r"gpu: world vis\b.*?eyeMap=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)", text)
    if not e:
        reasons.append("missing world-vis eyeMap (no world camera eye in the trace)")
    else:
        facts["camera_eye"]=[float(e.group(1)),float(e.group(2)),float(e.group(3))]
        if facts["camera_eye"]==[0.0,0.0,0.0]:
            reasons.append("wrong pose: eyeMap is (0,0,0)")
    if not parse_face_lines(text):
        reasons.append("missing face-trace rows (nothing to build a ledger from)")
    return (not reasons), reasons, facts

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--trace"); ap.add_argument("--map"); ap.add_argument("--tick",type=int)
    ap.add_argument("--demo",default=""); ap.add_argument("--frame",default="")
    ap.add_argument("--out"); ap.add_argument("--selftest",action="store_true")
    a=ap.parse_args()
    if a.selftest: return selftest()
    text=open(a.trace,encoding="utf-8",errors="replace").read()
    ok,reasons,facts=identity(text,a.map,a.tick)
    print(json.dumps({"accept":ok,"reasons":reasons,"facts":facts},ensure_ascii=False,indent=1))
    if not ok:
        print("REFUSED: no ledger rows written",file=sys.stderr); return 2
    rows=parse_face_lines(text)
    for r in rows:
        r.setdefault("region_id",region_of(r.get("point_x"),r.get("point_y")))
        r["map_identity"]="%s@%s"%(facts.get("map"),facts.get("bsp_bytes"))
        r["visibility_state"]="from gpu: world vis line (cameraCluster/clipApplied)"
        r["cull_state"]="unknown (no --no-world-cull control in this trace)"
        for k,v in (("issue_class",""),("owner",""),("next_step","")):
            r.setdefault(k,v)
        r["camera_eye_x"],r["camera_eye_y"],r["camera_eye_z"]=facts.get("camera_eye",["","",""])
        r["camera_mode"],r["camera_freeze"]=facts.get("camera_mode",""),facts.get("camera_freeze","")
        r["camera_pitch"]=""  # NOT EMITTED by the product (documented gap)
        r["camera_yaw"]=""    # NOT EMITTED by the product (documented gap)
        r["demo_path"]=a.demo
        r["capture_tick"]=facts.get("frame_tick","")
        r["trace_sha256"]=sha256(a.trace)
        r["frame_sha256"]=sha256(a.frame) if a.frame else ""
    if a.out:
        with open(a.out,"w",encoding="utf-8",newline="") as f:
            f.write("\t".join(COLS)+"\n")
            for r in rows: f.write("\t".join(str(r.get(c,"")) for c in COLS)+"\n")
        print("wrote %s (%d rows)"%(a.out,len(rows)))
    else:
        print("\t".join(COLS))
        for r in rows[:3]: print("\t".join(str(r.get(c,"")) for c in COLS))
    return 0

def selftest():
    here=os.path.dirname(os.path.abspath(__file__))
    good=os.path.join(here,"fixtures","positive_odyssey_trace.txt")
    text=open(good,encoding="utf-8",errors="replace").read()
    cases=[("positive",text,"pl_odyssey",2000,True),
           ("missing-trace","","pl_odyssey",2000,False),
           ("wrong-map",text.replace("map=pl_odyssey","map=cp_snakewater_final1"),"pl_odyssey",2000,False),
           ("wrong-pose",text.replace("eyeMap=(-2423.6,5566.0,-1280.0)","eyeMap=(0.0,0.0,0.0)"),"pl_odyssey",2000,False)]
    rc=0
    for name,body,m,t,want in cases:
        ok,reasons,facts=identity(body,m,t)
        verdict="PASS" if ok==want else "FAIL"
        if ok!=want: rc=1
        print("%-14s accept=%-5s want=%-5s %s reasons=%s"%(name,ok,want,verdict,"; ".join(reasons) if reasons else "none"))
    return rc

if __name__=="__main__":
    sys.exit(main())
