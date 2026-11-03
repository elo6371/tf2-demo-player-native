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

* The KOTH sample's map (`koth_bagel_rc13`) is not installed on this machine, so the
  camera-ui e2e runs against an empty world: it proves the trace, the counters and the
  HUD, not the picture. The picture claim comes from the snakewater run in section 3.
* `outline-check.sh --mutation` was NOT RUN in this round, so the eight defects it
  reintroduces remain NOT VERIFIED by this evidence.
* The 638 differing pixels of R vs D are attributed to animation phase by their
  distribution; no individual pixel was traced to its cause.
