# T105 model lighting -- the first-person weapon arm

Measured 2026-11-21 on the POV recording the viewmodel gates pin
(`E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem`, tick 0). The pr600
tick of the matcha recording used for the pin does not show a first-person weapon, so
the weapon shader needs its own capture; this is that capture.

Command, for all three frames, from the worktree root:

    ./native/build-nmake/tf2_demo_native.exe \
      --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
      --audio-device "" --demo "E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem" \
      --hidden-window --audio-disabled --capture-tick 0 \
      --viewmodel "models/weapons/v_models/v_scattergun_scout.mdl" --viewmodel-fov 90 \
      --start-paused --capture-frame .scratch/t105-vm/vm-<tag>.bmp [--lighting basic [--lighting-trace]]

| tag | extra switches | md5 of vm-<tag>.bmp |
|---|---|---|
| off   | (none)                             | `5860319352f90c31eb8de97b51cba965` |
| basic | `--lighting basic`                  | `adbf5d669c9b93986d0d63a6ae2fc38d` |
| trace | `--lighting basic --lighting-trace` | `afd02bbf37f453bbc0f57e20d50718f0` |

The `capture: model-lighting` line from each run (trace-off.txt / trace-basic.txt style,
here vm-off / vm-basic):

    off   capture: model-lighting arm=0 trace=0 sunPresent=1 sun=(0.819,-0.574,-0.000) intensity=1.500 sky=(1.314,1.315,1.224) ground=(0.997,0.988,0.906) side=(1.200,1.194,1.101)
    basic capture: model-lighting arm=1 trace=0 sunPresent=1 sun=(0.819,-0.574,-0.000) intensity=1.500 sky=(1.314,1.315,1.224) ground=(0.997,0.988,0.906) side=(1.200,1.194,1.101)

## The mask, and why it is not circular

On this tick the trace frame recolours the world as well as the models (the world pixel
shader reads `lightingParams` from a slot this feature binds, and the trace flips that
flag for the whole frame). So a "trace differs" mask cannot isolate the weapon.

The weapon mask below is therefore built from BOTH comparisons: a pixel counts as
first-person-weapon when **basic differs from off** (it moved) and **trace does not
differ from basic** (it is not part of the traced population). Both frames come from the
same executable and the same tick, and neither test uses the answer it is measuring.

Script: `.scratch/t105-vmmask.py` (copied here as `t105-vmmask.py`).

## The weapon is no longer dead black (the brief's own criterion)

Over the whole frame's weapon mask, 39233 px:

    off   mean=52.41  p10=18  p50=55   p90=76   dark<16=7.76%  dark<24=15.85%  min=3
    basic mean=110.59 p10=46  p50=120  p90=161  dark<16=0.12%  dark<24= 1.84%  min=10
    delta +58.18, brighter=39233, darker=0, same=0

Every single weapon pixel got brighter; none got darker. The `dark<16` share -- "dead
black" by the brief's word -- falls from 7.76% to 0.12%, a 65x reduction, and the
darkest pixel in the whole mask moves from 3 to 10.

## Two 1:1 crops with readings (brief requirement)

`crop-vm-weapon-1to1.png` -- region 740,470,110,120, **scale 1** (OFF left, BASIC right).
`crop-vm-handgun-2x.png` -- region 750,480,300,201 at scale 2, for the arm and glove the
1:1 window clips.

Readings for the 1:1 window (same mask rule, 2719 weapon px of 13200):

    off   mean=50.23 p10=13 p50=43  p90=99   dark<16=15.74% dark<24=29.83% min=4
    basic mean=117.31 p10=46 p50=87 p90=233  dark<16= 0.04% dark<24= 0.63% min=15
    delta +67.08, brighter=2719, darker=0

That window is the red sleeve and the black glove of the Scout's left hand -- the
darkest thing in the first-person composite. In the crop the glove's fingers and the
sleeve's underside go from unreadable to readable, and the sleeve keeps a gradient down
its length instead of a flat wash.

## Cost, stated: the same map over-exposes the model pass

`t105-vmclip2.py` over the same masks:

    weapon mask  clip_off=    0 (0.000%)   clip_basic=  1311 (3.342%)
    prop mask    clip_off=   86 (0.049%)   clip_basic=111488 (64.131%)

The weapon composite costs 3.3% of its pixels reaching 255 in some channel. The prop
mask on THIS map is far worse: this recording is a sunlit outdoor map whose own sun is
0.9 (intensity 0.9 -> the block's `sunDirectionAndIntensity.w` is 1.35 after the
exposure), where the reference tick's sun is 0.6 -> 0.9. The rocks in
`vm-basic.png` are visibly washed out.

This is the headroom limit of a six-face cube with no tone curve, and it is a function of
the MAP's sun, not of the shader: the reference tick pr600 pin measures 2.251% clipped
over its model mask, this brighter map measures 64% over its prop mask. The exposure
cannot simply be lowered -- it is pinned from below by the floor requirement (no surface
direction may be darker than the old arm's 0.62), which is what `check-lighting-floor.py`
asserts and what 1.5 is the solution to. Closing this needs a tone curve or a prefiltered
environment, i.e. lighting Step 2. Recorded here so the trade is visible rather than
discovered by the next person who captures an outdoor map.
