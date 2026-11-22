# The world lightmap atlas: three tails closed (task-38, 2026-11-21)

Date: 2026-11-22 (card written from the 2026-11-21 branch).
Branch `task/atlas38-lightmap`; the code landed on `82901898` ("lighting: close the three
world-lightmap atlas tails") and the counter split on `7e9df300` ("probe: name the two lightmap
drop counters apart and capture the HDR-only before-state").

Every number below is a line of a committed file under `evidence/atlas38-lightmap/`, and the
file is named next to it. The derived BSPs those readings were taken on are **not** committed
(one of them is a 59 MB copy of `cp_snakewater_final1`); the scripts that derive them are, so
the whole set is re-runnable from a clean checkout plus a build tree -- see section 6.

## 1. What the change does

`native/src/bsp_map.cpp` has three "give up and fall back" paths around the world lightmap
atlas. Each one used to give up silently. This card closes all three and names what is left.

1. **HDR-only maps are lit by lump 53, not by an average.** A map whose lump 8 (the
   tonemapped lighting page) is empty used to parse with no lighting page at all, so no face
   could be lightmapped and the whole world fell back to a single average colour. The page a
   face record is resolved in is now one binding, `lightPage` (`native/src/bsp_map.cpp:322`:
   `const Bytes& lightPage = lighting.empty() ? lightingHdr : lighting;`), so the offsets, the
   range test, the luxels written into the atlas, the sample count and the fallback average all
   read the same page. LDR wins whenever it is present -- which is every shipped map -- so for
   all of them the reading is byte-identical to what it was.
2. **A one-luxel clamp border around every packed patch, opt-in.** Off unless
   `TF2_NATIVE_LM_BORDER=1` (`native/src/bsp_map.cpp:979`). Bilinear filtering reads half a
   texel past the patch edge, and the luxel it finds there belongs to the *next* patch -- a
   different surface at a different exposure -- so every patch edge pixel blends two faces. A
   replicated ring makes the tap past the edge repeat the edge's own luxel. It is opt-in
   because it costs `2w + 2h + 4` luxels per patch and therefore moves atlas *heights*, and
   every reading pinned on an atlas height would otherwise be changing for free.
3. **Faces that did not reach the atlas are listed one per line, opt-in.** Off unless
   `TF2_NATIVE_LM_DROPS=1` (`native/src/bsp_map.cpp:983`). With the switch set, every refusal
   prints the face index, its material, its patch size in luxels and a sentence saying which
   check refused it; the listing is capped at 64 lines and the cap says how much it hid
   (`native/src/bsp_map.cpp:1079-1084`, `:1924-1931`). A map that refuses nothing prints
   nothing, so the switch is not noisy on a healthy install.

The fourth thing this branch did is a correction rather than a feature: **the two "did not
reach the atlas" counters are two different populations** and used to be read as one. They now
have separate names, a fourth `atlasReason` branch for the class the subtraction cannot see,
and section 4 below explains it.

## 2. Where it lives

| what | where |
| --- | --- |
| the page every face record is resolved in | `native/src/bsp_map.cpp:322` (`lightPage`) |
| lump 53 is optional, decoded on its own | `native/src/bsp_map.cpp:301-315` (`hdrDecoded` at `:314-315`) |
| "is lump 53 comparable to the page in use" | `native/src/bsp_map.cpp:327` (`hdrLumpUsable`, size test) |
| border switch, with its cost argument | `native/src/bsp_map.cpp:970-979` (`kBorder` at `:979`) |
| drops switch | `native/src/bsp_map.cpp:980-983` (`traceDrops` at `:983`) |
| listing buffer + total, 64-line cap | `native/src/bsp_map.cpp:1079-1084` |
| one face's lightmap test | `native/src/bsp_map.cpp:1129-1138` (`hasLightmap` at `:1137`) |
| `++lightmapFaceCount` -- only inside `hasLightmap` | `native/src/bsp_map.cpp:1234-1235` |
| refusal sentences (page missing / empty box / offset outside) | `native/src/bsp_map.cpp:1248-1264` |
| patch size with the border, shelf placement | `native/src/bsp_map.cpp:1265-1277` |
| the ring is copied from real luxels | `native/src/bsp_map.cpp:1291-1304` |
| HDR tone histogram (lump 53 read through lump 8 offsets) | `native/src/bsp_map.cpp:1342-1354` |
| atlas written, mode set, average fallback | `native/src/bsp_map.cpp:1868-1880` |
| `lightmapAtlasDroppedFaces` = shelf only | `native/src/bsp_map.cpp:1887-1888` |
| `lightRefusedBeforePacking` = everything else | `native/src/bsp_map.cpp:1898-1899` |
| the four `atlasReason` branches | `native/src/bsp_map.cpp:1900-1916` |
| the listing itself | `native/src/bsp_map.cpp:1924-1931` |

## 3. Numbers

### 3.1 Default path, six shipped maps

`evidence/atlas38-lightmap/readings-off.txt` (54 lines, one block per map):

| map | atlas | facesPacked / facesWithLightmap | facesDropped | atlasReason |
| --- | --- | --- | --- | --- |
| `cp_snakewater_final1` | 4096x1331 | 14988 / 14988 | 0 | all 14988 faces fit the 4096-wide atlas |
| `pl_upward` | 4096x1567 | 15032 / 15032 | 0 | all 15032 faces fit the 4096-wide atlas |
| `cp_process_final` | 4096x1179 | 13000 / 13000 | 0 | all 13000 faces fit the 4096-wide atlas |
| `pl_badwater` | 2048x1620 | 10100 / 10100 | 0 | all 10100 faces fit the 2048-wide atlas |
| `cp_badlands` | 4096x1162 | 13082 / 13082 | 0 | all 13082 faces fit the 4096-wide atlas |
| `koth_viaduct` | 2048x1848 | 12243 / 12243 | 0 | all 12243 faces fit the 2048-wide atlas |

Each block also carries `lightmapMode: source-equivalent`, `lightmapContract: luxel-index`,
`lightmapToneMap: ldr-clamp`, the sample count, `lightmap.state: active`, the
`trianglesWithLightUv` count and `uvAllInRange: true`. No face is lost on any of the six, on
the default path, with both switches off -- which is the point of the next table.

### 3.2 The border A/B: only the atlas height moves

`evidence/atlas38-lightmap/atlas-border-ab.txt` (40 lines, four maps x 2 settings):

| map | atlas border=0 | atlas border=1 | facesPacked | facesDropped | atlasReason |
| --- | --- | --- | --- | --- | --- |
| `cp_snakewater_final1` | 4096x1331 | 4096x1670 | 14988 both | 0 both | identical |
| `pl_upward` | 4096x1567 | 4096x1842 | 15032 both | 0 both | identical |
| `cp_process_final` | 4096x1179 | 4096x1438 | 13000 both | 0 both | identical |
| `pl_badwater` | 2048x1620 | 2048x2012 | 10100 both | 0 both | identical |

That is the whole contract of the switch: the packed set does not change, the reason sentence
does not change, only the height of the sheet the same faces are packed into. Since the switch
is **off by default**, the atlas sizes pinned elsewhere in this tree (for example
`world-material-check.sh`'s `4096x1331`) do not move.

### 3.3 HDR-only: the before-state is measured, not remembered

`evidence/atlas38-lightmap/hdr-only.txt` is this build on the derived copy (lump 8 header
zeroed, lump 53 intact -- `hdr-only-map-build.txt`: `lump8 (11662840, 11174748) lump53
(22837588, 11174748)` -> `patched lump8 (0, 0)`).

`evidence/atlas38-lightmap/hdr-only-before-after.txt` feeds the **same bytes** to two builds:

| reading | base `390a7c76` probe | this branch |
| --- | --- | --- |
| `lightmapMode` | `unavailable` | `source-equivalent` |
| `lightmapSamples` | 0 | 2793687 |
| `lightmapFaces` / `lightmapPackedFaces` | 0 / 0 | 14988 / 14988 |
| `atlas` | `0x0` | `4096x1331` |
| `lightmap.state` | `unavailable` | `active` |
| `trianglesWithLightUv` | 0 | 199922 |
| `uvAllInRange` | `false` | `true` |
| `atlasReason` | the bsp declares no lightmap faces | all 14988 faces fit the 4096-wide atlas |

The same file ends with the **unmodified** `cp_snakewater_final1` on both builds, and those two
blocks are identical (`source-equivalent` / 4096x1331 / 14988 / 199922 / `true`). So the
difference in the table above comes from the copy being HDR-only, not from any other difference
between the two builds.

### 3.4 The drop listing

`evidence/atlas38-lightmap/drops.txt`. The first section is the truncated page copy (lump 8
length / 3 -- `drops-map-build.txt`: `lump8 offset 11662840 length 11174748` -> `patched lump8
length -> 3724916 (divisor 3)`), which puts a lot of faces' `lightofs` outside the page:

    lightmap atlas drops: 8614 refused face(s), 64 listed (atlas 2048 wide, 8614 face(s) carried no usable lightmap and were refused before packing)
      face 6613 (WOOD/WALL020B) 32x22 luxels: the light offset does not land inside the lighting page
      ...
      ... 8550 more not listed

8614 refused, 64 printed, and the last line says exactly how many the cap hid -- 8614 - 64 =
8550. The second section is eight healthy shipped maps with the switch **on**: every one prints
`0 refused face(s), 0 listed` (`cp_badlands` 13082, `cp_dustbowl` 16832, `cp_gorge` 8018,
`koth_viaduct` 12243, `pl_badwater` 10100, `pl_goldrush` 21301, `ctf_2fort` 14333,
`cp_well` 19843 faces). A switch that only speaks when it has something to say.

### 3.5 Self-test

`evidence/atlas38-lightmap/selftest.txt` is one `world_material_probe --self-test` run. The
lightmap tail is 11 sub-items, all true (`:10`): `lightmapTailSelfTest`, `hdrOnlyPacks`,
`hdrOnlySourceIsHdrPage`, `noLightingPageStaysUnavailable`, `ldrPageWinsWhenBothPresent`,
`borderGrowsAtlas`, `borderClampsEdge`, `borderKeepsPatchBody`, `borderShiftsUv`,
`dropsListed`, `dropsIdentifyDataClass`. The same file holds the two synthetic drop fixtures the
listing is proved on (`:4-8`): `drops-saturated` (17 faces, 1 shelf overflow, atlas 2048x2048,
`face 16 (fixture_mat16) 512x512 luxels: shelf overflow: the next row would cross the atlas
bottom`) and `drops-badoffset` (0 faces with a lightmap, 1 refused early, `face 0 (fixture_mat0)
2x2 luxels: the light offset does not land inside the lighting page`).

The whole `--self-test` on the merged head is committed separately, with its own tree head and
exit code, as `evidence/world-material-selftest.txt` -- all seven objects, 75 checks, no
`:false`. That run proves the probe's self-tests pass on the merged tree; it does **not**
re-derive the readings in this card, which are separate runs against real map files.

### 3.6 The default path did not move

`evidence/atlas38-lightmap/off-path-identity.txt`: the same worktree built twice (HEAD vs this
branch). `world_geometry_probe` on `pl_odyssey` reports the same seven lightmap/geometry lines
byte-for-byte (`GEOMETRY-IDENTICAL-TO-HEAD`), and the captured frame is byte-identical
(`a5edf32d893ff39303270139f529eafb`, 2582406 bytes, on both). The same file records an honest
caveat: `visual-baseline-check.sh`'s pins had **already** drifted on this install at HEAD
(105700/47232/4877/30020/6225/1231/6556/1297 measured against pinned
105310/46912/4871/29916/6215/1230/6546/1296), so that gate cannot arbitrate this change and
that file says so instead of quoting it as a pass.

## 4. The two "did not reach the atlas" counters are not the same population

This is the part that was wrong before and is now named apart. Two numbers that look like the
same thing measure two different sets of faces, and the smaller one can be 0 while the larger
one is not -- which used to read as a contradiction (see `selftest.txt:4`: `shelfDropped=1` on
the same line as `1 refused face(s)`, and `:7`: `shelfDropped=0` on the same line as
`1 refused face(s)`).

| name | definition | who is in it |
| --- | --- | --- |
| `lightmapAtlasDroppedFaces` (the probe prints `shelfDropped=`) | `lightmapFaceCount - lightmapPackedFaces` (`native/src/bsp_map.cpp:1887-1888`) | only **shelf overflow**: the patch is wider/taller than the atlas, or the next row would cross the atlas bottom (`native/src/bsp_map.cpp:1271-1277`) |
| `N refused face(s)` in the listing (= `lightDropTotal`) | every refusal in this parse, at whatever step it happened (`native/src/bsp_map.cpp:1084`, incremented at `:1363`) | shelf overflow **plus** every face refused by `hasLightmap` before counting: no lighting page at all, samples box empty or larger than 512x512, `lightofs` outside the page (`native/src/bsp_map.cpp:1254-1264`) |

Why the second class cannot appear in the first: `lightmapFaceCount` is only incremented
**inside** the `hasLightmap` branch (`native/src/bsp_map.cpp:1234-1235`), and a face whose own
record fails `hasLightmap` (`:1137`) is refused before that line is ever reached. So the
subtraction `lightmapFaceCount - lightmapPackedFaces` can never see it. `lightDropTotal` is
incremented unconditionally at the refusal point (`:1363`), and the difference between the two
is exactly that class (`lightRefusedBeforePacking`, `:1898-1899`). `lightmapAtlasReason` has a
fourth branch that names it rather than blaming the shelf (`:1905-1907`:
`N face(s) carried no usable lightmap and were refused before packing`).

## 5. What this does not do

- **The border is not on by default**, and nothing here changes any pinned atlas size. Turning
  it on moves atlas heights (section 3.2) and nothing else; a reader who wants it must ask for
  it with `TF2_NATIVE_LM_BORDER=1`.
- **This is not a gate.** Nothing in this card runs under the host lock or produces a pass/fail
  exit code: it parses BSP files and prints. Only builds and captures need the lock. The two
  switches are diagnostics, off by default, so a normal run reads exactly as before.
- **The HDR-only and short-light maps are derived, and the derived copies are not committed.**
  They live in `.scratch/` (the source map is 59 MB); the scripts that derive them are
  committed (`tools/make_hdr_only.py`, `tools/make_short_light.py`, `tools/collect.sh`), and
  the derivation's own printout is committed (`hdr-only-map-build.txt`,
  `drops-map-build.txt`), so the copies can be re-derived but not diffed against a stored blob.
- **The listing is capped at 64 lines.** `8614 refused, 64 listed` is that cap working as
  designed, not a truncated run: the count is kept in full so the `... N more not listed` line
  says exactly what it hid (`:1928-1930`).
- **The average-intensity fallback still exists** (`native/src/bsp_map.cpp:1873-1880`). It is
  now reached only when there is no lighting page in either lump, which is the case that
  genuinely has nothing to pack -- an HDR-only map no longer falls into it.
- **No claim is made about how the frame looks.** This card is about which faces carry which
  luxels and which faces are refused; the visual check on the default path is the md5 identity
  in section 3.6, and nothing else.

## 6. How to reproduce

`evidence/atlas38-lightmap/tools/collect.sh` regenerates **every** text in that directory from a
clean checkout plus a build tree (`native/build-nmake/world_material_probe.exe`):

```bash
bash evidence/atlas38-lightmap/tools/collect.sh
# to re-run the HDR-only before/after as well, pass a probe built from the base commit:
bash evidence/atlas38-lightmap/tools/collect.sh --with-hdr-base /path/to/base/world_material_probe.exe
```

| script | what it does |
| --- | --- |
| `tools/make_hdr_only.py` | derives the HDR-only copy (lump 8 header zeroed, lump 53 intact) into `.scratch/hdr-only-cp_snakewater_final1.bsp` |
| `tools/make_short_light.py` | derives the short-lighting copy (lump 8 length / 3) into `.scratch/short-light-cp_snakewater_final1.bsp` |
| `tools/collect.sh` | runs both, then collects `selftest.txt`, `readings-off.txt`, `atlas-border-ab.txt`, `hdr-only.txt`, `drops.txt` (and `hdr-only-before-after.txt` with `--with-hdr-base`) |

The scripts read the game install at
`D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` and write the derived copies to
`.scratch/`; nothing needs the host lock. `collect.sh` needs `world_material_probe.exe` to
exist and says so if it does not.

## 7. Where the evidence is

`evidence/atlas38-lightmap/` -- one file per claim above, with the file table in that
directory's `README.md`. The probe self-test transcript for the whole probe (all seven objects,
75 checks) is `evidence/world-material-selftest.txt`. Build identities for the executables
these readings came from are collected in `evidence/BUILDS.md`.
