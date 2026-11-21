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
- `gates/` -- the transcripts of the three gates run on this tree
  (`tools/warn-check.sh`, `argument-refusal-check.sh`, `class-pixels-check.sh`), the
  list of translation units warn-check compiled, and a README saying what a transcript
  here means.
- The measurement scripts, copied verbatim so every number above can be re-derived from
  the four BMPs: `check-lighting-floor.py`, `check-terminator.py`,
  `measure-normal-dot-light.py`, `measure-pick-crops.py`, `t105-clip.py`,
  `t105-clipmap.py`, `t105-containment.py`, `t105-world-scope.py`, `t105-vmmask.py`,
  `t105-vmclip2.py`.

## Gates: what was run here

Three gates ran on this tree and each has its full output in `gates/`. They assert
nothing about the quality of `--lighting basic`; what they buy is that the surrounding
toolchain -- warnings, argument handling, the animation capture chain -- is where it was.

`gates/warn-check.log` is the real output of `bash tools/warn-check.sh`:

    WARN-GATE tus=142
    WARN-GATE=PASS new=0 fixed=0 same=0

exit 0. It compiles the targets that carry /W4 with the flags CMake recorded -- 142
translation units belonging to 55 targets on this run -- and `tools/warn-baseline.tsv` is
empty apart from its own comment header, so that PASS means zero warnings across the
whole set, not "no new ones". `gates/warn-check-tus.txt` lists the units. That is the
brief's /W4 requirement with the transcript beside it.

`gates/argument-refusal.log` is `bash argument-refusal-check.sh`, exit 0, ending:

    VERDICT argument-refusal: PASS (unknown '--' switches refuse by name with a non-zero
    exit; known and positional arguments unchanged)

Its four cases: `--world-flat=1` (a value attached to a switch that takes none) exits 13
and draws no frame; `--not-a-real-switch` exits 13 and draws no frame; a bare
`--world-flat` exits 0 and writes a frame; a positional word exits 0, which is D-23's
record-and-continue. This feature's own refusals ride the same path -- an unknown
`--lighting` value at `native/src/main.cpp:5095` and `--lighting-trace` without
`--lighting basic` at `:5104-5111`, both exit 13 before any frame -- so the gate covers
the shape of them even though it predates the switches.

`gates/class-pixels.log` is `bash class-pixels-check.sh` with
`T62_CLASS_OUT=.scratch/t62-class-pixels`, exit 0: `CLASS-PIXELS=PASS`, subject
`id=95 ent=5 model=models/player/demo.mdl state=moving seq=147` with 5337 pixels on
screen and 4040 of them differing between frames (75.70% share), and the identity capture
byte-identical when taken twice
(`67420d4db610b7c49266640ca6ec5d6d29e2cedb859ac6813638d5abf0eadd9f`). What it measures
is the animation drive, which this change does not touch; the product sha256 it reports,
`a052de0875f38a38c5119ded0d17f29619997857fd481960791d46ccb29fd9b1`, is the binary built
from the commit this evidence belongs to.

Both of the last two launch the product and take the host-wide lock at
`/d/_shared/tf2-hostlock/.lock` once per capture, which is why each log shows a
`VERIFY-LOCK=ACQUIRED` / `VERIFY-LOCK=RELEASED` pair per case and why they were the last
artefacts to land: each had to queue behind other owners. An earlier attempt lost the
lock and printed `VERIFY-LOCK=FAIL ... exit=2`; that is a retry signal, not a reading,
and it is not committed here as a result.

`/WX` is the separate question and it is unaffected by this change: `native/CMakeLists.txt:96-103`
names the seven targets that compile with it (`tf2_core`, `tf2_demo_native` and five
probes) and `:130-131` applies it, so the product under review was built with warnings as
errors and this change did not touch that list. Nothing was relaxed to make it pass.

`lighting-semantics-check.sh` and `lighting-dark-account-check.sh` cannot run on this
host at all: both hardcode a demo (`$TF/demos/autorecord_2026-07-02_13-26-46.dem`) that
does not exist in the installed tf tree, and neither has an override. Their failure is a
missing prerequisite, not a reading about this change.

## `--lighting basic --model`: what that combination does

The brief did not ask about the standalone `--model` diagnostic path, but its pixel shader
is the sibling of the viewmodel's, so the combination is stated here instead of being left
for a reader to trip over. It is out of scope for this card in the sense that nothing was
tuned for it; what follows is what the code does.

The pass binds `modelPixelShader_` (`native/src/native_renderer_frame.cpp:2179`), which is
compiled from `modelPixelSource` (`native/src/native_renderer.cpp:266`, created at `:458`),
and it binds the lighting block at PIXEL b1
(`native/src/native_renderer_frame.cpp:2187-2191`) -- vertex b1 is a different stage's slot,
as the comment there says. So `--lighting basic` does light the diagnostic model, with the
reference tick's amplitudes: that run prints `sun=(0.500,-0.866,-0.000) intensity=0.900`,
which is the 0.600 sun with `kModelLightingExposure` multiplied in.

`--lighting-trace` does NOT change that frame. The probe arm exists only in the WORLD
pixel shader's entity/prop branch (`native/src/native_renderer_shaders.cpp:594-596`); the
model pixel shader's only arm tests `lightingParams.x` (`:804`), exactly as the viewmodel's
two do (`:1290`, `:1340`). `lightingParams.y` occurs in three places in the whole tree --
the entity arm at `:594`, a comment at `:803`, and the capture print at
`native/src/native_renderer_frame.cpp:960` -- so on a `--model` frame the probe flag arrives
and is ignored. The model is lit through the ambient cube in MODEL space, for the same
reason as the viewmodel: the model vertex shader does not rotate the normal.

That is asserted from the source above, not from a capture, and the reason is worth
recording: on tick 600 of the pr600 recording, `--model models/player/demo.mdl` produced
the same three md5s as the bare demo run (`2ddade35...`, `d927c526...`, `3f39236e...`), so
no model pixels reached those frames and the pair cannot discriminate either arm. A
capture that could would need the standalone model framed on its own.

## Tying these pins to a tree

The pins above are md5s of BMPs, and a reader is entitled to ask which commit, built how,
produced the bytes that made them. The answer is the build-identity sidecar beside the
executable, `native/build-nmake/tf2_demo_native.identity`, written by
`native/cmake/write-identity-sidecar.cmake` from the provenance that
`native/cmake/gen-build-stamp.cmake` records at build time (the `tf2_build_stamp` and
`tf2_identity` targets in `native/cmake/tf2-version.cmake` and `native/cmake/tf2-identity.cmake`).

It is a build product, not a tracked file: it describes one set of bytes on disk, so
committing it would be committing a claim about a binary the repository does not carry.
The fields that matter for a pin:

    sourceTree=<commit>      git HEAD at build time
    dirty=0 dirtyFiles=0     the worktree at build time
    sourceContent=<hash>     contentMethod=git-clean-v1: a hash of the tracked inputs the
                             two executables compile from, so it is equal on any tree with
                             the same sources and changes when one of them does
    exeSha256=<hash>         file(SHA256) of the executable as it lies on disk
    contentBinding=recompiled  the hash was compiled into these bytes, not carried forward

So the chain is: the sidecar names the commit and says the tree was clean; it gives the
sha256 of the executable that was run; every pin row above names the capture command that
ran that executable. If the sidecar names a commit other than the one a reader is
holding, the pins in this directory do not belong to that tree and must be re-taken --
which is exactly what happened here once, and why the sidecar is quoted rather than
described.
