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

def show(name, kws):
    c = classes.get(name)
    if not c:
        print("MISSING CLASS", name); return
    print("--- %s id=%s flat=%s" % (name, c["id"], c["flat"]))
    for i,n,t in c["props"]:
        if any(k.lower() in n.lower() for k in kws):
            print("  [%d] %s %s" % (i,n,t))

show("CTFObjectiveResource", ["Progress","Perc","Cap","Owner","State","Team","Num","Visible","Boss","Prev","Lazy"])
show("CTFPlayerResource", ["Score","Death","Total","Ping","Team","Class","Alive","Health","Conn","Slot","Damage","MaxHealth"])
print("=== classes with Payload ===")
for n,c in classes.items():
    if "payload" in n.lower():
        print("--- %s id=%s flat=%s props=%d" % (n, c["id"], c["flat"], len(c["props"])))
        for i,pn,t in c["props"]:
            if any(k in pn.lower() for k in ["progress","perc","cap","state","team","push","cart","train","speed","checkpoint"]):
                print("  [%d] %s %s" % (i,pn,t))
print("=== total classes", len(classes))
