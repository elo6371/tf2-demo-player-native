# T2 rerun 20261014-193500-t2a

The rerun demanded by `TASK-T2-RESUME.md`. This is the **primary** run: it is
the one the forward gates, the four product mutations and the three dependent
gates were taken from.

| file | what it is |
|---|---|
| `exe-identity.txt` | the sha256 of the binary this run measured, and its provenance |
| `build.log` | `bash build-target.sh tf2_demo_native world_material_probe viewmodel_material_probe` (rc=0). It compiled and linked nothing: see `exe-identity.txt`. |
| `gate-forward.log` | `world-material-effects-check.sh` forward pass. `WORLD-MATERIAL-EFFECTS=PASS`, 101 OK, 0 FAIL |
| `gate-world-material.log` | `world-material-check.sh` redirected here. `WORLD-MATERIAL=PASS`, 117 OK |
| `gate-viewmodel-material.log` | `viewmodel-material-check.sh` redirected here. `VIEWMODEL-MATERIAL=PASS`, 221 OK |
| `gate-verify-fast.log` | `verify-fast.sh` redirected here. `VERIFY-FAST=PASS (assertions_ok=43)` |
| `clean-build.log` | the `build-cmake.sh` clean rebuild run afterwards, for the reproducibility finding |
| `binaries/` | the binaries behind the finding that the build is not byte-reproducible (see `binaries/README.txt`) |
| `world-material-effects/` | the effects gate's own evidence files |
| `world-material/` | `world-material-check.sh`'s evidence files |
| `viewmodel-material/` | `viewmodel-material-check.sh`'s evidence file |
| `fast/` | `verify-fast.sh`'s evidence files |
| `mutation/` | the `--mutation` pass: the four product mutations' readings, the gate's own mutation log, and the fixture runs |
| `../t2-rerun-20261014-215000-t2b/` | the same forward pass against a **different** binary, which reproduced this run assertion for assertion |

Commands, verbatim:

```
RUN_TAG=20261014-193500-t2a
mkdir -p .scratch/t2-rerun-$RUN_TAG
bash build-target.sh tf2_demo_native world_material_probe viewmodel_material_probe
WORLD_MATERIAL_EFFECTS_SCRATCH=".scratch/t2-rerun-$RUN_TAG" \
WORLD_MATERIAL_EFFECTS_OUT="evidence/t2-rerun-$RUN_TAG/world-material-effects" \
  bash world-material-effects-check.sh
WORLD_MATERIAL_SCRATCH=".scratch/t2-rerun-$RUN_TAG/world-material" \
WORLD_MATERIAL_OUT="evidence/t2-rerun-$RUN_TAG/world-material" \
  bash world-material-check.sh
VIEWMODEL_MATERIAL_OUT="evidence/t2-rerun-$RUN_TAG/viewmodel-material" \
VIEWMODEL_MATERIAL_TRACE_DIR=".scratch/t2-rerun-$RUN_TAG/viewmodel-material-trace" \
  bash viewmodel-material-check.sh
VERIFY_FAST_OUT="evidence/t2-rerun-$RUN_TAG/fast" bash verify-fast.sh
WORLD_MATERIAL_EFFECTS_SCRATCH=".scratch/t2-rerun-$RUN_TAG-mut" \
WORLD_MATERIAL_EFFECTS_OUT="evidence/t2-rerun-$RUN_TAG/mutation" \
  bash world-material-effects-check.sh --mutation
```

`verify-fast.sh` is pointed at this run with `VERIFY_FAST_OUT` rather than run
bare, so that its output lands under this tag instead of dirtying
`evidence/fast/` (which is gitignored and superseded, see
`evidence/fast/00-SUPERSEDED-2026-10-14.md`).
