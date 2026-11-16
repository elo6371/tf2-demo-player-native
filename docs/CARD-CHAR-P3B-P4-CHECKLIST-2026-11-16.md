# card-char P3b / P4 executable checklists (2026-11-16)

Purpose: the two checks I owe in the 13:50 window, written so that each one has an input, a
command, an expected value, and a file to blame when it fails. Read-only prep done at 13:26;
nothing here ran a product binary or took a lock.

## 0. Window mechanics (applies to every line below)

- Lock: `bash tools/lockrun.sh card-char -- <cmd>`. Success looks like
  `VERIFY-LOCK=ACQUIRED owner=card-char pid=<n> dir=.../.scratch/verify-all.lock` first and
  `VERIFY-LOCK=RELEASED dir=...` last. `VERIFY-LOCK=FAIL owner=card-ui ...` means card-ui is
  still inside its window: wait, do not delete the lock directory, do not retry a second time.
- lockrun also fails the run with rc=97 `LOCKRUN=TRACKED-DIRTY` if `native/tests/fixtures` or
  `evidence` changed during it (D40/D49). Neither P3b nor P4 writes those; a 97 means something
  else in the tree moved and is itself a finding.
- Product identity, recorded before and after every run (hard constraint (a)):
  `sha256sum native/build-nmake/tf2_demo_native.exe` plus size and mtime.
  **Baseline at prep time: 81af3a4aa35352a8b844a5b61d604833595d0580ae28019d7c447ddd331d741f,
  3,045,376 B, mtime 2026-11-16 13:20:24** (card-ui's rebuild; the old 26bff436 is retired).
  Neither check rebuilds or launches the product, so this sha must be identical before and after;
  any change means someone ran a build inside my window and the readings must be rebinned.
- Demo (unchanged): `D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem`,
  sha256 7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3.
- Environment paths handed to Windows binaries must be Windows form (`D:/...`). MSYS converts
  argv but not environment variables: a POSIX `/d/...` in `TF2_NATIVE_TRACE` once produced a
  0-byte trace (2026-11-15).

## 1. P3b — the audio gap is the 4096-message payload cap

Claim under test: of 35278 declared sound events, only 5528 are ever decoded, and that is the
4096-entry raw-payload cap (`native/src/demo_header_sound.cpp:42`), not decoder failure.

### B0 inputs
- probe source at HEAD d4729d67 carries three counters after `sound_misses`:
  `sound_messages`, `sound_dropped`, `sound_decoded` (`native/tools/demo_open_probe.cpp`).
- probe binary: `native/build-nmake/demo_open_probe.exe`, sha256
  5434013957710e7f8ade051b6596f58abf330001f1098931ae77212216a7d197 (built 12:56).
- evidence root: `D:/_shared/EXEC-2026-11-16/INST3/p3b/rerun/`.

### B1 rebuild is a no-op
```
bash tools/lockrun.sh card-char -- bash build-target.sh demo_open_probe
```
Expected: rc=0, no compiler errors, probe exe sha unchanged (same source as the last build, so
NMake relinks nothing). Failure attribution:
- compile error → `native/tools/demo_open_probe.cpp` (my instrumentation).
- sha changes → the source is not what d4729d67 recorded, or the build tree was dirty: stop and
  record both shas rather than proceeding.

### B2 the scan
```
bash tools/lockrun.sh card-char -- native/build-nmake/demo_open_probe.exe --scan \
  D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem
```
Expected substring (all on the one `path=<wide-path>` line):
`temp_events=68133 temp_failures=0 particles=986 explosions=1873 firebullets=4307 projectiles=1538`
`sounds=35278 sound_failures=20923 sound_matches=5482 sound_misses=46 sound_messages=25017 sound_dropped=20921 sound_decoded=5528`

Arithmetic assertions that must be printed by the run report:
- A1 `25017 - 20921 = 4096` exactly → the cap is saturated (nothing else eats messages).
- A2 `20923 - 20921 = 2` → only two genuine mid-message early breaks.
- A3 `5482 + 46 = 5528` → decoded = matched + unresolved, i.e. the product's read/schedule split.
- A4 `68133 >> 16384` → the temp-entity list is truncated elsewhere (D76), independent of audio.

Failure attribution, by counter:
| wrong value | blame |
|---|---|
| `sound_messages` ≠ 25017 | call site `native/src/demo_header.cpp:180` (which tick seeds each message) or the `++soundMessageCount` at `demo_header_sound.cpp:125` |
| `sound_dropped` ≠ 20921 | cap branch `demo_header_sound.cpp:42-52` (payload bits limit / 4096 size limit) |
| `sound_decoded` ≠ 5528 | per-message loop `demo_header_sound.cpp:65-118`, seed at :66, break at :69-118 |
| `sound_matches`/`sound_misses` | `buildSoundTimeline` `demo_header_sound.cpp:132-168` (read :136, unresolved :141, unplaceable :147) |
| `temp_events` ≠ 68133 | `native/src/demo_header_events.cpp` readTempEntities, cap at :310; or the demo arg is not the pinned file |
| probe prints no `sound_messages` at all | stale binary: the exe being run is not the one built from d4729d67 (check path and sha) |

### B3 product-side cross-check (documentary, no product run)
The product trace line already committed in `docs/N21-AUDIO-INVENTORY-2026-11-16.md` §8 reads
`read=5528 unresolved=46 scheduled=5482`, which is exactly A3 read from the other side
(`native/src/main.cpp` audio summary block). I re-read the stored trace only if the evidence copy
is still on disk; I do not re-run the product for this check, because the mechanism is fully
determined at probe level and a product capture costs a window.

### B4 artifacts
`scan.txt` (full stdout), `probe.sha256`, `product-before.sha256`, `product-after.sha256`,
and a one-paragraph `verdict.md` stating the four assertions with their arithmetic.

## 2. P4 — Media Foundation export capability

Claim under test: this host can produce an H.264 + AAC MP4 through the SinkWriter from RGB32
frames, deterministically, without a separate NV12 converter.

### D0 inputs
- probe binary: `D:/_shared/EXEC-2026-11-16/INST3/mf-p4/mf_export_probe.exe` (built with cl.exe
  from `native/tools/mf_export_probe.cpp`; not a CMake target, so there is nothing to rebuild).
  Record its sha256 first; a different sha from the 13:00 run means the binary was replaced.
- evidence root: `D:/_shared/EXEC-2026-11-16/INST3/mf-p4/rerun/`.

### D1 the probe does not create its output directory
```
mkdir -p D:/_shared/EXEC-2026-11-16/INST3/mf-p4/rerun
```
This is the 13:00 failure reproduced: without it, `MFCreateSinkWriterFromURL` returns
`0x80070003` (ERROR_PATH_NOT_FOUND) and Q3/Q4/Q5 cascade to `probe_rc=4`.

### D2 the run
```
bash tools/lockrun.sh card-char -- D:/_shared/EXEC-2026-11-16/INST3/mf-p4/mf_export_probe.exe \
  --out D:/_shared/EXEC-2026-11-16/INST3/mf-p4/rerun
```
Expected: rc=0, and these six verdict lines
```
[Q1] ANSWER: h264EncoderPresent=yes (filtered-count=4)
[Q2] ANSWER: aacEncoderPresent=yes (filtered-count=1)
[Q3] ANSWER: bothStreamsAdded=yes
[Q4] ANSWER: rgb32Accepted=yes chosenInput=MFVideoFormat_RGB32 needsOwnNv12Converter=no
[Q6] ANSWER: streamCount=2 videoStream=1 audioStream=0 firstFrameDecoded=yes
[determinism] sameByteSize=yes (sha256 compared outside this process)
```
plus `[throughput] ok=yes frames=100 ...` and `=== probe done ===`.
Then the check the probe does not do itself:
```
sha256sum rerun/det_a_2s.mp4 rerun/det_b_2s.mp4
```
Expected: equal, and equal to 57e3b37d9d1403cb026f95049139256b7c5db7fe2306b65e5440307c56213d40.

Failure attribution:
| symptom | blame |
|---|---|
| `hr=0x80070003` at MFCreateSinkWriterFromURL, probe_rc=4 | caller path: `--out` directory absent (probe never creates it) — not a capability gap |
| Q1 not yes / filtered-count=0 | host Media Foundation H.264 MFT absent (machine component), not our code |
| Q2 not yes | host AAC MFT absent |
| Q3 `bothStreamsAdded=no` | SinkWriter AddStream order/format in `runProbe` (`native/tools/mf_export_probe.cpp`) |
| Q4 `needsOwnNv12Converter=yes` | real finding for the export implementation (an NV12 conversion step becomes mandatory), not a probe failure |
| Q6 `firstFrameDecoded=no` | readback path in the probe (`readback()`), or the written file is malformed |
| det_a/det_b shas differ | encoder nondeterminism on this host; record which MFT the chosen encoder is (Q1/#0..#3) and report as a finding, do not call it a pass |
| rc=8 | `stats.failure` non-empty or readback failed: read `rerun/probe.log` |

### D3 scope note that must survive into the report
This proves the **capability exists**; it does not prove the product exports anything. N27's
second checkbox (in-product fixed-step render, offline audio, range/cancel, disk/encoder failure
recovery, exit 13 on missing components) is still unimplemented, and a product implementation
needs a product rebuild (lead's call).

## 3. Hazards recorded during prep

- `tools/lockrun.sh` is CRLF in the worktree (33 CRs) while its HEAD blob is LF; it still runs,
  tested in an isolated copy — it contains no backslash line continuations, so the D93 failure
  mode (a `\` followed by CR is not a continuation) cannot hit it today. Do not add
  continuations to it until the tree normalizes the file; `.gitattributes` carrying
  `/*.sh text eol=lf` is still an uncommitted change owned by someone else.
- The repo lock at 13:23 was owned by card-ui (`.scratch/verify-all.lock/owner`); the /d/_shared
  hostlock is a different lock and is FREE. Only `tools/lockrun.sh` matters for this window.
- Dirty tree at prep time (not mine, do not stage): `.gitattributes`, `docs/GATE-INPUT-REPLACEMENT-2026-11-16.md`,
  `docs/N-STATUS-2026-11-14.md`, `docs/N17-N20-CHAR-COVERAGE-2026-11-15.md`,
  `docs/PROJECT-NEXT-TASKS-2026-11-14.md`, `docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md`,
  `native/src/main.cpp`, `source-tv-coverage-check.sh`, plus untracked
  `docs/PROJECT-EXECUTION-ROADMAP-2026-11-15.md`, `docs/PROJECT-HANDOFF-2026-11-14.md`.
