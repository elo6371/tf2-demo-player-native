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

## A second gate owns the same question, and it is runnable

`install-lifecycle-check.sh` is not the only gate that pins the package list.
`install-readiness-check.sh` (T32/T37) pins it too, in section "3/13 the package",
and T54 made those constants stale in three places:

| line | old -> new | observed or derived | basis |
|------|------------|---------------------|-------|
| 255 | 7 names -> 11 names | **observed** | evidence/logstf/readiness.log (T54 lock hold, 2026-10-22) |
| 262 | `package file count` 7 -> 11 | **observed** | same run, same log |
| 264 | payload entries 3 -> 4, label rewritten | **observed** | same run, same log |

**Observed, not planned.** The witness is `evidence/logstf/readiness.log`, written
during lock hold `t54-logstf` on 2026-10-22 (run started 22:16:53, gate at
22:25:41). Its own verdict lines:

```
  OK   package file count = 11
  OK   the payload holds the three files it lays down at the top level plus
       WebView2Loader.dll (4 = 3 + loader; the licence files are not payload) = 4
  cases=13 checks=145 mutations=0
  INSTALL-READINESS=PASS
```

The staged file list that section checks against is
`evidence/logstf/install-preview-files.txt`, printed by `cmake --install` into a
scratch prefix and listing, in the order the install writes them:

```
.uninstall/payload/WebView2Loader.dll
.uninstall/payload/install_smoke_probe.ps1
.uninstall/payload/native_install_smoke_probe.exe
.uninstall/payload/tf2_demo_native.exe
VERSION.txt
WebView2Loader.dll
install_smoke_probe.ps1
native_install_smoke_probe.exe
tf2_demo_native.exe
third_party/webview2/LICENSE.txt
third_party/webview2/NOTICE.txt
```

**A note on how this row was nearly wrong**: for two rounds the file list was
reported missing, and the gate read as "install preview not completed", because the
runner's `$LOGDIR` was a relative path and the install step `cd`s into the staged
tree before writing its list. The install had succeeded every time -- the licences
were on disk while the gate said they were not. The reading was about the log's
path, not about the install (see evidence/logstf/gate-design.md). If this document
is read without `evidence/logstf/readiness.log` beside it, the three rows revert to
a plan, not an observation.

The count is 11, not 7 + 2: the package grows by four. The top level gains
`WebView2Loader.dll` (native/CMakeLists.txt:665-667) as well as the two licence
files, and the payload gains the loader too -- four payload entries are the three
files the installer lays down again *plus* the loader. A reader who assumes the
licences are the only additions will get 9 and be wrong by two; that is why the
label on the payload assertion now states the composition instead of counting.

**These three are observations, not derivations.** Unlike the twelve above, this
gate needs no WebView2 runtime and no product launch to answer its package
question, so it was executed -- and an executable expectation that has never been
executed is the least trustworthy kind of all.

## The cheapest way to turn all twelve into measurements

```
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> bash -c \
  'cd /d/TF2_Native_Worktrees/t54-logstf && bash install-lifecycle-check.sh'
```

Until that runs and is green, the correct status of these twelve is **NOT
VERIFIED (constants aligned, semantics unobserved)** -- and the T37 install
semantics are a cross-task gap, not a T54 claim.
