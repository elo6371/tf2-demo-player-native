# Latest readings (2026-11-13, after the clean-tree cut)

Everything here was measured on the **same binary** the clean tree builds, and each row
names the artifact it came from. Where a number could not be bound to that binary it says
so. Nothing in this document is a whole-chain verdict: the 15-step chain has not been run
on this tree.

## The identity these readings are bound to

| Field | Value |
|---|---|
| Source commit (worktree) | `50a355a` (`candidate/e5` at `D:/TF2_Native_Worktrees/t105-e5`) |
| `HEAD:native` tree | `0a50e97310b92277a7b8183b993fc3817ab1aed1` -- this is what tree-identity gates compare, **not** the commit |
| Product exe | `8bff0858221b2f0a30e3806a29439c7c39964309f43bf051b9d314f929a1dfcf`, 2,970,112 B |
| Content hash | `4498475d411e0dd9c126fe24e1b3ce66a87aac92fd09e9aa29b465201749dcf6` (`build-identity-content.sh hash`) |
| Sidecar | `dirty=0`, `contentBinding=carried`, `buildUtc=2026-11-13T03:13:34Z` |

Two rules learned the hard way, both now in `docs/HANDOFF-MISSING.md`:

- **`buildUtc` and `tag` are not inside the exe**; only `sourceContent` is. Three
  rebuilds on 2026-11-13 with identical sources produced identical exe bytes and three
  different `buildUtc` values. Gate on the exe SHA256 or on `sourceContent`, never on
  `buildUtc`.
- Naming a target to `build-target.sh` does **not** rebuild the sidecar unless you also
  name `tf2_identity`; an incremental build after a commit can otherwise leave a sidecar
  that names the previous tree.

## Seven-map playability -- 26 captures, one lock

```
26/26 rc=0, every frame 2,582,406 B (1264x681 BMP), frame_tick == requested tick 26/26
```

| Map | POV (early / late tick) | STV (early / late tick) |
|---|---|---|
| cp_process_f12 | 27679 / 249115 | 12095 / 108843 |
| cp_sunshine | 22157 / 199413 | 12844 / 115590 |
| cp_gullywash_f9 | 18018 / 162166 | 12666 / 113985 |
| cp_sultry_b8a | 18588 / 167291 | 14007 / 126056 |
| koth_bagel_rc11 | 2552 / 22964 | 10064 / 90570 |
| koth_product_final | 22779 / 205011 | 8832 / 79481 |
| pl_upward_f12 | **NO-DEMO** (no POV fixture exists) | 8521 / 76685 |

- Verdict: **POV+STV both play on 6 of 7 maps, on one binary.** `pl_upward_f12` POV is
  UNSUPPORTED-by-corpus, not a failure, and must not be written as "plays".
- Two harness traps cost two throwaway rounds and are fixed in the ticket:
  `--window-size` is parsed only in the pre-argument scan (`native/src/main.cpp:2111`)
  and the main loop rejects it under `--strict-args`; and real-time playback makes a
  late tick (~415 s of demo) exceed any sane per-capture budget -- `--playback-speed 64`
  (clamped 0.125..64, `main.cpp:2729`) still lands exactly on the requested tick.
- Not covered: the cart tick is still a 10%/90% ratio of the demo, not an entity reading;
  `brush_entity_probe` returns `candidates=0` on both cart demos, so **cart visibility is
  NOT VERIFIED**. The 6-objects x 7-maps matrix was not run.

## Black region on cp_sultry_b8a -- verdict

**BLACK-IS-NOT-HOLE.** An isolation render with the world pass only (`--world-isolate`)
returns the clear colour in 860,784 of 860,784 pixels; the clear colour is
`(14,18,22)` (`main.cpp:12893-12895`, 0.055/0.07/0.085), and no pixel in the normal
frame is exactly that value. So the black comes from world geometry being drawn with
~zero light, not from missing geometry.

- 11 sampled black points split **8 / 3**: eight have non-zero albedo (7 of those carry
  `lightbranch=texture-zero` or a factor <= 0.37 -- the lightmap sample is zero or nearly
  zero; one, 1143,132, has albedo 255 white and a bright lightmap and remains
  **UNRESOLVED**), three have albedo 0 themselves (inferred `alpha==0` texels, not proven).
- The atlas-budget refusal list is **not** the cause: a refused-material mask
  (`--world-refused-mask`) paints 4,066 px magenta (0.472%) and none of the black points
  overlap it; refused materials are drawn flat-shaded with vertex alpha 0, not dropped
  (`native_renderer_world.cpp:606-621`, `native_renderer_shaders.cpp:234`).
- Backface culling explains 10.54% of the dark area; vis fixes 0.08%.
- Open: 1143,132 needs a face-level VTX/UV decode or an alpha readout (the 24bpp BMP
  cannot carry alpha); the three albedo-0 points are labelled INFERENCE ONLY.

## Five gates frozen in October -- drift, not regressions

`R0/rec/C6a-DRIFT-SUMMARY.md`: of the five gates in the recording set, only
`lighting-dark-account` was actually run (**DRIFT-CONFIRMED**, 14 of 31 assertions red);
the other four are **NOT-RUN** -- two of them rebuild the product inside the gate, and one
of those five pins is from 2026-10-21.

- The drift is a **different camera pose**, not a regression: three binaries
  (2026-11-12 dirty tree, v3 `6c06c6ad`, v5 `8bff0858`) all capture
  `eye=815.5,-147.8,-194.3`, identical frame bytes (`5c29479e...`), dark=6,146;
  the stored pin captured `eye=-2294.8,348.8,383.4`, dark=208,630.
- Structural gap: the gate derives the eye from the trace and writes it, but **asserts
  nothing about it** and pins no demo hash, so a pose change silently re-bases every
  classification. Do not re-pin before adding a pose/demo assertion.

## Material coverage -- six reds that are pin drift

`workflow-auditor/MATERIAL-COVERAGE-FORWARD-RED.md` (verdict **PIN-DRIFT**) and
`verify-fast` **PASS (43/0)** in the same window:

- Identical inputs: `inputTriangles=242109`, `withoutCluster=207592`, `cameraCluster=2108`,
  same eye map, byte-identical atlas plan (`batches=3 tiled=141 refused=0`).
- Changed: PVS-derived counts only -- `visibleFromCamera` 1481 -> 858,
  `hiddenTriangles` 15039 -> 25995 (**+10956**), `drawnTriangles` 227070 -> 216114.
- Attribution: `10d1355` ("vis table is offset pairs and rows are Source zero-run RLE")
  re-pinned one gate and missed these six numbers; the gate is not in `verify-fast`, so it
  had not run since 2026-11-09.
- `batchDraws` 6 -> 5 is the sub-batch mechanism skipping an empty mode-0 index range
  (`native_renderer_world.cpp:950-1003`, `native_renderer_frame.cpp:412`).
- The mutation half of the same window still caught 7 of 7 mutations; the script's last
  line is nevertheless `MATERIAL-COVERAGE-MUTATION=FAIL` because a caught mutation over a
  red gate is not a pass. That wording is correct and should not be "fixed".

## Animation captures -- two tick axes (read this before trusting any action label)

`R4/R4-4-CAPTURE-RESULTS.md`: 27 frames, 26 demo-captured + 1 substitute + 2 synthetic.

- The product prints **two** tick numbers: `draw: begin tick=<playback>`
  (`main.cpp:12814`) and `animsum: tick=<server>` (`main.cpp:11883`), and
  **server = playback + S**, where S is roughly constant per demo (spy ~2.3-3.2k,
  sniper ~8.3-9.1k, soldier ~25.5k, demo ~24.6k, heavy 36.0k).
- The animation probe reports **server** ticks; `--capture-tick` takes **playback**
  ticks. Tickets that copied probe ticks across therefore drew the state at q+S: 12 of 13
  frames drew a different sequence than the label claimed. Converting with p = q - S
  recovers 6 of 12 labels; two targets are earlier than the product's first playback frame
  and are unreachable in their demo; four remain UNRESOLVED.
- Labels in that document are therefore assigned by the **drawn** sequence. Drawn
  coverage: idle 1/9, fire 4/9, reload 4/9, draw/deploy 5/9. Do not read the older
  "planned" table as coverage.
- Pixel evidence: two same-camera pairs are attributable (heavy idle vs synthetic
  16,882 px / 0.0196%; pyro 25,227 px / 0.0293%); the 13 cross-tick pairs are 87%-99.98%
  changed and are explicitly **not attributable**.
- A first attempt captured 12 frames of pure world because `--viewmodel` (the enabling
  flag, `main.cpp:6879`) was omitted -- `--viewmodel-sequence` only overrides. The
  synthetic cases caught it; every case now passes
  `--viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-fov 90
  --viewmodel-apply-animation`.

## Instance budget negative control (C=24)

The instance-budget rejection counter had never been seen to fire on a real recording.
Rebuilding with capacity 24 proves it is live, not dead code:

```
budget: instances capacity=24 submitted=96 accepted=24 slots=444
        rejected=missingMesh:0,deferredNotResident:41,badMesh:0,nonFinite:0,budget:31
```

- The account closes (24 + 41 + 31 = 96), budget rejections peak at 43, no
  `missingMesh` and no `0x8007000E` appear anywhere in 596 ledger lines.
- The mutant exe (`a2ee1477...`) and its frame (`7454e4d2...`) were each produced twice
  and were byte-identical -- the reproducibility claim is measured, not assumed.
- The restore returned the exe to `8bff0858` and the tree to clean.

## What is still open (do not read these as done)

| Item | State |
|---|---|
| 15-step release chain on this tree | NOT RUN (user decision for this batch) |
| Five October gate pins | DRIFT-CONFIRMED / NOT-RUN; re-pin only after adding pose+demo assertions |
| material-coverage 6 reds | PIN-DRIFT validated; re-pin deferred to a freeze |
| Cart visibility, 3D skybox, control-point state | NOT VERIFIED / UNSUPPORTED (`sky_camera` has zero hits in `native/`) |
| Instance capacity 96 -> 128, atlas 384 -> 400 MiB | analysed, deliberately deferred (either would change frame pixels and void the fresh pins) |
| S3 camera-script seam, attach-budget assertions | patch ready, **not applied** (applying moves the identity and voids this batch's captures) |

The full per-defect ledger is `DEFECTS-INTEGRATOR.md` in the batch directory outside this
repository (I-20 .. I-27 are the ones opened today).
