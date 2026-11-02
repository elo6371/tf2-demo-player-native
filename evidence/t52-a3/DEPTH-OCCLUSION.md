# The depth view was a mask; the occlusion claim needs the buffer

T52 A3, 2026-11-02. Section 5 of `outline-check.sh` ("depth awareness: the
occluded region, three ways") was the one reading this gate still carried as
NOT VERIFIED. It had two independent causes, and fixing one would not have
moved the other.

## Cause 1: the fixture camera saw nobody behind geometry

The pinned camera (eye 5699 -1611 150, pitch 14.4, yaw -143.3, tick 64250) looks
at the red and blue clusters in the open. The depth image was valid, the item
rows were valid, and no drawn player pixel was behind world geometry -- so the
occluded region was empty and the assertion was correctly skipped as vacuous.

A scan of 8 positions at the same tick found one that is not in the open: eye
5138 -1130 10, pitch 2.5, yaw -90 (on the ground, ~900 units from the clusters).
It sees 7 items; six reds are in plain sight and one, blue entity 8, is behind
the map's own geometry. Measured on three frames of that fixture
(`/d/_shared/t52-a3/depthfix/`):

| item | box | testedInk | xrayInk | offInk |
|---|---|---|---|---|
| red 2 | 477,341,526,353 | 548 | 548 | 0 |
| red 3 | 450,360,496,368 | 291 | 291 | 0 |
| red 4 | 477,341,528,351 | 507 | 507 | 0 |
| red 7 | 477,339,527,354 | 600 | 600 | 0 |
| **blue 8** | **665,334,690,340** | **0** | **143** | 0 |
| red 11 | 476,341,526,352 | 528 | 528 | 0 |
| red 13 | 510,342,536,356 | 311 | 311 | 0 |

"testedInk 0 with xrayInk 143" is the depth test doing its job: the x-ray frame
paints the player through the wall, the depth-tested frame paints nothing, and
the overlay-off frame proves the map itself draws no team ink there.

## Cause 2: the 8-bit depth view cannot resolve a wall from a player

`--world-depth` renders the world pass with the diagnostic shader mode 5, which
writes `position.z / position.w` into all channels. The full-pixel histogram of
that image is three values: 255 (633,720 px), 0 (227,021 px), 17 (43 px, the
crosshair's blue channel). Everything past a few world units saturates, and
that saturation is a *measured property T26 asserts*
(`world-geometry-hole-check.sh`: "the debug depth view is 8-bit and saturates
for everything past a few units", `saturated 1`), so it cannot be re-encoded
without breaking that gate.

The occlusion question needs the opposite: a wall ~900 units from the camera in
front of a player ~1200 units away, i.e. two NDC depths about 0.0027 apart --
45,000 steps of a 24-bit buffer, 177 steps after 16-bit quantisation, and zero
steps in the saturated 8-bit view.

## The fix: `--capture-depth` writes the depth buffer, 16-bit

The renderer already reads the depth buffer back at capture time for the face
trace (`Renderer::captureDepthBuffer(false)` in
`native/src/native_renderer_capture.cpp`, D24_UNORM_S8_UINT, value =
`(row[x] & 0x00FFFFFF) / 16777215.0f`, taken before the view model and HUD
clear depth). `--capture-depth <path.pgm>` writes that same buffer as a 16-bit
P5 PGM, top-down, no row padding:

* `native/include/native_renderer.h`: `requestFrameCapture(path, depthPath)`,
  `lastDepthCaptureSucceeded()`, `lastDepthCaptureBytes()`,
  `writeWorldDepthToPgm()`.
* `native/src/native_renderer_capture.cpp`: the writer. 1.0 is the cleared far
  value and an unmeasured entry reads as far, so a hole can never claim to
  occlude anything.
* `native/src/native_renderer_frame.cpp`: writes it after the BMP, from the
  depth captured for the same frame.
* `native/src/main.cpp`: `--capture-depth` (needs `--capture-frame`; a depth
  path without a frame is invalid, exit 13), a failed depth write makes the
  capture exit 16, and the summary line carries
  `capture_depth_written=1 capture_depth_bytes=N`.

`--world-depth` is unchanged: same 8-bit saturated image, same semantics, T26
still asserts its saturation. This is a second, optional artefact.

## Measured on the new fixture (tick 64250, camera above)

    capture: armed requested=64250 frame_tick=64250 frames=4670
    mode: summary ... capture_written=1 capture_depth_written=1 capture_depth_bytes=1721568
    wd.bmp 2582406 bytes / wd.pgm 1721586 bytes

    16-bit:  bytes=1721586 width=1264 height=681 distinct=3896 min=61090 max=65535
    8-bit:   bytes=2582406 nonGreyPct=0.00 nonzeroPct=66.75 uniqueSampled=2

    occluded: entity=8 team=blue occludedPixels=182 rect=665,334,690,340 (182 px)
    three-way: occluded_xray=143 occluded_tested=0 occluded_tested_diff=0 region=26x7
    VERDICT PASS

Orientation check (top rows are the far plane, bottom rows the near ground):
top rows 1.0000, middle row 0.9899, bottom rows 0.9337 -- the PGM is top-down,
so `outline-regions.py` reads it with `flip=False`.

The occluded rectangle is the player's own published box, 26x7 px, every pixel
of it proved occluded by the buffer; 143 team-coloured pixels of the x-ray frame
fall inside it and none of the depth-tested frame do.

## Reproduction

    bash outline-check.sh --mutation      # section 5, under a host lock

or, by hand, with the product built from this tree:

    native/build-nmake/tf2_demo_native.exe --tf-root "<tf>" --hidden-window \
      --audio-disabled --demo <demo> --capture-tick 64250 --playback-speed 8 \
      --capture-frame wd.bmp --capture-depth wd.pgm --outline-vertex-extent \
      --debug-camera 5138 -1130 10 2.5 -90 --no-player-animation \
      --outline off --world-depth
    python outline-regions.py --pgm-stats --depth-pgm wd.pgm
    python outline-regions.py --trace <xray run>.trace --occluded --depth-pgm wd.pgm
