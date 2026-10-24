import collections, os

base = 'D:/TF2_Native_Test/evidence/t64-events/oracle'
GOLD = ['player_death','teamplay_round_start','teamplay_round_win','controlpoint_starttouch','controlpoint_endtouch','teamplay_point_captured']

for demo in ['bagel.koth_bagel_rc13','snakewater.cp_snakewater_final1']:
    p = os.path.join(base, demo + '.events.tsv')
    tick_events = collections.defaultdict(list)
    per_name_tick = collections.defaultdict(lambda: collections.defaultdict(int))
    with open(p, encoding='utf-8') as f:
        for line in f:
            if line.startswith('#'):
                continue
            parts = line.rstrip('\n').split('\t')
            tick = int(parts[0]); name = parts[2]
            tick_events[tick].append(name)
            per_name_tick[name][tick] += 1
    print('=====', demo)
    total = sum(len(v) for v in tick_events.values())
    multi = {t: v for t, v in tick_events.items() if len(v) > 1}
    print('events=%d ticks=%d multi_ticks=%d max_per_tick=%d min_tick=%d max_tick=%d' % (total, len(tick_events), len(multi), max(len(v) for v in tick_events.values()), min(tick_events), max(tick_events)))
    top = sorted(tick_events.items(), key=lambda kv: (-len(kv[1]), kv[0]))[:5]
    for t, names in top:
        c = collections.Counter(names)
        agg = ', '.join('%s %d' % (n, k) for n, k in sorted(c.items(), key=lambda kv: (-kv[1], kv[0])))
        print('top tick %d count %d: %s' % (t, len(names), agg))
    mx_name = None; mx = 0
    for n, tm in per_name_tick.items():
        for t, k in tm.items():
            if k > mx:
                mx, mx_name = k, (n, t)
    print('max same name on one tick: %s x%d at tick %d' % (mx_name[0], mx, mx_name[1]))
    for n in GOLD:
        ticks = per_name_tick.get(n, {})
        shared = 0; max_other = 0; max_other_tick = -1
        for t, k in ticks.items():
            others = len(tick_events[t]) - k
            if others > 0:
                shared += 1
            if others > max_other:
                max_other, max_other_tick = others, t
        print('  %-26s occurrences=%d ticks=%d share_tick_with_others=%d max_others_on_tick=%d at %d' % (n, sum(ticks.values()), len(ticks), shared, max_other, max_other_tick))
