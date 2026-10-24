import glob,os,re
pats=["evidence/**/*.trace","evidence/**/trace*.txt","evidence/**/*.log"]
seen=set()
for p in glob.glob("D:/TF2_Native_Test/evidence/**/*",recursive=True):
    if not os.path.isfile(p): continue
    if not re.search(r"(trace|real-|product|render)",os.path.basename(p),re.I): continue
    if os.path.getsize(p)<1000: continue
    if p in seen: continue
    seen.add(p)
    try:
        with open(p,encoding="utf-8",errors="replace") as f:
            first=f.readline().strip(); last=None
            for line in f: last=line.strip()
    except Exception as e:
        continue
    def ts(l):
        m=re.match(r"([0-9]+(?:\.[0-9]+)?)\s",l or "")
        return float(m.group(1)) if m else None
    a,b=ts(first),ts(last)
    if a is not None and b is not None and b>a:
        print("%7.1fs  %s"%(b/1000.0,p))
