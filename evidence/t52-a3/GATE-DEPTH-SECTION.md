# Section 5 measures the depth test now, instead of describing why it cannot

T52 A3, 2026-11-02 (second pass; the first pass is `GATE-REPIN.md`). The gate has
run green with exactly one NOT VERIFIED for four rounds:
`NOT-VERIFIED depthaware=1` -- the depth image was valid, no drawn player was
behind world geometry at the pinned camera, so mutation m2's pixel reading had
no region to move. `DEPTH-OCCLUSION.md` has the two causes. This file is what
changed in the gate because of them.

## 5/6 is four runs at a second, pinned fixture

The pinned camera in section 4/6 stays exactly as it was re-pinned last round:
it is the camera ⑦ (no team ink inside the published regions of the overlay-off
frame) is measured on, and moving it would put the map's red brick back inside
the published boxes. The depth section gets its own fixture instead:

    tick   64250 (same pinned tick)
    eye    5138 -1130 10   pitch 2.5   yaw -90

chosen because blue entity 8 is behind world geometry there. Four runs, same
tick, same window, all through `run_frame` so `--hidden-window`,
`--audio-disabled`, `--outline-vertex-extent` and `--no-player-animation`
cannot be forgotten:

| run | flags | what it is for |
|---|---|---|
| `depthon` | `--outline on` | the boxes to measure, and the x-ray frame |
| `depthtested` | `--outline on --outline-depth tested` | the frame that must draw nothing behind a wall |
| `depthoff` | `--outline off` | the comparison target: the map draws no team ink there either |
| `worlddepth` | `--outline off --world-depth --capture-depth <pgm>` | the depth evidence, both images from one frame |

`CAM_EYE` is set to the depth fixture for those four launches and restored
immediately, so section 6's second off frame and every mutation still run at the
pinned camera.

## The depth evidence is proved at 16 bits, not at 8

The 8-bit `--world-depth` readings stay (exists / is scalar / has geometry /
more than one sampled value) because they are what makes "a colour frame as the
depth map" detectable. On top of them, four readings on the `--capture-depth`
image:

* bytes >= 100000 -- a capture that wrote no depth image proves nothing;
* size >= 800x600 -- it is the capture window, not a stub;
* **distinct > 256** -- the diagnostic view can only hold 256 values, so an
  image with more came from the buffer. Measured on the fixture: 3896 distinct.
* max > min -- one value means nothing was drawn.

## The occluded region, and the three-way claim

```
python outline-regions.py --trace $OUT/depthon.trace --occluded --depth-pgm $OUT/worlddepth.pgm
```

`--occluded` prefers `--depth-pgm` (16-bit) and still accepts `--depth` (the
8-bit BMP), because section 0/6's reader self-test feeds it both. On the fixture
the region is `665,334,690,340` -- blue entity 8's own published box, 26x7 px,
every pixel of it proved occluded by the buffer (182 px).

Then `outline-frame-check.py depth` with the three frames of the fixture:

* `occluded_xray >= 8` -- the x-ray frame paints the player through the wall
  (measured 143);
* `occluded_tested == 0` -- the depth-tested frame paints nothing there;
* `occluded_tested_diff == 0` -- and it is byte-identical to the overlay-off
  frame inside the region, so "nothing" is the map, not a missing model.

**The empty-region branch is now RED, not NOT VERIFIED.** The fixture was picked
because a player is behind the map there; an empty region now means the camera
or the tick moved, or the depth capture did not line up. (Before this pass the
branch was truthful: the pinned camera looked at the clusters in the open.)

## m2 runs at the depth fixture

m2 binds the x-ray list to the depth-tested state, so its pixel reading only
means something on the fixture where "through the wall" exists. The mutant's
frame is captured with `CAM_EYE=$DEPTH_CAM` and compared against
`$OUT/depthtested.bmp` / `$OUT/depthoff.bmp`, and the region comes from the
same fixture. The reading that does not depend on a region -- `stateBound` --
is unchanged.

## Two other defects closed in the same pass

**The non-interference reader could fail without failing the gate.** The gate
passed the product's tolerance as `"1134.000"` to a reader that did
`int(...)`, the reader raised `ValueError`, and the gate read its tolerance
assertion as green off a process that never printed a verdict -- the exact
"green assertion next to VERDICT FAIL" shape this file warns about. Now: one
integer conversion (`TOL_INT`) feeds both the comparison and the assertion, the
reader accepts either spelling, and the gate asserts the reader exited 0 and
printed `VERDICT PASS` before reading any number out of it.

**A stale depth image could survive a failed capture.** `run_frame` now removes
`<prefix>.pgm` with the frame it belongs to, so a run that fails to write depth
cannot leave last run's image to be measured.

## The run after this file is the measurement

Nothing in this file is a result. `evidence/outline/run-<stamp>/gate.log` from
the next full `bash outline-check.sh --mutation` is.
