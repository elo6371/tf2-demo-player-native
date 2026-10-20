# T33 — Code quality, performance, and footprint audit (2026-10-20)

Status: **audit delivered**. No product source was changed (read-only audit mode,
per NEXT-TASKS-2026-10-20.md assignment order step 4). The committed increment is
this report, one new gate (`quality-audit-check.sh`) and its evidence. Deletion
candidates are listed in §7 with their proof and are **not** applied: they are
other tasks' evidence and the integration owner decides.

- Branch: `task/t33-quality-audit`, base `36303c2` (the integration base).
- Worktree: `D:/TF2_Native_Worktrees/t33-quality-audit`.
- Every build, product launch, capture and mutation in this report ran under
  `bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t33 ...`, with
  `--hidden-window --audio-disabled`. No audio device was opened.

## 1. Inputs and command lines

| input | value |
|---|---|
| resource root | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` |
| demo (pinned POV) | `.../tf/demos/autorecord_2026-07-02_13-26-46.dem` |
| map (reference) | `maps/pl_odyssey.bsp` (the demo's own map; installed loose) |
| toolchain | MSVC 14.44.35207, Windows SDK 10.0.26100.0, NMake Release |

Reference build, one clean full Release build:

    bash build-cmake.sh
    # started 12:03:20, finished 12:10:07 (6m47s); log: evidence/cmake-build.log

Measurement run (the source of every number in §4):

    TF2_NATIVE_TRACE=<win-path> native/build-nmake/tf2_demo_native.exe \
      --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
      --demo ".../demos/autorecord_2026-07-02_13-26-46.dem" \
      --hidden-window --audio-disabled --capture-tick 2000 \
      --capture-frame <win-path>.bmp \
      --viewmodel models/weapons/v_models/v_scattergun_scout.mdl \
      --viewmodel-render companion \
      --metrics-file <win-path>.csv

The gate command is in §6; it re-runs this capture twice and then mutates,
rebuilds and restores.

## 2. Footprint inventory (tracked and untracked)

Measured from `git ls-tree -r -l HEAD` plus `git status --porcelain --ignored`,
not from a directory walk: the committed tree is what a clone pays for.

| scope | files | bytes | share |
|---|---:|---:|---:|
| whole tree | 3,351 | 1,301,057,789 (1,240.8 MiB) | 100% |
| `evidence/` | 3,070 | 1,294,883,055 (1,234.9 MiB) | 99.5% of bytes |
| `native/` | 140 | 2.9 MiB | 0.2% |
| repo root (docs, scripts) | 141 | 3.0 MiB | 0.2% |

Untracked/ignored in this worktree: only `.scratch/` (the `.gitignore` rule);
no untracked evidence is present.

### 2.1 Duplicate content (sha256 over every tracked evidence file)

- 208 duplicate groups, 1,453 files, **565.0 MiB wasted** (45.8% of the evidence
  tree; 43.4% of the whole repository).
- By kind: `.dem` fixtures **385.9 MiB**, `.bmp` **167.5 MiB**, `.txt` 3.3 MiB,
  traces 0 (their names differ per run, so sha256 found no trace duplicates).

The largest group is the corrupt-input fixture matrix: six fixtures are copied
into four run directories (`evidence/stability-release/run-03/fixtures/` and
`evidence/t4-rerun-20261015-t4b/run-0{5,6,7}/fixtures/`). The 51,834,617-byte
`bad-magic.dem` and `bad-version.dem` are byte-identical to each other's copies
across all four runs (sha256 `cdf7214181c4…` / `3e9ec1e6751c…`), as is the
31,100,770-byte `mid-truncated.dem` (`c8ae5062bbc3…`).

Why that duplication is avoidable: the fixtures are **generated**, and the
generator records each file's sha256 in the run's tracked `fixture-index.txt`:

    powershell -File stability-make-inputs.ps1 \
      -Demo "<tf>/demos/<snakewater>.dem" -OutDir "<run>/fixtures"

so the bytes are reproducible from a pinned input and the identity of every
fixture is still checkable from the tracked index after the bodies are dropped.

### 2.2 Largest tracked files

    51834617  evidence/t4-rerun-20261015-t4b/run-0{5,6,7}/fixtures/bad-{version,magic}.dem   (6 files)
    51834617  evidence/stability-release/run-03/fixtures/bad-{version,magic}.dem             (2 files)
    31100770  .../mid-truncated.dem                                                           (4 files)
    23488808  evidence/manual-followup/traces/switch-t16306.trace
    10427775  evidence/t8-render-reliability-20261016/diag6-…/trace.txt
     7923963  evidence/t4-rerun-20261015-t4b/run-07/res-missing-model/trace.txt

### 2.3 Evidence that no report or script names

A full-path/stem scan of every tracked `.md`/`.sh`/`.py`/`.ps1` text found 384
evidence files (192.4 MiB) that no document names. This is **not** proof they are
unreferenced: several scripts build paths from variables
(`echo "$OUT/traces/trace-ref-…"`), which a literal scan cannot see. The largest
hits are the `evidence/visual-baseline/traces/*.txt` and
`evidence/manual-followup/traces/*.trace` machine products. Treat the list as a
lead, not as a deletion warrant.

Policy drift: 23 tracked files already match a `.gitignore` rule that says the
same kind of file should not be tracked (18 `oracle-*.txt` under
`evidence/after-fix/`, `evidence/fast/00-SUPERSEDED-2026-10-14.md`, 4
`evidence/manual-acceptance/*.log`). They are small (well under 1 MiB total);
recorded so the next cleanup starts from the right list.

## 3. Release build measurements

| reading | value |
|---|---|
| build rc | 0 |
| compiler errors | 0 |
| compiler warnings | **10** |
| executable targets built | **41** (`native/CMakeLists.txt` `add_executable` count = 41) |
| total exe bytes | 21,169,664 (20.2 MiB) |
| `tf2_demo_native.exe` | 1,818,624 B (1.73 MiB) |
| largest probe | `viewmodel_animation_probe.exe` 907,264 B |

All 10 warnings are pre-existing at `36303c2` (T33 changes no product source):

| code | count | site | meaning |
|---|---:|---|---|
| C4244 | 5 | `native/src/main.cpp:1103–1107` | double→float conversion in the debug-camera argument parse |
| C4457 | 1 | `native/src/main.cpp:6185` | local `instance` hides a function parameter |
| C4267 | 2 | `native/src/native_renderer.cpp:3642–3643` | `size_t` → `uint32_t` when the texture-upload/reuse counters are copied into the sample struct — the counters this audit pins are the ones being narrowed |
| C4456 | 2 | `native/src/native_renderer.cpp:3950, 4120` | local `view` hides a previous local |

`exe_count=41` is worth recording against the task book's older "25 exe" note:
the tree now builds 41 executables, and `verify-all.sh`'s own step 1 pins a count
that must be read from the code, not from the README.

## 4. Runtime readings on the pinned reference frame

One run, tick 2000 exactly (first attempt), 60 s wall clock of which 28.2 s were
the 1,772 rendered frames:

    trace: 3,611,688 bytes / 31,969 lines
    frame: 2,582,406-byte BMP, sha256 f87f396a60dc72082f232bcd71547066c02c433e35ccda1576c3b21333ef9608
    metrics rows: 28 over 28.16 s

Deterministic load-time readings (all from the trace; quoted verbatim):

    gpu: world material-fetch distinct=108 tiled=85 capped=0 vmtMissing=3 vmtNoBase=20
      vtfMissing=0 vtfTooLarge=70 decodeEmpty=0 maxWidth=1024 maxHeight=2048
      fromPakfile=85 downscaled=70 blendFirst=10 blendLastRank=10 blendCapped=0
    gpu: world atlas-plan batches=2 blendBatches=1 tiled=85 refused=0 malformed=0
      duplicate=0 budget=0 layerBytes=229322128
      reason=85 of 85 materials tiled in 2 batch(es); refused=0 (malformed=0,
      duplicate=0, budget=0); layers=89+64+64+0+0 MiB
    gpu: world atlas size=4112 bytes=67634176 atlases=2 totalBytes=94053776 state=bound
    gpu: world blend enabled=1 terrainOnly=0 textures=85 layers=10 accepted=85
      acceptedLayers=10 tiles=10 modulateTiles=10 malformedLayers=0
      weightedVertices=85920 modulatedVertices=85920 weightMin=0.000
      weightMax=1.000 distinctWeights=34
    gpu: world sky-upload state=active vertices=36 triangles=12 faces=6 side=512 resampled=1
    gpu: world feature-fetch bumpSwitch=0 bumpDeclared=0 … detailSwitch=0 detailDeclared=0 …
    gpu: world isolation weaponDrawsWithWorldAtlas=0 worldDrawsWithWeaponTexture=0
      slot0Binds=5 batchDraws=2 texturedBatchDraws=2
    gpu: viewmodel materialReadings passRan=1 materialSlots=3 materialResolved=3
      texturesUploaded=7 texturesReused=2 fallbackSlots=0 … drawRanges=2

So the texture work is: **85 material tiles in 2 albedo batches, 1 blend batch,
2 atlas textures, 89.7 MiB on the GPU; the ViewModel pass uploaded 7 textures and
served 2 draws from the cache** (the reuse counter is wired: it is incremented on
a cache hit at `native/src/native_renderer.cpp:3960`, so `texturesReused=2` is a
real hit count, not a placeholder).

Envelope (not deterministic; the readings the gate bounds):

| reading | value |
|---|---|
| rendered frames / elapsed | 1,772 / 28.16 s |
| median fps (after 5 s) | **66.7** (min 58.4, max 77.5) |
| mean frame time | **15.89 ms** |
| peak working set | **2,825.8 MiB** (first sampled row already 2,822.6 MiB) |
| peak private bytes | 3,077.7 MiB |
| working-set change after 5 s | **−69.0 MiB** (the load is the peak; no steady growth) |

The last two rows matter for the audit: the reference-frame scenario peaks at
2.83 GiB of working set, which is 38% above the 2,048 MiB ceiling
`stability-release-check.sh` publishes — for a **different** scenario (a
snakewater smoke run). That is not a contradiction, but it does mean this
reference-frame scenario had no published memory ceiling before this audit. The
new gate publishes one (3,072 MiB, the stability gate's private-bytes ceiling)
and it would have gone red on the old 2,048 MiB number.

## 5. Hot-path inspection

Static, with the line that decides each question. Each entry says whether the
claim is measured or only read off the code.

1. **Repeated VMT/VTF parsing — load-time only, deduplicated per material, not
   per reference.** `materialOrder` is built from the map's triangles and every
   distinct material is fetched once (`main.cpp:2583–2602`); raw VTF bytes are
   cached per material path in `materialVtfCache` (`main.cpp:2077`). Feature
   VMTs (`<material>_detail`, `_bump`, `_bump2`) are looked up and parsed per
   owning material with no parse cache (`main.cpp:2924–2973`), so a detail VMT
   shared by N materials is parsed N times. On this frame `detailDeclared=0` and
   `bumpDeclared=0` (the feature switches are off), so the reference run pays
   none of it. **NOT VERIFIED**: no parse/lookup counter exists, so the count is
   unknown even when the switches are on. Cheapest next step: count
   `AssetStoreChain::lookup` and `VmtParser::parse` calls and print them on the
   existing `material-fetch` line; one line of instrumentation, no new tool.
2. **Atlas copies / redundant CPU mirror — a real, measured mirror.** The world
   pass builds one `WorldTexture` per material with albedo plus blend/modulate
   RGBA (`main.cpp:3026–3052`) and keeps the whole vector alive for the run
   (`main.cpp:2023`, read again by the capture-frame face trace at
   `main.cpp:4974–4990`). The plan prices that data at **218.7 MiB**
   (`layerBytes=229322128`) — a CPU copy of pixels that are also resident in the
   atlas textures (`totalBytes=94053776`). It cannot simply be freed after the
   upload because the capture-path face trace reads the per-material pixels.
   Proposal (out of scope for an audit commit): keep only the per-material
   rectangle and source sizes the face trace needs, and measure the peak working
   set with this gate before/after. Expected saving is bounded by
   `layerBytes` minus the metadata the face trace actually needs, i.e. up to
   218.7 MiB of the 2,825.8 MiB peak. **NOT VERIFIED** until the edit is measured.
3. **Per-frame allocations — none found in the frame loop.** The per-frame world
   trace is guarded (`worldDrawTraceEmitted`, `main.cpp:4659`): the four
   `unordered_map`s it builds (`main.cpp:4722,4724,4751,4757`) run once per run,
   not per frame. `traceFmt` early-returns when no trace file is open
   (`main.cpp:85`). Metrics writes are throttled to 1/s (`main.cpp:5900`) and
   the loop-rate counters are `verify-all.sh` step 18's. The 18 trace lines per
   frame in the measured run are `draw: begin/end` plus the scene snapshot lines.
   **NOT VERIFIED** that the renderer's own draw path allocates nothing; that
   needs a sampling profiler or allocation counters, which this host does not
   have installed.
4. **Shader resources / batching — already tight; one reading-design caveat.**
   85 materials reach the GPU as 2 textured batch draws (`batchDraws=2`) and
   2 albedo atlas textures (94,053,776 B); the sky is 6 faces / 1 draw. The
   feature layers are off on this frame (`bumpSwitch=0`, `detailSwitch=0`,
   `bumpDeclared=0`, `detailDeclared=0`), so the plan prices 0 for the two
   feature layers and its own reason line reads `layers=89+64+64+0+0 MiB`
   (albedo 89, blend 64, modulate 64). Blend and modulate are full-albedo-size
   plan-priced layers; the renderer's `worldAtlasTotalBytes` counts the two
   albedo batches only, so `layerBytes=229,322,128` and
   `totalBytes=94,053,776` describe different sets of textures. **NOT VERIFIED**
   which reading a future budget should use; reconciling them is a
   reading-design task, not a code fix.
5. **Warning-level code quality.** The two C4267 warnings are on the
   upload/reuse counters the gate now pins (`native_renderer.cpp:3642–3643`,
   `size_t` → `uint32_t`); a 64-bit counter truncated into a 32-bit sample is the
   kind of defect a footprint gate must not inherit. Proposed low-risk fix
   (needs owner approval, `native_renderer.h`/`native_renderer.cpp` are shared
   with T29/T30): widen the two sample fields to `std::size_t` and assert the
   reading does not change. The other eight are style (shadowing) or a
   deliberate float conversion in the debug-camera parser.

Nothing in this section proposes removing a diagnostic, weakening a refusal, or
changing a rendering or playback semantic.

## 6. Gate: `quality-audit-check.sh`

New file, sha256 `af1390f18bd874d3e8adcc0a8d692580fda0c1cbb44b32a89d6e230894e03873`
(`wc -l` is in the reproduced log; the hash is the one to compare).

What it asserts, forward mode (two captures of the same scene at tick 2000):

1. The 20 deterministic readings quoted in §4 as exact pins (material-fetch,
   atlas plan, atlas, blend, sky, isolation), plus work-count denominators
   (trace lines ≥ 1000, metrics rows ≥ 5, rendered frames ≥ 500) so a run that
   drew nothing cannot pass on empty strings.
2. The two captures must be **byte-identical** — the deterministic frame
   comparison, stated as a product property rather than as a pixel hash pinned
   against a tree T29/T30 are expected to change.
3. The envelope: peak working set ≤ 3,072 MiB, working-set change after 5 s
   ≤ +32 MiB, median fps ≥ 30. These are leak/stall bounds, not a benchmark —
   the measured values are 2,825.8 MiB, −69.0 MiB and 66.7 fps.

Mutation mode adds one real source mutation: `maxTilesPerBatch` 64 → 20 in
`native/include/world_tile_atlas.h`. All 85 tiles still reach an atlas and the
frame keeps its pixels, but the plan needs more batches and the renderer creates
more atlas textures. The gate must report different batch/atlas counts; the
source must return byte-identical; the restored build must reproduce the
reference readings and frame.

    bash quality-audit-check.sh              # forward: 2 x 60 s captures
    bash quality-audit-check.sh --mutation   # forward + mutated + restored runs + 2 incremental builds

Exit codes: 0 = all pins held (or mutation went red and restored); 1 = an
assertion failed; the evidence files are `evidence/quality-audit/readings.txt`,
`gate.txt` and `mutation.txt`.

### 6.1 Results

**First run (12:18:41–12:24, superseded, FAIL).** The script had a gate-side bug:
`assert_pins` read `ref-a.pins` while `forward()` only wrote `ref-a.out`, so every pin
assertion read an empty string, and `assert_le`/`assert_ge` compared empty as 0 and printed
`OK` for two envelope bounds. The 20 product readings themselves were captured
correctly and matched the pins (see below), but the run's verdict is FAIL and it is
**not** the delivered evidence. Two fixes went in: the pins file is now written,
and both range assertions reject a missing reading by name. This is recorded
because "a gate that prints PASS while checking nothing" is a defect class this
repository has already recorded four times, and the audit caught its own gate
doing it.

The superseded run is still informative on the mutation, and those readings are
quoted here for that reason only:

    mutated plan:   atlas_batches 2 -> 5, atlas_count 2 -> 5
                    atlas_plan_layerbytes 229,322,128 -> 133,154,784
                    atlas_totalbytes 94,053,776 -> 90,883,424
                    frame sha256 b8270c71372f6bd0cbe087c2c6ba2955651f858dcd78822d2f660ec6660cfc1d
                    (reference f87f396a60dc72082f232bcd71547066c02c433e35ccda1576c3b21333ef9608)
    restore:        source byte-identical (cmp), rebuilt binary relinked
                    (sha256 eb4fc8c3… vs 09096554…), restored frame and readings
                    reproduced the reference

Three independent captures of the same scene in that run (ref-a, ref-b, restored)
reported the same 20 deterministic readings and the same frame sha256; only the
envelope columns differed (peak WS 2,781.4 / 2,805.4 / 2,825.9 MiB, median fps
63.9 / 66.7 / 67.9), which is what the `det()` split in the gate now exists to
state explicitly.

**Corrected run (12:46:50–12:51:46, PASS, exit code 0).** Log committed as
`evidence/quality-audit/run-log.txt`; the superseded run's log is committed too, as
`run-log-superseded.txt`, so a FAIL cannot be mistaken for a pass.

    bash quality-audit-check.sh --mutation      # rc=0
    OK frame determinism (same scene twice) = f87f396a…9608
    OK the two runs report identical deterministic readings
    OK material-fetch distinct = 108            (all 20 deterministic pins OK)
    OK trace lines = 31507 >= 1000
    OK metrics rows = 26 >= 5
    OK rendered frames = 1628 >= 500
    OK peak working set MB = 2181.6 <= 3072
    OK steady growth MB (after 5 s) = -10.7 <= 32
    OK median fps after 5 s = 65.9 >= 30
    -- mutation: maxTilesPerBatch 64 -> 20 --
    binary_before=eb4fc8c35ffecd895a48367814458e9b4d684ab22474e367e5175377562e75b7
    RED as required: atlas-plan batches 5 (was 2), atlases 5 (was 2)
    RESTORE=OK (source byte-identical)
    RESTORE=NOTE the rebuilt binary sha256=d58417d0… differs (relink artifact)
    OK restored readings reproduce the reference exactly
    OK restored frame determinism = f87f396a…9608
    PASS: the forward pins held, the mutation went red, the source and the binary came back, the restored tree reproduced the readings.

Counted evidence: 20 deterministic pins + 2 determinism/work-count assertions +
4 envelope assertions = 26 named assertions, then the mutation's batch/atlas
count change, its source `cmp`, and the 20-pin + frame reproduction on the
restored build. Captures: 4 in the corrected run (forward A, forward B, mutated,
restored), 4 in the superseded run.

The corrected run's peak working set is 2,181.6 MiB against 2,805.4 / 2,781.4 /
2,825.9 MiB in the superseded run's three captures. That ~650 MiB spread is why
this reading is bounded, not pinned: it is real run-to-run variance (page cache,
other processes on the shared host), and pinning it would turn a correct run
red for a reason the product did not cause.

## 7. Deletion candidates (owner decision, not applied)

| candidate | proof it is disposable | saving |
|---|---|---:|
| `evidence/t4-rerun-20261015-t4b/run-0{5,6,7}/fixtures/*.dem` | byte-identical to `evidence/stability-release/run-03/fixtures/*.dem` (sha256 groups cdf7214181c4/3e9ec1e6751c/c8ae5062bbc3…); regenerable by `stability-make-inputs.ps1`, whose `fixture-index.txt` records each sha256 and is tracked | **386.1 MiB** |
| `evidence/t2-visual-20261015/verify-render/**` (145 MiB total dir) | a captured tier-2 run; README §分级验收 says tier-2 output belongs in `evidence/fast/` which is gitignored. Includes 7 identical `control.bmp`s and 6 identical `vm*.bmp`s already covered by the duplicate groups. **Caveat:** the directory is named by `t2-visual-matrix.sh` and by `T2-WORLD-VISUAL-ACCEPTANCE-2026-10-15.md`, so removing it weakens a cited acceptance report even though the bytes are regenerable; this one needs the T2 owner as well as the integration owner | up to 145 MiB |
| duplicate `*.bmp` within the same evidence directory (36 groups) | each group is "the same picture twice" (a control and the witness it is compared to); whether the second copy carries an independent claim is per-report | up to 167.5 MiB, needs per-group review |
| 23 tracked-but-ignored files (§2.3) | the tree's own `.gitignore` says these kinds are machine output | < 1 MiB |

`git rm` for the first row, if approved:

    git rm evidence/t4-rerun-20261015-t4b/run-0{5,6,7}/fixtures/{bad-magic,bad-version,mid-truncated,truncated-packet,incomplete-netmsg,truncated-header}.dem

I did not delete them because they are T4's evidence and the task assignment says
another owner's evidence goes to the Lead as a list, not to `git rm`.

## 8. Limitations and NOT VERIFIED items

1. **2.83 GiB peak working set is not attributed.** Measured, but the audit
   could not split it per load stage: the metrics file starts only after the
   world load. Named and measured pieces total ~0.4 GiB (atlas 89.7 MiB, plan
   layerBytes 218.7 MiB, lightmap atlas 4096x2251 = 36.9 MiB, 297,936
   lightmapped vertices). Cheapest next verification: one run with
   `--start-paused` (no world pass) to measure the non-world baseline, then one
   run at the reference frame; the difference is the world/world-load share.
   This needs the product launched, so it is a locked-step follow-up.
2. **No new product optimization was measured or committed.** The task requires
   before/after measurements for any change, and product edits are frozen for
   T33 until the owners of the touched files have merged. The two proposals in
   §5.2 and §5.5 come with the measurement plan and the gate that would prove
   them; neither is installed.
3. **VMT/VTF parse counts**: no counter exists (see §5.1); the feature-VMT
   re-parse claim is read off `main.cpp:2924–2973`, not measured.
4. **GPU-side cost** (draw time, texture-creation time) is not measured: the
   product exposes draw counts (`batchDraws=2`) but no GPU timestamps, and this
   host has no GPU profiler in the toolset.
5. **Frame rate is a hidden-window rate.** T23 measured hidden and visible
   captures byte-identical at 1264x681, so the picture claim transfers, but the
   rate here includes the hidden-window present path and must not be quoted as
   an on-screen benchmark.
6. **The pins are current-tree readings (36303c2).** T29/T30 are expected to
   move world visuals and possibly the material-resolution counts; when they
   merge, update the pin in the same commit and say which reading moved. Do not
   widen a pin to make a red run green.
7. **Deletions are proposals.** Nothing was removed, so the repository size is
   unchanged by this branch; the saving in §7 is available, not realised.
8. **The binary is not reproducible across relinks.** Three relinks of one
   source produced three hashes (§10). If reproducible binaries are wanted, add
   `/Brepro`; that is a build-config change, not a product-behaviour change, and
   it is not applied here.

## 9. Reproduce

    cd /d/TF2_Native_Worktrees/t33-quality-audit
    bash build-cmake.sh                       # clean Release build, ~6m47s
    bash quality-audit-check.sh --mutation    # under hostlock, ~8 min
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status

Evidence committed: `evidence/quality-audit/`. Machine products (traces, BMPs,
metrics CSVs, build logs) stay in `.scratch/t33/` (gitignored); the committed
readings carry the trace line they were read from, and the frame is identified by
its sha256 rather than by a 2.5 MiB copy — the same rule the repository applies to
large machine products elsewhere.

## 10. Source and binary hashes

| object | sha256 |
|---|---|
| `native/build-nmake/tf2_demo_native.exe` (clean Release build) | `090965547873217fa24c04c02d7cf216dde81cb6e86108f59ed900d193dfcfa3` |
| same source, relinked in the superseded run | `eb4fc8c35ffecd895a48367814458e9b4d684ab22474e367e5175377562e75b7` |
| same source, relinked in the corrected run | `d58417d0731c6d274646f6594952cf77f00cd4814ffbd454887fce0543cd6327` |
| `quality-audit-check.sh` | `af1390f18bd874d3e8adcc0a8d692580fda0c1cbb44b32a89d6e230894e03873` |
| reference frame (tick 2000, four independent captures) | `f87f396a60dc72082f232bcd71547066c02c433e35ccda1576c3b21333ef9608` |
| mutated frame (maxTilesPerBatch 64 -> 20) | `b8270c71372f6bd0cbe087c2c6ba2955651f858dcd78822d2f660ec6660cfc1d` |
| mutated source | saved and restored with `cp`, proved byte-identical by `cmp` (§6.1) |

Three different binary hashes from the **same source**: this toolchain's link is
not reproducible (the NMake path passes no `/Brepro`), so a binary hash is not a
restoration proof here — the source `cmp` plus the reproduced readings and
frame are. Proposal (build-level, low risk, needs an owner): add `/Brepro` to the
link options so two builds of one commit produce one hash; cheapest verification
is rebuilding twice and comparing, which this audit already did by accident.

No product source file differs from `36303c2` (see the empty
NaNgit status --short` in §11).

## 11. Worktree and process state

- Branch `task/t33-quality-audit` at one commit on top of `36303c2`; the commit
  adds this report, `quality-audit-check.sh` and `evidence/quality-audit/`
  (readings, gate verdict, mutation record, both run logs).
- `git status --short` after the commit: **empty** (verified by the delivery
  message; `.scratch/t33/` is the only untracked path and it is gitignored).
- T33 released the host lock at 12:51:46 when the corrected run exited. The lock
  was then taken by t30 for its own window; T33 holds no lock and no T33 product
  process remains. At 12:51:57 the only `tf2_demo_native`/`*_probe` process on
  the host was `model_robustness_probe.exe` (T30's active run), not T33's.
- All machine products (4 captures, 4 traces, metrics CSVs, build logs) are under
  `.scratch/t33/`; nothing machine-generated was left in a tracked path.
