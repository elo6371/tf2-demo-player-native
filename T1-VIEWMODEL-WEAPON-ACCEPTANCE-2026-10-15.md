# T1 -- ViewModel and Weapon Manual Acceptance

- Task: T1 per NEXT-TASKS-2026-10-15.md section 4.
- Branch / worktree: `task/viewmodel-weapon-acceptance` / `D:\TF2_Native_Worktrees\viewmodel-weapon-acceptance`
- Base commit: `2235227` (the integration branch's HEAD when this task opened)
- Final commit: this commit (see `git log -1`)
- Product source changes: 0. Everything new is evidence, the runner scripts'
  records, and this report. The product was built from the base commit and the
  binary is identified by commit + size below.
- Scope boundary: the "manual" half of this acceptance is a REVIEW OF CAPTURED
  FRAMES AND THE PRODUCT'S OWN READINGS, not a person watching a live window.

## 1. Real input

- Demo: `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem`
  (recorded on this machine; pl_odyssey; POV; 39122 ticks; the one pinned POV
  recording from the T3 manual round that carries a ViewModel entity).
- Command model for the matrix: `models/weapons/v_models/v_scattergun_scout.mdl`.
- Build: `bash build-target.sh tf2_demo_native` rc=0
  (`.scratch/t1-build.log`; exe 1561088 bytes, D:\TF2_Native_Worktrees\...\native\build-nmake\tf2_demo_native.exe).

### The entity identity, re-measured on this HEAD (not carried over)

From the product's own trace at capture tick 2000:

```
scene: vmAnim entity=1077 sequence=29 rate=1.00 cyclePresent=0
      model=models/weapons/c_models/c_sniper_arms.mdl status=missing poseAvailable=0
      applied=0 entityRule=the viewmodel entity with a model that holds the
      view entity's active weapon owner=18 viewEntity=18 modelIndex=1032
      drawnModel=models/weapons/c_models/c_sniper_arms.mdl entityModelMatchesDrawn=1
      sequenceInRange=1 bindPose=1
scene: weaponBody entity=1008 class=CTFSniperRifle worldModelIndex=533
      path=models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.mdl refusal=none
```

- ViewModel entity: **1077**, `c_sniper_arms.mdl`, modelIndex 1032, owner/viewEntity 18.
- Weapon entity: **1008**, `CTFSniperRifle`, `m_iWorldModelIndex=533` -> `c_pro_rifle.mdl`.
- Over the whole recording: 25249 `scene: weaponBody` observations, 25249
  `scene: vmAnim` observations; the entity's sequence changes 32 times, range
  1..30 (see section 5).

## 2. The frozen matrix (14 cases, every landing pinned to tick 2000)

`--capture-tick N` means the first draw at tick >= N, and the same command has
landed 2 ticks apart -- comparing frames from different ticks reads the
recording's own motion as a difference. Every case therefore retried (up to 8
attempts) until the LAST `draw: begin tick=` read 2000, and each landing is
recorded in `logs/landings.txt`. All 14 landed on 2000 (attempts 1-3).

Whole-frame differences, from `bmp-pixel-diff.py` over the captured BMPs
(`analysis/matrix-analysis.txt` holds every number; 1264x681x24, 2582352 bytes):

| pair | differing bytes | percent | max_delta | verdict |
|---|---|---|---|---|
| ViewModel off -> on (companion) | 77658 | 3.0073 | 214 | PASS |
| same flags twice (determinism) | 0 | 0.0000 | 0 | PASS |
| companion -> command model | 93951 | 3.6382 | 214 | PASS |
| weapon body on -> off | 42138 | 1.6318 | 201 | PASS |
| hand right -> left | 84571 | 3.2750 | 207 | PASS |
| fov 40 -> 90 | 492993 | 19.0909 | 214 | PASS |
| fov 90 -> 120 | 86499 | 3.3496 | 214 | PASS |
| fov 40 -> 120 | 468131 | 18.1281 | 214 | PASS |
| material -> flat colour | 36853 | 1.4271 | 214 | PASS |
| apply-animation (no binding) vs default | 0 | 0.0000 | 0 | PASS |
| constructed sequence 0 vs no sequence | 35520 | 1.3755 | 214 | PASS |

Two byte-identical pairs are themselves readings, not skips: the same flags
twice are byte-identical (determinism of the capture), and
`--viewmodel-apply-animation` on this recording changes nothing because there is
no binding to apply (section 5). Three more identities are visible in the
landings hashes: `body-on`, `hand-right`, `fov-90` and `anim-on` all equal the
default `vm-companion` frame byte-for-byte -- i.e. the default state IS body on
/ right hand / fov 90.

### The world-control regions are measured, not copied

The three strips `viewmodel-material-check.sh` uses were tried first and went
red: that gate runs the small command model, while this recording's pass draws
the companion arms (`c_sniper_arms`, 43 sequences, 58 bones), which reach into
the top band and the right edge, and at fov 40 into the lower left. The initial
run's numbers (recorded in the same analysis file) showed differences inside
0,0,420,681 / 844,0,420,681 / 0,0,1264,250 -- in the ARMS' pixels, not the
world's. The control set below was then measured: each region reads 0 differing
bytes for EVERY differing pair (nine pairs) before any bound was written.

```
0,0,420,200      top-left block, above and left of the arms
0,560,420,121    bottom-left, below the arms' lower edge
1100,100,164,200 right edge above the weapon body
1064,600,200,81  bottom-right corner
600,0,64,60      top centre (the fov-40 arms cross the top band, not this window)
```

With those regions, all seven confinement claims PASS: every pair that changes
pixels has 0 differing bytes in each control region. `MATRIX-ANALYSIS: ok=18
bad=0`, `T1-MATRIX=PASS`.

## 3. Weapon switching -- found, not blocked

The recording DOES contain a genuine switch. One full pass over the whole
recording (39122 ticks, one run, 25249 weapon-body observations) observed
exactly two weapon entities:

```
scene: weaponBody entity=1008 class=CTFSniperRifle worldModelIndex=533 path=.../c_pro_rifle.mdl
scene: weaponBody entity=1110 class=CTFClub       worldModelIndex=558 path=.../c_tw_eagle.mdl
```

First club observation at playback tick ~21540 (trace line 267752); first
observation back on the rifle at ~21960. Three captures bracket the two
transitions (not at one shared tick, because a switch is a fact about two ticks):

| case | landing tick | weapon entity | class | worldModelIndex | model path |
|---|---|---|---|---|---|
| switch-rifle | 21500 | 1008 | CTFSniperRifle | 533 | c_pro_rifle.mdl |
| switch-club | 21600 | 1110 | CTFClub | 558 | c_tw_eagle.mdl |
| switch-back | 22100 | 1008 | CTFSniperRifle | 533 | c_pro_rifle.mdl |

The drawn model follows the entity's own `m_iWorldModelIndex` across a real
switch and back, with `refusal=none` in all three. The club's resolved path
(`c_tw_eagle.mdl` for a CTFClub) is what that entity's index names; it is
recorded verbatim, not repaired.

Two other POV recordings were tried first as switch candidates and are recorded
as dead ends, not skips: `small.dem` and `short-2024.dem` produced 0 `vmAnim`
observations in 180 s runs (no ViewModel entity; both recordings are shorter
than the requested capture tick -- last draws tick 115 and tick 115).

## 4. What changed pixels / what changed only counters / what is unverified

- Changed pixels (and only inside the ViewModel's drawing area, per the
  measured control regions): ViewModel on/off, companion vs command model,
  weapon body on/off, hand mirror, all three fov pairs, material vs flat
  colour, constructed sequence 0.
- Changed only counters/readings: `--viewmodel-apply-animation` (0 bytes;
  `status=missing` in both runs); implicit defaults (body-on / hand-right /
  fov-90 are byte-identical to the default capture).
- Real input absent: a second recording with a ViewModel entity AND a switch
  was not found among the local POV demos tried (two dead ends recorded above);
  the switch claim rests on this one recording's two events.
- Animation claims NOT verified: applying the entity's NATURAL sequence to the
  drawn model. There is no binding (`status=missing`,
  `reason=sequence has no animation binding`) and the product refuses to
  invent one; the constructed `--viewmodel-sequence 0` is a separate,
  explicitly-constructed case and is never called a real player-animation
  result.
- Material claims: this round verifies "the material binding vs flat colour
  changes the ViewModel's pixels, confined to it". It does not re-verify the
  phong/bump chain (that is `viewmodel-material-check.sh`'s subject).

## 5. Animation: natural vs constructed, kept apart

- Natural: over the whole recording the ViewModel entity's `m_nSequence`
  changes 32 times (range 1..30) and its rate changes with it; every observation
  reads `status=missing poseAvailable=0 applied=0`, so the drawn pass keeps the
  bind pose. `--viewmodel-apply-animation` reproduces the default frame
  byte-for-byte -- the flag is live, there is nothing to apply.
- Constructed: `--viewmodel-apply-animation --viewmodel-sequence 0` (index 0 is
  inside the drawn model's own 7-sequence table) changes 35520 bytes (1.3755%),
  confined to the ViewModel area. This is an explicitly constructed valid
  sequence, not a real player-animation result.

## 6. Gates (current HEAD)

- `bash verify-fast.sh` -> `VERIFY-FAST=PASS (assertions_ok=43)` rc=0
- `bash verify-render.sh` -> `VERIFY-RENDER=PASS (9/9 gates, mutation=0)` rc=0
- `bash verify-render.sh --mutation` -> `VERIFY-RENDER=PASS (9/9 gates, mutation=1)`
  rc=0; all seven perturbation gates read `MUTATION-CAUGHT=PASS`, sources
  restored byte-for-byte after the run (`git status native/` clean)
- `bash weapon-body-check.sh` -> `WEAPON-BODY=PASS (two draw requests, the body
  from the entity's own index, ...)` rc=0. The first attempt aborted with
  `FATAL: missing native/build-nmake/weapon_body_probe.exe` (a target this task
  had not built); the target was built and the gate rerun -- the original
  output is preserved in `logs/weapon-body-first-attempt.log`.

Two infrastructure incidents during this round, both caused by killed runs and
both cleaned before any verdict was taken; recorded because they are the kind
of state a reviewer must know a clean tree was recovered from:

- An overnight-interrupted mutation run left `native/src/animation_binding.cpp`
  patched (the neutered range branch) and stale lock directories
  (`.scratch/weapon-animation.lock`, `.scratch/viewmodel-animation.lock`,
  `.scratch/verify-all.lock`). The source was restored from git, the locks
  cleared, and a follow-up run that hit the same lock abort was itself aborted,
  its leftover patch (`native/src/viewmodel.cpp`) restored, and the tier rerun
  from scratch. The aborted attempts' log is kept as
  `logs/gate-render-mutation-STALELOCK.log` (in .scratch, not committed).
- The first matrix pass used a 300 s per-case timeout that is shorter than the
  switch cases need (tick 21500 takes ~5.5 min of playback); the switch cases
  were rerun with a 700 s bound. Landings for every case are in `logs/landings.txt`.

## 7. Evidence

- `evidence/t1-viewmodel-acceptance/`
  - `frames/` 18 PNG captures (17 matrix + the entity-finding frame)
  - `logs/` case logs, matrix run logs, analysis log, `landings.txt`, build log,
    the full-recording scan log, the weapon-body first attempt
  - `traces/` per-case trace EXTRACTS (readings lines + landing) and the raw
    traces' sha256 (raw files, 159 MB, stay in .scratch)
  - `analysis/` the full matrix analysis and the per-case readings table
  - `SHA256SUMS` covers all of the above
- Runner scripts: `.scratch/run-matrix.sh`, `.scratch/analyse-matrix.sh`,
  `.scratch/collect-evidence.sh` (kept in .scratch; the report names them so a
  reviewer can rerun the procedure).

## 8. Unverified / blockers (final status vocabulary)

- Applying the entity's natural sequence: `NOT VERIFIED` (no binding exists;
  named refusal).
- A second switching recording: `NOT VERIFIED` (no local input found; two
  candidate recordings recorded as dead ends).
- Any claim about how the frames LOOK to a human beyond the recorded frame
  review: out of scope.
