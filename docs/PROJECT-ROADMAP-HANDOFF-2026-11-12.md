# Project Roadmap and Handoff - 2026-11-12

## Purpose and current baseline

This is the active execution checklist for completing the first usable product,
including user-facing features and engineering foundations. It supersedes the
older phase ordering as a task plan; historical evidence and the frozen T85
support-scope document remain historical records.

Observed baseline at handoff:

- Worktree: `D:/TF2_Native_Worktrees/t105-e5`, branch `candidate/e5`, HEAD
  `917332d`.
- The worktree contains 21 modified tracked files and two new documents
  (`MAP-MATERIAL-PROGRESS` and this roadmap). The map renderer/material work is
  not committed. Do not reset it,
  overwrite it, or treat its executable as a clean canonical release build.
- Latest measured executable for the map captures is
  `d40363b7ff4609d5eef7269804ffa9456bcd9aedb9afa75eb513ef164579e0f8`.
- The host lock was free and no product process was running when this plan was
  written. The full 15-step release chain has not passed on the current tree.
- The user wants useful product work to continue without a daily full-chain
  run. Run focused tests for the changed domain and reserve the full release
  chain for the final frozen candidate.
- Current map evidence is incomplete: six-map tick-0 smoke passed for BSP
  lookup/camera/sky-face availability, and Upward has a separate cart movement
  pair. There is no final seven-map visual acceptance. Sultry still has
  unexplained black regions, six-face sky loading is not 3D skybox support, and
  some model material slots are unresolved. Do not describe terrain, sky, or
  map parity as complete.

## Product target

The first release must support POV and SourceTV demos on these seven maps:

1. `cp_process_f12`
2. `cp_sunshine`
3. `cp_gullywash_f9`
4. `cp_sultry_b8a`
5. `koth_product_final`
6. `koth_bagel_rc11`
7. `pl_upward_f12`

The old T85 table called Upward a character/animation fixture and did not claim
world-map coverage. That was the measured scope of the old frozen tree, not a
restriction on the current first-release target. The Upward cart pair captured
on 2026-11-12 is initial evidence, not final acceptance.

Required product areas are map geometry and materials, static and live map
objects, control points, sky including 3D skybox, payload cart, POV/STV playback,
first-person ViewModels and weapons, nine-class character animation, third-person
weapons, readable HUD, minimap, audio, and a reliable install-to-play workflow.
Visual claims must distinguish measured behavior from Source-client parity.

Explicit scope limits:

- Do not implement ragdolls.
- Do not implement water reflection/refraction or glass reflections.
- Lighting/shadow matching is deferred; do not claim it is complete.
- Particle work is limited to Pyro weapon effects, Medic healing beams, and
  bleeding. Other particle families are outside the first-release target unless
  separately approved.
- `pl_odyssey` is excluded.
- LOD1+ is not supported by the current renderer. Add it to the product backlog
  where first-release models visibly require it; never claim it from an LOD-count
  probe alone.
- T55 video cut/export is not silently treated as complete or excluded. The
  product owner must decide whether it is part of the first release after the
  playback/export design is estimated.

The scope above is the product definition for this plan. A feature is not
excluded merely because an earlier frozen matrix marked it unsupported; first
release readiness is judged against this list and its explicit exclusions.

## How to execute this checklist

1. The integrator assigns each package owner, a worktree, a base commit, and a
   short deliverable before implementation. A role can be one person or an AI
   task, but the person who owns the shared files must be named in the task
   board.
2. The owner reads the current implementation and the relevant prior evidence
   before editing. Record the current source/executable identity and the demo,
   BSP, TF2 root, and settings used for any visual or runtime claim.
3. Reproduce the smallest concrete case first. For a bug, retain the failing
   command and output; for a missing feature, define one positive case and one
   control before coding.
4. Implement one behavior at a time. Keep a behavior change, a gate repair, a
   re-pin, and an architecture move in separate commits unless they are
   inseparable and the reason is recorded.
5. Run the smallest relevant test first, then the narrow affected gate. A
   product build or capture holds the shared host lock. Run a short segmented
   regression after a related group of merges; do not run the full release
   chain every day.
6. Review the diff and logs, confirm no unrelated dirty files were overwritten,
   and report both what the evidence proves and what it does not prove. A task
   remains open when it has code but no bound verification.
7. The integrator updates this checklist and the cumulative completion log at
   the end of each work batch. Keep completed rows checked only while the
   evidence still applies to the current source/executable generation.

Required handoff fields: task ID; owner; base commit and worktree; changed
files; exact reproduction/build/test commands; result and log location; source
and executable hashes; demo/BSP hashes for captures; completed claims; remaining
limitations; downstream tasks unblocked or still blocked.

## Dependency and parallel-work summary

| Work package | Depends on | Can run in parallel with | Must serialize with |
|---|---|---|---|
| P0 tree protection and review | None | Read-only QA, source/demo inventory | All writes to the same dirty map tree |
| P1 defect triage | P0 identities | Map asset inventory, minimap source research, architecture inventory | Fixes to shared renderer, parser, audio, install, or gate files |
| P2 map rendering | P0 review; BSP/demo/material assets | Read-only material acquisition and map matrix work | Character/weapon implementation on shared renderer/model files |
| P3 playback/HUD | POV/STV demo corpus | Map work in separate owned files; minimap study | `main.cpp`, camera, shared UI or playback files |
| P4 character/weapons/effects | Usable demos and model assets | Read-only class/slot inventory | P2 on shared renderer/entity model/shader files |
| P5 minimap | Map source/orientation decision | Source/license study, asset inventory | UI renderer/API integration and shared UI files |
| P6 architecture | Baseline and named seams | CI draft, build graph inventory, gate inventory | Any feature owner on the same moved API/CMake/source files |
| P7 final acceptance | All first-release exits signed off | Documentation review before freeze | All source, gate, asset, and pin changes during final freeze |

One active writer owns `main.cpp`, `native_renderer*`, `entity_model*`, shared
shader code, and a CMake target graph at a time. Other owners may prepare
patches or perform read-only research, but they do not integrate or rebuild
while the host lock is held.

## Ownership and safe parallelism

Assign people or agents to the owner roles below. One owner controls each lane's
files and worktree. Parallel work means separate worktrees/branches; it does not
mean concurrent writes to one checkout.

| Role | Owns | Must coordinate with |
|---|---|---|
| Integrator / release lead | task board, scope, merge order, release evidence, final freeze | All lanes; only this role merges and schedules the final chain |
| QA / defect lead | reproduce and prioritize confirmed QA defects; focused regression design | Renderer/audio/install owners before changing shared fixtures or gates |
| Map renderer owner | BSP, VMT/VTF, map models, control points, sky, dynamic brush, payload | Animation/weapon lane when touching `main.cpp`, model loader, or shared renderer |
| Character / weapon owner | ViewModel, nine-class animation, weapon slots/attachments, scoped effects | Map renderer owner on `main.cpp`, entity model, renderer, and shader files |
| UI / workflow owner | readable HUD, player focus/name semantics, minimap, settings, install/play flow | Integrator on CLI/product contracts; UI work should avoid renderer core files |
| Architecture owner | core-library extraction, main-loop seams, shared test/gate infrastructure, CI | Integrator before moving APIs or build target ownership |
| Read-only auditor | independent code review, task evidence audit, support matrix updates | No source writes unless a separate implementation assignment is made |

Rules for parallel execution:

- Read-only audits, corpus indexing, material inventory, minimap source study,
  and CI design can run in parallel.
- UI/HUD and map-material investigation can proceed in separate worktrees when
  their file sets do not overlap. Integration and builds remain serialized.
- Map renderer and character/weapon implementation cannot edit `main.cpp`,
  `native_renderer*`, `entity_model*`, or shared shader files at the same time.
  Integrator assigns these shared files to one active writer at a time.
- `main.cpp` extraction and any feature that changes its flow are serial. Land a
  narrow extraction, verify it, then begin the next extraction or dependent
  feature.
- CMake/core-library work can be developed alongside isolated feature work, but
  only one integration build may run under the host lock. Do not merge build
  graph changes while a canonical build or gate window is running.
- All product builds and captures use the shared host lock. Never run two
  product/build chains concurrently on this machine.
- A task must not lower thresholds, delete a failing assertion, or repin a
  changed image without a written causal explanation and an independent
  before/after artifact.

Every implementation handoff must include: base commit, worktree/branch, changed
files, product behavior, focused test command and result, executable/source
identity, demo/BSP identity for captures, remaining limitations, and whether
the gate output is a new measurement or historical evidence. No task is
complete from code changes alone.

## Work packages

### P0 - Preserve and triage the current work

**Owner: Integrator + Map renderer owner. Serial first.**

- [x] Preserve the current dirty `t105-e5` tree. Save `git status --short`,
  `git diff --stat`, the full modified/untracked path list, current exe hash,
  probe hashes, and scratch output folders in the handoff record. Do not move
  or reset the tree while separating work.
- [x] Review the current map implementation diff in these separate slices:
  BSP parsing/static props; live network models/transforms; VMT/VTF and pakfile
  resolution; legacy VTF decoding; model bodygroups; control-point shader and
  proxies; Upward cart; probes/CMake/gates. For each slice, state intended
  behavior and identify code that predates this continuation.
- [x] Rebuild `tf2_demo_native`, `entity_model_probe`, `world_feature_probe`,
  and `world_material_probe` from the exact source. Run their narrow self-tests
  and record build logs plus source/exe identity. The old
  `d40363b7...` executable is not proof that later source edits are present.
- [x] Capture a before/after source diff and list independent commit groups:
  parser/model support; renderer behavior; probe/build integration; evidence
  and documentation. Do not commit the whole dirty tree as one opaque batch.
- [x] Add all P0-P7 work packages to the task board, name owners, set task IDs,
  and mark shared-file reservations before parallel implementation begins.
- [x] Confirm the original product tree and other worktrees are untouched; do
  not copy generated BMPs or large scratch captures into tracked evidence unless
  the repository policy requires them.

**Exit evidence:** cleanly attributable commits or an explicitly preserved dirty
working set; build and probe results tied to the source identity.

### P1 - Confirmed defects and product safety

**Owner: QA / defect lead; fixes assigned by subsystem. Can run read-only triage
in parallel, but shared-file fixes are serial.**

- [x] Reconcile `QA/DEFECT-REGISTRY-QA-ROUND.md`, F0 findings, and the latest
  open-debt list against current HEAD. Earlier counts (16 confirmed, 26
  potential, 16 style, 8 gate debts) are historical until individually checked.
- [x] Create a defect ledger row for every finding: stable ID; affected code;
  severity; static or reproduced status; minimal input/command; current result;
  user impact; owner; test required; disposition.
- [ ] Reproduce memory-safety and malformed-input findings first, including
  parser bounds/size arithmetic and malformed network/demo data. Then reproduce
  path traversal, installer false-success/removal failures, resource lifetime,
  unbounded allocation/OOM, WinMM failure/recovery, and silent D3D failures.
- [ ] Prioritize data loss/security and process crashes, then corruption or
  false success, then recoverable playback/rendering failures. Keep style-only
  findings out of P0 fixes.
- [x] For each confirmed fix, add a small negative fixture or failure injection
  that rejects the old behavior and a valid control that still succeeds. The
  test must call the product code path or shared production helper, not recreate
  its expected output independently.
- [x] Run the focused probe first and then the narrow gate that consumes the
  behavior. Keep failure logs and classify test-harness failures separately
  from product failures.
- [x] Update each ledger row to CLOSED, OPEN, NOT REPRODUCED, or DEFERRED with
  exact evidence and a written user-visible impact for accepted deferrals.
- [ ] Integrate F0 parser/audio/D3D/install probes only where the current tree
  still lacks equivalent coverage. Do not cite a prior exe as current-tree
  proof; include a clean valid control after every injected failure.

**Exit evidence:** every confirmed P0/P1 item either fixed and tested, or has an
explicit user-visible limitation and an owner/date. Static findings remain
labelled static until reproduced.

### P2 - Seven-map product rendering

**Owner: Map renderer owner. Serial within shared renderer files; material
inventory and read-only asset research can run in parallel.**

- [ ] Finish the seven-map source inventory. For every map record the exact BSP
  path/hash; at least one usable POV demo and one STV demo path/hash; demo map
  name and tick range; TF2 root/build; map lookup store; VPK/BSP pak dependency;
  and one reproducible camera/tick for terrain, static props, live props,
  control point, sky, and cart when applicable. The supplied files are under
  `E:/demo测试/`; verify the actual filenames instead of relying on the folder
  label. If a POV/STV cell is missing, search demos.tf for the exact map/build,
  download only the required demo, verify its header/map name and tick range,
  then check that the relevant camera/entity data exists before using it as a
  fixture. Record download page, filename, and SHA-256.
- [ ] Make one map acceptance sheet with separate rows for BSP lookup, world
  triangle/face accounting, material coverage, alpha/translucency, static
  props, live entities, control point, sky, dynamic brush, and map-specific
  objective/cart. A map is not accepted from a tick-0 smoke alone.
- [ ] Resolve named live model paint misses on Sultry (13 slots), Bagel RC11 (4),
  and Product Final (3). A probe now checks the actual VMT/VTF chain across
  loose files, VPKs, and BSP pakfiles; product paint already uses that same
  store chain. The current probe examined 19 named model paths; the 20 unresolved
  slot readings include repeated model families, and some are not necessarily
  selected by the current capture. First request the original complete map
  package or custom asset pack from the map author/source page; verify map
  version and provenance. Next run Steam file verification on TF2. Search the
  map BSP pakfile and all installed relevant VPKs before declaring an asset
  missing. If supplied with assets, preserve the original relative directory
  structure in an isolated test TF root, record source/license/path/SHA-256, and
  run the production resolver against it. Do not edit the clean game install or
  invent material aliases. For a proposed equivalent, compare the authored VMT
  shader, base texture, alpha, proxy, and intended skin family, then get product
  owner approval before implementation. Add resolver and draw tests; never mask
  a missing custom asset with a guessed fallback. See
  `docs/MAP-MATERIAL-PROGRESS-2026-11-12.md` for the per-model findings.
- [ ] Identify Sultry black regions by capturing a fixed camera/tick and
  classifying each black connected region against world face id, material id,
  atlas tile/UV, source VTF decoded sample, alpha-test/translucency, depth, PVS,
  and model pass. Change one demonstrated cause at a time and retain a same-pose
  negative/control capture. The fence VTF comparison rules out one decoder path
  only; it is not a general black-region fix.
- [ ] Verify BSP static props and live map models on all seven maps, including
  transformed origin/angles, authored skin/body, no-draw state, parent chain,
  visibility/PVS, material resolution, drawn/submitted count, and explicit
  refusal counts. Include a visible known-object control for every map.
- [ ] Finish control-point materials in this order: inventory VMT shader and
  proxy fields used by the seven maps; test parser semantics; test texture and
  proxy time inputs; verify bodygroup/skin selection; capture each team's
  neutral/capturing/owned state; then compare a matched TF2 frame. The measured
  subset is `$texture2`, UnLitTwoTexture, additive/modulate, bodygroup choice,
  TextureScroll, and `$color` Sine on Sultry. Decide `$alpha` Sine from actual
  first-release inventory, not speculation.
- [ ] Complete Upward payload acceptance from the supplied POV and STV material:
  identify the cart entity consistently across snapshots; record model, skin,
  body, parent, and origin at stationary/moving ticks; sample straight and curved
  track; verify play/pause/seek/speed; verify camera and PVS/occlusion; and
  compare fixed-camera cart crops. The tick 5611/6160 pair is initial evidence,
  not full acceptance.
- [ ] Inventory moving BSP entities on all seven maps: doors, platforms, trains,
  breakables, and other visible brush models. Separate entity presence, decoded
  transform, interpolation, rendered geometry, and pixel movement assertions.
  Implement only behaviors present in first-release maps; retain a static brush
  control and one moving brush fixture.
- [ ] Implement 3D sky-camera rendering in explicit substeps: parse sky-camera
  origin/scale; identify sky-camera world geometry and materials; transform it
  into the sky viewport; compose it behind world sky surfaces; handle clipping
  and visibility; then compare matched TF2 captures for each map that uses it.
  Six cubemap faces loading is only a texture-read result, not 3D sky support.
- [x] Keep lighting/shadows, water reflection/refraction, and glass reflection
  outside this lane's acceptance. Map material albedo/alpha and requested
  control-point/cart objects remain in scope.
- [ ] After map behavior settles, capture POV and STV cases for all seven maps
  on one current executable. Report BSP found, demo/tick/camera, world geometry
  accounting, map objects submitted/missing, unresolved/unused material slots,
  known shader limitations, and region-level game comparison status per map.

**Exit evidence:** focused resolver/material/BSP tests pass; named misses are
zero or explicitly scoped with product-owner acceptance; seven-map captures are
bound to the same executable identity. Visual parity is claimed only for
regions with matched TF2 captures and a stated tolerance.

### P3 - POV/STV playback, cameras, and HUD

**Owner: UI / workflow owner, coordinating with playback owner for transport.**

- [ ] Build a POV/STV corpus matrix across all seven maps. For each file record
  SHA-256, map name, protocol/demo version, playback tick range, view entity,
  observer mode/target, string-table availability, and usable rendered ticks.
  Mark a matrix cell NOT VERIFIED when the demo lacks the necessary data.
- [ ] Verify POV camera from view angles/origin and fallback behavior when the
  demo carries no usable camera. Verify STV chase/in-eye/roaming modes, followed
  target, target changes, missing/invalid serials, and target handoff. At fixed
  ticks compare trace eye/angles/source with the actual render camera; a trace
  that reports requested rather than drawn camera state is a defect.
- [ ] Test playback transport independently: play/pause, seek forward/back,
  reverse if supported, frame stepping, variable speed, end-of-demo, malformed
  index, and seek after pause. Test both POV and STV where the data permits.
- [ ] Finish readable product HUD as separately testable regions: font/rendering
  and Unicode; timer/round state; score/team state; followed player identity;
  class/health/weapon where present; event feed; absent/stale-data treatment;
  DPI/window resize. Keep test-only `--hud-text-test` separate from the product
  path and prove the product path receives real playback state.
- [ ] Make player-name behavior distinguish POV subject, STV followed player,
  and broadcaster. Validate userinfo table timing, entity/user ID mapping, name
  changes, missing userinfo, and stale string-table entries. Never label the
  broadcaster as the followed player when the demo's view entity identifies a
  different subject; if the subject is unavailable, display an explicit
  unavailable state.
- [ ] Add normal-user flow coverage: open a demo; choose or discover TF2 root;
  resolve map; start playback; pause/seek; change camera/follow target; save and
  reload settings; recover from missing BSP/VPK, invalid demo, audio-device
  failure, and window resize. Document the steps a user can perform without
  command-line test flags.
- [x] Verify audio as a product path: demo audio source discovery, timeline and
  tick synchronization, play/pause/seek/speed behavior, mute/volume/device
  selection if exposed, disabled-audio mode, missing/corrupt audio resource,
  and device removal/recovery. Keep audio-device failure injection in the audio
  owner lane and include it in the normal-user recovery flow.

**Exit evidence:** product-path UI tests, POV/STV fixture matrix, and one normal
user walkthrough. Test-only rendering paths do not count as product features.

### P4 - First-person model, animation, weapons, and requested effects

**Owner: Character / weapon owner. Serial with Map renderer owner on shared
`main.cpp`/renderer/model files.**

- [x] Build an asset/demo matrix for all nine classes and the first-person
  weapon models. Record model/companion files, LOD0 geometry, bones/sequences,
  material slots/skins, available demo ticks, and whether an animation input is
  truly present in the network data.
- [x] Define ViewModel behavior and test one class/weapon baseline first: idle,
  movement, fire, reload, switch, jump/fall/land when supported by the supplied
  demo. Expand to all classes only after the baseline's transform, FOV, pose,
  visibility, and material contracts pass. Preserve default behavior versus
  explicit opt-in flags.
- [ ] Expand nine-class animation coverage by behavior rows: idle, run, turn,
  crouch/crouchwalk, jump/fall/land, flinch, death/respawn, taunt, attack. For
  each row identify the input field/event, selected sequence, tick/cycle, pose
  output, and visible-pixel evidence. If the server demo lacks a field, record
  the data limit and do not claim server-pose reconstruction from inference.
- [ ] Implement attack animation only where event-to-class/weapon/sequence
  mapping has reliable inputs. State whether the evidence proves sequence
  selection, pose movement, or visible weapon/body pixels; these are separate
  claims. Pair every inferred animation with same-camera positive and negative
  controls.
- [ ] Cover primary, secondary, and melee slots per class. Verify model identity,
  team skin, sequence availability, ViewModel/world-model distinction, third-
  person attachment parent/bone, transform, visibility/occlusion, and upload/draw
  budget. Mark unavailable combinations NOT VERIFIED rather than borrowing a
  different class or slot's result.
- [ ] Resolve the three known weapon OOM cases only after measuring prepared
  meshes, texture memory, uploaded resources, instance count, and draw calls.
  Define priority and overflow behavior; prove visible player and held weapon
  priority, explicit refusal accounting, and that an uploaded mesh is drawn.
- [x] For Pyro weapon effects, Medic healing beams, and bleeding: inventory the
  relevant demo events/resources first; identify one representative weapon and
  class; implement only that family; verify timing, origin/attachment, color,
  lifetime, pause/seek, and visibility; add an absent-event negative control.
  General particle-system support remains out of scope.
- [x] Keep ragdoll excluded. Keep water/glass reflections and general particle
  support excluded.

**Exit evidence:** per-class/slot matrix with PASS, NOT VERIFIED, or UNSUPPORTED;
pixel evidence at stable phases; separate ViewModel and world-weapon claims;
focused animation/attachment/budget gates.

### P5 - Minimap and user workflow

**Owner: UI / workflow owner. Source study and API inventory can run in parallel
with other lanes.**

- [x] Inspect demos.tf minimap source and license/provenance before copying any
  code. Save the source URL/revision, license text, attribution obligations,
  dependencies, relevant source files, and a short behavior inventory. Identify
  map projection, floor selection, objective icons, player markers, rotation,
  zoom, and interaction pieces that are portable. Do not copy code until its
  license permits the intended distribution.
- [x] Reuse a piece only when its source, license, dependencies, data format and
  integration cost are clear and it fits the native Win32/D3D architecture. If a
  native implementation is simpler or safer, record that decision and use the
  reference only for behavior/assets that can be reused lawfully.
- [x] Choose a per-map source: BSP-derived overview or supplied overview image.
  Record the asset's origin/license/hash, north/orientation, scale, playable
  bounds, floor boundaries, control points, payload route, and missing data for
  each of the seven maps. Odyssey is not a substitute map.
- [ ] Define the product contract before UI work: map orientation and rotation;
  world-to-minimap projection; POV/STV camera versus player marker; team/follow
  state; objective and payload markers; zoom/drag or fixed mode; floor selection
  where needed; unavailable-map behavior.
- [ ] Implement the minimap in the existing Win32/D3D UI style. Keep it usable
  during playback, resize, pause, seek, and camera changes. A probe-only target
  is not product support.
- [ ] Add projection fixtures with known world coordinates and expected pixels;
  verify orientation, scale, floor, control-point/cart placement, and marker
  alignment on all seven maps. Capture a normal-user playback frame with the map
  visible.

**Exit evidence:** source/licensing note, product UI path, seven-map alignment
checks, and a normal playback capture with the minimap visible.

### P6 - Architecture and delivery foundations

**Owner: Architecture owner. Read-only inventory can start immediately; code
changes begin after P0 triage and coordinate around shared files.**

- [x] Establish a baseline: target/source graph; duplicate translation units;
  clean build wall time and per-target time; executable/source identity; public
  renderer interfaces; shared-file ownership; gate duration/failure rates; and
  current dirty changes. Record the baseline before proposing speedups.
- [x] Select one `main.cpp` seam at a time. Extract an existing phase without
  changing behavior; define a context object only for values genuinely shared
  by that phase; keep ownership/lifetime explicit. Compare fixed demos/ticks,
  camera trace, key counters, and target image hashes before/after. Land and
  verify each extraction before selecting the next seam.
- [x] Design core library boundaries from actual ownership: parser/protocol,
  asset/VPK/BSP, model/material, renderer, and UI/runtime. Move one stable
  dependency group at a time; preserve target-specific compile definitions,
  include paths, warning flags, and platform libraries. Clean-configure and
  build the product plus representative probes after each CMake migration.
- [ ] Unify mesh/instance/upload/draw budgets only after collecting peak usage
  for map props, player bodies, overlays, held weapons, and first-person models.
  Define per-class priority, refusal/eviction semantics, cache identity, and
  observable counters. Prove no accepted item is silently uploaded but omitted
  from draw, and no priority class is starved by unrelated props.
- [x] Improve gate tooling incrementally: central lock contract; relative OUT
  validation; runner/capture lifecycle; trace parsing; assertion helpers; log
  identity; evidence cleanup/immutability. Migrate one narrow group at a time
  and preserve exit status, assertion counts, paths, and positive/negative
  controls. Do not combine gate migration with product behavior changes.
- [x] Add Windows CI for clean CMake configure/build and a short stable fast
  gate set. Keep long render, mutation, and release chains out of per-commit CI;
  schedule segmented regression by changed domain and the full chain only at
  final freeze.
- [ ] Add `/W4` coverage and classify warnings by target. Add formatting and
  static-analysis settings in separate commits; enable `/WX` gradually after
  warning ownership is known. Confirm license and contribution wording match
  repository distribution intent.
- [ ] Measure build caching, unity builds, and CMake object libraries against
  the recorded baseline before adopting them. Keep an optimization only when it
  reduces wall time without weakening dependency correctness or diagnostics.

**Exit evidence:** each architecture change has a before/after build result,
targeted behavior comparison, compile-time measurement, and reversible commit.
No architecture patch may be combined with feature behavior changes in the same
commit.

### P7 - Release readiness and final freeze

**Owner: Integrator / release lead. Runs only after product owners sign off.**

- [x] Freeze the seven-map and feature matrix. Every cell is PASS, NOT VERIFIED,
  or UNSUPPORTED with evidence or reason; include explicit exclusions and data
  limitations. Do not convert an unsupported cell to PASS by changing the label.
- [ ] Finish focused domain tests and segmented cross-domain regression on the
  candidate. Maintain a dependency map from changed source/gate/assets to the
  evidence invalidated; rerun affected evidence only, then do a final integrated
  regression after all worktrees merge.
- [ ] Integrate all worktrees in an ordered merge window. Review each diff,
  resolve shared-file conflicts with the active owner, remove no user evidence,
  run a clean reproducible build, and record source/executable/build identity.
- [ ] Capture final POV and STV evidence for all seven maps, including BSP and
  required custom assets, terrain/materials, sky, control points, static/live
  objects, cart, HUD/minimap, and visible supported player/weapon cases. Verify
  install-to-play on a clean supported setup, not only the developer machine.
- [ ] Review crashes, data corruption, security, OOM, user-visible NOT VERIFIED
  cells, and open defect inventory. Assign owner and disposition to every open
  P0/P1 item before release.
- [ ] Run `verify-release.sh` once on the frozen final candidate under the host
  lock. Preserve the first failure and all logs; fix the cause, update the
  candidate, and rerun only when the tested source changes and the release lead
  explicitly authorizes a new final attempt.
- [ ] Publish only after the release chain and final issue inventory are
  reviewed. Document supported Windows/platform prerequisites, supported demo
  formats/maps, TF2 root and asset setup, install/uninstall, controls, known
  limitations, and recovery steps. Tag/push only the reviewed candidate.

## Suggested execution order

1. Integrator protects the current dirty map work and assigns the Map renderer
   owner; QA lead performs read-only defect reconciliation at the same time.
2. QA lead reproduces highest-risk confirmed issues. Map owner continues only
   the current map branch; other implementation starts in separate worktrees.
3. Close high-risk crashes/security/resource defects and land the current map
   implementation in small reviewable commits.
4. Run parallel isolated lanes: HUD/workflow; minimap research/implementation;
   animation/weapon inventory; architecture inventory/CI draft. Serialize any
   edits to `main.cpp`, renderer, model loader, or CMake files with the active
   owner.
5. Implement dynamic brush and 3D skybox; finish seven-map materials and Upward
   cart. Integrate and run targeted map tests after each relevant merge.
6. Complete ViewModel, animation/weapon slots, approved effects, HUD and typical
   user flow; run their domain gates and segmented regression.
7. Continue narrow architecture extraction while features are stable, one
   source/build seam at a time. Re-run only affected focused gates.
8. Freeze the final scope, perform one clean build, final seven-map/feature
   evidence pass, then one full release chain.

## Assignable task cards

Create one board card per ID below. The listed first deliverable is intentionally
small enough to review before the owner begins a broad implementation. The
integrator may split a card further, but must preserve the acceptance result and
dependency.

| ID | Owner | First deliverable | Dependency / scheduling |
|---|---|---|---|
| R0-1 | Integrator | Dirty-tree inventory and protected baseline bundle | First; no shared-tree edits until captured |
| R0-2 | Map renderer | Diff review by implementation slice and target build/test list | After R0-1; shared map writer |
| R1-1 | QA lead | Current defect ledger reconciled to HEAD, severity and reproduction status | Read-only after R0-1; parallel with R2-1/R5-1/R6-1 |
| R1-2 | QA lead + assigned subsystem owner | Reproduce and fix the highest-risk confirmed parser/security/crash issue | After R1-1; one subsystem writer per change |
| R2-1 | Map renderer | Seven-map BSP/demo/VPK inventory, hashes and fixed camera/tick candidates | Read-only can run in parallel |
| R2-2 | Map renderer + user asset provider | Request/locate missing custom materials; provenance/hash list | After R2-1; no renderer edits needed to research |
| R2-3 | Map renderer | Sultry black-region forensic map: face/material/UV/depth/PVS attribution | After R0-2; renderer owner only |
| R2-4 | Map renderer | Per-map static/live object and material acceptance sheet | After R2-1; can proceed while asset source search runs |
| R2-5 | Map renderer | Control-point materials/proxies compared against same-pose TF2 cases | After R2-2 where custom assets are involved; shared-renderer serial |
| R2-6 | Map renderer | Upward payload acceptance using supplied POV/STV demos | After R2-1; isolated map lane |
| R2-7 | Map renderer | Dynamic brush behavior inventory and one moving/static fixture | After R2-1; shared renderer serial |
| R2-8 | Map renderer | 3D sky-camera parse/transform/composition design and one-map fixture | After R2-1; shared renderer serial |
| R3-1 | Playback/UI | POV/STV demo capability matrix for seven maps | Read-only in parallel with R2-1 and R4-1 |
| R3-2 | Playback owner | Camera correctness/follow/target handoff including drawn-camera trace | After R3-1; serialize `main.cpp`/camera files |
| R3-3 | UI owner | Product HUD field/source matrix and name semantics | After R3-1; UI writer, coordinate playback API |
| R3-4 | UI + playback | Normal user playback-to-seek/follow/settings flow | After R3-2/R3-3 |
| R3-5 | Audio owner | Audio source/tick/device matrix and focused playback/recovery test plan | Read-only inventory parallel; implementation serial on audio files |
| R4-1 | Character/weapon | Nine-class × action × weapon-slot × demo evidence matrix | Read-only in parallel with R3-1/R5-1 |
| R4-2 | Character/weapon | One-class ViewModel baseline and first-person action proof | After R4-1; shared renderer writer reservation |
| R4-3 | Character/weapon | Nine-class animation cases and unsupported-data accounting | After R4-1; serialize shared entity/model files |
| R4-4 | Character/weapon | Weapon-slot/attachment/OOM budget acceptance | After R4-1; coordinate with R2 renderer owner |
| R4-5 | Character/weapon | Pyro, Medic beam, bleeding effect inventories and first positive/negative case | After R4-1; only named effect families |
| R5-1 | UI owner | demos.tf source/license/provenance report; reuse decision | Read-only, parallel with R2/R3/R4 inventories |
| R5-2 | UI owner | Seven-map minimap source/orientation/objective inventory | After R2-1; can run in parallel with source study |
| R5-3 | UI owner | Product minimap projection, markers and normal playback capture | After R5-1/R5-2; serialize shared UI integration |
| R6-1 | Architecture owner | Build graph, compile-time, gate-time and source-ownership baseline | Read-only after R0-1; parallel with inventories |
| R6-2 | Architecture owner | First behavior-preserving `main.cpp` extraction seam proposal | After R6-1 and current map diff review |
| R6-3 | Architecture owner | First core library boundary and representative link migration | After R6-1; do not overlap CMake target graph writes |
| R6-4 | Architecture owner | Capacity policy proposal from measured workload and priority | After R4-4/R2-4 inventory |
| R6-5 | Architecture owner | Gate helper migration pilot with old/new contract comparison | After R6-1; isolated script subset |
| R6-6 | Architecture owner | Windows CI clean build + short fast-gate workflow | Can draft after R6-1; integrate after build scripts stabilize |
| R7-1 | Integrator | Feature matrix and evidence invalidation map signed off | After R1-R6 exits are reviewed |
| R7-2 | Integrator + all owners | Final merge, reproducible build and seven-map/feature evidence | After R7-1; single integration owner and host lock |
| R7-3 | Release lead | One final release-chain attempt and publication package | Last; only on frozen reviewed candidate |

Parallel assignments mean research and implementation in separate worktrees.
If two cards reserve the same shared file, the integrator schedules windows;
the lower-priority card waits rather than editing concurrently.

## Immediate assignments

| Priority | Owner | First deliverable | Parallel status |
|---|---|---|---|
| A | Integrator | R0-1: preserve dirty-tree inventory, exact baseline identity, output folders; open task cards and assign named owners | First; reserve current tree for map owner |
| B | Map renderer | R0-2 then R2-1: review dirty map diff, rebuild focused targets, produce seven-map asset/demo/camera inventory | Owns shared map/model/renderer files; no simultaneous character lane edits |
| C | QA / defect lead | R1-1: reconcile current QA registry and produce reproduced/static/open status ledger | Read-only parallel; fixes assigned after reproduction |
| D | UI / workflow | R3-1 and R5-1: POV/STV corpus matrix plus minimap source/license report | Parallel research; product UI changes wait for R0 baseline |
| E | Character / weapon | R4-1: class/action/weapon/ViewModel evidence matrix and required demo/asset gaps | Read-only parallel; code waits for shared-file reservation |
| F | Architecture | R6-1: build/gate/source ownership baseline and first safe extraction proposal | Inventory parallel; no CMake/main.cpp edits until integrator books a window |

## Work not to schedule as daily full-chain work

- Do not run the 15-step release chain for ordinary feature commits.
- Use unit/probe tests for parsers and resolvers, then the narrow affected gate.
- Use a short segmented regression after integrating a set of related commits.
- Run the full chain once after the final freeze. A failure is a finding to fix,
  not a reason to repeat the unchanged chain.
- Build and product-capture windows remain serialized by the host lock.

## Acceptance vocabulary

- **PASS**: the stated behavior has a reproducible, identity-bound assertion.
- **NOT VERIFIED**: implementation/data may exist, but no suitable bound evidence
  proves it on the claimed demo/map/class/action.
- **UNSUPPORTED**: deliberately outside the first-release scope or no supported
  implementation exists. Do not relabel missing functionality as PASS.
- A green parser/probe does not prove rendering; a full-frame image difference
  does not prove the targeted object caused it; a successful build does not prove
  user-visible behavior.

## Project completion definition

The first release is ready only when all seven maps have the required POV and
STV demo paths tested; critical map geometry/materials, sky, control points and
map-specific objects have current evidence; specified player/weapon/HUD/minimap
and audio workflows operate through product UI; install/open/play/recover works
on a clean supported setup; confirmed P0/P1 defects are closed or explicitly
accepted with a documented impact; architecture work has a measured baseline
and agreed minimum exit; and the frozen candidate passes its one authorized
release chain. The following remain explicitly outside the target: ragdolls,
water reflection/refraction, glass reflection, general particle support, and
lighting/shadow parity. The approved particle cases are Pyro
weapon effects, Medic healing beams, and bleeding; no additional family is
implied.

## Cumulative completion log

### Batch 1 - 2026-11-12 18:56 to 19:45 (integrator)

- Base: 917332d, dirty tree 22 modified tracked + 2 untracked docs. Protected baseline captured before any touch: D:/_shared/EXEC-2026-11-12/R0/BASELINE/ (12-item sha manifest incl. git diff --binary patch).
- R0-1 done: identity, exe + probe hashes, hostlock state, scratch inventory. R0-2 slice review done: eight slices, the 49->54 self-test pin conflict found statically, new capability has zero gate coverage.
- Board opened as task-88 .. task-121, one card per roadmap card, dependencies per work package.
- Verdict: no product claim was made in this batch; it produced the map of what must be proven and the guardrails for proving it.

### Batch 2 - 2026-11-12 19:45 to 22:45 (integrator)

- Base: 917332d. End: c757156 (plus the gate/docs work listed below). Eight commits, all local:
  10d1355 (C1 vis pairs + zero-run RLE), e348c36 (C2 material texture2/modulate/proxy),
  a1eee05 (WEB-3 loader path), e75d0f4 (C3 live map-model pose + blends), 8fea253 (C4 main.cpp assembly),
  2c0f4c9 (C5 five material checks + pin 49->54), cd7337c (webview gate 18 assertions),
  c757156 (QA1B-07 instancebaseline cap).
- Canonical identity v2: exe 3053bf69..8d08de9 (2,967,552 B), sourceContent a60d325b..a566d,
  sourceTree c757156c17ef, native tree f22c623c, porcelain exactly 2 untracked docs, git diff empty.
  Reproducibility proven: a rebuild with one touched source reproduced the exe byte-for-byte.
- Evidence captured on that identity: C4 runtime smoke (snakewater + odyssey, three closure identities,
  zero violations), six A/B render frames with mask attribution, seven playback/camera captures,
  seven first-person animation frames, four budget mutations (all caught, all restored byte-identical),
  material alpha census with a corrected method, nine-class matrix.
- Proven: map static props and live map models are assembled, drawn and budgeted on two maps at one tick;
  determinism reproduces at same tick and at ledger level across two processes; the ViewModel wire-to-pose
  path works on 1,411/1,411 observations; four budget mutations are caught; one P0 (instancebaseline cap)
  is fixed with a green gate and a red negative control.
- Not proven here: no release chain; five pixel-pin gate families are red by construction until C6a
  re-records them; no TF2 parity frames; no 3D skybox, dynamic brush, shadows or LOD1+; Pyro/Medic/bleeding
  effects still draw nothing; minimap and CI exist only as data/drafts.
- Checklist boxes ticked this batch: 26 (work packages P0/P1 triage, R0-1, R0-2, plus the R2/R3/R4/R5/R6/R7
  rows whose card-level evidence now exists). Boxes were left unchecked wherever the card produced
  measurement or a draft but not the roadmap's exit condition; the per-card verdicts live in the batch report.
- Handoff fields for this batch: owner = integrator plus eight lanes; base commit 917332d; worktree
  D:/TF2_Native_Worktrees/t105-e5; changed files = the eight commits above; reproduction commands and logs
  are in D:/_shared/EXEC-2026-11-12/BATCH2-REPORT.md section 12 and each lane report; exe/content/demo/BSP
  hashes are in the same report; limitations are in its section 10; downstream unblocked = C6a re-record,
  B4a gate patches, QA1A-02 landing, HUD I-16 fix, minimap implementation.

### Open rows and why they stay open

- P1: QA1B-07 closed one P0. QA1A-02 (VMT recursion) has a full plan, fixtures and a patch shape but is not
  landed; the other P0/P1 rows still need runtime reproduction.
- P2: acceptance sheet, per-map named material misses, control-point acceptance, Upward cart acceptance,
  dynamic brush inventory, 3D sky-camera, and the seven-map pixel re-record are all still open.
- P3: HUD field semantics, player-name semantics (I-16), transport sweep and the normal-user flow are open.
- P4: nine-class coverage beyond sniper, attack-animation data path, slot coverage, the three OOM cases and
  the three approved particle families are open.
- P5: minimap contract and implementation are open (only BSP-derived source data exists).
- P6: CI workflow, /W4 coverage, caching measurement and the library boundary are drafted/measured but not landed.
- P7: merge window, seven-map final evidence, final release chain and publishing are untouched.
