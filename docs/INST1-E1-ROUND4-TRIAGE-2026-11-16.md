# INST1 E1 round 4 — 12-gate arithmetic sweep on 75dae10e (2026-11-16)

Scope: re-bind the E1 arithmetic/pixel sweep gates to the new product build and
record one reading per gate. Round 3 (exe 26bff436) is in
`docs/INST1-E1-ROUND3-TRIAGE-2026-11-16.md`; round 4 re-runs the same gate set
against a product that changed under D71/D93/U3a and the 0a1aad40 floor-material
fix. Nothing in this round edited a gate judgement to make a red turn green.

## 1. Frozen identity

| item | value |
|---|---|
| product exe | `native/build-nmake/tf2_demo_native.exe` |
| exe sha256 | `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab` |
| exe bytes | 3,044,864 |
| exe mtime | 2026-11-16 13:28:50 |
| sidecar contentBinding | `recompiled` |
| sidecar sourceContent | `661a4bc5e988f57bbf2e0b0115a6cfecf0d2a2cb05ed911481cd2d13aa5fbe66` |
| sidecar sourceTree | `95d9fe196e2a` |
| sidecar dirty | dirtyTracked=8 dirtyUntracked=2, buildUtc=2026-11-16T05:28:27Z |
| HEAD | `e8cabb15` |
| tree content hash | `661a4bc5…` = sidecar sourceContent (agrees) |

Every reading below was produced from this exe. The probe binaries the gates
consume were rebuilt in this round from the same tree; section 4 records that they
came back byte-identical.

## 2. Gate table (round 4)

| gate | verdict | secs | key reading |
|---|---|---|---|
| player-model | PASS | 42 | rc=0 |
| vmt-nesting | PASS | 1 | `VMT-NESTING=PASS (36 assertions)` |
| minimap-probe-build (aux) | rc=0 | 1 | probe rebuilt |
| minimap-check | PASS | 2 | `MINIMAP-CHECK=PASS` |
| fixtures-backup (aux) | rc=0 | 0 | snapshot taken |
| userinfo | PASS | 272 | `ASSERTIONS=191` `RESULT=PASS` |
| fixtures-guard (aux) | rc=0 | 0 | no write-back |
| release-rebuild | PASS | 18 | `REBUILD=PASS targets=5 rebuilt=0 fresh=5 noop=5 basis=identity-agrees mtime_newer=0 exe_sha256=75dae10e…` |
| hud-live | PASS | 222 | `HUD-LIVE-CHECK=PASS` |
| t2-visual-matrix | FAIL | 267 | 58/61 assertions, 3 reds (section 3.1) |
| world-material-effects | FAIL | ~660 (killed) | gate invocation defect, section 3.2 |
| map-geometry-lighting | NOT RUN (partial) | 1470 | 31 captures, 2 of 10 poses complete, killed at window (section 5) |
| viewmodel-material | NOT RUN (partial) | 480 | reached section 7/9, every assertion so far OK, killed at window (section 5) |
| viewmodel-companion | NOT RUN | — | WINDOW-DEADLINE |
| minimap-hud | NOT RUN | — | WINDOW-DEADLINE |
| E4 reuse reading | REUSE-OK | 2+2 | `E4-VERDICT=REUSE-OK`, pre/post sha unchanged, tree stable |

Aux steps (`minimap-probe-build`, `fixtures-backup`, `fixtures-guard`) carry no
PASS token of their own; their judgement is rc=0.

## 3. Red triage

### 3.1 t2-visual-matrix — three reds, all expectation/behaviour questions

```
FAIL vis on input = 242109, expected 200000
FAIL vis on hidden = 25995, expected 15039
FAIL on vs vis-off differing=0 percent=0.0000 (min 5.0000)
```

The vis identity still holds (`drawn 216114 + hidden 25995 = input 242109`), so
the two counts are self-consistent; input grew by 42109 and hidden by 10956, the
same order as the geometry/material count change after 0a1aad40. Trial verdict:
**stale expectation**, not a product regression — re-pin only after the counting
basis is settled. The third red is different: the whole-frame BMP of `on` and
`vis-off` differs in **0 bytes**, while `on` vs `repeat` is also 0 and
`water cam vs sky cam` is 99.9660% — the capture is deterministic, so the
visibility toggle simply moves no pixel at this camera. Trial verdict: **the
check lost discriminating power on this camera**, not a threshold problem. All
three go to the card-ui / semantics owner together with the A1 basemap-shape
question; this round does not change the thresholds and does not claim them
fixed.

### 3.2 world-material-effects — the gate was invoked with a non-MSYS path

The gate builds its `--capture-frame` from `$SHARED`; this round passed
`SHARED=D:/_shared/...`, so the product was asked to write
`D:/TF2_Native_Worktrees/t105-e5/D:/_shared/...`. The captures never landed and
every comparison read `-1 bytes`:

```
FAIL water-off == water-on (-1 bytes); the switch moved no pixel
FAIL water on == water repeat = -1, expected 0
```

The check itself was not judged; the run was killed. Same class of mistake as
the minimap-hud `rc=2 1 s` (`mkdir /d/TF2…/D:/…: Invalid argument`):
**MSYS tools need `/d/...` paths, not `D:/...`, when the script concatenates
them itself.** Both need a clean rerun with `/d/` form.

## 4. Probe freshness: the mtime-only judgement was wrong

Round 3 opened with `release-rebuild` and `userinfo` red on
"source newer than binary" (`REBUILD-FRESH=inputs-changed-binary-not-rebuilt`,
`STALE-BINARY: native/src main.cpp newer than demo_open_probe.exe`). The product
exe was fine (`state=identity-agrees`, sha unchanged) and two build attempts
returned rc=0 without relinking: `native/src/main.cpp` had been rewritten by a
mutation restore, so its mtime is newer while its content matches HEAD.

To settle it with evidence instead of a judgement call, the five binaries were
force-rebuilt (delete the target binary, reconfigure the build tree, rebuild the
named targets) and hashed before/after:

| target | before sha256 | after sha256 | result |
|---|---|---|---|
| entity_overlay_probe | 6410e4bf… | 6410e4bf… | same-bytes |
| entity_model_probe | 5042f555… | 5042f555… | same-bytes |
| entity_protocol_probe | 7e12f982… | 7e12f982… | same-bytes |
| world_geometry_probe | dfbd95db… | dfbd95db… | same-bytes |
| demo_open_probe | 54340139… | 54340139… | same-bytes |
| tf2_demo_native | 75dae10e… | 75dae10e… | same-bytes |

So the earlier reds were **false alarms**: the old binaries had already been
built from the same content and mtime-only freshness cannot tell "content
unchanged, timestamp refreshed" from "content really changed". Reruns after the
rebuild: `release-rebuild` PASS (`rebuilt=0 fresh=5 basis=identity-agrees`),
`userinfo` PASS (`ASSERTIONS=191`). The judgement fix (content hash plus a
per-target dependency set, with a negative control that flips one byte red and a
positive control that only restores the byte green) is a later, separate commit.

### 4.1 Incident: deleting `CMakeFiles/<target>.dir` deletes `build.make`

The first attempt at a force rebuild deleted
`native/build-nmake/CMakeFiles/<target>.dir` wholesale. That directory holds
cmake's generated `build.make`, not just object files. The next
`build-cmake.sh` run failed its `cmake --build --target clean` step with
`NMAKE : fatal error U1052: 未找到文件 CMakeFiles\entity_overlay_probe.dir\build.make`
and exited rc=2 **without a message**, after the partial clean had already
removed the produced exes — including `tf2_demo_native.exe`, which was briefly
absent. Recovery: `bash build-cmake.sh configure` (3.4 s) regenerates the make
files, then `bash build-target.sh tf2_demo_native <probes>` (74 s) rebuilt
everything and the product exe came back **byte-identical** (75dae10e…).

Rules that follow: force a rebuild by deleting the object files only, or by
going through `cmake --build --target clean`; never delete a
`CMakeFiles/<target>.dir` directory. Also worth fixing later: `build-cmake.sh`
swallows the clean failure's text (`>/dev/null 2>&1 || exit $?`), which is why
this looked like "reconfigure did nothing".

## 5. Window outcome

Round 4 opened at 13:45 with a 14:55 hard boundary. The probe rebuild plus
`hud-live` (222 s), `t2-visual-matrix` (267 s) and `userinfo` (272 s) consumed
most of it.

**`map-geometry-lighting` — partial, no verdict.** Started 14:18:40, stopped at
14:43 by the window: 31 captures written, **2 of 10 pose groups complete**
(`cp_snakewater_final1/p1`, `cp_snakewater_final1/p2`, plus part of
`koth_bagel_rc11/p1`) out of 5 maps × 2 poses × 11 frames. It is **WINDOW-DEADLINE,
not a pass and not a failure**. Its own evidence output was reverted
(`git checkout -- evidence`) and the untracked `evidence/t62-map-matrix/inputs`
it regenerates was removed, so the tracked tree is clean again.

**`viewmodel-material` — partial, no verdict.** Started 14:43 after the lock
came free, reached section **7/9** by 14:52 with every printed assertion OK, then
was stopped by the window. Worth recording: the two round-3 numeric pins this
rerun existed to confirm — `the pass built one range = 2` and `the range has a
texture bound = 2` (`viewmodel-material-check.sh:689/:691`) — print **OK** in
this partial run (sections 6 output above), so those pins look correct, but the
full gate has no verdict and is recorded as **WINDOW-DEADLINE**.

**`viewmodel-companion` and `minimap-hud` — not run**: `companion` still needs
its witness windows re-derived from the 8-08 POV recording (round 3: history
observations `<none>`, witness ticks derived 0); `minimap-hud` needs its
`MINIMAP_CHECK_SCRATCH` passed in `/d/...` form. Both are **WINDOW-DEADLINE**,
not passes and not failures.

Outstanding work, in order: `viewmodel-material` (its two numeric pins from
round 3 still unconfirmed), `minimap-hud` with `MINIMAP_CHECK_SCRATCH` in
`/d/` form, `viewmodel-companion` witness re-derivation from the 8-08 POV
recording, and the A1/A6/A7 basemap-shape question together with the three t2
reds.

## 6. Reproduction

```bash
bash tools/lockrun.sh card-arch -- bash .scratch/e1-heavy4.sh   # round-4 runner
bash build-cmake.sh configure                                   # after a bad force rebuild
bash build-target.sh tf2_demo_native entity_overlay_probe entity_model_probe \
     entity_protocol_probe world_geometry_probe demo_open_probe
TF2_USERINFO_OUT=/d/_shared/.../userinfo bash userinfo-check.sh
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-arch env \
     VMT_NESTING_OWNER=card-arch bash vmt-nesting-check.sh
```
