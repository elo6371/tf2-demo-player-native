# Materials a recipient has to supply, and how to check them

This repository ships the code, the gates, the documentation, the wire/VMT
fixtures and 37 frozen baselines that the chain reads at run time. It cannot
ship three classes of material: Valve's game content, third-party demo
recordings, and 955 MB of run evidence. This document lists each one with the
identity a recipient needs in order to prove their local copy is the copy every
number in these documents was measured against.

Everything below was read from the machine that produced the evidence
(2026-11-13). Paths are Windows paths; `tf/` is the TF2 installation.

## 1. The game installation (Valve content, cannot be redistributed)

Every gate that opens a demo or a map needs a Team Fortress 2 install. The
seven maps this project targets are:

| Map | Path under `tf/` | Bytes | SHA256 |
|---|---|---|---|
| `cp_process_f12` | `maps/cp_process_f12.bsp` | 18,558,778 | `cebe58a8014d26498b4a7eee49f6ed33372aa115131488d96b2ca8364d2a64f9` |
| `cp_sunshine` | `maps/cp_sunshine.bsp` | 30,319,857 | `ca9ad7d01f46112abbc5b02f3d144fad833e6a0ff3bd82a1fff38fa8d3c32474` |
| `cp_gullywash_f9` | `maps/cp_gullywash_f9.bsp` | 13,231,135 | `0606906c809756e3f40a116b66d222ed929073f636f9c3b1b2c5dbbf9cbc9a5b` |
| `cp_sultry_b8a` | `download/maps/cp_sultry_b8a.bsp` | 38,394,319 | `49bab2feeb53f102cd79601b38f3cbd9a30b79d19184fcb1baa24ac139aa66b2` |
| `koth_product_final` | `maps/koth_product_final.bsp` | 18,143,384 | `f36ce3d43377cc786821c43e44ccc88ee9549b7cb30d6e793d316134c6b59b03` |
| `koth_bagel_rc11` | `download/maps/koth_bagel_rc11.bsp` | 26,370,688 | `f06cfe4b389da5ca152dff0702fb3f4d69b48bfc7b08ed9994e1d057a211720a` |
| `pl_upward_f12` | `maps/pl_upward_f12.bsp` | 25,252,306 | `0d9fe5486023df49f0e6d4da43ac3e777baee87f1c588933dc16750cb7c51677` |

Two of them live under `download/maps/` because they are workshop downloads,
not stock maps. Also required: `tf2_misc_dir.vpk` and `tf2_textures_dir.vpk`
(the material resolver opens five fixed VPKs; see `main.cpp` `installWinMmBackend`
neighbourhood for the list). Sizes are on the order of 10 GB combined and are
not listed here because no assertion pins them -- what matters is that the VPK
contains the materials named in `docs/HANDOFF-EVIDENCE.md`.

Authority: the four `evidence/t62-matrix`-family baselines were captured with
the install above. A different install (other language, other mod content) can
change extracted VMT/VTF bytes and therefore pixels.

## 2. Demo recordings (third party, cannot be redistributed)

### 2.1 Gate fixtures -- four files, hard-coded in ~60 scripts

```
D:/TF2_Demo_Player/testdata/demos/*.dem
```

| File | Bytes | SHA256 |
|---|---|---|
| `4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem` | 46,921,111 | `03529698db749e77a1df1c476c1c0cc06847e1173b5366a0602f1e15161b5436` |
| `a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem` | 75,663,754 | `7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3` |
| `bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` | 51,834,617 | `308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f` |
| `dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem` | 64,600,031 | `0e8197dcc1fe18ca1028aba0fe09df2db291f7c64c4f54e9476e658199cea37d` |

These paths are the reason `docs/HANDOFF-MISSING.md` section 4 exists: either
recreate `D:/TF2_Demo_Player` or re-home the constant in the scripts.

### 2.2 The playback corpus used for the 26-cell playability run

```
E:/demo测试/<map>/{POV,STV}/*.dem
```

| Map | POV | STV |
|---|---|---|
| `cp_gullywash_f9` | `autorecord_2026-05-21_21-08-36.dem` (63,296,494) | `gully4454.dem` (81,660,813) |
| `cp_process_f12` | `autorecord_2026-09-03_21-05-39.dem` (100,302,450) | `1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem` (90,995,077) |
| `cp_sultry_b8a` | `autorecord_2026-06-16_21-42-44.dem` (56,193,150) | `f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem` (85,506,321) |
| `cp_sunshine` | `autorecord_2026-05-17_16-32-36.dem` (75,905,225) | `SUNSHINE.dem` (82,759,239) |
| `koth_bagel_rc11` | `testdemo111.dem` (5,497,243) | `dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem` (64,600,031) |
| `koth_product_final` | `autorecord_2026-09-10_21-01-05.dem` (100,388,768) | `prodcutscout.dem` (62,389,932) |

`pl_upward_f12` has **no POV recording**; that is why the playability matrix
records its POV cell as `NO-DEMO` rather than a pass (`docs/HANDOFF-LATEST-READINGS.md`).
`cp_process_f12/动作素材/demotest231312314444.dem` (6,283,371 B) is scratch, not
part of the matrix.

### 2.3 The 24-demo calibration corpus

Names are in `evidence/corpus-calib/demos.txt` (23 `autorecord_2026-05-18..2026-09-30_*.dem`
plus `73.dem`, about 0.6 GB). The files themselves are not in this machine's
searched roots any more and are **not needed** to reproduce the gate result:
step 9 of `verify-all.sh` compares against the 24 frozen reports that *are*
committed under `evidence/corpus-calib/reports`.

## 3. The oracle binary (the single largest gap)

```
D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe
```

A Rust second implementation used by `check-oracle.sh`; the path is written into
`verify-all.sh:405` and reused by `oracle-corpus-check.py`. Without it steps 4, 5,
6 and 10 cannot run. Its source is not in this repository or in the archive
bundle. Fallback: `bash verify-fast.sh` (43 assertions, no oracle).

## 4. Built artefacts (reproducible, deliberately not committed)

The product is built with `/Brepro`, so the same source content produces the same
bytes. The identity of the tree at `main` is:

| Artefact | SHA256 | Bytes |
|---|---|---|
| `tf2_demo_native.exe` | `8bff0858221b2f0a30e3806a29439c7c39964309f43bf051b9d314f929a1dfcf` | 2,970,112 |

Rebuild and check (from the repository root, Windows, VS toolchain):

```
bash build-cmake.sh                       # configure
bash build-target.sh tf2_demo_native tf2_identity
bash build-identity-content.sh hash       # must print 4498475d411e0dd9c126fe24e1b3ce66a87aac92fd09e9aa29b465201749dcf6
```

`buildUtc` and `tag` are **not** embedded in the binary; only the content hash is.
Two builds of the same content therefore differ in their sidecar's timestamp but
not in the executable's bytes.

## 5. Checking a copy you obtained

```
sha256sum <tf>/maps/cp_process_f12.bsp        # compare with section 1
sha256sum D:/TF2_Demo_Player/testdata/demos/*.dem   # compare with section 2.1
git -C <this repo> ls-tree -r --name-only HEAD | wc -l   # 595
bash build-identity-content.sh hash           # 4498475d... (docs do not enter the hash)
bash verify-fast.sh                           # 43 assertions, no oracle, no captures
```

## 6. Things that look missing but are present on purpose

| Path | Why it matters |
|---|---|
| `evidence/probe-baseline` (11 files) | frozen probe reports the chain diffs against |
| `evidence/corpus-calib` (26 files) | step 9's 24 frozen reports + summary; deleting them makes 48 assertions fail with `pinned_reports=0` |
| `testdata/fixtures` (14 files) | VMT nesting and instancebaseline wire fixtures the gates feed to probes |
| `tools/hostlock.sh` | the machine-wide build/capture mutex the evidence commands assume |
| `native/tools` (58 files) | the probe executables' sources -- the gates call the built binaries |

Everything else that the repository does not carry (6,135 `evidence/` files, 61
root `T*-2026-10` reports, `PROGRESS.txt`) is in the
`archive/full-history-2026-11-13` branch and in the 241 MB bundle recorded in
`docs/HANDOFF-MISSING.md` section 5.
