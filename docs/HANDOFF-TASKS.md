# What is done, what is not, and in whose words

## The three words, and how to use them

`docs/T85-SUPPORT-SCOPE.md` fixes the vocabulary this repository uses. Do not invent a
fourth:

| Word | Means | Does not mean |
|---|---|---|
| `PASS` | the named gate printed its verdict line on the named commit | that the feature is complete, that it works outside the assertions, or that the product may be shipped |
| `NOT VERIFIED` | no gate has been run, or the gate was changed after its last run | that it is broken |
| `UNSUPPORTED` | a decision not to implement: named in `docs/T85-SUPPORT-SCOPE.md` (B1..B6) or in the roadmap's completion definition | that it is a bug somebody forgot |

## The one status line that matters

`PASS*` below means: **passed at commit `59d94a7` in the 2026-11-02 `verify-all.sh --quick`
run (29 of 30 steps green, step 27 fixed by `fa1233f` and re-run green), and NOT re-run on
this tree.** Per `docs/HANDOFF-STATE.md`, this tree is 271 commits ahead of the newest
public head, so treat every `PASS*` as `NOT VERIFIED` for the binary you build today and
re-run the chain before quoting any of it.

```bash
bash verify-all.sh --quick      # 30 steps, cheap version of step 5 only
bash verify-all.sh              # 30 steps, ORACLE_SAMPLE=40 (about an hour)
```

Each step prints `=== N/30 <title> ===` and writes raw output under `evidence/verify/`
(`verify-all.sh:234`), so a claim can be traced to a log file.

## A. The 30-step chain (`verify-all.sh`)

| # | Step (line in `verify-all.sh`) | Gate script | Status here |
|---|---|---|---|
| 1 | clean full Release build (`:240`) | `build-cmake.sh` | `PASS*` |
| 2 | nine-demo census (`:376`) | `run-demos.sh` | `PASS*` |
| 3 | wire fixtures (`:390`) | inline fixtures under `testdata/` | `PASS*` |
| 4 | oracle cross-check, nine demos (`:404`) | `check-oracle.sh <path to ent-oracle.exe>` | `PASS*` |
| 5 | oracle cross-check, corpus sample (`:413`) | `check-oracle.sh` + `oracle-corpus-check.py` | `PASS*` |
| 6 | demo recording types (`:446`) | `oracle-recording-types.sh` | `PASS*` |
| 7 | probe output is additive (`:454`) | `check-probe-output-additive.sh` | `PASS*` |
| 8 | mutation suite, C++ (`:462`) | `mutate.sh` | `PASS*` |
| 9 | census verdict is falsifiable (`:504`) | `census-negative-test.sh` | `PASS*` |
| 10 | reconstructed positions, per value (`:518`) | `check-oracle.sh` trajectory mode | `PASS*` |
| 11 | entity history coverage (`:538`) | `history-coverage-check.sh` | `PASS*` |
| 12 | weapon world model (`:556`) | `weapon-world-model-check.sh` | `PASS*` |
| 13 | observer focus (`:579`) | `observer-focus-check.sh` + `--mutation` | `PASS*` |
| 14 | entity property slot freshness (`:610`) | `slot-freshness-check.sh` + `--mutation` | `PASS*` |
| 15 | real SourceTV coverage (`:646`) | `source-tv-coverage-check.sh` + `--mutation` | `PASS*` |
| 16 | frame capture (`:680`) | `frame-capture-check.sh` + `--mutation` | `PASS*` |
| 17 | resource reachability (`:723`) | `resource-reachability-check.sh` + `--mutation` | `PASS*` |
| 18 | idle spin (`:767`) | `idle-spin-check.sh` + `--mutation` | `PASS*` |
| 19 | entity property lookup (`:815`) | `entity-property-lookup-check.sh` + `--mutation` | `PASS*` |
| 20 | entity material (`:858`) | `entity-material-check.sh` + `--mutation` | `PASS*` |
| 21 | animation availability (`:887`) | `animation-availability-check.sh` + `--mutation` | `PASS*` |
| 22 | weapon animation (`:919`) | `weapon-animation-check.sh`, three mutation modes | `PASS*` |
| 23 | viewmodel animation (`:1046`) | `viewmodel-animation-check.sh` + `--mutation` | `PASS*` |
| 24 | viewmodel companion proof (`:1139`) | `viewmodel-companion-check.sh` | `PASS*` |
| 25 | player animation (`:1234`) | `player-animation-check.sh` (233 assertions) | `PASS*` |
| 26 | player animation pixels (`:1295`) | `player-animation-pixels.sh` | `PASS*` |
| 27 | SourceTV relay camera (`:1343`) | `spectate-check.sh` (73 assertions) | `PASS*` after `fa1233f` |
| 28 | world geometry at the pinned pose (`:1401`) | `world-geometry-hole-check.sh` | `PASS*` |
| 29 | world materials (`:1446`) | `material-coverage-check.sh` | `PASS*` |
| 30 | review workspace round-trip (`:1503`) | `review-workspace-check.sh` | `PASS*` |

## B. Gates that are in the release chain but not in the 30-step chain

These are wired into `verify-release.sh:89` only. Their last recorded run is older than
the 2026-11-02 chain, and none has been run on this tree:

| Gate | Chain step | Status here |
|---|---|---|
| `gate-launch-hygiene-check.sh` | 1 | `NOT VERIFIED` -- expected red until debt **D-22** is fixed (`docs/RELEASE-POLICY.md`) |
| `debt-claim-check.sh` | 2 | `NOT VERIFIED` |
| `outline-check.sh` | 5 | `NOT VERIFIED` |
| `third-person-weapon-check.sh` | 6 | `NOT VERIFIED` |
| `player-body-appearance-check.sh` | 7 | `NOT VERIFIED` |
| `class-pixels-check.sh` (MIN_BODY_PIXELS=200, `:108`) | 8 | `NOT VERIFIED` |
| `build-identity-check.sh` | 9 | `NOT VERIFIED` (the identity value itself is in `docs/HANDOFF-STATE.md`) |
| `world-geometry-budget-check.sh` | 10 | `NOT VERIFIED` |
| `install-cli-contract-check.sh` | 11 | `NOT VERIFIED` |
| `playback-error-paths-check.sh` | 12 | `NOT VERIFIED` |
| `timeline-check.sh` | 13 | `NOT VERIFIED` |
| `hud-live-check.sh` | 14 | `NOT VERIFIED` -- and its working copy was **modified, uncommitted** at the cut |
| `pin-provenance-check.sh` | 15 | `NOT VERIFIED` |

`bash verify-release.sh` prints `VERIFY-RELEASE=PASS|FAIL steps_ok=N steps_total=15`. That
line is the only whole-chain release verdict this repository allows; it has never been
`PASS`.

## C. Capabilities the author explicitly does not support

From `docs/T85-SUPPORT-SCOPE.md` (frozen at `e15577b`), unchanged by this handoff:

| | Not supported | Why it is listed rather than missing |
|---|---|---|
| B1 | ragdoll physics | would be a new subsystem, not a fix |
| B2 | `pl_odyssey` | map is out of the authorised set |
| B3 | water **reflection / refraction** (the warp effect is implemented) | needs a render pass the project does not have |
| B4 | video export (T55) | `UNSUPPORTED` in the task that asked for it |
| B5 | LOD1 and above, LOD switching | only LOD0 is built; nothing claims otherwise |
| B6 | pixel parity with the Source renderer | the gates compare against measured expectations, not against screenshots of TF2 |

The roadmap's completion definition (`docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md:611`)
excludes the same ground and narrows particles to three approved cases: Pyro weapon
particles, the Medic heal beam, and bleeding.

## D. Not done

Carried from the roadmap's open rows (`docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md:665`)
and from the debt list (`docs/T85-DEBT-LIST.md`, 17 open `D-` rows):

- **P1 / QA1A-02** -- VMT include recursion is not implemented in the probe.
- **P2** -- the seven-map pixel campaign is not done: no re-recorded pixels, no control
  points, no `pl_upward_f12` vehicle, no dynamic brush, no 3D skybox.
- **P3** -- HUD field semantics and the I-16 player-name path are not done.
- **P4** -- player models stop at `sniper`; the other classes are `NOT VERIFIED`.
- **P5** -- minimap draws only what the BSP carries.
- **P6** -- no CI. Every gate is a hand-run script.
- **P7** -- no merge window, no final evidence pass.
- **Audio** -- no chain step covers audio playback. Evidence directories exist
  (`evidence/t7-audio-content` at the cut), but there is no gate whose verdict line can
  be quoted, so audio is `NOT VERIFIED`.
- **`hud-live-check.sh`** -- modified and uncommitted at the cut; the committed version
  is what is in this tree, and it has not been run.
- **The 30-step chain itself** -- `NOT VERIFIED` on this tree, which is the single
  cheapest thing a recipient can fix.

## E. How to move a row

1. Run the gate. It must print its own verdict line; a bare exit 0 has been wrong before
   (see `docs/GATE-DESIGN-TRAPS.md`, and the `item_schema_probe` note in
   `docs/HANDOFF-TOOLING.md`).
2. On success, edit this table in the same commit and add the log under
   `evidence/verify/<step>-<name>.txt`.
3. If the gate itself changed, say so: a gate edited after its last run makes every row
   it covers `NOT VERIFIED` again.
