
import importlib.util, os, json
here = r'D:/TF2_Native_Worktrees/t105-e5/tools/n14-oracle'
spec = importlib.util.spec_from_file_location('bo', os.path.join(here,'bsp_oracle.py'))
bo = importlib.util.module_from_spec(spec); spec.loader.exec_module(bo)
MAPDIRS=[r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps',
         r'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/download/maps']
maps=['cp_process_f12','cp_sunshine','cp_gullywash_f9','cp_sultry_b8a','koth_product_final','koth_bagel_rc11','pl_upward_f12']
for m in maps:
    p=None
    for d in MAPDIRS:
        c=os.path.join(d,m+'.bsp')
        if os.path.exists(c): p=c; break
    bm=bo.read_bsp(p)
    cps=[a for a in bm['anchors'] if a['classname']=='team_control_point']
    info=[(a.get('targetname',''), [round(v,1) for v in a['origin'][:2]], a.get('team_control_point',''), a.get('point_default_owner','')) for a in cps]
    print('%-20s cps=%d %s' % (m, len(cps), info))
