# T60 entity-material: the count and the paint, reconciled

Status: product fixed, gate green, both mutations re-measured. The full chain was
NOT re-run; when it is next run it must start at step 1 (a chain may not resume in
the middle after a product change).

Tree: D:/TF2_Native_Test, branch p0-entity-protocol, HEAD 5967587 (unchanged:
nothing was committed, merged or pushed by this round). The product fix is STAGED
in the index (see section 9 for why).

## 1. Where this started

The full chain was killed during step 13 on 2026-10-28 for two reasons. One was
step 8:

    mutation_red=43 hold=5 green=0 broke=0 restored_identical=6

against the pins in verify-all.sh:476-488 (red=46 hold=6 green=0 broke=0 ident=6).
The four missing lines were exactly case m12, and mutate.sh printed

    PATTERN NOT FOUND: const bool textureUploaded =
            renderer.uploadEntityModelTexture(prepared.cacheKey, preparedRgba, preparedWidth, preparedHeight);

because T60 (0819fce, 2026-10-24) replaced that call with a per-(material slot,
skin family) upload. The second reason was that the same gate that step 8's m12
runs was red on a clean tree:

    entityMaterials=91/201 is not N/N with N > 0        (entity-material-check.sh:253)
    entityMaterialRanges=74/86 is not N/N with N > 0    (entity-material-check.sh:261)
    ENTITY-MATERIAL=FAIL                                22 OK, 2 FAIL, exit 1

The user chose to treat the gate's red as the finding (not as an anchor to
re-pin): re-anchoring m12 alone would have produced a mutation that proves a
gate which is itself broken.

The pre-fix reading is preserved verbatim and is the only record of it:

    evidence/entity-material/t60-count-regression/gate-forward-BEFORE-FIX.log   (6,832 B)
    evidence/entity-material/t60-count-regression/trace-BEFORE-FIX.txt          (7,938,218 B)
    evidence/entity-material/t60-count-regression/ledger-BEFORE-FIX.tsv         (43,970 B)

Tree at that measurement: HEAD 5967587, git status --porcelain empty.

## 2. Two defects behind the one red gate

### 2.1 The denominator counted draw ranges, not instances

T60 changed the entity pass to one draw range per (mesh, material run). The
renderer then did this (native_renderer.cpp, before this round):

    for (const auto& run : bucket.mesh->runs) {
      range...; packed.insert(packed.end(), bucket.items.begin(), bucket.items.end());
    }
    ...
    entityModelInstanceCount_ = packed.size();      // 201

so "instances" became instance rows summed over 86 ranges, while the textured
numerator is added once per bucket (91). N/N was arithmetically unreachable for
any model with more than one run, and 91/201 was not a paint defect at all.

Committed 2026-10-22 reading (evidence/verify/20-entity-material.txt at HEAD, from
before T60): entityMeshes=59 entityModels=95 entityVerts=127797
entityMaterials=95/95 entityMaterialRanges=29/29. One range per instance then, so
the counter happened to be right; T60 split the ranges and the counter silently
became something else.

### 2.2 The paint asked for a skin family the model does not have

main.cpp derived the paint index from the team:

    draw.skinFamily = (instance.transform.hasTeam && instance.transform.team == 3) ? 1u : 0u;

and the paint stage uploaded families {0, 1} regardless of what the model has. A
model that ships ONE skin family has no family 1, and
resolveMaterialSlot (native/include/material_slot_resolution.h:58-63) refuses:

    skin family 1 has no texture for material slot 0

The pre-fix trace (TF2_NATIVE_TRACE) has exactly 35 such refusals, ALL family=1,
over 22 distinct models: cappoint_hologram 9, resupply_locker 4, windowed_door 2,
lantern001 2, then 1 each for c_bet_rocketlauncher, c_xms_cold_shoulder,
c_ttg_max_gun, c_scout_sword, c_boston_basher, c_sandwich, c_pistol, c_pickaxe,
c_hippocrates_bust, comp_win_banner_scaled, orange_cone001, mvm_flask_generic,
medkit_large/medium/small, ammopack_large/medium/small. Those refusals are the 12
untextured ranges (86-74) and the 4 instances (95-91) that the gate was counting.

The engine rule for an out-of-range paint index is to use the model's default
family (0). This round writes that rule down once, in
native/include/material_slot_resolution.h, and applies it on BOTH sides:

    constexpr std::uint32_t kMaxSelectedSkinFamilies = 2;  // red and blu

    inline std::uint32_t effectiveSkinFamily(std::uint32_t familyCount, std::uint32_t requested) {
      if (familyCount == 0) return requested;
      return requested < familyCount ? requested : 0;
    }

    inline std::uint32_t selectableSkinFamilyCount(const ModelMetadata::SkinFamilyTable& skinFamilies) {
      const std::uint32_t count = skinFamilies.skinFamilyCount > 0 ? skinFamilies.skinFamilyCount : 1u;
      return count < kMaxSelectedSkinFamilies ? count : kMaxSelectedSkinFamilies;
    }

resolveMaterialSlot itself is NOT changed: it is the auditor, and "family 1 is
absent from this model" is a true statement the probe must be able to make (the
probe's per-family counts and the gate's synthetic layer are unchanged).

## 3. The product change

    git diff --cached --stat -- native/
    native/include/material_slot_resolution.h | 41 +++++++++++++++++++++++
    native/include/native_renderer.h          |  8 ++++++
    native/src/main.cpp                       | 37 +++++++++++++++++++++++++--
    native/src/native_renderer.cpp            | 13 +++++++++-
    4 files changed, 96 insertions(+), 3 deletions(-)

Line endings: all four are uniformly CRLF (main.cpp 11291/11291, native_renderer.cpp
6872/6872, native_renderer.h 1986/1986, material_slot_resolution.h 136/136).
git diff --check is clean.

  * material_slot_resolution.h -- the two helpers above, with the rule and the
    2026-10-28 evidence in the comment.
  * main.cpp -- entitySkinFamilyCountByKey (cacheKey -> families.size()) filled next
    to entityMeshKeyByPath; the paint loop walks
    family < selectableSkinFamilyCount(prepared.skinFamilies) instead of {0u, 1u};
    the draw clamps the team-derived request with effectiveSkinFamily(); the window
    title gains entitySlots= next to entityModels=.
  * native_renderer.h -- entityModelInstanceSlotCount() / _ as a separate reading.
  * native_renderer.cpp -- entityModelInstanceCount_ = accepted;
    entityModelInstanceSlotCount_ = packed.size(); the slot counter is reset with
    the instance counter at all three sites.

Draw behaviour is unchanged for models that have the requested family; only the
phantom family-1 buckets of single-family models fold into family 0 (86 ranges ->
77 ranges, 9 buckets).

## 4. The gate's readings, before and after

Forward run (bash entity-material-check.sh), same gate, same demo, same tick:

    reading                before (HEAD 5967587)      after (fixed build)
    entityMeshes           68                          68
    entityModels           201  (range-slot sum)       95   (instances drawn)
    entitySlots            -                           201  (the same rows, named)
    entityModelVerts       309870                      304962
    entityMaterials       91/201  FAIL                95/95  OK
    entityMaterialRanges  74/86   FAIL                77/77  OK
    entityMaterialModels  59/59   OK                  59/59  OK
    entityPaintedSlots     191                         191
    ledger                 models=59 resolved=59       models=59 resolved=59
                           uploaded=59                 uploaded=59
    medic slots            21 declared / 19 resolved   21 / 19 (unchanged)
    assertions             22 OK, 2 FAIL, exit 1       24 OK, 0 FAIL, exit 0
    verdict                ENTITY-MATERIAL=FAIL        ENTITY-MATERIAL=PASS

The 24 assertions are the count verify-all.sh:844-859 pins (15 model-layer +
8 run-layer + 1 pixel-layer). entity-material-check.sh was NOT edited: the N/N
assertions held once the two defects were fixed, so no pin had to move and no
25th assertion had to be added (which the step-20 pin would not allow).

Mutation mode of the same gate (bash entity-material-check.sh --mutation):

    witness vs fresh:  blockDiff=30 frameDiff=860765 first=429,309 (107,107,107)->(35,38,37)
    witness vs mutated: blockDiff=0 frameDiff=0
    MUTATION-CAUGHT=PASS

Logs: evidence/entity-material/t60-count-regression/gate-forward-AFTER-FIX.log,
gate-mutation-AFTER-FIX.log, and gate-forward-AFTER-RESTORE.log (the same forward
run repeated after the mutation harness had restored the tree).

## 5. The m12 mutation, re-anchored

Old anchor (dead since 0819fce): the single uploadEntityModelTexture call with
preparedRgba/preparedWidth/preparedHeight. New anchor is the call that exists now,
main.cpp:7427:

    if (renderer.uploadEntityModelTexture(prepared.cacheKey, painted.materialSlot, painted.skinFamily,
          preparedRgba, preparedWidth, preparedHeight)) {

mutate.sh:562-619 hides it behind a constant false (the arguments, and therefore
the whole resolution above them, stay in the binary). The expectation stays at
three named reds and one hold, which is what the step-8 pin (46 red / 6 hold)
requires:

    bash mutate.sh m12
    --- m12 no model SRV reaches the draw (the atlas paints the models again)
    MUTATION-RED   m12 no model SRV -> entity-material gate refuses        exit=1 ENTITY-MATERIAL=FAIL
    MUTATION-RED   m12 no model SRV -> every instance loses its texture    0/95 (fixed=95/95)
    MUTATION-RED   m12 no model SRV -> the ledger's uploaded count drops   0 (fixed=59)
    MUTATION-HOLD  m12 no model SRV -> medic still resolves 19 slots       19 (unchanged)

    RESTORED-IDENTICAL bagel.fixed.txt  proto23  pov  model  fixture  recording
    MUTATION-SUITE=PASS        exit 0

The three reds are the three different readings of the one defect: the gate's
refusal, the instance count, and the per-model upload ledger. The hold is the
synthetic layer: it must not move, or the mutation moved the wrong thing.
Baseline readings were captured on the fixed build at the start of the run and the
tree was rebuilt from the restored sources at the end; the forward gate re-run
after that restore is gate-forward-AFTER-RESTORE.log (24 OK / 0 FAIL / PASS).

Run log: .scratch/m12/run-m12.log (the mutate harness writes its probes to
evidence/mutation/, which is run output, not evidence of this round).

## 6. Step 8 re-run (the whole mutation suite)

Step 8 was PENDING when this document was written (`STEP8-PENDING`, 2026-10-28).
It was re-measured on 2026-10-29 on the same tree and the same staged fix:

    bash mutate.sh          # all 13 cases, under hostlock run step8-suite, 14m43s
    RED=46  HOLD=6  GREEN=0  BROKE=0  RESTORED-IDENTICAL=6
    MUTATION-SUITE=PASS        exit 0

That is the tuple `verify-all.sh:476-488` pins. The 2026-10-28 chain log's
`MUTATION-SUITE=FAIL` (four reds short, cause: m12's dead anchor) is superseded by
this run and is left in `.scratch/verify-all-p0-2026-10-28.log` unedited. Log of
this run: `.scratch/t60-verify/step8-suite.log`.

The same round also re-ran the focused gates T60 can reach. Steps 20/21/16 are
green at their pins (24 / 23 / 18 OK); steps 22 and 23 are **red** — 54 OK + 4 FAIL
and 150 OK + 4 FAIL — because `0819fce` changed which animation table a model
adopts, not because of this round's paint fix. Details, readings and attribution:
`T60-VERIFY-2026-10-29.md` §6. That red blocks the P4 freeze.

## 7. What this does NOT prove

  * The full chain was not run. Step 8 and step 20 were re-run; steps 1-7, 9-19
    and 21-30 were not, and the next chain run must start at step 1.
  * The skin-family clamp has no mutation of its own. m12 removes the upload, not
    the clamp; a mutant that kept the phantom family-1 request would redden the
    same three readings (that is what the pre-fix tree did), but the suite does
    not contain a case that isolates the clamp. Adding one would add a red line
    and break the frozen 46/6 pins in verify-all.sh, which this round may not edit.
  * The gate does not pin the number of instances. entityModels=95 and
    entitySlots=201 are printed (entity-material-check.sh:280) but not asserted;
    N/N with N > 0 would also accept a fix that silently drew fewer instances.
    The count is published so a later round can pin it without guessing.
  * No other gate reads entityModels/entitySlots from the title (checked: only
    entity-material-check.sh parses those fields; verify-all.sh:854 mentions them
    in a comment).
  * The visual result was measured by the gate's pixel layer (30 changed pixels in
    the model block) and by the three counters above; no human looked at the frame.
  * The product fix was compiled with the project's usual warning-only settings;
    no new warnings were introduced by these edits (the build log shows the same
    pre-existing warning families).

## 8. Reproduce

    cd /d/TF2_Native_Test
    bash build-target.sh tf2_demo_native entity_model_probe
    bash entity-material-check.sh                      # forward: 24 OK, PASS
    bash entity-material-check.sh --mutation           # MUTATION-CAUGHT=PASS
    bash mutate.sh m12                                 # 3 red + 1 hold, suite PASS

A gate run after any mutation suite needs a fresh build first: the mutation
harness rebuilds what it mutates, and build-target.sh touches sources when a
header is newer than .headers-stamp (see T61's ordering note).

## 9. State, scope and holds

  * Nothing was committed, merged or pushed. The product fix is staged
    (git add -- native/): the harness refuses to run on a dirty native/ because its
    restore is git checkout -- native/, which restores from the INDEX, so staging
    is what lets the mutation run against the fixed sources without a commit and
    without losing them. Index diff sha256:
    aa412679d2a5399a4a64f357822180dffb23347d04d85f91daba271b07249a06
    (copy kept at .scratch/emfix-product.patch).
  * Files changed by this round: the four product files above plus mutate.sh
    (20 insertions / 10 deletions, comments included) plus this report plus the
    evidence directory. install-lifecycle / verify-all.sh / entity-material-check.sh
    were not touched.
  * The T75 lifecycle change is untouched: it lives on task/t75-cmake, committed
    as ad1cbfe.
  * Host lock: held around each build (owners emfix-r1, emfix-r2, emfix-r3) and
    released by hostlock run at the end of each.
