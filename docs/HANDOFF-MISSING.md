# What a recipient needs that is not in this repository

Everything here was verified to be absent from the commit, not merely assumed absent. Each
item says where it lives, what breaks without it, and -- where one exists -- the cheapest
workaround.

## 1. The oracle binary (blocks steps 4, 5, 6)

`check-oracle.sh` compares the product's decode of a demo against an independent
implementation. That implementation is a Rust program that is **not in this repository**:

```
D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe
```

The path is written into `verify-all.sh:405` and used again by `oracle-corpus-check.py`.
Without it, `verify-all.sh` cannot run: steps 4, 5 and 6 all invoke it, and step 10 does
too. Its source is not in the bundle either -- the bundle contains this project's refs
only. **This is the single largest gap.** A recipient must obtain the oracle from the author
or rebuild an equivalent; nothing in this tree substitutes for it, because the whole point
is that it is a second implementation.

Fallback if the oracle is lost: run `bash verify-fast.sh` (43 assertions, no oracle) and the
product-side steps by hand. That is a much weaker story, and it should be stated as such.

## 2. `hostlock.sh` (blocks 31 scripts)

31 root scripts serialize machine-wide work through a helper that lives outside the
repository:

```
LOCK=${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}
```

Referenced by, among others, `build-identity-check.sh`, `class-pixels-check.sh`,
`outline-check.sh`, `attach-weapon-budget-check.sh`, `player-animation-mutations.sh`, and
`review-workspace-mutations.sh`. Contract used by the callers:

- `hostlock.sh run <owner> <command...>` -- take the host lock, run, release.
- `hostlock.sh status` -- report the current owner.
- it must be re-entrant within a process tree, because `verify-all.sh` sets
  `VERIFY_ALL_LOCK_HELD=1` and expects nested callers not to deadlock.

**This tree now ships a copy**: `tools/hostlock.sh` (sha256 `fa222b6a…0be26`), the exact
script the gates have been serialising through. Point the callers at it with
`export HOSTLOCK=tools/hostlock.sh` (they otherwise keep the absolute authoring path as the
default, so an existing machine is unaffected). Verified with `bash -n` only -- it has not
been exercised from a fresh clone on a second machine.

Workaround: set `HOSTLOCK` to any script with that contract, or to a stub that just
`exec "$@"` after dropping the owner argument. A stub is fine on one machine and wrong on
a build host that runs two gates at once -- the lock exists because two gates writing
`native/build-nmake/` corrupt each other.

## 3. A Team Fortress 2 installation (blocks every step that reads a demo or a map)

No Valve asset is in this repository, by design. The scripts default to

```
D:/SteamLibrary/steamapps/common/Team Fortress 2/tf
```

and 80 scripts mention `steamapps`. Override with `TF_ROOT` (and the per-suite variants
`T61_TF_ROOT`, `T62_CLASS_TF_ROOT`, `T83_WEAPON_TF_ROOT`, `CAPTURE_TF_ROOT`,
`T85_WORLD_DEMO`, `BONE_MISMATCH_DEMO`); the product itself takes `--tf-root` everywhere.

### Maps the evidence refers to

From `docs/T85-SUPPORT-SCOPE.md`. The five authorised maps, with the exact file identity
the gates were pinned against -- a different build of the same map name will not reproduce
the numbers:

| Map | BSP bytes | SHA256 (prefix) | Role |
|---|---|---|---|
| `cp_snakewater_final1` | 59,590,420 | `f09d406d...` | authorised |
| `cp_sunshine` | 30,319,857 | `ca9ad7d0...` | authorised |
| `koth_bagel_rc12` | 26,486,963 | `c991a61e...` | authorised |
| `koth_bagel_rc11` | 26,370,688 | `f06cfe4b...` | authorised |
| `pl_upward_f12` | 25,252,306 | `0d9fe548...` | fixture only: player/animation work |

`koth_bagel_rc13` appears in the corpus as a **negative control** -- a demo whose BSP is
deliberately missing, used to prove the loader reports absence instead of guessing.

### Demos

| Set | Count | Where it is |
|---|---|---|
| corpus calibration | 24 | listed in `evidence/corpus-calib/demos.txt` (23 `autorecord_2026-05-18..2026-09-30_*.dem` + `73.dem`); about 0.6 GB in total |
| gate fixtures | 4 | hardcoded as `D:/TF2_Demo_Player/testdata/demos/*.dem` in ~60 scripts |

The four fixture demos, by the SHA1 prefix the scripts use: `4a9bfb92...koth_bagel_rc13.dem`,
`bb841c6d...cp_snakewater_final1.dem`, `a9630c65...pl_upward_f12.dem`,
`dfbce608...koth_bagel_rc11.dem`. They are ordinary recordings; the hashes exist so a gate
can refuse a substituted file. Any recording of the same map from a compatible build works
for exploration, and will not reproduce the pinned numbers.

## 4. The old checkout path `D:/TF2_Demo_Player`

60 scripts hardcode fixture paths under a checkout directory that never existed on any
other machine, e.g. `SRC=D:/TF2_Demo_Player`, and the demo paths above. Nothing in the
repository creates that directory. Two ways forward:

```bash
# a) conform: put a checkout there
git clone <this repo> D:/TF2_Demo_Player
# and place the four fixture demos under D:/TF2_Demo_Player/testdata/demos/

# b) re-home: rewrite the constant in place, then re-run the gate before trusting it
grep -rl 'D:/TF2_Demo_Player' --include='*.sh' . | xargs sed -i 's|D:/TF2_Demo_Player|$PWD|g'
```

(b) is the honest fix and also the one that can break a gate silently, because several
scripts compare a printed path against a literal. Change it in a branch, run the affected
gates, and record what moved.

## 5. The evidence and history that was deliberately left out

Not in the commit, preserved elsewhere. Both files live outside this repository, on the
handoff media:

| File | Bytes | SHA256 |
|---|---|---|
| `archive/tf2-native-all-refs-2026-11-13.bundle` | 241,243,698 | `8c1e66ab39d4761b4a38a047ddc4cff66b23910021636bc3d9e58e7a6e127d29` |
| `archive/patches/` (20 files: 16 `.patch`, 4 `.patch.gz`) | 11,724,238 | per-file, see `archive/patches/README.md` |

The bundle records a complete history: 142 local branches, 55 remote heads, 1 tag, plus
the source commit. `git bundle verify` passes on it. It is **not** committed here because a
241 MB blob in a repository that exists to be small defeats its purpose.

`archive/patches/` holds the 20 commits that exist on the eight `DIVERGED` heads and
nowhere else, one directory per branch, produced with `git format-patch --no-signature
--no-binary`. Four patches are gzipped because a single commit in `manual-followup` added
45 evidence files whose 22 MB text diff is not worth carrying uncompressed.

## 6. Machine state the release chain assumes

Debt **D-22** (`docs/T85-DEBT-LIST.md`) is that two gates mutate machine-wide settings:
`%LOCALAPPDATA%/TF2 Demo Player/settings.ini` and `ui.ini`. `verify-release.sh` snapshots
existence, size, mtime and sha256 before and after, and prints `MACHINE-MUTATED=FAIL` when
they change. Consequences for a recipient:

- a "clean machine" means those two files do not pre-exist, or are expected to change.
- the launch-hygiene gate (release step 1) is expected red until D-22 is fixed, which is
  why `verify-release.sh` has never printed `PASS`.

## 7. Decisions the repository owner has to make

| Decision | Why it cannot be made here |
|---|---|
| **LICENSE** | there is none. `README.md` says public visibility does not grant permission to reuse the code. No licence text was invented for this handoff. Third-party terms that do exist: `native/third_party/webview2/LICENSE.txt`, `native/third_party/webview2/NOTICE.txt`, and the LZMA SDK headers. |
| Keep or drop `archive/full-history-2026-11-13` | it is what makes the evidence traceable; it is also a 241 MB clone for anyone who fetches it by default |
| Publish the oracle | without it, steps 4/5/6/10 are permanently `NOT VERIFIED` for everybody |

## 8. Checklist: first green chain on a new machine

1. Install VS 2022 with MSVC `14.44.35207` and SDK `10.0.26100.0`; put the SDK `bin/x64` on
   `PATH` (`build-cmake.sh` needs `rc.exe` and `mt.exe`).
2. Install Git for Windows bash, Python 3 with Pillow, PowerShell 5.1.
3. Install TF2; point `TF_ROOT` at its `tf` directory.
4. Place the four fixture demos; either use `D:/TF2_Demo_Player` or re-home the scripts.
5. Obtain `ent-oracle.exe`; confirm `ent-oracle.exe --version` runs.
6. Provide `hostlock.sh`, or a stub, via `HOSTLOCK`.
7. `bash build-cmake.sh` then `bash verify-fast.sh` -- this pair needs none of 3..6 except a
   build toolchain, so it is the first honest signal.
8. `bash verify-all.sh --quick`, then `bash verify-all.sh`.
9. Only then consider `bash verify-release.sh`, and expect step 1 to fail on D-22.
