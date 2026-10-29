# T52 — the outline gate's mutation suite, first complete run

Run: 2026-10-29 21:16:13 → 22:59:51 (+08:00), 1h44m wall.
Tree: `D:/TF2_Native_Worktrees/candidate-final`, branch `candidate/p0-final`,
HEAD `c1879eb49940a6842df0d8112f744bbea8ced5e4` (tree not clean: ` M outline-check.sh`,
the harness repair described below, committed separately as the harness change).
Host lock: `owner=t52-outline`, acquired by the runner before the build and held for the whole run.
Binaries measured (rebuilt inside the lock, both newer than every source under
`native/src`, `native/include`, `native/tools`):

    tf2_demo_native.exe        2,657,280 B  0ac2922ca2b40914f20129738b3a0214d81d7ca13a16c289b65831ccfa554bb5
    entity_overlay_probe.exe     590,848 B  9eda4b2aa712ef65b0a9613804ede7326ba91941d594f871c5078e0c205911c8

## Verdict

    MUTATION-SUITE=ALL-RED-RESTORED
    OUTLINE-CHECK=PASS
    driver rc = 0

Forward phase: **98 OK / 0 FAIL** across sections -1/6..6/6, plus 9 restored-build
readings after the mutation cases — **107 OK lines, no FAIL line**.
Mutation phase: 14 numbered cases + m-meta — **16 MUTATION-RED, 0 MUTATION-GREEN,
1 MUTATION-INVALID by design** (m-meta: "a source that does not compile reads
MUTATION-INVALID and fails the run").

## Why this run exists

The suite had never completed before this run:

1. **Run 1** (2026-10-29 19:46:22, `_shared/t52-run/run-20261029-194622`) stopped at
   m6 with `OUTLINE_MUTATION_RC=3`. `MUTATION-INVALID`: the m6 `patch_in` anchor
   carried 12 spaces of indentation while `native/src/main.cpp:9402` has had 10
   since the capture block lost a nesting level. m6's semantics still held. Every
   case before it was red; m7–m14 and m-meta never ran.
2. **Run 2** (`run-20261029-203334`) was **invalid evidence and was killed** at
   21:11. The forward phase does not build (`build-target.sh` appears only inside
   `rebuild_product()`, mutation-only), so run 2 measured the binary run 1 left
   behind — the m5 mutant, which flips `DepthEnable` and `DepthWriteMask` together.
   That is exactly the four depth readings that went red in run 2 and are green in
   this run on an unchanged source tree.
3. The gate now refuses to run on a stale binary (SECTION -1, both directions
   self-tested), and the runner builds inside the lock and records the exe hashes.
   The m6 anchor was matched to the current source. All 16 `patch_in` anchors were
   verified before the run by `_shared/t52-run/preflight_anchors.py` (0 missing).

## Mutation cases, as executed

Order is definition order in the script, not numeric order: m-meta, m1, m2, m3, m5,
m6, m7, m8, m9, m4, m11, m12, m13, m14, m10. Each case patches sources, rebuilds,
requires the specific assertion to go red, then restores the sources and rebuilds.

| case | the mutant | reading under mutation | reading on the restored build |
| --- | --- | --- | --- |
| m-meta | a source that must not compile | MUTATION-INVALID (by design) | — |
| m1 | RED and BLUE exchanged | team pixels in the WRONG region = 58 | 0 |
| m2 | the pass depth-tests everything | x-ray depth test state actually bound = 1 | 0 |
| m3 | the overlay is always on | team-coloured pixels in the --outline off frame = 127 | 0 |
| m5 | the overlay writes depth | x-ray depth write state = 1 | 0 |
| m6 | the captured tick depends on the playback speed | speed=1 and speed=8 capture different frames | identical |
| m7 | no fallback to the class model | silhouettes submitted = 0; players rejected as noMesh = 20 | 14; 6 |
| m8 | the silhouette carries the bare path, not the upload key | silhouettes DRAWN = 0 | 5 |
| m9 | the bind mesh preferred over the world mesh | probe self-test failures = 5 | 0 |
| m4 | the overlay draws after the ViewModel pass | pass order (overlay before viewModel) = 0; overlay ordinal = 8 | 1; 7 |
| m11 | the readback reads the wrong instance | identity mismatches in the readback table = 448 | 0 |
| m12 | the GPU draw is shifted 3 px, the box is not | pixels outside every drawn region = 156 | 52 |
| m13 | the drawing expands ten times as far as declared | pixels outside every drawn region = 3370 | 52 |
| m14 | the product publishes expandPx=0 while the shell still expands | silhouettes the band refuses = 14; out-of-region pixels accepted = 0 | 0; 1 |
| m10 | a colour frame offered as depth | non-grey percent = 90.64 (section 5 requires 0) | 0 |

m10's region comparison does not catch the mutant on this tick (§ "the two regions
coincide on this tick"); the non-grey reading is what catches it.

## Forward readings that matter here

The four depth assertions that went red in run 2, on the same source tree:

    OK   x-ray: depth test = 0
    OK   x-ray: the bound state has the depth test off = 0
    OK   depth-tested control: depth test = 1
    OK   depth-tested control: depth write = 0
    OK   the x-ray state does not write depth = 0
    x-ray draws through the wall, the control does not -- occluded_xray=256 occluded_tested=0 occluded_tested_diff=0 region=16x16 | VERDICT PASS

Section 4/6 kept both directions: `off rc=0 bmp=2582406`, `on rc=0 bmp=2582406`,
`tested rc=0 bmp=2582406`, capture `tick=64161` pinned by `T52_TICK`.

## Restore verification

`post.txt` and `pre.txt` carry the same four sha256 values, byte for byte:

    87fe100ce2b5f92f2e3d6e49253676ebaea1fd27b33a2c57936c7a97981e5220  native/src/native_renderer.cpp
    7d564565d2a52a82a03523937591bf26146437da69ca88dffc36e09ed56e7a81  native/src/demo_header.cpp
    5e81ad07c3f5c2837d908447afa15ab10bb2944740530be8593190704634cd78  native/include/demo_header.h
    0d37f815165a2fec27e88a5dcf108b0effe8e119c94a018fed6cb496fabc9451  native/tools/entity_overlay_probe.cpp

Each case also printed `restored <file> byte-identical (cmp)` for every file it
patched. `status_after` was ` M evidence/outline/mutation-build.failed` (that file
is a copy of the failed mutant build log, refreshed by this run and committed with
this evidence) plus ` M outline-check.sh` (the harness change).

## Files here

| file | what it is |
| --- | --- |
| `outline-mutation.log` | the complete gate output: forward sections, every mutation case, the final verdicts |
| `runner.sh` | the driver that ran inside the host lock: build, record exe hashes, run the gate |
| `pre.txt` | HEAD, branch, status, four source hashes before the run |
| `post-build.txt` | exe hashes, exe mtimes, and the empty "sources newer than exe" list |
| `post.txt` | exit code, status after, the same four source hashes |
| `build.log` | the build step's own line |

Reproduce: `bash build-target.sh tf2_demo_native entity_overlay_probe` then, inside
the host lock, `bash outline-check.sh --mutation`. A forward run rebuilds before it
measures; a mutation run leaves the last case's restored build on disk
(`SUITE-ARTIFACT` says so in the log).

## What this establishes, and what it does not

Establishes: every assertion the gate relies on can go red, and the m5/m6 repair
did not blunt the suite (m6 is still red after the anchor fix, i.e. the assertion
still discriminates). It also establishes that a clean binary on this source tree
reads depth the way the contract says.

Does not establish: the eight open product defects listed as NOT VERIFIED in
`evidence/outline/T52-REPORT.md` remain NOT VERIFIED — a mutation suite proves the
gate can fail, it says nothing about whether the product is right on those points.
Nor does this file claim a full-chain result: `evidence/release-gates/outline-p0-clock.log:1329`
was the last PASS in a full-chain context, and that run explicitly did not request
the mutation suite (`:1322-1326`).
