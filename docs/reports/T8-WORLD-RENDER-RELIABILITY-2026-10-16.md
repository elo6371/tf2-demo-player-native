# T8 -- World Rendering Reliability

**Owner:** AI-C
**Branch:** `task/world-render-reliability`
**Worktree:** `D:/TF2_Native_Worktrees/world-render-reliability`
**Base:** `4360403` (`test: record current stability run and gate results`, `p0-entity-protocol`)
**Commits:** `8336d48` (fix) -> `8e7d616` (sky scope)
**Status:** fix + extra scope implemented and forward-verified; **mutation passes and `verify-render.sh` NOT RUN** (see L7).

---

## 1. F4: what the defect was

`SUNSHINE.dem` (cp_sunshine, SourceTV, 128432 ticks, sha256
`7b8db6b4d5f375761723ededb89c232c842d1998f61d6134d9ad4ddc712e6a08`, 82,759,239 bytes)
left the main loop through the renderer device-error policy: `exit_code=3`, harness
verdict CRASH, `last_tick=0`, trace tail `mode: summary ... drawn=0 ... exit=3`.

Reproduced on the baseline in a fresh output directory: identical hash, `PRODUCT_EXIT=3`,
trace `loop n=1 tick=0` -> `loop n=2 tick=0` -> immediate exit 3. Nothing was ever drawn.

Exit 3 is `PostQuitMessage(3)`, the arm taken when `renderer.lastError()` is failed and is
neither `DXGI_ERROR_DEVICE_REMOVED` (0x887A0005) nor `DXGI_ERROR_DEVICE_RESET` (0x887A0007).

## 2. Which of the three it is: renderer state bug

Not device loss. Not unsupported resource data. A **sticky error channel**.

Two new readings pin it:

```
gpu: reload-stage stage=water lastError=0x80004005
gpu: post-reload   lastError=0x80004005
```

- `0x80004005` is E_FAIL, a generic COM failure -- not one of the `0x887A00xx` device-loss
  codes, so the device never went away.
- It is already set **after the reload returns success and before the first draw**, so it is
  not a Present result. It is something the load recorded.
- The stage lines show it first appearing at the **water** stage, and `lastError_` was never
  cleared. The loop's policy then reads a stale load-time refusal, cannot tell it apart from
  a device failure, and ends the program.

The water stage still refuses on cp_sunshine even though the water pass is off by default.
That is a separate pre-existing limitation and this task does not touch it.

## 3. The fix

`native/include/native_renderer.h`: added `clearLastError()`.
`native/src/main.cpp`: call it once the GPU reload has returned success.

A Present failure after that still sets `lastError_` and still reaches the same arms
(4 / 2 / 3), so explicit device-failure reporting is preserved -- only the stale verdict is
gone.

## 4. Extra scope (stated before implementing)

Of the documented limitations, this task takes **sky texture correctness**, narrowed to:

> the six skybox materials land on the six cube directions the geometry uses, and the box
> carries decoded images rather than one flat colour.

Measured per cube index 0..5: direction (from the geometry basis), suffix (from the naming
convention), the `.vmt` and `.vtf` that index resolved, and one sampled centre pixel.

**Acceptance boundary.** PASS proves suffix order, direction order, path resolution and
per-face decoded content agree, on a synthetic six-colour fixture and on cp_sunshine.
PASS does **not** prove pixel parity with the Source renderer, HDR/compression equivalence,
resample fidelity, or face-internal UV orientation (whether a face is mirrored or rotated in
place). A pixel difference alone does not establish visual correctness; nothing here asserts
image equality.

### What the reading found

| index | dir | suffix | cp_sunshine (`sky_tf2_04`) `.vtf` | centre pixel |
|---|---|---|---|---|
| 0 | +X | rt | `sky_tf2_04side.vtf` | 117,162,194,255 |
| 1 | -X | lf | `sky_tf2_04side.vtf` | 117,162,194,255 |
| 2 | +Y | bk | `sky_tf2_04side.vtf` | 117,162,194,255 |
| 3 | -Y | ft | `sky_tf2_04side.vtf` | 117,162,194,255 |
| 4 | +Z | up | `sky_tf2_04up.vtf` | 101,149,187,255 |
| 5 | -Z | dn | `sky_tf2_04dn.vtf` | 220,220,189,255 |

Two findings:

1. **Contradictory labels in the code.** `kSkyFaceBasis` (world_material.cpp) had index 2
   (+Y) commented `"up"` and index 4 (+Z) commented `"bk"`, while `worldCubemapFaceSuffix`
   and `BspParser::skyMaterialPath` use `{rt,lf,bk,ft,up,dn}` = {+X,-X,+Y,-Y,+Z,-Z}. The
   geometry order and the file order were both correct and agree; only the prose contradicted
   them -- directly under a comment asserting the two agree. Labels fixed.
2. **`sky_tf2_04` shares one texture across rt/lf/bk/ft.** Verified as real content
   (`vpk_query` lists `materials/skybox/sky_tf2_04*.vmt` in `tf2_misc_dir.vpk`), not a
   resolve defect. So "not a flat colour" is bounded at **>= 2 distinct images**, not 6.
   Six different files is the fixture's job, not this install's.

## 5. Inputs, commands, exit codes

| run | input | flags | exit | key reading |
|---|---|---|---|---|
| reliability | `SUNSHINE.dem` | `--start-paused --capture-tick 0` | **0** | `drawn=1 capture_written=1 exit=0` |
| reliability + sky | `SUNSHINE.dem` | `+ --world-sky` | **0** | `sky state=active vertices=36 triangles=12 faces=6 side=512 resampled=1 draws=1` |
| control | `bb841c6d..._cp_snakewater_final1.dem` | same | **0** | `drawn=1 capture_written=1 exit=0` |

Baseline comparison: `exit 3 / drawn=0` before, `exit 0 / drawn=1` after.
Earlier full-window run (pre-sky build): `SUNSHINE_DEFAULT_EXIT=124` over the whole 150 s
window, 7854 frames, tick 10500.

Build identity of the final binaries: `bash build-cmake.sh` -> rc 0, `errors=0`,
`exe_count=32`, `tf2_demo_native.exe` sha256
`60168f3231e352acb83e5463889d5298a0f3160c97de25fffcd5e99f4e9e4a0d`.

Anchor is the source patch hash plus these readings, not the exe hash alone.

## 6. Gate results

| gate | command | result |
|---|---|---|
| T8 reliability (forward) | `bash world-render-reliability-check.sh` | **PASS**, `checks=42 fail=0` |
| world material (forward) | `bash world-material-check.sh` | **PASS** |

`world-material-check.sh` now carries the per-face pairing on real content
(`sky.faceN.{suffix,dir,vmt,vtf,rgba,decoded}`) and the new fixture:
`fixtures = 10`, `fixture checks = 127`, `assertions with mutations = 157`.

## 7. NOT RUN -- limitations of this delivery

- `bash world-render-reliability-check.sh --mutation` -- **NOT RUN**.
- `bash world-material-check.sh --mutation` -- **NOT RUN**.
- `bash verify-render.sh` -- **NOT RUN**.

The mutation branches were rewritten this round to perturb **real source** (delete
`renderer.clearLastError()`; swap `up`/`dn` in `BspParser::skyMaterialPath`'s suffix table),
rebuild, assert red, restore and `cmp` byte-identical -- replacing the previous branch that
printed a canned `MUTATION_RESULT=red_exit3` without touching anything. They have not been
executed, so no `MUTATION-CAUGHT` is claimed. The sky mapping assertions are therefore
currently **forward-verified only**: the suffix-swap mutation that would prove the per-face
check can go red has not been observed.

## 8. Changed files

| file | why |
|---|---|
| `native/include/native_renderer.h` | `clearLastError()` |
| `native/src/main.cpp` | clear the channel after a successful reload; stage probes after each upload checkpoint; `gpu: device-error-policy` and `gpu: pre-draw error` readings; six `gpu: world sky-face` lines |
| `native/include/world_material.h` | `WorldSkyFaceOrigin`; `origins` on `WorldSkyReading` and `WorldSkyFaceCapture`; `worldSkyFaceDirection` |
| `native/src/world_material.cpp` | `skyFaceOrigin()` shared by `measureSky` and `captureSkyFaces`; `skyFaceSampleRgba`; `path`/`texturePath` on `SkyFaceLoad`; corrected `kSkyFaceBasis` labels; `worldSkyFaceDirection` |
| `native/tools/world_material_probe.cpp` | `sky.faceN.*` flattened; `skyFaceMappingChecks`; new fixture `skybox-six-distinct` (six distinct flat colours) |
| `world-render-reliability-check.sh` | new gate: F4 regression + sky mapping; real-source mutation branches |
| `world-material-check.sh` | per-face assertions on the real map; counts 10/127/157; wrong-expectation half for the new readings |

## 9. Evidence

Per-run directories, each tagged with timestamp+pid:

```
evidence/t8-render-reliability-20261016/                 baseline reproduction + diagnostics
evidence/t8-render-reliability-20261017001108-14247-forward/
evidence/t8-render-reliability-20261017001449-16203-forward/
evidence/t8-render-reliability-20261017004448-27674-forward/
evidence/t8-render-reliability-20261017102233-412-forward/   final forward run
```

Old evidence was not rewritten. Scratch files live in `.t8-tmp/` (untracked, to be removed).

## 10. Cross-task dependency

T8 may not merge until T5 establishes the release baseline. Builds, launches and soaks share
one validation queue with the other owners; this run waited on T6
(`viewmodel-animation-binding`) and on the audio task rather than overlapping them, which is
why the full chain was not completed in this window.
