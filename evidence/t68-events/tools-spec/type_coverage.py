import collections, os, re

base = 'D:/TF2_Native_Test/evidence/t64-events/oracle'
GOLD = ['player_death','teamplay_round_start','teamplay_round_win','controlpoint_starttouch','controlpoint_endtouch','teamplay_point_captured']
for demo in ['bagel.koth_bagel_rc13','snakewater.cp_snakewater_final1']:
    p = os.path.join(base, demo + '.events.tsv')
    defs = {}
    used = set()
    max_str = 0
    with open(p, encoding='utf-8') as f:
        for line in f:
            if line.startswith('# def id='):
                m = re.match(r'# def id=(\d+) name=(\S+) fields=(.*)$', line.rstrip('\n'))
                fields = []
                if m.group(3):
                    for part in m.group(3).split(','):
                        n, t = part.rsplit(':', 1)
                        fields.append((n, t))
                defs[m.group(2)] = fields
            elif not line.startswith('#'):
                parts = line.rstrip('\n').split('\t')
                used.add(parts[2])
                for v in parts[3:]:
                    if len(v) > max_str:
                        max_str = len(v)
    types = collections.Counter()
    for n in used:
        for _, t in defs[n]:
            types[t] += 1
    print(demo, 'used names', len(used), 'field type occurrences', dict(types), 'max value str len', max_str)
    print('  used defs with Local:', [n for n in used if any(t == 'Local' for _, t in defs[n])])
