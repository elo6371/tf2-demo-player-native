# T31 evidence inventory

Frozen readings for the audio-content-completeness task. Every file here is
copied out of a git-ignored run directory by `collect.sh` and hashed in
`SHA256SUMS`; the report `T31-AUDIO-CONTENT-2026-10-20.md` cites them.
The raw traces and BMP frames stay on disk (`evidence/fast/audio-content/`)
because they are machine output that the next run overwrites.

| File | What it is | Where the gate reads it |
|---|---|---|
| `content-inputs.txt` | the pinned cp_well recording (size + sha256) | verify-audio-content.sh 1/5 |
| `corpus-scan-manifest.txt` | the seven pinned recordings the MP3-aware scan reads | manual / report §10 |
| `corpus-scan-7.jsonl` | one JSON record per recording (event kinds, MP3-named events by kind, table MP3 names, no-extension names) + the summary line | report §10 |
| `mp3-census.txt` | index-only census of `.mp3` entries in the shipped sound VPKs, by directory | report §10 |
| `content-well.tsv` | the full content map of the pinned recording, one row per scheduled event (tick/index/kind/status/seconds/srctable/path/name/detail) | verify-audio-content.sh 2/5 |
| `mp3-events-null.txt` | the two per-event `audio: mp3` lines of the null-sink run | verify-audio-content.sh 3/5 |
| `mp3-events-device.txt` | the same two lines from the device-6 run | verify-audio-content.sh 5/5 |
| `null-run-readings.txt` | window line + mode summary + audio summary of the null-sink run | verify-audio-content.sh 3/5 |
| `device-run-readings.txt` | the same three lines from the device run | verify-audio-content.sh 5/5 |
| `device-enumeration.txt` | the device list line the device section checks before opening device 6 | verify-audio-content.sh 5/5 |
| `refusal-probe-readings.txt` | the one readings line of `audio_scheduler_probe` (named refusals + the 2d playback-boundary counts) | verify-audio-content.sh 4/5 |
| `gate-mutation.log` | the null-sink gate run: every assertion plus every capture mutation verdict | report §5, §9.1 |
| `gate-device.log` | the device gate run: null + device sections + both mutation sets | report §5, §8, §9.1 |
| `source-mutation.log` | source mutation, its capture by the forward gate, byte-for-byte restore, reproduced reference | report §9.2 |
| `verify-audio-mutation.log` | the existing audio gate, forward + mutation (null sink) | report §5.2 |
| `verify-audio-device.log` | the existing audio gate, device acceptance + mutation | report §5.2 |
| `source-mutation.sh` | the recipe: which line is mutated, and the restore/compare steps | report §9.2 |
| `collect.sh` | copies the readings above out of the ignored run directories and writes `SHA256SUMS` | report §11 |

## Reproducing the frozen readings

The commands are in the report §4.2; all of them run inside one
`hostlock.sh run t31 ...` because they build and launch the product. After the
runs:

```
bash evidence/t31-audio-content/collect.sh   # refresh the copies + SHA256SUMS
```

## Checking the hashes

This repo has `core.autocrlf=true`: the committed blobs are LF, the worktree copies are
CRLF, and `SHA256SUMS` records the **worktree bytes** (CRLF). Check it with:

```
cd evidence/t31-audio-content
tr -d '\r' < SHA256SUMS | sha256sum -c -
```

The `tr` is for the sums file itself: without it `sha256sum` reads each file name with a
trailing \r and reports every entry as missing. `collect.sh` run on an LF worktree would
produce a different (LF) hash set -- that is a different measurement, not a broken file.
