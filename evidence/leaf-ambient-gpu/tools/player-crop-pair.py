# 1:1 leaf|basic crop pair for the two players the tick shows (same boxes as
# player-crops-stats.txt; this script only draws them for the evidence folder).
from PIL import Image, ImageDraw
base = ".scratch/leafgpu/"
leaf = Image.open(base + "leaf.bmp").convert("RGB")
basic = Image.open(base + "basic.bmp").convert("RGB")
picks = [("courtyard (outdoor leaf, up face 0.002354)", (709, 481, 829, 681)),
         ("doorway (indoor leaf, up face 0.000105)", (1052, 481, 1172, 681))]
CW, CH = 120, 200
sheet = Image.new("RGB", (CW * 2 + 10, (CH + 26) * len(picks) + 18), (18, 18, 18))
d = ImageDraw.Draw(sheet)
d.text((2, 4), "LEFT leaf arm | RIGHT basic arm, 1:1, cp_process_f12 tick 600", fill=(0, 255, 255))
for i, (label, box) in enumerate(picks):
    y = 18 + i * (CH + 26)
    sheet.paste(leaf.crop(box), (0, y + 20))
    sheet.paste(basic.crop(box), (CW + 10, y + 20))
    d.text((2, y + 4), label, fill=(255, 255, 0))
sheet.save(base + "accept-players-1to1.png")
print("wrote accept-players-1to1.png", sheet.size)
