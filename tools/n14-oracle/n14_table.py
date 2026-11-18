
import json, os
B = r'D:/_shared/EXEC-2026-11-15/n14'
J=lambda p: json.load(open(os.path.join(B,p),encoding='utf-8'))
ruler={m['map']:m for m in J('ruler.json')['maps']}
land={m['map']:m for m in J('landmarks.json')['maps']}
cont={m['map']:m for m in J('containment.json')['maps']}
floors={m['map']:m for m in J('floors.json')['maps']}
bm={m['map']:m for m in J('basemap.json')['maps']}
st=J('strategy-runs.json'); co=J('coalesce.json')
print('%-20s %6s %7s %7s %6s %6s %6s %4s %6s %6s %5s %6s' % ('map','aspect','outF%','cropOutF','cov%','fill%','rectsC','lmk','noncol','outAnc','bands','stack%'))
for k in ruler:
    r=ruler[k]; c=cont[k]; f=floors[k]; b=bm[k]
    print('%-20s %6.4f %7.2f %7.2f %6.1f %6.1f %6d %4d %6d %6d %5d %6.3f' % (
      k, r['rect_aspect'],
      100*c['floor_mass']['floor_mass_outside_fraction'],
      100*r['crop']['floor_area_outside_fraction'],
      100*r['crop']['coverage_in_rect'],
      100*b['floor_fill_fraction'], co[k]['rects_C'],
      r['landmark_distance_units']['count'], land[k]['collinear_triple_count'],
      c['anchors_outside_total'], f['height_bands_over_2pct'], f['stacked_fraction']))
print()
print('margins (u, frac):')
for k in ruler:
    c=cont[k]
    print('  %-20s %s  frac %s' % (k, c['margin_units'], {a:round(v,3) for a,v in c['margin_fraction_of_rect'].items()}))
