# TF2 Native Demo Player - Next Parallel Task Wave

Date: 2026-10-16
Planning baseline: `p0-entity-protocol@4360403`
T5 status: integration and release checks are complete for scheduling. T6-T8
are now merged into the integration branch. The
coordinator stopped `verify-all.sh --quick` after step 21/24; steps 22-24 are
provisionally accepted for task scheduling but remain unverified in the final
release record.

## Purpose

T1-T4 delivered the current rendering, audio, manual acceptance, and stability
evidence. T5 integrated those deliveries and T6-T8 extended animation, audio
content, and renderer reliability on top of the current working
baseline. Its final total-chain run was intentionally stopped after step 21;
do not start another total-chain run during task development.

Start the four tasks below now in separate worktrees. Each AI owns one task from
inspection through implementation, focused verification, evidence, and a
delivery report. No task may claim a limitation as fixed without a measured
input and a regression check. Task branches may be prepared while T5 runs, but
their product builds and runtime validation must wait for the shared validation
queue, and they may not merge until T5 establishes the release baseline.

## Assignment Summary

| Task | Owner | Main area | Can code in parallel? | Must wait to run product checks? |
| --- | --- | --- | --- | --- |
| T6 ViewModel animation binding | done / merged | Entity animation binding and ViewModel sequence selection | complete | complete |
| T7 Audio content coverage | done / merged | Music, TempEntity/PCF, voice, and MP3 paths | complete | complete |
| T8 World rendering reliability | done / merged | SUNSHINE device failure and world material fallbacks | complete | complete |
| T9 Resource discovery and startup | done / pushed | Download-map lookup, title parser compatibility, startup packaging facts | complete | complete |
| T10 Visual correctness baseline | next | World, player entity, hands, weapon, and ViewModel screenshot correctness | Yes | Yes |

Parallel development means code and static inspection in independent worktrees.
Builds, product launches, captures, mutation suites, real-device runs, and soak
tests share machine resources and must use the single validation queue. Do not
run them concurrently.

## Validation Cost Rule

T6-T9 must not run `verify-all.sh`, `verify-all.sh --quick`, or any other full
24-step release chain in their task worktrees. Each owner runs only the focused
probe, affected gate, and relevant mutation required by that task. A full chain
is reserved for one release-boundary run after all selected tasks are merged;
it is never a per-task completion requirement. If a task owner starts a full
chain accidentally, stop it and preserve the partial log as an interrupted
run; do not treat the partial result as a task failure or as a release pass.

The coordinator owns the validation queue. Before launching any product check,
the owner must state the exact command, expected evidence directory, and
whether it needs a build, capture, device, or soak slot. No task may mutate
shared evidence, build outputs, locks, or another task's worktree while a
queued check is running.

## Integration Quality Gate

Passing a focused script is not sufficient for merge. The coordinator must
review the product diff for ownership, lifetime, fallback, error propagation,
coordinate-space, material binding, draw ordering, and cleanup behavior. The
review must trace the changed path from real input to draw/audio output and
check that counters are not being used as a substitute for the user-visible
result.

Every rendering change must include a human-readable reference frame from a
real input, a before/after comparison, and a named explanation for every major
visible region. A non-flat frame, changed hash, positive draw count, or decoded
texture count does not prove that the map, player, hands, weapon, or ViewModel
is correct. If the frame still shows broken materials, missing entities, or
incorrect placement, the change is rejected or marked `NOT VERIFIED` even when
the automated gate is green.

Before merging a task, record:

1. Code-review findings and their disposition.
2. The exact executable used for the visual evidence and its source commit.
3. Region-level visual results for world, player, hands, weapon, and HUD.
4. Any known fallback or placeholder path that remains active.
5. A merge decision of `ACCEPT`, `ACCEPT WITH LIMITATIONS`, or `REJECT`.

The previous world-material work did not satisfy this standard because its
resource and counter evidence did not establish correct map appearance. T10
must close that gap before any release claim.

## Shared Project Context

The product is a native Windows TF2 demo player. It parses Source demo streams,
reconstructs entity state, resolves models/materials/sounds from loose files,
VPK archives, and BSP data, renders the world and ViewModels, schedules audio,
and exposes playback, camera, and HUD controls.

Current implementation boundaries and evidence are recorded in:

- `T1-VIEWMODEL-WEAPON-ACCEPTANCE-2026-10-15.md`
- `T2-WORLD-VISUAL-ACCEPTANCE-2026-10-15.md`
- `T3-CONTENT-AND-DEVICE-2026-10-15.md`
- `T4-STABILITY-RELEASE-2026-10-16.md`
- `NEXT-TASKS-2026-10-16.md`

Do not edit `D:\TF2_Demo_Player`. Use a fresh worktree per task. Do not use
destructive Git cleanup commands. New paths and source text must be ASCII.
Keep old evidence immutable; create a new evidence directory for every run.

## T6 - ViewModel Animation Binding (AI-A) - COMPLETE

### Goal

Determine whether real TF2 player animation state can be mapped to the correct
ViewModel model and sequence, then implement that mapping only when supported by
real demo evidence. The current manual report found natural sequence changes but
no usable binding; constructed sequence 0 is not a real-animation result.

### Steps

1. Start from the final T5 baseline in a fresh worktree and inspect the current
   entity fields, model ownership, sequence tables, and animation binding path.
2. Identify at least one real POV demo with a ViewModel entity and a real
   animation binding candidate. Record entity identity, model path, sequence
   values, tick range, and the source fields that drive the state.
3. Verify model sequence descriptors and entity sequence semantics independently.
   Do not assume `m_nSequence` indexes an unrelated `v_*.mdl` table.
4. Write a minimal fixture or probe that distinguishes valid binding, absent
   binding, invalid sequence, and model mismatch. Preserve named refusal reasons.
5. Implement the smallest mapping supported by the evidence. Keep bind-pose
   fallback when the source data cannot identify an animation safely.
6. Capture same-tick natural-animation frames and a static control. Compare the
   ViewModel region separately from world pixels and record traces and hashes.
7. Run the focused animation probe, `verify-render.sh`, and its relevant
   mutation. Do not run the full release chain from this task worktree.

### Boundaries

- Do not invent sequence names, map sequence numbers by coincidence, or call a
  constructed sequence a real-player result.
- Do not modify world material or audio behavior.
- If no supported real input exists, deliver a precise `NOT VERIFIED` report and
  a reproducible fixture requirement instead of speculative product logic.

### Acceptance

- A real input and the mapping evidence are recorded, or the blocker is explicitly
  demonstrated.
- Valid, missing, and out-of-range cases have deterministic named outcomes.
- A meaningful mutation makes the animation-specific assertion fail.
- The report separates real animation from synthetic probe coverage.

## T7 - Audio Content Coverage (AI-B) - COMPLETE

### Goal

Expand real-content evidence for the audio scheduler. Existing evidence covers
resolved sound names and device 6 playback, but music events, real TempEntity/PCF
events, MP3 device playback, and broader device/input coverage remain unverified.

### Steps

1. Inspect the current audio content report, event classifier, resource decoder,
   scheduler, and `verify-audio.sh` before changing code.
2. Search the available demo corpus for music, TempEntity/PCF, voice, announcer,
   and other audio event candidates. Record which inputs actually contain them.
3. For each reachable event, trace source table/index, event tick, resource path,
   classification, decode status, and scheduled/played outcome.
4. Separate missing resource, unsupported format, filtered event, duplicate,
   malformed payload, and successful playback counts.
5. Add support only for a concrete reproduced gap. Keep null-sink behavior and
   explicit device selection intact.
6. Run focused null-sink and device checks separately. Record `device_opened`,
   `device_open_attempts`, and human listening confirmation independently.
7. Run forward and mutation audio gates after product or gate changes.

### Boundaries

- Do not use the default `WAVE_MAPPER`.
- Do not infer audibility from counters.
- Do not change demo bit parsing or renderer code for an audio-only issue.
- Do not merge historical content maps as current evidence; bind every result to
  the input and source commit used for that run.

### Acceptance

- At least one real input is recorded for each claimed event category.
- Unsupported or absent categories remain explicitly `NOT VERIFIED`.
- Null sink does not open a device; device runs use a pinned device id.
- Relevant mutation cases are caught and source restoration is verified.

## T8 - World Rendering Reliability (AI-C) - COMPLETE

### Goal

Investigate T4 finding F4: `SUNSHINE.dem` reproducibly exits through the renderer
device-error policy after about 31 seconds. Identify whether this is device loss,
unsupported resource data, or a renderer state bug, and fix only the reproduced
cause. Also review the documented world-render limitations and choose one
measurable sub-scope; do not attempt a broad renderer rewrite.

### Steps

1. Reproduce F4 on the T5 baseline using a fresh output directory. Record source
   commit, input hash, GPU/feature level, device-removal reason, last successful
   frame, and process exit details.
2. Compare the failing run with a control demo on the same adapter and settings.
   Determine whether the error is deterministic and tied to a specific resource,
   draw path, or elapsed runtime.
3. Add the narrowest diagnostic reading needed to distinguish the suspected cause.
4. Fix only a demonstrated product defect. Preserve explicit device failure
   reporting when recovery is impossible.
5. Add a regression fixture or gate that catches the same failure without
   relying on a long soak when a focused reproduction is possible.
6. Select one additional world-render scope from water refraction, displacement
   completeness, sky texture correctness, VIS behavior, or large-atlas handling.
   State its acceptance boundary before implementation.
7. Run world material forward/mutation gates and `verify-render.sh` serially.

### Boundaries

- A pixel difference alone does not establish visual correctness.
- Do not change the capture gate to hide device failure or alter evidence.
- Water render-target/refraction parity, VIS portal recursion, and large atlas
  behavior are separate capabilities; do not claim them from the basic paths.
- If F4 is a driver/environment limitation, record the exact adapter and reason
  and leave it `BLOCKED` or `NOT VERIFIED`.

### Acceptance

- F4 has a reproduced cause and either a verified fix or a precise environment
  limitation.
- The selected additional scope has a focused input, a named reading, and a
  regression check.
- Relevant positive and mutation gates pass on the final task commit.

## T9 - Resource Discovery and Startup Contract (AI-D)

### Goal

Improve resource discovery and release startup behavior without weakening the
explicit TF root contract. T4 recorded two adjacent issues: POV title wording was
not accepted by one stability parser, and maps present only under
`tf/download/maps` were not found. No installer exists yet, so installation
behavior must not be reported as tested.

### Steps

1. Inspect `AssetRoot`, map lookup, title construction, stability parsing, and
   startup smoke probes on the final T5 baseline.
2. Reproduce the POV title parsing mismatch using a captured title and the exact
   gate parser. Decide whether the product title or the parser owns the contract.
3. Reproduce a demo whose BSP exists only in `tf/download/maps`. Record selected
   map path, fallback reason, and frame or scene consequence.
4. Define deterministic search order for `tf/maps`, `tf/download/maps`, and VPK
   entries. Ensure explicit `--tf-root` remains the only root when supplied.
5. Implement the narrow search/parser fix and add positive and negative cases for
   installed map, downloaded map, missing map, missing root, empty root, and
   automatic root search.
6. Inspect packaging options already supported by the repository and document
   prerequisites for an installer. Do not create a new installer framework in
   this task unless the project already has a concrete packaging target.
7. Run focused root, map lookup, startup, and fast gates; preserve fresh evidence.

### Boundaries

- Never silently fall back from an explicit root to Steam discovery.
- Never accept an existing empty directory as a valid TF root.
- Do not change the audio or renderer implementation.
- Do not claim installer, relocation, or clean-machine support without executing
  those scenarios on a packaged artifact.

### Acceptance

- Search order is deterministic and covered by real and negative fixtures.
- Explicit root, missing root, empty root, and auto-search contracts remain
  distinct and tested.
- The stability parser accepts the supported title forms without coupling to one
  localized status string.
- Installer availability is reported as `NOT VERIFIED` until a package exists.

## T10 - Visual Correctness Baseline (AI-E)

### Goal

Resolve the user-observed release blocker: the current window shows no usable
map, broken material blocks, no complete player, and only partial ViewModel
sleeves. Establish a real screenshot baseline first, then fix the smallest
reproduced cause for world, player entity, hands, weapon, and ViewModel draws.

### Steps

1. Start from the merged T6-T8 head and capture a visible-window reference on
   one pinned POV demo at a pinned tick. Save the BMP, trace, executable hash,
   GPU adapter, feature level, and all render counters in a new evidence tree.
2. Add a diagnostic-only camera mode (CLI or probe flag) that places the camera
   above the map with a deterministic target and projection. Capture the same
   world with the player camera and this top-down camera. This is an experiment,
   not a release UI feature; do not use it to hide a bad player camera.
3. Add named counters before changing the material path: input/drawn/degenerate
   triangles, visible bounds, VMT missing, VTF missing, VTF over-512 rejection,
   atlas insertion, atlas draw ranges, and material fallback. Measure the
   current 512 filter on the exact pinned frame instead of inferring it from
   the image.
4. Run a flat-color or lightmap-only diagnostic using the same world geometry.
   Classify the result with this decision table:
   - top-down flat frame is blank or clipped: geometry upload, winding, bounds,
     depth, or camera matrix is the root class;
   - top-down flat frame is visible but textured frame is broken: material/VTF/
     atlas binding is the root class;
   - world is correct but the player is absent: entity model selection,
     transform, culling, or depth ordering is the root class;
   - world and player are correct but hands/weapon are absent: ViewModel
     attachment, model selection, or projection is the root class.
5. Inspect the frame in the four required regions: world geometry/materials,
   world player entity, ViewModel hands, and ViewModel weapon. Name each absent
   or corrupted region with the responsible draw request, model path, material
   path, transform, and rejection reason.
6. Reproduce each classified defect with a focused switch or probe. A counter changing is
   insufficient; each fix needs a before/after screenshot and a region diff.
7. Fix only the first demonstrated root cause. Do not implement multi-atlas,
   entity rendering, and ViewModel placement changes in one speculative patch.
   If the experiment proves independent root causes, stop after the diagnostic
   evidence and open a follow-up task with the exact failing region and input.
   Preserve explicit refusal and fallback readings for missing resources.
8. Verify skinning, attachment transforms, hand model selection, weapon model
   selection, depth/culling, and camera placement on the same pinned tick.
9. Add a visual gate requiring non-empty world geometry, a visible player
   entity, visible hands, and a visible weapon where the input contains them;
   include negative controls for demos without those entities.
10. Run only the focused visual gate, affected render gates, and their mutation.
   Do not run the full 24-step chain from this task worktree.

### Boundaries

- Do not accept a frame because it is non-flat or because a switch changes
  pixels; the expected object must be visible in the correct region.
- Do not implement the 512 texture cap change until the new rejection counter
  proves it is material to the pinned frame; the current top-64 coverage number
  alone does not identify the loss mechanism.
- Do not replace real models with placeholder geometry or synthetic textures.
- Do not hide missing entities by weakening assertions or changing capture
  thresholds.
- Do not broaden into a renderer rewrite, installer, or audio task.

### Acceptance

- A human-readable reference frame shows the world, player, hands, and weapon
  for an input that contains them, or each absent item has a measured named
  reason.
- World and model materials are visibly bound to the intended resources.
- Before/after screenshots, region hashes, traces, and mutation evidence are
  tied to the final executable.
- No release claim is made until this gate passes on the merged head.

## Integration Order After Task Delivery

1. T9 and T10 start in separate worktrees from the merged T6-T8 head. Code and
   static inspection may proceed in parallel immediately.
2. Product builds, renderer launches, audio device checks, mutations, and soak
   tests are queued one at a time by the coordinator; none may overlap another
   task's runtime validation.
3. Review each task's diff and evidence against its own acceptance criteria
   before merge. The interrupted total-chain tail is not replaced by old
   evidence.
4. Merge product changes one task at a time. Resolve shared-file conflicts by
   function-level edits; never replace `main.cpp` or renderer files wholesale.
5. Run only affected gates after each merge. Run the full `verify-all.sh --quick`
   once at the next release boundary, not once per task branch. Do not use an
   interrupted or provisional chain tail as a substitute for that release run.

## Delivery Format For Every Owner

Each AI returns:

1. Task id, branch, worktree, base commit, and final commit.
2. Changed files and reason for each product or gate change.
3. Exact real inputs, commands, exit codes, and build identity.
4. Positive readings, refusal readings, and frame/resource hashes where relevant.
5. Mutation result and byte-for-byte source restoration proof when mutations ran.
6. Evidence path, worktree cleanliness, residual process status, and limitations.
7. Any cross-task dependency discovered, stated before merge.
