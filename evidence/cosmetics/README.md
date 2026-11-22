# evidence/cosmetics -- what each file is, and what it was produced by

This directory is the committed half of the task-41 cosmetics card
(`docs/COSMETICS-2026-11-22.md`). Eleven artifacts were produced by the drivers -- seven
`.txt` and four `.png` -- and `instruments.txt` was assembled by hand from two committed
sources. The table names what wrote each one.

The drivers are NOT here and are NOT meant to be: `.scratch/wear41c-demos.sh`,
`.scratch/wear41c-accept.sh` and `.scratch/wear41c-ledger.sh` are scratch instrument code,
the card's brief forbids committing scratch, and the working tree they ran in is gone. What
survives is their output, which is what this directory is. Every file below is reproduced
exactly as the driver left it; nothing here was retyped or reformatted afterwards.

| file | produced by | what it is |
| --- | --- | --- |
| `acceptance.txt` | `.scratch/wear41c-accept.sh` | the on-path acceptance: four capture pins (ids / off / basic / basic-lit) all `bytes=2582406`, the 1:1 crop proof (id-pixel bbox, diff bbox, `frac_on_body=0.963`, `frac_within_2px=0.588`, `CROP=PASS`) and eleven `cosmetic:` bone-merge lines |
| `demos.txt` | `.scratch/wear41c-demos.sh` | the per-demo counter lines (bagel 600, bagel 56747, POV 60162), the `--player-animation off` negative control, the four `ACCOUNT=CLOSED` lines and the two probe-suite results |
| `identity.txt` | `.scratch/wear41-readings.sh` | the four-column off-path identity table (baseline / no-switch / off / basic) with its three cross-exe comparisons, plus the re-measured `--lighting basic` pin |
| `ledger.txt` | `.scratch/wear41-ledger.sh` | the paused tick-0 deterministic ledger: five columns, 980 masked trace lines each, `bmp_md5 4f0a047a2b0a21dac4a692a19680e220`, `control_a_vs_b: IDENTICAL` |
| `readings.txt` | `.scratch/wear41-readings.sh` | one `scene: cosmetics` line plus the per-(class, owner, wearable, model) bone-merge hit rate for stv tick 600 |
| `selftests.txt` | `.scratch/wear41c-demos.sh` | the two probe suites, one line per check: `--wearable-pose-selftest` 16 checks and `--wearable-fixture-suite` 10 checks, `failed=0` on both |
| `weaponattach.txt` | `.scratch/wear41c-demos.sh` | the held-weapon arm under `--cosmetics off`: `distinct_set: IDENTICAL`, `settled_line: IDENTICAL`, 553 lines each side |
| `instruments.txt` | hand-assembled from committed files | the same POV 60162 row read by the two instruments (task-40 probe table vs task-41 product frame table), one under the other |
| `onpath-off.png` | `.scratch/wear41c-accept.sh` | the 1:1 crop, `--cosmetics off`, default lighting |
| `onpath-basic.png` | `.scratch/wear41c-accept.sh` | the same crop, `--cosmetics basic` -- the acceptance crop |
| `onpath-basic-3x.png` | `.scratch/wear41c-accept.sh` | the same crop's head/upper band at 3x NEAREST |
| `onpath-basic-lit.png` | `.scratch/wear41c-accept.sh` | the same crop with `--cosmetics basic --lighting basic` |

The exe every line in this directory belongs to is
`sha256 3a3cb9f1ef348fc0fab27db6784ed6c36777f1ce21abef8d21e0a072b54f4b89`, identity tag
`a73f5ff84d724a053c0658cfd2556b8dbce6dfa259d33a7dda1d6d66d0c6078b`,
`contentMethod git-clean-v1` (the task/wear41-cosmetics branch build). Where that build came
from, and what the tree actually rebuilds to today, is `evidence/BUILDS.md`.

## Numbers in the card that this tree cannot reproduce

Four numbers in `docs/COSMETICS-2026-11-22.md` were measured on that branch and have no
committed source: they lived in the `.scratch/` traces and the `.scratch/` BMPs, both of
which were deleted with the branch. They are listed here instead of being quietly dropped,
so a reader knows exactly which claims are backed by a file in this directory and which are
not. Nothing in this list is a pin; no gate depends on any of them.

```
# section 7 -- the two BMP md5s behind the negative control, and the pixel counts
# between them. Both .bmp files and the diff tool were .scratch/ artifacts.
stv-negctl normal   bmp md5 15cbd4a9fc68118f93151fbd311b655b
stv-negctl anim-off bmp md5 512ccb46ae4ce8999bd8cfd7ce8ec628
negctl vs basic.bmp: 13890 px changed
negctl vs off.bmp  : 13725 px changed

# section 6 -- the bagel map lookup line and the uniform clear frame it produced
map: lookup map=koth_bagel_rc13 store=none selected=- bytes=0 reason=not found in maps, download/maps or a vpk
bagel frame: one colour (14,18,22), 860784 px, uniq=1
```

Note on the last line: `860784 = 1264 x 681`, the capture size, so "every pixel of the frame
is that one colour" is a statement about the whole surface, not a sample. The line is quoted
from the card's own record; the count itself is `width * height` and is reproducible from any
frame of that size.

Everything else in the card traces to a file in this directory or to a committed code
anchor. The bagel md5 `3305b1d8f0b3951b5e7093a974f7adbd` is in `demos.txt` (twice, the same
value, because both bagel runs drew nothing) and the POV md5 `dffe1665afcd9fc2ce8e43561a4ba4cc`
is in `demos.txt` too; both are evidence that the frame is empty, not pixel pins, and the
card says so.
