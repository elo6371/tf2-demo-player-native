import hashlib, os, re, sys

base = 'D:/TF2_Native_Test/evidence/t64-events/oracle'
gold = 'D:/TF2_Native_Test/evidence/t68-events/golden'

for demo in ['bagel.koth_bagel_rc13','snakewater.cp_snakewater_final1']:
    p = os.path.join(base, demo + '.events.tsv')
    ids = []
    used = {}
    rows = 0
    with open(p, 'r', encoding='utf-8') as f:
        for line in f:
            if line.startswith('# def id='):
                m = re.match(r'# def id=(\d+) name=(\S+) fields=', line)
                if m:
                    ids.append(int(m.group(1)))
            elif line.startswith('#'):
                pass
            else:
                parts = line.rstrip('\n').split('\t')
                rows += 1
                used[int(parts[1])] = used.get(int(parts[1]), 0) + 1
    seq = ids == list(range(len(ids)))
    print(demo, 'defs=%d seq=%s min=%s max=%s rows=%d used_ids=%d used_max=%d' % (len(ids), seq, ids[0] if ids else None, ids[-1] if ids else None, rows, len(used), max(used) if used else -1))
    print('  used_ids=', sorted(used)[:60])

for name in sorted(os.listdir(gold)):
    p = os.path.join(gold, name)
    with open(p, 'rb') as f:
        data = f.read()
    lines = data.count(b'\n')
    print('golden %-40s bytes=%d lines=%d sha256=%s' % (name, len(data), lines, hashlib.sha256(data).hexdigest()[:16]))

for demo in ['bagel.koth_bagel_rc13','snakewater.cp_snakewater_final1']:
    p = os.path.join(base, demo + '.events.tsv')
    with open(p, 'rb') as f:
        data = f.read()
    print('input %-40s bytes=%d sha256=%s' % (os.path.basename(p), len(data), hashlib.sha256(data).hexdigest()))
