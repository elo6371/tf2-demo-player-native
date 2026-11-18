
import json, os
B = r'D:/_shared/EXEC-2026-11-15/n14'
J=lambda p: json.load(open(os.path.join(B,p),encoding='utf-8'))
f=J('floors.json')['maps'][0]
print('floors keys:', list(f.keys()))
print('height_bands type:', type(f['height_bands']), json.dumps(f['height_bands'])[:400])
print('height_bands_over_2pct:', json.dumps(f['height_bands_over_2pct'])[:300])
co=J('coalesce.json')
print('coalesce map0 keys:', list(co['cp_process_f12'].keys()) if isinstance(co['cp_process_f12'],dict) else type(co['cp_process_f12']))
print('coalesce summary:', json.dumps(co['_summary'])[:600])
st=J('strategy-runs.json')
print('strategy sample:', json.dumps(st['cp_process_f12'])[:400])
