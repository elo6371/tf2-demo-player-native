# T10 Visual Correctness Baseline -- staged diagnosis and delivery

Reference recording: `tf/demos/autorecord_2026-07-02_13-26-46.dem`, map
`pl_odyssey.bsp`, first-person observer camera, capture tick 2000,
1264x681. All numbers below are from the product's own trace, the geometry
probe, and `frame-region-stats.py` -- no assertion in this task rests on an
eyeball.

## 0. What the release blocker looked like

The reference frame drew the ViewModel weapon and props correctly and the
world as a chaotic mix of shards, flat blocks and voids. The task's rule: fix
the first root cause the evidence points to, per region, and open separate
tasks for the independent problems instead of stacking pipeline changes.

## 1. Stage one: same POV, same tick, player camera vs diagnostic top-down

The diagnostic camera (`--debug-camera x y z pitch yaw`) was specified for
exactly this comparison. Mid-task it was found to have never worked: the
one-shot apply after the world upload was cleared by the first frame's
camera-mode transition, and every earlier "top-down" reading was the default
orbit camera. Proof: three different `--debug-camera` positions captured
byte-identical frames (md5 `7334906...` x3, traces `trace-camA/B/C.txt` in
the run scratch), and two positions on the same build differed only before
the mode transition could fire.

**Fixed** (commit in this branch): the diagnostic camera is applied every
frame, idempotently, next to the camera decision; the mode transition no
longer clears it. Verified: two positions produce two different frames
(`frames/camA2.png`, `frames/camB2.png`).

## 2. Stage two: the counters

Geometry (probe, `analysis/probe.txt`):

    triangles=99612 faces=18052 displaced=47232 water=300 lightmapped=37000
    degenerate=4790 maxEdge=1559.8 over256=13412 over1024=337 over4096=0
    displacement faces=1392 triangles=47232
    materials distinct=108 atlasCap=64 trianglesCoveredByCap=98311 of 99612 (98.7%)

Material lookup (product trace, `analysis/trace-ref.txt`):

    gpu: world material-fetch distinct=108 tiled=57 capped=0 vmtMissing=29
    vmtNoBase=20 vtfMissing=2 vtfTooLarge=43 decodeEmpty=0 maxWidth=1024
    maxHeight=2048 fromPakfile=23 downscaled=43

512-texture filter: the rejection counts exist and are pinned -- 43 vtf
sources above the cap were downscaled into tiles, 0 were silently dropped
(`capped=0`). The cap itself was NOT changed; the task forbids that without
rejection-count evidence, and the evidence is now on file.

Atlas draw / world draw:

    gpu: world vis ... inputTriangles=99612 drawnTriangles=63314
         clipApplied=1 state=active
    gpu: world cull backfaces=1 mode=cull-back
    gpu: world geometry displacedTriangles=47232 waterTriangles=300

Geometry-vs-material separation: the 2x2 capture matrix (vis x cull, all four
frames in `frames/`) separates the questions -- with VIS off and culling off,
background-colour pixels are triangles that never reached the screen at all,
so the residual voids are a geometry/pipeline question, not a material one.

## 3. Stage three: per-region attribution

| region | verdict this task |
| --- | --- |
| world | two root causes found and fixed (below); one further defect confirmed and handed to its own task |
| player entity | props and player-world models draw (`weaponEntity=1008`, markers visible in every frame); no change required this round |
| hands | unchanged: the c_sniper_arms decode carries one range of 600 vertices for a real multi-mesh model -- the named defect handed over by the animation-binding task; own task |
| weapon | draws correctly (`weaponVertices=12357`, `weaponDrawCalls=24`, `weaponVisible=1`); no change required |

### World root cause #1 (fixed): the pvs clip clustered the wrong point

`Renderer::observerFocusWorld()` returns the observer focus in the world
buffer's NORMALIZED unit-box space (x,y in [-0.9,0.9]-ish, z in
[0.1,0.9]). main.cpp handed that point straight to
`BspParser::clusterAt(displayMap, ...)`, which walks leaves in MAP units.
Every camera therefore clustered to whatever leaf covers (±0.4, ±0.9,
0.1..0.9) in map coordinates -- pl_odyssey answered cluster **2017** for
every position this task measured (also the long-standing "stale
cameraCluster=2017" note under --debug-camera) -- and the clip hid an
arbitrary 41500 of 99612 triangles from a place nobody was looking at.
Empirically: with the clip on, the frame was shards; with the clip off
(full-buffer draw), the same build showed a coherent room.

**Fix**: `Renderer::observerFocusMapWorld()` -- the exact inverse of
`setObserverFocusWorld`'s three lines, living next to them so the two cannot
drift. main.cpp's two eye fills use it, and the vis trace line now prints
`eyeMap=(x,y,z)` so the input is on the record next to the cluster it
produced.

**Verified**: cluster moved 2017 -> **1032** (the real camera leaf), PVS
popcount 352 -> **1021**, hidden 41500 -> **36298**, drawn 58112 ->
**63314**, and the probe independently computes cluster=1032 /
visClusters=1021 from the same eye -- two instruments agreeing. The gate's
M1 mutation reverts exactly this and must turn the eyeMap/cluster pins red
(measured: eye degenerates to `(-0.1,0.6,0.4)`, cluster back to 2017).

### World root cause #2 (fixed): the diagnostic camera (see stage one)

### World defect #3 (OPEN, own task): world completeness / displacement terrain

With VIS off and culling off (nothing may hide a triangle), the reference
frame still has large clear-colour voids, and the top-down captures show no
terrain albedo at all (the NATURE/BLEND materials -- rank 1/3/4 by triangle
count -- render flat blue-grey). Established on the way:

- The displacement decode arithmetic is consistent with the lump:
  dispinfo 1392 entries x 176 bytes; dispverts 727360 bytes = 36368 verts x
  **20** bytes exactly (powers: 1364x2, 28x3 -> sum of (2^p+1)^2 = 36368).
  Stride 16 and 28 are both impossible (727360/28 is not an integer).
- The winding data is ~97% uniform (raw census: 5416 of 16136 faces disagree
  with the side-aware plane rule), so no single global flip explains partial
  visibility; both net conventions were rendered and neither completes the
  world (frames archived under the run scratch, numbers in the gate log).
- The facing census from the real eye
  (`analysis/probe-facing.txt`): pvsKept front=7647 back=8435 -- a ~50/50
  split is NORMAL for a conservative pvs; this instrument cannot arbitrate
  winding, and did not.

The task's discipline was applied: this defect did NOT ship as part of the
eye-space patch, and the winding-orientation experiment (variant (c)) was
removed from the branch before delivery -- the shipped winding is byte-for-byte
HEAD's. The experiment's readings live in this document and the gate log.

## 4. The acceptance

`visual-baseline-check.sh` (repo root) pins:

1. the probe census (triangles, displaced, degenerate, atlas coverage, the
   eye's cluster and pvs popcount -- two instruments, one number),
2. the product trace (material-fetch counters, the full vis line including
   `eyeMap`, the cull line, gpu displaced/water),
3. measurable object presence in four world regions (mean luminance,
   spread, background fraction, gradient, each with a margin). W1/W4 carry
   presence; W2/W3 are pinned open-defect guards (the completeness defect
   leaves them mostly background) -- they fail on any reshuffle of what the
   world pass draws.
4. mutations: M1 reverts the eye-space fix (must redden eyeMap + cluster),
   M2 flips the world cull apply-site default (must redden the cull line);
   both sources must come back byte-identical.

## 5. What did not ship and why

- The 512 cap: unchanged (task rule; rejection counts now on file).
- The winding orientation experiment: removed (unproven benefit; would have
  been a second pipeline change riding the fix).
- Sky on pl_odyssey reads `state=unavailable ... reason=skybox draw disabled
  by the caller` although the caller passed no sky flag -- a lying reason
  worth its own small defect entry; not touched here.

## 6. Follow-up tasks opened

- **F1 world completeness / displacement terrain** (world region): the voids
  and the missing terrain albedo, with the stride verification and the
  winding findings above as starting evidence.
- **F2 hands/arms mesh decode** (carried from the animation-binding task):
  c_sniper_arms decodes one 600-vertex range; hands do not draw.
- **F3 pl_odyssey sky reading**: unavailable with a wrong reason word.
