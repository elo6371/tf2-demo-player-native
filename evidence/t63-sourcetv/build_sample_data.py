#!/usr/bin/env python3
"""build_sample_data.py -- freeze the T63 shared demo sample list and the corpus map census.

Inputs (read-only):
  evidence/t56-map-audit/demo_headers_tfdemos.jsonl   1748 demo header rows (all of tf/demos)
  D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/*.bsp
Outputs (written here):
  map_summary.json   per-map counts by recording type + BSP presence (all 146 maps)
  sample_detail.json selected demos: size, full sha256, header-region sha256, header, packet clock
  sample_list.json   the frozen shared list (demos + inspection points + entities)
"""
import hashlib, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import packet_clock

JSONL = os.path.join(REPO, "evidence", "t56-map-audit", "demo_headers_tfdemos.jsonl")
MAPDIR = r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps"
TFDEMOS = r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"
TESTDATA = r"D:/TF2_Demo_Player/testdata/demos"

rows = [json.loads(line) for line in open(JSONL, encoding="utf-8") if line.strip()]
bsps = {n[:-4].lower() for n in os.listdir(MAPDIR) if n.lower().endswith(".bsp")}
map_summary = {}
for r in rows:
    m = r.get("map") or "<none>"
    rec = "SourceTV" if (r.get("client") or "").startswith("SourceTV") else "POV"
    e = map_summary.setdefault(m, {"total": 0, "SourceTV": 0, "POV": 0, "ticks": 0})
    e["total"] += 1; e[rec] += 1; e["ticks"] += int(r.get("ticks") or 0)
for m, e in map_summary.items():
    e["bsp_present"] = m.lower() in bsps
json.dump({"generated_by": "evidence/t63-sourcetv/build_sample_data.py",
           "source_jsonl": "evidence/t56-map-audit/demo_headers_tfdemos.jsonl",
           "demos_total": len(rows), "maps_total": len(map_summary),
           "maps_with_bsp": sum(1 for e in map_summary.values() if e["bsp_present"]),
           "maps": dict(sorted(map_summary.items()))},
          open(os.path.join(HERE, "map_summary.json"), "w"), indent=1)

SELECTED = [
    ("snakewater", os.path.join(TESTDATA, "bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"), 2),
    ("review12", os.path.join(TFDEMOS, "review12.dem"), 2),
    ("ashville73", os.path.join(TFDEMOS, "73.dem"), 2),
    ("prodcutscout", os.path.join(TFDEMOS, "prodcutscout.dem"), 2),
    ("bagel", os.path.join(TESTDATA, "4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"), 1),
    ("sunshine", os.path.join(TFDEMOS, "SUNSHINE.dem"), 2),
]
detail = {}
for name, path, first_tick in SELECTED:
    with open(path, "rb") as f:
        b = f.read()
    hdr = packet_clock.parse_header(b)
    clock = packet_clock.map_demo(path)
    detail[name] = {
        "path": path.replace("\\", "/"),
        "map": hdr["map"], "recording": "SourceTV" if hdr["client"].startswith("SourceTV") else "POV",
        "client": hdr["client"], "server": hdr["server"],
        "demo_ticks_header": hdr["ticks"], "playback_seconds": round(hdr["playback_time"], 2),
        "size_bytes": len(b),
        "sha256_full": hashlib.sha256(b).hexdigest(),
        "sha256_header_1072": hashlib.sha256(b[:1072]).hexdigest(),
        "bsp_present": hdr["map"].lower() in bsps,
        "clock": clock,
    }
json.dump(detail, open(os.path.join(HERE, "sample_detail.json"), "w"), indent=1)
print(json.dumps({k: {kk: vv for kk, vv in v.items() if kk != "clock"} for k, v in detail.items()}, indent=1))
print("clock:", json.dumps({k: {"first_packet_tick": v["clock"]["first_packet_tick"], "first_server_tick": v["clock"]["first_server_tick"], "last_server_tick": v["clock"]["last_server_tick"], "offsets": v["clock"]["server_minus_packet_offsets"], "constant": v["clock"]["offset_constant"]} for k, v in detail.items()}, indent=1))
print("maps:", len(map_summary), "with_bsp:", sum(1 for e in map_summary.values() if e["bsp_present"]))
