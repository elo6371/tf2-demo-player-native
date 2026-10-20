# T28 Release Acceptance — 2026-10-20

Freeze commit: `b34372893e7fdaf524862d323c4ca710577b4cd5` on `p0-entity-protocol`.
Remote verified: `git ls-remote origin refs/heads/p0-entity-protocol` = the same
hash; ahead/behind vs `origin/p0-entity-protocol` = 0/0 after the push.

## 1. What was released

T29-T33 were implemented in five isolated worktrees from `36303c2`, reviewed one
by one, and merged into `p0-entity-protocol`. Merge commits and their log
entries are in `NEXT-TASKS-2026-10-20.md` §Merge log.

| Task | Branch tip | Merge | Result |
|---|---|---|---|
| T33 audit | `132caa7` | `8628ab8` | audit + new `quality-audit-check.sh`; no product change |
| T32 install | `84b67c7`+`3bf941d` | `4cc6f15` | present-but-unreadable VPK named; install gate |
| T29 world | `21bfffd` | `1c24d22` | near clip stated in map units — the room the camera stands in is drawn |
| T31 audio | `ef11a61` | `81c6c94` | per-event MP3 record + named `inaudible` |
| T30 models | `fb62c82` | `3ffd6cb` | product unchanged; model robustness probe + gate |

Integration commits after the merges: `43a9949` (exe_count pin 38→42),
`b343728` (step 23/24 assertion-count pins 139→154, 108→114).

## 2. Build

Clean full Release build on the merge result, `bash build-cmake.sh`:

- `cmake_build_rc=0 errors=0 warnings=10 exe_count=42`
- `grep -c '^add_executable(' native/CMakeLists.txt` = 42
- The ten warnings are pre-existing (5× C4244 main.cpp:1103-1107, 1× C4457,
  2× C4267 texture counters, 2× C4456); none comes from this round's code.
- Artifact SHA-256 (first 16): `tf2_demo_native.exe` 62E9E35A1B81C98A
  (1,821,696 B), `model_robustness_probe.exe` 607A737372313BE1,
  `world_feature_probe.exe` E1D232FD4D1F4657.

Binary hashes are **not** reproducible across relinks (T33 measured three
relinks of one source producing three hashes); the hashes above describe this
build only and no cross-build reproducibility is claimed.

## 3. Focused gates on the merge result

Run one at a time under `_shared/hostlock.sh` (builds, launches, captures and
mutations are serialized on this host).

| Gate | Command | Result |
|---|---|---|
| fast tier | `verify-fast.sh --no-build` | **PASS** (43 assertions) |
| visual baseline | `visual-baseline-check.sh --mutation` | **PASS** (5/5 mutations red) |
| near clip | `world-near-clip-check.sh --mutation` | **PASS** |
| material chain | `world-material-face-check.sh` | **PASS** |
| model robustness | `viewmodel-robustness-check.sh --mutation` | **PASS** (4/4) |
| viewmodel range | `viewmodel-range-check.sh` | **PASS** (74 OK) |
| audio content | `verify-audio-content.sh --no-build` | **PASS** (32 assertions) |
| quality/footprint | `quality-audit-check.sh --mutation` | **PASS** |
| install readiness | `install-readiness-check.sh --forward` | **PASS** (13 cases / 102 checks) |
| install mutation | `install-readiness-check.sh --mutation` | **PASS** (3/3) |

Four pixel-pin gates are **red** on the merge result. They are red *because*
T29 changed the picture, and the attribution is measured, not asserted:

| Gate | Reading on merge result | Pin (pre-T29 frame) |
|---|---|---|
| `world-geometry-hole-check.sh` | missing 1648, sky 1648, unexplained 0, backface 80917 | missing 38328, backface 167030 |
| `terrain-blend-check.sh` | 1 failed attribution assertion (`0 of 7` lost pixels occluded) | occluded attribution held |
| `lighting-semantics-check.sh` | dynamic 1,776,259 / specular 390,361 | 1,584,117 / 267,343 |
| `world-material-semantics-check.sh` | dynamic 1,776,259, detail 271,060, bump2 7,343, specular 0 | 1,584,117, 82,213, 15,953, 135 |

Attribution evidence (`t29-visual`, read-only analysis over T29's own frames):

- `tick 2000` reference frame has **60.1%** of its byte positions replaced by
  the fix; `tick 1200` has 34.7%. Whole-frame counts must move at that scale.
- `world-geometry-hole`: the old `missing` population was the near-clip hole
  (open=36015 in one component); after the fix open=0 and the remaining 1648
  pixels are enclosed slivers, every one of them covered by a sky face
  (`unexplained=0`). The gate's structural assertions still hold (9/9 point
  oracle, three ≥200 px slivers, depth control, named sky/displacement faces).
- `terrain-blend`: the terrain region grew 13,714 → 277,581 px and the on/off
  difference 470 → 277,523 px; the only red assertion is an attribution bound
  that existed to explain a 3,060-pixel loss which no longer exists (now 7 px).
- `lighting-semantics`: every direction assertion holds (dynamic 0 brighter /
  1,776,259 darker; specular 390,361 brighter / 0 darker; bump 830,580/80,196).
- `material-semantics`: `specular 135 → 0` is a **population extinction, not a
  dead consumer**: 134 of the 135 old positions have changed content (mean
  |Δ| 64.2) and the single unchanged byte had no specular effect before either.
  The same shader block still moves 390,361 bytes in the wide-arm `--world-specular`
  test on the same tree, and `feature-apply` still reports 9/9 materials,
  19,305 vertices, identical to the pin. The per-material→pixel chain is
  therefore marked **NOT VERIFIED** for this camera, with the cheapest next
  step recorded (re-solve a camera with `world_feature_probe --camera` that
  still sees those nine materials).

**Re-pinning was deliberately not done in this round.** Each of these pins
encodes the pre-fix picture, and moving it needs its own mutation re-run and a
population note. That work is a task-sized blocker, not a release-day edit.

## 4. The one full-chain run: `verify-all.sh --quick`

Single instance, host lock held for the whole run
(`.scratch/final-verify-1/verify-all.log`, 24 steps, 15:02 → 16:37).

`VERIFY=FAIL`. Step verdicts:

**PASS (20)**: BUILD, CENSUS (9/9), COVERAGE, FIXTURE 58/58, ORACLE,
ORACLE-CORPUS, ORACLE-RECORDING-TYPES, PROBE-OUTPUT-ADDITIVE, MUTATION suite,
CENSUS-NEGATIVE, TRAJECTORY-ORACLE, HISTORY-COVERAGE, WEAPON-WORLD-MODEL,
OBSERVER-FOCUS, SLOT-FRESHNESS, SOURCE-TV-COVERAGE, FRAME-CAPTURE,
RESOURCE-REACHABILITY, IDLE-SPIN, ENTITY-PROPERTY-LOOKUP, ENTITY-MATERIAL,
ANIMATION-AVAILABILITY, plus every mutation half of those gates.

**FAIL (1)**: step 22 `WEAPON-ANIMATION` — two assertions:
1. `distinct captured frames = 1 of 3` (tick 2000/4000/6000 produce one frame);
2. `identity skinning does not deform the bind pose` — 3 bytes, max Δ59.

Both are **caused by the T29 fix**, measured directly (not inferred):

- With the exact pre-T29 near plane restored
  (`worldNearPlane = 0.05f` box units = 602.78 map units), the same demo and the
  same three ticks produce **three different frames**
  (`d91dd163…`, `a79c7823…`, `20070e31…`).
- With the merged build (near = 4 map units) the same three ticks produce
  **one frame** (`f841c39f…` ×3).
- The source was restored byte-for-byte (`source_sha_after` ==
  `source_sha_before` = `85b00d68…`) and the tree rebuilt (rc=0).

Mechanism: this demo's camera sits still across those ticks, so the three
frames only differed because the *world behind it* moved; after the fix the
camera-adjacent wall fills the frame and the picture stops changing. The 3-byte
identity-skinning delta is the same phenomenon at the byte level (a 3-byte
difference against a 1-byte tolerance).

Steps 23/24 were initially reported as FAIL only because `verify-all.sh` pinned
their assertion counts at 139/108 while the gates now emit 154/114. Both gates
printed their own `PASS`; the counts were re-measured twice on the merged tree
(chain run and a standalone rerun) and the pins updated in `b343728` with the
measurement written into the comment. This is a stale-pin repair, not a gate
being weakened: the same pins still go red when an assertion disappears.

## 5. Release blockers

1. **verify-all step 22 is red on the merge result, attributable to T29.**
   Owner: T29 follow-up. The gate's two failing assertions encode a
   pre-fix world picture; making them meaningful again needs either a
   near-plane-independent capture (a camera that actually moves) or a
   deliberate re-pin with its own mutation re-run.
2. **Four world-frame pixel-pin gates are red** (`world-geometry-hole`,
   `terrain-blend`, `lighting-semantics`, `world-material-semantics`).
   Owner: T34 candidate (re-pin pass with population notes). The `specular`
   per-material→pixel chain is **NOT VERIFIED** on this camera and must be
   re-measured from a camera where those materials are visible.
3. **Installer features are absent** (recorded by T32, gate pins `gaps=5`):
   no installer package, no uninstaller, no version/upgrade metadata, no
   shortcuts/file associations, TF2 resources not bundled. `cmake --install`
   staging is not an installer and no installer claim is made.
4. **`pak01_dir.vpk` fails to open on this install** (T31 corpus probe). With
   T32's product fix the product now names it as `vpk: refused` instead of
   silently skipping it; the root cause (install corruption vs reader gap) is
   not attributed.
5. **T29's own named blocker**: residual black is 98.7% lightmap-driven
   (205,911 px) and the back-face share of it is NOT VERIFIED (needs
   `--world-face-id`).

## 6. Lifecycle (T32 evidence, re-read for this report)

Clean-directory staging install: 3 files (1,819,136 / 297,984 / 812 B), installed
exe SHA-256 equal to the built exe, imports all System32 with no VC runtime
import, first launch → close → restart → explicit `--tf-root` → relocation →
delete-directory uninstall all captured with exit codes, a missing/empty root
fails by name without falling back to Steam, and the install directory hash is
unchanged by running it. These are the branch's readings on `36303c2`+T32, not
re-verified on `b343728` — **NOT VERIFIED** on the freeze commit, cheapest next
step: rerun `install-readiness-check.sh --forward` (it already passed on the
merge result; the lifecycle half was not re-run there).

## 7. Status of every task

| Task | State |
|---|---|
| T29 | merged `1c24d22`; near-clip fix verified; T26/T24/T27/T12 pins pending re-pin |
| T30 | merged `3ffd6cb`; product unchanged; forward+mutation green on branch |
| T31 | merged `81c6c94`; content gate green forward+device+mutation on branch |
| T32 | merged `4cc6f15`; install gate green forward+mutation on merge result |
| T33 | merged `8628ab8`; audit report + footprint gate green on merge result |
| T28 | this report; push verified; release verdict **NOT PASS** (blockers 1-2) |

## 8. Host state

- `git status --short`: clean at `b343728`.
- Host lock: FREE; no `tf2_demo_native`/`*_probe`/`cl`/`nmake`/`cmake` process.
- All five teammates: inactive. Every build, launch, capture, mutation and
  device run in this round went through one lock holder at a time.

## 9. Reproduce

```bash
cd /d/TF2_Native_Test
bash build-cmake.sh                       # cmake_build_rc=0 errors=0 exe_count=42
bash verify-fast.sh --no-build            # VERIFY-FAST=PASS (43)
bash visual-baseline-check.sh --mutation  # PASS
bash world-near-clip-check.sh --mutation  # PASS
bash world-material-face-check.sh         # PASS
bash viewmodel-robustness-check.sh --mutation
bash viewmodel-range-check.sh
bash verify-audio-content.sh --no-build
bash quality-audit-check.sh --mutation
bash install-readiness-check.sh --forward <out>
bash install-readiness-check.sh --mutation <out>
bash verify-all.sh --quick                # VERIFY=FAIL, step 22 only
```

Attribution artifacts for step 22:
`.scratch/final-verify-1/step22-nearattrib3/` (mut-*.bmp with the pre-fix near
plane, cur-*.bmp with the merged build, traces with the `gpu: world clip` line
showing `nearMapUnits=602.78` vs `4.00`).
