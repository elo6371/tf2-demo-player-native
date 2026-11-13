# State at the handoff cut

## What was cut, and from where

| | |
|---|---|
| Cut date | 2026-11-13 (Asia/Shanghai) |
| Source worktree | `D:/TF2_Native_Worktrees/t105-e5` (branch `candidate/e5`) |
| Source commit | `2f8d78d` -- "feat(trace): print the lightmap sample behind a hole-trace point (R2-3)" |
| Clean tree | `main`, one orphan commit, 529 files, 14,102,672 bytes |
| Full history | `archive/full-history-2026-11-13` -> `2f8d78d` |
| Remote | `git@github.com:elo6371/tf2-demo-player-native.git` |

The clean tree is derived mechanically from the source commit's tree: 529 whitelisted
paths and nothing else. It is not a trimmed copy that lost files by accident -- every
one of the 6,197 other paths is listed in the `DROP-PATHS` inventory kept outside the
repository, and all of them are still reachable on `archive/full-history-2026-11-13`.

Recompute the numbers:

```bash
git ls-tree -r --name-only main | wc -l          # 529
git ls-tree -r -l main | awk '{s+=$4} END {print s}'   # 14102672
```

## Identity: how to say "this exact build" in one number

The product does not carry a version string worth trusting for this purpose
(`VERSION.txt` is `0.1.0` / `versionCode=100`, a package number that a release can
re-stamp without a rebuild). The identity that matters is the **content hash of the
compiled inputs**, recomputed from the git index by a second implementation:

```bash
bash build-identity-content.sh hash     # 64 lowercase hex
bash build-identity-content.sh method   # git-clean-v1 when the tree is clean
bash build-identity-content.sh stats    # the D-90 input census
```

At this cut, on this tree, `method=git-clean-v1` and

```
hash = 98240bf9c08be3018d9275c79feb60ff8f27d671df7317e57ffc0d7a2c8277f1
```

That value covers `native/CMakeLists.txt`, `native/src`, `native/include`,
`native/third_party` and `native/cmake` -- see the header of
`build-identity-content.sh`. It does **not** cover the root scripts, so a gate script
edit does not move it. `build-identity-check.sh` compares this against the hash compiled
into the executable; a mismatch means the binary is not from this source.

## What is verified, exactly

One thing is verified at the level of "the whole chain ran and passed":

- **2026-11-02, commit `59d94a7`**: `bash verify-all.sh --quick` ran end to end on the
  merged tree, 30 steps, **29 PASS**; step 27 failed only because the runner compared a
  gate's assertion count against a hardcoded 71 while the gate printed 73, and `fa1233f`
  fixed the comparison to a lower bound. Step 27 was then re-run alone:
  `SPECTATE-CHECK=PASS`, 73 assertions, exit 0. Recorded in `CHANGELOG.md`.

Everything after that commit is, in the vocabulary of `docs/T85-SUPPORT-SCOPE.md`,
**NOT VERIFIED at chain level**. Concretely:

- `candidate/e5` is **271 commits ahead** of the newest public head
  (`p0-entity-protocol`, `d5839b5`), and the 2026-11-02 run is earlier still.
- 20 commits landed on 2026-11-12/13 alone, touching 50 files (+203,250 / -163 lines)
  since the roadmap baseline `917332d`.
- No full chain, and no `verify-release.sh` run, is recorded on this tree. The last
  *release* chain has never passed: step 1 of it (`gate-launch-hygiene-check.sh`) and the
  machine-isolation check (debt D-22) are expected red until D-22 is fixed -- see
  `docs/RELEASE-POLICY.md`.

So the honest one-line state is: **the product builds and plays demos; the gates that
exist are the acceptance authority; the full chain has not been re-run since 2026-11-02;
the release chain has never passed.**

Per-capability status is in `docs/HANDOFF-TASKS.md`, with the gate and the line that
would have to turn red for the claim to be wrong.

## What was uncommitted when the cut was taken

The source worktree at `2f8d78d` was not perfectly clean. What reproduces here:

| Item | Disposition |
|---|---|
| `hud-live-check.sh` modified | **not** included as modified; the committed version is in `main` |
| `docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md` (untracked, 46,220 B) | **included** in `main`, byte for byte |
| `docs/MAP-MATERIAL-PROGRESS-2026-11-12.md` (untracked, 10,520 B) | **included** in `main`, byte for byte |
| `D`, `D：/` (empty junk directories) | ignored |

Including the two untracked documents is a deliberate exception to "the tree is exactly
the commit's tree": they are the most recent written state of the project (2026-11-12,
the day before the cut) and the roadmap is the execution checklist this handoff points
at. They are copied unchanged, so anything in them that describes the 11-12 worktree
still reads that way -- in particular the roadmap's opening paragraph about "21 modified
tracked files" describes that evening, not this tree; those changes are committed as of
`2f8d78d`. To drop them, `git rm docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md
docs/MAP-MATERIAL-PROGRESS-2026-11-12.md` and re-commit.

## Document drift found while cutting this tree (fix or believe accordingly)

- `docs/RELEASE-POLICY.md` item 2 enumerates **14** chain steps. The code has **15**
  (`verify-release.sh:89`, `STEPS=(...)`): `hud-live` was added to the chain after that
  paragraph was written, and `TOTAL=${#STEPS[@]}` therefore prints 15. The policy's
  *rule* is still right; its count and its example verdict line are one step behind.
- `run-demos.sh` and 59 other scripts hardcode `D:/TF2_Demo_Player/...` (the old checkout
  path on the authoring machine). No such directory exists on a fresh host; see
  `docs/HANDOFF-MISSING.md`.
- `docs/T85-SUPPORT-SCOPE.md` is frozen at commit `e15577b` (2026-11-07) and names a
  binary hash from then. It is the authoritative *scope* statement, not a statement about
  this binary.

## What is deliberately not here

| Not in `main` | Where it is |
|---|---|
| `evidence/` -- 6,172 files, 954.9 MB, 90 directories | `archive/full-history-2026-11-13`, and the bundle |
| 62 root `T*-<date>.md` task cards and review notes | same |
| 8 non-`.md` files that existed only on `handoff-*` branches (6 probe sources, `native/include/model_instance_contract.h`, `docs/native-world-texture-check.png`) | same |
| the 40 remote heads of the 2026-10-05 line | bundle + `patches/` per branch |
| build outputs, `.scratch/`, `native/build-nmake/` | never committed (`.gitignore`) |
