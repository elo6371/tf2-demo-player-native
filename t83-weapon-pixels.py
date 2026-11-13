#!/usr/bin/env python3
"""t83-weapon-pixels.py -- T83: the held weapon is IN THE PICTURE and AT THE BODY.

WHY THIS EXISTS
    "the frame changed when I turned the flag on" is not evidence: the world pass
    already draws every weapon entity where the demo left it, so a frame can
    differ because a gun vanished from the floor without any gun appearing in a
    hand. This reads ONE identity capture (view=entity-id), where the product
    paints every drawn instance a unique red (g=b=0, r=id) and prints an
    'entity: id=...' ledger, and measures:

      BODY    the mask of the player entity's own id (ent=<ent>).
      WEAPON  the mask of the id the ledger marks attach=1 heldEnt=<ent> --
              i.e. the instance the attach path pushed, not the standalone body.
      HAND    geometry between the two: the weapon must touch the body, sit in
              its upper part (a hand, not the floor) and not be a whole second
              body. Plus the model-space hold point from the 'weaponAttach:'
              line, which must be a hand-height point and nowhere near the model
              origin (0,0,0 is the FEET -- the exact bug this feature fixes).

    Every reading has a threshold that FAILS, and the shell gate runs this
    checker on an off/off pair as its negative control: with no attach instance
    there is no id to measure and it must exit non-zero.

Usage: t83-weapon-pixels.py <identity.bmp> <identity.summary> <ent>
Exit:  0 all readings held, 1 a reading failed, 2 an input is unreadable.
"""
import math
import struct
import sys

import numpy as np

MIN_BODY_PIXELS = 200      # the body must be big enough to be a subject
MIN_WEAPON_PIXELS = 25     # px of the attach instance's own id
MAX_WEAPON_TO_BODY = 30    # px: the weapon must touch (or nearly touch) the body
MAX_WEAPON_CENTRE_SHARE = 0.75   # centre must be above the body's bottom quarter
MAX_WEAPON_HEIGHT_SHARE = 0.90   # a held gun is not as tall as the body
# The model space is Y-UP. Measured on demo.mdl: the bind frame of bip_head is
# (0, 76.17, -0.4), bip_hand_R (-20.35, 46.30, 3.35), bip_foot_R (-7.97, 6.67,
# -2.68) and the grip bone weapon_bone (-19.34, 40.17, 6.43). A hand-held weapon
# therefore lives in a band around y = 25..70, and anything at y < 15 is on or
# under the floor -- which is exactly the bug this feature had at first.
HOLD_MIN_Y = 20.0          # model units: hand height on an 80-unit tall model
HOLD_MAX_Y = 75.0
HOLD_MAX_LATERAL = 60.0    # |x|
HOLD_MAX_DEPTH = 45.0      # |z|
HOLD_MIN_RADIUS = 15.0     # not the model origin (the feet)


def load_rgb(path):
    try:
        data = np.fromfile(path, dtype=np.uint8)
    except OSError as exc:
        raise SystemExit('FAIL cannot read %s: %s' % (path, exc))
    if data.size < 54 or data[0:2].tobytes() != b'BM':
        raise SystemExit('FAIL not a BMP: ' + path)
    raw = data.tobytes()
    offset = struct.unpack_from('<I', raw, 10)[0]
    width, height = struct.unpack_from('<ii', raw, 18)
    bpp = struct.unpack_from('<H', raw, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit('FAIL want 24 or 32 bpp, got %d in %s' % (bpp, path))
    nbytes = bpp // 8
    stride = ((width * nbytes) + 3) & ~3
    rows = abs(height)
    body = data[offset:offset + stride * rows].reshape(rows, stride)
    body = body[:, :width * nbytes].reshape(rows, width, nbytes)
    return body[:, :, 2].copy(), body[:, :, 1].copy(), body[:, :, 0].copy()


def read_ledger(path):
    rows = []
    try:
        handle = open(path, 'r', errors='replace')
    except OSError:
        return rows
    with handle:
        for line in handle:
            kind = None
            text = None
            at = line.find('entity: id=')
            if at >= 0:
                kind, text = 'entity', 'id=' + line[at + len('entity: id='):]
            else:
                at = line.find('weaponAttach: class=')
                if at >= 0:
                    kind, text = 'attach', line[at + len('weaponAttach: '):]
            if kind is None:
                continue
            fields = {}
            for word in text.split():
                if '=' in word:
                    key, value = word.split('=', 1)
                    fields[key] = value
            fields['_kind'] = kind
            rows.append(fields)
    return rows


def dilate(mask):
    grown = mask.copy()
    grown[1:, :] |= mask[:-1, :]
    grown[:-1, :] |= mask[1:, :]
    grown[:, 1:] |= mask[:, :-1]
    grown[:, :-1] |= mask[:, 1:]
    return grown


def mask_of(chan_r, chan_g, chan_b, ident):
    return (chan_g == 0) & (chan_b == 0) & (chan_r == ident)


def bbox_of(mask):
    ys, xs = np.nonzero(mask)
    return int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())


def main(argv):
    if len(argv) != 4:
        raise SystemExit('usage: t83-weapon-pixels.py <identity.bmp> <identity.summary> <ent>')
    identity_path, summary_path, want_ent = argv[1], argv[2], argv[3]

    ledger = read_ledger(summary_path)
    if not ledger:
        print('FAIL no entity: id= / weaponAttach: ledger line in ' + summary_path)
        return 1
    body_rows = [r for r in ledger
                 if r.get('_kind') == 'entity' and r.get('ent') == want_ent
                 and r.get('attach') != '1' and 'id' in r]
    if not body_rows:
        print('FAIL no entity: id= ledger line names ent=%s' % want_ent)
        return 1
    attach_rows = [r for r in ledger
                   if r.get('_kind') == 'entity' and r.get('attach') == '1'
                   and r.get('heldEnt') == want_ent and 'id' in r]
    hold_rows = [r for r in ledger if r.get('_kind') == 'attach' and 'holdAt' in r]
    # One frame can carry attach lines for several players (the whole match is
    # drawn): read the hold point of THE BODY this call measures (ent=), not of
    # whichever class happened to be traced last. A frame with no line for this
    # body is a failure, not a reason to borrow another body's number.
    own_hold_rows = [r for r in hold_rows if r.get('ent') == want_ent]
    if own_hold_rows:
        hold_rows = own_hold_rows

    try:
        body_id = int(body_rows[-1]['id'])
    except (KeyError, ValueError):
        print('FAIL the ledger line for ent=%s carries no numeric id' % want_ent)
        return 1
    model = body_rows[-1].get('model', '-')

    chan_r, chan_g, chan_b = load_rgb(identity_path)
    body = mask_of(chan_r, chan_g, chan_b, body_id)
    body_pixels = int(body.sum())
    if body_pixels == 0:
        print('FAIL the identity frame has no pixel of the id-%d colour (ent=%s model=%s)'
              % (body_id, want_ent, model))
        return 1
    bmin_x, bmin_y, bmax_x, bmax_y = bbox_of(body)
    body_height = bmax_y - bmin_y + 1
    print('BODY id=%d ent=%s model=%s pixels=%d bbox=%d,%d,%d,%d'
          % (body_id, want_ent, model, body_pixels, bmin_x, bmin_y, bmax_x, bmax_y))

    fail = 0
    if body_pixels < MIN_BODY_PIXELS:
        print('FAIL body mask %d < %d pixels: too small to be evidence'
              % (body_pixels, MIN_BODY_PIXELS))
        fail = 1

    if not attach_rows:
        print('FAIL the ledger carries no entity: attach=1 heldEnt=%s line:'
              ' this frame has no attached weapon instance to measure' % want_ent)
        return 1
    weapon_ids = sorted({int(r['id']) for r in attach_rows if r['id'].isdigit()})
    weapon = np.zeros_like(body)
    for wid in weapon_ids:
        weapon = weapon | mask_of(chan_r, chan_g, chan_b, wid)
    weapon_pixels = int(weapon.sum())
    if weapon_pixels == 0:
        print('FAIL the identity frame has no pixel of the attach instance colour(s) %s'
              ' (ent=%s): the instance was pushed but never reached a pixel'
              % (weapon_ids, want_ent))
        return 1
    wmin_x, wmin_y, wmax_x, wmax_y = bbox_of(weapon)
    centre_x = (wmin_x + wmax_x) / 2.0
    centre_y = (wmin_y + wmax_y) / 2.0
    weapon_height = wmax_y - wmin_y + 1
    weapon_width = wmax_x - wmin_x + 1
    print('WEAPON ids=%s pixels=%d bbox=%d,%d,%d,%d centre=%.1f,%.1f size=%dx%d'
          % (weapon_ids, weapon_pixels, wmin_x, wmin_y, wmax_x, wmax_y,
             centre_x, centre_y, weapon_width, weapon_height))

    distance = None
    grid = body
    for step in range(MAX_WEAPON_TO_BODY + 1):
        if step > 0:
            grid = dilate(grid)
        if bool((weapon & grid).any()):
            distance = step
            break
    inside_share = float((weapon & body).sum()) / float(weapon_pixels)
    centre_share = float((centre_y - bmin_y) / float(body_height))
    print('HAND distance=%s shareInsideBody=%.3f centreShareFromTop=%.3f'
          ' bodyHeight=%d weaponHeightShare=%.3f'
          % ('<none>' if distance is None else str(distance), inside_share,
             centre_share, body_height, weapon_height / float(body_height)))

    if weapon_pixels < MIN_WEAPON_PIXELS:
        print('FAIL the attached weapon covers %d px < %d: not visible enough to be a'
              ' reading' % (weapon_pixels, MIN_WEAPON_PIXELS))
        fail = 1
    if distance is None or distance > MAX_WEAPON_TO_BODY:
        print('FAIL the closest weapon pixel is %s px from the body (> %d): the weapon'
              ' is not at this body'
              % ('<none>' if distance is None else str(distance), MAX_WEAPON_TO_BODY))
        fail = 1
    if centre_y > bmin_y + MAX_WEAPON_CENTRE_SHARE * body_height:
        print('FAIL the weapon centre is %.1f%% down the body (> %.0f%%): that is below'
              ' the hips, not a hand'
              % (100.0 * (centre_y - bmin_y) / body_height,
                 100.0 * MAX_WEAPON_CENTRE_SHARE))
        fail = 1
    if weapon_height > MAX_WEAPON_HEIGHT_SHARE * body_height:
        print('FAIL the weapon blob is %d px tall against a %d px body (> %.2fx): that is'
              ' a second body, not a held weapon'
              % (weapon_height, body_height, MAX_WEAPON_HEIGHT_SHARE))
        fail = 1

    if not hold_rows:
        print('FAIL the summary carries no weaponAttach: holdAt= line: the hold point'
              ' was not reported')
        fail = 1
    else:
        row = hold_rows[-1]
        try:
            hx, hy, hz = [float(v) for v in row['holdAt'].split(',')]
            bx, by, bz = [float(v) for v in row['boneAt'].split(',')]
        except (KeyError, ValueError):
            print('FAIL weaponAttach: holdAt/boneAt is not three numbers: %r' % (row,))
            return 1
        radius = math.sqrt(hx * hx + hy * hy + hz * hz)
        print('HOLDPOINT source=%s bone=%s boneAt=%.1f,%.1f,%.1f holdAt=%.1f,%.1f,%.1f'
              ' radius=%.1f' % (row.get('source', '-'), row.get('bone', '-'),
                                bx, by, bz, hx, hy, hz, radius))
        if not (HOLD_MIN_Y <= hy <= HOLD_MAX_Y):
            print('FAIL the hold point is at y=%.1f, outside the hand band [%.0f,%.0f]'
                  ' model units (y is up)' % (hy, HOLD_MIN_Y, HOLD_MAX_Y))
            fail = 1
        if abs(hx) > HOLD_MAX_LATERAL or abs(hz) > HOLD_MAX_DEPTH:
            print('FAIL the hold point is at x=%.1f z=%.1f, further than (%.0f, %.0f)'
                  ' from the body axis' % (hx, hz, HOLD_MAX_LATERAL, HOLD_MAX_DEPTH))
            fail = 1
        if radius < HOLD_MIN_RADIUS:
            print('FAIL the hold point is %.1f units from the model origin (< %.0f): that'
                  ' is the FEET, the placement bug this feature fixes'
                  % (radius, HOLD_MIN_RADIUS))
            fail = 1
        print('HOLDPOINT driftFromBone=%.2f'
              % max(abs(hx - bx), abs(hy - by), abs(hz - bz)))

    print('WEAPON-PIXELS=%s' % ('FAIL' if fail else 'PASS'))
    return fail


if __name__ == '__main__':
    sys.exit(main(sys.argv))
