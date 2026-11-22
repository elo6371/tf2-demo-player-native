# Builds cited by the evidence in this repository

The six cards and the evidence directories under `evidence/` quote md5s of BMPs and of
probe output. An md5 is a statement about one executable's bytes, so every pin inherits
the build it was taken from. This file is the index of those builds: which executable,
which sha256, and **how that sha256 was observed** -- because "a report says so" and "the
gate hashed the file it launched" are not the same grade of evidence.

Provenance classes, strongest first:

| class | what it means |
|---|---|
| `sidecar` | The build wrote its own identity at compile time into `native/build-nmake/tf2_demo_native.identity` (target `tf2_build_stamp` / `tf2_identity`, `native/cmake/gen-build-stamp.cmake` + `native/cmake/write-identity-sidecar.cmake`), and a committed evidence file quotes that sidecar. The sidecar also records `contentBinding=recompiled`, i.e. the content hash was compiled into these bytes rather than carried forward. |
| `gate-printed` | A gate launched the executable and printed `sha256 product=<hash>` from the file it had just run, so the hash is a measurement of the bytes under test, taken by the tool doing the testing. |
| `run-printed` | The run itself printed `exe_sha256=` before and after the work, byte-identical both times, so the binary provably did not change mid-run. |
| `header-quoted` | The hash appears only in the header a driver wrote. It is a record of what was run, not a re-checkable measurement: nothing in the repository recomputes it. |
| `unrecorded` | The value appears in no committed file. |

## The builds

| build | what it is | exe sha256 | sourceContent (git-clean-v1) | provenance | what inherits from it |
|---|---|---|---|---|---|
| task-41 cosmetics | `native/build-nmake/tf2_demo_native.exe`, the branch build the task-41 card was written on | `3a3cb9f1ef348fc0fab27db6784ed6c36777f1ce21abef8d21e0a072b54f4b89` | `a73f5ff84d724a053c0658cfd2556b8dbce6dfa259d33a7dda1d6d66d0c6078b` | `sidecar` -- `evidence/cosmetics/identity.txt:7-10` and `ledger.txt:13-15` quote the sidecar fields, including `contentMethod git-clean-v1` | every md5 in `evidence/cosmetics/` (the four BMP pins, the ledger `4f0a047a…` line, the crop proof) |
| task-41 baseline | `.scratch/pin/tf2_demo_native.baseline.exe`, a copy kept from the merge base `53d6e5de` before any task-41 code existed | `8e013c47d612f35c4454d020dff361d03c113f4c4b066bdaf26e2160f2657e5c` | not recorded | `header-quoted` -- `evidence/cosmetics/identity.txt:4-6`, `ledger.txt:12` | the `baseline` column of `identity.txt` (`2ddade35…`) and `ledger.txt` (`4f0a047a…`) -- the "the off path did not move" comparison |
| task-36 product | the product exe the three task-36 gates ran | `a052de0875f38a38c5119ded0d17f29619997857fd481960791d46ccb29fd9b1` | not recorded (`gates/README.md:5-7` names the tree `cfd01bd1` = `e0e80fa2` + doc/comment edits, without a content hash) | `gate-printed` -- `evidence/t105-model-lighting/gates/class-pixels.log:27` (`sha256 product=a052de08…`), repeated at `gates/README.md:7`, `argument-refusal.log:8`, `REPORT.md:97` | `evidence/t105-model-lighting/pins.txt` (four md5s), `crops.txt`, `floor-gate.txt`, `terminator.txt`, `normal-dot-light-bins.txt`, `trace-*.txt`, the three gate logs |
| merged head probe | `native/build-nmake/world_material_probe.exe` on the merged head `93bb23d4328628cc16b810b0ac6aa105110d6966` | `9c4a98be2414412ac9d57885d75b3f2528b2698d34a9c86af7d65b3acd33d535` | not recorded (the probe is not one of the two executables the stamp targets) | `run-printed` -- `evidence/world-material-selftest.txt:2` and `:19`, identical before and after, with `tree_head=93bb23d4…` at `:3` | `evidence/world-material-selftest.txt` only. It asserts the seven named self-tests pass; it does **not** re-derive any world-material gate pin (see below) |
| atlas tail readings | `world_material_probe.exe` from the `task/atlas38-lightmap` worktree, plus one probe built from base `390a7c76` for the HDR-only before-state | not recorded | not recorded | `header-quoted` / none: `evidence/atlas38-lightmap/*.txt` records readings, not the binary that produced them | `evidence/atlas38-lightmap/` (six-map readings, border A/B, HDR-only before/after, drops). Re-derivable with `tools/collect.sh` from any tree that has the change |

Two hashes that are easy to confuse: `a73f5ff8…` is a **source content** hash
(`sourceContent=`, `contentMethod=git-clean-v1`), not an executable hash; `3a3cb9f1…` is
the executable built from those sources. The `git-clean-v1` method hashes the tracked
inputs the two executables compile from -- the git index entries of `native/src`,
`native/include`, `native/third_party`, `native/cmake` plus `native/CMakeLists.txt` -- so it
is equal on any tree with the same sources. `native/generated` and `native/tools` are not
inputs of either executable, and a build that needs an untracked compile input is refused
(`TF2_IDENTITY_ALLOW_UNTRACKED=1` downgrades it to `git-untracked-v1`, which no gate accepts
as pinnable).

## Values that appear in no committed file

`5132fb76f7b8b63e9cf85d74e61d7fc14598e48917cd1298e40cbf5452f8a4f3` is quoted in the
task-41 brief as what this tree rebuilds to. It occurs nowhere in the committed tree --
not in a log, not in a sidecar, not in a doc. Recording it here keeps the claim traceable
without adopting it: reproducing it needs a build of this head on a machine with the same
toolchain, and the build's own sidecar (`sourceContent=`, `exeSha256=`) would then be the
observation, not the brief.

`8e013c47…` (the task-41 baseline) is in the same position from the other side: it is
quoted in two evidence headers, but the executable it names lives at
`.scratch/pin/tf2_demo_native.baseline.exe`, which is deliberately not committed, so no
reader can re-hash it. Reproducing it needs `53d6e5de` checked out and built; that build
would produce its own `exeSha256`, which is not required to equal `8e013c47…` unless the
toolchain matches exactly.

## The warning transcript is not from these trees

`evidence/t105-model-lighting/gates/warn-check.log` is the only /W4 transcript in the
repository, and it is the only evidence file keyed to a **different** tree than the pins
it sits beside: it was produced on `cfd01bd1` (`e0e80fa2` plus documentation and comment
edits, 142 translation units, exe `a052de08…`), not on any tree a reader of the six cards
is likely to be holding. It answers "does this change introduce a warning" for the task-36
tree; it does not answer it for the merged head. The merged head's warning state is
recorded by the final /W4 build of that head -- see the reserved row below.

## Reserved: final /W4 build of the merged head

This row is filled in when the final full build of the merged head runs. It is left
visible and empty rather than dropped, so that a reader can tell "not yet recorded" from
"recorded and clean".

| build | what it is | build rc | warning count | exe sha256 | sourceContent | provenance |
|---|---|---|---|---|---|---|
| final /W4 build of the merged head | `bash build-cmake.sh` on `93bb23d4` (or the merge that supersedes it) | _not yet recorded_ | _not yet recorded_ | _not yet recorded_ | _not yet recorded_ | will be `sidecar` + build-log line |

## What a pin here does and does not assert

A pin asserts that one command, run on one executable, produced one md5. It does not
assert that the same command on a different build produces the same md5 -- several rows in
the cards change precisely because the build changed (the `--lighting leaf` arm's 0.225%
against a stale predecessor exe in `docs/LEAF-AMBIENT-GPU-2026-11-22.md` section 8 is the
worked example). When a card says a number "is the same on both sides", both sides must be
builds listed above.
