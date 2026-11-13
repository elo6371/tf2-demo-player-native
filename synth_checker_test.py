#!/usr/bin/env python3
"""synth_checker_test.py -- offline proof that t83-weapon-pixels.py can fail.

The pixel checker only ever runs on product captures, which need the host lock.
This builds a FAKE identity frame and FAKE ledgers by hand, so the checker's
thresholds can be exercised with no product at all:

  S1  a weapon blob at the hand            -> must PASS
  S2  the same frame, no attach ledger row -> must FAIL (N1 shape)
  S3  the attach id rewritten to the body  -> must FAIL (N2 shape: second body)
  S4  the weapon 300 px away from the body -> must FAIL (distance)
  S5  the weapon at the body's feet        -> must FAIL (centre share)
  S6  hold point at the model origin       -> must FAIL (feet radius)

Usage: synth_checker_test.py <dir-for-artifacts>
Exit 0 only if every expectation held.
"""
import os
import struct
import subprocess
import sys
import tempfile

PY = sys.executable
CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 't83-weapon-pixels.py')
BODY_ID = 95
WEAPON_ID = 96


def write_bmp(path, width, height, body_rect, weapon_rect):
    stride = ((width * 3) + 3) & ~3
    pixels = bytearray(stride * height)
    def paint(rect, ident):
        if rect is None:
            return
        x0, y0, x1, y1 = rect
        for y in range(y0, y1 + 1):
            row = y * stride                      # product writes rows top-down
            for x in range(x0, x1 + 1):
                at = row + x * 3
                pixels[at + 0] = 0        # blue
                pixels[at + 1] = 0        # green
                pixels[at + 2] = ident    # red carries the id
    paint(body_rect, BODY_ID)
    paint(weapon_rect, WEAPON_ID)
    header = b'BM' + struct.pack('<IHHI', 54 + len(pixels), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    with open(path, 'wb') as handle:
        handle.write(header + info + bytes(pixels))


def summary(path, attach_id=None, hold='-19.3,40.2,6.4', bone='-19.3,40.2,6.4', origin='-19.3,40.2,6.4'):
    lines = ['tag=synth rc=0 frameTick=1 sceneTick=1',
             'scene: weaponAttach on=1 held=1 drawn=1 poseMissing=0 overBudget=0 refused=0'
             ' noHoldPoint=0 noWorldMesh=0 skippedStandalone=1 firstRefusal=-',
             'weaponAttach: class=CTFPlayer ent=5 source=bone bone=21 body=models/player/demo.mdl'
             ' weaponEnt=744 weaponClass=CTFPipebombLauncher'
             ' weapon=models/weapons/c_models/c_stickybomb_launcher/c_stickybomb_launcher.mdl'
             ' worldIndex=208 family=0 boneAt=%s holdAt=%s' % (bone, hold),
             'entity: id=95 ent=5 model=models/player/demo.mdl team=3 family=0 state=active seq=-1'
             ' label=player skinned=1 origin=0.0,0.0,0.0']
    if attach_id is not None:
        lines.append('entity: id=%d ent=744 model=models/weapons/c_models/c_stickybomb_launcher/'
                     'c_stickybomb_launcher.mdl team=3 family=0 state=attached seq=-1'
                     ' label=weaponAttach skinned=0 origin=0.0,0.0,0.0 attach=1 heldEnt=5'
                     ' source=bone bone=21' % attach_id)
    with open(path, 'w') as handle:
        handle.write('\n'.join(lines) + '\n')


def summary_rows(path, rows, attach_id=WEAPON_ID):
    """A frame with SEVERAL players' attach lines, in the given order.

    rows: list of (ent, hold, bone_at, body_model) tuples. The first row is the
    body the checker is asked about (ent=5 in every case below).
    """
    lines = ['tag=synth rc=0 frameTick=1 sceneTick=1',
             'scene: weaponAttach on=1 held=1 drawn=1 poseMissing=0 overBudget=0 refused=0'
             ' noHoldPoint=0 noWorldMesh=0 skippedStandalone=1 firstRefusal=-']
    for ent, hold, bone_at, body_model in rows:
        lines.append('weaponAttach: class=CTFPlayer ent=%d source=bone bone=21 body=%s'
                     ' weaponEnt=744 weaponClass=CTFPipebombLauncher'
                     ' weapon=models/weapons/c_models/c_stickybomb_launcher/c_stickybomb_launcher.mdl'
                     ' worldIndex=208 family=0 boneAt=%s holdAt=%s' % (ent, body_model, bone_at, hold))
    lines.append('entity: id=95 ent=5 model=models/player/demo.mdl team=3 family=0 state=active seq=-1'
                 ' label=player skinned=1 origin=0.0,0.0,0.0')
    lines.append('entity: id=%d ent=744 model=models/weapons/c_models/c_stickybomb_launcher/'
                 'c_stickybomb_launcher.mdl team=3 family=0 state=attached seq=-1'
                 ' label=weaponAttach skinned=0 origin=0.0,0.0,0.0 attach=1 heldEnt=5'
                 ' source=bone bone=21' % attach_id)
    with open(path, 'w') as handle:
        handle.write('\n'.join(lines) + '\n')


def run(name, expect, bmp, txt):
    proc = subprocess.run([PY, CHECKER, bmp, txt, '5'], capture_output=True, text=True)
    ok = (proc.returncode == 0) == (expect == 'PASS')
    print('%-4s expect=%-4s rc=%d %s' % (name, expect, proc.returncode, 'ok' if ok else 'MISMATCH'))
    for line in (proc.stdout + proc.stderr).strip().splitlines():
        print('       ' + line)
    return ok


def main(argv):
    # Artifacts are scratch, so they go to a temp directory by default: the
    # repository root must not collect capture-looking files that a reviewer
    # could mistake for product evidence.
    root = argv[1] if len(argv) > 1 else os.path.join(tempfile.gettempdir(), 't83-synth-checker')
    os.makedirs(root, exist_ok=True)
    print('synth artifacts: %s' % root)
    width, height = 640, 480
    body = (500, 200, 560, 400)                     # 61 x 201 px torso
    good_bmp = os.path.join(root, 's1.bmp')
    write_bmp(good_bmp, width, height, body, (470, 250, 500, 280))
    s1 = os.path.join(root, 's1.summary'); summary(s1, attach_id=WEAPON_ID)
    s2 = os.path.join(root, 's2.summary'); summary(s2, attach_id=None)
    s3 = os.path.join(root, 's3.summary'); summary(s3, attach_id=BODY_ID)
    far_bmp = os.path.join(root, 's4.bmp')
    write_bmp(far_bmp, width, height, body, (120, 250, 150, 280))
    s4 = os.path.join(root, 's4.summary'); summary(s4, attach_id=WEAPON_ID)
    feet_bmp = os.path.join(root, 's5.bmp')
    write_bmp(feet_bmp, width, height, body, (500, 385, 530, 415))
    s5 = os.path.join(root, 's5.summary'); summary(s5, attach_id=WEAPON_ID)
    s6 = os.path.join(root, 's6.summary'); summary(s6, attach_id=WEAPON_ID, hold='0.5,0.4,0.3')
    # S7/S8: one frame, two players' attach lines. The checker must read the line
    # of the body it is asked about (ent=5), never "whichever line came last".
    # S7 puts the measured body's BAD hold point first and another player's GOOD
    # hold point last: the old last-row read would pass, the ent-filtered read
    # must fail. S8 is the mirror (measured body good first, other player bad
    # last): the filter must pass where the last-row read would fail.
    s7 = os.path.join(root, 's7.summary')
    summary_rows(s7, [(5, '0.5,0.4,0.3', '0.5,0.4,0.3', 'models/player/demo.mdl'),
                      (15, '20.3,55.3,-7.5', '20.3,55.3,-7.5', 'models/player/scout.mdl')])
    s8 = os.path.join(root, 's8.summary')
    summary_rows(s8, [(5, '-19.3,40.2,6.4', '-19.3,40.2,6.4', 'models/player/demo.mdl'),
                      (15, '0.5,0.4,0.3', '0.5,0.4,0.3', 'models/player/scout.mdl')])

    checks = [
        run('S1', 'PASS', good_bmp, s1),
        run('S2', 'FAIL', good_bmp, s2),
        run('S3', 'FAIL', good_bmp, s3),
        run('S4', 'FAIL', far_bmp, s4),
        run('S5', 'FAIL', feet_bmp, s5),
        run('S6', 'FAIL', good_bmp, s6),
        run('S7', 'FAIL', good_bmp, s7),
        run('S8', 'PASS', good_bmp, s8),
    ]
    print('SYNTHETIC-CHECKER=%s' % ('PASS' if all(checks) else 'FAIL'))
    return 0 if all(checks) else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
