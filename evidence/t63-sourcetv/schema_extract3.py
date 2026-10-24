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

def dump_nonarray(name, extra=()):
    c = classes[name]
    print("--- %s id=%s flat=%s" % (name, c["id"], c["flat"]))
    for i,n,t in c["props"]:
        if re.search(r"\.\d{3}$", n):
            continue
        if n == "m_nModelIndexOverrides" or n.startswith("m_nModelIndexOverrides"):
            continue
        print("  [%d] %s %s" % (i,n,t))

dump_nonarray("CTFObjectiveResource")
print()
dump_nonarray("CTFPlayerResource")
print()
dump_nonarray("CTeamTrainWatcher")
