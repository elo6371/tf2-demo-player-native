# T2 rerun 20261014-215000-t2b

The verification run. `world-material-effects-check.sh` forward pass, executed
against a **different binary** from the t2a run, to answer the question the t2a
run raised: the build is not byte-reproducible, so does the evidence still hold
when the binary changes?

| file | what it is |
|---|---|
| `exe-identity.txt` | the sha256 of the binary this run measured, and how it was built |
| `tf2_demo_native.2729dc80.exe` | that binary, kept so the run can be re-verified rather than only cited |
| `gate-forward.log` | the forward pass. `WORLD-MATERIAL-EFFECTS=PASS`, 101 OK, 0 FAIL |
| `world-material-effects/` | the effects gate's own evidence files |

Result: **identical**. `diff` of the 101 `OK` lines and every measured byte count
against the t2a forward log is empty, and all ten frames are byte-identical
(water-off `fdefb876`, water-on `e109a39e`, vm-natural `68d51a6c`,
vm-phong-off `61c766ae`, vm-bump-off `763510da`, vm-selfillum-on `d62f7b78`,
t1-sky `fdefb876`). Two different binaries of the same source produce the same
readings and the same frames.

Command, verbatim:

```
RUN_TAG=20261014-215000-t2b
WORLD_MATERIAL_EFFECTS_SCRATCH=".scratch/t2-rerun-$RUN_TAG" \
WORLD_MATERIAL_EFFECTS_OUT="evidence/t2-rerun-$RUN_TAG/world-material-effects" \
  bash world-material-effects-check.sh
```

The dependent gates were not re-run here: they are not binary-sensitive in any
way the t2a run did not already cover, and re-running an 11-minute chain to
produce the same 117/221/43 assertions would have added wall clock without adding
a reading.
