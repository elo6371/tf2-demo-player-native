#!/usr/bin/env python3
"""t61_entityid.py -- T61.1 per-entity evidence: model, pick, sample and pixels bound to ONE body.

Why this exists. Until now the pixel gate merged every model's legal sequence into
one set and compared that set with a scene line that carries no entity identity.
A single model choosing a wrong clip could be masked by another model's legal
clip, and nothing in the reading said which body moved. The --world-entity-id
view paints each drawn instance a unique red and prints one ledger line per
instance, so a colour in the frame decodes back to a specific entity.

The chain this checks, all on the same instance:
  LEDGER   every pushed instance has a line: id, entity index, model, drive
           state, resolved sequence, resolved label, skinned flag, frame tick.
           No id may be claimed twice: a repeated id means two bodies were
           painted one colour, so the colour cannot name one body and a mask
           read from it could belong to either line.
  MASK     pixels are grouped by the exact red byte, so each id owns its own
           mask and no mask can absorb a neighbour.
  SUBJECT  the player instance with the largest mask is the subject. If no
           player body is visible the run FAILS -- an empty subject would make
           every claim below vacuous.
  JOIN     the subject ledger line must agree with the pick line for its OWN
           model in the state that line reports, in BOTH sequence and label.
  SAMPLE   every per-entity sample line must report the sequence its own model
           pick resolved for the state that line reports. This is the check a
           single body choosing a wrong clip breaks, and scene-level set
           membership cannot see it.
  DRAWN    every drawn player instance in the ledger must report the sequence
           its own model pick resolved for the state it reports. The ledger has
           one row per pushed instance and no cap, so this is the band that
           covers bodies the eight-line sample trace never printed.
  MOTION   over the subject mask alone, the animated and off frames must
           differ by a measured share, so the pixels being judged are its own.

Usage: t61_entityid.py <identity.bmp> <identity.summary> <animated.bmp> <off.bmp>
The summary is the gate's own capture summary, which carries the ledger, the
per-model pick lines and the per-entity sample lines of the SAME run.
Exit: 0 all bounds held, 1 a bound failed, 2 input unreadable.
"""
import struct
import sys

MIN_SUBJECT_PIXELS = 200
MIN_MOTION_SHARE = 0.20


def load_raw(path):
    try:
        data = open(path, 'rb').read()
    except OSError as exc:
        raise SystemExit('FAIL cannot read %s: %s' % (path, exc))
    if data[:2] != b'BM':
        raise SystemExit('not a BMP: ' + path)
    off = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit('want 24 or 32 bpp, got %d' % bpp)
    nbytes = bpp // 8
    stride = ((width * nbytes) + 3) & ~3
    return data, off, width, abs(height), nbytes, stride


def pixel(data, off, nbytes, stride, x, y):
    base = off + y * stride + x * nbytes
    return (data[base + 2], data[base + 1], data[base + 0])


def fields_of(line, marker, lead_key):
    at = line.find(marker)
    if at < 0:
        return None
    rest = line[at + len(marker):].split()
    if not rest:
        return None
    out = {}
    out[lead_key] = rest[0]
    for word in rest[1:]:
        if '=' in word:
            key, value = word.split('=', 1)
            out[key] = value
    return out


def parse_summary(path):
    ledger, order, picks, samples, duplicates = {}, [], {}, [], []
    for line in open(path, 'r', errors='replace'):
        row = fields_of(line, 'entity: id=', 'id')
        if row is not None:
            try:
                ident = int(row['id'])
            except ValueError:
                continue
            if ident in ledger:
                # T61.1 review 3, P1: the id is the ONLY link between a pixel
                # and a body. Two ledger lines claiming one id means the paint
                # put two bodies in one colour: keeping the first line would
                # make the other body's pixels read as its own -- a reading
                # that looks like a pass. Keep the line and let main() refuse.
                duplicates.append((ident, ledger[ident], row))
                continue
            row['id'] = ident
            ledger[ident] = row
            order.append(ident)
            continue
        row = fields_of(line, 'anim: pick model=', 'model')
        if row is not None:
            picks[row['model']] = row
            continue
        row = fields_of(line, 'anim: ent=', 'ent')
        if row is not None:
            samples.append(row)
    return ledger, order, picks, samples, duplicates


def pick_for(picks, model, state):
    row = picks.get(model)
    if row is None or state not in ('idle', 'moving'):
        return None, None
    return row.get(state + 'Seq'), row.get(state + 'Label')


def main(argv):
    if len(argv) != 5:
        raise SystemExit('usage: t61_entityid.py <identity.bmp> <identity.summary>'
                         ' <animated.bmp> <off.bmp>')
    ident_path, summary_path, anim_path, off_path = argv[1:5]
    ledger, order, picks, samples, duplicates = parse_summary(summary_path)
    if not ledger:
        raise SystemExit('ENTITY-ID=FAIL no ledger line in ' + summary_path)
    print('LEDGER lines=%d distinct-ids=%d'
          % (len(order) + len(duplicates), len(order)))
    print('PICKS  models=%d' % len(picks))
    if duplicates:
        # Sort by id only: the kept/dropped rows are dicts and cannot be
        # compared against each other, which is what a bare sorted() does on
        # ties (m12 found this: 94 duplicates sharing two ids).
        for ident, kept, dup in sorted(duplicates, key=lambda t: t[0])[:8]:
            print('DUP    id=%d kept ent=%s model=%s ; dropped ent=%s model=%s'
                  % (ident, kept.get('ent'), kept.get('model'),
                     dup.get('ent'), dup.get('model')))
        if len(duplicates) > 8:
            print('DUP    ... and %d more duplicated ledger line(s)'
                  % (len(duplicates) - 8))
        ids = ', '.join(str(i) for i in
                        sorted(set(i for i, _kept, _dup in duplicates))[:8])
        print('FAIL %d ledger line(s) reuse an entity id another body already'
              ' claims (ids %s): the id colour names one body twice, so the'
              ' subject mask could be read from the wrong body'
              % (len(duplicates), ids))
        print('ENTITY-ID=FAIL')
        return 1

    data, off, width, height, nbytes, stride = load_raw(ident_path)
    counts = {}
    for y in range(height):
        for x in range(width):
            r, g, b = pixel(data, off, nbytes, stride, x, y)
            if g == 0 and b == 0 and r > 0:
                counts[r] = counts.get(r, 0) + 1
    print('MASK   distinct-ids=%d pixels=%d' % (len(counts), sum(counts.values())))
    unknown = sorted(i for i in counts if i not in ledger)
    if unknown:
        print('ENTITY-ID=FAIL frame carries id(s) with no ledger line: %s' % unknown)
        return 1

    failures = []
    players = [(i, counts[i]) for i in counts
               if 'models/player/' in ledger[i].get('model', '')]
    if not players:
        print('ENTITY-ID=FAIL no player body is visible in the id frame;'
              ' a per-entity claim about a body that is not on screen would be vacuous')
        return 1
    players.sort(key=lambda pair: (-pair[1], pair[0]))
    player_ids = set(i for i, _ in players)
    subject_id, subject_px = players[0]
    subject = ledger[subject_id]
    print('SUBJECT id=%d ent=%s model=%s state=%s seq=%s label=%s pixels=%d'
          % (subject_id, subject.get('ent'), subject.get('model'),
             subject.get('state'), subject.get('seq'), subject.get('label'),
             subject_px))
    print('VISIBLE players=%d props=%d' % (len(players),
          len([i for i in counts if i not in player_ids])))

    if subject_px < MIN_SUBJECT_PIXELS:
        failures.append('subject mask %d < %d pixels' % (subject_px, MIN_SUBJECT_PIXELS))
    state = subject.get('state', '-')
    if state not in ('idle', 'moving'):
        failures.append('subject drive state is %r, so there is no pick to agree with' % state)
    else:
        want_seq, want_label = pick_for(picks, subject.get('model'), state)
        if want_seq is None:
            failures.append('no pick line for %s, so the subject %s cannot be joined'
                            % (subject.get('model'), state))
        else:
            if subject.get('seq') != want_seq:
                failures.append('subject seq=%s but its own model pick for %s is %s'
                                % (subject.get('seq'), state, want_seq))
            if subject.get('label') != want_label:
                failures.append('subject label=%s but its own model pick for %s is %s'
                                % (subject.get('label'), state, want_label))
            if not failures:
                print('JOIN   subject matches its own pick: %s %s seq=%s label=%s'
                      % (subject.get('model'), state, want_seq, want_label))

    # T61.1 P1: the per-instance half. The ledger carries one row per pushed
    # instance with no cap, so this band judges bodies the eight-line sample
    # trace never printed. A drawn player that reports a drive state and a
    # skinned sequence must report the sequence its OWN model pick resolved.
    drawn_judged = 0
    drawn_skipped = 0
    for ident in order:
        row = ledger[ident]
        if 'models/player/' not in row.get('model', ''):
            continue
        dstate = row.get('state', '-')
        if dstate not in ('idle', 'moving'):
            continue
        if row.get('skinned') != '1':
            drawn_skipped += 1
            continue
        drawn_judged += 1
        want_seq, want_label = pick_for(picks, row.get('model'), dstate)
        if want_seq is None:
            failures.append('ledger id=%d %s state=%s has no pick line'
                            % (ident, row.get('model'), dstate))
        elif row.get('seq') != want_seq or row.get('label') != want_label:
            failures.append('ledger id=%d ent=%s %s %s sampled seq=%s label=%s'
                            ' but its pick is seq=%s label=%s'
                            % (ident, row.get('ent'), row.get('model'), dstate,
                               row.get('seq'), row.get('label'), want_seq,
                               want_label))
    if drawn_judged < 1:
        failures.append('no drawn player body carries a drive state and a'
                        ' skinned sequence: the per-instance half of the chain'
                        ' would be vacuous')
    elif not failures:
        print('DRAWN  %d skinned player instance(s) report their own pick'
              ' (%d not skinned, not judged)' % (drawn_judged, drawn_skipped))
    bad_samples = []
    judged = 0
    for row in samples:
        model = row.get('model', '')
        sstate = row.get('state', '-')
        if sstate not in ('idle', 'moving'):
            continue
        judged += 1
        want_seq, want_label = pick_for(picks, model, sstate)
        if want_seq is None:
            bad_samples.append('%s state=%s has no pick line' % (model, sstate))
        elif row.get('seq') != want_seq or row.get('label') != want_label:
            bad_samples.append('ent=%s %s %s sampled seq=%s label=%s but its pick is'
                               ' seq=%s label=%s'
                               % (row.get('ent'), model, sstate, row.get('seq'),
                                  row.get('label'), want_seq, want_label))
    if bad_samples:
        for reason in bad_samples:
            failures.append(reason)
    elif judged < 1:
        failures.append('no per-entity sample line names a drive state: the sample'
                        ' half of the chain would be vacuous')
    else:
        print('SAMPLE %d per-entity sample line(s), each matching its own model pick'
              % judged)

    anim, anim_off, anim_w, anim_h, anim_n, anim_s = load_raw(anim_path)
    off_d, off_off, off_w, off_h, off_n, off_s = load_raw(off_path)
    if (anim_w, anim_h) != (off_w, off_h) or (anim_w, anim_h) != (width, height):
        failures.append('frame sizes differ: id %dx%d animated %dx%d off %dx%d'
                        % (width, height, anim_w, anim_h, off_w, off_h))
    else:
        differ = 0
        for y in range(height):
            for x in range(width):
                r, g, b = pixel(data, off, nbytes, stride, x, y)
                if not (g == 0 and b == 0 and r == subject_id):
                    continue
                a = pixel(anim, anim_off, anim_n, anim_s, x, y)
                o = pixel(off_d, off_off, off_n, off_s, x, y)
                if max(abs(a[k] - o[k]) for k in range(3)) >= 8:
                    differ += 1
        share = differ / float(subject_px)
        print('MOTION subject=%d over-mask=%d differ=%d share=%.2f%%'
              % (subject_id, subject_px, differ, share * 100.0))
        if share < MIN_MOTION_SHARE:
            failures.append('the subject moved %.2f%% of its own pixels, below %.0f%%'
                            % (share * 100.0, MIN_MOTION_SHARE * 100.0))

    print('DECODED id=%d ent=%s model=%s state=%s seq=%s label=%s'
          % (subject_id, subject.get('ent'), subject.get('model'),
             state, subject.get('seq'), subject.get('label')))
    if failures:
        for reason in failures:
            print('FAIL ' + reason)
        print('ENTITY-ID=FAIL')
        return 1
    print('ENTITY-ID=PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
