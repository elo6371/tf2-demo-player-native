# T9 — Resource discovery and startup contract

**Owner:** AI-D · **Branch:** `task/resource-discovery-startup`
**Worktree:** `D:/TF2_Native_Worktrees/resource-discovery-startup`
**Base:** `dadb17c` ("docs: record T6-T8 integration and visual release blocker", the final T5 line)
**Anchors:** source patch hash `a584a98` + `3b8e67e`; readings below are from binaries built from those two commits, not from an exe hash.

T9 owns two T4 findings that sit next to each other in `stability-run-matrix.ps1`
(lines 45–77) and one question the plan asked separately: what does releasing a
startup depend on when there is no installer?

The two findings, in T4's own words:

> …the product titles POV demos with a `POV ticks=` prefix while the release
> gate's `load_title` only parses `SourceTV ticks=`; the soak end-tick assertion
> would then read 0 and fail unconditionally. Extending the gate's parser to
> accept the POV prefix was rejected: it is the judgement layer, and this task
> does not edit it to make a run pass.

> gullyscout.dem / processdemo.dem (SourceTV, 120933 commands) were rejected
> because their maps' BSPs live in `tf\download\maps\` while the product
> resolves the map only under `tf\maps` (main.cpp: assets.tfDirectory /
> "maps"), so both played with "BSP: invalid/BSP header is invalid" and a
> fallback material path…

Both are fixed, each where its contract lives. Section 4 says why fixing the
second one is not the edit T4 refused.

---

## 1. What the product did before, measured

On the base line the product built exactly one path for a map:

```cpp
std::filesystem::path mapPath = std::filesystem::path(L"maps")
  / std::filesystem::path(<map name> + L".bsp");
auto mapBytes = readFile(assets.tfDirectory / mapPath);
if (mapBytes.empty()) { /* ask the VPKs for the same string */ }
```

`tf/download/maps` was never consulted. On this install that is not a corner
case: `cp_gullywash_f9.bsp` and `cp_process_f12.bsp` exist **only** under
`tf/download/maps`, and `ls tf/maps/cp_gullywash_f9.bsp` reports no such file.

## 2. What it does now, and the counterfactual

`native/src/map_lookup.cpp` (new) states the search as a list, and
`native/src/main.cpp` walks it:

```
map: lookup map=cp_gullywash_f9 store=download-maps
     selected=download/maps/cp_gullywash_f9.bsp bytes=13231135 reason=found
```

`store` is reported, not assumed. `store=none` is a reading with a `reason`, not
silence — and the two refusals are different readings on purpose:

| case | what was asked | store | selected | bytes | title's BSP segment |
|---|---|---|---|---|---|
| installed-map | `cp_snakewater_final1` (in `tf/maps`) | `maps` | `maps/cp_snakewater_final1.bsp` | 59590420 | `200000 tris` |
| download-gully | `cp_gullywash_f9` (only `download/maps`) | `download-maps` | `download/maps/cp_gullywash_f9.bsp` | 13231135 | `104832 tris` |
| download-process | `cp_process_f12` (only `download/maps`) | `download-maps` | `download/maps/cp_process_f12.bsp` | 18558778 | `70685 tris` |
| missing-map | `cp_t9_no_such_map` (nowhere) | `none` | `-` | 0 | `invalid/BSP header is invalid` |
| traversal-map | `..\..\t9_probe` | — refused before any store | — | — | — |

**Counterfactual (the strongest single piece of evidence here).** The `download/maps`
candidate was deleted from `map_lookup.cpp`, `tf2_demo_native` was rebuilt, and
`download-gully` was re-run (`evidence/t9-resource-discovery-revert-product`):

```
map: lookup map=cp_gullywash_f9 store=none selected=- bytes=0 reason=not found in maps, download/maps or a vpk
BSP segment: invalid/BSP header is invalid
```

That is T4's symptom, reproduced by reverting the fix and by nothing else. The
source was then restored and rebuilt.

## 3. The search order, and why that order

1. `maps/<name>.bsp` — loose, in the TF root
2. `download/maps/<name>.bsp` — loose, pushed by a server
3. `maps/<name>.bsp` — packed inside a VPK

Loose before packed, because that is the order every other asset in this product
resolves in. The install's own copy before the server-pushed one, because
`download/` can be a *different revision* of the same map, and silently
preferring the pushed copy would make playback depend on which servers the
operator happened to join. A VPK cannot contain `download/`, so there is no
fourth store.

The order is asserted by `map_lookup_probe`, which links **only**
`map_lookup.cpp`: a green run cannot be helped by anything installed on this
machine. It also asserts `packed` on the VPK slot only, determinism across calls,
six accepted names, and eight refused ones (empty, `/`, `\`, `:`, `..`, leading
dot, trailing dot, space, non-ASCII byte — 9 refusals, 8 of them distinct
failure classes).

### The name rules are two rules, not one

`demo_header.cpp`'s `validMapName` (alnum, `_`, `-`, ≤128 bytes) runs at header
parse and rejects the whole demo; `mapNameIsUsable` is that plus `.`, with `..`
and leading/trailing dots refused separately so the reason can name which one it
was. Anything `validMapName` accepts, `mapNameIsUsable` accepts, so **from the
demo-header path the lookup's refusal is unreachable** — it is the second gate,
not the first.

The `traversal-map` case pins what actually happens rather than what would be
flattering: the hostile name never appears in any lookup reading, and the
product loads its documented default, `2koth_abbey`. Claiming the lookup had
refused it end-to-end would be a claim about a defence this reading does not
reach. That relationship is now written into `map_lookup.cpp` so nobody "fixes"
the difference by loosening `validMapName`.

## 4. The POV title: who owns the contract

The product writes `<map> <label> ticks=<n>`, where the label is one of
`SourceTV`, `POV`, `POV (heuristic)` or `unknown` (classifyDemoRecording).
`load_title` matched the literal string `SourceTV ticks=`.

The consequence is not a parse error. It is an **absent reading**: `T[ticks]`
was never set, and the end-tick assertion read it as `0`. Every POV case failed
unconditionally and the failure looked like a stalled demo — a defect in the
instrument, reported as a defect in the product.

**The product title owns the format; the gate owns the tolerance.** `load_title`
now takes the whole `Demo: ` segment and parses the `ticks=` **key** out of it,
so it reads every label the classifier can emit. Measured on the captured
titles of this run, with the gate's own function lifted out of
`stability-release-check.sh` at run time (not a copy of it):

| title | label read | ticks read |
|---|---|---|
| `cp_snakewater_final1 POV ticks=5374 …` | `POV` | `5374` |
| `cp_gullywash_f9 SourceTV ticks=120936 …` | `SourceTV` | `120936` |
| a title with no `Demo:` segment | — | absent (not `0`) |

The product's title format is unchanged.

**On T4's decision not to touch this.** T4 refused to *extend the parser to
accept the POV prefix*, on the grounds that the parser is the judgement layer
and must not be edited to make a run pass. This change is not that edit: it does
not add POV to a list, it removes the label from the pattern altogether, so the
parser now accepts **every** label instead of one. A gate that accepts every
supported form cannot be bent toward a particular demo. It is still an edit to
the judgement layer, so it is flagged here for the coordinator rather than
presented as settled: if the release gate is meant to reject unknown labels,
that belongs in an explicit allow-list assertion, not in a pattern that silently
returns nothing for three of the four labels the product emits.

## 5. The TF-root contract is unchanged and re-verified

T9 did not touch root selection. `stability-root-check.ps1` was re-run on the
final binaries (`evidence/t9-root-contract-202610171145-forward`), 4 rows:

```
explicit-valid-root    connected_root=D:\…\Team Fortress 2\tf  vpk_entries=104137
explicit-missing-root  connected_root=(empty)                  vpk_entries=-1
explicit-empty-root    connected_root=(empty)                  vpk_entries=-1
no-root-auto-search    connected_root=D:\…\Team Fortress 2\tf  vpk_entries=104137
ROOT-CONTRACT=PASS
```

An explicit `--tf-root` is still the only root, a missing root and an existing
empty directory still do not fall back to Steam discovery, and no flag still
auto-searches. `AssetRoot::valid()` already requires materials, models and maps
to each have a loose asset or a matching `*_dir.vpk`, which is what makes an
empty directory invalid rather than merely unhelpful.

## 6. Gates run, and what they said

| gate | command | result |
|---|---|---|
| map lookup (pure) | `map_lookup_probe.exe` | `checks=26 bad=0` |
| resource discovery, forward | `bash resource-discovery-check.sh --forward evidence/t9-resource-discovery-20261017113652-50508-forward` | `cases=5 checks=34 mutations=0` **PASS** |
| resource discovery, mutation | same, `--mutation` | `cases=5 checks=41 mutations=2` **PASS** |
| product counterfactual | revert `download/maps`, rebuild, re-run `download-gully` | symptom restored, then reverted |
| TF-root contract | `powershell -File stability-root-check.ps1` | `ROOT-CONTRACT=PASS` |
| install layout smoke | `cmake --install … --prefix .t9-tmp/stage` then `install_smoke_probe.ps1` | `install_layout=ok tf2_assets_packaged=0` |

The probe's `checks=26` is **pinned** in the gate: a probe that silently dropped
the download-maps assertions would otherwise report a green, smaller run.

Both mutations perturbed real source and were caught:

- **M1** — delete the `download/maps` candidate from `map_lookup.cpp`, rebuild
  the probe: `bad=2 of 20`. Restored, `cmp` byte-identical, rebuilt, green again.
- **M2** — restore the `SourceTV ticks=` label coupling in
  `stability-release-check.sh`: the POV reading goes back to absent (`ticks=[]`).
  Restored, `cmp` byte-identical.

M1's first attempt was **not** caught, and that is worth recording: the mutation
build failed (`build rc=1`) because `nmake` is not on `PATH` in this shell, so
the probe ran a stale binary and reported `bad=0`. A mutation whose build fails
is a false pass, not a near miss. The gate now sets the same three environment
variables `build-cmake.sh` sets, and asserts `M1 build succeeded` before it
reads the probe.

**NOT RUN:** `verify-all.sh`, `verify-all.sh --quick`, and any full chain — the
cost rule for T6–T9 reserves those for one release-boundary run. T9 ran only its
focused probe, its own gate, the root gate and the install-layout smoke.

## 7. Two things found on the way, neither of them the point of T9

### 7a. The merged line did not link

`bash build-cmake.sh` failed four times on the T9 base, each on a different
target and each one layer deeper:

```
demo_open_probe     LNK1120 x4   buildSoundTimeline -> audio_timeline
demo_open_probe     LNK1120 x3   audio_timeline     -> audio_resource
presentation_probe  LNK1120 x2   readSoundResource  -> VpkArchive
viewmodel_fov_probe LNK1120 x4   same class, six targets further down
audio_resource.cpp  C2589        std::min without NOMINMAX (line 196)
```

The audio layer moved `buildSoundTimeline` into `demo_header.cpp`; fourteen
targets compile that file and none carried the closure it now needs
(`audio_timeline.cpp`, `audio_resource.cpp`, `vpk_archive.cpp`, `LzmaDec.c`,
`winmm`, `msacm32`, `NOMINMAX`). The merged line therefore had **no buildable
state**: every owner's next gate would have failed at the build, not at an
assertion, and the failure would have read as a product defect.

Repaired once, as a named closure, in its own commit (`3b8e67e`) so it can be
reverted or reviewed apart from the T9 work it unblocked. `demo_header.cpp` is
byte-identical to the base and every changed target block was untouched at the
base, so this is a merge break that predates T9. After it: `cmake_build_rc=0`,
`exe_count=34`.

### 7b. The title harness read somebody else's window

The first capture run produced a `installed-map` title whose HUD read
`state=playing tick=1699 demo=autorecord_2026-07-02_13-26-46.dem` — a different
demo, playing, from a process that was not mine (every case here launches
`--start-paused`). `title-capture.ps1` scanned for the first window of class
`TF2DemoNativeRenderer`, and `EnumWindows` order is not "the process I just
started"; a second player instance was running on this host. It then also posted
`WM_CLOSE` to that foreign window, which would have ended another task's run.

Fixed by adding an owner-filtered entry point to the scanner **lifted from the
root gate** (still one enumeration, not a second copy), refusing any window whose
owner is not this process's child, and never posting to a window that is not
ours. Every case now carries `window-pid.txt` / `child-pid.txt` /
`foreign-windows-seen.txt`, and the gate asserts `window_pid == child_pid`. The
re-run recorded `foreign=1` on all five cases: the foreign instance was there,
and was left untouched.

## 8. Packaging: what exists, what a release needs, what is NOT VERIFIED

There is no installer. No Inno Setup / WiX / MSI / self-extracting archive, no
CPack configuration, no packaging script anywhere in the tree.

There **is** a staging rule — `install(TARGETS tf2_demo_native
native_install_smoke_probe …)` plus `tools/install_smoke_probe.ps1`, all into
`.` — and it works: `cmake --install` produced a three-file directory, and the
existing smoke passed on it:

```
install_executable=1 tf_root_missing_prompt=1 warp_fallback=1 feature_level=b100
install_layout=ok tf2_assets_packaged=0
```

Prerequisites a real installer would have to satisfy, taken from what the
product actually depends on rather than from a template:

1. **TF2 assets are not packaged and cannot be.** `tf2_assets_packaged=0`. The
   product ships no materials, models or maps; `AssetRoot::valid()` requires all
   three. An installer must locate a TF install or prompt for one — it cannot
   supply one.
2. **Startup with no TF root must be a prompt, not a crash.** The smoke probe
   asserts exactly that (`tf_root_missing_prompt=1`), and that a machine with no
   usable GPU still starts (`warp_fallback=1`, `feature_level=b100` = 11_0).
3. **System libraries only.** `d3d11`, `dxgi`, `d3dcompiler_47`, `winmm` are
   checked by the smoke script. The product also links `msacm32`
   (audio_resource.cpp's ACM path); the script's list does not include it.
   Recorded here rather than changed — see the boundary below.
4. **The MSVC runtime is statically linked** (`CMAKE_MSVC_RUNTIME_LIBRARY
   MultiThreaded`), so no redistributable has to ship.
5. **Per-user state must not live in the install directory.** Settings and cache
   go to `FOLDERID_LocalAppData` (`%LOCALAPPDATA%\TF2 Demo Player\cache`, visible
   in every title captured here). An install under Program Files therefore needs
   no write access to its own directory — and an uninstall that removes it will
   leave per-user state behind.

**NOT VERIFIED — there is no package to verify it on:** install to a fresh
directory, start, open a demo, close, restart, uninstall; relocation of an
installed tree; clean-machine first-run; multi-user or non-admin install;
upgrade over a previous version. Staging a development directory into `.t9-tmp`
is not an install-package acceptance and is not claimed as one.

T9 did not create an installer framework, did not add a packaging script, and
did not touch `install_smoke_probe.ps1`.

## 9. Boundaries kept

- No audio or renderer implementation was changed.
- No installer was created; installer behaviour is reported as NOT VERIFIED.
- The explicit-root contract was not weakened: no silent fallback from an
  explicit `--tf-root` to Steam discovery, and an existing empty directory is
  still not a valid root.
- No full chain was run.
- The VPK store's order and `packed` flag are asserted purely; there is no
  packed-only map on this install, so no end-to-end packed-map case is claimed.

## 10. One observation for whoever owns BSP parsing

`cp_snakewater_final1` reports `BSP: 200000 tris`, which is exactly
`BspParser::parse`'s `triangleLimit` (default 200000,
`if (map.triangles.size() >= triangleLimit) return;`). The map parses valid, but
its geometry is truncated at the cap. Not a T9 finding and not changed here;
noted because snakewater is a soak input elsewhere in the matrix.

---

### Files

| file | change |
|---|---|
| `native/include/map_lookup.h`, `native/src/map_lookup.cpp` | new — the search plan and the name rule |
| `native/tools/map_lookup_probe.cpp` | new — pure assertions on the order and the name rule |
| `native/src/main.cpp` | walk the plan; emit `map: lookup …` |
| `stability-release-check.sh` | `load_title` parses `ticks=` by key, not by label |
| `title-capture.ps1` | new — capture a title, owned-window only |
| `resource-discovery-capture.ps1` | new — the product runs of the gate |
| `resource-discovery-check.sh` | new — the gate |
| `native/CMakeLists.txt` | `map_lookup.cpp` + `map_lookup_probe`; plus the link closure (`3b8e67e`) |

**Commits:** `a584a98` (T9) → `3b8e67e` (build-graph repair)
