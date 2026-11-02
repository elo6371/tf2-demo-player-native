# Outline gate, full run with the pinned camera and the depth test measured

Run of `bash outline-check.sh --mutation` on `integration/t79-final` at
`e3e2910`, 2026-11-02 12:43:05 - 14:23:05, under
`hostlock.sh run t52-a3-gate2`.

    OUTLINE-CHECK=PASS
    MUTATION-SUITE=ALL-RED-RESTORED
    121 OK, 0 FAIL, 18 MUTATION-RED, 0 NOT-VERIFIED
    GATE-RC=0

Raw log: `evidence/t52-a3/gate-cam2.log` (this directory, committed).
Frames and traces: `evidence/outline/run-20261102-cam2/` (83 MB, untracked on
purpose -- traces are regenerable, the log is the record).

## What this run changes relative to the last recorded gate run

The last recorded run (`evidence/outline/run-20261102-gate-repin`, 109 OK, 1
"depth not verified") passed with the depth-awareness clause **not measured**:
no player pixel sat behind world geometry at that fixture. This run measures
it, and the gate no longer carries a NOT-VERIFIED line.

## Sections

| section | result | what it measured |
| --- | --- | --- |
| -1/6 harness self-test | green | every `assert_*` can go red; the readers can print `VERDICT FAIL` |
| 0/6 offline readers | green | both directions, no GPU, no demo, no lock |
| 1/6 contract invariants | green | the overlay contract's own arithmetic |
| 2/6 five coverage cases | green | the cases as data |
| 3/6 real recording | green | identity, team and pose at every sampled tick |
| 4/6 frames | green | nothing else changed, both colours, both directions |
| 5/6 depth | green | the occluded region, three ways (below) |
| 6/6 toggle | green | one slot, one owner, foreign presses refused |

### 4/6, the fixture (pinned, not derived)

    capture tick=64250
    capture camera=5699 -1611 150 14.4 -143.3
    capture animation=off

The tick and the camera are pinned together because they are only valid
together: `serverTickOrigin` moved 10252 -> 10320 between two runs of the same
demo, which moved the derived capture tick 64229 -> 64161, and at 64161 all 11
items are `onScreen=0 extent=0 box=0` -- the camera has already left. The
derived tick is still printed for the record; it is no longer the fixture.

Speed invariance held this run: the speed=1 replay (994 s, 694 MB of trace,
trimmed) produced the same frame as the speed=8 run.

### 5/6, the depth image, 8-bit view

    depth image: bytes=2582406 nonGreyPct=0.00 nonzeroPct=66.75 uniqueSampled=2

The scalar picture is scalar again. Getting here took two product fixes, both
recorded in `DEPTH-SCALAR.md`: the debug view cleared to a tinted colour, and
the sky pass drew 26-28% of the frame in its own colours into a view whose
documented contract is a depth image. The 43 pixels that are still not pure
grey are the crosshair marker (0.005% of the frame).

### 5/6, the depth image, 16-bit capture

    capture-depth image: bytes=1721586 width=1264 height=681 distinct=3896 min=61090 max=65535

3896 distinct depths, so this is the depth buffer and not the 8-bit view: the
8-bit view saturates past a few units (T26 measures exactly that, and still
does -- `world-geometry-hole-check.sh` expects `saturated=1`).

### 5/6, the occluded region

    occluded: entity=8 team=blue occludedPixels=182 rect=665,334,690,340
    occluded_xray=143 occluded_tested=0 occluded_tested_diff=0 region=26x7
    VERDICT PASS

A wall in front of a blue player, found from the 16-bit buffer rather than from
a guess: 182 occluded pixels, 143 team-coloured pixels drawn straight through
the wall when the depth test is off, 0 when it is on, and 0 pixels of difference
against the overlay-off frame inside the same rectangle. The three readings are
consistent with each other: the 143 drawn through the wall are a subset of the
182 occluded pixels.

### 6/6, the switch

    off frame team pixels inside the published regions: team_inside=0 team_outside=2872 regions=11
    VERDICT PASS

2872 team-coloured pixels remain in the overlay-off frame and they are all
**outside** the eleven published boxes -- they are scene reds (brick, the demo's
own red walls). This is why the clause is phrased "inside the published
regions": as a frame-wide count the same reading would be impossible to satisfy
on any map with red in it, which is what made clause 7 red for a whole round.

## Mutations: all 14 cases, each rebuilt, each required to go red

    m1  team pixels in the WRONG region=848 (fixed=0)
    m2  team-coloured pixels through the wall=0 (fixed=143)
    m2  x-ray depth test state actually bound=1 (fixed=0)
    m3  team-coloured pixels inside the published regions in the --outline off frame=1132 (fixed=0)
    m5  x-ray depth write state=1 (fixed=0)
    m6  without the hold, speed=1 and speed=8 capture different frames
    m7  silhouettes submitted without the class fallback=0 (fixed=15)
    m7  players rejected as noMesh without the class fallback=20 (fixed=5)
    m8  silhouettes DRAWN when the item loses the upload key=0 (fixed=6)
    m9  probe self-test failures when the bind mesh wins=5 (fixed=0)
    m4  pass order (overlay before viewModel)=0 (fixed=1)
    m4  overlay ordinal after the move=8 (fixed=7)
    m11 identity mismatches in the readback table=449 (fixed=0)
    m12 pixels outside every drawn region with the draw shifted 3 px=1170 (fixed=40)
    m13 pixels outside every drawn region when the product declares no expansion=7065 (fixed=40)
    m14 silhouettes the band refuses because the product declared zero=11 (fixed=0)
    m14 out-of-region pixels the gate still accepts once the shell is declared zero=0 (fixed=1)

m-meta (a mutant that does not compile reads MUTATION-INVALID, not green) also
ran and was caught. Every source was restored byte-identically (`cmp`) after
each mutant; `MUTATION-SUITE=ALL-RED-RESTORED`.

The three mutants this pass changed are the ones that matter for the re-pin: m3
now measures 1132 team-coloured pixels inside the published boxes (it measured
7607 frame-wide before, i.e. it was measuring the map), m2 measures the x-ray
count against a measured occluded rectangle instead of reading M2-UNDECIDABLE,
and m14 now has a shell that is declared non-zero so its second clause can
distinguish "the band refuses everything" from "the band accepts everything".

## Reproduce

    cd /d/TF2_Native_Worktrees/t79-integration
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t52-a3-gate \
        bash outline-check.sh --mutation

Roughly 100 minutes: the speed=1 section alone is ~17 min, and the mutation
suite rebuilds the product 15 times.
