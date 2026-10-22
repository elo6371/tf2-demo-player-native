# T54 -- the 12 constants `install-lifecycle-check.sh` pins that T54 invalidated

T54 adds one file to the installer payload (`WebView2Loader.dll`, needed for the
logs.tf panel) and two files to the package root (`third_party/webview2/LICENSE.txt`
and `NOTICE.txt`, required by the SDK's BSD-3-Clause clause 2 for a binary
redistribution). Both change numbers another gate pins. This file records every
number that moved, so the next person can see which ones were **observed** and
which were **derived by arithmetic**.

**Nothing here was observed in a run.** `install-lifecycle-check.sh` has not been
run on this branch: no install, upgrade, downgrade, migration or uninstall was
executed. Every value below is derived from "the payload holds one more file" and
"the package root holds two more files", and the payload composition itself is
read from the install rules in `native/CMakeLists.txt`, not counted in a tree.

That distinction is the point of this file: a constant that is wrong in the same
direction as a measurement *looks* verified, and is not.

| line | old -> new | source | basis |
|---|---|---|---|
| 204 | 3 -> 4 | derived | payload file count: 4 staged payload files (product, smoke probe, .ps1, loader) |
| 295 | 4 -> 5 | derived | install owns 4 payload files + the VERSION.txt it writes |
| 307 | 5 -> 8 | derived | 4 payload + VERSION.txt + manifest + the 2 licence files that ship in the package (see the causal sentence in the gate's own label) |
| 314 | 4 -> 5 | derived | the manifest records every install record; one more record, one more line |
| 332 | 4 -> 5 | derived | a fresh install owns 4 payload files + VERSION.txt |
| 334 | 4 -> 5 | derived | in an empty directory every record is new |
| 339 | 5 -> 6 | derived | fresh directory = 4 payload + VERSION.txt + manifest |
| 350 | 4 -> 5 | derived | status reports every manifest record as present |
| 542 | 2 -> 3 | derived | the migrated uninstall removes one more record (its running image stays deferred, and the file the upgrade replaced stays a restore) |
| 622 | 4 -> 5 | derived | the install owns five records: four payload files and the version file |
| 631 | 2 -> 3 | derived | two of five records were deleted by hand, so three stay present |
| 647 | 1 -> 2 | derived | the uninstall removes the records still on disk; the loader is one of them now |

307 is the one number that moved twice: first 5 -> 6 with the payload, then 6 -> 8
with the two licence files. It is on the list once because it is one constant.

## Why 307 is the dangerous one

307's subject is **the install target**, not the payload: it counts everything in a
directory that a `cmake --install` produced. If someone reverts the licence files,
or moves them into the payload, 307 can be wrong in two ways at once and the
reading will not say which happened. That is why the gate's label spells the sum
out ("4 payload + VERSION.txt + manifest + 2 licence files") instead of pinning a
number with no cause attached.

## The cheapest way to turn all twelve into measurements

```
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> bash -c \
  'cd /d/TF2_Native_Worktrees/t54-logstf && bash install-lifecycle-check.sh'
```

Until that runs and is green, the correct status of these twelve is **NOT
VERIFIED (constants aligned, semantics unobserved)** -- and the T37 install
semantics are a cross-task gap, not a T54 claim.
