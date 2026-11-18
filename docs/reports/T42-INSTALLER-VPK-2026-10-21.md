# T42 -- VPK mutation and installer lifecycle closure

Worktree `D:\TF2_Native_Worktrees\t42-installer-vpk`, branch `task/t42-installer-vpk`
from `34e1714`. Measurement date 2026-10-21. The product is the Windows native
demo at `native/`; the two gates are `install-lifecycle-check.sh` (new, 15 forward
sections and 3 mutations) and `install-readiness-check.sh` (T32/T37, 13 forward
sections and a 4-case mutation phase with 3 mutations).

## Result

- T37's open mutation **M2 is caught**. It was not caught before because the
  fixture never ran the mutated binary: T37's M2 fixture copied its installed
  payload from the forward run's stage, which is the REFERENCE build. That is
  MUTATION-NOT-LINKED in the fixture, not an unreachable product path. The gate
  now stages every mutation fixture from the mutated build, asserts the sha256 of
  the binary that will execute, and exits **90 / MUTATION-NOT-LINKED** naming the
  mismatch if it is the reference binary. With a linked fixture, the invariant
  that was already reachable -- "a file the user added before the install must
  survive uninstall" (a `preserved` manifest record) -- catches M2.
- The installer lifecycle is pinned by 15 forward sections: clean install,
  reinstall, uninstall, upgrade and downgrade, migration, first launch, restart,
  non-admin refusal, missing runtime file, and the five VPK states through both
  `vpk_query` and the installed product.
- VPK refusals carry five stable, distinct attributions that the probe and the
  product agree on: ABSENT, UNREADABLE, UNSUPPORTED, TRUNCATED, OPEN_FAILED.
- Gate readings (this branch, this worktree):
  - `install-lifecycle-check.sh --forward`: **cases=15 checks=213, PASS**
  - `install-lifecycle-check.sh --mutation`: **cases=3 checks=25 mutations=3,
    PASS** (all three caught, every source restored byte-identical)
  - `install-readiness-check.sh --forward`: **cases=13 checks=107, PASS**;
    `--mutation`: **cases=4 checks=40 mutations=3, PASS**

## M2: why T37 measured a miss, and what makes it a catch

T37 recorded M2 (uninstall no longer skips `preserved`/`unchanged` records) as
uncaught. The question was whether the changed behaviour reached any assertion at
all. The measurement (evidence `t42-m2-rootcause.txt`, kept from the exact
fixtures the diagnosis ran):

| step | fixture | installed uninstaller sha256 | uninstall reading | user file |
|---|---|---|---|---|
| 1 | T37-shaped: payload copied from the forward stage | `49239329...` (reference) | `INCOMPLETE removed=3 preserved=1 deferredRunningImage=1` | present |
| 2 | same shape, payload staged from the MUTATED build | `caab6897...` (mutated) | `INCOMPLETE removed=4 preserved=0 deferredRunningImage=1` | **absent** |

Step 1 ran the reference binary, so its correct behaviour says nothing about the
mutation. Step 2 ran the mutated binary and the existing invariant failed exactly
as the mutation predicts. Root cause: `fresh_package`/`stage_package` in T37's gate
reused the forward run's `$OUT/stage` payload. The mutated build existed, but the
fixture installed the old copy, and an uninstall launched from the installed
directory therefore ran the reference uninstaller.

The gate now makes that class impossible instead of relying on care:

1. **The forward run names its reference.** Section 1 prints the build's exe
   sha256 and writes `$OUT/reference-exe.sha256`; the mutation phase refuses to
   start without it.
2. **A rebuild must change the exe.** `rebuild()` records the exe before and after
   and returns **90** if a supposedly mutated rebuild produced a byte-identical
   binary (a silently failed relink), printing the log tail.
3. **A fixture must run the mutated binary.** `mutated_binary_identity()` hashes
   the exe a fixture will execute (the staged package, the installed uninstaller)
   and returns **90 / MUTATION-NOT-LINKED** if it is the reference sha, printing
   both hashes. All three mutations call it before they measure anything.
4. **The M2 fixture is fresh.** It stages the payload from the mutated build in a
   new directory after the rebuild, puts the user's file in place *before* the
   install (a file added after an install is not in the manifest, so no change to
   the state-word filter could delete it), installs with the mutated package, and
   only then uninstalls.

Restore proof: every mutation copies the source aside first, restores it with
`cp`, checks `cmp -s`, rebuilds, and the verdict re-checks all three sources.
The mutation run ends with a clean `git status` and the same tree (`2c6170b`).
This run's identities: reference `8c64712e...`, M1 mutated `3a9c5f33...`, M2
mutated `e7b4a8d2...`, M3 mutated `58112314...`; duplicate backup builds differ
from each other because the PE image is not byte-reproducible, which is why the
restore proof is the source `cmp`, not an exe hash.

## Installer lifecycle (15 sections, forward)

Each case gets its own directory under `$OUT/clean`; the section name, the
readings it pins and the shape it refuses are listed here. The full assertion text
is `evidence/t37-install/t42-lifecycle-forward.log`.

| # | population | pinned readings (this run) |
|---|---|---|
| 1 | the staged package | `cmake --install` rc 0, product+payload+VERSION.txt, staged exe sha == build sha |
| 2 | the package's own refusal modes | `no_payload`, `empty_payload` named; no directory is created |
| 2b | the seven fixture roots | 7 roots, each satisfies the product's `AssetRoot::valid()` |
| 3 | installed file records | 4 records (3 payload + VERSION.txt), every file hashed and sized |
| 4 | the no-op install | second install over itself is all-unchanged, no backup, exit 0 |
| 4b | uninstall restores | the pre-existing file is restored, the package's file removed |
| 5 | uninstall of a real install | `INCOMPLETE removed=2 restored=1 preserved=0 deferredRunningImage=1`, exit 3 |
| 6 | modified user data | an edited installed file is left, counted `modified=1`, exit 3 |
| 7 | uninstall without a manifest | `uninstall=FAIL reason=no_manifest`, exit 2, nothing deleted |
| 8 | VPK: absent and corrupt | see the matrix below |
| 9 | upgrade / downgrade | v100 -> v200 by `packageVersionCode`: `mode=upgrade`, changed=1, backups=1, manifest 0.2.0/200, disk marker present; v100 package over the v200 install: exit 2, exact line `install=FAIL reason=downgrade installedVersionCode=200 packageVersionCode=100`, no `install=START`, manifest and payload untouched |
| 10 | migration | the moved directory verifies (`status=VERIFIED`), still names the old install path in its manifest, uninstalls with `removed=2 restored=1 preserved=0 deferredRunningImage=1`, restores the pre-upgrade bytes, keeps the user's file, never counts a record the manifest does not hold |
| 11 | first launch | install then boot the installed product with a fixture root: exit 0, title captured, `root valid=1`, `connected_root` == the given fixture root, archive opened=1 refused=0, map read `store=vpk`, settings written with `tfRoot=`; the install directory itself holds no settings file |
| 12 | restart | second process with no `--tf-root`: `requested=-`, `connected_root` == the saved root, archive opened=1, and no second settings file is needed |
| 13 | non-admin shape | write-denied directory: exit 2, `install=FAIL reason=cannot_copy`, no PASS, inventory byte-identical, user file present, no manifest |
| 14 | missing runtime file | delete `native_install_smoke_probe.exe` + `VERSION.txt`: status exit 0 `status=DEGRADED present=2 missing=2 modified=0`; `--version` exit 0 and falls back to the compiled version; uninstall exit 3 `removed=1 alreadyMissing=2 deferredRunningImage=1` |
| 15 | VPK matrix | both readers, five states, and two named limitations; see below |

Section 13 has a measured subtlety, recorded in `install-lifecycle-fixtures.py`
and in commit `2c6170b`: denying the simple right `W` denies
`FILE_GENERIC_WRITE`, whose `STANDARD_RIGHTS_WRITE` shares the `READ_CONTROL` bit,
so a native reader cannot even open the payload and the product honestly refuses
with `cannot_hash_payload`. That is not the case the gate means to test. The
fixture denies `WD,AD` (write data, append data) only, so the payload stays
readable, the product hashes it, reaches the copy and refuses with
`cannot_copy`. The measurement is recorded in the fixture's `deny_write`
docstring and in commit `2c6170b`; the gate's own run is the reproducible
evidence.

## VPK refusals: five states, one vocabulary

`vpk_query` (a new tool, built from `native/tools/vpk_query.cpp`) answers with
one stable word per state and the reader's own reason in parentheses:

| state | `vpk_query` answer | product trace | title |
|---|---|---|---|
| absent | `ABSENT (ERROR_FILE_NOT_FOUND)` | no refusal line at all, `refused=0` | no refusal count |
| unreadable | `UNREADABLE (ERROR_ACCESS_DENIED)` | `vpk: refused attribution=unreadable`, reason `cannot read VPK directory file` | `VPK refused: 1` |
| unsupported format | `UNSUPPORTED (unsupported VPK header)` | attribution `unsupported` | refused 1 |
| truncated | `TRUNCATED (truncated VPK directory tree)` | attribution `truncated` | refused 1 |
| valid | `1 entries, 1 match 'maps' ext=.bsp` | `archives opened=1 refused=0`, `store=vpk`, `BSP: 2512 tris` | bracket present |

Absent is deliberately not a refusal (a TF2 install without a `*_dir.vpk` is
normal); the three present-but-broken states get three different attributions, and
the product's attribution is compared to `vpk_query`'s word for the same root
(section 15 asserts equality, not just presence). The second trace line was added
so the pre-T42 first line keeps its exact shape for the gates that already parse
it (`vpk: refused file=<name> reason=<reader reason>`); the attribution line is
`vpk: refused attribution=<word> file=<name>`.

Two limitations are recorded rather than hidden, and the matrix asserts them:
`vpk-volume-missing` opens the directory index, finds the named volume absent,
counts no refusal (the open succeeded) and leaves `store=none` with no per-entry
reason; `precedence-shadow` proves a bad loose BSP still shadows a valid archived
one (`store=maps`, `BSP: invalid`). Both are named limitations, not passes.

## Mutation gate (M1, M2, M3)

| mutation | what it breaks | invariant that catches it | reading |
|---|---|---|---|
| M1 | the product takes the console even when stdout is redirected | a redirected run must leave its answer in the file | exit 0, redirect **0 bytes** |
| M2 | uninstall ignores `preserved`/`unchanged` | a user's own file survives uninstall | `removed=4 preserved=0`, user file **absent** |
| M3 | a manifest need not declare its format | a foreign text file is not a manifest | exit **0** (accepted) -- the reference exits 2 `no_manifest` |

Every mutation: rebuild, identity check against the mutated build, measure,
restore, `cmp`, rebuild. `INSTALL-LIFECYCLE-MUTATION=PASS (3 mutations caught,
sources restored)`.

## Readiness gate

`install-readiness-check.sh` keeps its 13 sections and its 4-case mutation
phase (3 mutations). T42 updates two statements there: §13 used to list
upgrade/downgrade as an open gap, and now prints
`CLOSED upgrade_and_downgrade_decision=1 owner=T42` and pins the two
remaining gaps by name (`INSTALLER_GAPS=2`):
`no_shortcuts_or_file_association` (owner T28 release packaging) and
`tf2_assets_not_packaged_by_design` (owner product: locate or prompt for a TF2
install).

T42 also retargeted one of the four mutations. M3 dropped the smoke probe
from `install(TARGETS)`, but T37 moved the probe into `.uninstall/payload`, so
removing the root rule no longer changed the shipped package at all: measured
under M3, the staged package still carried
`.uninstall/payload/native_install_smoke_probe.exe`, while the mutation's three
assertions still compared it with a pre-T37 two-file layout. The mutation had
become vacuous and its readings were about the old package. M3 now removes the
probe from the payload rule as well and pins the consequence (the generated
install script loses both references, the staged package loses exactly the two
probe copies and adds nothing, the count is the forward run's count minus two,
and the restored package equals the forward run's own listing). No assertion was
weakened and nothing hardcoded was deleted; the numbers are now derived from the
forward reference.

Readings on this branch, forward and mutation executed in **one** host-lock
transaction that reads the lock owner at start and end and fails closed if it is
not this owner (start guard, then `hold_owner_end=t42` and
`LOCKED-COMMAND-EXIT rc=0 owner=t42` at 12:42:19):
- forward: **cases=13 checks=107, INSTALL-READINESS=PASS**
- mutation: **cases=4 checks=40 mutations=3, INSTALL-READINESS-MUTATION=PASS**

Evidence: `t42-readiness-forward.log`, `t42-readiness-mutation.log`.

## How to reproduce

Both gates need the host lock and the product must be rebuilt from clean sources
first. From the worktree root, one waiter per owner at a time:

```
HOSTLOCK_TIMEOUT=5400 bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t42 \
  bash .scratch/t42/rebuild-run-lifecycle.sh    # build, forward, then mutation
HOSTLOCK_TIMEOUT=5400 bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t42 \
  bash .scratch/t42/run-readiness.sh            # build, forward, then mutation
```

Expected: lifecycle forward `cases=15 checks=213`, mutation `cases=3 checks=25
mutations=3`, both exit 0; readiness forward `cases=13 checks=107`,
mutation `cases=4 checks=40 mutations=3`, both exit 0. The mutation
phase of the lifecycle gate must print `reference exe sha256=` and, for each
mutation, an `is the binary the mutation built` OK line; a run that prints
`MUTATION-NOT-LINKED` exits 90 and is not a pass.

## Evidence index (`evidence/t37-install/`)

| file | what it is |
|---|---|
| `t42-lifecycle-forward.log` | the 15-section forward run, 213 assertions |
| `t42-lifecycle-mutation.log` | the mutation run, 25 assertions, 3 caught |
| `t42-lifecycle-forward-baseline.log` | the pre-T42 eight-section run, for comparison |
| `t42-m2-rootcause.txt` | the reference/mutated fixture measurement with hashes |
| `t42-mutation-summary.txt` | the mutation readings and identity hashes, cut from the log |
| `t42-gate-summary.txt` | all four `cases=/PASS` lines |
| `t42-upgrade-downgrade.log` | section 9 excerpt |
| `t42-migration.log` | section 10 excerpt |
| `t42-first-launch-restart.log` | sections 11-12 excerpt |
| `t42-nonadmin-missing-file.log` | sections 13-14 excerpt |
| `t42-vpk-matrix.log` | section 15 excerpt |
| `t42-vpk-query-states.log` | `vpk_query`'s answer for all five states, verbatim |
| `t42-readiness-forward.log`, `t42-readiness-mutation.log` | the readiness gates |

## NOT VERIFIED (named, not hidden)

- **No MSI/MSIX/CPack artifact.** The product installs itself (`--install`), keeps
  a manifest, and uninstalls itself; there is no packaging layer and the gates pin
  that (`installer_defs=none`, `CPack=none`). Release status:
  `no_shortcuts_or_file_association`, owner T28 release packaging.
- **No TF2 assets are packaged.** The package is the demo plus its metadata; the
  product locates a TF2 directory. Release status:
  `tf2_assets_not_packaged_by_design`.
- **A real standard-user install was not exercised.** This host runs elevated; the
  non-admin case is a DACL simulation (see section 13 above). Release status: a
  genuine unprivileged install needs a standard-user token host (T43 candidate).
- **No migration engine.** A moved directory verifies and uninstalls, but the
  manifest's `installDirectory` is not rewritten; nothing reads it today, and the
  gate asserts that it still names the old path. Downgrade is refused by
  `packageVersionCode`; equal codes with a different version string are a
  reinstall, not an upgrade.
- **The running image cannot be deleted by itself**; uninstall defers it by name
  and exits 3 (`INCOMPLETE`). This is by design and pinned.
- **`vpk-volume-missing` has no per-entry reason** (`store=none`), and a bad loose
  file shadows a valid archived one. Both are named limitations in section 15.

## Residual risk

- The mutation gate's strength depends on the fixture actually executing the
  staged binary; the identity guard covers the staging mistake, but a future
  fixture that runs something else entirely would need its own identity check.
- The lifecycle gate's timings and file sizes are host-dependent; it pins relations
  (same size, same hash, restored bytes), not absolute numbers.
- `install_readiness` §13's gap count (2) is pinned on purpose: closing one has to
  update the line, so a silent addition is caught.

## Commits

| commit | what |
|---|---|
| `bd64d14` | five stable VPK refusal attributions in `vpk_query` and the product |
| `b1d8dba` | an install compares the manifest `packageVersionCode` (upgrade note, downgrade refusal) |
| `741df9e` | the T37 lifecycle fixtures and M2 against the mutated binary |
| `a829fb6` | keep the pre-T42 VPK refusal line byte-compatible; fix the gate readings the run caught |
| `2c6170b` | deny only the data writes when simulating a protected directory |
| `7862400` | the lifecycle gate's evidence |
| `811b258` | retarget the readiness M3 mutation to the post-T37 package (it had become vacuous) |

The readiness evidence and this report are added by the commit that carries them;
their readings are the four gate verdicts above.
