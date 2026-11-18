# T14 — Weapon body material integrity

**Owner:** AI-D · **Branch:** `task/t14-weapon-material`
**Worktree:** `D:/TF2_Native_Worktrees/t14-weapon-material`
**Base:** `4dbf396` ("Merge T11 world geometry and T12 terrain blend")
**Anchors:** source blobs `81c72a2` (`native/include/native_renderer.h`), `e945fa6`
(`native/src/native_renderer.cpp`), `7f6217f` (`native/src/main.cpp`), `e2577a2`
(`native/tools/weapon_body_probe.cpp`). Every reading below comes from binaries
built from those blobs. The exe's sha256 is recorded in §7 as a receipt, not as an
identity: this toolchain writes a link timestamp into the PE header, so two builds
of the same source differ in bytes (T13's finding, unchanged here).

The plan asked for seven things. Where each is answered:

| # | the line | where |
|---|---|---|
| 1 | trace the entity through index, MDL slot, VMT, VTF | §1 |
| 2 | bind the body's own albedo to every rendered range | §2 |
| 3 | keep body material state independent of hands and world | §4 (mask), §6 m3 |
| 4 | missing/malformed resource gets an explicit fallback or refusal | §5 |
| 5 | fixed-tick textured and flat-control frames from one executable | §4 |
| 6 | change limited to the body region, deterministic across repeats | §4 |
| 7 | mutations for wrong slot, missing texture, state leakage | §6 |

Verdicts: `WEAPON-MATERIAL=PASS` (74 assertions, 0 failed) and
`MUTATION-CAUGHT=PASS (3 defects, each with its own named reading and control)`.

---

## 1. The chain, measured

The body is not "the weapon". It is the model the weapon **entity** names, reached
through the same field the world pass reads:

```
entity 1008 (CTFSniperRifle) -> m_iWorldModelIndex 533
  -> models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.mdl
     -> LOD 0 descriptor 0: 12357 verts, material slot 0
     -> LOD 0 descriptor 1:   324 verts, material slot 1
```

`weapon_body_probe --model <path> --demo <recording> --tick 16306` walks that
chain with the *product's* resolver (`resolveViewModelMaterialTable`, shared, not
a re-implementation) and prints one line per range. On the recording's own body:

| descriptor | vertices | slot | slot name | VMT | VTF |
|---|---|---|---|---|---|
| 0 | 12357 | 0 | `.../c_pro_rifle` | `.../c_pro_rifle.vmt` | `.../c_pro_rifle.vtf` |
| 1 | 324 | 1 | `.../c_pro_rifle_scope` | `.../c_pro_rifle_scope.vmt` | `.../c_pro_rifle.vtf` |

Two slots, two VMTs, **one VTF**. The scope's VMT points `$basetexture` at the
rifle's atlas. That is a fact about this weapon's content, and it is why this
recording cannot show the defect T14 fixes: paint both ranges with slot 0's
albedo and the picture does not change by one byte (§4 measures exactly that).

So the gate needs a second body, and it takes a real one rather than a fixture:

```
--viewmodel-weapon-model models/weapons/c_models/c_tfc_sniperrifle/c_tfc_sniperrifle.mdl
```

| descriptor | vertices | slot | slot name | VMT | VTF |
|---|---|---|---|---|---|
| 0 | 20817 | 0 | `.../c_tfc_sniperrifle` | `.../c_tfc_sniperrifle.vmt` | `.../c_tfc_sniperrifle.vtf` |
| 1 | 444 | 1 | `.../c_sniperrifle_scope` | `.../c_sniperrifle_scope.vmt` | `.../c_sniperrifle_scope.vtf` |

`rangeMaterials ranges=2 textured=2 distinctVtf=2 slots=2` for the override body,
`... distinctVtf=1 ...` for the natural one. The probe reports `override=` on every
product line so an overridden run can never be read as a natural one.

**How common is the shape.** The probe was run over every `c_*.mdl` under
`models/weapons/c_models` (248 models; 239 built a mesh):

| LOD-0 ranges | models |
|---|---|
| 0 | 9 |
| 1 | 171 |
| 2 | 50 |
| 3 | 12 |
| 4 | 5 |
| 17 | 1 (`stattrack.mdl`) |

68 models have more than one range. Of the 67 that produced a material-table
reading, **48 resolve their ranges to different VTFs**. Before this task the pass
bound one texture for the whole buffer, so on those 48 the second range was
painted with the first range's albedo; on the other 200 models the defect was
real and invisible, which is why it survived every gate until now.

## 2. What the pass did, and what it does now

Before (`native_renderer.cpp`, the whole weapon pass):

```cpp
const auto weaponTexture = viewModelMaterialTextures_.find(viewModelWeaponState_.materialKey);
const bool hasWeaponTexture = weaponTexture != viewModelMaterialTextures_.end();
weaponConstants.material = XMFLOAT4(hasWeaponTexture ? 1.0f : 0.0f,
  viewModelWeaponState_.materialAlphaTest ? 1.0f : 0.0f, 0, 0);
context_->PSSetShaderResources(0, 1, &weaponAlbedo);
context_->Draw(viewModelWeaponVertexCount_, 0);
```

One key from the state, one Draw over the whole vertex buffer. The reading was
truthful — `weaponMaterials=2/2/2/2` meant "two slots were decoded and uploaded" —
and the draw used one of them.

Now the body carries a range table, built by the same `viewModelDrawRangesFor`
lambda the arms use (clamping is the renderer's: a range outside the created
buffer is skipped, a non-multiple-of-three count is truncated, a range that clamps
to nothing is dropped), and the pass issues one Draw per range:

```cpp
for (const auto& range : viewModelWeaponDrawRanges_) {
  auto cached = range.materialKey.empty()
    ? viewModelMaterialTextures_.end()
    : viewModelMaterialTextures_.find(range.materialKey);
  const bool textured = cached != viewModelMaterialTextures_.end();
  ... PSSetShaderResources(0, 1, &view); Draw(range.vertexCount, range.firstVertex);
}
```

The no-range case is kept as the old whole-buffer draw rather than synthesised
into a one-range table — a caller that supplied no ranges cannot have wanted a
range reading, and the picture is the same either way.

The body also stops inheriting the previous frame's counters: the five readings are
reset before the `enabled` branch, so a body that is off or unbuildable reports
zero ranges instead of the last frame's.

New reading row (its own line — the existing `weaponBodyReading` line is 512-byte
`traceFmt`-limited and appending to it silently truncated the key list in the first
attempt):

```
gpu: viewmodel weaponBodyMaterial ranges=2 texturedRanges=2 fallbackRanges=0
  boundSlots=2 keys=<k1>,<k2> runtimeFailure=- unboundReason=- flatColor=0 override=1
```

Measured on the final tree, capture frame at tick 2000:

| run | ranges | textured | fallback | boundSlots | keys |
|---|---|---|---|---|---|
| `natural-on` | 2 | 2 | 0 | 2 | `c_pro_rifle.vtf, c_pro_rifle.vtf` |
| `override-on` | 2 | 2 | 0 | 2 | `c_tfc_sniperrifle.vtf, c_sniperrifle_scope.vtf` |
| `override-flat` | 2 | 0 | 2 | 0 | `-` |
| `override-off` | 0 | 0 | 0 | 0 | `-` |
| `missing-body` | 0 | 0 | 0 | 0 | `-` |

`keys` is in range order and each entry is the key of the SRV that range's Draw
actually bound, which is §3.

## 3. The reading had to name the resource, not the request

The first `--mutation` run caught 2 of 3. m3 (state leakage) changed the picture by
20234 bytes and the reading was **still green**: `keys` named the body's own two
textures while the draw had bound the hands' atlas.

The cause was in the product, not the test. `keys` was accumulated from
`range.materialKey` — what the range asked for — and m3 replaces the *lookup*, not
the request. A reading built from intent cannot see a lookup that answered with
something else, and that is exactly the defect class this project keeps finding.

Fixed by recording the resource instead:

```cpp
// The key of the SRV that was actually bound -- NOT range.materialKey.
viewModelWeaponMaterialKeys_ += cached->first;
```

On a correct run these are the same string, so no forward reading moved (§2's table
is byte-for-byte what the previous gate measured). Under m3 the row now reads
`keys=materials/models/player/sniper/sniper_red.vtf,.../sniper_red.vtf`, and the
mutation is caught by a named assertion rather than by the frame diff alone.

That is the second product change T14 made because a mutation asked for it: the
range table was the first, this is the reading that can prove the range table is
being used.

## 4. The picture, at one tick, from one executable

`--capture-tick N` lands on the first Draw at or past N, and on this machine that
is 2000, 2001 or 2002 depending on load. Two ticks of this recording differ by
~1.5 MB — more than the whole body — so every frame is retaken until it lands on
the requested tick; the gate says so per attempt:

```
..   override-flat: frame landed on tick 2001, wanted 2000 (attempt 1)
..   override-flat: frame landed on tick 2000 (attempt 2)
```

All six forward frames come from **one** build: sections 0–5 build nothing, and
the log's first build line is section 6's m1. Determinism — four captures of the
same configuration at tick 2000, two of them from separate gate invocations:

| run | sha256 of the frame |
|---|---|
| `override-on` (forward run) | `69a027ff6d3f9a9d…` |
| `override-on-repeat` (same invocation, second process) | `69a027ff6d3f9a9d…` |
| `mutation-baseline-keep` (second gate invocation) | `69a027ff6d3f9a9d…` |
| `mutation-restored` (after restoring source and rebuilding) | `69a027ff6d3f9a9d…` |

`byte_diff(override-on, override-on-repeat)=0`. The last row is the strongest of
the four: the source was mutated, rebuilt, restored byte-for-byte and rebuilt
again, and the frame came back identical. `missing-body` and `override-off` share
a different hash (`0fbbbf01…`) because neither draws a body — the claim there is
that the two agree with each other, not with the frames that have a rifle in them.

**Containment.** The body's own coverage is the union of the two differences
"body on vs body off" and "body flat vs body off". The material difference must sit
inside it:

```
BMP-MASK changed=7190 inside=7190 outside=0 mask_pixels=7190
         bbox=(x=659..707, y=231..581) w=49 h=351 status=PASS
```

`outside=0` is the claim "hands and world did not move": a hand pixel is not in the
body's coverage, so a leak into the hands would land in `outside`. The world strip
above the composite is checked by name as well:

```
BMP-DIFF region=0,0,1264,220 differing=0 total=834240 percent=0.0000 status=PASS
```

The strip stops at y=220 because the body's own silhouette starts at y=231; a strip
that included the body would fail for the right reason and hide the wrong one.

Byte-level differences at the same tick (a pixel is 3 bytes here):

| pair | bytes | pixels |
|---|---|---|
| `on` vs `flat` | 21570 | 7190 |
| `on` vs `off` | 20658 | 6886 |
| `flat` vs `off` | 21558 | 7186 |
| `missing-body` vs `off` | 0 | 0 |
| `missing-body` vs `natural-on` | 11618 | — |

`evidence/weapon-material/body-on-flat-off.png` is the same 90×380 rectangle of the
three forward frames, doubled: the rifle's camo albedo, the same silhouette in flat
grey, and nothing. `body-mutations.png` is the scope region of the baseline and the
three mutants, quadrupled.

## 5. A body that is not there

`--viewmodel-weapon-model models/weapons/c_models/t14_no_such_body.mdl`:

```
scene: weaponBody entity=1008 class=CTFSniperRifle worldModelIndex=533
  path=models/weapons/c_models/t14_no_such_body.mdl refusal=none fallback=buildFailed
  reason=the weapon entity's own m_iWorldModelIndex names the weapon body
gpu: viewmodel weaponBodyReading weaponDrawCalls=0 weaponMaterials=0/0/0/0
  weaponFallback=buildFailed weaponVisible=0 weaponReason=viewmodel was not found
gpu: viewmodel weaponBodyMaterial ranges=0 ... keys=-
```

The frame is byte-identical to the switched-off body's (0 differing bytes) and
differs from the frame with a body in it (11618). So "nothing was drawn" is a state
with a name and a reason, and it is not a rifle painted with whatever texture was
bound last.

## 6. The three mutations

Each is a source edit that must make a **named** reading go red while a named
control holds, then the source is restored byte-for-byte (`git hash-object`), the
tree rebuilt, and the restored frame compared against the baseline.

| # | defect | named reading that went red | control that held | frame |
|---|---|---|---|---|
| m1 | wrong-slot binding: every range collapsed onto range 0's slot and key (= the pre-T14 shape) | `boundSlots=1`, `keys=<rifle>,<rifle>` | `ranges=2`, `texturedRanges=2` — it still draws twice | 1365 bytes, 456 px, `x=659..689 y=391..504` |
| m2 | missing texture binding: only the first range binds | `texturedRanges=1`, `fallbackRanges=1`, `keys=<rifle>` | `ranges=2` — the second range is still drawn, flat | 1368 bytes, 456 px, same box |
| m3 | material-state leakage: the body's lookups answer with the arms' first key | `keys=<sniper_red>,<sniper_red>` | `ranges=2`, `texturedRanges=2` — two textured draws | 20234 bytes, 7092 px, the whole body |

The m1/m2 box is the scope range's own screen area — 456 of the body's 7190 pixels,
exactly the 444-vertex descriptor — and m3's box is the whole body. Three defects,
three different readings, three different regions.

m1 is worth reading twice: it *is* the pre-T14 draw, expressed as a range table.
Its 1365-byte difference from the fixed frame is the defect this task removes,
measured on a real weapon, at a fixed tick.

## 7. Commands, exit codes, artefacts

| command | exit | log |
|---|---|---|
| `bash build-target.sh tf2_demo_native` | 0 | `evidence/weapon-material/build-product.log` |
| `bash build-target.sh weapon_body_probe` | 0 | `evidence/weapon-material/build-probe.log` |
| `bash weapon-material-check.sh` | 0 | `evidence/weapon-material/weapon-material-forward.log` (74 OK, 0 FAIL) |
| `bash weapon-material-check.sh --mutation` | 0 | `evidence/weapon-material/weapon-material-mutation.log` (104 OK, 0 FAIL, `MUTATION-CAUGHT=PASS`) |
| `bash weapon-material-archive.sh` | 0 | `ARCHIVE=OK` |
| `python -m py_compile bmp-diff-mask.py bmp-crop-grid.py` | 0 | — |
| `sha256sum -c evidence/weapon-material/artifacts.sha256` | 0 | 57/57 OK, also from a fresh checkout |

Binaries (receipts, not identities):

```
tf2_demo_native.exe  1632768 bytes  sha256 35190468b938bbd58a1cbd3d9ab2b42619307af5c90bdf2481a1b861271699bd
weapon_body_probe.exe              sha256 9fda4698c580c37330fedf867dc8cf566ee19756d603cc5eb06b44c3d073ca0b
```

The exe hash is the build the mutation run left behind — §9's restore rebuild is
the last write to it. Rebuilding the same source produces a different hash here;
`git hash-object` of the sources and the frame hashes below are the stable
identities.

`evidence/weapon-material/` holds, per run: the frame (`.bmp`), the settings used,
the metrics CSV, the product's stdout (empty — it writes to the trace, not to the
console), and `<run>.readings.txt` — the body/arms/material lines the assertions
read, extracted from the trace by `weapon-material-archive.sh`. Two receipts:

- `artifacts.sha256` — the 57 committed artefacts. `sha256sum -c` verifies them
  from a fresh clone; `.gitattributes` marks the directory `-text` because
  `core.autocrlf` would otherwise re-encode the text extracts on checkout and 29
  of 57 lines would fail there while passing in place.
- `traces.sha256` — the 11 raw traces. ~3.4 MB each and derivable from a run, so
  they are excluded from git (`.gitignore`) and their hash says which run an
  extract came from without committing the file.

The two PNGs are crops, not new evidence; `natural-on-before-fix.*` is the base
tree's frame and reading, kept because §8's "nothing changed here" is a comparison.

## 8. Findings, and what is not verified

- **The fix is invisible on the pinned recording, and that is the measurement.**
  `evidence/weapon-material/natural-on-before-fix.bmp` is the frame the base tree
  produced at tick 2000 (captured before any T14 edit); its sha256 is
  `17e12f5e59c7a286…`, and `natural-on.bmp` — the final tree, same tick — is
  byte-identical. Both slots share one atlas, so a correct pass and a wrong one
  look the same here. The gate's value is the override body, and §1 says why that
  matters: 48 of the 67 multi-range bodies do not share one albedo.
- **The body renders edge-on.** Its silhouette is 49×351 px at x=659..707, y=231..
  581 — a narrow sliver of a rifle, because the recording's viewmodel is the
  scattergun seen from the side and the body sits behind it. Pre-existing framing;
  this task records it rather than fixing it. It is why the gate measures a mask
  rather than "look at the screenshot".
- **A mutation found a second defect, in the instrumentation.** §3. The first
  `--mutation` run was 2/3; the missing catch was the reading, not the fix.
- **Not verified:** malformed (as opposed to absent) MDL/VMT/VTF input; the other
  47 multi-albedo bodies are measured through the chain but not rendered; LOD
  selection beyond "the file-declared lowest LOD"; `stattrack.mdl`'s 17 ranges; and
  whether the second range *reuses* an uploaded texture rather than re-uploading
  it — the key list shows two distinct keys were bound, not that the second upload
  was skipped.

## 9. Final state

Files added by this task:

```
native/include/native_renderer.h        modified  range-bound weapon draw + five readings
native/src/native_renderer.cpp          modified  one Draw per range, clamped upload, bound-key reading
native/src/main.cpp                     modified  range table for the body, --viewmodel-weapon-model,
                                                  --viewmodel-weapon-flat-color, material row
native/tools/weapon_body_probe.cpp      modified  per-range material self-test (product resolver)
weapon-material-check.sh                new       the gate (sections 0-6, --mutation)
weapon-material-archive.sh              new       readings + hash packaging (does not re-run)
bmp-diff-mask.py                        new       A/B difference vs a coverage mask (outside=)
bmp-crop-grid.py                        new       crops the same rectangle out of N frames
.gitignore                              modified  evidence/weapon-material/*.trace
.gitattributes                          new       evidence/weapon-material/** -text
T14-WEAPON-MATERIAL-2026-10-18.md       new       this report
evidence/weapon-material/               new       57 artefacts (7 distinct frames, 2 PNGs,
                                                  11 reading extracts, the gate logs)
```

`git status --porcelain` is clean apart from the files above. No
`tf2_demo_native.exe` or `weapon_body_probe.exe` is left running, and
`.scratch/weapon-material.lock` is removed by the gate's exit trap — a stale lock
would make the next run exit 2 with "another run owns" rather than measure.
