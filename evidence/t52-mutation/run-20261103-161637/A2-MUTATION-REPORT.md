# A2 — outline forward gate and mutation suite

Run: 2026-11-03 16:16:37 to 17:56:08 (+08:00), about 1 h 40 min.

Tree: `D:/TF2_Native_Worktrees/t79-integration`, branch `integration/t79-final`,
HEAD `b461b1b05317e781e7afeae31e75558aa0d957e4`. `native/` was clean before the
run and was restored byte-for-byte after every mutation case and at the end.
Only the harness repair in `outline-check.sh` and the previously completed B5
analysis pins were dirty. The pre-existing untracked file `D` was not part of the run.

Host lock: `a2-final2`, held for the build, forward capture, mutation suite, all
restores, and the final rebuild. At the end the lock is FREE and no product process
remains. No full-chain gate was requested or run.

## Verdict

    OUTLINE-CHECK=PASS
    MUTATION-SUITE=ALL-RED-RESTORED
    OUTLINE-EXIT rc=0
    LOCKED-COMMAND-EXIT rc=0

The complete log contains 126 OK lines and no FAIL line: forward assertions plus
the restored-build assertions. All 14 numbered mutation cases produced their
expected red reading. m-meta is deliberately not compilable: the suite reports
MUTATION-INVALID, fails that case, and does not count it as green. There are zero
MUTATION-GREEN verdicts. Text searches such as MUTATION-RED also match explanatory
sentences; the verdict above is the authoritative summary.

## Harness repair exercised by this run

An absolute `OUTLINE_OUT` used to be appended to `$PWD` for the trace path. Cygwin
then interpreted the embedded drive as a directory and produced an empty trace:
`sed: can't read D:/_shared/a2-outline-final/on.trace`. The output directory is now
normalised once, and `absolute_out()` is used when the trace is converted for the
native program. The run's real `off.trace` and `on.trace` were written to
`D:/_shared/a2-outline-final2`; the failed invocation was a harness path failure,
not a product result. This report records the successful rerun, not the aborted run.

## What the mutations demonstrated

| case | deliberate defect | red reading on the mutant | restored reading |
| --- | --- | --- | --- |
| m1 | team regions exchanged | wrong-region pixels 848 | 0 |
| m2 | x-ray depth testing enabled | through-wall pixels 0, depth test bound 1 | 143, 0 |
| m3 | overlay forced on | off-frame inside pixels 1132 | 0 |
| m5 | overlay writes depth | depth-write state 1 | 0 |
| m6 | capture depends on playback speed | speed=1 and speed=8 frames differ | identical |
| m7 | class-model fallback removed | silhouettes 0, noMesh 20 | 15, 5 |
| m8 | bare path used as the upload key | silhouettes drawn 0 | 6 |
| m9 | bind mesh preferred over world mesh | probe self-test failures 5 | 0 |
| m4 | overlay moved after the view model | pass order 0, ordinal 8 | 1, 7 |
| m11 | readback uses the wrong instance | identity mismatches 449 | 0 |
| m12 | GPU draw shifted without moving its box | outside difference 1170 | 40 |
| m13 | drawing expands beyond its declaration | outside difference 7065 | 40 |
| m14 | published expansion does not match the draw | refused 11, accepted 0 | 0, 1 |
| m10 | colour frame offered as depth | non-grey pixels 98.71% | 0 |

## Restored artifact

    tf2_demo_native.exe    06a715f41e9c6b891ed70e8937390017f08851dd3404c64199d5133b746854f7
    entity_overlay_probe.exe ae7ac2cc195fdb1562bd2972bb43b6e08ec5bd4c634a3a8c820b40c51ef8062d

These hashes are from the final restored build. The mutation suite also compares
every patched source against its saved copy before rebuilding. No source mutation
is included in the evidence commit.

## Files and scope

- `outline-mutation.log`: complete output, including all builds, mutations, restores,
  forward assertions, and final verdicts.
- `runner.sh`: the wrapper that held the log and invoked the gate inside the lock.
- `run-identity.txt`: run HEAD, the dirty-file classification, lock state, and final
  restored executable hashes.
- The complete frame/trace directory remains at `D:/_shared/a2-outline-final2`
  (about 86 MB); it is not duplicated into the repository.

This establishes that the outline gate and every assertion needed by its mutation
suite can discriminate the injected defects on this tree, and that the forward gate
passes on the restored source. It does not claim a full-chain run or erase any
previously recorded NOT VERIFIED product limitation.
