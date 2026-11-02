# T52 depth view: a --world-depth capture is a scalar image again

Date: 2026-11-02. Owner: t52-a3. Tree: D:/TF2_Native_Worktrees/t79-integration.

## What was red

The outline gate's depth section (assertions 5 and 6) read, on the pinned camera at
tick 64250:

    bytes=2582406 nonGreyPct=26.35 nonzeroPct=100.00 uniqueSampled=34
    occluded region: <none>

`nonGreyPct` must be 0: the depth view writes one scalar into R, G and B, so a real
depth capture is grey by construction (`outline-regions.py:294-311`). The historical
PASS readings in `evidence/release-gates/outline-p0-clock.log` are
`nonGreyPct=0.00 nonzeroPct=16.71`.

## Two causes, both in the depth view's own output

**1. The tinted clear colour showed through where no geometry was drawn.** On a
1264x681 capture the non-grey pixels were 227,021 pixels of exactly (14,18,22) --
`(0.055, 0.07, 0.085) * 255`, the window clear colour -- concentrated where the world
pass drew nothing. The depth view now clears to black (`native/src/main.cpp`,
render loop: `clearR/G/B = worldDebugView == 2 ? 0.0f : ...`), so "no geometry" reads
as 0 depth instead of as a colour.

**2. The sky box is a colour pass and it was painting the map's sky into the depth
image.** The sky is drawn by its own pass, not by the world shader, so the depth
shader never touched it. Measured at tick 500 on the same camera:

| capture | nonGreyPct | distinct non-grey colours | dominant colour |
|---|---|---|---|
| sky drawn (default) | 27.61 | 4857 | (94,94,136), 18713 px |
| `--no-world-sky` | 0.00 | 1 | -- |

The product documents the view as "no texture/lightmap/bump/env sampling,
`--world-depth` draws it as greyscale depth" (`native/src/main.cpp:2690`), so a
colour sky in it contradicts the view's own contract. The sky pass is now forced off
when `worldDebugView == 2`, at the same place T12 already forces it off for the
terrain-only geometry filter.

## After the fix, at the gate's own fixture (tick 64250, eye 5699 -1611 150 14.4 -143.3)

    bytes=2582406 nonGreyPct=0.00 nonzeroPct=73.66 uniqueSampled=3
    nonGrey 43 distinct colours 1  -> all (106,84,17), the crosshair marker (0.005%)

The four gate readings now pass: exists (>= 100000 bytes), arrived at the tick,
scalar (0.00%), has geometry (73.66% not black), more than one sampled value (3).

## Scope

Only the depth view changes. Nothing else in the render path reads `worldDebugView`
for the sky, and no gate pins a depth frame:

* `world-geometry-hole-check.sh:819-825` states the depth control "cannot answer the
  behind/in-front question on this build: the debug depth view is 8-bit and saturates
  for everything past a few units", and that it "is not used as evidence";
* its two frame pins (`PIN_TEXTURED_SHA`, `PIN_REF_SHA`, lines 199-200 / 738-739) are
  the textured and reference COLOUR frames, which this change does not touch;
* `--world-geometry sky --world-depth` (T26's sky-depth capture) is unaffected: the
  sky BOX pass is already forced off for every non-`All` geometry filter
  (`worldGeometry == WorldGeometryFilter::All` in the same expression), and the sky
  BRUSHES it measures are world-pass geometry, not this pass.

## Reproduce

    cd D:/TF2_Native_Worktrees/t79-integration
    bash _shared/hostlock.sh run <owner> bash -c '
      TF2_NATIVE_TRACE=/d/_shared/t52-a3/depthcap2/wd.trace \
      native/build-nmake/tf2_demo_native.exe \
        --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
        --hidden-window --audio-disabled \
        --demo D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem \
        --capture-tick 64250 --playback-speed 8 --capture-frame /d/_shared/t52-a3/depthcap2/wd.bmp \
        --outline off --world-depth --no-player-animation \
        --debug-camera 5699 -1611 150 14.4 -143.3'
    python outline-regions.py --depth-stats --depth /d/_shared/t52-a3/depthcap2/wd.bmp
