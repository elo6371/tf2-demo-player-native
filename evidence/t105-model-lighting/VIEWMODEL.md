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

`--lighting-trace` does not repaint the whole frame, and the reason it cannot is worth
stating precisely, because the obvious explanation (a flag flipped for the frame) is
wrong.

The probe is a per-pixel arm of the ENTITY/PROP pixel shader only. That shader's
lighting block is `cbuffer ModelLightingConstants : register(b2)`
(`native/src/native_renderer_shaders.cpp:168`), and inside the arm it returns the encoded
normal before any lighting arithmetic runs
(`native/src/native_renderer_shaders.cpp:594-596`). Three facts bound where that can
reach:

- The world pass binds `b0` alone -- `context_->PSSetConstantBuffers(0, 1, &worldConstants);`
  at `native/src/native_renderer_frame.cpp:1171` -- and this feature binds `b2` only
  inside the entity pass (`native/src/native_renderer_frame.cpp:1634`) and unbinds it at
  the end of that pass (`native/src/native_renderer_frame.cpp:1785`). No BSP draw ever
  sees a non-zero `lightingParams.x`, so `--lighting-trace` cannot recolour world
  geometry. That is also why the non-entity population is byte-identical between the
  basic and trace frames (`world-untouched.txt`: n=742988, mean 124.72, 0 clipped in
  both).
- The viewmodel pixel shader DOES receive the block -- `register(b1)`,
  `native/src/native_renderer_shaders.cpp:1213`, bound at
  `native/src/native_renderer_viewmodel.cpp:742` -- with `lightingParams.y` set to 1 on a
  trace run, but its only two arms test `lightingParams.x`
  (`native/src/native_renderer_shaders.cpp:1290` and `:1340`). There is no probe arm in
  that shader, so the flag arrives and is ignored.
- The standalone `--model` diagnostic path is the same shape: it binds the model pixel
  shader at `native/src/native_renderer_frame.cpp:2191`, and that shader's only arm tests
  `lightingParams.x` (`native/src/native_renderer_shaders.cpp:804`).

So "frame-global" is the wrong word for this flag twice over: only a few draws bind the
buffer, and of the shaders that bind it only the entity/prop one has an arm that reads
the probe. What the trace run actually paints is the entity/prop population, which on
this capture is all props -- `vm-trace.log` carries 16 `lighting-normal:` lines and every
one is a prop (`sign_gameplay01.mdl`, `cappoint_hologram.mdl`); the viewmodel never
appears in it.

That is what makes the weapon mask possible rather than circular: the weapon is the
population that MOVED with the arm and was NOT traced, precisely because the pass that
draws it has no probe arm to answer with. A pixel counts as first-person-weapon when
**basic differs from off** (it moved) and **trace does not differ from basic** (it is not
part of the traced population). Both frames come from the same executable and the same
tick, and neither test uses the answer it is measuring.

One consequence for the diagnostic path: `--lighting basic --lighting-trace --model` does
NOT emit normal-as-colour, because the shader it binds has no probe arm. It renders the
diagnostic model through the ambient cube applied in MODEL space, exactly as the
viewmodel does below.

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
mask on THIS map is far worse, and the cause is in the capture line four paragraphs up:
`sunIntensity=1.000`, and `intensity=1.500`, which is that 1.000 after
`kModelLightingExposure` (1.5) is multiplied in at
`native/src/native_renderer_frame.cpp:163`. The reference tick of the pr600 pin reports
`sunIntensity=0.600`, so its block scalar is 0.900. The same cube therefore hands this
map 1.500 / 0.900 = 1.67x the sun it hands the reference tick. The rocks in
`vm-basic.png` are visibly washed out.

This is the headroom limit of a six-face cube with no tone curve, and it is a function of
the MAP's sun, not of the shader: the reference tick pr600 pin measures 2.251% clipped
over its model mask, this brighter map measures 64% over its prop mask. The exposure
cannot simply be lowered -- it is pinned from below by the floor requirement (no surface
direction may be darker than the old arm's 0.62), which is what `check-lighting-floor.py`
asserts and what 1.5 is the solution to. Closing this needs a tone curve or a prefiltered
environment, i.e. lighting Step 2. Recorded here so the trade is visible rather than
discovered by the next person who captures an outdoor map.

## The viewmodel's cube is applied in MODEL space, and that has a measurable cost

The comment promised this paragraph
(`native/src/native_renderer_shaders.cpp:1207-1212`), so here it is, with the mechanism
and the cost.

The entity pass rotates each normal into world space before it reaches the pixel shader
(`native/src/native_renderer_shaders.cpp:1059-1061`), which is why the pin's trace proves
world space for THAT pass. The viewmodel vertex shader does not rotate: it writes
`output.normal = normalLocal;` (`native/src/native_renderer_shaders.cpp:1177`). So for the
weapon the six cube faces and the sun direction are applied to the WEAPON's own axes --
the key light travels with the player's yaw instead of staying fixed in the world, and
the "sky" face is the weapon's +Z rather than up.

Measured consequence, which is why it was accepted rather than half-fixed here: the arm
is not an attenuator on this object either -- every weapon pixel on this capture got
brighter (39233 brighter, 0 darker, mean 52.41 -> 110.59), and the pass carries no
ambient occlusion or shadow term for a wrong-space vector to desynchronise from. What the
space error costs is the DIRECTION of the shading on one object, not its darkness, and on
a viewmodel the eye is nearly fixed relative to the weapon, so a light that rotates with
the view is close to a constant light on the thing being looked at. Fixing it means
passing a rotation into the viewmodel pixel shader; that is a small change and it belongs
with the tone curve in Step 2 rather than being half-done here.
