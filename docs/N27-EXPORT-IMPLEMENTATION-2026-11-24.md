# N27 export step 2 — implementation note (2026-11-24)

Status: the encoder library, the frame-sequence tool and the extended
`capture-sequence-check.sh` are committed on `work/n27-export` and pushed.
The product-side wiring (main.cpp, <= 20 lines) is NOT applied here — main.cpp is
owned by the integration group and this branch must not touch it.

## What exists

`native/include/demo_export.h` / `native/src/demo_export.cpp` (links into tf2_core):

* `Mp4ExportSink` — IMFSinkWriter wrapper. H.264 video, AAC audio, MP4 container.
  `open()` writes `<target>.partial`; only a successful `finalize()` renames it,
  so a failed export never leaves a half MP4 under the caller's name. `abort()`
  releases the writer and deletes the partial file.
* Timestamps are 100 ns. One demo tick = 150000 (200/3 fps); one tick of 48 kHz
  audio = 720 sample frames. Video timestamps are passed by the caller; audio
  timestamps are derived from the sample count inside the sink so the tracks
  cannot drift by a caller rounding mistake.
* `finalize()` zeroes the wall-clock timestamps of `mvhd` and, per track, `tkhd`
  and `mdhd`. mdhd lives under `trak > mdia > mdhd`; the first walker only looked
  at direct children, so two encodes that crossed a wall-clock second differed by
  exactly those two 4-byte fields. That is fixed and the gate now asserts
  `normalized-fields = 6` (video only) and `10` (video + audio).
* Audio is chunked into 1024-sample AAC frames; the remainder is flushed on
  finalize. `audio == nullptr` means a video-only file — never a fabricated
  silent track.

`native/tools/video_export.cpp` + `native/cmake/tf2-probes-export.cmake`
(target `video_export`):

* `--frames <dir> --frame-count N [--start-index I]` reads the product's
  `frame_<digits>.bmp` files in numeric order. The product writes
  `frame_%03d.bmp` (N27 finding — earlier notes said %06d).
* No `--size` means: take the size from the first frame, crop odd width/height by
  one pixel and print `note=source height 681 is odd; cropping one row`.
  An explicit odd `--size` is refused (exit 2) — the crop must be announced, not
  silently applied. `fitFrame` only crops/pads: never scales.
* `--audio <pcm|wav> --audio-rate --audio-channels`; WAV requires 16-bit PCM.
  A missing audio file is refused before the sink opens and writes nothing.
* `--verify <file>` reopens the result with IMFSourceReader, enumerates streams,
  reads `MF_PD_DURATION`, decodes every video sample (frame count, first/last ts,
  monotonic) and every audio sample (rate, channels, peak, RMS). The decoded
  peak is the assertion that proves a non-zero mix reached the muxer; counts
  alone cannot distinguish a silent track.
* Trace lines all start with `video-export:`; `result=ok|fail` is last.

`native/tools/export_audio_mix_probe.*` (delivered by the export-audio agent):
the product has no 48 kHz mixing stage — `AudioPlayer` hands PCM to winmm and the
*device* converts and sums. The probe re-runs the product parse/resolve chain and
`AudioEventScheduler` offline and sums/resamples deterministically to 48 kHz,
exactly 720 frames per tick. This is the audio source for the gate; it does not
change product behaviour and its scheduler is the product's own.

`capture-sequence-check.sh` (8 sections): the product captures 8 consecutive
ticks, then

1. 0/8 build; 1/8 sequence; 2/8 determinism (per-frame bytes); 3/8 argument
   refusals; 4/8 default capture path unchanged;
2. 5/8 MP4 export of those 8 frames + `--verify`: 1 video stream, 0 audio
   streams, frame count, size, first ts 0, last ts within 5000 of 7*150000,
   monotonic, duration 120 ms ±3, crop note present once;
3. 6/8 second export is byte-identical (sha256);
4. 7/8 export refusals: existing target (`code=already-exists`, rc 1), odd
   explicit size (rc 2, nothing written), missing audio (rc 1, nothing written,
   no `.partial` left);
5. 8/8 audio: mix 8 ticks of the demo at tick 1512 (first non-silent block;
   ticks 1200..1500 are silent because `pak01_dir.vpk` is missing on this
   machine), pinned PCM sha256, encode A/V, verify 2 streams, 6144 decoded audio
   frames (AAC padding of 5760 input frames), decoded peak > 0, duration within
   -3/+20 ms, and a second A/V export with the same sha256.

Current readings: `CAPTURE-SEQUENCE=PASS` (65 OK, 0 FAIL) on 2026-11-24;
video-only sha `167c7733…` for the 8-frame smoke; A/V duration 127 ms for 120 ms
of input (AAC priming), decoded audio peak 17476 / RMS 4349.

## Why the gate starts at tick 1512

The demo's first audible block starts at 1512. Starting at 1200 would encode
23040 bytes of zeros and every count assertion would still pass, which is the
failure mode the decoded-peak assertion exists to catch. The tick is a property
of this demo plus this machine (missing `pak01_dir.vpk`), so it is pinned in one
place with that comment.

## What is still missing

* Product wiring: `--export-video` / `--export-audio` / `--capture-sequence`
  coexistence in main.cpp (<= 20 lines) and the shared sequence-export entry
  point it calls. Until that lands, the export path is driven by the probe
  binary from the gate.
* A forced mid-write failure (to exercise `abort()` deleting the `.partial`).
  Today the refusal paths prove nothing is created; the abort path is unit
  reviewed but not gate-covered.