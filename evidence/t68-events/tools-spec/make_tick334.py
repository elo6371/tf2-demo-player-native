import collections, hashlib, os

base = 'D:/TF2_Native_Test/evidence/t64-events/oracle'
out = 'D:/TF2_Native_Test/evidence/t68-events/golden/same-tick-334.tsv'

demo_files = [('bagel','bagel.koth_bagel_rc13'), ('snakewater','snakewater.cp_snakewater_final1')]
lines = ['# T68 same-tick negative-control fixture: every event at tick 334, in wire order',
         '# source=evidence/t64-events/oracle/*.events.tsv',
         '# demo\tseq\ttick\tevent\tvalues']
counts = {}
for label, stem in demo_files:
    p = os.path.join(base, stem + '.events.tsv')
    seq = 0
    with open(p, encoding='utf-8') as f:
        for line in f:
            if line.startswith('#'):
                continue
            parts = line.rstrip('\n').split('\t')
            if int(parts[0]) != 334:
                continue
            seq += 1
            values = ';'.join(parts[3:]) if len(parts) > 3 else ''
            lines.append('%s\t%d\t%s\t%s\t%s' % (label, seq, parts[0], parts[2], values))
    counts[label] = seq
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')
data = open(out,'rb').read()
print('wrote', out, 'rows', {k: v for k, v in counts.items()}, 'bytes', len(data), 'sha256', hashlib.sha256(data).hexdigest())
print('--- first 8 data rows:')
for l in lines[3:11]:
    print(l[:160])
print('--- last 3 data rows:')
for l in lines[-3:]:
    print(l[:160])
