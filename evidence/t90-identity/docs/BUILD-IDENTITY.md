# T90: what a binary is allowed to say about the source it was built from

Status: implemented in `native/cmake/gen-build-stamp.cmake` + `native/cmake/tf2-version.cmake`,
read by `native/src/install_lifecycle.cpp` and written into install manifests by
`native/src/install_manifest.cpp`. Gate: `build-identity-check.sh` (sections 1, 1b, 1c, 2, 3,
4, 4b; mutations M1-M3; reproducible mode `--reproducible`). This note records the reasoning
and the measured boundary; the raw readings and build logs sit beside it in
`evidence/t90-identity/`.

## The defect, measured on the frozen candidate

`tf2_demo_native.exe` from `t79-integration` (sha256 `a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9`,
2763264 bytes, mtime 2026-11-06 20:20) answered:

    package=tf2_demo_native version=0.1.0 versionCode=100
    buildUtc=2026-11-02T16:22:30Z sourceDateEpoch=0 sourceTree=12042361ffd1

while the tree it was linked from was at `e15577b`. `git rev-list --count 12042361ffd1..e15577b`
is **41**, so the binary named a source tree 41 commits behind its own sources, and a build
time four days older than its own file mtime. `sourceDateEpoch=0` was not a reading at all:
it was a literal `"0"` in a cache variable, so the field answered a constant on every build
ever made. And nothing in the output could say the worktree had uncommitted changes.

## Why configure-time evaluation is wrong

The three inputs — HEAD, the worktree's dirty state, the wall clock — are properties of the
moment a binary is *linked*, not of the moment a build tree is *configured*. Configure runs
once; `cmake --build` (which is what `build-target.sh`, every `*-check.sh` and every step of
`verify-all.sh` invoke) never runs it again. Any value captured in `tf2-version.cmake` is
therefore frozen at the first configure and stays plausible-looking forever while the objects
around it are recompiled from newer sources. That is worse than a missing field: a stale
commit reads exactly like a current one.

The fix is not "re-run configure more often" — it is to move the measurement into the build:
`gen-build-stamp.cmake` runs as the `tf2_build_stamp` target on **every** build, before the
targets that compile `install_lifecycle.cpp`, and writes
`native/generated/tf2_build_stamp.cpp`. That file is a translation unit rather than a
generated header because NMAKE has **no header dependencies** in this tree (every `.obj.d` is
empty; `build-target.sh` carries a header-stamp guard precisely because of it) — a rewritten
header would leave its consumers stale and reproduce the defect in a new place.

Two consequences worth stating explicitly:

- The stamp is rewritten **only when the identity changes** (compared against
  `generated/tf2_build_stamp.identity`, written with `cmake -E copy_if_different`). A no-op
  build therefore neither recompiles nor relinks, which the gate's section 2/5 asserts by
  exe sha256 and stamp sha256. `buildUtc` consequently means "when this identity was first
  captured", not "when nmake last ran" — a re-link of identical sources keeps its timestamp.
- A fresh clone has no generated file, and `add_executable()` requires its sources to exist at
  configure time. Configure writes a **placeholder** whose `sourceTree` is the literal
  `configure-placeholder` and whose `dirty` is `-1`. If the generator ever stopped running
  first, the word would appear in `--version` and `build-identity-check.sh` fails on it —
  a loud failure instead of a silent stale commit.

## How "dirty" is expressed

`--version` gained a third line (the first two keep their shape because
`install-lifecycle-check.sh` parses them):

    dirty=<1|0|-1> dirtyFiles=<n> dirtyTracked=<n> dirtyUntracked=<n>

- `dirty` is `1` when `git status --porcelain -uall` produced at least one record, `0` when it
  produced none, and **`-1` when git could not answer**. Unmeasurable is never rounded to
  clean; that asymmetry is the whole point of the field.
- The counts are the porcelain record total and its tracked/untracked split (`-uall` counts an
  untracked directory per file). They are there so the claim can be checked against the tree
  rather than believed: the gate's section 3/5 makes the tree dirty by a known number of
  records and asserts the numbers move accordingly, then section 4/5 restores byte-exactly and
  asserts they return.
- Install manifests gained `sourceDirty` and `sourceDirtyFiles` (appended after `sourceTree`,
  with a `parse()` branch each). A manifest written before T90 lacks both keys and still
  parses; a package can now say "built from e15577b with 3 uncommitted files" instead of
  naming a commit it does not match.

Git being unavailable is a **reading** (`sourceTree=unknown`, `sourceDateEpoch=0`, `dirty=-1`),
not a build failure — same fallback the old code had, so a broken git truncates the link at
the same place rather than at a new one. A stamp that cannot be **written** is fatal: linking
a binary whose identity was invented is the defect being removed.

## Why an exe sha256 is not an identity

On 2026-11-07 the same source tree was built three times and produced three different exe
sha256 values (74657339...f1cf5, f47341de...99c68, 1bd58f4f...13ca). The variable sits in the
PE header: the COFF TimeDateStamp at `e_lfanew + 8` read 0x6aeee287 on one build and
0x6aeee295 on another fourteen seconds later. The link stamps the wall clock into the file, so
an exe sha256 names one **link**, not one **tree**; two people quoting two shas for one commit
are both right.

Both the compiler and the linker are asked for `/Brepro` in `native/cmake/tf2-settings.cmake`
(`add_compile_options(/Brepro)` and `add_link_options(/Brepro)`), because the NMake generator
adds neither on its own. Whether the switch actually holds is not taken on faith: the gate's
`--reproducible` mode runs two **clean full builds** (23 targets through `build-cmake.sh` each
time) and asserts byte equality of the two exes, equality of the COFF TimeDateStamp, equality
of the size and equality of the `--version` bytes. It prints
`BUILD-REPRODUCIBLE=PASS builds=2 sha=<sha256>` only when all of that held; if the shas differ
it names the residue (both PE reports and the first ten differing bytes) rather than loosening
the bound.

The identity this gate records is therefore a triple, never a bare sha:

    exe sha256 + git HEAD (40 hex) + sha256 of `git ls-tree -r HEAD -- native/`

Section 4b/5 writes that triple to `source-identity.txt`, asserts the recorded values equal
the live ones, and asserts that the version's `sourceTree` is the short form of the same HEAD.
A bare exe sha is not an identity because it cannot be reproduced and does not name a tree; a
bare HEAD is not an identity because it does not name the bytes that were linked.

## The measurements this note rests on

Raw files sit in `evidence/t90-identity/`; every number below is a line in one of them.

- The frozen candidate answered (`before/before-version.txt`):

      package=tf2_demo_native version=0.1.0 versionCode=100
      buildUtc=2026-11-02T16:22:30Z sourceDateEpoch=0 sourceTree=12042361ffd1

  for an exe (sha256 `a3c93d57...`) whose tree was at `e15577b`.
- The same product built from this branch answered (`after/after-version-final-tree.txt`):

      package=tf2_demo_native version=0.1.0 versionCode=100
      buildUtc=2026-11-07T09:06:42Z sourceDateEpoch=1794023209 sourceTree=e15577b4f055
      dirty=1 dirtyFiles=15 dirtyTracked=11 dirtyUntracked=4

  `sourceDateEpoch` is now the committer epoch of the HEAD the exe was linked from
  (`git log -1 --format=%ct` = 1794023209), not a cached literal; `sourceTree` is that
  HEAD; `dirty` names 15 porcelain records that `git status --porcelain -uall` produced at
  the same moment.
- Two clean full builds (`--reproducible`, `reproducible/reproducible.txt`) produced the same
  file twice:

      build 1: sha256=040eb11caf30232ae8d5d3da27dbfc4cfadaff0bbb3c1e5b924345ae2b17d533 size=2772480 pe_timedate=1970978447
      build 2: sha256=040eb11caf30232ae8d5d3da27dbfc4cfadaff0bbb3c1e5b924345ae2b17d533 size=2772480 pe_timedate=1970978447
      BUILD-REPRODUCIBLE=PASS builds=2 sha=040eb11caf30232ae8d5d3da27dbfc4cfadaff0bbb3c1e5b924345ae2b17d533

  The COFF TimeDateStamp is 1970978447 in both. Before `/Brepro` it was the wall clock
  (1794040907 on the 16:24 build), which is exactly why three builds of one tree had three
  shas. Note what this does **not** say: the exe sha also covers the stamp translation unit,
  so a build made while the tree had a different dirty count is a different file
  (`380b81ab...` at 09:02 with 15 records, before that count moved). Reproducibility is a
  property of *this tree plus this identity*, not of "the source" in the abstract.
- Gate tokens, both from the same window (`logs/window-final.log`):

      BUILD-IDENTITY=PASS assertions=115 identity=exe-sha+HEAD+native-tree-sha head=e15577b4f055
      BUILD-IDENTITY-MUTATION=PASS (3 mutations caught, sources restored, 146 assertions)

## What this does not prove

- It is not a content hash: the tree is not hashed, only HEAD and the porcelain counts. Two
  dirty trees with the same number of records are indistinguishable.
- It is not a cross-machine reproducible build: `--reproducible` proves two clean full
  builds on **this** machine and toolchain are byte-identical; another compiler, SDK, source
  path or machine is outside the measurement.
- It says nothing about whether the binary *runs* correctly — only that its identity reading
  corresponds to the tree it was linked from.
- The dirty counts describe the tree at the moment the stamp was written, i.e. the build that
  captured this identity; a later edit does not change an already-linked binary (that is the
  correct behaviour, and section 2/5 pins it).

## Gate (build-identity-check.sh)

Each new assertion is preceded by an existence assertion, because a value assertion on an
empty reading passes vacuously — the trap the whole wave is about (and the gate's own first
forward run failed for the mirror-image reason: an end anchor written as `[$]`, a bracket
expression matching a literal dollar, counted zero matches on correct output).

Modes: `--forward` (incremental builds, dirty-tree cycles, the strict-args surface,
identity triple),
`--mutation` (the forward suite first, then the three mutants), `--reproducible` (two clean
full builds compared byte for byte). The named assertions state what they assert;
`BUILD-IDENTITY=PASS (<n> assertions, identity=exe-sha+HEAD+native-tree-sha, head <short>)`
and `BUILD-REPRODUCIBLE=PASS builds=2 sha=<sha256>` are only printed when all of them held.
