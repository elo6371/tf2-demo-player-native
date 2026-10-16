# evidence/t2-visual-20261015 -- what each file is

Written 2026-10-15 with the T2 world visual acceptance round. Every readings
file is the captured output of a named command; nothing here is hand-edited.
The frames are the bytes the product wrote (BMP, committed) plus a
standard-library PNG conversion for viewing (`bmp-to-png.py` in the repo root).
`SHA256SUMS` at this directory's root covers every file below.

| Path | Produced by | What it is |
|---|---|---|
| `frames/` | `t2-visual-matrix.sh`, `world-material-check.sh` | The round's frames. Matrix, tick 0 paused: `on`, `sky-off`, `vis-off`, `water-off`, `repeat` as BMP+PNG, with `sha256.txt` and `comparisons.txt` (byte counts, bounds, regions). Gate frames: `world-lightmapped` (the world-material gate's section-6 launch), `cubemap-plain` / `cubemap-forced` (section 7, 2koth_abbey). |
| `readings/switches-tick0.txt` | `t2-visual-matrix.sh` | The six `gpu: world` trace lines (sky / vis / water, on and off) the matrix asserts. |
| `world-material/` | `world-material-check.sh` (forward) | The gate's eight sections' captured readings (`self-test`, fixtures, the real map's six capabilities, the negative control, the render-side trace lines). |
| `world-material-mutation/` | `world-material-check.sh --mutation` | The same sections re-run under the mutation branch, the two `mutated-*.txt` target readings, and four build logs (two mutated rebuilds, two restore rebuilds). |
| `world-material-effects/` | `world-material-effects-check.sh` (forward) | Water self-tests and fixtures, the water draw readings, the ViewModel effect readings, the T1 regression readings. |
| `verify-render/` | `verify-render.sh` | The render tier's per-gate logs, its build log, and the tier verdict. |
| `pov/` | manual runs, recorded in `pov-water-live.txt` | The supplementary POV input: the water pair and the repeat control at live ticks (frames in PNG; hashes, byte counts and the control lesson in the txt). |
| `explore/ticks.txt` | manual runs | How the primary demo's ticks were selected (water pairs at ticks 0/3000/6000) and what each measured. |
| `analysis/` | `tools/bmp-diff-map.py`, `tools/crop-stack.py` | Difference heat maps and same-region crops used by the report (the clip switch's 13.44% change; the POV camera-shift control). |
| `tools/` | this round | The two analysis aids, committed with the evidence (they are not run by any gate). |
| `logs/` | -- | Full console logs of the record runs: `chain-1-matrix-and-wm-forward.log`, `chain-2-mutation-effects-verify-render.log`, `build.log`. |
| `inputs.txt` | -- | Input identities: repo commit, demo/map/POV hashes, tooling. |
| `exe-identity.txt` | -- | Product/probe hashes at record time. MSVC rebuilds can differ while source and behaviour stay equivalent; the source commit (see `inputs.txt`) is the identity anchor, not the hash. |

Not committed by choice: the raw scratch BMPs/traces/CSVs of the record runs
(`.scratch/t2-visual/`), which are re-derivable by re-running the scripts; and
the unmodified copies of the existing gates' own evidence directories
(`evidence/world-material/`, `evidence/world-material-effects/`), which this
round did not rewrite.
