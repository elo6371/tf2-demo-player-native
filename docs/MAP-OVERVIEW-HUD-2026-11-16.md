# Map overview HUD (E11) -- 2026-11-17

The spectator screen gets a real map: a 250 px square in the top-right corner,
showing the map's own supplied top-down render with a marker per player, plus
the two team rosters down the left and right edges. It replaces the 160 px
schematic panel while it is open (the schematic panel is still there, with its
BSP linework, when the overview is closed).

Everything below is a fact about this tree, not a plan.

## What it draws

| element | source | fallback when the source is missing |
| --- | --- | --- |
| panel frame and map area | `kPanelSide = 250`, `kBorder = 2`, `kMapInset = 2` (`native/src/map_overview.cpp:33-60`) | the panel shrinks (see "small windows") |
| base image | one PNG from the supplied package (`<overview-root>/images/<map>.png`), decoded by WIC into a single RGBA8 texture with a full mip chain | `loaded=0` with a published reason and a `NO BASEMAP` placeholder -- never a guessed image and never a blank square |
| player markers | `m_vecOrigin` from the entity stream, projected with the published affine | a dead/position-less player is counted in `iconsSkipped` (and `iconsDead` / `iconsNoPos`) and drawn nowhere |
| class portraits | `materials/vgui/hud/class_icons/...` VMT -> VTF, decoded and fitted into a 16 px chip | the chip is filled with the team colour and the class's two-letter code (`chips`) |
| facing arrow | `m_angEyeAngles[1]` (else `m_angRotation`) | no arrow, and the trace counts it |
| team rosters | `m_iTeamNum`, `m_iHealth`, `m_iClass`, userinfo table for names | a row with no health reads "?" rather than a number |
| dead rows | `player_death` / `player_spawn` ticks in the demo's game-event stream | the row says DEAD with no countdown instead of inventing one |

Rows show the chip, the name (ASCII, 18 chars), the class name, the health
right-aligned, a green `+overheal` when health is above the class maximum, and a
4 px health bar whose fill is clamped to [0, 1].

## The base image is one textured quad, not a mosaic

The first cut of E11 drew the base image as a cached run-length mosaic of filled
rectangles, because the HUD pass bound exactly one texture (the 8x8 font atlas).
That is gone. `Renderer::setHudImageTexture` (`native/src/native_renderer.cpp`)
now owns a second SRV/SRV-sampler pair and the HUD pass draws the image through
`hudImageView_`:

- `DXGI_FORMAT_R8G8B8A8_UNORM`, `MipLevels = 0`, bind
  `SHADER_RESOURCE | RENDER_TARGET`, misc `GENERATE_MIPS`;
- `UpdateSubresource` uploads the decoded pixels, then `GenerateMips` builds the
  chain, then the SRV is swapped in (`D3D11_FILTER_MIN_MAG_MIP_LINEAR`, clamp);
- a same-size update reuses the texture and rebuilds mips instead of recreating;
- the sampler name and the mip count are published to the gate
  (`hudImageSamplerMode()`, `hudImageMipLevels()`).

One `HudPrimitiveKind::Image` primitive carries the whole basemap (uv 0,0..1,1,
white, filled). The panel contains it in the boundary rect with its own aspect
ratio, so the art is never stretched: `textureX/Y/W/H` is the art quad and
`rect` is the world-boundary rectangle the markers use. The difference between
the two is measured, not hidden: `aspectDev` is the PNG/boundary aspect
deviation in percent. Five of the six supplied maps sit at 0.003-0.018%;
`pl_upward` is 1.093% because the supplied art carries bottom padding
(`pl_upward.png` has non-black content only down to y=891 of 1107). That is a
property of the supplied render, and the trace prints it. `map-overview-check.sh`
asserts the exact-map (cp_process) deviation is under 1%.

## Why the base images are supplied, not rendered

The user's instruction for this pass is explicit: fix the map materials first,
use the images they supplied for now. The pipeline is now a read-only lookup:

    E:/demo测试/demos_tf_maps/boundaries.json   (per-map world bounds, x/y min/max)
    E:/demo测试/demos_tf_maps/images/<map>.png  (the supplied top-down render)
    E:/demo测试/demos_tf_maps/mapview.js         (the package's own viewer, unused by the product)

`--overview-root <dir>` points at that package (a `--overview-image <path>`
override exists for one-off runs). The product decodes PNG through Windows
Imaging Component (`native/src/wic_image.cpp`, `CoCreateInstance` +
`IWICImagingFactory` + `IWICFormatConverter` to 32bppRGBA) and never parses
PNG by hand. The previous authoring step -- `tools/overview-import.py` turning a
PNG into `assets/overviews/<map>.bmp` -- has been retired along with the BMPs:
nothing in the tree remembers that format any more.

A decode is cached per map name for the life of the process, and the cache hit
still reports the originally paid `decodeMs`/`uploadMs`, so a gate reading the
second frame sees the same numbers as the first. `decodeImageFile` scopes its WIC
COM objects in an inner block and calls `CoUninitialize()` after they are
released -- the 2026-11-17 crash (`0xC0000005` inside `windowscodecs`, mip
`ComPtr` Release after `CoUninitialize`) was exactly that ordering.

## The map -> asset mapping

The demo header's map name is the key (`DemoHeader::mapName`; it is *not* in
`DemoNetworkSummary`). `native/src/map_basemap.cpp` resolves it against
`boundaries.json` with four rules, in order, and prints the winner:

| rule | example |
| --- | --- |
| `exact` | `cp_sultry_b8a` -> `cp_sultry_b8a`, `pl_upward_f12` -> `pl_upward` (the key is the trim) |
| `trim` | `pl_upward_f12` -> `pl_upward` |
| `reverse-trim` | `cp_reckoner` -> `cp_reckoner_b3a` |
| `reverse-trim-2` | `cp_process_f12` -> `cp_process_final`, `cp_gullywash_f9` -> `cp_gullywash_final1` |

`exact=1` means the rule was `exact`; `legacy=1` means the art is a different
map version and the panel stamps a `LEGACY MAP` badge. If nothing resolves, or
the PNG will not decode, the panel draws `NO BASEMAP` plus the reason and the
trace carries `loaded=0` -- the mapping block still runs, so the world bounds
and the affine are still known.

Adding a map: add its key and bounds to `boundaries.json` and its PNG to
`images/`. No native code changes.

## Geometry, anchoring and the no-overlap rule

- The panel is anchored top-right at a constant **pixel** margin: `side =
  min(250, W-24, H-24)`, `x = W-12-side`, `y = 12`. A resize therefore never
  moves it in fractions of the window and never resizes it while it fits. At
  1264x681 the client area puts it at `1002,12`; at 1904x1041 at `1642,12`.
- The map area is the panel inset by the 2 px border: `side-8` = 242 px.
- The left (RED) roster starts at `(12, 12)`. The right (BLU) roster starts at
  `x = W-12-width`, `y = minimapBottom + 12`, i.e. **below** the minimap. This is
  deliberately asymmetric: stacking BLU under the map costs one roster's height
  and keeps both panels inside the safe area at 1280x720, which a 250 px map on
  the right edge cannot otherwise afford. `map-overview-check.sh` asserts the
  three rectangles (panel, RED, BLU) are mutually disjoint at 1280x720,
  1920x1080 and 2560x1440, and that each stays inside the frame.
- Roster width is `min(220, (W-36)/2)`; below 80 px wide the roster is skipped
  rather than drawn as a sliver.
- **Small windows**: the panel side is `min(250, clientW-24, clientH-24)`, so a
  short window shrinks the map instead of clipping it; below `kMinPanel = 60` px
  the code guard would skip the panel entirely (`reason=panel-too-small`, side 0)
  and the rosters clamp their rows instead of overlapping. That guard is not
  reachable through `--window-size`: the prescan (`native/src/main.cpp:2999-3027`)
  refuses an outer window below **320x240** with exit 13 -- "refuse, do not
  guess", because a gate that asked for one size and silently got another would
  produce incomparable captures. At the floor the client area is ~304x201, the
  panel is ~177 px and the run still composes, captures and writes a bitmap.
  `map-overview-check.sh` section D asserts exactly that, plus the 300x200
  refusal (exit 13, no bitmap).

## Keys: M toggles the map, Ctrl+M toggles mute

The 2026-11-17 user decision: **M** is the map overview key, and the master mute
moved to **Ctrl+M**. `registerKeybinds` (`native/src/main.cpp:2149`) registers
`hud.overview` on `parseChord("M")`; the hud dispatch arm (`main.cpp:2543-2546`)
claims it, toggles the panel and traces `overview: toggle open=0|1 source=key`.

Bare M can never reach the audio arm: the hud table claims it first and returns,
and the audio arm itself only accepts `wParam == 'M'` when
`GetKeyState(VK_CONTROL) & 0x8000` is set (`main.cpp:2638-2639`). The two
toggles are independent by two mechanisms, not by one guard.

`[` steps the volume down 10 points, `]` steps it up; a volume key while muted
unmutes first and then steps. Each key persists immediately
(`g_nativeUi->saveSettings()`) and traces `audio: master volume=%.2f muted=%d
source=key-ctrl-m|key-volume-up|key-volume-down`; the scheduler then reports
`audio: master applied volume=... muted=...`.

`audio-keys-check.sh` drives all of it with real posted key edges: bare M must
toggle the map and produce **no** `audio: master` line, Ctrl+M must mute and
produce **no** second overview toggle, `]` must unmute and step to 0.60, and two
restart reads must see the persisted file. The helper
`send-window-keys.ps1` synthesises the Ctrl state in the shared input queue
(`AttachThreadInput` + `GetKeyboardState`/`SetKeyboardState`) and holds it until
the process exits, because `PostMessage` does not update `GetKeyState` and the
product queues key edges while its load phase blocks the message pump.

## Trace contract (what a gate can read)

Every line is prefixed with an elapsed-ms token and is CRLF-terminated; a reader
must strip `\r`. `map-overview-check.sh` is the executable version of this
table.

    minimap: source=<key> exact=0|1 rule=<exact|trim|reverse-trim|reverse-trim-2> path=<png> loaded=0|1 reason=<token> w=<px> h=<px> format=DXGI_FORMAT_R8G8B8A8_UNORM sampler=linear-mip mip=<n> aspectDev=<percent> decodeMs=<ms> uploadMs=<ms>
    minimap: rect=<x,y,w,h> texture=<x,y,w,h> bounds=<minX,minY,maxX,maxY> scale=<s> offsetX=<px> offsetY=<px> legacy=0|1
    overview: frame open=1 ready=0|1 reason=<token> map=<map> panel=<x,y,side> border=2 area=<x,y,side> red=<x,y,w,h> blu=<x,y,w,h> rowsRed=<n> rowsBlu=<n>
    overview: mapping ok=0|1 reason=<token> bounds=<minX,minY,maxX,maxY> scale=<s> offsetX=<px> offsetY=<px> area=<x,y,side>
    overview: summary players=<n> shown=<n> hidden=<n> rowsRed=<n> rowsBlu=<n> skipped=<n> icons=<n> iconsSkipped=<n> iconsDead=<n> iconsNoPos=<n> iconsOffMap=<n> arrows=<n> classIcons=<n> chips=<n> deadRows=<n> images=<n> rects=<n> text=<n> circles=<n> hiddenEntity=<id>
    overview: toggle open=0|1 source=key      (only on the hotkey)

`images=1` is the assertion that the basemap is one textured quad, not a
`rects` mosaic. Marker accounting is `icons + iconsSkipped == shown` (a dead
player keeps its roster row and loses its marker) and
`classIcons + chips == icons` (a code chip is not a missing marker). A capture
tick can land on a dead player, so a gate must not assert `iconsSkipped == 0`.

The panel joins the readout's publish key only while it is open, and the closing
frame publishes a non-empty `overview-closed` signature so the last painted panel
is removed instead of being stranded by an unchanged publish key.

## Look

- Panel background is near-black (`kPanelBg` 23,23,28), border 2 px in
  team-neutral grey-blue; the map area is inset by 2 px inside it.
- Player chips are 16 px, one per player: team colour (RED 184,59,64, BLU
  84,133,168, dead 107,107,115), the portrait or the class code in white, and a
  3 px facing arrow outside the chip.
- The viewer (the entity the camera is actually on) is a 1 px white square, not a
  chip, so "who am I" is never confused with "who is that".
- A legacy map carries a `LEGACY MAP` badge: an 88x9 scrim at the art quad's
  top-left plus 4 px inset `kWarn` (235,107,102) text. The gate counts those warn
  pixels, so the badge cannot silently disappear.
- A missing basemap paints `NO BASEMAP` in `kWarn` at the area's top-left plus
  the reason in `kMuted` below it, and the area keeps its inset background -- the
  gate asserts the area is not a black block (black pixels <= 5%).

## What is honest-but-incomplete (do not read these as assertions)

1. The base images are the supplied renders, not geometry projected through the
   map's own BSP. A supplied render authored from a different camera convention
   would show control points offset from the artwork; the control points are
   still placed by the published bounds/scale and the trace's `minimap: rect`
   line prints them, so the disagreement is measurable (and `pl_upward`'s
   1.093% aspect deviation is exactly that kind of measurement).
2. Class portraits come from the install's own `class_icons` materials. In the
   demos.tf install this tree tests against the chain does not resolve, so every
   marker falls back to a two-letter code chip (`classIcons=0 chips=N`). That is
   the documented fallback, not a missing marker.
3. Dead countdown is only shown when the demo's own `player_death` tick exists
   for that player; otherwise the row says DEAD and nothing more.
4. Overheal is a reading, not a maximum: `+N` means health > the class's max.