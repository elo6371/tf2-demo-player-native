# TF2 Native Demo Player - Remaining Work Plan

Date: 2026-10-15
Current integration branch: `p0-entity-protocol`
Current HEAD: `2235227`
Repository: `tf2-demo-player-native`

## 1. Project Background

This project is a native Windows TF2 demo player. It reads Source demo files,
rebuilds entity state over demo ticks, resolves TF2 resources from VPK/BSP/MDL
and material files, renders world and first-person ViewModel content, schedules
demo audio, and exposes camera/HUD controls.

The product has separate evidence gates for protocol parsing, entity models,
world rendering, ViewModel rendering, audio, camera/UI, and release stability.
Every claim must be tied to a command, real input, named reading, exit code, and
fresh evidence. An old branch result is not a result for the current HEAD.

## 2. Current Baseline

Already integrated or verified in the current line:

- Demo playback, seeking, pause, PacketEntities state reconstruction.
- Entity model references, skeletal pose, skinning, attachments, and bind-pose fallback.
- First-person ViewModel draw, companion arms, FOV 40..120 with default 90, hand mirroring.
- ViewModel material resource chain: MDL -> VMT -> VTF -> BC1/BC3.
- GPU texture reuse and named material fallback.
- Independent static weapon-body draw request.
- World BSP geometry, lightmap, cubemap, skybox geometry, atlas-width selection, and VIS triangle clipping.
- Water capability detection and basic water draw path.
- Sound resource parsing, tick scheduling, duplicate suppression, reverse recovery, and null sink.
- Audio main wiring using packet-entry tick origin is integrated and verified.
- First-person, third-person, free-look, SPACE camera cycle, P pause, observer HoldCurrent, HUD layout planning.
- Explicit TF root contract, corrupt-demo refusal, soak evidence, blocked-environment reporting.
- T3 manual evidence is integrated under `evidence/manual-followup/`.

Known verification facts:

- `verify-fast.sh`: 43/43 on the current line.
- `verify-audio.sh`: 48 assertions and 12/12 mutation on the current audio integration.
- `verify-render.sh`: 9/9 forward and mutation after the fov-default quarantine fix.
- `verify-all.sh --quick`: not yet rerun on the current final integration line.

## 3. Global Rules

### Repository and worktrees

- Use one independent worktree per task: `D:\TF2_Native_Worktrees\<task-name>`.
- Do not edit the integration worktree while a task is being developed.
- Do not edit `D:\TF2_Demo_Player`.
- Use ASCII for new code, paths, file names, scripts, and new documentation text.
- Do not use destructive Git commands such as `git reset --hard` or `git clean -fd`.
- Do not rewrite old evidence to make a gate pass.

### Build and process isolation

- Only one build, mutation, or render gate may use a worktree at a time.
- Do not run two mutation suites concurrently.
- Do not run `verify-all.sh` while another task is building or mutating.
- Before and after every run, check for `tf2_demo_native`, `nmake`, and gate processes.
- After mutation, verify source restoration byte-for-byte and rebuild the affected target.

### Evidence rules

- Evidence must include the real input, command, commit, target executable path,
  exit code, named readings, and SHA256 where applicable.
- A pixel difference proves input sensitivity, not visual correctness by itself.
- A counter proves an attempted operation, not that a human heard or saw the result.
- Missing input, missing resource, unsupported capability, and blocked hardware must
  produce a named refusal or `NOT VERIFIED`; never silently use SKIP or PASS.
- `--capture-tick N` means the first reached tick can be greater than `N`.
  Retry until the requested comparison frames have the same measured tick.
- HUD readers must use length-aware Win32 title reads and anchor on `HUD[tick=`.
  Cross-process `GetWindowText` truncates long titles at 511 characters.
- Do not use executable hashes as the only identity anchor; MSVC rebuilds can differ
  while source and behavior remain equivalent. Record source commit and build log too.

### Required delivery format

Each task must deliver:

1. Task name, branch, and worktree.
2. Base commit and final commit.
3. Changed files and why each changed.
4. Real inputs and exact commands.
5. Build and gate exit codes.
6. Named forward readings and negative/refusal readings.
7. Mutation results and source restoration proof.
8. Evidence paths and hashes.
9. Unverified items and external blockers.
10. Clean-worktree and runnable status.

## 4. T1 - ViewModel and Weapon Manual Acceptance

### Goal

Close the remaining real-input evidence around ViewModel, companion arms, static
weapon body, FOV, hand mirroring, material appearance, animation limitations, and
weapon switching. Do not claim real player animation when the demo has no usable
animation binding.

### Work

1. Start from current `2235227` in a clean task worktree.
2. Identify a real POV demo containing a ViewModel entity and record entity id,
   weapon entity, model index, model path, and measured tick.
3. Capture a frozen matrix for ViewModel off/on, weapon body off/on, FOV 40/90/120,
   left/right hand, companion path, and material/flat-color comparison.
4. Retry each capture until compared frames report the same measured tick.
5. Attempt a weapon-switch input or locate a demo that genuinely contains a switch.
   If no suitable input exists, record the blocker and leave the switch unverified.
6. Test the animation path with a natural sequence and an explicitly constructed
   valid sequence. Keep those results separate.
7. Record BMP hashes, ViewModel-region and world-region differences, draw counts,
   model paths, animation status, and refusal reasons.
8. Run `verify-fast.sh` and the affected ViewModel/render gates.

### Commands

```text
bash verify-fast.sh
bash verify-render.sh
bash verify-render.sh --mutation
```

### Must not do

- Do not map `m_nSequence` directly to an unrelated `v_*.mdl` sequence table.
- Do not call a constructed sequence a real player-animation result.
- Do not change renderer behavior only to make a screenshot look better.

### Acceptance

The report must state which dimensions changed pixels, which only changed counters,
which real input was absent, and which animation/material claims remain unverified.

## 5. T2 - World Material and Visual Acceptance

### Goal

Provide real-frame evidence for world lightmap, cubemap, skybox, VIS clipping, and
water. Separate implemented basic paths from Source-equivalent capabilities that
remain unavailable.

### Work

1. Select real demos and ticks that contain skybox geometry and visible water.
2. Capture lightmap, cubemap, skybox, VIS on/off, and water off/on/repeat frames.
3. Prove that each switch changes geometry or pixels, not only a state counter.
4. Record sky face paths, dimensions, resampling, lightmap coverage, visible face
   counts, VIS clip counts, water triangles, texture binding, and frame hashes.
5. Add or use negative fixtures for missing sky faces, missing cubemap, missing water
   resources, unavailable PVS, and missing BSP data.
6. Run the existing world-material and render gates without changing unrelated gates.

### Commands

```text
bash world-material-check.sh
bash world-material-check.sh --mutation
bash verify-render.sh
```

### Explicit limitations to preserve

- Water render target/refraction is not Source-equivalent unless separately implemented.
- Displacement may remain a named fallback where geometry is truncated or unavailable.
- VIS portal recursion and cluster merge optimization are not implied by triangle clipping.
- Skybox texture visibility must be distinguished from a flat-color sky.
- 4096+ atlas behavior and frequent map/cluster changes need separate inputs.

## 6. T3 - Audio Content and Real-Device Acceptance

### Goal

Extend the already integrated audio scheduler evidence to real content categories
and real device behavior, while preserving the null-sink contract.

### Work

1. Select demos containing weapon, footstep, Uber, TempEntity, PCF, voice, music,
   and announcer candidates where available.
2. Build a traceable mapping from sound index/string table to resource path, tick,
   source table, classification, and decode status.
3. Record resolved, filtered, missing, duplicate, rejected, scheduled, and played
   counts separately.
4. Run null sink and device 6 as separate runs. Record `device_opened` and
   `device_open_attempts` for each.
5. Add negative cases for missing WAV, malformed WAV, unsupported format, excessive
   duration, duplicate tick, and backward playback.
6. Record the user's human listening confirmation separately from automated counts.
7. Run the audio forward and mutation gates after every product/gate change.

### Commands

```text
bash verify-audio.sh
bash verify-audio.sh --mutation
```

### Must not do

- Do not use the default `WAVE_MAPPER`.
- Do not interpret null-sink `played=0` as a playback failure.
- Do not interpret a device playback counter as proof that a human heard the sound.
- Do not change the protocol bit reader or renderer for an audio-only task.

## 7. T4 - Stability and Release Validation

### Goal

Bind stability and release evidence to the current integrated source, then cover
long runs, corrupt inputs, resource-root behavior, hardware blockers, and startup.

### Work

1. Build the current HEAD in a clean release worktree and record build logs,
   source commit, executable path, size, and identity metadata.
2. Run the 19-case stability matrix against this executable, not the old T4 run.
3. Cover empty/truncated/corrupt demos, missing or moved TF roots, missing BSP/VPK,
   missing model/material, and invalid audio device.
4. Run at least a 10-minute soak and record start/end time, tick, frame count,
   working set, crashes, exit code, and residual processes.
5. Test hidden/no-audio mode, device 6, WARP or real GPU when available, and record
   blocked environments explicitly.
6. Test installation/startup/resource-directory relocation when an installer exists.
7. Run release gates and mutation. Any evidence mismatch must be reported, not
   repaired by editing old evidence.

### Commands

```text
bash verify-fast.sh
bash verify-render.sh
bash verify-audio.sh
bash stability-release-check.sh --run-dir <fresh-run>
```

### Acceptance

The final report must distinguish current-HEAD evidence from historical T4 evidence.
WARP, feature level, installer, cross-machine, or hardware limitations must be
listed as `NOT VERIFIED` when unavailable.

## 8. T5 - Final Integration and Release Gate

T5 is the only serial task. Start it only after T1-T4 deliveries are reviewed and
their commits are merged or explicitly marked evidence-only.

### Step 1: Freeze the integration tree

```text
git status --short --branch
git log -1 --oneline --decorate
Get-Process tf2_demo_native,nmake -ErrorAction SilentlyContinue
```

Require a clean tree, no locks, and no build/mutation process.

### Step 2: Review and integrate

- Review each delivery against the required format.
- Use `git merge-tree` before merging.
- Merge commits in dependency order: protocol/entity, world/render, audio, UI,
  then evidence-only manual acceptance.
- Resolve shared `main.cpp` changes by function-level edits; never replace the file wholesale.
- Keep evidence-only work separate from product changes.

### Step 3: Run the final gates

```text
bash verify-fast.sh
bash verify-render.sh
bash verify-render.sh --mutation
bash verify-audio.sh
bash verify-audio.sh --mutation
bash verify-all.sh --quick
```

Run these serially in a committed clean tree. Do not edit source, scripts, or
evidence while `verify-all.sh` is running.

### Step 4: Close the release record

1. Preserve the complete final log and every nonzero exit code.
2. Update README, CHANGELOG, HANDOFF, and this plan from actual command output.
3. Check `git diff --check`, locks, child processes, and worktree status.
4. Push the final branch and verify `git ls-remote` matches local HEAD.
5. Report capabilities, limitations, external blockers, and the exact final commit.

## 9. Parallelism and Order

T1, T2, T3, and T4 may run in parallel only in separate worktrees and only when
they do not share a build directory or evidence output. They must not run in the
same worktree at the same time.

T5 cannot run in parallel with any T1-T4 build, mutation, render, audio, or release
run. T5 is the final serial integration and release gate.

Recommended order:

1. T1/T2/T3/T4 development and evidence collection in parallel.
2. Review each delivery and merge product commits one task at a time.
3. Rebuild and run affected gates after each merge.
4. Merge evidence-only reports after their source commit relationship is recorded.
5. Run T5 only after the tree is clean and all task-specific results are recorded.

## 10. Final Status Vocabulary

Use only these meanings:

- `PASS`: the named assertion ran and held on the stated input and current commit.
- `FAIL`: the assertion ran and did not hold.
- `NOT VERIFIED`: the input, hardware, installer, or environment was unavailable.
- `UNSUPPORTED`: the product intentionally reports a named capability fallback.
- `BLOCKED`: an external or environment condition prevented the test; preserve the
  original error and the attempted command.

Never convert `NOT VERIFIED`, `UNSUPPORTED`, or `BLOCKED` into `PASS` by omission.

## 11. Integration Update - 2026-10-15

The first final-chain run on `2235227` completed all 24 steps in about 2h20m.
The only red result was step 1: the build itself returned `cmake_build_rc=0` with
`errors=0`, but the gate still expected `exe_count=27` while the tree produced 31
executables. The four targets added after the old pin were:

- `quiet_capture_probe`
- `viewmodel_material_probe`
- `weapon_body_probe`
- `sound_timeline_census_probe`

The pin and step labels were corrected in local commit `9025a13`. This commit has
not yet been represented as a completed final-chain run: the post-fix rerun was
stopped around step 10 after about 30 minutes, and its partial log is retained as
diagnostic evidence. Therefore the current status is:

```text
old HEAD 2235227: 23/24 steps passed, step 1 failed on stale exe_count=27 pin
fixed HEAD 9025a13: pin corrected to exe_count=31
fixed HEAD verify-all: NOT VERIFIED (rerun stopped before completion)
```

Before release, push `9025a13`, confirm a clean tree and no active processes or
locks, then run the complete command once without editing source, scripts, or
evidence during the run:

```text
bash verify-all.sh --quick
```

Only a completed run on the fixed HEAD may be reported as `VERIFY=PASS`. If any
step fails, preserve the original output, fix the specific cause, and rerun the
complete chain; do not combine partial runs into a single result.

The task plan remains parallel for T1-T4 in separate worktrees. T5 is serial and
starts only after T1-T4 results are reviewed, integrated, and the final chain is
ready to run.
