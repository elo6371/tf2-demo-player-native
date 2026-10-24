import json, os
p="D:/TF2_Native_Test/evidence/t63-sourcetv/corpus_scan.json"
d=json.load(open(p))
print(type(d), list(d)[:10] if isinstance(d,dict) else len(d))
s=json.dumps(d)[:3000]
print(s)
