#!/usr/bin/env python3
"""packet_clock.py -- static packet-tick -> server-tick mapping for Source demos.

Why: the product's entity snapshot history is keyed by the server tick carried in
net_Tick messages, while playback positions and demo headers use the demo packet
tick (cmdinfo tick). The T63 sample list must name inspection points in BOTH
clocks so a runtime probe can be rerun exactly. This script walks the demo
command stream (same layout as native/src/demo_header.cpp:95-160) and reads the
first net_Tick message (type 3: 32-bit tick + 16-bit frame time + 16-bit
deviation, LSB-first bit order per MessageBits::read at demo_header.cpp:406-411).

Verified against evidence/final/oracle-survey-ashville73.txt known pairs:
  pkt 12419 -> 64229, pkt 22671 -> 74481, pkt 64229 -> 116039, pkt 74481 -> 126291
"""
import json, struct, sys, os

def parse_header(b):
    if b[:8] != b"HL2DEMO\0":
        raise SystemExit("missing HL2DEMO signature")
    demo_protocol, net_protocol = struct.unpack_from("<ii", b, 8)
    def fixed(off, n):
        raw = b[off:off+n]
        return raw.split(b"\0", 1)[0].decode("utf-8", "replace")
    server = fixed(16, 260); client = fixed(276, 260); mapname = fixed(536, 260)
    game_dir = fixed(796, 260)
    playback, ticks, frames = struct.unpack_from("<fii", b, 1056)
    return dict(demo_protocol=demo_protocol, net_protocol=net_protocol,
                server=server, client=client, map=mapname, game_dir=game_dir,
                playback_time=playback, ticks=ticks, frames=frames)

class Bits:
    def __init__(self, buf, start_bit=0):
        self.buf = buf; self.pos = start_bit
    def read(self, n):
        v = 0
        for i in range(n):
            bit = self.pos + i
            byte = bit >> 3
            if byte >= len(self.buf):
                raise EOFError
            v |= ((self.buf[byte] >> (bit & 7)) & 1) << i
        self.pos += n
        return v

def walk_commands(b):
    hdr = parse_header(b)
    proto = hdr["demo_protocol"]
    size = len(b)
    cur = 1072
    packets = []
    commands = 0
    while cur < size:
        start = cur
        cmd = b[cur]; cur += 1
        if cmd == 7 and size - cur == 3:
            tick = b[cur] | (b[cur+1] << 8) | (b[cur+2] << 16); cur += 3
        else:
            if cur + 4 > size: break
            tick = struct.unpack_from("<i", b, cur)[0]; cur += 4
        if proto >= 4 and cmd != 7:
            cur += 1
        if cmd in (1, 2):
            if cur + 76 + 8 + 4 > size: break
            length = struct.unpack_from("<i", b, cur + 76 + 8)[0]
            data_off = cur + 76 + 8 + 4
            data_size = length
            if length < 0 or data_off + length > size: break
            packets.append((tick, data_off, data_size))
            cur = data_off + length
        elif cmd in (3, 7):
            pass
        elif cmd in (4, 6, 8):
            length = struct.unpack_from("<i", b, cur)[0]; cur += 4
            cur += length
        elif cmd == 5:
            cur += 4
            length = struct.unpack_from("<i", b, cur)[0]; cur += 4
            cur += length
        else:
            break
        commands += 1
        if cmd == 7: break
    return hdr, packets, commands

def first_net_tick(payload):
    bits = Bits(payload)
    for _ in range(16):
        if bits.pos + 6 > len(payload) * 8: return None
        t = bits.read(6)
        if t == 0:
            continue
        if t == 3:
            try:
                tick = bits.read(32)
                bits.read(16); bits.read(16)
            except EOFError:
                return None
            return tick
        return None
    return None

def map_demo(path):
    with open(path, "rb") as f:
        b = f.read()
    hdr, packets, commands = walk_commands(b)
    pairs = []
    for (ptick, off, size) in packets:
        st = first_net_tick(b[off:off+size])
        if st is not None:
            pairs.append((ptick, st))
    offsets = sorted(set(st - pt for pt, st in pairs))
    out = dict(path=path.replace("\\", "/"), map=hdr["map"], ticks=hdr["ticks"],
               packets=len(packets), net_tick_pairs=len(pairs),
               first_packet_tick=pairs[0][0] if pairs else None,
               last_packet_tick=pairs[-1][0] if pairs else None,
               first_server_tick=pairs[0][1] if pairs else None,
               last_server_tick=pairs[-1][1] if pairs else None,
               server_minus_packet_offsets=offsets,
               offset_constant=(len(offsets) == 1),
               samples=[dict(packet_tick=p, server_tick=s) for p, s in pairs[::max(1, len(pairs)//5)][:6]])
    return out

if __name__ == "__main__":
    out = []
    for p in sys.argv[1:]:
        out.append(map_demo(p))
    print(json.dumps(out, indent=2))
