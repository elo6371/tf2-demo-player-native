# T79 -- the follow camera, and the trace that reported the request

Worktree `D:/TF2_Native_Worktrees/t79-integration`, branch `integration/t79-final`,
base HEAD `1204236`. Pixel proof input demo:
`bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`
(`TF=D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`), capture tick 2000, 1x playback.

## 1. Symptom

In a SourceTV recording the demo names no eye, so the first-person follow camera is
placed by the relay itself (`native/src/main.cpp:10047`). The trace line describing that
camera was written at the *decision* site, before the relay had placed anything, and the
`eye=` fields were fed only by a demo eye or by `--debug-camera`
(`cameraEyeX/Y/Z` initialised `0.0f`). On the KOTH sample the trace therefore said

    camera: summary mode=first-person ... follows=0 holdsNoTarget=445 freezeFrames=0
    FAIL trace camera followed observe targets = 0, expected >= 1

(`D:/_shared/t8/camera-ui-base.log:34` and `:41`, final verdict `CAMERA-UI=FAIL` at
`:125`) while the picture was already inside the followed player's head. Every frame
claimed `src=held eye=0.0,0.0,0.0` -- the world origin, a thousand units below the map.

This is the same defect class T84 fixed for `--debug-camera`
(`native/src/main.cpp:10198`): *the trace reports the REQUEST, not the camera the frame
was drawn with.*

## 2. Fix

1. `placeSpectatorView()` (`native/src/main.cpp:360-399`) -- the relay's placement is a
   function that returns the pose it applied through `outEye[3]` / `outAngles[2]`
   (nullptr-safe; the caller that only wants the side effects passes nothing). The
   placement and the record of the placement are now the same computation.
2. Relay guard (`native/src/main.cpp:10043`) now also requires `!g_debugCamera`: the
   diagnostic camera owns its own frames, and the old path would additionally hide the
   followed player on frames the debug camera was drawing.
3. Trace bookkeeping -- `cameraTraceEye[3]`, `cameraTracePitch/Yaw`, `cameraTraceMode`,
   `cameraTraceSource` are declared at `native/src/main.cpp:8522` and seeded from the
   decision-site pose at `native/src/main.cpp:8711-8713`; the relay overwrites them with
   the pose it actually applied (`native/src/main.cpp:10062-10067`, `src=follow`), and the
   single print sits after the relay and before `renderer.draw()`
   (`native/src/main.cpp:10085-10089`).
4. A frame the relay is about to place is no longer counted as a hold
   (`native/src/main.cpp:8696`): `holdsNoTarget` fell from 445 to 1. `follows` counts
   placements, so on the KOTH sample it reads 511 rather than the 676 drawn frames.

## 3. Pixel proof that the published eye is the drawn eye

`D:/_shared/camfix2/run-pix.sh`, log `D:/_shared/camfix2/run.log`. Three runs, same
command line, same tick:

| run | camera arguments | trace line the frame was drawn with |
| --- | --- | --- |
| R | none (relay follows) | `mode=first-person target=1 follows=1746 holds=1 freeze=0 eye=1185.5,-813.5,207.1 pitch=-6.71 yaw=118.59 src=follow` |
| D | `--debug-camera 1185.5 -813.5 207.1 -6.71 118.59` | `mode=free-look ... src=debug` eye 1185.5,-813.5,207.1 |
| D2 | `--debug-camera 0 0 1000 0 0` | `mode=free-look ... src=debug` eye 0,0,1000 |

All three runs captured at `frame_tick=2000`. Frame differences (`pixel-stats.py`,
exact per-pixel max channel delta over 860,784 pixels):

| pair | any >0 | >2 | >8 | % >8 | max delta |
| --- | --- | --- | --- | --- | --- |
| R vs D (same pose) | 161,389 | 6,899 | 638 | 0.074% | 195 |
| R vs D2 (origin pose) | 859,666 | 844,988 | 799,512 | 92.882% | 247 |

The 638 pixels above delta 8 in R vs D sit in 77 of the 220 64x64 cells, the largest cell
holding 54 of them -- consistent with per-frame animation phase on the viewmodel and
distant entities, not with a different lens. R vs D2 is a different picture everywhere.
**Before the fix the trace line R printed was byte-for-byte the D2 pose**, i.e. the gate's
own record of "where the camera was" described the world origin.

Artifacts: `evidence/t79-camera/shots/{R,D,D2}.png` (converted from the captured BMPs),
`maskRD.png` (R vs D difference mask), `pixel-stats.txt` (the readings above, produced by
`pixel-stats.py` in this directory).

## 4. Gates

`bash camera-ui-check.sh` -- host lock `camfollow-ui`, log `D:/_shared/camfix2/ui1.log`:
**CAMERA-UI=PASS, 108 OK / 0 FAIL**. The three assertions added with this fix
(`camera-ui-check.sh:353-365`) read:

    OK   trace camera followed observe targets = 511 (>= 1)
    OK   trace camera published the pose each followed frame drew with = 511 (>= 1)
    OK   no followed frame claims the world origin for an eye = 0
    OK   the published followed frame names the watched target = 1 (>= 1)

and the summary line now reads `follows=511 holdsNoTarget=1 freezeFrames=0`
(`ui1.log:34`).

One host lock, sequential, `D:/_shared/camfix2/focused-gates.log`:
`SPECTATE-CHECK=PASS`, `FRAME-CAPTURE=PASS`, `DEMO-CLOCK-CHECK=PASS`,
`OUTLINE-CHECK=PASS` (293 OK / 0 FAIL together).

## 5. What this does not claim

* At the time of this round the KOTH sample's map (`koth_bagel_rc13`) was not
  installed, so the camera-ui e2e ran against an empty world: it proved the trace, the
  counters and the HUD, not the picture. The picture claim came from the snakewater run
  in section 3. Section 6 re-runs the same evidence on `koth_bagel_rc11`, which **is**
  installed, so the e2e there does draw the real map.
* `outline-check.sh --mutation` was NOT RUN in this round, so the eight defects it
  reintroduces remain NOT VERIFIED by this evidence.
* The 638 differing pixels of R vs D are attributed to animation phase by their
  distribution; no individual pixel was traced to its cause.
## 6. Re-run on the shipped sample: koth_bagel_rc11 (2026-11-03)

The map the release sample actually ships with is `koth_bagel_rc11`, which is installed
on this machine (`download/maps/koth_bagel_rc11.bsp`, 26,370,688 bytes,
sha256 `f06cfe4b389da5ca152dff0702fb3f4d69b48bfc7b08ed9994e1d057a211720a`). The demo is
`dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem`
(sha256 `0e8197dcc1fe18ca1028aba0fe09df2db291f7c64c4f54e9476e658199cea37d`), read from
`D:/TF2_Demo_Player/testdata/demos/`. Everything below is a fresh run, not a reuse.

### 6.1 camera-ui e2e now draws a real world

`bash camera-ui-check.sh` under the shared host lock, log `D:/_shared/b4-rc11-path-fixed/collect.log`:
**CAMERA-UI=PASS, 108 OK / 0 FAIL**. The same four trace assertions as section 4, now
fired on a map the machine can load:

    camera: summary mode=first-person ... follows=426 holdsNoTarget=1 freezeFrames=0
    OK   trace camera followed observe targets = 426 (>= 1)
    OK   trace camera published the pose each followed frame drew with = 426 (>= 1)
    OK   no followed frame claims the world origin for an eye = 0
    OK   the published followed frame names the watched target = 1 (>= 1)

`follows=426` is a placement count, so it is a property of how often the relay
re-places the camera on this recording; it is not comparable across maps and was not
used as a floor. The floor stays ">= 1 published followed frame", unchanged from
section 4. The trace shows the world it drew: `map: lookup ... selected=download/maps/koth_bagel_rc11.bsp`,
`world geometry displacedTriangles=46048 waterTriangles=118`, sky
`vertices=36 triangles=12 faces=6`, and the last frame
`eye=1227.5,-400.5,452.0 pitch=17.29 yaw=92.55 target=3`. Screenshot
`evidence/t79-camera/rc11-20261103/camera-ui-e2e.png`.

### 6.2 The R/D fixture needed a new pose source, not a new threshold

Replaying D with the eye the trace prints **does not reproduce R on a real map**.
`scene: camera` prints eye at `%.1f` and angles at `%.2f` (`native/src/main.cpp:407`,
`:10086`). Measured at tick 900 on rc11, with the eye held fixed:

| D fed | R vs D >8 |
| --- | --- |
| trace values `18.00 / 159.77` | 6,127 (0.712%) |
| `18.002` / `18.004` | 10,760 (1.250%) |
| `18.01` | 22,496 (2.613%) |
| `159.78` | 17,153 (1.993%) |
| `17.998` / `159.770` / `159.766` | 6,127 (0.712%) |
| exact probe pose (below) | **120 (0.014%)** |

Two decimals of pitch are worth several thousand pixels on this map, i.e. an order of
magnitude more than the 638-pixel signal the fixture is supposed to detect. A fixture
that cannot tell a rounding artefact from a regression is not measuring the camera, so
`run-pix.sh` now feeds D a **probed** pose instead of the printed one:

* eye: `entity_model_probe --observer-focus-at <sceneTick> --entity <target>` prints
  `selfOrigin` (the same `EntityModelResolver::extractTransform` the playback loop
  calls, `native/src/main.cpp:371-373`); `eyez = origin.z + 68.0` because
  `placeSpectatorView` applies `kSpectatorEyeHeight` with `kSpectatorBackUnits = 0`
  (`native/src/main.cpp:329/337/376-380`, trace prints `behind=0`).
* angles: `--props-at` prints `m_angEyeAngles[0]` / `[1]` at `%.6f`; the script
  asserts each is **single-valued** across the snapshot's local and non-local copies
  rather than silently picking one.
* the scene tick is derived, not assumed: `serverTick - stale` from
  `scene: clock` (`native/src/main.cpp:8567`) and `scene: snapshot`
  (`:8588`), cross-checked against the independent `tick=` field of
  `scene: anim` (`:9354/9362`). `--capture-tick` names a different instant.
* D is then checked against what it drew: `src=debug` and all five components within
  eye 0.06 / angle 0.006 of what was fed in.

This changes the fixture, not the acceptance numbers: **the thresholds are unchanged**
(R vs D <= 0.1% above delta 8, R vs D2 >= 90%, three runs with the same
`capture: armed frame_tick`).

### 6.3 Readings, tick 900

`bash evidence/t79-camera/run-pix.sh --demo ...rc11.dem --tick 900 --out D:/_shared/b4-rc11-b44-second`,
log `D:/_shared/b4-rc11-b44-second.log`, artifacts copied to
`evidence/t79-camera/rc11-20261103/`. R printed
`3480.4 -218.2 420.0 18.00 159.77`; `serverTick=27568 stale=146` -> scene tick **27422**,
followed entity **3**, exact pose
`3480.350098 -218.232910 420.031250 18.000008 159.765396`.

| run | camera arguments | trace line the frame was drawn with |
| --- | --- | --- |
| R | none (relay follows) | `mode=first-person target=1 follows=1495 holds=1 freeze=0 eye=3480.4,-218.2,420.0 pitch=18.00 yaw=159.77 src=follow` |
| D | `--debug-camera 3480.350098 -218.232910 420.031250 18.000008 159.765396` | `mode=free-look ... src=debug` eye 3480.4,-218.2,420.0 |
| D2 | `--debug-camera 0 0 1000 0 0` | `mode=free-look ... src=debug` eye 0,0,1000 |

All three at `frame_tick=900`. Bodies: R `draws=84 playerDraws=11 propBudget=85` with
`cameraFilter removed=1 indices=3`; D `draws=85 playerDraws=12 propBudget=84` -- the
followed body stays in D's set, as T84 documents.

| pair | any >0 | >2 | >8 | % >8 | max delta | largest component |
| --- | --- | --- | --- | --- | --- | --- |
| R vs D (same pose) | 120 | 120 | **120** | **0.014%** | 117 | 120 px, box (759,672)-(782,680) |
| R vs D2 (origin pose) | 860,683 | 858,310 | **836,501** | **97.179%** | 254 | 836,078 px |

`PIX-EVIDENCE=PASS`, `LOCKED-COMMAND-EXIT rc=0`. The 120 residual pixels are one
24x9 patch on a ground/geometry edge: no body, no missing object (8x zoom checked).
R vs D is the same order as the snakewater 638 and still well inside 0.1%; R vs D2 is a
different picture everywhere.

### 6.4 Ticks where the followed body is in frame cannot be fixtures

Same script, `--tick 2000` (log `D:/_shared/b4-rc11-b44-negative.log`, dir
`D:/_shared/b4-rc11-b44-negative/`), exits 1 **by design**: R vs D is
`gt8=21,059` = **2.446%**, one connected component of 21,059 px in box
(632,564)-(913,680). That block is the followed player's body: diagnostic frames
deliberately keep it (`native/src/main.cpp:10036-10043`), and on this tick the relay
camera sits inside that player's head, so the body fills the lower frame. The pose
replay is not at fault -- feeding the exact pose changed the count from 24,716 (printed
pose) to 21,059 (exact pose), i.e. the remainder is the body, not rounding.

So "which tick" is part of the fixture's validity, not a knob for a greener number.
Tick 900 is used because the body is not in frame there; tick 2000 is recorded here as
the negative control that shows the ceiling is live. Both readings come from the same
unchanged code and the same unchanged thresholds.

### 6.5 What this does not claim

* The 120-pixel residual is attributed to sub-pixel edge sampling on a ground/geometry
  boundary by its shape and zoomed inspection; no individual pixel was traced to a
  cause. It is not claimed to be zero, and it is not claimed to be animation phase the
  way section 3's 638 is.
* The KOTH e2e proves the trace, the counters, the HUD **and** that a real map was
  loaded and drawn; it does not prove the picture matches a reference render -- there is
  no second renderer to compare against.
* `outline-check.sh --mutation` remains NOT RUN in this round (see section 5).

