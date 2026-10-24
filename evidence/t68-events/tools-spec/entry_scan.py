import struct, sys

def scan(path, limit=8):
    data = open(path,'rb').read()
    demo_protocol = struct.unpack_from('<i', data, 8)[0]
    net_protocol = struct.unpack_from('<i', data, 12)[0]
    print('===', path)
    print('  demo_protocol=%d net_protocol=%d' % (demo_protocol, net_protocol))
    off = 1072
    rows = 0
    while off < len(data) and rows < limit:
        start = off
        cmd = data[off]; off += 1
        if cmd == 7 and len(data) - off == 3:
            tick = data[off] | (data[off+1] << 8) | (data[off+2] << 16)
            off += 3
        else:
            tick = struct.unpack_from('<i', data, off)[0]; off += 4
        if demo_protocol >= 4 and cmd != 7:
            off += 1
        if cmd in (1, 2):
            off += 76 + 8
            length = struct.unpack_from('<i', data, off)[0]; off += 4
            payload_off = off
            print('  entry@%d cmd=%d tick=%d payload_off=%d size=%d first_bytes=%s' % (start, cmd, tick, payload_off, length, data[payload_off:payload_off+6].hex()))
            off += length
        elif cmd in (4, 6, 8):
            length = struct.unpack_from('<i', data, off)[0]; off += 4
            print('  entry@%d cmd=%d tick=%d payload_off=%d size=%d' % (start, cmd, tick, off, length))
            off += length
        elif cmd == 3:
            print('  entry@%d cmd=%d tick=%d' % (start, cmd, tick))
        elif cmd == 5:
            off += 4
            length = struct.unpack_from('<i', data, off)[0]; off += 4
            print('  entry@%d cmd=%d tick=%d payload_off=%d size=%d' % (start, cmd, tick, off, length))
            off += length
        else:
            print('  entry@%d unknown cmd=%d' % (start, cmd)); break
        rows += 1

for p in sys.argv[1:]:
    scan(p)
