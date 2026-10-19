# T18 — the applied ViewModel pose is a picture, not a reading

**Task:** `NEXT-TASKS-2026-10-19.md`, T18 "ViewModel applied-pose camera and animation
frame".
**Problem as stated:** "T13 proves that skinning and animation are applied, but the
`--viewmodel-apply-animation` evidence frame is a camera-inside-model close-up and is
not visually acceptable."

---

## 0. Verdict

The frame was not a camera problem. The camera was exactly where the placement solve
put it, on both runs, and its numbers are identical to the last decimal between the
bind and the applied capture. The geometry was in the wrong place: the ViewModel
vertex shader read its bone table as a **transposed** matrix, so every vertex was
rotated about the model origin instead of about its bone. The model is 29.88 units
across and its bones sit roughly 52 units from that origin, so a hand rotating a few
degrees threw the mesh across tens of units and the framing followed.

Fixed in the shader's bone read. The applied frame is now the same first-person
composition as the bind frame with the hand rotated on the grip, and the difference
between the two frames went from **192,357 pixels across the whole frame** to
**10,097 pixels inside a 262×81 box**, with 2 pixels outside the ViewModel's own
coverage.

T18's step 2 — "compare camera matrices, model origin, FOV, hand pose, sequence time
and weapon attachment" — is answered by identity, not by assertion: the product's own
`gpu: viewmodel frame` line is byte-identical on the bind and applied runs.

| reading | bind | applied |
|---|---|---|
| eye | `(-0.09,0.22,0.70)` | `(-0.09,0.22,0.70)` |
| forward | `(-0.354,0.891,0.283)` | `(-0.354,0.891,0.283)` |
| placement | `(-31.70,55.50,12.75)` | `(-31.70,55.50,12.75)` |
| fov / near / far | `90.00 / 1.4940 / 185.2539` | same |
| distance / radius | `65.7352 / 29.8797` | same |
| hand | `(1,0,0,0,0,1,0,0,0,0,1,0)` | same |
| attachment | `weapon_bone` | same |
| mesh centre, clip | `(4.829,-6.304,62.886,63.873)` | same |
| ndc | `(0.076,-0.099)` | same |

---

## 1. Repository state

| item | value |
|---|---|
| worktree | `D:/TF2_Native_Worktrees/t18-apply-camera` |
| branch | `task/t18-apply-camera` |
| base commit | `e67630b` — *Merge branch 'task/t15-vpk-build' into p0-entity-protocol* |
| product commit | `fba06aa` — *fix(viewmodel): the bone table was read as its own transpose* |
| gate and evidence | `a6ddc50` — *gate(t18): judge the applied pose by its picture and by the base commit* |
| report commit | the docs-only commit that adds this file on top of `a6ddc50` |
| push status | not pushed; no remote command was run |

A detached worktree at `e67630b` was created for the control experiment in §7.5
and removed afterwards (`git worktree remove --force`, then `git worktree prune`).

`git status --short` on the main worktree was read before starting and its four
pre-existing entries (`M evidence/world-material/real-cp_snakewater_final1.txt`,
`?? NEXT-TASKS-2026-10-18.md`, `?? NEXT-TASKS-2026-10-19.md`,
`?? RELEASE-BLOCKERS-NEXT-WAVE-2026-10-17.md`) were not touched: every edit below is
inside this worktree.

---

## 2. What the applied frame was actually showing

### 2.1 The measurement that separated framing from geometry

The first thing this round added is a reading that says where the pose puts the
vertices, taken from the vertices that were uploaded and the matrices that were
uploaded — not from a second model of them:

```
gpu: viewmodel pose extent bindCenter=(0.000,52.050,-1.801) bindRadius=29.880
    posedCenter=(-0.009,52.050,-1.801) posedRadius=29.887 centerShift=0.009
    vertices=9996 posedVertices=9996 nonFinite=0 maxDisplacement=6.126 valid=1
```

On the broken build this reading said **the mesh was healthy**: the box had barely
moved. It was not lying about its own numbers; it was answering a question that
cannot see the defect. `p < min` and `p > max` are both false for NaN and both
"unchanged" for a vertex that has been flung somewhere else inside a box that already
contains the whole model. So the box was replaced in two ways:

* `nonFinite` — a count of posed vertices whose components are not finite, which a box
  cannot report at all;
* `maxDisplacement` — how far the worst vertex moved, which a box that is bigger than
  the displacement can never show.

**What this reading could and could not catch, precisely.** It is a second
implementation of the same skinning rule — `skinVertex` mirrors the shader's
three-weight sum — so it cannot see a defect that lives only in the shader's read of
the table, and it did not. Its value was the opposite: it **exonerated the data**. On
the broken build it reported 9,996 of 9,996 vertices posed, no non-finite component,
and a maximum displacement of 6.126 units, against a picture that filled half the
screen. A correct result from the matrices plus a wrong picture from the same matrices
moves the question from "are the uploaded transforms wrong" to "is the shader reading
them wrong", and that is the question that had an answer.

`nonFinite` and `maxDisplacement` were added because the first version of this reading
reported a healthy box for a torn mesh, and neither a NaN nor a displacement smaller
than the box's own size is visible to min/max. They guard the reading itself; they are
not what caught this defect.

### 2.2 The defect

`native_renderer.cpp` declared the bone table as

```hlsl
StructuredBuffer<float4x4> viewModelBones : register(t4);
```

and `animation_binding.cpp:41` (`toColumnMajor`) stores, for each bone, the **transpose**
of the Source `matrix3x4_t` — that is what makes `mul(float4(p,1), M)` reproduce
`M_source * p` in a column-major pipeline. HLSL packs matrices in structured buffers
**column-major** by default (`/Zpc`), so the shader read each stored transpose back as
a matrix again and applied the transpose of the intended transform: the 3×3 rotation
was inverted and the translation was read out of the wrong floats — zero, in this
layout.

The consequence is not "slightly wrong pose". A rotation inverted about the origin
moves a vertex by up to twice its distance from that origin. The mesh centre is 52
units from the origin; the whole model is 30 units across. Every vertex left the
model's own volume, which is exactly the "camera inside a featureless close-up" T13
recorded and could not explain.

`row_major` is not available as an escape hatch here: `StructuredBuffer<row_major
float4x4>` fails to parse (`X3000: unexpected token 'row_major'`) and
`row_major StructuredBuffer<float4x4>` is rejected as a non-matrix declaration
(`X3077`). The fix therefore assembles the matrix by hand out of four `float4`s.

### 2.3 Why the tree could not see it

`--viewmodel-identity-bones` is the instrument that was supposed to prove the skinning
path, and it uploads an identity per bone. **An identity matrix is its own transpose.**
Every assertion built on that instrument — including T2's byte-identical frame pair —
is blind to this defect by construction. `skinning=1`, `boneTableCoverage=58/58` and
`vmAnimApplied=1` were all correct while the mesh was in pieces, because they are
statements about the branch, not about the geometry it produced.

That is the reason the gate added this round judges a **picture inside the ViewModel's
own coverage** and then proves the judgement discriminates, instead of adding another
counter.

---

## 3. The fix

`native/src/native_renderer.cpp`:

* the declaration became `StructuredBuffer<float4> viewModelBones : register(t4);`
* a helper assembles the matrix from the four stored columns:

```hlsl
float4 skinByBone(float3 vertexPosition, float weightW, uint boneSlot) {
  uint first = boneSlot * 4;
  float4 c0 = viewModelBones[first + 0];
  float4 c1 = viewModelBones[first + 1];
  float4 c2 = viewModelBones[first + 2];
  float4 c3 = viewModelBones[first + 3];
  return float4(
    vertexPosition.x * c0.x + vertexPosition.y * c1.x + vertexPosition.z * c2.x + weightW * c3.x,
    vertexPosition.x * c0.y + vertexPosition.y * c1.y + vertexPosition.z * c2.y + weightW * c3.y,
    vertexPosition.x * c0.z + vertexPosition.y * c1.z + vertexPosition.z * c2.z + weightW * c3.z,
    weightW);
}
```

* the upload sizes the buffer as float4s: `StructureByteStride = sizeof(float) * 4` and
  `NumElements = capacity * 4`. The bytes on the GPU are unchanged; only the stride
  that tells the shader how to find bone *N* changed. A 64-byte stride with a 16-byte
  element type would make every bone after the first read the wrong 16 floats.

The normal first-person camera was not touched. The placement solve, the FOV, the
near/far, the hand matrix and the attachment are the same code as before, and the
table in §0 is the proof that they produced the same numbers on both runs.

**A second, latent defect was fixed in the same pass.** `scene: vmAnim entity=` printed
the *pre-override* sequence in `sequence=`, so a run with `--viewmodel-sequence 1`
against a recording whose entity carries sequence 29 printed
`sequence=29 sequenceOverride=1` — two fields that contradict each other. It now
prints `sequence=1 entitySequence=29 sequenceOverride=1`.

---

## 4. Changed files

| file | change and why |
|---|---|
| `native/src/native_renderer.cpp` | The defect and its fix (§3). Also publishes `viewModelFramePlanes_` (near/far/distance/radius) so the gate can assert the framing from the product's own numbers instead of re-deriving it. |
| `native/include/native_renderer.h` | `viewModelFramePlanes()` accessor and the member behind it. |
| `native/include/viewmodel.h` | `ViewModelPoseExtent` — the measurement of §2.1, including the two fields a bounding box cannot produce (`nonFinite`, `maxDisplacement`). |
| `native/src/viewmodel.cpp` | `measureViewModelPoseExtent`, a CPU mirror of the shader's three-weight rule over the same vertices and the same matrices that were uploaded. Non-finite vertices are counted and kept out of the box. |
| `native/src/main.cpp` | Emits the extent reading; prints the effective and entity sequences separately; extends `gpu: viewmodel frame` with fov, near, far, distance, radius, hand and attachment. |
| `viewmodel-apply-check.sh` | New gate, 6 sections, `--mutation`. See §5–§7. |
| `patch-source.py` | New helper. One exact text substitution, matched against the file's own newline, refusing a needle that is absent or ambiguous. Written because the first two attempts at mutating a CRLF file from a bash heredoc silently did nothing. |
| `evidence/viewmodel/apply/` | `summary.txt`, the six frames, and three inspection images. |
| `evidence/viewmodel/range/` | Re-pinned by running that gate: `product-ranges.txt` and the two frames it names. The bind frame's move predates this task (§7.5); the applied frame's is this fix. |
| `T18-VIEWMODEL-APPLY-2026-10-19.md` | This report. |

Nothing in `verify-all.sh` was modified.

---

## 5. Commands and exit codes

All on `task/t18-apply-camera`, run one at a time on the host.

```
bash build-target.sh tf2_demo_native viewmodel_animation_probe viewmodel_range_probe
  rc=0   1m49s  (the probes were not built in this worktree; two of the three
                 sibling gates refuse to start without them)

bash viewmodel-animation-check.sh     rc=0   684s   VIEWMODEL-ANIMATION=PASS
bash viewmodel-companion-check.sh     rc=0   474s   VIEWMODEL-COMPANION=PASS
bash viewmodel-range-check.sh         rc=0   220s   VIEWMODEL-RANGE=PASS (74 OK, 0 FAIL)

bash viewmodel-apply-check.sh --mutation  rc=0  358s  MUTATION-CAUGHT=PASS (4/4)
                                                      VIEWMODEL-APPLY=PASS (13 assertions, 0 failures)
bash viewmodel-apply-check.sh             rc=0  269s  VIEWMODEL-APPLY=PASS (41 assertions, 0 failures)
```

The forward run is last on purpose: the evidence file has to describe the tree the
commits contain, and no capture may be reused from a run that happened before an
edit. The three sibling gates were run before it for the same reason — their
verdicts are about the same source, and running them afterwards could only
invalidate the frames if they failed.

---

## 6. Results — the forward gate

`VIEWMODEL-APPLY=PASS (41 assertions, 0 failures)`.

The gate is six sections:

1. **pinned recording** — one recording, checked against size and sha256 in
   `evidence/viewmodel/animation-inputs.txt`. Nothing else is accepted.
2. **the contract and the effect, on the product's own lines** — `vmAnimInput=ok`,
   `vmAnimApplied=1`, `skinning=1`, `boneTableCoverage=58/58`,
   `animationInput=bones:58`, `sequenceInRange=1`, `drawnModel=c_sniper_arms.mdl`,
   `drawCalls=1`, `ranges=2`, `boundSlots=2`, `frame=0/1`, and the override trio
   `sequence=1 entitySequence=29 sequenceOverride=1`.
3. **the camera and the framing did not move** — the two `gpu: viewmodel frame` lines
   and the two `gpu: viewmodel centre viewSpace` lines compared as text, plus the
   individual numbers in §0 and the rule `distance ≈ 2.2 × radius`.
4. **the pose extent** — `posedVertices == vertices`, `nonFinite == 0`, `valid == 1`,
   `centerShift <= bindRadius`, `maxDisplacement <= bindRadius`, and the posed box
   within half to twice the bind box.
5. **the picture** — the applied frame repeats byte for byte, differs from the bind
   frame, and both the textured and the flat-colour pairs stay inside the ViewModel
   mask.
6. **mutations** — §7.

### The picture, measured

```
bind vs applied, textured   changed=10097 inside=10095 outside=2  mask_pixels=25601
                            bbox=(x=577..838, y=313..393) w=262 h=81   status=PASS
bind vs applied, flat       changed=5586  inside=5584  outside=2  mask_pixels=25601
                            bbox=(x=579..838, y=316..383) w=260 h=68   status=PASS
```

The mask is built from the world-only capture of the same tick: a pixel where that
frame and the bind frame differ is a pixel the ViewModel covers. 10,097 changed pixels
against a 25,601-pixel coverage, and 2 pixels outside it — the pose legitimately moves
the silhouette by a pixel or two at the hand. The same measurement on the defect is
`changed=192357 outside=169981` (§7, m2).

---

## 7. Results — the mutations

`MUTATION-CAUGHT=PASS (4/4)`. Each defect was applied to the source, built, captured
and measured, then restored and proven byte-identical by sha256 and `cmp`.

| # | defect | what the gate reads instead | defect reading | pinned |
|---|---|---|---|---|
| m1 | the pose is resolved and the upload is handed an empty table | `vmAnimApplied`, `skinning`, and the frame hash | `vmAnimApplied=0`, `skinning=0`, applied frame **equals the bind frame byte for byte** | `1`, `1`, a different frame |
| m2 | the stored columns are consumed as rows (T13's defect, written back) | pixels outside the ViewModel mask | `changed=192357 outside=169981` | `outside <= 64`, `changed >= 1000` |
| m3 | the extent is not measured whenever a bone table is present | `pose extent valid` and `posedVertices` | `valid=0`, `posedVertices=0` of 9996 | `valid=1`, `posedVertices == vertices` |
| m4 | the placement distance collapses to `0.22 × radius` | the framing rule from `gpu: viewmodel frame` | `distance=6.5735` against `radius=29.8797` — the eye is inside the model | `distance ≈ 2.2 × radius` |

m1 is the "skipped skinning" mutation T18 asked for; m4 is the "stale camera/model
transform" mutation, and it reproduces T13's symptom deliberately: the pose is still
applied, the bone table is still correct, the frame is still deterministic, and only
the framing moved.

m2 is the round's most useful number, because it is the defect written back into a
fixed tree: the same source, the same capture command, and 169,981 pixels outside the
region the ViewModel is allowed to change.

### Restoration

Each mutation's backup is compared with `cmp` and hashed, and the hash must equal the
pre-mutation hash of that file:

```
native/src/main.cpp            e553de7609fc6027ec7089a006f1a7033975de69d06d709cb4ab31acd7601cbf
native/src/native_renderer.cpp ad6a3f6871cbd4873ebacb5cb279b813506ebd84e5aeeb9ce597913a73f6ff70
native/src/viewmodel.cpp       232dc6340dc9f050778cb1295fe9125e400f356f71d3be678923071f26012b87
```

After the last restoration the gate rebuilds and **re-draws the bind frame**; the hash
must equal the frame captured before any mutation:
`2f0822edab9f359a2ce09d3b935316bd98dcb9b35a74a01fb7347eda68b85b7c`.

**It cannot be judged on the executable hash, and this was measured, not assumed.**
Three consecutive builds of one unchanged tree produced
`8e284755…`, `d599d33a…`, `d6c787b1…`: MSVC stamps a fresh signature into every link.
The first version of this gate asserted the opposite and reported "the restored source
rebuilds the same executable" as a failure that was a property of the linker.

---

## 7.5 What actually moved, measured against the base commit

Re-running `viewmodel-range-check.sh` rewrites its evidence, and two frame pins in
`evidence/viewmodel/range/product-ranges.txt` moved. Only one of them is this task's
doing, and the difference matters, so it was measured rather than argued: a detached
worktree at the base commit `e67630b` was built and the same gate run there.

| tree | bind frame | applied frame |
|---|---|---|
| T13's pin (2026-10-18) | `a3d8a09a…` | `c01a5ada…` |
| base `e67630b` | `2f0822ed…` | `c01a5ada…` |
| this task | `2f0822ed…` | `bfec347d…` |

The base commit already produces `2f0822ed…` for the bind frame and still produces
T13's `c01a5ada…` for the applied one. So:

* the **bind** frame moved between T13 and the base commit, i.e. from the T11–T15
  merges that changed the world drawn behind the ViewModel. T13's pin was stale before
  this task started, and this task's tree agrees with the base commit byte for byte;
* the **applied** frame is unchanged from T13 to the base commit and changes only
  here, which is the signature of a skinning-only fix: the frames that do not skin did
  not move, and the frame that skins did.

The base worktree was removed after the run. Both new pins are committed with the
gate, and the base values are recorded here because a reader who only sees the diff
would otherwise have to guess which of the two changes belongs to whom.

---

## 8. The six defects this round found in its own harness

Recorded because each one produced a **plausible-looking verdict that was wrong**, and
each was only visible because the number it produced contradicted something else in
the same run.

1. **`sed -i` appended a parenthesis** while inserting `&& false`
   (`error C2059` at `main.cpp:5043`), and it rewrites the whole file's line endings
   as a side effect. Replaced by `patch-source.py`.
2. **An LF needle against a CRLF file** (`viewmodel.cpp` is CRLF,
   `native_renderer.cpp` is LF) applied nothing and reported "pattern not found",
   which reads like a stale pattern. `patch-source.py` matches the file's own
   newline and refuses an ambiguous needle.
3. **`python - <<'PY'` consumes stdin.** With `-`, the heredoc *is* the program, so a
   helper that also wanted the patch document on stdin read nothing at all. The
   helper became a separate file.
4. **`outside=` appears twice in the mask tool's output** — once as a field, once
   inside its `reason=` clause — so `grep -o` returned `169981\n169981` and `awk`
   read that as 0. The gate reported "the defect did not exceed 64" about a defect
   that exceeded it by a factor of 2,600. Fixed by parsing only the field section.
5. **A rebuild is not byte-identical** (§7). The restoration check moved from the
   executable hash to the frame hash.
6. **A killed run leaves the tree mutated.** An interrupted `--mutation` run left the
   transposed read in `native_renderer.cpp` — and bash does not run its EXIT trap for
   a signal it has no handler for. The next run would have captured "the applied
   frame" of a transposed read and filed it as evidence, and the stale lock would have
   refused every later run with "another run owns it". The gate now records its owner
   pid, reclaims a lock whose owner is gone, and sweeps orphaned backups **before any
   capture**, rebuilding when it had to put a file back. Verified by reproducing the
   state deliberately:

   ```
   ..   restored native/src/native_renderer.cpp from an interrupted run (ad6a3f68…)
   ..   1 file(s) came back from a killed run; rebuilding before any capture
   ..   rebuilt
   ```

The same round also produced one wrong **sequence**: `--viewmodel-sequence 0` gives
`vmAnimApplied=0` and a frame byte-identical to the bind frame, because the model's
only posable sequence is 1 and the recording's entity carries 29. The gate pins
`POSE_SEQUENCE=1` with that sentence next to it.

---

## 9. Hashes

Fixed input:

| item | value |
|---|---|
| recording | `autorecord_2026-07-02_13-26-46.dem`, 21,856,667 bytes, `c8a58501d222bdd9…` |
| tick | 0, `--start-paused` |
| sequence | 1 (`entitySequence=29` in the recording, overridden) |
| viewmodel | `models/weapons/v_models/v_scattergun_scout.mdl` |
| drawn model | `models/weapons/c_models/c_sniper_arms.mdl` |
| executable | `77a9a08a8ba6a920a216a72f48b4d3f307aaebc05a12d189f1b32dddb9b4c28d` |

Frames, 2,582,406 bytes each, 1264×681:

| frame | sha256 |
|---|---|
| bind | `2f0822edab9f359a2ce09d3b935316bd98dcb9b35a74a01fb7347eda68b85b7c` |
| applied | `13a0d7363872022d0cdfe0694f561ccef703533a76185454bfaf28a328956a49` |
| applied, repeated | `13a0d7363872022d0cdfe0694f561ccef703533a76185454bfaf28a328956a49` |
| world only | `e4d802c27021fbd49649271c3140be1d4ec584a7b04b36d2a8d3272e802ddfd4` |
| flat bind | `320ec443c247d0e80ac3e38dbbd5c86accad8b6dccd78c868bfc58b96dece45c` |
| flat applied | `dd04f80228d28427137da777ce04632a6e6b47331cf0a51a965febf76913699e` |

---

## 10. What the picture shows

`evidence/viewmodel/apply/inspect-applied.png` is the whole applied frame. The weapon
is a slim band across the lower middle of the screen — 262 pixels of a 1264-pixel
width, 20.7% — with the hand gripping its left end and the arm entering from below.
Behind it the world is shattered into floating slabs with a blue patch of sky at the
bottom, which is T17's F1 and is not what this gate judges. What matters is the scale:
a first-person weapon seen at arm's length, not a wall of geometry filling the frame.

`evidence/viewmodel/apply/inspect-bind-applied-3x.png` is the 340×140 region around
the weapon at 3×, bind on the left and applied on the right. Both panels show the same
composition at the same angle: the wooden body running from the hand at the left edge
to the muzzle at the right, the same sky and slats behind it, the same silhouette. The
difference is confined to the hand and the muzzle — the fingers sit a few degrees
further round and the muzzle shifts by a pixel or two. No panel is a close-up, neither
is clipped, and nothing shows an interior surface.

The pose moves each vertex at most **6.126 units of a 29.88-unit model**, and the box
centre by **0.009** units. A hand rotating on a grip is that shape of number; the
transposition was 192,357 pixels of it.

---

## 11. Not verified

* **The world is not.** The map is shattered (F1/T17), so the frames show the ViewModel
  against broken geometry. The gate judges the composition and the ViewModel region,
  never the scene behind it, and says so in the evidence file
  (`world_geometry=shattered`).
* **One sequence, one frame.** The drawn model's only posable sequence is 1 and it is
  one frame long, so "the pose is applied" is a statement about `frame=0/1` at tick 0.
  A playing sequence is not covered; the capture is paused by design.
* **The pose's artistic correctness is not measured.** "The hand is on the grip" is a
  reading of a 3× crop by a human, not a number. What is measured is that the pose
  moves geometry by a plausible amount and stays inside the ViewModel's coverage.
* **Overlap with another worktree.** A T19 capture (`t19-material-coverage`) was live
  during part of this work. `viewmodel-range-check.sh` measured this case: a paused
  capture is byte-identical with four product processes live, a playing one is not,
  and every capture here is paused at a fixed tick. The condition is recorded in the
  evidence (`host_overlap=`) rather than assumed away, and the repeat hash is what
  would catch it if the assumption broke.
* **The `--mutation` forms of `viewmodel-animation-check.sh` and
  `viewmodel-companion-check.sh` were not re-run this round** — only their forward
  forms, which is what T18's acceptance asks for. Their mutation suites passed in T13
  against the same skinning path and the same source; that is a statement about T13's
  tree, not about this one.
* **`viewmodel-range-check.sh` was run forward, not with `--mutation`**, so this round
  has its 74 forward assertions and not a fresh proof that its own defect set is still
  caught. Its applied frame moved (`c01a5ada…` → `bfec347d…`) and its bind frame did
  not, both confirmed against a base-commit worktree in §7.5.
* **Frame hashes are build-specific in one direction only.** The frames are
  deterministic for a given source (`13a0d736…` came back in every run of the gate
  that captured it, three runs across two calendar days), but the executable that
  draws them is not reproducible byte for byte, so the
  `executable_sha256` in `evidence/viewmodel/apply/summary.txt` identifies the binary
  that drew these frames and says nothing about a rebuild.

---

## 12. Final worktree and process state

* Branch `task/t18-apply-camera`, three commits on top of `e67630b`, and
  `git status --short` clean.
* The `t18-baseline` worktree used for §7.5 was removed with
  `git worktree remove --force` followed by `git worktree prune`; `git worktree list`
  no longer names it.
* No `tf2_demo_native.exe`, `cl.exe`, `link.exe`, `nmake`, `cmake` or `timeout`
  process started by this task is still running, and no `.scratch/*.lock` directory
  survives. The gates remove their own lock in a `cleanup` trap; the one lock that a
  killed run did leave behind (§8.6) is what the pid-based reclaim exists for.
* `.scratch/t18/` holds this round's logs and frames and is untracked.
