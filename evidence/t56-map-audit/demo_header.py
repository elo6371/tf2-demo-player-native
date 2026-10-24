#!/usr/bin/env python3
"""Static Source-engine demo header reader (no product/probe launch).

Reads only the 1072-byte demo header: magic, demo protocol, net protocol,
server name, client name, MAP NAME, game dir, playback time, ticks, frames,
signon length. Prints one JSON object per line.
"""
import json, os, struct, sys

HDR = 8 + 4 + 4 + 260*4 + 4 + 4 + 4 + 4

def read_header(path):
    st = os.stat(path)
    with open(path, "rb") as f:
        b = f.read(HDR)
    if len(b) < HDR:
        return {"path": path, "error": "short_header", "bytes": st.st_size}
    if b[:8] != b"HL2DEMO\x00":
        return {"path": path, "error": "bad_magic", "magic": b[:8].hex()}
    off = 8
    dem_proto, net_proto = struct.unpack_from("<ii", b, off); off += 8
    def s(off):
        raw = b[off:off+260].split(b"\x00", 1)[0]
        return raw.decode("utf-8", "replace")
    server = s(off); off += 260
    client = s(off); off += 260
    mapname = s(off); off += 260
    gamedir = s(off); off += 260
    (playback,) = struct.unpack_from("<f", b, off); off += 4
    ticks, frames, signon = struct.unpack_from("<iii", b, off)
    return {
        "path": path, "bytes": st.st_size,
        "demoProtocol": dem_proto, "netProtocol": net_proto,
        "server": server, "client": client, "map": mapname, "gameDir": gamedir,
        "playbackSec": round(playback, 3), "ticks": ticks, "frames": frames,
        "signonLength": signon,
    }

if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "one":
        for p in sys.argv[2:]:
            print(json.dumps(read_header(p), ensure_ascii=False))
    elif mode == "scan":
        root = sys.argv[2]
        for dirpath, _dirs, files in os.walk(root):
            for fn in files:
                if fn.lower().endswith(".dem"):
                    print(json.dumps(read_header(os.path.join(dirpath, fn)), ensure_ascii=False))
