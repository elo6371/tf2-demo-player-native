# Gate transcripts for this tree

`warn-check.log` is the stdout of `bash tools/warn-check.sh` run from the worktree
root at commit `e0e80fa2`, i.e. on the exact tree this evidence describes. Exit 0.
`warn-check-tus.txt` is the list of translation units it compiled, copied from the
gate's own `.scratch/warn-check/tus.tsv`.

A transcript in here means the gate actually ran on this tree. A gate that did not run
has no file, and the report says so in words rather than implying a pass -- see
`../REPORT.md`, section 'Gates: what was run here, and what was not'.

WHY TWO GATES ARE MISSING. `argument-refusal-check.sh` and `class-pixels-check.sh`
both launch the product, so both take the host-wide lock at
`/d/_shared/tf2-hostlock/.lock` before capturing (that lock exists so two worktrees
never capture at once -- a capture is timing-sensitive). Another owner held it
continuously while this evidence was assembled, and a run that loses the lock exits
2 with `VERIFY-LOCK=FAIL ...`, which is a retry signal and not a finding. Rather than
publish that as a result, the two gates are recorded as not run.
