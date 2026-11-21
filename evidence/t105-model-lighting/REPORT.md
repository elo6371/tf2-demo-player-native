# T105 -- model lighting Step 1 (task-36)

Branch `task/light36-ambient`. Everything here is reproducible from the four pr600
captures on tick 600 of
`E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem`.

## The pin

| pass | command extra | md5 of pr600.bmp |
|---|---|---|
| handover command verbatim | (none) | `2ddade356e85dd77b795a7380c254bcb` |
| off, explicit | `--lighting off` | `2ddade356e85dd77b795a7380c254bcb` |
| basic | `--lighting basic` | `d927c52639fb10c62a50b3018806a2d3` |
| trace | `--lighting basic --lighting-trace` | `3f39236ee189629f9a5ed42c170a80da` |

The first row is the acceptance pin and it reproduces. The second row is the same bytes,
so "off" is the arithmetic the previous build ran, not an approximation of it.

## What is in this directory

- `pins.txt` -- the four md5s, with the demo and tick they were taken at.
- `trace-off.txt`, `trace-off-explicit.txt`, `trace-basic.txt`, `trace-trace.txt` --
  the `capture: lights ...` and `capture: model-lighting ...` lines from each run: the
  arm flag, the sun, and the six ambient-cube face colours the GPU was handed.
- `world-space-normal-trace.txt` -- 16 `lighting-normal:` samples: per instance, the
  three normal rows the entity vertex shader uses. Two instances of the SAME model at
  different yaw carry DIFFERENT rows, and every row is a rotation, so the normal that
  reaches the pixel shader is world space.
- `check-lighting-floor.py` + `floor-gate.txt` -- the floor inequality
  (min over all directions of the new lighting >= the old arm's 0.62, while the peak is
  brighter), read out of a capture log rather than recomputed from source.
- `check-terminator.py` + `terminator.txt` -- the terminator sweep: the old arm is flat
  over 0.508 of the arc with a 0.00332 slope jump at the clip; the new one is flat over
  0.119 and its largest slope step is 0.00008 (44x smaller).
- `measure-normal-dot-light.py` + `normal-dot-light-bins.txt` -- model luminance delta
  binned by N.L against the map's sun.
- `measure-pick-crops.py` -- how the crop rectangles were chosen (mask fill x N.L spread),
  so the crops are a measurement and not a framing.
- `crops.txt`, `crop-A-near-pair-1to1.png`, `crop-B-doorway-1to1.png`,
  `crop-C-both-1to1.png` -- the two required 1:1 crops plus a wider two-body panel, each
  OFF on the left and BASIC on the right, with the region readings beside them.
- `SCOPE-AND-HEADROOM.md` -- that `--lighting basic` moves only model/prop pixels
  (containment against the normal-trace mask), and what the change costs in blown
  highlights.

## Not in this directory

`verify-iso.sh` seeds an isolated settings directory for each gate run, so the gates that
run the product were run here rather than having their scratch trees committed:
`argument-refusal-check.sh`, `class-pixels-check.sh` and `tools/warn-check.sh` all
passed on this tree; `lighting-semantics-check.sh` and `lighting-dark-account-check.sh`
cannot run on this host at all -- both hardcode a demo (`$TF/demos/autorecord_2026-07-02_13-26-46.dem`)
that does not exist in the installed tf tree, and neither has an override. Their failure
is a missing prerequisite, not a reading about this change.
