# Tooling: what builds it, what checks it, what is missing

## The machine this expects

| Need | Exact version / path | Where it is hardcoded |
|---|---|---|
| MSVC | `14.44.35207`, `C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/` | `build-cmake.sh:30-31` |
| Windows SDK | `10.0.26100.0`, `C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64` | `build-cmake.sh:30-31` |
| CMake | **NMake Makefiles** generator only | `build-cmake.sh` (see the note below) |
| bash | Git for Windows / MSYS2 bash | every `.sh` |
| Python 3 | stdlib + Pillow for the pixel gates | the `*.py` helpers |
| PowerShell | 5.1+, for the capture harnesses | the 11 `*.ps1` |
| A TF2 install | `--tf-root`; the scripts default to `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` | ~80 scripts (`TF_ROOT` overrides) |

The generator is not a preference. `build-cmake.sh` says why: the Visual Studio
generators call `vcvars64.bat`, which shells out to `reg.exe`, which this authoring
machine's application blacklist kills -- so the build fails for a reason that has nothing
to do with the source. NMake is used to keep the build a direct `cl.exe` invocation.

## Build

```bash
bash build-cmake.sh               # configure + build the default target set
bash build-cmake.sh configure     # configure only
bash build-cmake.sh clean         # remove native/build-nmake/
bash build-target.sh tf2_demo_native   # build one target
bash build-target.sh entity_protocol_probe
```

Output lands in `native/build-nmake/`; the build log is `evidence/cmake-build.log`.
`build-identity-content.sh` recomputes the index hash (see `docs/HANDOFF-STATE.md`), and
`build-identity-check.sh` compares it against the value compiled into the exe.

## The four kinds of script in the root

| Kind | Count | What it is |
|---|---|---|
| `*-check.sh` | 79 | one gate each. A gate prints its own verdict line and exits non-zero on failure. This is the acceptance authority. |
| `verify-*.sh` | 9 | the chains and the lock: `verify-all.sh`, `verify-fast.sh`, `verify-release.sh`, `verify-lock.sh`, ... |
| `build-*.sh` | 5 | build entry points and the identity tooling |
| `.py` / `.ps1` | 48 / 11 | measurement helpers: pixel comparisons, VPK/BSP readers, trace post-processing, capture harnesses |

Plus one-off drivers that are still part of the workflow: `run-demos.sh` (census over a
demo directory), `mutate.sh` (the C++ mutation suite), `check-oracle.sh` (the independent
oracle cross-check).

## The chains

| Command | Steps | Cost | Needs |
|---|---|---|---|
| `bash verify-fast.sh` | 6 self-tests, 43 assertions | seconds | a built tree |
| `bash verify-all.sh --quick` | 30 | ~40 min | build + TF2 + demos + **oracle** |
| `bash verify-all.sh` | 30 | ~1 h (step 5 ~20 min at `--sample 40`) | same |
| `bash verify-release.sh` | 15 | longest | same, plus a clean machine |

Locks: `verify-all.sh` takes `.scratch/verify-all.lock`; scripts that need exclusive use
of the machine go through `hostlock.sh` (`run <owner> ...`, `status`). `verify-lock.sh`
self-heals a stale lock by checking owner + pid + heartbeat, and refuses with exit 2 while
the owner is alive. `hostlock.sh` is shipped as `tools/hostlock.sh` (the scripts still default to
`/d/TF2_Native_Worktrees/_shared/hostlock.sh`; set `HOSTLOCK=tools/hostlock.sh` to use the
copy in this tree). See `docs/HANDOFF-MISSING.md` for the contract and the caveat that the
copy has only been syntax-checked, not exercised from a fresh clone.

## Probes: the second implementation

`native/tools/` holds 53 probe programs (`entity_protocol_probe`, `entity_state_probe`,
`model_mesh_probe`, `world_material_probe`, `item_schema_probe`, `audio_index_probe`, ...).
They read the same demo with a different code path and print structured output; the gates
diff that against the product or against the Rust oracle. If a gate disagrees with the
product, the probe is the second opinion -- that is why the probes are in this tree and not
in a side repository.

Two probe traps worth knowing before you trust a green run:

- `item_schema_probe --self-test` is **not** a self-test: the flag is treated as a file
  name, the probe prints an error and still returns 0. The real entry point is
  `--wire-self-test`. `verify-fast.sh` uses the latter.
- Probe output is compared **additively** (step 7): a new field is allowed, a lost field
  is not. That is what `evidence/probe-baseline/` is for.

## Test data in the tree

| Path | Files | Purpose |
|---|---|---|
| `testdata/instancebaseline-budget/` | 4 | wire fixtures for the baseline budget path |
| `testdata/vmt-nesting/` | 10 | VMT include/patch nesting cases used by the material gates |
| `scripts/timeline-{clamp,pause,speed}.script` | 3 | input scripts for `timeline-check.sh` |
| `native/tests/fixtures/gate-settings.ini` | 1 | the one gate fixture directory in `native/` |
| `evidence/probe-baseline/`, `evidence/corpus-calib/` | 37 | frozen baselines, see `docs/HANDOFF-EVIDENCE.md` |

**No Valve asset is in this tree.** No `.mdl`, `.vtf`, `.vmt`, `.bsp`, `.vpk` or `.dem`
from the game itself is present outside `evidence/`, and the `.mdl`/`.vtf`/`.dem` files
that *are* in the archive are synthetic malformed samples written to exercise error paths.
That is a deliberate boundary, and it is why the gates need an installed TF2.

## Rules that come from outside this repository

`docs/READ-THE-PLAYBOOK-FIRST.md` points at an external repository
(`github.com/elo6371/coding-problem-playbook`, `problems/INDEX.md`) as the place where
recurring traps are written down. Two rules from it are enforced in this tree and will
bite a recipient who does not know them:

1. **Every product launch must be silent**: pass `--audio-disabled`, or pass an explicit
   `--audio-device` and write the reason at the call site. A gate that opens an audio
   device on a machine somebody is using is treated as a defect.
2. **A gate must print its verdict, not just exit 0.** `docs/GATE-DESIGN-TRAPS.md` is the
   collected list of ways a gate has passed for the wrong reason (empty fixture set,
   empty executable, empty mutation list).

## What this tooling cannot do

`docs/HANDOFF-MISSING.md` lists, with sizes and locations, the four things that keep this
tree from being self-sufficient: the oracle binary, the demo corpus, the map/demo assets
from a TF2 install, and `hostlock.sh`.
