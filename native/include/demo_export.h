#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tf2::native {

// N27 export step 2: the product-side MP4 writer.
//
// WHY THIS FILE EXISTS
//   docs/N27-PRODUCT-EXPORT-ENTRY-DESIGN-2026-11-16.md picks IMFSinkWriter as
//   the in-product encoder (H.264 video + AAC audio in an MP4 container). The
//   capability questions were already answered by native/tools/mf_export_probe.cpp
//   on this machine; this header is the product wrapper around the same calls,
//   split so the export loop in main.cpp only ever hands it decoded pixels and
//   mixed PCM.
//
// CONTRACT
//   * One sink writes one file. open() writes to <target>.partial; only a
//     successful finalize() renames it to <target>, so a crashed or failed
//     export never leaves a half MP4 under the name the caller asked for.
//   * Timestamps are 100 ns units, the same unit MFTIME uses. The demo tick is
//     15 ms (200/3 Hz), so one video frame is 150000 and one tick of 48 kHz
//     audio is 720 sample frames. The caller passes video timestamps
//     explicitly; audio timestamps are derived inside the sink from the sample
//     count so the two tracks cannot drift apart by a rounding mistake in the
//     caller.
//   * open() rejects odd dimensions instead of silently cropping them. H.264
//     input is converted to 4:2:0 inside Media Foundation and a half-chroma
//     sample would be undefined there; the caller must pick an even render
//     target (or crop the sequence offline, which native/tools/video_export.cpp
//     does on purpose and reports).
//   * Audio is written in 1024-sample blocks because that is the AAC frame
//     size the Media Foundation encoder expects; the sink buffers whatever the
//     caller hands it and flushes the remainder in finalize().
//   * finalize() zeroes the MP4 creation/modification timestamps before the
//     rename. Along with the fixed frame rate that makes two exports of the
//     same input byte-identical, which is what the export gate asserts.
//   * finalize() retries the rename a few times: Windows keeps a handle on a
//     file the just-finalized writer has released only slightly late, and a
//     read-only virus scanner can hold it longer. A permanent failure keeps the
//     .partial file and reports its path rather than deleting the only copy of
//     the encoded data.

// Result codes are explicit rather than bool so the caller can trace which
// step failed. The names are the trace tokens the gate greps for.
enum class ExportResult : std::uint8_t {
  Ok = 0,
  InvalidArgument,
  AlreadyExists,
  MediaFoundationUnavailable,
  WriterCreateFailed,
  VideoStreamFailed,
  AudioStreamFailed,
  BeginWritingFailed,
  WriteFailed,
  FinalizeFailed,
  IoFailed,
};

const char* exportResultName(ExportResult result);

struct ExportVideoConfig {
  // Even numbers only; open() rejects anything else.
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  // The demo tick rate is 200/3 fps, i.e. 150000 * 100 ns per frame. A ratio
  // rather than a float keeps the frame duration an exact integer.
  std::uint32_t fpsNumerator = 200;
  std::uint32_t fpsDenominator = 3;
  std::uint32_t bitRate = 8000000;
};

struct ExportAudioConfig {
  std::uint32_t sampleRate = 48000;
  std::uint32_t channels = 2;
  std::uint32_t bitsPerSample = 16;

  std::uint32_t blockAlign() const {
    return channels * (bitsPerSample / 8);
  }
};

// Counters the caller reports after finalize(). They are witnesses, not
// estimates: videoFrames counts successful WriteSample calls on the video
// stream and audioFrames counts source sample frames handed to the sink, so the
// gate can compare them against the requested tick count.
struct ExportStats {
  std::uint32_t videoFrames = 0;
  std::uint64_t audioFrames = 0;
  std::uint64_t audioBytes = 0;
  std::int64_t firstVideoTimestamp = 0;
  std::int64_t lastVideoTimestamp = 0;
  std::uint32_t renameAttempts = 0;
  std::uint32_t normalizedFields = 0;
};

class Mp4ExportSink {
 public:
  Mp4ExportSink();
  ~Mp4ExportSink();

  Mp4ExportSink(const Mp4ExportSink&) = delete;
  Mp4ExportSink& operator=(const Mp4ExportSink&) = delete;

  // target       final path (UTF-16 so the product's wide-path handling works)
  // audio        nullptr for a video-only file; never a fabricated silent track
  // overwrite    false makes an existing target an AlreadyExists error
  ExportResult open(const std::wstring& target, const ExportVideoConfig& video,
                    const ExportAudioConfig* audio, bool overwrite, std::string* detail);

  // rgb24 is top-down, tightly packed or with strideBytes per row. The sink
  // converts it to the bottom-up BGRA the Media Foundation video input type
  // expects. timestamp100ns must strictly increase across calls.
  ExportResult writeVideoFrame(const std::uint8_t* rgb24, std::size_t strideBytes,
                               std::int64_t timestamp100ns, std::string* detail);

  // Interleaved PCM matching the ExportAudioConfig given to open(). bytes must
  // be a multiple of blockAlign(). Any amount is accepted; the sink chunks it.
  ExportResult writeAudio(const std::uint8_t* pcm, std::size_t bytes, std::string* detail);

  // Finalize the container, normalise the timestamps, rename .partial -> target.
  // On failure the .partial file is kept and its path is appended to detail.
  ExportResult finalize(std::string* detail);

  // Release the writer without finalizing and delete the .partial file. Safe to
  // call twice, and called by the destructor if finalize() never ran.
  void abort();

  bool opened() const;
  const ExportStats& stats() const;
  const std::wstring& partialPath() const;
  const std::wstring& targetPath() const;

 private:
  struct Impl;
  Impl* impl_;
};

// Zero the creation/modification times of the mvhd and of every trak's tkhd and
// mdhd boxes, in place. Returns false (with detail) if the file is not an MP4
// this parser understands. fieldsZeroed reports how many 4- or 8-byte fields
// were visited; the probe measured 5 boxes (mvhd, tkhd, mdhd per track) for a
// two-track file, and the gate asserts the same number.
bool normalizeMp4File(const std::wstring& path, std::uint32_t* fieldsZeroed, std::string* detail);

}  // namespace tf2::native
