import sys, struct

def read_bmp(path):
    data = open(path, 'rb').read()
    if data[:2] != b'BM':
        raise SystemExit("not a BMP: %s" % path)
    offset = struct.unpack_from('<I', data, 10)[0]
    width = struct.unpack_from('<i', data, 18)[0]
    height = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 24:
        raise SystemExit("expected a 24-bit BMP, got %d" % bpp)
    stride = ((width * 3 + 3) // 4) * 4
    return data, offset, width, abs(height), stride, height > 0

a_path, b_path = sys.argv[1], sys.argv[2]
left, top, right, bottom = (int(v) for v in sys.argv[3:7])
a = read_bmp(a_path)
b = read_bmp(b_path)
if a[2:6] != b[2:6]:
    raise SystemExit("geometry differs: %s vs %s" % (a_path, b_path))
data_a, off_a, width, rows, stride, bottom_up = a
data_b, off_b = b[0], b[1]
if data_a[:off_a] != data_b[:off_b]:
    raise SystemExit("BMP headers differ (a different frame shape was captured)")

inside = 0
outside = 0
outside_diff = 0
inside_diff = 0
first = None
for row in range(rows):
    y = (rows - 1 - row) if bottom_up else row   # file row -> image y
    base = row * stride
    line_a = data_a[off_a + base: off_a + base + width * 3]
    line_b = data_b[off_b + base: off_b + base + width * 3]
    in_row = top <= y < bottom
    # Three spans for this row: left of the rect, the rect itself, right of it.
    spans = [(0, max(0, min(width, left)), 'outside'),
             (max(0, left), min(width, right), 'inside' if in_row else 'outside'),
             (max(0, min(width, right)), width, 'outside')]
    for (sx, ex, kind) in spans:
        if ex <= sx:
            continue
        s, e = sx * 3, ex * 3
        count = ex - sx
        same = line_a[s:e] == line_b[s:e]
        if kind == 'inside':
            inside += count
            if not same:
                for i in range(s, e, 3):
                    if line_a[i:i+3] != line_b[i:i+3]:
                        inside_diff += 1
        else:
            outside += count
            if not same:
                for i in range(s, e, 3):
                    if line_a[i:i+3] != line_b[i:i+3]:
                        outside_diff += 1
                        if first is None:
                            first = (i // 3, y)
print("inside=%d" % inside)
print("outside=%d" % outside)
print("insideDiff=%d" % inside_diff)
print("outsideDiff=%d" % outside_diff)
print("firstDiff=%s" % (("%d,%d" % first) if first else "-"))
