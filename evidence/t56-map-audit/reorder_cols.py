import json, re
s = json.load(open('ledger_schema.json', encoding='utf-8'))
order = [c['key'] for c in s['columns']]
src = open('face_trace_to_ledger.py', encoding='utf-8').read()
m = re.search(r"COLS = \[.*?\]\n", src, re.S)
assert m, "COLS block not found"
lines = []
for i in range(0, len(order), 5):
    lines.append("        " + ",".join('"%s"' % k for k in order[i:i+5]) + ",")
body = "COLS = [\n" + "\n".join(lines) + "\n]\n"
src = src[:m.start()] + body + src[m.end():]
open('face_trace_to_ledger.py', 'w', encoding='utf-8').write(src)
print("rewrote COLS with", len(order), "keys")
