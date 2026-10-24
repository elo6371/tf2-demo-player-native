import struct, sys, re, lzma
from collections import Counter

path = sys.argv[1]
with open(path, 'rb') as f:
    ident = f.read(4)
    version, = struct.unpack('<i', f.read(4))
    lumps = []
    for i in range(64):
        raw = f.read(16)
        off, length, lver = struct.unpack('<iii', raw[:12])
        fourcc = raw[12:16]
        lumps.append((off, length, lver, fourcc))
    off, length, lver, fourcc = lumps[0]
    f.seek(off)
    data = f.read(length)

print('file', path)
print('ident', ident, 'version', version, 'entity_lump_bytes', length)

if data[:4] == b'LZMA':
    actual_size, lzma_size = struct.unpack('<II', data[4:12])
    props = data[12:17]
    raw = data[17:17 + lzma_size]
    stream = props + struct.pack('<Q', actual_size) + raw
    text = lzma.decompress(stream, format=lzma.FORMAT_ALONE).decode('latin-1', errors='replace')
    print('lzma actual_size', actual_size, 'lzma_size', lzma_size, 'decoded_bytes', len(text.encode('latin-1', errors='replace')))
else:
    text = data.decode('latin-1', errors='replace')
    print('uncompressed entity lump')

classes = re.findall(r'"classname"\s+"([^"]+)"', text)
c = Counter(classes)
print('entity_count', len(classes), 'unique_classes', len(c))
for name in ['team_train_watcher', 'team_control_point', 'team_control_point_master', 'team_round_timer', 'tf_gamerules', 'func_tracktrain', 'path_track', 'func_door', 'prop_dynamic']:
    print(name, c.get(name, 0))

blocks = text.split('{')
n = 0
for b in blocks:
    if '"classname" "team_train_watcher"' in b:
        n += 1
        body = b.split('}')[0]
        print('--- team_train_watcher block', n, '---')
        print(body.strip())
print('team_train_watcher_blocks', n)
