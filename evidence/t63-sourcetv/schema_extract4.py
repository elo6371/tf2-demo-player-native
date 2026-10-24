import re
p = r"D:\TF2_Native_Test\evidence\animation-availability\oracle-all-classes.txt"
classes = {}
cur = None
with open(p, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        m = re.match(r"class id=(\d+) name=(\S+) table=(\S+) flat=(\d+)", line.strip())
        if m:
            cur = {"id": int(m.group(1)), "name": m.group(2), "table": m.group(3), "flat": int(m.group(4)), "props": []}
            classes[cur["name"]] = cur
            continue
        m2 = re.match(r"\[(\d+)\] (\S+) (\w+)", line.strip())
        if m2 and cur is not None:
            cur["props"].append((int(m2.group(1)), m2.group(2), m2.group(3)))

def first(name, prefixes, k=3):
    c = classes[name]
    print("--- %s" % name)
    for pre in prefixes:
        hits = [(i,n,t) for i,n,t in c["props"] if n == pre or n.startswith(pre + ".")]
        print("  %s: %d e.g. %s" % (pre, len(hits), hits[:k]))

first("CTFObjectiveResource", ["m_iOwner","m_iState","m_flCapTime","m_flCapPerc","m_bTeamCanCap","m_iTeamIcons","m_iTeamOverlays","m_iTeamReqCappers","m_flLazyCapPerc","m_iNumControlPoints"], 2)
first("CTFPlayerResource", ["m_iTotalScore","m_iDeaths","m_bAlive","m_iPing","m_iTeam","m_iClass","m_iScore","m_iHealth","m_iMaxHealth"], 2)
