# T45 wide-area pose: WALL013 (T35's nine-pose set, qualified by measurement)

This directory records the one wide-area diagnostic pose that T45 added to
`world-material-semantics-check.sh`, and the measurements that chose it. Every
number here was captured on the tree at `6888e6d` (exe sha256
`096f2c1f4c6506f6c322077af2d3be1789ad446ecc648168185ee528d2942784`) under the
shared host lock, at `draw: begin tick=2000`, with the gate's own frame flags
(`--hidden-window --audio-disabled --viewmodel models/weapons/v_models/v_scattergun_scout.mdl
--viewmodel-render companion --settings-file <gate ini>`).

## Why a second camera was needed

After T29's near-clip fix the tick-2000 reference camera covers only 28 px of the
world with the nine material-normal materials, so the specular lobe there has
nothing to read: `S1 == S2` byte-identical (0/0), and the surviving sliver of
the bump-under-dynamic pair moves one way only (`C vs D`: 79 bytes brighter /
0 darker). T40 printed both readings NOT VERIFIED and named the cheapest next
step: a wide-area pose from T35's nine-pose set, which T35 derived with the
probe but never launched the product at.

## Why THIS pose

`_shared/t45/wide-select.sh` captured `S1w` (specular on, material normal off) at
all nine poses at tick 2000 and ranked each pose by the pixel population its
material-normal switch moves against the `S2w` frame (specular on + material
normal on) of the same pose, same build, same flags:

    pose          differing  brighter  darker  max   in rank-table.txt
    WALL013        775,135    386,760  388,375   24   chosen
    GREEK_GROUND   109,963          0  109,963   24   one-sided
    IMETAL006       40,448     14,010   26,438   10
    WALL017         25,030     10,266   14,764   11
    METALDOOR001     4,069      1,797    2,272   11
    BEAM001            222        201       21    3
    DIRTGROUND4         22         21        1    3
    BEAM002              5          5        0    1
    IMETAL005            0          0        0    0
    (differing/brighter/darker count BYTES over the 2,582,352-byte frame,
    i.e. 1264x681x24; the bmp tools' "total" is the byte count, not the pixel
    count. The rank table's PAIRPX column carries the byte count.)

A pose qualified only if both directions were nonzero (the two-sided claim this
task needs) and then the largest total won: WALL013, eye
(-3498.225, 1021.852, 1327.843), pitch 70.529329, yaw 134.999352.

The spread above is itself the answer to "is the tick-2000 one-sidedness a
defect": the same reader, the same nine tiles and the same atlas give 0/0 at one
pose, one-sided darkening at another (GREEK_GROUND: 0 / 109,963) and two-sided
movement at four others. The sign is a property of which surfaces are on screen,
not of the shader.

## What the pose does and does not change (read this before quoting a number)

`--debug-camera` moves the RENDER view only. The cluster cull is still computed
from the observer camera, so every pose's trace carries the identical
`gpu: world vis ... cameraCluster=1032 drawnTriangles=76344 eyeMap=(-2423.6,5566.0,-1280.0)`
line, identical bump-control scope (44 materials / 53,462 triangles) and
identical specular scope (100 materials / 76,231 triangles). Consequences:

* the trace counters cannot tell two poses apart, which is why the pose was
  chosen by the frames, not by the trace;
* a pose frame shows the observer-camera-drawn set re-viewed, so it contains
  holes where geometry behind the pose camera was culled (visible in
  `S2w-wide-pose.png`);
* the pair readings are still valid: both frames of a pair share the same drawn
  set and differ by exactly one switch;
* the population claim is "pixels the reader moves in this diagnostic view", not
  "the whole scene at that camera". Both NOT VERIFIED lines in the gate keep that
  wording.

## Pinned readings (the pose the gate now captures)

    pose WALL013 eye=(-3498.225,1021.852,1327.843) pitch=70.529329 yaw=134.999352
    Aw  vs A   2,570,233 / 2,582,352 bytes differ (99.53%, max_delta 220)
    Aw  vs Aw2 0 -- the pose frame is deterministic
    S2w vs the hold-A S2w frame of the same pose: 0 -- deterministic across holds
    S1w vs S2w 775,135 bytes, max_delta 24, brighter 386,760 / darker 388,375
    Cw  vs Dw  688,102 bytes, max_delta 60, brighter 203,847 / darker 484,255
    feature-apply: bump enabled=1 tiles=9 materials=9 vertices=19305 (S2w, Dw)
                   bump refused (S1w, Cw); specular on (S1w, S2w), dynamic on (Cw, Dw)

Frame sha256:

    Aw   56aaa2f295eff76711af461b9d9b4ee841babdd1bb4956d380b6f2cd124baf18
    Aw2  56aaa2f295eff76711af461b9d9b4ee841babdd1bb4956d380b6f2cd124baf18
    S1w  303010f2e022ba73885cea5aa484db85a4a0e0e298554c0e67aa38debc19ca40
    S2w  124e7c481443ac299b963583f4d6be461f94efd1ba1d8124ae90601c8e5af340
    Cw   8ee623f54d640409d9c155932f2ad4876a75d02b9748926183a00a9d7a358f76
    Dw   b4ff3e317b68ee7a408cbb59c76161c129146e3622336b3a3ba572503acb3a2d

## Files

    readings.txt     the full readings block (frames, trace lines, pairs, proofs)
    rank-table.txt   the nine-pose qualification table
    wide-select.sh   the script that ranked the poses and captured the winner
                     (documentation copy; the live copy runs from
                     _shared/t45/wide-select.sh under the host lock and sources
                     _shared/t45/lib.sh, which builds the same product flags)
    S2w-wide-pose.png, S1w-wide-pose.png
                     the winning pose with the material normal on / off

Reproduce (under the host lock, one owner, from the worktree root):

    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t45 \
      bash /d/TF2_Native_Worktrees/_shared/t45/wide-select.sh
