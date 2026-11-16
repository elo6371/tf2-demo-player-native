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
