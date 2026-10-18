# T13 — ViewModel multi-range hands

Delivery record for the release blocker F3 ("ViewModel Hands and Arms"), task T13
of `RELEASE-BLOCKERS-NEXT-WAVE-2026-10-17.md`.

| | |
|---|---|
| Task | T13 — ViewModel multi-range hands |
| Branch | `task/t13-viewmodel-hands` |
| Worktree | `D:/TF2_Native_Worktrees/t13-viewmodel-hands` |
| Base commit | `d3ab18a` (`p0-entity-protocol`, "fix: keep skybox out of world depth buffer") |
| Product commit | `9d62883` ("fix: draw every LOD-0 range or refuse the model") |
| Gate + evidence commit | `8bfce68` ("test(t13): a gate for the range table, and the pins the 9996 vertices move") |
| Verification commit | this file, plus `evidence/viewmodel/range/` (see §8) |

---

## 1. What F3 actually was, measured before any change

F3 says:

> `c_sniper_arms.mdl` currently exposes only one 600-vertex range in the visible
> path. The weapon can draw while the hands are absent or incomplete.

The first sentence was **true of an older build and false at the base commit**.
`952ddf2` ("fix: complete model materials and archive validation", one commit
before `d3ab18a`) introduced `ModelLoader::buildModelMeshDataAll`, which decodes
every descriptor at the target LOD. Measured at `d3ab18a` through the product's
own builder (`viewmodel_range_probe`, §3.1):

```
range model=models/weapons/c_models/c_sniper_arms.mdl resolution=found-vpk bodyParts=1
      bones=58 vvdVertices=2215 vtxDescriptors=2 targetLod=0 targetDescriptors=2
      targetIndices=9996 meshBuilt=1 meshRanges=2 meshVertices=9996 materialTable=available
range descriptor bp=0 model=0 lod=0 mesh=0 group=0 vertices=140  indices=600  material=0 name=models/player/sniper/sniper_red
range descriptor bp=0 model=0 lod=0 mesh=1 group=0 vertices=2075 indices=9396 material=1 name=models/player/sniper/sniper_handL_red
```

**The 600-vertex range is the sleeve. The hands are the 9396-index range on
material slot 1.** So the round's job was not "decode more ranges" — it was:

1. prove that the two ranges and their two material slots are separate and both
   drawn (the picture claim),
2. refuse a malformed descriptor instead of drawing the part that survived (the
   contract claim F3's second sentence is really about),
3. make the gate that pinned `vertices=600` say the true thing.

Item 3 was a live red: at `d3ab18a`, `viewmodel-companion-check.sh` failed on
exactly one assertion and nothing else (§2).

---

## 2. The baseline, before the change

`bash viewmodel-companion-check.sh` at `d3ab18a`, unmodified worktree:

```
VIEWMODEL-COMPANION=FAIL
failed assertions:
  witness-a vertices: 9996 != 600
```

105 other assertions held (111 `OK` lines in the log, 6 of them the self-test's
own counters). That single line is the whole defect at the base commit: the
product already drew 9996 vertices and the gate was still asserting
600 — i.e. the gate was reading a stale pin, and the blocker list had inherited
the same stale number.

---

## 3. Changed files, and why each one changed

### 3.1 `native/tools/viewmodel_range_probe.cpp` (new)

Prints the descriptor table and the built range table for one model, through
`ModelLoader::resolveAsset` + `inspect`/`inspectVpk` +
`buildModelMeshDataAll` — the same three calls `viewmodel.cpp` makes.

Why it is a probe and not a gate assertion: the pass reports one vertex count,
and four different causes produce the same count. The probe reports the cause.

Readings per range: descriptor index, first vertex, vertex count, material slot,
material name, the highest bone index the range's vertices reference, and the
number of vertices whose weights do not sum to one.

`--fixture` injects one defect into the decoded model before the builder runs,
because a shipped model cannot show the refusal states:

| fixture | what it breaks | where it lands |
|---|---|---|
| `descriptor-not-triangles` | a target-LOD descriptor's index count is not a multiple of three | `buildModelMeshData`'s "not a bounded triangle list" |
| `descriptor-out-of-bounds` | the descriptor's render indices start past the index array | same guard |
| `bone-out-of-range` | the skeleton shrunk so every vertex's bone index is outside it | `buildBindPoseMesh`'s "out-of-range bone index" |
| `no-descriptors` | the descriptor table emptied | `buildModelMeshDataAll`'s "no renderable descriptors" |

### 3.2 `native/src/model_loader.cpp` — `buildModelMeshDataAll`

Two changes.

**A malformed descriptor now refuses the model instead of being skipped.**

```cpp
// before
if (!buildModelMeshData(inspection, index, part, partError)) continue;
// after
if (!buildModelMeshData(inspection, index, part, partError)) {
  error = "descriptor " + std::to_string(index) + " (bodyPart ... mesh ... lod ...)"
    + " was refused: " + partError + "; the mesh is not built from a partial range set";
  mesh = {};
  return false;
}
```

`continue` is the defect F3's second sentence describes: the ranges that survived
were drawn and the call returned success, so a model whose second mesh was
malformed produced a picture with pieces missing and every counter true of the
part that remained. The refusal names the descriptor, because "the second range
failed" and "the model has one range" are different repairs.

**The LOD to draw is the lowest one the file declares, not "0 if any says 0".**

The old rule was `haveLod0 ? lod==0 : every descriptor`. The otherwise-branch
concatenated LOD 1..3 of the same mesh into one buffer — the same geometry at
three detail levels, drawn on top of itself. Every model in this repository has a
LOD 0, so the branch was never exercised; it was still a wrong answer waiting for
a model that starts at LOD 1. `std::min` over the descriptor table gives the same
answer for every model that has a LOD 0 and a defined answer for the ones that do
not.

A post-condition (`mesh.ranges.size() == wanted`) turns a disagreement between
the scan and the build into a refusal rather than a short range list.

### 3.3 `native/src/viewmodel.cpp` — `applyViewModelMaterialResult`

`flatColorFallback` used to be read from one slot (`materials.drawnSlot`), which
was one slot when the mesh was one descriptor. `c_soldier_arms.mdl` draws three
ranges on three slots and `c_engineer_arms.mdl` four, so a single slot can no
longer answer the question: a model whose first range resolved and whose second
did not is neither "textured" nor "flat colour". The reading is now the count of
ranges whose material slot produced pixels, compared with the range count.

Only `viewmodel_contract_probe` reads this field; no gate asserts it, and the
product's own `flatColor=` trace field is a different reading
(`--viewmodel-flat-color` forced), so this change moves no gate.

### 3.4 `native/src/main.cpp` — two trace additions

`gpu: viewmodel ranges` gained three comma-separated lists in range order:

```
rangeSlots=0,1  rangeVertices=600,9396  rangeTextured=1,1
```

`gpu: viewmodel pass order` gained `ranges=` and `boundSlots=`. The pair is what
distinguishes "two ranges from one material" from "two ranges from two
materials"; `firstRange` alone was silent about every range after the first,
which is precisely the reading F3 needed.

Both are additions to existing lines: every needle a gate already matched still
matches.

### 3.5 `viewmodel-companion-check.sh`

One stale pin replaced and two assertions added:

```bash
assert_eq "witness-a vertices" "$(field "$LAST_ORDER" vertices)" "9996"   # was 600
assert_eq "witness-a ranges" "$(field "$LAST_ORDER" ranges)" "2"
assert_eq "witness-a distinct material slots bound" "$(field "$LAST_ORDER" boundSlots)" "2"
```

The comment above it records why the number moved, so the next reader does not
have to re-derive it from a commit message.

### 3.6 `viewmodel-range-check.sh` (new) — the focused gate

See §5.

### 3.7 `verify-render.sh` — the gate registered as gate 9

Build list gained `viewmodel_range_probe`; `VIEWMODEL_RANGE_EVIDENCE_DIR` is
exported to `$OUT/viewmodel-range` so a tier run does not rewrite the committed
evidence; the final line reads `$ok/10` instead of `$ok/9`.

### 3.8 `native/CMakeLists.txt`

`viewmodel_range_probe` added, linking only `model_loader.cpp` +
`asset_root.cpp` + the demo-header sources: the probe's readings are the parser's
and it never opens a device.

---

## 4. Positive readings

### 4.1 Every shipped arms model decodes every range

`viewmodel_range_probe --tf-root <tf> --model models/weapons/c_models/c_<class>_arms.mdl`,
run 2026-10-18:

| model | LOD-0 descriptors | ranges built | vertices | material slots |
|---|---|---|---|---|
| `c_scout_arms.mdl` | 1 | 1 | 13446 | 0 |
| `c_soldier_arms.mdl` | 3 | 3 | 13074 | 0, 1, 2 |
| `c_pyro_arms.mdl` | 3 | 3 | 8412 | 0, 1, 1 |
| `c_demo_arms.mdl` | 2 | 2 | 10086 | 0, 1 |
| `c_heavy_arms.mdl` | 2 | 2 | 15732 | 0, 1 |
| `c_engineer_arms.mdl` | 4 | 4 | 11736 | 0, 1, 1, 2 |
| `c_medic_arms.mdl` | 2 | 2 | 8640 | 0, 1 |
| `c_sniper_arms.mdl` | 2 | 2 | 9996 | 0, 1 |
| `c_spy_arms.mdl` | 1 | 1 | 11640 | 0 |

Eight of the nine have more than one range, and two of them bind the same slot
twice (`c_pyro_arms` 1,1 and `c_engineer_arms` 1,1) — which is why the gate
asserts `ranges == descriptors` and `boundSlots` separately instead of treating
them as the same number.

### 4.2 The pinned model, range by range

`c_sniper_arms.mdl`: 58 bones, 43 sequences, 3 texture slots, 2215 VVD vertices,
2 VTX descriptors, 0 out-of-bounds, 0 degenerate triangles.

| range | descriptor | vertices | material slot | material | max bone index | bad weights |
|---|---|---|---|---|---|---|
| 0 | 0 | 600 | 0 | `models/player/sniper/sniper_red` | 6 | 0 |
| 1 | 1 | 9396 | 1 | `models/player/sniper/sniper_handL_red` | 57 | 0 |

The bone column is the "model-local index spaces are preserved" reading: the
hands reference bone 57 of 58, i.e. the top of the skeleton, so a decoder that
re-based indices per range would not reach it.

### 4.3 The pass draws both ranges

`tf2_demo_native --demo <pinned POV> --start-paused --capture-tick 0
--viewmodel v_scattergun_scout.mdl`:

```
gpu: viewmodel ranges path=models/weapons/c_models/c_sniper_arms.mdl ranges=2
     firstRange=(slot 0, 600 vertices, textured=1 ...) uploaded=1
     rangeSlots=0,1 rangeVertices=600,9396 rangeTextured=1,1
gpu: viewmodel pass order ... vertices=9996 ... drawnModel=models/weapons/c_models/c_sniper_arms.mdl
     ranges=2 boundSlots=2
```

`boundSlots=2` is the per-frame statement: two distinct material slots were bound
to the device on that draw.

### 4.4 Frames

| frame | mode | sha256 |
|---|---|---|
| bind pose | `--start-paused --capture-tick 0` | `a3d8a09a21418a8e03f57e7b2247925ec637235d50ac56d3613b85ff287fae9f` |
| bind pose, repeated | identical command | `a3d8a09a21418a8e03f57e7b2247925ec637235d50ac56d3613b85ff287fae9f` |
| applied pose | same + `--viewmodel-apply-animation` | `c01a5ada75ce0ebc3a23ddbfe8e0ca740b01fc053d24824b2718a8ed3b95ac39` |

All three frames are 2,582,406-byte BMPs. `skinning=1 vmAnimApplied=1
bindPose=0` on the applied run. The two bind runs are byte-identical;
the applied run differs, so the pose is in the picture. The same two hashes were
reproduced by three separate links of the product (§6.2, §6.3), which is the
cross-check that the rebuilds after the mutation runs are behaviourally the same
binary.

**What the frames actually show** (inspected, not inferred from counters; the
committed BMPs were converted with the repository's `bmp-to-png.py` and the hand
region cropped 4× nearest-neighbour with a throw-away stdlib script —
`inspect-bind.png`, `inspect-applied.png`, `inspect-bind-hands-4x.png`):

- **Bind frame.** The wooden scattergun body is the large brown object across the
  centre. At its left end a gloved hand is visible gripping it: separate fingers
  over the top, knuckles toward the camera, and the arm/sleeve continuing to the
  lower right behind the weapon. That is the 9396-index range on slot 1, drawn and
  textured. The sleeve (the 600-index range on slot 0) is not separable from the
  weapon at this angle.
- **Applied frame.** The frame is a full-screen close-up of featureless brown
  planes — the camera is inside the posed geometry. `skinning=1 vmAnimApplied=1`
  is true and the bytes differ from the bind frame, but the applied pose is **not
  a usable picture**. This is recorded as a limitation, not as evidence of a
  correct pose; it is not investigated in T13 (§7.1).
- **World.** Both frames show the shattered world of F1 (fragments floating in
  clear colour), so the ViewModel's placement against the map cannot be judged
  here. The hand is attached to the weapon; whether it is in the *right* place in
  a world frame is not something this round can say.

**Why paused.** A playing capture is armed by `tick >= captureTick` and grabbed
on the next draw, so the tick it lands on depends on how many ticks the loop
advanced per frame — which depends on the host. Measured during this task: the
same command twice, with a parallel task's product running, produced frames that
differed by 38.7% of their pixels with a full-frame bounding box, because one
capture landed a tick later. `frame-capture-check.sh` already draws this line
("the paused scene is static, so two runs must agree"); this gate uses the same
criterion. A paused frame is a fixed input; a playing one is a race with the
scheduler.

---

## 5. The focused gate

`bash viewmodel-range-check.sh` — five sections plus two mutations.

| section | what it asserts |
|---|---|
| 0 | the pinned recordings match their recorded size and sha256 (a missing or changed file is a FAIL, not a skip) |
| 1 | all nine arms models build, with `ranges == targetDescriptors`, `vertices == targetIndices`, and the pinned per-model range counts |
| 2 | the sniper's two ranges, their slots, their material names, their bone reach and their weight sums |
| 3 | four malformed descriptors each refuse the whole mesh with a named cause, and the unmodified model still builds |
| 4 | the pass reports `ranges=2 boundSlots=2`, the upload reports `rangeSlots=0,1`, two identical runs give identical frame bytes, and the applied pose differs |
| 5 (`--mutation`) | two single-line defects in the product's own source |

Mutations:

| mutation | file | what it does | the readings that must move |
|---|---|---|---|
| `partial-ranges-drawn` | `model_loader.cpp` | the refusal branch becomes `&& false`, i.e. the old skip | five: `meshBuilt=1` (pinned 0), `meshRanges=2` (pinned 0), range 0 `count=0` (pinned 600), range 0 `material=-1` (pinned 0), `meshVertices=9396` (pinned 9996) |
| `first-range-drawn` | `main.cpp` | the draw-range loop breaks after the first range | `ranges=1` and `boundSlots=1` on the pass line and on the upload line (both pinned 2), plus the frame hash |

`first-range-drawn` is the F3 symptom reproduced at the draw layer, and it is the
machine-checkable form of "the hands are in the picture": `vertices=9996` does
not move — the buffer is the control that separates "a range stopped being
drawn" from "the geometry changed" — one Draw disappears, and the frame's bytes
move from `a3d8a09a…` to `5b188c94…`.

Both mutations are **witnessed, not detected**: the gate never runs as the
verdict of a mutated build, so there is no red assertion to count. What is
checked instead is that the readings the gate pins are load-bearing — each one
takes the defect's value *and* that value differs from the pin, both halves
asserted. A mutation that only broke the build, or whose defect did not
reproduce, is counted as a failure, not a catch.

Both mutations are restored byte-for-byte and the restore is asserted twice —
`cmp` against the backup and a sha256 comparison against the pre-mutation hash —
then the target is rebuilt and the rebuild's exit status is asserted.

Host serialisation: the gate records whether another `tf2_demo_native.exe` was
live (`host_overlap=1`, written into the evidence and printed with the verdict)
rather than refusing to run. Refusing was the first version, and it was wrong for
two reasons: the capture is paused, so a neighbour cannot move the picture
(measured: two paused captures byte-identical with four product processes live),
and the refusal itself left its lock directory behind — it ran before
`trap cleanup EXIT` was registered, so the next run could never start. The
repeat-hash assertion is the real guard.

---

## 6. Verification commands and results

Every run below was made on `task/t13-viewmodel-hands` at `8bfce68`, in one shell,
serially, on 2026-10-18 (times are UTC+8). The complete logs and a per-run summary
are committed under `evidence/viewmodel/range/logs/` with `logs.sha256`; the paths
in the table are relative to that directory.

### 6.1 The runs, in the order the task names them

| # | command | start–end | exit | verdict |
|---|---|---|---|---|
| 1 | `bash viewmodel-animation-check.sh` | 18:21:00–18:32:29 | 0 | `VIEWMODEL-ANIMATION=PASS` (154 OK, 0 FAIL) |
| 2 | `bash viewmodel-companion-check.sh` | 18:32:37–18:42:12 | 0 | `VIEWMODEL-COMPANION=PASS` (114 OK, 0 FAIL) |
| 3 | `bash viewmodel-companion-check.sh --mutation` | 18:42:22–18:58:25 | 0 | `MUTATION-CAUGHT=PASS` (6 of 6) |
| 4a | `bash build-target.sh tf2_demo_native viewmodel_animation_probe viewmodel_range_probe` | after 18:58:25 | 0 | both lines `rc=0` |
| 5 | `bash viewmodel-range-check.sh` | 18:58:50–19:02:45 | 0 | `VIEWMODEL-RANGE=PASS` (74 OK, 0 FAIL) |
| 6 | `bash viewmodel-range-check.sh --mutation` | 19:02:54–19:08:48 | 0 | `MUTATION-CAUGHT=PASS` (2 of 2), 88 assertions held |
| 4b | `bash build-target.sh tf2_demo_native viewmodel_range_probe` | after 19:08:48 | 0 | `rc=0` |
| 7 | `bash viewmodel-range-check.sh` (post-rebuild) | 19:09:05–19:12:57 | 0 | `VIEWMODEL-RANGE=PASS` (74 OK, 0 FAIL) |
| 8 | `bash viewmodel-range-check.sh` (quiet host, replaces the committed frames) | 19:16:03–19:20:06 | 0 | `VIEWMODEL-RANGE=PASS` (74 OK, 0 FAIL), `host_overlap=0` |

Log files: `1-animation-forward.log`, `2-companion-forward.log`,
`3-companion-mutation.log`, `4a-rebuild-after-companion-mutation.log`,
`5-range-forward.log`, `6-range-mutation.log`,
`4b-rebuild-after-range-mutation.log`, `7-range-forward-post-rebuild.log`,
`8-range-forward-clean-host.log`. The two rebuilds are the two halves of task
step 4: `4a` rebuilds what the companion mutations touched before the range gates
run on a restored tree, `4b` does the same after the range mutations.

Run 8 exists because run 7 recorded `host_overlap=1` — a product process from the
mutation suite was still exiting when it started. The paused capture is
insensitive to that (runs 5 and 7 produced the same frame hashes anyway), but the
committed evidence should not say "another process was live" when a four-minute
re-run can say it was not. Run 8 started with zero `tf2_demo_native.exe` on the
host, recorded `host_overlap=0`, reproduced both frame hashes, and its readings are
the ones `evidence/viewmodel/range/` now holds. The only difference between the
committed evidence and `HEAD` is that line: `hostOverlap=1` → `hostOverlap=0`.

### 6.2 Executable hashes, and why two links are not one hash

| stage | `tf2_demo_native.exe` sha256 | `viewmodel_range_probe.exe` sha256 |
|---|---|---|
| before run 1 (the binary already in the tree, mtime 14:59) | `fc18151771504ba71215394891b622ec08fd1436a87ab0e7cb61a5b185655774` | `e3ce9a6b2efc355acdf77ca458f0add8c1ec3637c28d5e51b246d33fd5de3bbd` |
| after 4a, used by run 5 | `fb05326afd7c4e8fcab27d4e9d75239c119da79823bd0593566964cd0524c1ce` | `e3ce9a6b…` (unchanged) |
| after 4b, used by runs 7 and 8 | `685e0c0f1f09fa031b5f70b575e775d85e840b46ba4147845c8d8f67b1bede7f` | `d685779e14b2bb66cfe4dea58c723df50bc64b7ea888664dba7493411c1b4b00` |

The three hashes are three links of the **same sources**. `git hash-object` for
every source the product compiles matches `HEAD` before and after the mutation
runs (§6.4), so this is not drift; MSVC embeds the link time in the PE header
(`TimeDateStamp=0x6ad4a8b1` = 19:08:33 local on the final binary) and the tree has
no `/Brepro`, so two links of one source tree cannot be byte-identical. The
probe's hash moved between 4a and 4b for the same reason although its own file did
not change.

The cross-check that the rebuilds are behaviourally the same binary is §6.3: two
different links (`fb05326a…` and `685e0c0f…`) produced byte-identical frames and
identical pass readings.

### 6.3 Frames, repeats, and what moved

| reading | value |
|---|---|
| bind frame (`--start-paused --capture-tick 0`), runs 5, 7, 8 | `a3d8a09a21418a8e03f57e7b2247925ec637235d50ac56d3613b85ff287fae9f` |
| bind repeat, same run | identical to the bind frame in every run |
| applied frame (`--viewmodel-apply-animation`) | `c01a5ada75ce0ebc3a23ddbfe8e0ca740b01fc053d24824b2718a8ed3b95ac39` |
| frame bytes | 2,582,406 each |
| pass readings (runs 5, 7, 8) | `ranges=2 boundSlots=2 vertices=9996`, upload `rangeSlots=0,1 rangeVertices=600,9396` |
| mutation `first-range-drawn` frame | `5b188c94578d400e2d9b96953eb4029929d8b1fbe6eeffeef60557c0789117ba` |

### 6.4 Source restoration, proved against `HEAD` rather than against a backup

The companion suite caught six defects and the range suite two. Each printed the
restored file's hash and asserted the rebuild:

```
mismatch-off, range-off, bone-count-off, model-index-zero-off:
  source restored byte-for-byte (b965abda483e) / the restored tree rebuilt
force-v-model, owner-cleared:
  source restored byte-for-byte (143528d498f0) / the restored tree rebuilt
partial-ranges-drawn:
  native/src/model_loader.cpp restored byte-for-byte (10c14ad8dc10…)
first-range-drawn:
  native/src/main.cpp restored byte-for-byte (1b0c3c3b810c…)
```

The companion suite's twelve-character values are **Git blob** hash prefixes
(`b965abda483e`, `143528d498f0`), so they can be checked against the commit
directly instead of against a scratch backup. The range suite prints the full
SHA-256 of the working file instead. Measured after the last mutation run, with
`git hash-object --path=<file> <file>`:

| file | worktree blob | `HEAD:` blob | result |
|---|---|---|---|
| `native/src/viewmodel.cpp` | `b965abda483ec82b6dd09b82108ed99dda56538e` | same | MATCH |
| `native/src/main.cpp` | `143528d498f089fd9714d02e0b8f8787490c2a94` | same | MATCH |
| `native/src/model_loader.cpp` | `4ed51a26f834d1ea882ea769f76f35eb914faa20` | same | MATCH |
| `native/tools/viewmodel_range_probe.cpp` | `4213e865356bdf10412672680fdbdb9f02f242d3` | same | MATCH |

`git status --porcelain -- native/` is empty. (The range log's
`10c14ad8…`/`1b0c3c3b…` are SHA-256 of the CRLF working copy; the blob hashes
above are the same content after the checkout filter normalizes CRLF to LF, and
they are what a reviewer can reproduce from the commit.)

### 6.5 Residual process and lock state

- `Get-CimInstance Win32_Process -Filter "Name='tf2_demo_native.exe'"` returns
  nothing at the end of the sequence; no probe process either.
- `.scratch/viewmodel-animation.lock`, `viewmodel-companion.lock` and
  `viewmodel-range.lock` are all absent.
- No other worktree's product was started, stopped, or inspected.

### 6.6 One environment repair, outside the repo

Run 1's first attempt died before it read anything:
`FATAL: missing interpreter: C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe`.
That path is a symlink the runtime installer created; it pointed at
`/e/workbuddy123/resources/vendor/python`, and the install directory has since been
renamed to `/e/workbuddy`. The link was repointed to the live vendor tree
(Python 3.13.14) and run 1 was restarted. No repository file was changed for this;
`viewmodel-animation-check.sh` still names the path it always named, and the
sibling gates' fallback list already reaches the same interpreter.

### 6.7 Reproduce

```bash
cd /d/TF2_Native_Worktrees/t13-viewmodel-hands
bash build-target.sh tf2_demo_native viewmodel_range_probe viewmodel_animation_probe
bash viewmodel-animation-check.sh            # VIEWMODEL-ANIMATION=PASS, rc 0
bash viewmodel-companion-check.sh            # VIEWMODEL-COMPANION=PASS, rc 0
bash viewmodel-companion-check.sh --mutation # MUTATION-CAUGHT=PASS (6/6), rc 0
bash viewmodel-range-check.sh                # VIEWMODEL-RANGE=PASS, rc 0
bash viewmodel-range-check.sh --mutation     # MUTATION-CAUGHT=PASS (2/2), rc 0
```

The gates take the GPU and the frame paths under `.scratch/`; run them one at a
time, not in parallel.

---

## 7. What is not verified, and what inspection found

Counters and hashes are not eyes. Specifically:

1. **The frames were inspected, and the applied pose is wrong in the picture.**
   §4.4 records what the committed bind and applied frames show. The bind frame
   has the weapon body and a gloved hand gripping it, so F3's "hands absent" is no
   longer true. The applied frame is a full-screen close-up of geometry: the pose
   is applied (`skinning=1 vmAnimApplied=1`) but is not visually usable, and the
   cause was not investigated in this task. Correctness of hand placement,
   orientation and clipping in a real world frame remains unverified — F1's world
   geometry makes that judgement impossible here.
2. **Only the sniper's model has a frame.** The other eight arms models are read
   at the descriptor/range level (§4.1) but not captured, because the pinned POV
   recording is a sniper.
3. **The weapon body's material state is T14's**, not this task's. The arms pass
   and the weapon-body pass share the ViewModel draw call but not the material
   table; this gate pins the arms' slots and says nothing about the weapon's.
4. **`--viewmodel-flat-color` is not exercised here.** The control that forces
   flat colour lives in `viewmodel-material-check.sh`.
5. **Paused only.** No claim is made about a playing frame's byte stability; §4.4
   explains why, and F1 (world determinism across two identical runs) is the task
   that owns that question.
6. **`verify-render.sh` was not run in full** for this task. The blocker list's
   policy §6 says to run the focused gates while a blocker is being developed, and
   the full tier is 55+ minutes; the gate is registered as gate 9 and the tier's
   `$ok/10` line was updated, but the tier itself is the merge-time check.

---

## 8. Final state

| | |
|---|---|
| branch | `task/t13-viewmodel-hands` |
| worktree | `D:/TF2_Native_Worktrees/t13-viewmodel-hands` |
| base commit | `d3ab18a` (`p0-entity-protocol`) |
| product commit | `9d62883` |
| gate + evidence commit | `8bfce68` |
| delivery commit | the commit that carries this file and `evidence/viewmodel/range/logs/` — evidence and documentation only, no product or gate change |
| push status | **not pushed.** No `origin/task/t13-viewmodel-hands` ref exists; the branch is local to this worktree. Integration into `p0-entity-protocol` is the integrator's step, as for T11 and T12. |
| final product binary | `685e0c0f1f09fa031b5f70b575e775d85e840b46ba4147845c8d8f67b1bede7f` |
| final range probe | `d685779e14b2bb66cfe4dea58c723df50bc64b7ea888664dba7493411c1b4b00` |
| final bind frame | `a3d8a09a21418a8e03f57e7b2247925ec637235d50ac56d3613b85ff287fae9f` |
| final applied frame | `c01a5ada75ce0ebc3a23ddbfe8e0ca740b01fc053d24824b2718a8ed3b95ac39` |
| worktree after the delivery commit | clean; no untracked file outside the ignored `.scratch/` |
| residual processes | none (no `tf2_demo_native.exe`, no probe, no lock directory) |

### Changed files, and why

| file | change |
|---|---|
| `native/src/model_loader.cpp` | `buildModelMeshDataAll` refuses a malformed descriptor instead of skipping it; draws the lowest LOD the file declares; asserts the range count as a post-condition |
| `native/src/viewmodel.cpp` | `flatColorFallback` counts resolved ranges instead of reading slot 0 |
| `native/src/main.cpp` | per-range `rangeSlots`/`rangeVertices`/`rangeTextured` on the upload line, `ranges`/`boundSlots` on the pass line |
| `native/tools/viewmodel_range_probe.cpp` | new probe: descriptor table and built range table, with four injectable defects |
| `native/CMakeLists.txt` | registers `viewmodel_range_probe` |
| `viewmodel-range-check.sh` | new focused gate (5 sections + 2 mutations) |
| `viewmodel-companion-check.sh` | stale `vertices=600` pin → `9996`; `ranges`/`boundSlots` assertions added |
| `verify-render.sh` | registers the new gate as gate 9 of 10 |
| `evidence/viewmodel/range/` | readings, both frames, three inspection PNGs, and the nine run logs with `logs.sha256` |
| `.gitattributes` | `evidence/viewmodel/range/logs/** -text`, so `core.autocrlf=true` cannot rewrite the archived logs and break `logs.sha256` on the next checkout |
| this file | delivery record |

No file outside T13's scope was touched. The uncommitted changes in the main
worktree (`evidence/world-material/real-cp_snakewater_final1.txt`, the two
untracked task lists) were left exactly as they were; nothing from them was
staged or committed here.

