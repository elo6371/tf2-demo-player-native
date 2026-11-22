from PIL import Image
a = Image.open(".scratch/leafgpu/leaf-preview.png").convert("RGB")
b = Image.open(".scratch/leafgpu/leaf.bmp").convert("RGB")
print("sizes", a.size, b.size)
xa, xb = a.load(), b.load()
W, H = a.size
x0 = y0 = 10**9; x1 = y1 = -1; n = 0
for y in range(H):
    for x in range(W):
        if xa[x, y] != xb[x, y]:
            n += 1
            x0 = min(x0, x); y0 = min(y0, y); x1 = max(x1, x); y1 = max(y1, y)
print("changed=%d bbox=(%d,%d)-(%d,%d) %dx%d" % (n, x0, y0, x1, y1, x1 - x0 + 1, y1 - y0 + 1))
