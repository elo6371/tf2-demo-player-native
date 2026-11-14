
import json, os
D=r'D:/_shared/EXEC-2026-11-15/n14'
def L(n):
    p=os.path.join(D,n)
    return json.load(open(p)) if os.path.exists(p) else None
rul=L('ruler.json'); con=L('containment.json'); lm=L('landmarks.json')
base=L('basemap.json'); flo=L('floors.json'); coa=L('coalesce.json')
bud=L('budget.json'); ide=L('identity.json'); sky=L('skybox-classified.json')
anc=L('anchors-outside.json'); vt=L('verify-trace.json')
def idx(doc):
    if doc is None: return {}
    rows = doc.get('maps') if isinstance(doc,dict) and 'maps' in doc else doc
    out={}
    if isinstance(rows,list):
        for r in rows: out[r.get('map')]=r
    elif isinstance(rows,dict):
        out=rows
    return out
R=idx(rul); C=idx(con); Lm=idx(lm); B=idx(base); F=idx(flo); Co=idx(coa); S=idx(sky); A=idx(anc)
maps=['cp_process_f12','cp_sunshine','cp_gullywash_f9','cp_sultry_b8a','koth_product_final','koth_bagel_rc11','pl_upward_f12']
print('%-18s %7s %7s %7s %7s %6s %6s %5s %5s %5s' % ('map','inside%','sky%','other%','fill%','rectsC','edge','cps','ncT','band'))
for m in maps:
    r=R.get(m,{}); c=C.get(m,{}); s0=S.get(m,{}); b=B.get(m,{}); co=Co.get(m,{}); l0=Lm.get(m,{}); f=F.get(m,{})
    print('%-18s %7s %7s %7s %7s %6s %6s %5s %5s %5s' % (
      m,
      s0.get('pct_inside'), s0.get('pct_skybox'), s0.get('pct_other'),
      round(100*b.get('floor_fill_fraction',0),1) if b else '?',
      co.get('rects_C'), co.get('runs_edge'),
      len(l0.get('control_points',[]) or []),
      l0.get('collinear_triple_count'),
      len(f.get('height_bands',[]) or []) ))
print()
print('== containment (raster pixel method)')
for m in maps:
    c=C.get(m,{})
    print('%-18s mass_outside=%-7s outside_bbox=%s anchors_outside=%s on_floor=%s' % (
      m, c.get('floor_mass_outside_fraction'), c.get('outside_bbox'),
      A.get(m,{}).get('outside'), A.get(m,{}).get('on_floor')))
print()
print('== ruler facts')
for m in maps:
    r=R.get(m,{})
    keep={k:v for k,v in r.items() if k in ('aspect','coverage_in_rect_fraction','floor_outside_rect_fraction','landmarks_matched','collinear_triple_count','noncollinear_triples','landmark_pairs_ok','orientation_pairs')}
    print(m, json.dumps(keep)[:400])
print()
print('== floors')
for m in maps:
    f=F.get(m,{})
    print('%-18s bands=%s over2pct=%s main_share=%s' % (m, f.get('height_bands'), f.get('height_bands_over_2pct'),
        round(f.get('main_band_share',0),3) if f.get('main_band_share') is not None else '?'))
print()
print('== identity')
if ide: print(json.dumps(ide.get('_summary') or {k:v for k,v in ide.items() if k!='pairs'})[:900])
print('== verify-trace summary:', json.dumps(vt.get('summary')) if vt else None)
print('== budget:', json.dumps(bud.get('_summary')) if bud else None)
