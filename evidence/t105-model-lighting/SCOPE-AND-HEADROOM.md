# T105 model lighting -- scope and headroom readings

Produced 2026-11-21 from the four pr600 captures in .scratch/light36-{off,off-explicit,basic,trace}/
on branch task/light36-ambient. Every number below is reproducible with the two
scripts named beside it, both of which read only the BMPs.

## 1. --lighting basic moves ONLY model/prop pixels   (.scratch/t105-containment.py)

The normal-trace frame is captured with `--lighting basic --lighting-trace`. With the
probe on, the entity/prop pixel shader returns the encoded world normal before any
lighting arithmetic runs, and the WORLD pixel shader reads lightingParams from a b2
slot this feature binds only inside the entity pass -- so trace-vs-off marks exactly the
pixels the entity and prop passes drew, and it does so on a DIFFERENT pair of frames
from the pair that measures the lighting change.

    threshold=0   basic_changed=114616  trace_changed=117804  inside=114616  OUTSIDE=0
    threshold=1   basic_changed=114431  trace_changed=117804  inside=114431  OUTSIDE=0
    threshold=10  basic_changed=106686  trace_changed=117796  inside=106683  OUTSIDE=3

At the capture's own precision (a channel difference of 1) every pixel --lighting basic
moves lies inside the model mask: 114431 of 114431. At a threshold of 10, three pixels
sit outside; they are the fringe of the same bodies (bbox x=732..800 y=604..627, i.e.
the near player pair's silhouette, where a 1/255-channel difference between the two
runs decides which side of the threshold a pixel lands on).

This is the reading behind "the world is untouched", and it is a containment test
rather than a whole-frame equality: the entity mask is 117804 px of 860784, so a
feature that leaked into the BSP could not hide behind it.

Independent cross-check (world-region probes, .scratch/t105-world-scope.py): the dome,
the left wall, the railings and the sky region are changed=0 px in BOTH basic and trace,
and the non-entity population's mean luminance is 124.72 in both frames with 0 clipped
pixels. (One 64x64 probe at 1184,480 reports ~50% changed in both frames -- that probe
straddles the doorway body, not a wall; the same-luminance check above is the one that
covers the world population as a whole.)

## 2. Headroom: what basic costs in blown pixels   (.scratch/t105-clip.py, t105-clipmap.py)

    entity mask   OFF mean=103.92 max=215.0 clipped-any-channel=54 (0.046%)
    entity mask   BASIC mean=145.36 max=255.0 clipped-any-channel=2652 (2.251%)

2.251% of the model population reaches 255 in at least one channel, and 821 of those
pixels are fully white. They are spread over the whole frame (bbox x=110..1192
y=0..680) in small clusters rather than one hot spot: the densest 64x64 cell holds 513
of them, the next 290.

This is a real trade and it is stated rather than tuned away: the alternative measured
on this tick was a block that put 9928 model pixels BELOW the flat fake's floor and none
above it. The exposure that closes that (1.5, with the bounce shares above) is the one
where every surface direction clears the old floor; a lower exposure would re-open the
dark side, and a higher one would blow more highlights. Refining it needs a tone map in
the model pass, which is lighting Step 2's problem (a prefiltered environment plus a
curve), not this step's.

## 3. Where the light lands   (evidence/normal-dot-light-bins.txt, crops.txt)

N.L over the entity mask, L = the map's sun (0.500,-0.866,-0.000):

    N.L -1.00..-0.75   n= 1387   OFF 72.6   BASIC  60.6   delta -12.1
    N.L -0.75..-0.50   n= 2364   OFF 71.3   BASIC  61.8   delta  -9.5
    N.L -0.50..-0.25   n= 8599   OFF 116.5  BASIC 104.0   delta -12.5
    N.L -0.25..+0.00   n= 3484   OFF 93.3   BASIC  93.6   delta  +0.3
    N.L +0.00..+0.25   n=11954   OFF 99.5   BASIC 112.9   delta +13.4
    N.L +0.25..+0.50   n= 6337   OFF 97.9   BASIC 127.0   delta +29.1
    N.L +0.50..+0.75   n=38551   OFF 105.5  BASIC 152.5   delta +47.0
    N.L +0.75..+1.00   n=45120   OFF 105.7  BASIC 169.3   delta +63.6

    entity mean 103.9 -> 145.4, p50 107.1 -> 156.9, dark<48 4.4% -> 4.1%

Two 1:1 crops (crop-A-near-pair-1to1.png, crop-B-doorway-1to1.png; readings in crops.txt):
the near red/blue pair gains on the sun side and the doorway body -- almost entirely
shadow side, 1834 back-facing of 2038 normal samples -- moves 86.0 -> 86.5 overall with
its darkest-200 pixels 18.8 -> 17.3. Both bodies keep their form: the deep creases still
read dark, the sun-facing surfaces lift, and nothing is left at the old flat 0.62.

The bounce shares exist because of the doorway crop: with the brief's original shares
(walls 0.25, ground 0.0) that body went 86.0 -> 78.3 and its dark<24 share 1.9% -> 10.5%.
No surface direction of the arm that ran before could be darker than 0.62, so a change
that darkens a shadow side is moving the defect, not closing it.
