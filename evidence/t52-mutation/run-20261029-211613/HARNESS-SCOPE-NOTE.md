# Harness scope note — which outline-check.sh produced the mutation log

Written 2026-10-30 at Codex's ruling (see report-009 Q3a, answer "(ii)").

## What ran

`run-20261029-211613` (the run this directory records) executed
`bash outline-check.sh --mutation` on the tree at `c1879eb` `+` two uncommitted
repairs to the harness itself:

1. the m6 mutation anchor re-indented to match the current `native/src/main.cpp:9402`
   (12 -> 10 spaces; the mutation target and its assertion were not changed), and
2. the first version of the stale-binary guard: it compared each of the two binaries
   against **every** `*.cpp`/`*.h` under `native/src`, `native/include` and
   `native/tools`.

## What is committed

`39a19c9 "fix: refuse to measure an outline build that is older than its sources"`
contains the same m6 fix but the **narrowed** guard: each target is compared only
against its own translation units, read from
`native/build-nmake/CMakeFiles/<target>.dir/DependInfo.cmake`, plus the
`native/build-nmake/.headers-stamp` rule that `build-target.sh:124-133` already uses.

The narrowing exists because the wide version flagged the binaries as stale for a
source they do not compile: touching `native/tools/projectile_track_probe.cpp` (not a
dependency of either gate target) reported both binaries stale, and the rebuild the
message suggested did not clear it.

## Why the run's verdict still stands

- Every forward assertion and all 16 mutation cases are byte-identical between the
  harness that ran and the harness committed; the only difference is the scope of the
  precondition guard.
- That guard passed in the run (the binaries were freshly built inside the run by
  `build-target.sh`, and `post-build.txt` records no source newer than either exe).
- The narrowed guard was verified in both directions before it was committed
  (`_shared/t52-run/stale-scope-test.sh`): touching `projectile_track_probe.cpp` keeps
  both targets green; touching `entity_overlay.cpp` turns both red (the product
  target compiles it too); touching `demo_header.h` turns both red through the header
  stamp; an untouched tree is green and `--skip-live` exits 0 with
  `OUTLINE-CHECK=PARTIAL`.

## What this note does not change

The original logs in this directory are untouched and are the record of the run.
No part of the suite was re-executed to produce this note. If the suite is ever run
again, it will be the narrowed harness that does the measuring.
