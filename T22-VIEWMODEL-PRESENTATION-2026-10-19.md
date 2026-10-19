# T22 — ViewModel and weapon presentation, judged in pixels

**Task:** `NEXT-TASKS-2026-10-19.md`, T22 "ViewModel and weapon presentation".
**Problem as stated:** re-assess the applied-animation camera/model framing and the
T18 rendered pose on a real captured frame; confirm hand mesh ranges, weapon body
material slots and animation pose placement independently; do not treat `skinning=1`
or `drawCalls>0` as visual acceptance; cover at least one model with multiple ranges
and different albedo textures; deliver masked frame comparisons, control regions and
mutation checks for any changed product logic.

---

## 0. Verdict

Every claim this task inherited now has a pixel-level witness, and the one claim that
had none got one by giving the product a new input.

- **The T18 pose holds on the real frame.** The camera line is byte-identical between
  the bind and applied runs; the pose moves 9,740 px of which 9,738 stay inside the
  ViewModel's own coverage; the hand mask shrinks from 20,282 to 18,929 px, keeps
  contact with the weapon (`contactDistance=1`), and its centroid moves 6.35 px.
  2 px leave the coverage — edge-of-mask noise, and the gate bounds them instead of
  pretending they are zero.
- **The hand mesh ranges own disjoint pixel sets.** Forcing the arms' two ranges onto
  one shared albedo and differencing gives the hand range 20,282 px and the sleeve
  range 328 px; the sets are disjoint (0 shared px) and sum exactly to the difference
  of the two forced frames (20,610).
- **The weapon body's ranges own disjoint pixel sets too — measured through a new
  input.** T14 could only show that the body's two ranges bound two keys; the body
  had no `--viewmodel-material-slot` equivalent. T22 adds `--viewmodel-weapon-material-slot`,
  reported as `slotOverride=` on the body's own line, and the forced frames partition
  the body exactly: scope 1,302 px, rifle 8,308 px, disjoint, sum 9,610.
- **A model that is both weapon and hands, on two albedos.** The shipped
  `v_scattergun_scout.mdl` is one mesh of two ranges (9,723 + 13,446 indices) on two
  different albedos; forced slots partition it into 8,651 (hands) + 4,609 (gun), 0
  shared, sum exactly 13,260.

The gate is `viewmodel-presentation-check.sh`: 90 forward assertions and a 3-mutant
suite (3/3 caught, with a same-frame restoration control). `bmp-mask-contact.py` is
new: it answers "where is this mask, and does it still touch that one" — the
question the pose claim needs but no existing tool asked.

The mutation suite found two defects **in the gate itself** before finding any in the
product: a glob pattern that could never match a full resource path, and a baseline
that read a value only the forward run computes. Both are fixed; §10 records them.

---

## 1. Repository state

| item | value |
|---|---|
| worktree | `D:/TF2_Native_Worktrees/t22-viewmodel-presentation` |
| branch | `task/t22-viewmodel-presentation` |
| base commit | `9591499` — *Merge branch 'task/t17-world-visual' into p0-entity-protocol* |
| product commit | `500220a` — *feat(t22): the body's slot override, and a gate that attributes pixels to ranges* |
| evidence commit | this branch, on top of the product commit |
| report commit | this file, docs-only, on top of the evidence commit |
| push status | not pushed; no remote command was run |

Note: while this task ran, the integration branch moved (T12-close and T23 commits
landed, including 159 changed lines in `native/src/main.cpp`). This branch was cut
from `9591499` and is written against that tree; the integration merge is the
integrator's step and any `main.cpp` overlap resolves there.

---

## 2. The three inherited claims, restated as measurements

### 2.1 "The applied pose is a picture" (T18)

T18 fixed a transposed bone-table read and judged the result by frame differences at
one tick. T22 re-measures on the same pinned recording with a stricter instrument:
the difference is not only inside the ViewModel's coverage, it is *the hand*, and
the hand is located by its own forced-slot mask rather than by the whole composite.

| reading | bind | applied |
|---|---|---|
| `gpu: viewmodel frame` (eye, forward, placement, fov, near, far, hand, attachment) | byte-identical | byte-identical |
| hand mask (forced slot pair, effects off) | 20,282 px, x=571..838 y=297..415 | 18,929 px, x=571..829 y=297..415 |
| hand mask centroid | (685.58, 359.80) | (679.49, 361.59) |
| hand touches weapon | yes, `contactDistance=1` | yes, `contactDistance=1`, keeps 82 % of the bind contact |
| pose change vs composite coverage | — | 9,740 px changed, 9,738 inside, 2 outside (≤ 64 allowed) |
| pose change vs the body's coverage | — | 9,481 of 9,740 px are OFF the body: the pose moves the arms, not the body |
| repeat applied frame | — | byte-identical hash (`04fbd243…`) |

The 2 px are real pixels at the mask's edge; the gate asserts `outside <= 64` and
records the exact number, rather than an equality that one future driver update
would break for no reason.

### 2.2 "The arms' two ranges" (T13/T2.5)

The range table says the drawn `c_sniper_arms` has two ranges on slots 0,1 with two
albedos. The partition:

| range | forced reading | pixels | bbox |
|---|---|---|---|
| hand (slot 1 range read as slot 0) | 20,282 | x=571..838, y=297..415 | 268×119 |
| sleeve (slot 0 range read as slot 1) | 328 | x=582..744, y=383..415 | 163×33 |
| forced frames against each other | 20,610 | x=571..838, y=297..415 | — |

disjoint: 0 px owned by both, both directions; 20,282 + 328 = 20,610 exactly.

### 2.3 "The weapon body's two slots" (T14)

T14's own caveat was that the fix is invisible on the pinned recording because both
slots resolve into one atlas, and that its evidence was a key list, not a picture.
T22 closes this with the override body `c_tfc_sniperrifle` (two genuinely different
albedos) and the new input:

| range | forced reading | pixels | bbox |
|---|---|---|---|
| scope (slot 1 range read as slot 0) | 1,302 | x=677..714, y=376..576 | 38×201 |
| rifle (slot 0 range read as slot 1) | 8,308 | x=655..715, y=228..578 | 61×351 |
| forced frames against each other | 9,610 | x=655..715, y=228..578 | — |

disjoint: 0 shared; 1,302 + 8,308 = 9,610 exactly. Controls: nothing above the
composite changes (0 px in y=0..220), and the world pass is byte-identical under
every ViewModel flag set in the run.

### 2.4 One mesh, two ranges, two albedos (the task's explicit ask)

`v_scattergun_scout.mdl`, the recording's own ViewModel:

| range | forced reading | pixels |
|---|---|---|
| scout's hands | 8,651 | x=643..710, y=245..549 |
| scattergun | 4,609 | x=643..714, y=244..549 |
| forced frames against each other | 13,260 | — |

Range table order is gun first (9,723 indices), hands second (13,446); disjoint, and
the sum is exact. This model cannot be posed on this recording — the entity names a
different model and the product refuses the pose by design — which is why it makes
the range claim independent of the pose.

---

## 3. The product change: `--viewmodel-weapon-material-slot`

One input, symmetric with the arms' existing one since T2.5:

- parsed in `main.cpp`'s argument loop, rejected like the other body inputs when the
  body is off;
- flows into `viewModelWeaponMaterialSlotOverride` and through
  `viewModelDrawRangesFor(request, flatColor, slotOverride)` — the same builder the
  arms use, so the two channels cannot drift;
- reported on the body's own line: `gpu: viewmodel weaponBodyMaterial … slotOverride=`,
  so a run that forced slot 0 says so next to the keys it produced;
- deliberately **not** read by the arms' builder. The m3 mutation (§10) proves the
  gate would catch a leak, and that a leak is invisible to every reading line —
  the readings report the input a channel was given, not the slot a range used.

No existing needle moved: the `weaponBodyMaterial` line gained a field at the end,
and every other trace line is byte-identical (the T13/T18 gates' needles keep
matching, and the world pass is untouched).

---

## 4. The new tool: `bmp-mask-contact.py`

The existing tools answer "do these two frames differ, and where". The pose claim
needs the next question: *given a mask that locates something (here, the hand), how
big is it, where is its centroid, and does it still touch the weapon.* The tool takes
a mask pair and an optional body-coverage pair, and reports mask pixels, bbox,
centroid, contact fraction within a radius, contact distance, and how much of the
mask lies outside the body. The gate's section 7 is built on it.

---

## 5. Changed files

| file | change |
|---|---|
| `native/src/main.cpp` | the body's slot override: parse, flow into the body's range build, `slotOverride=` field on the body's trace line |
| `bmp-mask-contact.py` | new: mask size / location / contact measurement |
| `viewmodel-presentation-check.sh` | new: the focused T22 gate (7 forward sections, 90 assertions; `--mutation` runs a 3-mutant suite with baseline controls and a byte-for-byte restoration check) |
| `evidence/viewmodel/presentation/*` | forward-run evidence: 10 frames, 5 inspection grids, `summary.txt` |

---

## 6. Commands and exit codes

```
bash build-target.sh tf2_demo_native                       # product + tools
bash viewmodel-presentation-check.sh                       # forward,  rc=0 (90/90)
bash viewmodel-presentation-check.sh --mutation            # run 1: 3 failed of 9  (gate defects)
bash viewmodel-presentation-check.sh --mutation            # run 2: 1 failed of 10 (m2 fixed)
bash viewmodel-presentation-check.sh --mutation            # run 3: rc=0 (3/3 caught, 10/10)
bash viewmodel-presentation-check.sh                       # final script, rc=0 (90/90)
```

The forward gate ran twice before the gate's own fixes and once after
(`forward-1/2/3`); the first run's two failures were the gate reading the wrong field
name of its own tool's output — the same class of defect the mutation runs then
exposed more of. The frame hashes of `forward-2` and `forward-3` are identical for
every frame (e.g. bind `7f271c8d…`, applied `04fbd243…`, override body `6681e535…`)
while the executable hashes differ — the frames are a function of the source, the
binary is not reproducible byte for byte (T18 §11 said the same). All runs are
recorded in `.scratch/t22/*.log` in the worktree, and the final pair
(`gate-forward.log`, `gate-mutation.log`) is archived in the evidence.

---

## 7. Forward gate — the 90 assertions

Sections and what each pins (full text in `forward-2.log`, readings in
`evidence/viewmodel/presentation/summary.txt`):

0. **pinned inputs** — recording hash, executable hash.
1. **twenty captures** — every run rc=0, every BMP full-size.
2. **two draw requests, on the product's lines** — one ViewModel draw call, drawn
   model = entity model, pose applied only when asked, arms 2 ranges / 2 slots /
   600+9,396 vertices, body 2 textured ranges, override body reports two distinct
   albedos as an override, forced runs report the slot they were given.
3. **framing and controls** — world pass byte-identical under a different ViewModel
   flag set; composite 25,601 px (2.97 % of frame) with ≥ 100 px margins; the pose
   moves no mask edge more than 12 px; applied frame repeats byte-for-byte; the pose
   touches neither the strip above the composite nor anything below y=420 (0 px each).
4. **the arms' partition** — §2.2's numbers, plus "the hand's pixels are on the arms".
5. **one mesh, two albedos** — §2.4's numbers.
6. **the body's partition** — §2.3's numbers, plus the small/large ordering (scope
   1,302 ≤ 3,000; rifle 8,308 ≥ 4,000).
7. **the posed hand, in the picture** — §2.1's numbers.

Two display lines in section 7 print `status=FAIL` from the underlying tool (the
containment is 2 px over an exact zero). The assertions judge them with the
documented tolerance (≤ 64 px) and pass; the evidence file keeps the tools' exact
lines so the tolerance is visible rather than hidden.

---

## 8. Mutation suite

Three defects, one at a time, each restored byte-for-byte afterwards; the restored
tree redraws the baseline frame identically (`6681e535…`) and `git status` is clean.

| # | defect injected | caught by |
|---|---|---|
| m1 | the body's override is parsed and reported but the range builder is not told | forced frame ≡ natural frame (0 px changed vs baseline 9,610), keys unchanged, while the line still reports `slotOverride=0` — a wiring defect, not an unparsed flag |
| m2 | each natural body range reads the slot next to the one it names | the body's keys flip order: scope's albedo on the rifle range and vice versa (`c_sniperrifle_scope.vtf,c_tfc_sniperrifle.vtf`) |
| m3 | the body's override leaks into the arms' range builder | 18,802 px change outside the body's coverage (baseline control: 0), while the arms' reading still says `slotOverride=-1` — the leak is invisible to every reading line and only the picture sees it |

### The gate's own defects, found by its own suite

1. **A glob that could never match.** m2's expected key order was written as
   `*scope.vtf,c_tfc…*`, but each key is a full resource path, so the pattern demanded
   a comma directly between two basenames. First run: "not caught" for a defect the
   trace showed plainly. Fixed to `*scope.vtf,*c_tfc…*`.
2. **A baseline that read the forward run's homework.** The mutation section took its
   baseline pixel counts from section 6's readings, which the `--mutation` path never
   computes. First run: the baseline assertion failed on an empty value. Fixed by
   measuring the baseline from the forced reference frames the mutation path does
   capture.
3. **A judgement that trusted a reading the defect cannot touch.** m3 was first
   written to require `arms slotOverride=0` — but the readings report the *input* a
   channel was given, which the injected leak does not change. The measurement (18,802
   px outside) was correct in run 1; the judgement around it was not. Fixed by
   asserting the leak in pixels with the reading as the *control* (`-1`), which is
   also the stronger claim: for this class of defect the readings are blind by
   construction, and pixels are the only instrument.

This is the same lesson T14 §6 recorded in miniature — "a mutation found a second
defect, in the instrumentation" — and the reason the suite exists.

---

## 9. What is not verified

* **The world behind the ViewModel.** The map is shattered (F1/T17); the gate judges
  the composite, its coverage and its controls, never the scene behind it
  (`world_geometry=shattered` in the evidence).
* **One paused frame.** Every capture is paused at tick 0 (`paused_capture=1`); a
  playing frame is scheduler-dependent and out of scope here.
* **The pose's artistic correctness.** "The hand is on the grip" is a human reading
  of a 4× crop (`inspect-bind-applied-4x.png`); what is measured is mask location,
  contact and containment.
* **The body does not move with the pose.** `body_static=1`: 9,481 of 9,740 pose
  pixels are off the body's coverage, and the body's own forced-slot partitions are
  identical between bind and applied runs. The body is attached
  (`alignment=weapon_bone`) but takes no sequence transform. This task records the
  behaviour with numbers instead of deciding whether the shipped game animates the
  body — the recording has no visible body-pose delta to compare against.
* **Effects contaminate the partition frames** (`partition_effects_off=1`): forcing a
  slot also moves the pass's global effect constants, so the partition frames run
  with phong/bump/selfillum off. The natural-look frames keep effects on and are
  judged by coverage, not by partition.
* **Texture reuse on the second range.** T14's open question stands: the keys show
  two distinct bindings, not whether the second range re-uploads. This task's
  partition says nothing about upload counts.
* **Only the sniper's arms have frames** (the pinned recording's POV); the other
  eight arms models are covered at the descriptor/range level by the probes, as in
  T13.
* **A T24 capture was live during one earlier forward run.** The second forward run
  recorded `host_overlap=1` (a T24 lighting capture was live); the final forward run
  and the archived evidence record `host_overlap=0`. All captures are paused at a
  fixed tick, and the repeat-hash assertion is what would catch it if that stopped
  being safe.

---

## 10. Final state

- branch `task/t22-viewmodel-presentation`: product commit `500220a`, then evidence
  (`evidence/viewmodel/presentation/`), gate logs and this report.
- product processes: none left running; mutated sources never survived a run
  (restoration is asserted in the gate).
- `git status --porcelain -- native/` clean at gate time; the mutation suite's final
  assertion re-checks this every run.
- integration: the branch is ready to merge; the integration branch has moved since
  `9591499` (see §1), so the merge owner should expect `main.cpp` overlap.
