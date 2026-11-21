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
  (containment against the normal-trace mask), what the change costs in blown
  highlights, and the two rejected arms with the numbers that rejected them.
- `VIEWMODEL.md` + `crop-vm-weapon-1to1.png`, `crop-vm-handgun-2x.png`, `vm-trace.log` --
  the same feature on the FIRST-PERSON weapon, which the pr600 tick never shows. A
  separate capture on the POV recording, its own mask rule (moved but not traced, and
  why that is not circular), the readings, and the model-space caveat the viewmodel
  carries.
- `gates/` -- the transcript of the one gate that could be run on this tree
  (`tools/warn-check.sh`), plus the list of translation units it compiled.
- The measurement scripts, copied verbatim so every number above can be re-derived from
  the four BMPs: `check-lighting-floor.py`, `check-terminator.py`,
  `measure-normal-dot-light.py`, `measure-pick-crops.py`, `t105-clip.py`,
  `t105-clipmap.py`, `t105-containment.py`, `t105-world-scope.py`, `t105-vmmask.py`,
  `t105-vmclip2.py`.

## Gates: what was run here, and what was not

`gates/warn-check.log` is the real output of `bash tools/warn-check.sh` on this tree:

    WARN-GATE tus=142
    WARN-GATE=PASS new=0 fixed=0 same=0

exit 0. It compiles the targets that carry /W4 with the flags CMake recorded -- 142
translation units belonging to 55 targets on this run -- and `tools/warn-baseline.tsv` is
empty apart from its own comment header, so that PASS means zero warnings across the
whole set, not "no new ones". `gates/warn-check-tus.txt` lists the units. That is the
brief's /W4 requirement with the transcript beside it.

`/WX` is the separate question and it is unaffected by this change: `native/CMakeLists.txt:96-103`
names the seven targets that compile with it (`tf2_core`, `tf2_demo_native` and five
probes) and `:130-131` applies it, so the product under review was built with warnings as
errors and this change did not touch that list. Nothing was relaxed to make it pass.

Two more gates launch the product and therefore take the host-wide lock at
`/d/_shared/tf2-hostlock/.lock`; another owner held that lock for this session's whole
window, so `argument-refusal-check.sh` and `class-pixels-check.sh` have NO transcript in
this commit. A `VERIFY-LOCK=FAIL ... exit=2` line from either of them is the lock
refusal, not a reading about this change -- do not read it as a failure of the feature.
What they would have asserted is covered elsewhere or untouched: the switch refusals are
in `native/src/main.cpp:5095` (an unknown `--lighting` value) and `:5104-5111`
(`--lighting-trace` without `--lighting basic`), both exit 13 before any frame, and the
animation drive this feature does not touch is what `class-pixels-check.sh` measures.

`lighting-semantics-check.sh` and `lighting-dark-account-check.sh` cannot run on this
host at all: both hardcode a demo (`$TF/demos/autorecord_2026-07-02_13-26-46.dem`) that
does not exist in the installed tf tree, and neither has an override. Their failure is a
missing prerequisite, not a reading about this change.
