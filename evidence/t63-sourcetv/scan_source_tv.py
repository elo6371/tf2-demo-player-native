import os, struct, json, sys, hashlib

DEMOS = r"D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos"
OUT = r"D:\TF2_Native_Test\evidence\t63-sourcetv\corpus_scan.json"

def parse_header(path):
    with open(path, "rb") as f:
        b = f.read(1072)
    if len(b) < 1072:
        return None
    magic = b[:8]
    dp, np_ = struct.unpack_from("<ii", b, 8)
    def s(off):
        raw = b[off:off+260]
        return raw.split(b"\x00",1)[0].decode("utf-8", "replace")
    server = s(16); client = s(276); mapn = s(536); gamedir = s(796)
    playback = struct.unpack_from("<f", b, 1056)[0]
    ticks, frames, signon = struct.unpack_from("<iii", b, 1060)
    return dict(magic=magic.decode("latin1"), demoProtocol=dp, netProtocol=np_,
                server=server, client=client, map=mapn, gameDir=gamedir,
                playbackSec=round(playback,2), ticks=ticks, frames=frames, signonLength=signon)

rows = []
for name in sorted(os.listdir(DEMOS)):
    if not name.lower().endswith(".dem"):
        continue
    p = os.path.join(DEMOS, name)
    try:
        h = parse_header(p)
        if h is None:
            rows.append(dict(demo=name, error="short header")); continue
        h["demo"] = name
        h["bytes"] = os.path.getsize(p)
        h["sourceTV"] = ("sourcetv" in h["client"].lower()) or ("hltv" in h["client"].lower())
        rows.append(h)
    except Exception as e:
        rows.append(dict(demo=name, error=repr(e)))

st = [r for r in rows if r.get("sourceTV")]
pl = [r for r in rows if str(r.get("map","")).startswith("pl_")]
pl_src = [r for r in pl if r.get("sourceTV")]
clients_pl = {}
for r in pl:
    clients_pl[r.get("client","")] = clients_pl.get(r.get("client",""),0)+1

res = dict(total=len(rows), parse_errors=[r for r in rows if "error" in r],
           sourceTV_count=len(st), sourceTV=st,
           pl_count=len(pl), pl_sourceTV_count=len(pl_src), pl_sourceTV=pl_src,
           pl_clients=sorted(clients_pl.items(), key=lambda kv:-kv[1])[:15])
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(res, f, indent=1)
print("total", len(rows), "sourceTV", len(st), "pl", len(pl), "pl_sourceTV", len(pl_src))
for r in st:
    print("SRC", r["demo"], r["map"], "ticks", r["ticks"], "sec", r["playbackSec"], "server", r["server"])
print("pl clients:", res["pl_clients"][:8])
