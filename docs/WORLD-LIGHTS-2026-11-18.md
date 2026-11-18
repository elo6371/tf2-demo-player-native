# The map's lights: 204 lamps and one sun, parsed from the BSP and measured on two cameras

Date: 2026-11-18
Branch: `work/integrate`

Until this card the world was lit by one constant: a hardcoded sun direction
`(0.35, -0.45, 0.82)` scaled by `lerp(1.0, 0.62 + 0.38 * N.L, 1)`
(`native_renderer_frame.cpp`, the `lightDirectionAndMode` constant). The map's
own light entities were never read -- `light_environment`, `light`, `light_spot`,
`sky_camera` had zero hits in `native/`. This card parses them and feeds them to a
new lighting path behind `--world-lights on`, which is **off by default** because
every pinned capture in the tree was made with the old arithmetic.

## What the map says

Entity lump 0 of `pl_upward_f12.bsp` is LZMA-compressed; decoded, it holds 1106
entities. The light entities:

| classname | count |
| --- | ---: |
| `light_spot` | 151 |
| `light` | 53 |
| `light_environment` | 1 |
| `env_sun` | 1 |
| `shadow_control` | 1 |

`worldspawn` names the sky `sky_upward`. `light_environment` says
`_light "253 242 208 1000"`, `_ambient "159 174 185 450"`, `pitch -55`, yaw 325 --
i.e. a sun at 55 degrees above the horizon from the north-west, warm, and a cool
sky ambient. The parser turns that into a surface-to-light direction
`(cos(elevation)cos(yaw), cos(elevation)sin(yaw), sin(elevation))` =
`(0.4698, -0.3290, 0.8192)`, which is the same octant as the constant it replaces.

`sky_camera` is parsed too (origin `7168 -2048 0`, `scale 16`, fog end 20000) but
nothing draws it yet; that is the next card.

## Where it lives

- `native/include/bsp_map.h`: `BspLight` (spot flag, origin, direction, colour,
  intensity, cone, inner cone, exponent), `BspSun` (present, direction, colour,
  intensity, ambient colour and intensity, pitch, yaw), `BspSkyCamera`; `BspMap`
  carries `sun`, `lights`, `lightsDeclared`, `lightsRejected`, `skyCamera`.
- `native/src/bsp_map.cpp`: the same walk the fog block already does, right after
  it. `_light` is `"r g b brightness"`; a light with no origin or a malformed
  `_light` is counted in `lightsRejected` instead of being guessed at. Spot
  direction is `(cos(pitch)cos(yaw), cos(pitch)sin(yaw), -sin(pitch))`.
- `native/include/native_renderer.h`: `WorldLightSource` / `WorldLightSet`,
  `Renderer::setWorldLights`, `worldLightsUploaded()`; `RenderSettings::worldLights`
  defaults to `false`.
- `native/src/main.cpp`: `--world-lights on|off` (strict: anything else is a
  refusal, rc 13, no frame) and the `lights: requested=... read=... sun=...
  point=... spot=... rejected=... source=bsp` setup trace. Brightness is
  normalised by this renderer, not by the map: a 300 lamp is intensity 1.0
  (`/300`, clamped 0..2), its falloff reaches zero at `brightness * 1.5` map units
  (clamped 48..1536), the sun uses `/1000` and the ambient `/1000`.
- `native/src/native_renderer_frame.cpp`: the world constant buffer grew from 192
  to 1280 bytes (four `float4` headers plus four 16-slot arrays). Each pane picks
  the sixteen lamps nearest its eye with a fixed-size max-heap, converts position
  and radius to the renderer's normalised box (`(p - centre) * worldHorizontalScale`,
  radius times the same scale) and uploads colours, cone cosines and the spot flag.
- `native/src/native_renderer_shaders.cpp`: the world pixel shader has two
  exclusive lighting paths. With `sunDirectionAndMode.w == 0` the old expression
  runs untouched, so an off frame is the frame every pinned capture knows.
- `gpu: world lightmap status=active intensity=0.634 ... vertices=200121
  lightmapped=199941` -- 99.9% of world vertices carry a lightmap, which is what
  makes the rule below load-bearing.

## The rule the first attempt got wrong

The first version added the parsed sun and ambient on top of the lightmap product.
That counts the same light twice -- VRAD already baked that sun into the lightmap --
and the measurement said so loudly: **95.1% of pixels changed** (818,620 of
860,784), mean absolute delta 34, max 175, the sand went from 210/255 to 118/255,
and every BSP triangle of the curved dune turned into a visible facet, because the
lightmap had been hiding the faceted geometric normal.

The shipped rule:

- a **lightmapped** face starts at `1.0` and receives only the lamps (a lightmap
  cannot contain a dynamic lamp);
- a face with **no lightmap uv** (the entity pass writes `lightUv = (-1,-1)`) takes
  the sun and ambient term, and for those faces the new term is only allowed to
  raise the old approximation (`max`, not replace): replacing it turned the
  corridor's white wall lamps into grey blocks, e.g. pixel (263,415) went from 194
  to 54 because those lamp bodies face away from a sun 55 degrees up.

Lamp contributions are added after that base, so a lamp lights a lightmapped wall
as an addition -- which is what a dynamic light is.

Two consequences a reader should not have to derive from the numbers: on a
lightmapped face the on frame is brighter wherever the old `0.62 + 0.38 * N.L`
was below 1.0, because the bake replaces that approximation -- that lift is most
of the 11% / 9% below, and the lamps are the remainder; and on an unlit face the
sun/ambient term only shows where it exceeds the old approximation, which is why
the outdoor mask lights up the characters and the wooden panels (no lightmap uv)
and not the sand.

## Measured, final

Same exe, same demo, `--capture-tick 1`, 1264x681, only `--world-lights` differs:

| camera (`--debug-camera`, eye then pitch then yaw) | differing | fraction | mean abs | max |
| --- | ---: | ---: | ---: | ---: |
| `500 -2240 240 17.8 127.97` (outdoor) | 98,473 / 860,784 | 11.44% | 0.90 | 53 |
| `-373.5 -899.8 443.7 0 0` (corridor) | 77,798 / 860,784 | 9.04% | 0.32 | 97 |

The corridor pair is the lamp evidence: every changed pixel got **brighter**
(14,894 positive, 0 negative), the brick wall next to the camera barely moves
(mean 0.11 -- lightmapped faces must not move), and the largest deltas sit in the
lamp-lit hallway behind the doorway, e.g. (462,337) `(161,158,150) -> (255,255,228)`
and (460,342) `(169,133,82) -> (255,212,123)`: warm, saturated, clamped at the 2.0
ceiling. The outdoor pair's near sand (rows 500 and 650) does not move at all.

`capture: lights enabled=1 sun=1 uploaded=16 declared=204 rejected=0
sunIntensity=1.000 ambientIntensity=0.450` (with `--world-lights off`:
`enabled=0 uploaded=0`).

## Reproduce

```
bash .scratch/n21-lights/capture.sh                       # outdoor pair, .scratch/n21-lights/lights-{off,on}
PREFIX=corridor CAMERA='-373.5 -899.8 443.7 0 0' bash .scratch/n21-lights/capture.sh
python .scratch/n21-lights/diff.py <off.bmp> <on.bmp> [mask.png]
```

Paths handed to the native exe must be **relative** (the script `cd`s to the
worktree root first). An MSYS absolute path in `TF2_NATIVE_TRACE`,
`--capture-frame` or `--settings-file` cannot be opened by the exe, which raises a
modal dialog and sits at 5 s of CPU and 2.4 GB of working set until the timeout
kills it -- no trace, no BMP. The settings file is always a copy: the run writes
its settings back.

## Limits, stated so the next card does not have to rediscover them

- No shadows: neither the sun nor a lamp casts one. A lamp lights through a wall
  if the wall is inside its radius and facing it.
- Sixteen lamps per pane, nearest to the eye, no culling by PVS or by visibility
  of the lamp itself.
- The spot cone is a `smoothstep(cosOuter, cosInner, dot)` raised to
  `max(exponent, 1)`; Source's `_exponent` is not that quantity.
- No light styles, no animated lights, no `env_projectedtexture`, no
  `shadow_control` mapping.
- Brightness divisors (1000 / 300) and the radius rule belong to this renderer,
  not to Source; they are the reason a 300 lamp reads at full strength here.
- `sky_camera` is parsed but not drawn; water reflection/refraction remains
  unavailable.
