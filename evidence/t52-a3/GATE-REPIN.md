# T52 outline gate: the capture fixture is pinned, and the toggle reading counts ink

Date: 2026-11-02. Owner: t52-a3. Tree: D:/TF2_Native_Worktrees/t79-integration.

## What was red (run of 2026-11-02 00:00, 91 OK / 7 FAIL)

1. differing pixels INSIDE the drawn regions = 0, expected >= 1
2. published items with an attribution row = 0
3. drawn items on the capture frame = 0
4. no red and blue player regions could be derived from the trace
5. the depth image is not a depth image: 29.08% of pixels carry colour
6. depth evidence not usable (occluded region: <none>)
7. team-coloured pixels in the overlay-off frame = 7607, expected 0

## Causes

**1-4: the capture frame had no player on screen.** The capture tick was derived from
the retained window (`FIRST = retainedFirst - serverTickOrigin`). `serverTickOrigin`
moved 10252 -> 10320 between waves, so the derived playback tick moved 64229 -> 64161,
and 64161 is a frame the camera no longer looks at the players on: the captured draw
group has 11 items, every one `onScreen=0 extent=0 box=0`. With no on-screen item the
published regions, the attribution rows and the red/blue regions are all empty by
construction, so four assertions fail together.

**5-6: the depth view drew colour.** Fixed in the companion commit (scalar clear and
no sky pass in the depth view); see `DEPTH-SCALAR.md`.

**7: the frame-wide team count was measuring the map.** On this camera the map's own
red brick reads 2872 red pixels in the overlay-OFF frame, every one of them OUTSIDE
every published box (`team_inside=0 team_outside=2872`). "No team-coloured pixel
anywhere in the off frame" is a statement about Snakewater's brickwork, not about the
overlay switch, so no threshold can make it true.

## Edits

`outline-check.sh`

* the capture fixture is pinned as a PAIR: `CAPTURE_TICK=64250` and
  `CAM_EYE="5699 -1611 150 14.4 -143.3"`, with `--debug-camera $CAM_EYE` added to the
  single product launch the gate builds (`run_frame`'s `productArgs`). Measured at
  that pair: 11 players `onScreen=1 extent=1` with real boxes (e.g. red2
  453,334-477,346; blue8 753,331-793,341). The window-derived tick is still printed,
  so drift stays visible; `T52_TICK` / `T52_CAM` keep the diagnostic override;
* `--no-player-animation` is passed to every gate frame: the frame under test is a
  BIND-mesh fixture (the renderer publishes a vertex extent only for a non-skinned
  instance, `native/src/native_renderer_frame.cpp:830-836`), and the animated
  silhouette is covered by `A3-REPORT.md` at the same camera and tick;
* assertion 7 now counts team-coloured pixels INSIDE the published regions
  (`$OUT/regions.txt`, already produced at line 891 from the on-trace). The
  frame-wide count stays as a printed diagnostic. Measured: off frame
  `team_inside=0 team_outside=2872`; on frame (bind) `team_inside=1132`;
* mutation m3 reads the same quantity, with the regions kept from the FIXED capture
  (the mutant's own trace is captured with `--outline off` and publishes none).

`outline-frame-check.py`: new mode `teamregions` (`frame=`, `regions_file=`,
optional `max_inside=`), printing `team_inside= / team_outside= / regions=`.

## Expected effect

1-4 restored by the pinned fixture, 5-6 by the depth commit, 7 by the in-region
reading and 3 by the same fixture. The gate run that follows this commit is the
measurement, not this document.
