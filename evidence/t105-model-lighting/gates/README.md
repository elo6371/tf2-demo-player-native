# Gate transcripts for this tree

A transcript in here means the gate actually ran on this tree, and the file is its
full output: a provenance header saying the command and the commit, then the gate's own
lines with nothing elided. The tree is `cfd01bd1`, which is `e0e80fa2` plus
documentation and comment edits; the product binary is
`a052de0875f38a38c5119ded0d17f29619997857fd481960791d46ccb29fd9b1`.

| file | gate | verdict |
|---|---|---|
| `warn-check.log` + `warn-check-tus.txt` | `bash tools/warn-check.sh` | `WARN-GATE=PASS new=0 fixed=0 same=0`, exit 0, 142 TUs |
| `argument-refusal.log` | `bash argument-refusal-check.sh` | `VERDICT argument-refusal: PASS`, exit 0 |
| `class-pixels.log` | `T62_CLASS_OUT=.scratch/t62-class-pixels bash class-pixels-check.sh` | `CLASS-PIXELS=PASS`, exit 0 |

A gate that did not run has no file, and `../REPORT.md` says so in words rather than
implying a pass -- see its section 'Gates: what was run here'.

TWO DETAILS THAT MATTER WHEN RE-RUNNING THESE. `argument-refusal-check.sh` and
`class-pixels-check.sh` both launch the product, so each capture takes the host-wide
lock at `/d/_shared/tf2-hostlock/.lock` (that lock exists so two worktrees never capture
at once -- a capture is timing-sensitive). Both logs therefore show a
`VERIFY-LOCK=ACQUIRED` / `VERIFY-LOCK=RELEASED` pair per case, and both gates had to
queue behind other owners before they could run. A run that loses the lock prints
`VERIFY-LOCK=FAIL ...` and exits 2; that is a retry signal, not a finding, and it is why
an earlier attempt at these two produced no transcript.

`class-pixels-check.sh` was pointed at `.scratch/t62-class-pixels` instead of its
default `evidence/t62-matrix/class-pixels`, so it wrote no tracked file. That directory
is a copy of the committed seeds (`settings.json`, `targets.txt`, `ui.json`) -- the gate
reads those seeds rather than inventing them, so the copy has to be seeded or the run
silently falls back to product defaults.
