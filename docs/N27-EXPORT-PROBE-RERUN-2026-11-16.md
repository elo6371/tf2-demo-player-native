# N27 export: Media Foundation capability probe re-run, 2026-11-16

Scope of this note: **the probe, not the product export.** The product exe is frozen this window at
`26bff4361874a5f17b13990b1c29dd89874a56084afeed7884ee77227df11966` and was not rebuilt; N27's second
checkbox (in-product fixed-step render, offline audio, range/cancel/disk recovery, missing-component
exit 13) is still **not implemented**. This run re-establishes only the precondition: that this
machine can encode H.264 + AAC into an MP4 in-process, without any external executable.

## What was run

```
bash hostlock.sh run card-char bash .scratch/card-char/p4-mf2.sh
```

which builds the existing probe with `native/tools/mf_export_probe.sh` (cl.exe directly; not via
CMake, not linked into the product) and then runs `mf_export_probe.exe --out <existing dir>`.

## One failed attempt first, recorded because it is easy to misread

The first run passed an `--out` directory that did not exist yet. The probe answered
`MFCreateSinkWriterFromURL(...) hr=0x80070003` (= `ERROR_PATH_NOT_FOUND`) and the whole Q3/Q4/Q5
chain failed with it (`probe_rc=4`). **That was the caller's mistake, not a capability gap**: the probe
does not create its own output directory. After `mkdir -p` the same binary returned every answer
positive. Evidence of the failed attempt is kept deliberately:
`D:/_shared/EXEC-2026-11-16/INST3/mf-p4/run.log` (30,351 B) next to the successful `run2.log` (32,487 B).

## Results (this machine, this run)

| question | reading | verdict |
|---|---|---|
| H.264 encoder MFT | `h264EncoderPresent=yes (filtered-count=4)`; software `H264 Encoder MFT` `{6CA50344-051A-4DED-9779-A43305165E35}` enumerates an input type | yes |
| AAC encoder MFT | `aacEncoderPresent=yes (filtered-count=1)` | yes |
| MP4 container, two streams | `bothStreamsAdded=yes` | yes |
| CPU pixel format | `rgb32Accepted=yes chosenInput=MFVideoFormat_RGB32 needsOwnNv12Converter=no` | yes |
| independent read-back | `streamCount=2 videoStream=1 audioStream=0 firstFrameDecoded=yes` | yes |
| determinism (encoding only) | two encodes of the same 60 frames + PCM: both 207,556 B, 179 ms / 165 ms | same size; byte identity checked by hash below |

Produced (not in the repository): `sample_720p_2s.mp4`, `throughput_100f.mp4`, `det_a_2s.mp4`,
`det_b_2s.mp4` under `D:/_shared/EXEC-2026-11-16/INST3/mf-p4/run2/`. hashes:

```
57e3b37d9d1403cb026f95049139256b7c5db7fe2306b65e5440307c56213d40  det_a_2s.mp4
57e3b37d9d1403cb026f95049139256b7c5db7fe2306b65e5440307c56213d40  det_b_2s.mp4
8b9550d4752aa1d69ca3c9126eb2d15d8a79f38e2f75bcbc6383f979f3eeee79  sample_720p_2s.mp4
66383101c8b9c969ea9aadb02221d9abd9b7090b85d90d638ed1acd7c6421a8b  throughput_100f.mp4
5c2c12efc6d845e4f386c36e40ca75a7700b42c986beb7b47350f60fccd504b8  product.sha256
```

## What this does not say

- It does not mean N27 is done: there is no product export entry point at all, so there is nothing to
  attempt an export *with*. "Export failed" would be the wrong label; the accurate statement is
  **the product has no export feature yet**, and that is unchanged by a green probe.
- It does not re-verify audio content, per-frame render determinism, tick-to-sample mapping, or the
  component install/uninstall contract. Those are the second N27 checkbox and need product code.
- The probe remains throwaway evidence built by `cl.exe`, not a product target.

Recompute: `bash native/tools/mf_export_probe.sh <dir> && <dir>/mf_export_probe.exe --out <dir>/run`
after `mkdir -p <dir>/run`. Product exe sha before and after is in `<dir>/product.sha256` and
`D:/_shared/EXEC-2026-11-16/INST3/p3b/product-after.sha256` (both `26bff436...`).


## 9. Rerun at 13:41 — the 20 bytes that are not deterministic

Same binary, same inputs, 40 minutes later: `PROBE_RC=0`, all five ANSWERS yes, and the two files
written by one run are still identical. But the rerun's files are **not** identical to the 13:00
files: 20 bytes of 207,556 differ (both files are 207,556 B). A `cmp -l` plus a box walk puts
every one of them in a container timestamp:

| box | fields |
|---|---|
| `/moov/mvhd` | creation_time, modification_time |
| video `/moov/trak/tkhd` | creation_time, modification_time |
| video `/moov/trak/mdia/mdhd` | creation_time, modification_time |
| audio `/moov/trak/tkhd` | creation_time, modification_time |
| audio `/moov/trak/mdia/mdhd` | creation_time, modification_time |

`mdat` is byte-identical. The mvhd delta is 2427 s, exactly the wall-clock gap between the two
runs, so the SinkWriter stamps wall time into the container.

Correction to how the identical 13:00 hashes were read: they show **media** determinism. Encoded
payload bytes reproduce; container timestamps do not. A byte-identical export needs a post-pass
that zeroes those 12 timestamp values after Finalize — until then do not claim cross-run byte
identity for N27.

Evidence: `D:/_shared/EXEC-2026-11-16/INST3/mf-p4/rerun/` (mf-run.log, probe.sha256,
exe-before/after.sha256, det_a/det_b, verdict.md).

## 10. Task A, 15:19 — the export is re-runnable evidence, not a one-off

Four probe modes make the sequence assertable instead of merely observable:

| mode | writes | asserts |
|---|---|---|
| `--dump-inputs <dir>` | frames.rgb24 (60 x 2,764,800 B), frames.txt (per-frame SHA-256), pcm.raw (384,000 B = 96,000 sample frames, 48 kHz stereo 16-bit, 2.0 s), pcm.txt, audio.wav | both digests printed, then re-read by the next mode |
| `--encode-dump <dir> --mp4 <file>` | MP4 encoded only from that dumped artifact (RGB32 frames, PCM buffer) | framesRead=60, frameShaMatches=60, pcmShaMatch=yes, videoSamplesWritten=60 |
| `--normalize <in> <out>` | same MP4 with container creation/modification times zeroed | fieldsZeroed=5, bytesZeroed=40, verify_nonzero_fields=0 |
| `--decode <mp4> --dump-decoded <dir> --expect-dump <dir>` | decoded RGB24 frames, decoded PCM, per-frame SHA-256 manifests | video frame count and PCM sample count compared against the dump |

Chain, one command: `bash native/tools/mf_export_verify.sh <mf_export_probe.exe> <work-dir>`.

Readings, 15:19, probe sha256 c66b15b1dfbb4fbfad572951bbbc4e8ece2474669317c8686d1ef2db34f196d5:

- dump: frames=60 frameBytes=2764800 framesSha256=6e592af18c31afb5feb9c5f7986735cabc4dc640e0b6ac218f060d54345719b6, pcmSampleFrames=96000 pcmBytes=384000 pcmSha256=c2126a45d484b296ff6e12809b1d4d32f8652b9442402673aa27568599c48663; sha256sum over the two files reports the same digests, so the probe's own SHA-256 is not the only witness.
- encode A and B: framesRead=60 frameShaMatches=60 pcmShaMatch=yes videoSamplesWritten=60 audioSamplesWritten=94 bytes=207556, every HRESULT S_OK.
- negative control: the two exports differ — RAW_DIFF_BYTES=10 with encodes 2 s apart. Ten bytes is not a coincidence: ten timestamp values, each with a +2 change in its low byte. The 13:41 pair differed by 20 bytes because those encodes were 2,427 s apart, which changes two bytes per value.
- normalize: fieldsZeroed=5 bytesZeroed=40 traks=2; cmp raw norm reports exactly 40 touched bytes per file and verify_nonzero_fields=0.
- positive control: both normalized files hash to 428abe60ab2cb4cf74e7f3ff74637806165b233bd28ea4c7963a7a669fb21a43 — byte-identical, and the same hash the previous run produced, so normalization removes the wall clock rather than patching it.
- decode of the raw export: video frames=60 (firstTs=333333, lastTs=19999999, videoFramesMatch=yes), frame content videoMeanAbsDiff=1.958 maxFrameMeanAbsDiff=2.129; audio 96,256 sample frames against 96,000 source (delta=256, tolerance 2048), pcmSimilar=yes with meanAbsDiff=40.431 maxAbsDiff=8267 on a 16-bit scale. Decoding the normalized export gives the same readings.
- probe exe sha256 before/after the chain is unchanged; the only rebuild in this window was the audio-reader fix (3fb31dc6... -> c66b15b1...).

Two implementation findings worth keeping:

1. One IMFSourceReader cannot decode both tracks of these files. After the video pass reads to EOS, ReadSample on the audio stream returns EOS with zero samples (audio samples=0) even though SetCurrentMediaType(audio, PCM) reports S_OK. Audio needs a second reader opened at position 0 with its own stream selection. With that, 94 samples / 96,256 frames.
2. AAC is lossy, so decoded PCM can never hash-match the source. The assertion is delta <= 2048 sample frames (measured 256 = one encoder frame) plus mean absolute sample difference <= 600 (measured 40.4).

Count correction to §9: its table lists five boxes and ten fields, not twelve. Measured on the file: 5 boxes x 2 fields = 10 values = 40 bytes.

What this still does not show: the product export entry. The chain proves the probe can dump, encode, normalize and decode deterministically; N27's second checkbox (in-product fixed-step render through that path) remains unimplemented.

Evidence: D:/_shared/EXEC-2026-11-16/INST3/taskA/ (build/, run/, run_chain.log, verdict.md).

