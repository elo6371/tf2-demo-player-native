# Handoff: read this first

This repository is the Windows-native TF2 demo player: a C++17 / Win32 / Direct3D 11
program that reads a Team Fortress 2 `.dem` recording and draws the match. No Electron,
no Node.js runtime, no WebGL. The only web technology in the product is a vendored
WebView2 loader behind the logs panel (`native/README.md`). It is **not** a release
product: see "What this is not" below.

Every claim in the five HANDOFF documents points at a file, a line or a command. Where a
claim cannot be checked that way, it is written as NOT VERIFIED and it is not presented as
done.

## 30 seconds: build, run one demo, run one gate

```bash
# 1. build (needs the toolchain in docs/HANDOFF-TOOLING.md; about 4 minutes cold)
bash build-cmake.sh                  # -> native/build-nmake/tf2_demo_native.exe

# 2. play a demo (any .dem from a TF2 install)
native/build-nmake/tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --demo    "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/<name>.dem"

# 3. one gate that needs no demo, no window and no audio device
bash verify-fast.sh                  # 6 self-tests, 43 assertions
```

`bash verify-all.sh` is the 30-step chain (about an hour). `--quick` shrinks only the
oracle corpus sample. `bash verify-release.sh` is the release chain, and its printed line
is the only citable whole-chain verdict -- see `docs/RELEASE-POLICY.md`.

**None of the three has been run on this tree.** The last full chain that passed is dated
2026-11-02 and ran on a much older commit; see `docs/HANDOFF-STATE.md` for the exact
difference.

The product has no `--help`. It answers `--version` and `--install-status`, and
`--strict-args` turns an unknown argument into exit code 2 instead of silence
(`native/src/main.cpp`).

## What is in this tree

| Path | Files | What it is |
|---|---|---|
| `native/src` | 84 | the product and the probes (`main.cpp` is 13,281 lines) |
| `native/include` | 59 | headers shared by the product and the tooling |
| `native/tools` | 58 | 53 probe executables (`entity_protocol_probe`, `world_material_probe`, ...) |
| `native/cmake` | 26 | CMake modules, including the build-identity stamp generator |
| `native/third_party` | 10 | WebView2 headers/loader + LICENSE + NOTICE, LZMA SDK |
| `native/docs`, `native/tests` | 18 | renderer notes, and the one gate fixture directory |
| `*.sh` in the root | 131 | build scripts, gates, and the chain entry points |
| `*.py` / `*.ps1` in the root | 59 | pixel and geometry analysis, capture harnesses |
| `docs` | 20 | design traps, release policy, debt list, per-task reports |
| `testdata` | 14 | wire fixtures and VMT nesting fixtures the gates diff against |
| `evidence/probe-baseline`, `evidence/corpus-calib` | 37 | frozen baselines the chain reads at run time -- **do not delete** |
| `scripts` | 3 | three timeline test scripts |

529 files, 14,102,672 bytes. There is no `tools/` directory at the root; the probes live
in `native/tools/`.

## The five documents

| Document | Question it answers |
|---|---|
| `docs/HANDOFF-STATE.md` | What is at this commit, what was verified, what was not |
| `docs/HANDOFF-TASKS.md` | Which capability is PASS, NOT VERIFIED or UNSUPPORTED |
| `docs/HANDOFF-EVIDENCE.md` | Where each number comes from, and which evidence is not in this tree |
| `docs/HANDOFF-TOOLING.md` | What builds it, what checks it, what each kind of script is for |
| `docs/HANDOFF-MISSING.md` | What a recipient needs that is not, and cannot be, in this repository |

`docs/archive-2026-10/` holds narrative documents recovered from the seven `handoff-*`
branches of the old history. They describe the 2026-10-01..10-06 window and are
historical: they are not a description of this tree. `docs/archive-2026-10/INDEX.md` says
what each one was.

## What this is not

- **Not a release.** `README.md` says it in one line: active development, not a
  release-ready product.
- **Not licensed.** There is no LICENSE file anywhere in this project, and the README
  states that public visibility does not grant permission to reuse the code. This is
  unresolved, not an oversight the handoff can fix: the owner has to choose a licence.
  Third-party terms that do exist: `native/third_party/webview2/LICENSE.txt` (1,513 B)
  and `native/third_party/webview2/NOTICE.txt` (3,979 B), and the LZMA SDK files carry
  their own header text.
- **Not pixel-parity with the Source renderer.** The gates that could be mistaken for
  that say so themselves; `docs/T85-SUPPORT-SCOPE.md` is the frozen statement of what is
  out of scope.
- **Not reproducible from this tree alone.** A release-grade run needs a TF2 install,
  the demo corpus and one small tool that are not in the repository at all:
  `docs/HANDOFF-MISSING.md`.

## History shape (why the clone is small)

`main` is a single orphan commit. The old history had 55 remote heads, 6,726 files and
955 MB, 98.5% of which was `evidence/`; a clone of it could not be used to read the
product. That history is preserved unaltered on `archive/full-history-2026-11-13`,
which points at the same source commit the clean tree was taken from, and in the
all-refs bundle named in `docs/HANDOFF-EVIDENCE.md`. `git log` on `main` will show one
commit: that is deliberate, not a truncated clone.

## One manual step: the old default branch

Pushing `main` and the archive branch, then deleting the other 55 heads, is scripted and
checkable. One branch is deliberately not deleted: GitHub will not delete the branch that is
currently the repository's default, and that is still `p0-entity-protocol`. The run skips it,
prints the skip, and stops as a clean state -- everything else is pushed, nothing else is
left.

1. **Settings -> Branches -> Default branch -> switch it to `main`**:
   `https://github.com/elo6371/tf2-demo-player-native/settings/branches`
2. Then delete the one branch that is left:

   ```bash
   git -C D:/TF2_Release_Staging push origin --delete p0-entity-protocol
   ```

`p0-entity-protocol` was the default when this tree was cut; the finalize script prints the
name it actually skipped. Nothing is lost by deleting it: every one of the 55 heads is in
`archive/tf2-native-all-refs-2026-11-13.bundle` (201 refs, sha256 in
`docs/HANDOFF-EVIDENCE.md`), and `archive/full-history-2026-11-13` keeps the history reachable
from a live branch.

