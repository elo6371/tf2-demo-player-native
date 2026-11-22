# Wearable cosmetics drawn on the player body (task-41, 2026-11-22)

Branch `task/wear41-cosmetics`. Base `53d6e5de` (five-branch merge: task-36/38/39/40 landed).
All numbers in this document were measured on the build this branch produces. What is
committed is the settled ledger lines plus the four crop PNGs under `evidence/cosmetics/`
-- NOT the raw traces: the traces (the bagel 56747 one alone is 2,988,564 lines) and the
`.scratch/` drivers that make them stay in the working tree, so every counter line here is
re-derivable only from that tree. The commands are in section 9. Nothing here is carried over from a predecessor build: the off-path and
lighting pins are byte pins, so they must be re-measured on the exe under test -- and
were (section 5).

The builds cited in this evidence, and how each one's identity was observed, are collected in
`evidence/BUILDS.md`. This card's pins belong to exe
`3a3cb9f1ef348fc0fab27db6784ed6c36777f1ce21abef8d21e0a072b54f4b89` (`git-clean-v1` content
hash `a73f5ff8…`); the baseline column of section 5 is a different, older exe and BUILDS.md
says which.

## 1. What the change does

A wearable (hat, badge, campaign item) is a mesh whose geometry is authored in the SAME
skeleton as the player model it belongs to. It therefore has no world placement of its
own: its bone frames have to be replaced, bone by bone, with the frames the owner model
has *this tick*, and the assembled piece is then drawn exactly where the owner body is
drawn.

    for every wearable render bone:
        find the owner-model bone with the same NAME
            found     -> the owner frame (model space, this tick) replaces the wearable frame
            not found -> the wearable keeps its own (bind) frame, counted bonesBindPose
        owner model has no usable pose this tick
            -> the piece is NOT drawn, counted poseUnavailable (never moved elsewhere)

That is the whole placement rule. There is no origin, no angles and no guessed bone in
it, because task-39 measured `m_vecOrigin`/`m_angRotation` as (0,0,0) on **332 of 332**
wearable instances: an origin fallback would put every hat on the map origin. So:

- bone matching is by **name**, never by index (two models do not share an index space),
- an unavailable owner pose means **dropped and counted**, never placed,
- `CTFWearableVM` rows are routed to the view-model path by task-40 and are **not** merged
  onto a world body (that is how a first-person gun ends up on a player's head).

The drawn pieces join the **player** draw group and its budget: they are part of the
player body, not props. Each piece is one `EntityModelDrawInstance` whose bone table was
appended to the shared per-tick bone buffer and whose instance carries the `boneOffset`
of that slice (`native/include/native_renderer.h:362-367` documents the per-instance
`boneOffset`/`boneCount` and declares them at `:366-367`; `:1168` is
`setEntityBoneMatrices`; `:1249` is the 96-instance `entityModelInstanceCapacityLimit`).
The instance transform is the **body's**: the merged
bone table is already in the body's model space, so placing the piece is the same
operation as placing the body it rides.

Any piece that resolves an owner but is NOT drawn by the player pass is removed from the
prop pass and counted `skippedStandalone` -- the same shape as the held-weapon
`attachSkippedStandalone` counter. A piece is either riding its owner or not drawn; it is
never left standing alone at its own origin.

CLI: `--cosmetics off|basic`, **default off** (`--cosmetics off` is the acceptance path).
`basic` draws the pieces; `off` must not change a single pixel (section 5).

## 2. Where it lives

Everything user-visible is in `native/src/main.cpp`; the placement math is in
`native/src/wearable_pose.cpp` with its contract in `native/include/wearable_pose.h`.

| what | where |
| --- | --- |
| mode enum + default off | `native/src/main.cpp:3415-3416`, set at `:4026`/`:4029` |
| per-tick draw tables | `native/src/main.cpp:13793` (`cosmeticsByOwner`), `:13796` (`cosmeticOwnedEntities`), `:13817` (`cosmeticSkippedStandalone`) |
| owner pre-pass + counters | `native/src/main.cpp:13839-13906` |
| prop-pass cull of an owned piece | `native/src/main.cpp:13930-13940` |
| body bone matrix slice | `native/src/main.cpp:14827-14831` (`draw.boneOffset`, `draw.boneCount`) |
| merge + per-piece draw (skinned arm) | `native/src/main.cpp:14957-15053` |
| piece append to the player group | `native/src/main.cpp:15257-15264` (budgeted by `cosmeticBudget`) |
| reconciliation before the ledger | `native/src/main.cpp:15428-15445` |
| ledger line | `native/src/main.cpp:15473-15482` |
| merge contract | `native/include/wearable_pose.h:69-98` (`WearableBoneMerge`, `buildWearableBoneMatrices`) |
| merge implementation | `native/src/wearable_pose.cpp` |
| per-instance bone slice | `native/include/native_renderer.h:362-367`, `:1168` |

The ledger line is the instrument for "every piece is accounted for". It closes two
identities, both printed on the same line:

    worldRenderable = noOwnerBody + templateRefused + noMesh + demand
    demand          = drawn + overBudget + poseUnavailable

`worldRenderable` is the task-40 attribution table's world-path rows
(`drawPathWorld`), i.e. rows that were NOT routed to the view-model path; the table's own
`renderable` is larger by construction and is printed beside it. `skippedStandalone` is
deliberately NOT part of these sums: it counts pieces removed from the prop pass, a
different population (they are drawn by the player pass, so they are already in `drawn`).

That second identity is the reason a hole had to be closed here. A piece can be attributed,
supplied and still never drawn: its owner body may be counted in `drawnBodyEntities` and
then never reach a draw of its own (no mesh of its own, or dropped by the body budget).
Before the reconciliation those pieces were counted NOWHERE, and a reader comparing
`demand` with `drawn + overBudget + poseUnavailable` would see unexplained arithmetic.
The reconciliation block attributes exactly that remainder to `poseUnavailable`
(`native/src/main.cpp:15428-15445`; the comment at `:15429-15439` states the trade). That
makes `poseUnavailable` a RESIDUAL BUCKET: it holds pieces whose owner pose was missing AND
pieces whose owner body never drew (no mesh of its own, or dropped by the body budget). It
is therefore not a pure pose probe; it deliberately conflates the two, because the
alternative is an open ledger. It touches no rendering state.

## 3. The preserved ledger contradiction (cause and fix)

The resume brief reports that `evidence/cosmetics/ledger.txt` "contradicts itself". It did:
an earlier version of that file said `baseline_vs_off = DIFFERS` while every column
showed the same `bmp_md5 = 4f0a047a2b0a21dac4a692a19680e220`. That is not a rendering
change and both instruments were telling the truth -- they measure different things:

1. `bmp_md5` is the pixel content of a *paused tick-0* frame. Insensitive on purpose: the
   comparison is "does anything move/shade differently", so a matching md5 is the proof,
   not a proxy.
2. The trace text is the sensitive instrument -- and its normalisation masked too little.

Two lines were left unmasked, and both are non-deterministic across runs of the SAME exe:

- `metrics t=0.39 tick=0 iter=3 ...` -- wall-clock frame time. Two runs of one binary
  differ (`.scratch/wear41/paused-baseline.text2` vs `paused-off.text2` show
  `metrics t=0.39` vs `t=0.37`, and the paused control pair `control-a` vs `control-b`
  differed for that reason alone).
- `logstf: runtime ... loader=missing (<pin dir> not loadable)` -- on the baseline exe path,
  `WebView2Loader.dll` was not beside it in `.scratch/pin`, so the baseline printed
  `loader=missing` while the new exe printed `loader=present`. Same pixels, different log
  line.

So the comparison read DIFFERS even though the frame was identical, and it was a *mask*
bug, not a behaviour difference. Note what this implies for reading the old file: a
normalised-trace diff that reports DIFFERS must be read line by line before it is believed,
because an unmasked clock or DLL-presence line is exactly what a "contradiction" looks like.

Fix (in the instrument, not in the renderer):

- `norm()` now masks both `metrics t=<float>` and the trailing ` <float>ms` GPU time, and
  strips CR (the traces are CRLF; without `s/\r$//` the end-anchored masks are no-ops).
- the baseline exe is run with `WebView2Loader.dll` copied beside it in `.scratch/pin`, so
  `loader=` is `present` on every column.
- the script asserts the paused control pair (`control-a` vs `control-b`) is IDENTICAL
  before it asserts anything across exes: if two runs of one binary differ, the run is
  invalid and nothing downstream is worth printing.

After the fix the file is self-consistent (section 5): five columns, all
`bmp_md5 = 4f0a047a2b0a21dac4a692a19680e220`, all comparisons IDENTICAL, including the
control pair. The file on this branch was produced by that fixed instrument on THIS build.

## 4. Lighting

`--lighting` changes the shading of a drawn piece. The CLI takes three values -- `off`,
`basic` and `leaf` (`native/src/main.cpp:3969-4004`; the `leaf` arm is `:3985-3992`) -- and
they become two flags at `:5104-5115`: `leaf` is basic PLUS the per-leaf ambient cube of
task-37 Half B (`native/src/main.cpp:5105-5107`: "Three values, two flags: leaf implies
basic"), not a third alternative to it; `off` is the only value that turns basic off.
Anything else is refused with exit 13 (`native/src/main.cpp:5160-5168`). So the brief
addendum's `basic|leaf` under-reports the switch (it omits `off`) rather than naming a
value that does not exist. The `leaf` arm is not captured in this card: its pins are
`docs/LEAF-AMBIENT-GPU-2026-11-22.md` section 7.

Crops:

Every crop states its mode. The two comparison crops are at the **default (`off`)** so
they are comparable to the off pin; one further crop states `basic`:

- `evidence/cosmetics/onpath-off.png` -- `--cosmetics off`, default lighting (off)
- `evidence/cosmetics/onpath-basic.png` -- `--cosmetics basic`, default lighting (off): the
  crop whose pixel diff is the acceptance proof (the subsection below)
- `evidence/cosmetics/onpath-basic-3x.png` -- the same crop's head/upper band at 3x NEAREST,
  so the piece can be seen rather than inferred
- `evidence/cosmetics/onpath-basic-lit.png` -- `--cosmetics basic --lighting basic`

The `--lighting basic` pin (section 5) is measured with cosmetics at its default (`off`),
so the lighting comparison and the cosmetics comparison never vary two things at once.
`basic-lit` BMP md5 `b134e36ece59346e0506fc359223733a` differs from both, which is what
a shading switch is supposed to do.

### The 1:1 crop proof -- what the pixels show

The accepted subject is `ent=5`, `models/player/scout.mdl`, found by decoding the id-painted
frame (`--world-entity-id`, red byte = entity id exactly, `native/include/entity_id_color.h:22-29`):
5779 painted pixels, bbox `753,561,834,680` (82x120 px). Comparing the SAME box in the
`--cosmetics off` and `--cosmetics basic` frames at tick 600:

    changed=272 px, bbox 32,20,91,133, on_body=262 (0.963 within 6 px of the silhouette,
    0.588 within 2 px)

The pieces the trace resolved for owner 5 are a mask (3 bones: 1 merged by name, 2 kept in
bind pose) and the ozfortress badge (1 bone, merged). The 272 changed pixels are NOT one
head-shaped blob; recomputed from the committed PNGs they fall in two bands:

    rows 12-40   (face)            px=67
    rows 112-133 (lower leg/feet)  px=205  cols 32-48   <- 75% of the diff

So the pixel COUNT does not prove the head placement: the `changed>=80 and
frac_on_body>=0.75` rule is already satisfied by the leg band alone. What shows the mask on
the head is the committed 3x zoom, `onpath-basic-3x.png` (top 55% of the same crop at
NEAREST); the mask is visible over the face in the `basic` crop, and the 67 px face band is
exactly those pixels differing from `off`. The placement claim therefore rests on the
bone-name merge lines plus the 3x zoom, not on the diff count.

Why this is a placement proof and not merely "some pixels changed":

- the diff sits ON the owner silhouette (0.963 within 6 px; the remainder is the few pixels a
  piece is allowed to stick out past the body edge), so the changed pixels belong to the
  owner body and not to the background;
- a piece placed by origin/angles would be at the entity origin -- task-39 measured that
  triple as 0,0,0, i.e. the map origin, which is nowhere near this 82x120 body box; it would
  not appear in the crop at all;
- the same trace lines say the merge happened by bone name (`merged=1` on a 1-bone piece,
  `merged=1 bindPose=2` on the 3-bone mask), so the pixels and the bone bookkeeping agree.

The instrument still has teeth in the other direction: its assertion is `changed >= 80` AND
`frac_on_body >= 0.75`, so a diff in the background is a FAIL, not a pass -- and a piece left
at the map origin would not be inside this crop at all. What the instrument cannot do is tell
one on-body band from another; that is why the 3x zoom is kept beside it.

## 5. Pins on this build

All three pins are BMP md5 values on the stv demo at `--capture-tick 600` with
`--audio-disabled --hidden-window`, plus `AUDIO_DEVICE=6`:

| pass | expected pin | measured on this build |
| --- | --- | --- |
| `--cosmetics off` (acceptance path) | `2ddade356e85dd77b795a7380c254bcb` | **`2ddade356e85dd77b795a7380c254bcb`** (IDENTICAL) |
| no switch (default = cosmetics off) | `2ddade356e85dd77b795a7380c254bcb` | **`2ddade356e85dd77b795a7380c254bcb`** (IDENTICAL) |
| `--lighting basic` | `d927c52639fb10c62a50b3018806a2d3` | **`d927c52639fb10c62a50b3018806a2d3`** (IDENTICAL) |
| `--cosmetics basic` (on path, must differ) | -- | `15cbd4a9fc68118f93151fbd311b655b` (differs, as it must) |

`evidence/cosmetics/identity.txt` is the raw record. It states the exe identity the pins are
bound to (`sha256 3a3cb9f1ef348fc0fab27db6784ed6c36777f1ce21abef8d21e0a072b54f4b89`, identity
tag `a73f5ff84d724a053c0658cfd2556b8dbce6dfa259d33a7dda1d6d66d0c6078b`, `contentMethod
git-clean-v1`) and then the four captures:

    column     switch              bmp_md5                            bmp_bytes
    baseline   (none)              2ddade356e85dd77b795a7380c254bcb   2582406
    no-switch  (none)              2ddade356e85dd77b795a7380c254bcb   2582406
    off        --cosmetics off     2ddade356e85dd77b795a7380c254bcb   2582406
    basic      --cosmetics basic   15cbd4a9fc68118f93151fbd311b655b   2582406

    baseline_vs_off:      IDENTICAL
    baseline_vs_noswitch: IDENTICAL
    basic_vs_off:         DIFFERS

The baseline column is the exe built from the merge base `53d6e5de` -- before any task-41
code existed (`sha256 8e013c47d612f35c4454d020dff361d03c113f4c4b066bdaf26e2160f2657e5c`).
So `--cosmetics off` is byte-identical to a binary that has no cosmetics code at all: the
off path is not merely "unchanged by configuration", it is unchanged by the feature.
Scope of that claim: byte identity is measured on ONE captured frame (tick 600, four passes)
and on the paused tick-0 ledger run (five columns, 980 masked lines each), not across every
frame of live playback -- the draw cadence there is wall-clock (section 3).

`evidence/cosmetics/ledger.txt` proves the same thing a second, stricter way (section 3):
five paused runs, each 980 masked trace lines, every column and every cross-exe comparison
IDENTICAL, `bmp_md5 4f0a047a2b0a21dac4a692a19680e220`, and the control pair
`control_a_vs_b` IDENTICAL -- without that control a matching cross-exe comparison would
prove nothing about determinism.

The lighting pin exists to prove the cosmetics work did not disturb the lighting work:
same tick, same demo, one switch different from the bare pass. Its provenance is
`evidence/t105-model-lighting/REPORT.md:9-14` and `pins.txt:1-13` (four passes: no switch,
`--lighting off`, `--lighting basic`, `--lighting basic --lighting-trace`).

## 6. Counters per demo

The counters are printed once per frame; `evidence/cosmetics/demos.txt` records one settled
line per capture. `worldRenderable = noOwnerBody + templateRefused + noMesh + drawn +
overBudget + poseUnavailable` is asserted (`ACCOUNT=CLOSED`) for FOUR of the five rows below
-- the four captures `.scratch/wear41c-demos.sh` makes (bagel-600, bagel-56747, pov-60162,
stv-negctl). The stv normal row is NOT asserted by that script: it is quoted from
`.scratch/wear41/ledger-basic.trace`, produced by `.scratch/wear41-readings.sh`, i.e. a
different instrument run of the same exe, demo and tick.

| demo | tick | examined | renderable | worldRenderable | drawn | poseUnavailable | skippedStandalone | noOwnerBody | demand |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stv | 600 | 36 | 21 | 20 | **19** | **0** | **5** | 1 | 19 |
| stv, `--player-animation off` (negative control) | 600 | 36 | 21 | 20 | **0** | **19** | **5** | 1 | 19 |
| bagel | 600 | 34 | 21 | 19 | **15** | **0** | **3** | 4 | 15 |
| bagel | 56747 | 40 | 25 | 22 | **19** | **0** | **7** | 3 | 19 |
| POV | 60162 | 37 | 2 | 2 | **0** | **0** | **0** | 2 | 0 |

A disclosure about these rows:

- **bagel rendered nothing.** Neither bagel run found the map (`map: lookup
  map=koth_bagel_rc13 store=none selected=- bytes=0 reason=not found in maps, download/maps
  or a vpk`) and neither drew geometry (geometry capacity 0, static-props 0, sky drew 0).
  Both bagel BMPs are a uniform clear frame -- one colour (14,18,22), 860784 px, uniq=1 --
  which is why their md5 lines are identical (`3305b1d8...`). The bagel COUNTERS are still
  valid, because they are computed from network entity state before any geometry is drawn;
  the bagel md5 lines are evidence of nothing and are not cited as a pixel pin anywhere.

Reading the table:

- `renderable` > `worldRenderable` on stv/bagel is task-40's view-model routing: those
  rows are renderable but must NOT join a world body. The difference is printed, not hidden.
- `skippedStandalone` is the proof the prop-pass cull fires: 5 pieces on stv, 3 and 7 on
  bagel were removed from the standalone prop pass because they ride an owner. Without this
  counter a duplicate hat at the map origin and a correct hat would look the same.
- POV 60162 draws 0 pieces and that is the honest answer, not a failure. Two instruments are
  recorded side by side rather than mixed: task-40's probe table
  (`docs/WEARABLE-ATTRIBUTION-2026-11-21.md:25-27`) reports 17 examined / 4 attributed /
  2 renderable for POV 60162, while this product capture reports examined=37, renderable=2,
  worldRenderable=2. Both rows are reproduced verbatim, one under the other, in
  `evidence/cosmetics/instruments.txt`, so the two instruments can be compared without
  opening two documents and without mixing their columns. They are different instruments
  (probe table vs product frame table), so each number is cited with its source. Both agree
  on the outcome that matters: 2 world renderable rows, both owners out of frame this tick
  (`noOwnerBody=2`), so nothing can be placed.
- `poseUnavailable` is 0 on every normal capture -- the owner pose was available for every
  piece that got as far as placement. Section 7 forces the other case.
- `bonesMerged` / `bonesBindPose` (in the raw lines) are the per-piece merge hit rate: e.g.
  stv 67 merged / 15 bind-pose / 0 invalid, rates 0.817 / 0.183. A bone the owner model does
  not name keeps the wearable's own frame and is counted, never guessed.

Raw: `evidence/cosmetics/README.md` (which file came from which script, and which four
numbers this tree cannot reproduce), `evidence/cosmetics/instruments.txt` (the two POV rows
side by side), `evidence/cosmetics/readings.txt` (stv tick 600, per-piece lines),
`evidence/cosmetics/demos.txt` (per demo + negative control),
`evidence/cosmetics/acceptance.txt` (crop instrument), `evidence/cosmetics/identity.txt` and
`evidence/cosmetics/ledger.txt` (pins), `evidence/cosmetics/weaponattach.txt` (held weapon
unaffected), `evidence/cosmetics/selftests.txt` (probe suites).

## 7. Pose unavailable: the piece is dropped, not placed

The negative control is `--player-animation off`: the body is still drawn, in bind pose, but
it has no animation pose for a piece to ride. On stv tick 600, same demo:

    normal  : drawn=19 poseUnavailable=0  demand=19 skippedStandalone=5   bmp 15cbd4a9fc68118f93151fbd311b655b
    anim off: drawn=0  poseUnavailable=19 demand=19 skippedStandalone=5   bmp 512ccb46ae4ce8999bd8cfd7ce8ec628

19 pieces were owned by bodies in this frame; with the pose gone, all 19 are DROPPED and all
19 are COUNTED `poseUnavailable`; the account still closes (`worldRenderable=20` both sides).
Neither BMP md5 on this page is re-derivable from a committed file -- the two `.bmp` files
and the diff tool that produced the px counts were `.scratch/` artifacts; see
`evidence/cosmetics/README.md`, "Numbers in the card that this tree cannot reproduce".
The pixel evidence here is weak and is not relied on: `stv-negctl.bmp` differs from
`basic.bmp` by 13890 px, but it differs from `off.bmp` by 13725 px -- only 165 px more --
because switching player animation off changes the pose of every body in frame. The
dropped-piece conclusion therefore comes from the counter A/B, not from the pixels: same
demo, same tick, same exe, `drawn=19 poseUnavailable=0` vs `drawn=0 poseUnavailable=19`,
with `worldRenderable=20` on both sides. `drawn=0` with `poseUnavailable=0` is exactly the
failure the accounting hole (section 2) used to produce; this run is the test that it cannot
come back.

Before this branch the cosmetics hook lived only in the skinned arm, so a bind-pose body's
pieces vanished with no counter and the ledger looked innocent -- that was found by review,
fixed by the `notePiecesWithoutOwnerPose` calls in the bind-pose and no-binding arms, and is
now measured by this control. In this control the bucket holds exactly the 19 pose-missing
pieces; in a normal capture it can also absorb pieces whose owner body never drew, so it is a
residual bucket (section 2), not a pose probe.

## 8. Fixtures and negative controls

`demo_open_probe` ships both suites; they run on the shipped probe exe (no demo, no GPU) and
their raw output is `evidence/cosmetics/selftests.txt`.

    native/build-nmake/demo_open_probe.exe --wearable-pose-selftest   -> checks=16 failed=0
    native/build-nmake/demo_open_probe.exe --wearable-fixture-suite   -> checks=10 failed=0

The fixture suite covers the five cases the card requires, each as a synthetic `items_game`
row set decoded through the real table builder:

| required case | fixture line | verdict asserted |
| --- | --- | --- |
| missing property | `missing-owner-property` | `no-owner` + `renderable=0` |
| invalid handle | `invalid-handle-sentinel`, `invalid-handle-past-snapshot` | `no-owner` / `empty-slot` + `renderable=0` |
| deleted-and-reused entity | `deleted-and-reused-slot` | `generation-changed`, owner generation 1 at attribution and 2 at end, `reject=broken-attachment` |
| missing model index | `missing-model-index`, `no-route-is-no-path` | attributed but `no-path`, `renderable=0`, `rejectNoPath=1` |
| schema fallback | `schema-template-loses-to-literal`, `schema-per-class-wins`, `schema-empty-falls-to-precache` | the literal beats a `%s` template; the per-class path wins; an empty schema falls through to precache (`modelPrecache[42]`) |

Every refusal row ALSO asserts `renderable=0`, so a refusal can never be silently drawn.
The pose self-test covers the merge itself: identity owner reproduces the bind frame,
matched-by-name-not-index, unmatched bone keeps its bind frame and is counted, partial merge
reports its reason, no owner pose is refused with a reason, table-size mismatch refused, empty
wearable refused, non-finite owner frame dropped, invalid inverse bind counted and mapped to
identity, every produced matrix finite and affine, bone count reported.

## 9. Reproduce

Build (the only build path used; it is /W4 and links the seven /WX targets):

    bash /d/_shared/tf2-hostlock/hostlock.sh <owner> -- bash tools/lockrun.sh <owner> -- bash build-cmake.sh
    # summary: cmake_build_rc=0  errors=0  warnings=0  exe_count=55  header_stamp=written
    # (that summary line is this build's own echo. The committed /W4 transcript in this
    #  repository is evidence/t105-model-lighting/gates/warn-check.log, and it was produced
    #  on a DIFFERENT tree -- cfd01bd1, 142 TUs. See evidence/BUILDS.md for what inherits
    #  and what does not.)

Pins (stv demo, tick 600, `--audio-disabled --hidden-window`, `AUDIO_DEVICE=6`):

    DEMO="E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
    native/build-nmake/tf2_demo_native.exe --demo "$DEMO" --capture-tick 600 \
        --capture-frame .scratch/off.bmp --audio-disabled --hidden-window
    md5sum .scratch/off.bmp      # 2ddade356e85dd77b795a7380c254bcb
    native/build-nmake/tf2_demo_native.exe --demo "$DEMO" --capture-tick 600 \
        --capture-frame .scratch/basic.bmp --audio-disabled --hidden-window --cosmetics basic
    md5sum .scratch/basic.bmp    # 15cbd4a9fc68118f93151fbd311b655b (differs, as it must)
    native/build-nmake/tf2_demo_native.exe --demo "$DEMO" --capture-tick 600 \
        --capture-frame .scratch/lit.bmp --audio-disabled --hidden-window --lighting basic
    md5sum .scratch/lit.bmp      # d927c52639fb10c62a50b3018806a2d3

Counters, crop and probe suites (all under the same host lock; the capture windows are the
expensive part, so one lock window per group):

    bash .scratch/wear41c-demos.sh    # counters per demo + negative control + probes -> demos.txt
    bash .scratch/wear41c-accept.sh   # ids/off/basic/basic-lit captures + 1:1 crop -> acceptance.txt
    bash .scratch/wear41c-ledger.sh   # deterministic paused ledger -> ledger.txt

The `.scratch/` scripts are scratch instrument code and are deliberately NOT committed (the
brief forbids committing scratch); the committed artifacts are their outputs under
`evidence/cosmetics/`. `evidence/cosmetics/README.md` maps every file in that directory to
the script that produced it, and lists the four numbers in this card that the tree cannot
reproduce (they lived in `.scratch/` BMPs and traces). Each script is short and the important
command lines are the ones spelled out above.

## 10. Findings handed to the next agent

- `native/include/wearable_pose.h:102` says the self-test is reachable through
  `demo_open_probe --cosmetics-pose-selftest`. The probe's real flag is
  `--wearable-pose-selftest` (`native/tools/demo_open_probe.cpp:646-649`, dispatch
  `:730-746`). The header comment is stale; the flag in this document is the real one.
- `--lighting basic|leaf` in the task-41 addendum is incomplete, not wrong: the CLI accepts
  `off|basic|leaf` (`native/src/main.cpp:3985-3992`, applied `:5104-5115`). `leaf` is basic
  plus the per-leaf ambient cube, so the addendum's pair names both lit arms and omits only
  the default. See section 4.
- `native/src/demo_header_entity.cpp:410-424` -- the removed-entity finish-up loop (it clears
  `entityClassByIndex`, `entityStates`, decrements `activeEntityCount` and drops history
  events) -- does NOT advance the slot generation. The advance sites are the Delete arm
  (`native/src/demo_header_entity.cpp:339` advances, `:343-345` records) and the Enter arm
  (`:395-396` advances, `:398-400` records). Known asymmetry; recorded, deliberately not
  "fixed" here.
- An unmasked non-deterministic trace line reads exactly like a rendering difference. Before
  calling two runs different, check the paused control pair (section 3).

## 11. Open risks

- **Domain coverage.** The measured demos are the three task-40 used (stv, bagel, POV) at the
  ticks the attribution table is known at, plus stv tick 600 for the crop. A demo whose
  player models are missing from the local `tf` install degrades to `bindPose` bones and
  will still draw, but the piece may sit at the model bind pose rather than the animated
  pose; the counters distinguish this (`bonesBindPose`), the pixels do not.
- **`%s` schema templates.** Task-40 refuses a template that names no openable file
  (`templateRefused`, `no-path`). Implementing per-class substitution would resolve more
  pieces (stv has 15 of 36 such rows) and is deliberately NOT done here -- the counter is
  honest today, and a wrong substitution would draw the wrong model.
- **Budget.** Pieces are drawn from the player group's prop budget; when the player group is
  already at `kDrawBudget` capacity the pieces are dropped and counted `overBudget`, not
  drawn at a wrong place.
- **View-model route.** `CTFWearableVM` pieces are not merged onto world bodies (task-40
  decision). They are therefore not part of the world-path sums by construction.
