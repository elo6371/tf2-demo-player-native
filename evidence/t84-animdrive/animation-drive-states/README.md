# T84 animation-drive-states — what is kept, what is not

`animation-drive-states-check.sh` runs the product nine times (two ticks for flinch, three for crouch,
one repeat, a coverage run at tick 20000, an early run at tick 12000) and then copies the frames and
traces into four mutated directories, one per negative control. Each capture is a 2.5 MB BMP plus a
40-80 MB trace; the output directory held **2.9 GB** after the 2026-11-06 run.

**Tracked here**

| File | Why |
|---|---|
| `gate.txt` | the verdict and every assertion line, including the ticks, the md5s and the negative-control outcomes |
| `default.bmp` | the frame assertion `A` compares against the `--animation-drive legacy` frame (they must be byte-identical) |
| `states-crouch.bmp` | the states half of the crouch pixel pair |
| `legacy-crouch.bmp` | the legacy half of that pair — assertion `B` requires the two to differ |
| `*.csv`, `*.log`, `*.rc` for those three | the per-run metrics, the product's stderr and the child's exit code |
| `neg-*/check.txt` | each negative control's own check output, which is what proves the control failed for the intended reason |

**Not tracked** (ignored in `.gitignore`, regenerable): every other `.bmp`, `.csv`, `.log`, `.rc` and
every `.trace` in this directory and in `neg-*/`. The traces are the bulk of the 2.9 GB and are pure machine
output; the frames they accompany are reproducible because the product binary and the ticks are pinned in
`gate.txt`.

**To regenerate**: `bash animation-drive-states-check.sh` on the product binary named in
`../../t62-matrix/RUNS-2026-11-04.md` §25.4, under the shared host lock. The gate rewrites this directory in
place; the tracked files above are the ones a run of record replaces.
