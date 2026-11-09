# build-identity, chain step 9, measured on a clean tree (2026-11-10 00:03Z)

This directory is the evidence the chain's step 9 (`build-identity`) promises,
copied verbatim out of `.scratch/build-identity-run/forward` — a directory the
gate uses as its default OUT and that `.gitignore` keeps out of the tree. Without
this copy the step would have "run and passed" with nothing reviewable in the
repository, which is exactly the defect D-60 recorded.

## Who produced it

| field | value |
|---|---|
| producer | `bash build-identity-check.sh` (default forward mode; no `--mutation`) |
| commit | `5b78542ff9a50a53f1902747d4acb3c76013363a` |
| tree state | `git status --porcelain` = 0 lines, before and after |
| exe | `tf2_demo_native.exe` 2,838,528 B sha256 `0438ec4844b7c5fa24203bbbe87f01bd9d95f22577a9fd6bfbebb06cf40669c7` |
| hostlock | `ACQUIRED owner=workflow-auditor at 2026-11-10 00:03:58` |
| run log | `D:/_shared/EXEC-2026-11-09/t46-buildid/run-20261109T160357Z.log` (189 lines / 12,976 B / sha256 `a5f479a324c700318e6067ce58b8ddb16c67547d6438ab8cd94240e087882e9a`) |
| verdict | `BUILD-IDENTITY=PASS assertions=118 identity=exe-sha+HEAD+native-tree-sha` (0 red) |

## Gate identity, three numbers that all differ

The gate records the sha256 of the file **as it sits on disk**, which is CRLF:

| form | sha256 |
|---|---|
| working-tree file (CRLF) — the one the run recorded | `3e01cc0d57c5db68924bae932c5714195055c3f61718c792f1af52dc31899421` |
| `git show HEAD:build-identity-check.sh` (LF blob content) | `7fe5e16878adc1611fe8c098d0352ccf02740a1757c40f9900bb0e62a621fa7d` |
| blob id | `c1b32a166edae6a04b7b897438ad70a3f263c9b3` |

Do not treat the CRLF hash as a disagreement with the commit: it is the hash of a
different byte sequence. The same caveat applies to every gate that prints its own
sha256 (see `evidence/t91-merge/PIXEL-DELTA.md` for the class-pixels case).

## What the product said about itself

```
package=tf2_demo_native version=0.1.0 versionCode=100
buildUtc=2026-11-09T16:03:59Z sourceDateEpoch=1794240228 sourceTree=5b78542ff9a5
dirty=0 dirtyFiles=0 dirtyTracked=0 dirtyUntracked=0
```

This is the first build-identity reading in the project with `dirty=0`: the earlier
green run (`run-20261109T160102Z.log`, same assertions, exe `74fa6341…`) was taken
while the gate itself was the one uncommitted file, so its stamp said
`dirty=1 dirtyFiles=1`. Because `native/cmake/tf2-product.cmake:185` compiles the
stamp into the exe, those two runs necessarily produced different binaries — which
is D-54 in one sentence.

## The twelve files

| file | bytes | what it is |
|---|---:|---|
| `version-baseline.txt` | 191 | `--version` on the clean tree — the reading the run is about |
| `version-dirty-probe.txt` | 191 | `--version` after the gate dirtied one tracked file on purpose |
| `version-restored.txt` | 191 | `--version` after restoring — must equal the baseline |
| `probe-version.txt` | 199 | the probe's own version answer |
| `args-strict.err` | 93 | stderr of the `--strict-args` case |
| `args-strict.trace` | 169 | trace of the `--strict-args` case |
| `args-unknown.trace` | 169 | trace of the unknown-argument case (exit 13, refused) |
| `args-unknown.log` | **0** | stdout of that case — deliberately empty, see below |
| `args-clean.trace` | 77,166 | trace of a normal launch: no unrecognised argument |
| `quit.camera` | 65 | the camera line, proving the process ended where it should |
| `source-identity.txt` | 564 | HEAD + native tree sha256 as the gate computed them |
| `readings.txt` | 888 | the human-readable summary the gate writes last |

Every sha256 is in `SHA256SUMS` next to this file.

### Why `args-unknown.log` is zero bytes and why that is not a missing file

The product answers an unrecognised argument with exit 13 and **nothing on stdout**.
The gate's first version of the promise check used `-s` (non-empty) for all twelve
names, so this file was reported `MISSING` and RUN22 went red with
`BUILD-IDENTITY=FAIL assertions=118 red=1` (log
`t46-buildid/run-20261109T160102Z.log`). The check now separates `present` (`-f`) for
logs that are legitimately empty from `nonempty` (`-s`) for readings — commit
`5b78542`. The zero-byte file is the fixed behaviour, not a truncated artifact:
its sha256 is the sha256 of the empty string,
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

## Limits of this evidence

- **Forward only.** This run does not include `BUILD-IDENTITY-MUTATION`; no mutation
  was attempted, so nothing here shows that a wrong identity would be caught.
- **It is not a chain run.** Step 9 was executed on its own under hostlock, not by
  `verify-release.sh`. The chain has never reached this step (its best run stopped
  at 3 of 15).
- **The tree moved after it.** Later commits changed the tree again; the readings
  describe commit `5b78542` and exe `0438ec48…`, not whatever HEAD is today.
- `source-identity.txt` and `readings.txt` are the gate's own reconstruction; a
  reviewer who wants to re-derive them should run the gate, not trust the text.

## How to re-check without re-running the product

```sh
cd evidence/t91-merge/RUN22-20261110-buildid
sha256sum -c SHA256SUMS
grep '^dirty=' version-baseline.txt            # expect dirty=0
grep -c 'BUILD-IDENTITY=PASS assertions=118' /d/_shared/EXEC-2026-11-09/t46-buildid/run-20261109T160357Z.log
```
