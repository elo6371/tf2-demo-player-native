import collections, os, re

base = 'D:/TF2_Native_Test/evidence/t64-events/oracle'
GOLD = ['player_death','teamplay_round_start','teamplay_round_win','controlpoint_starttouch','controlpoint_endtouch','teamplay_point_captured']

for demo in ['bagel.koth_bagel_rc13','snakewater.cp_snakewater_final1']:
    p = os.path.join(base, demo + '.events.tsv')
    defs = {}
    counts = collections.Counter()
    empty_field = 0
    with open(p, encoding='utf-8') as f:
        for line in f:
            if line.startswith('# def id='):
                m = re.match(r'# def id=(\d+) name=(\S+) fields=(.*)$', line.rstrip('\n'))
                defs[m.group(2)] = (int(m.group(1)), m.group(3))
            elif not line.startswith('#'):
                parts = line.rstrip('\n').split('\t')
                counts[parts[2]] += 1
    print('=====', demo, 'defs', len(defs))
    for g in GOLD:
        print('  %-26s id=%s fields=%s' % (g, defs[g][0], defs[g][1]))
    print('  distinct used names=%d' % len(counts))
    print('  names:', ', '.join('%s:%d' % (n, counts[n]) for n in sorted(counts)))
    # events with empty field list among used names
    for n in counts:
        if defs[n][1] == '':
            empty_field += counts[n]
    print('  events with empty definition field list=%d' % empty_field)
