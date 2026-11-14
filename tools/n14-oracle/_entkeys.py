import importlib.util, os, sys
here = r"D:/TF2_Native_Worktrees/t105-e5/tools/n14-oracle"
spec = importlib.util.spec_from_file_location("bo", os.path.join(here, "bsp_oracle.py"))
bo = importlib.util.module_from_spec(spec); spec.loader.exec_module(bo)
bsp = r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_upward_f12.bsp"
data = open(bsp, "rb").read()
ents = bo.parse_entities(bo.lump(data, 0).decode("utf-8", "replace"))
seen = {}
for e in ents:
    cn = e.get("classname")
    if cn in ("info_player_teamspawn", "path_track", "team_train_watcher", "item_teamflag") and cn not in seen:
        seen[cn] = e
for cn, e in seen.items():
    print("===", cn)
    for k in sorted(e):
        print("   ", k, "=", str(e[k])[:60].replace("\n", " "))
