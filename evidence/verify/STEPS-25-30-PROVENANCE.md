# Steps 25-30: what these files are, and where they came from

VERDICT-STEPS-25-30=RUN13

These are the text artifacts of verify-all.sh steps 25-30 from the one run that
took all thirty steps green. They live in the tree so that a reader can check the
chain's own claims instead of trusting the chain's log.

## Producer

- run: RUN13, 2026-11-09, started 04:58:13Z (12:58 CST), finished 16:12 CST
- HEAD: `a5ebd8c069b0` (branch `integration/t91`)
- product exe sha256: `abb56698cd87df49a73c413ab66194c003c96bc1b6ff5e26e246288316420807`
- result: `VERIFY=PASS`, rc 0, 11,418 s, all 30 steps (the 58-file evidence
  commit is `4a8aaff`)
- chain log: `D:/_shared/EXEC-2026-11-09/RUN13-VERIFY-ALL/verify-all.log`
  (outside the tree; `summary.txt` beside it records rc, seconds and the exe)
- file mtimes: 15:14-16:08 CST on 2026-11-09, in step order

`28-world-geometry/geometry.txt` identifies its own producer in section 0
(`HEAD=a5ebd8c069b0 branch=integration/t91`). The other directories do not print
a HEAD, which is why this file exists.

## What is tracked, per step

| step | directory | tracked | left out |
|---|---|---|---|
| 25 player animation | `25-player-animation/` | 16 `.txt` readings (40,937 B) | -- |
| 26 animation pixels | `26-animation-pixels/` | 5 `.summary` + 2 `.txt` | 5 BMPs (13 MB), 5 empty logs |
| 27 sourceTV relay | `27-sourceTV-relay/` | 18 `.txt` (window, handover, seek, replay, negatives) | 9 traces (46 MB), 8 BMPs (21 MB) |
| 28 world geometry | `28-world-geometry/` | `geometry.txt` (9,032 B) | -- |
| 29 world materials | `29-world-materials/` | 20 `.txt` + 21 `.csv` | 21 traces (30 MB), 21 BMPs (54 MB), 21 empty logs, 4 `.build` logs |
| 30 review workspace | `30-review-workspace/` | 2 `.txt` + 3 `.script` | 4 traces, 9 `.dem`, 21 `.ini` (the gate's own workspace) |

## Why they were missing

`.gitignore` ignored these six directories whole. A directory-level ignore cannot
exclude only the frames, so RUN13's evidence commit had nothing to add for steps
25-30 -- the chain's most expensive steps were the only ones with no committable
proof, and the gap was invisible because `git status` stayed clean.

The ignore rules are now content-level: the binaries stay out (they are why the
directories are large), the readings come in. Total tracked here is about 400 KB.

## Caveat

These files are written in place by the gates. A later chain run will modify them
and the tree will look dirty until the new readings are committed, exactly as with
every other tracked file under `evidence/`. Do not read them as a statement about
the current HEAD -- read the producer fields above, or the current run's log.
