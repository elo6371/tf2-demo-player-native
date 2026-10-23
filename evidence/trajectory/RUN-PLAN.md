# T53 run plan — how to turn "the gate is fixed" into a verdict

STATUS: this plan has not been executed. The code it describes (`a23f12d` split into
`195801e` / `ed753a5` / `38c8d84`, plus the `STALE-BINARY` guard) carries
`CODE ONLY. NOT VERIFIED BY A RUN.`

## 0. Before anything: the binary must be the current source

A mutation is caught by rebuilding the product with a defect in it. If the run
stops between "the mutant was built" and "the real binary was rebuilt", the tree
is left holding a **mutant**. On 2026-10-23 that happened twice on this host, in
two different gates, for the same structural reason: the restore step sat after a
failure path.

So the first command is never a reading:

```bash
cd /d/TF2_Native_Test
git status --porcelain -- native     # must print nothing
bash build-cmake.sh                  # must end with exe_count=49 and 0 errors
```

The gate now also refuses on its own (`STALE-BINARY`): `native/` dirty, or
`projectile_track_probe.exe` older than `projectile_track.cpp`, exits 1 with the
file named. If you see `STALE-BINARY FAIL`, do not argue with it — rebuild.

## 1. Which tree

The integration tree, `D:/TF2_Native_Test`, after this branch is merged. The
`--mutation` half rebuilds the product, so it must hold the host lock:

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh acquire t53-gate 900
```

## 2. Commands, in this order

```bash
bash trajectory-overlay-check.sh                 # forward
bash trajectory-overlay-check.sh --mutation      # nine mutants
```

## 3. What the readings must be

| reading | expected | why |
|---|---|---|
| `STALE-BINARY OK` | present once | the binary describes the source |
| forward `OK`/`FAIL` | 0 FAIL | the forward half was already valid; this run is re-establishing it after the gate edits |
| `R8c` | green, `growPacketsAbove1ms=0` | the capacity-growth mechanism is gone |
| `R8d` | green, `max` well under 16.7 ms | one frame at 60 Hz, not a lucky sample |
| `R8b` | printed, **not** a verdict | it was a coin flip: 2 reds in 5 runs on one binary |
| `m2` | `FAIL R4c subject has ... samples; a flight is 40..300` | R4c alone must catch it |
| R8b inside m2 | must not be the only red | if R8b is what catches m2, the suite is back to luck |
| meta-mutation | `META-CATCH` | an unrelated injected failure must raise the count by exactly one |
| `MUT_RC` | 0 | every mutant caught and none silently ignored |

## 4. If a reading is off, look here first

- **`MUTATION-CAUGHT` while a `FAIL` appears inside a mutant block**: the
  `MUT_FAILS` fix did not take. That is the exact defect `38c8d84` addresses, and
  the old behaviour was to print the FAIL and count nothing.
- **`MUTATION-INVALID`**: a mutant could not be built or patched. Not a catch,
  not a pass. Count it as "the suite proved nothing about that rule".
- **`R8c` red**: capacity growth is back — check whether `reserve` survived the
  merge (it is five lines in `projectile_track.cpp`, easy to lose in a conflict).
- **`R8d` red with `R8c` green**: the append is slow for a reason that is not
  growth. That is a real finding, not noise; do not raise 16.7 ms.
- **`META-CATCH` missing**: the meta-mutation threshold is wrong again. It must be
  "baseline + 1 measured at run time", never a literal.

## 5. After the run

Only then change the commit messages from `CODE ONLY` to the readings, and merge
this branch into `p0-entity-protocol`. A branch merged before its first run is a
claim, not a fix.