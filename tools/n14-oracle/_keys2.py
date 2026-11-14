
import json, os
B = r'D:/_shared/EXEC-2026-11-15/n14'
J=lambda p: json.load(open(os.path.join(B,p),encoding='utf-8'))
c=J('containment.json')['maps'][0]
print('map',c['map'])
print('floor_mass:',json.dumps(c['floor_mass'])[:600])
print('keys:',list(c.keys()))
print('margin:',json.dumps(c.get('margin_units'))[:200], json.dumps(c.get('margin_fraction_of_rect'))[:200])
