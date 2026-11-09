# The class-pixels pin: what moved, why, and the re-pin

Status: the four class baselines in evidence/t62-matrix/class-pixels/<class>/
were re-pinned in this commit. This note is the record of the measurement: the
old pin, the new pin, the per-frame difference, the two controlled experiments
that attribute the difference, and the commands that reproduce all of it.

## 1. What the pin is

third-person-weapon-check.sh asserts, per class, that its own **off** capture is
byte-identical to <class>/animated.bmp and that its **identity** capture is
byte-identical to <class>/identity.bmp (third-person-weapon-check.sh:411-421,
:15-16). Those two files are therefore a renderer pin: they freeze what the
frozen product drew for one request.

The pins were produced by the T62/T79 product (exe sha256 891bfd7a…, recorded in
the old entity-id.txt files) and had never been regenerated since. Round 2 of the
release chain and the merge window's preflight both drew the same request with
the merged product and got a different frame, so the pin had to be re-measured
before the chain could use it: an assertion that compares against a frame the
product no longer draws is red for the wrong reason.

## 2. The difference, frame by frame

Frame: 1264x681 = 860,784 px, 2,582,406 B BMP. Measured with python3 + Pillow,
pixel by pixel, old = `git show <old-commit>:evidence/t62-matrix/class-pixels/<class>/<frame>.bmp`,
new = the frame today's product produced for the same request.

| class | frame | old sha256 (12) | new sha256 (12) | changed px | share | max delta | px with delta > 8 |
|---|---|---|---|---|---|---|---|
| demo | animated | e92bcfe04d4b | 00fd8776862d | 2176 | 0.253% | 1 | 0 |
| demo | off | 396ce6abc57a | 59dfe0abfdba | 2175 | 0.253% | 1 | 0 |
| demo | identity | 148e244a2871 | 148e244a2871 | 0 | 0 | 0 | 0 |
| scout | animated | be6842bc5dba | 3d2cc9bc5a13 | 748 | 0.087% | 3 | 0 |
| scout | off | 616891ac1b79 | ee0db9cc2f75 | 747 | 0.087% | 3 | 0 |
| scout | identity | 7317b7a28afe | 8b5b532d3c82 | 74 | 0.009% | 3 | 0 |
| soldier | animated | f3aab78fbde1 | 8b0910adbb52 | 818 | 0.095% | 3 | 0 |
| soldier | off | 3590ecd84ea5 | 52fff44aeeca | 817 | 0.095% | 3 | 0 |
| soldier | identity | b6497fe698ae | fc9df91d1702 | 2 | 0.000% | 3 | 0 |
| medic | animated | ac842f4d31de | 4d286124a2ae | 820 | 0.095% | 3 | 0 |
| medic | off | 077e2ac8816b | a1155429e18a | 815 | 0.095% | 3 | 0 |
| medic | identity | 84e569c33dac | e538f1ff8c0b | 15 | 0.002% | 3 | 0 |

12 frames: 4,856 changed pixels in total. **No frame has a pixel whose channel
moved by more than 3 of 255, and none has a pixel above delta 8.** The demo
identity frame is byte-identical; scout/soldier/medic identity frames move 74 / 2
/ 15 pixels, which is a silhouette-edge and shading-level difference, not a
different subject, camera or tick (the animsum phase in every summary is
unchanged: demo/scout 0.838676, soldier/medic 0.000000).

That signature — a fraction of a percent of pixels, delta <= 3, identity views
nearly untouched — is a shading/quantisation difference. A geometry, camera or
entity-set difference looks nothing like it: the T62 measurement that DID find
one (evidence/t62-matrix/…) showed max delta 213 with a localised wedge.

## 3. Two controlled experiments (both under hostlock, product args unchanged)

The request: demo bb841c6d…matcha-20260927-1347-cp_snakewater_final1.dem,
--capture-tick 64161 --playback-speed 8
--debug-camera 5363.8 -1102.1 5.9 4.1 0.0 --player-animation, the baseline's own
settings.json/ui.json, --hidden-window --audio-disabled.

1. **The old pin is the frozen product's own output, and it still reproduces.**
   The frozen T79 binary (a3c93d57…, 2,763,264 B) drew e92bcfe04d4b… — the pin,
   byte for byte — on the merged tree's own settings. So the pin is not a stale
   artefact of an unknown build, and the request is reproducible today.
2. **The merged product is deterministic across runs.** The merged binary
   (25d55f0e…, 2,809,856 B) drew 00fd8776862d… twice, in two processes
   (frame_tick=64161 both times). So the drift is a property of the code, not of
   the machine, and re-pinning cannot make the chain flaky.
3. **Bisect: the drift comes in with T88's map code.** Two trees were built and
   asked the same question:
   | tree | t88 map code (fc884cd) | exe sha256 (12) | frame |
   |---|---|---|---|
   | 5b3aeed (t85+t86+t87+t92+t94 merges) | **absent** | 70b43ea12cf3 | **e92bcfe04d4b = the old pin** |
   | 16599e1 (that plus the t88 merge 739ae31) | present | 1e96da9372e4 | 00fd8776862d = the drift |

   So restoring the func_illusionary faces and raising the world triangle budget
   (2,000,000… see T88) is what moves the class frames by up to 3/255. Every
   later merge (t90, t93, timeline) draws 00fd8776 as well — the merged tree is
   self-consistent with the tree that first showed the drift.

   The micro-mechanism inside the map code was not reduced to one line here: the
   measured effect is a shading quantisation over the drawn world, and the T88
   notes already record the two visible consequences (2,892 restored
   func_illusionary faces on snakewater, 42,643 triangles that the old 200k bound
   refused). What the experiment does establish is that the drift is T88's, is
   deterministic, and is bounded by delta 3.

## 4. The re-pin

* Files replaced: <class>/{animated,off,identity,identity-2}.bmp,
  <class>/{animated,off,identity,identity-2}.summary, <class>/entity-id.txt for
  the four classes. Nothing else in the directory changed; settings.json, ui.json
  and targets.txt are untouched (same bytes, same requests).
* Producer: the merged product at exe sha256 25d55f0e… (2,809,856 B), running the
  class-pixels gate itself, at the gate's current default speed 8. The old pins
  had been made at speed 32 by product 891bfd7a…; the re-pin therefore also
  aligns the pins with the speed the gates ask for today, and speed invariance is
  separately evidenced (§3.1: the frozen binary at speed 8 drew the speed-32 pin
  exactly).
* Phases did not move (0.838676 / 0.000000 as before), so third-person's phase
  retry still targets the same phase the pin was taken in.
* Proof that the pin is now the current product's output: the class-pixels gate
  re-ran green on the same tree with these frames written to a scratch directory
  (CLASS-PIXELS=PASS, four classes, demo 1711 px subject / share 72.00%, scout
  1161/43.07%, soldier 1185/92.41%, medic 1400/64.57%), and its identity vs
  identity-2 pairs are byte-identical in every class.

## 5. Reproduce

```bash
# the four frames the pin should now hold (per class: animated, off, identity, identity-2)
T62_CLASS_OUT=/scratch/class-pixels bash class-pixels-check.sh     # needs hostlock
# the drift itself (one frame, two trees)
bash build-target.sh tf2_demo_native                              # on 5b3aeed vs on 16599e1
native/build-nmake/tf2_demo_native.exe --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --hidden-window --audio-disabled --settings-file <scratch>/settings.ini --ui-settings-file <scratch>/ui.ini \
  --demo D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem \
  --capture-tick 64161 --playback-speed 8 --capture-frame <scratch>/frame.bmp \
  --debug-camera 5363.8 -1102.1 5.9 4.1 0.0 --player-animation
```

## 6. Where the pin's bytes came from, and how that is now checked

Two binaries are involved here, and confusing them is how a baseline stops being
evidence:

* **25d55f0ee541577da55504d26ba1a790f22544d3069b3e44a05467288415e976** drew these frames when the pin was re-pinned on 2026-11-07
  22:47. It is what the capture logs themselves record, as
  `frame: product sha256=25d55f0ee541577da55504d26ba1a790f22544d3069b3e44a05467288415e976` in every `<class>/entity-id.txt` and
  `<class>/<frame>.summary`.
* **60fd00365db2717b37cadba52d1b9a451399a4b80ee8c210a086de97c0b1c041** is the binary measured at 2026-11-08T09:49:50+08:00 (t91 HEAD `dbdb00138dc75823c208fbe91fab382b6cca6ebf`, `native/`
  tree `0a9acd2d505692fe008b69def435a9cb6360be05f3e7aef81977fbef45c8f6f3`). It re-ran the demo frame and reproduced these BMPs
  **byte-for-byte** (three controlled runs: `D:/_shared/EXEC-2026-11-07/T4-PIN-PROVENANCE.md`;
  the frozen t79 product reproduces the *old* pin instead, which is what
  attributes the drift to the merged map code, not to identity or playback).

The capture logs are deliberately left byte-identical in this commit: they are
the record of what ran at 22:47, and hand-editing their `product sha256` line
would make a log claim a binary it never ran. The refreshed identity lives in
`<class>/provenance.json` instead -- data with named fields, one file per class:
`exe_sha256` (the refreshed reading above), `exe_sha256_at_capture` (what the log
recorded, historical), `git_head`, `native_tree_sha256`, `read_at`, the exact
`request` (demo / tick / speed / camera / flags), the `expected_bmp_sha256` of
all four frames, and which class body the identity frame judges.

`pin-provenance-check.sh` (new, static: it starts no process and reads files
only) turns that data into an assertion: 84 assertions across the four classes --
the four ledger keys exist and are shaped right (64-hex shas, 40-hex head), the
request is legal (tick > 0, speed 1..64, five finite camera numbers in
pitch/yaw range, a `.dem` path), every pinned BMP hashes to its recorded value,
identity and identity-2 are byte-identical, and all four frames share one
non-zero size. Since D-35/D-36 the assertion set is pinned by name (A1..A20) and
the pin must be corroborated from independent records: the capture receipt
(`entity-id.txt`), the four `<tag>.summary` files, and the repository itself
(`git cat-file -e <git_head>` plus `sha256(git ls-tree -r <git_head> -- native)`
equal to `native_tree_sha256`). Deleting an assertion is a failure, not a pass.

```bash
bash pin-provenance-check.sh            # PIN-PROVENANCE=PASS (84 assertions), rc 0
```

It deliberately does **not** assert that `exe_sha256` equals the exe in this
tree right now: a pin is a historical capture, a relink changes the tree's
binary and not the pin, and the capture gates are what tie the pin to live
rendering (they compare the BMPs, not this sha). The field answers "which
binary produced these bytes, and when was that measured", which is exactly the
question the free-text line could not answer safely.

No BMP changed in this commit: `git show --stat` for it lists only the gate, the
four `provenance.json` files and this note.

# previous pin: demo e92bcfe04d4b/396ce6abc57a/148e244a2871, scout be6842bc5dba/616891ac1b79/7317b7a28afe, soldier f3aab78fbde1/3590ecd84ea5/b6497fe698ae, medic ac842f4d31de/077e2ac8816b/84e569c33dac (product 891bfd7a… at speed 32)
