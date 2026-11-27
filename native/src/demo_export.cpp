// demo_export.cpp -- IMFSinkWriter H.264/AAC MP4 writer (N27 export step 2).
//
// WHY THIS FILE EXISTS
//   The capability probe (native/tools/mf_export_probe.cpp) already proved that
//   this machine can encode H.264 and AAC into an MP4 and that repeated runs are
//   byte-identical once the MP4 creation times are zeroed. This file is the
//   product wrapper: it owns the sink writer, the two stream indices, the RGB32
//   conversion buffer and the audio chunker, so callers only hand over decoded
//   pixels and mixed PCM.
//
// WHAT IT DELIBERATELY DOES NOT DO
//   * It does not decode a demo, drive a renderer or mix audio. Those belong to
//     the caller; the product export hook and native/tools/video_export.cpp are
//     two different callers with the same contract.
//   * It does not silently fix an odd frame size, a non-integral frame rate or
//     a non-16-bit sample format. Each of those would make the encoded file
//     disagree with the evidence the gate records, so they are rejected.
//   * It does not fabricate a silent audio track when the caller passes no
//     audio config (the design doc calls that out explicitly).

#include "demo_export.h"

// WIN32_LEAN_AND_MEAN and NOMINMAX come from tf2_core's PUBLIC definitions (see
// cmake/tf2-libraries.cmake); defining them here again is C4005 and /WX turns
// that into a build failure.
#include <windows.h>

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace tf2::native {
namespace {

constexpr std::uint32_t kAudioChunkFrames = 1024;
constexpr std::uint32_t kAacProfileLevelIndication = 0x29;
// H.264 baseline (66). Baseline has no B frames, so the encoder never reorders
// a frame's presentation time ahead of the previous one and the first video
// timestamp stays 0 -- the ordering the gate asserts. Main profile would work
// too; the file would just be a little larger for the same quality.
constexpr std::uint32_t kH264BaselineProfile = 66;
constexpr std::int64_t kTicksPerSecond = 10000000;

std::string hexHresult(HRESULT hr) {
  char text[16] = {};
  std::snprintf(text, sizeof(text), "0x%08lx", static_cast<unsigned long>(hr));
  return std::string(text);
}

std::string fail(const char* what, HRESULT hr) {
  return std::string(what) + " failed hr=" + hexHresult(hr);
}

void setDetail(std::string* detail, std::string text) {
  if (detail) *detail = std::move(text);
}

// Detail lines are printed through the product's narrow trace, so a wide path
// has to become UTF-8 explicitly; narrowing wchar_t byte by byte would print
// garbage and put a path in a log that cannot be opened again.
std::string narrowPath(const std::wstring& text) {
  if (text.empty()) return std::string();
  const int bytes =
      WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) return std::string();
  std::string narrowed(static_cast<std::size_t>(bytes - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, narrowed.data(), bytes, nullptr, nullptr);
  return narrowed;
}

// MFStartup/MFShutdown are global. The product and the standalone tool both
// create at most one sink, but a refcount keeps the pair correct if a caller
// ever opens two, and keeps the startup cost out of the second open.
std::mutex& mfMutex() {
  static std::mutex mutex;
  return mutex;
}

int& mfRefs() {
  static int refs = 0;
  return refs;
}

bool& mfRunning() {
  static bool running = false;
  return running;
}

HRESULT acquireMediaFoundation() {
  std::lock_guard<std::mutex> lock(mfMutex());
  if (!mfRunning()) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return hr;
    mfRunning() = true;
  }
  ++mfRefs();
  return S_OK;
}

void releaseMediaFoundation() {
  std::lock_guard<std::mutex> lock(mfMutex());
  if (mfRefs() > 0) --mfRefs();
  if (mfRefs() == 0 && mfRunning()) {
    MFShutdown();
    mfRunning() = false;
  }
}

HRESULT createSinkWriter(const std::wstring& path, IMFSinkWriter** writer) {
  Microsoft::WRL::ComPtr<IMFAttributes> attributes;
  HRESULT hr = MFCreateAttributes(attributes.GetAddressOf(), 2);
  if (FAILED(hr)) return hr;
  hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
  if (FAILED(hr)) return hr;
  hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
  if (FAILED(hr)) return hr;
  return MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes.Get(), writer);
}

HRESULT makeH264OutputType(const ExportVideoConfig& config, IMFMediaType** type) {
  Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
  HRESULT hr = MFCreateMediaType(mediaType.GetAddressOf());
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AVG_BITRATE, config.bitRate);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_MPEG2_PROFILE, kH264BaselineProfile);
  if (FAILED(hr)) return hr;
  hr = MFSetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, config.width, config.height);
  if (FAILED(hr)) return hr;
  hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_FRAME_RATE, config.fpsNumerator,
                           config.fpsDenominator);
  if (FAILED(hr)) return hr;
  hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  if (FAILED(hr)) return hr;
  return mediaType->QueryInterface(IID_PPV_ARGS(type));
}

HRESULT makeVideoInputType(const ExportVideoConfig& config, IMFMediaType** type) {
  Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
  HRESULT hr = MFCreateMediaType(mediaType.GetAddressOf());
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (FAILED(hr)) return hr;
  // rgb24ToRgb32() below produces exactly this layout, so the input type is
  // pinned to it rather than letting the sink pick a surrogate.
  hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (FAILED(hr)) return hr;
  hr = MFSetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, config.width, config.height);
  if (FAILED(hr)) return hr;
  hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_FRAME_RATE, config.fpsNumerator,
                           config.fpsDenominator);
  if (FAILED(hr)) return hr;
  hr = MFSetAttributeRatio(mediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  if (FAILED(hr)) return hr;
  return mediaType->QueryInterface(IID_PPV_ARGS(type));
}

HRESULT makeAacOutputType(const ExportAudioConfig& config, IMFMediaType** type) {
  Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
  HRESULT hr = MFCreateMediaType(mediaType.GetAddressOf());
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, config.bitsPerSample);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, config.sampleRate);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, config.channels);
  if (FAILED(hr)) return hr;
  // 96 kbit/s for 48 kHz stereo 16-bit: sampleRate/4 frames * blockAlign / 4.
  const std::uint32_t averageBytes = (config.sampleRate / 4) * config.blockAlign() / 4;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, averageBytes);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, kAacProfileLevelIndication);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
  if (FAILED(hr)) return hr;
  return mediaType->QueryInterface(IID_PPV_ARGS(type));
}

HRESULT makePcmInputType(const ExportAudioConfig& config, IMFMediaType** type) {
  Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
  HRESULT hr = MFCreateMediaType(mediaType.GetAddressOf());
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, config.bitsPerSample);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, config.sampleRate);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, config.channels);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, config.blockAlign());
  if (FAILED(hr)) return hr;
  hr = mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                            config.sampleRate * config.blockAlign());
  if (FAILED(hr)) return hr;
  return mediaType->QueryInterface(IID_PPV_ARGS(type));
}

// MFVideoFormat_RGB32 is BGRA in memory and its rows run bottom-up, while both
// callers hold top-down RGB24. This is the same conversion the probe validated,
// kept in one place so the product and the tool produce identical input bytes.
void rgb24ToRgb32(const std::uint8_t* rgb24, std::size_t strideBytes, std::uint32_t width,
                  std::uint32_t height, std::vector<std::uint8_t>* destination) {
  destination->resize(static_cast<std::size_t>(width) * height * 4);
  std::uint8_t* target = destination->data();
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint8_t* sourceRow = rgb24 + static_cast<std::size_t>(height - 1 - y) * strideBytes;
    for (std::uint32_t x = 0; x < width; ++x) {
      target[x * 4 + 0] = sourceRow[x * 3 + 2];
      target[x * 4 + 1] = sourceRow[x * 3 + 1];
      target[x * 4 + 2] = sourceRow[x * 3 + 0];
      target[x * 4 + 3] = 0xFF;
    }
    target += static_cast<std::size_t>(width) * 4;
  }
}

// ---- MP4 box walking for timestamp normalisation -------------------------
//
// Only the boxes that carry wall-clock time are touched: mvhd and, per track,
// tkhd and mdhd. Both the size-32 form and the size-64 ("largesize") form are
// handled; a box with size 0 runs to the end of its parent, which is legal for
// the last child.
struct Mp4Box {
  std::size_t offset = 0;
  std::size_t size = 0;
  std::uint32_t type = 0;
};

std::uint32_t readU32(const std::uint8_t* data) {
  return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16) |
         (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

std::uint64_t readU64(const std::uint8_t* data) {
  return (static_cast<std::uint64_t>(readU32(data)) << 32) | readU32(data + 4);
}

bool walkBoxes(const std::uint8_t* data, std::size_t begin, std::size_t end,
               std::vector<Mp4Box>* boxes, std::string* detail) {
  std::size_t cursor = begin;
  while (cursor + 8 <= end) {
    Mp4Box box;
    box.offset = cursor;
    std::uint64_t size = readU32(data + cursor);
    box.type = readU32(data + cursor + 4);
    std::size_t header = 8;
    if (size == 1) {
      if (cursor + 16 > end) {
        setDetail(detail, "truncated largesize box header");
        return false;
      }
      size = readU64(data + cursor + 8);
      header = 16;
    } else if (size == 0) {
      size = end - cursor;
    }
    if (size < header || cursor + size > end) {
      setDetail(detail, "box [" + std::string(reinterpret_cast<const char*>(&box.type), 4) +
                            "] has an impossible size");
      return false;
    }
    box.size = static_cast<std::size_t>(size);
    boxes->push_back(box);
    cursor += box.size;
  }
  return true;
}

std::uint32_t zeroTimestamps(const std::uint8_t* data, const Mp4Box& box) {
  // FullBox: 1 byte version, 3 bytes flags. Version 0 keeps creation and
  // modification as 32-bit counts since 1904; version 1 widens both to 64 bits.
  const std::uint8_t version = data[box.offset + 8];
  const std::size_t body = box.offset + 12;
  if (body + 8 > box.offset + box.size) return 0;
  if (version == 1) {
    if (body + 16 > box.offset + box.size) return 0;
    std::memset(const_cast<std::uint8_t*>(data) + body, 0, 16);
    return 2;
  }
  std::memset(const_cast<std::uint8_t*>(data) + body, 0, 8);
  return 2;
}

bool normalizeMappedMp4(std::uint8_t* data, std::size_t size, std::uint32_t* fieldsZeroed,
                        std::string* detail) {
  std::vector<Mp4Box> top;
  if (!walkBoxes(data, 0, size, &top, detail)) return false;
  const Mp4Box* moov = nullptr;
  for (const Mp4Box& box : top) {
    if (box.type == 0x6D6F6F76u) {  // 'moov'
      moov = &box;
      break;
    }
  }
  if (!moov) {
    setDetail(detail, "no moov box");
    return false;
  }
  std::vector<Mp4Box> children;
  if (!walkBoxes(data, moov->offset + 8, moov->offset + moov->size, &children, detail)) return false;
  std::uint32_t zeroed = 0;
  for (const Mp4Box& child : children) {
    if (child.type == 0x6D766864u) {  // 'mvhd'
      zeroed += zeroTimestamps(data, child);
    } else if (child.type == 0x7472616Bu) {  // 'trak'
      std::vector<Mp4Box> trackBoxes;
      if (!walkBoxes(data, child.offset + 8, child.offset + child.size, &trackBoxes, detail)) {
        return false;
      }
      for (const Mp4Box& trackChild : trackBoxes) {
        if (trackChild.type == 0x746B6864u) {  // 'tkhd'
          zeroed += zeroTimestamps(data, trackChild);
        } else if (trackChild.type == 0x6D646864u) {  // 'mdhd'
          zeroed += zeroTimestamps(data, trackChild);
        } else if (trackChild.type == 0x6D646961u) {  // 'mdia' -- the media header is in here
          // Two encodes in different wall-clock seconds differed by exactly the
          // mdhd creation/modification seconds: the first version of this walker
          // only looked at a trak's direct children, and mdhd is nested one level
          // deeper (trak > mdia > mdhd).
          std::vector<Mp4Box> mediaBoxes;
          if (!walkBoxes(data, trackChild.offset + 8, trackChild.offset + trackChild.size,
                         &mediaBoxes, detail)) {
            return false;
          }
          for (const Mp4Box& mediaChild : mediaBoxes) {
            if (mediaChild.type == 0x6D646864u) {  // 'mdhd'
              zeroed += zeroTimestamps(data, mediaChild);
            }
          }
        }
      }
    }
  }
  if (fieldsZeroed) *fieldsZeroed = zeroed;
  return true;
}

bool fileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

const char* exportResultName(ExportResult result) {
  switch (result) {
    case ExportResult::Ok: return "ok";
    case ExportResult::InvalidArgument: return "invalid-argument";
    case ExportResult::AlreadyExists: return "already-exists";
    case ExportResult::MediaFoundationUnavailable: return "mf-unavailable";
    case ExportResult::WriterCreateFailed: return "writer-create";
    case ExportResult::VideoStreamFailed: return "video-stream";
    case ExportResult::AudioStreamFailed: return "audio-stream";
    case ExportResult::BeginWritingFailed: return "begin-writing";
    case ExportResult::WriteFailed: return "write";
    case ExportResult::FinalizeFailed: return "finalize";
    case ExportResult::IoFailed: return "io";
  }
  return "unknown";
}

bool normalizeMp4File(const std::wstring& path, std::uint32_t* fieldsZeroed, std::string* detail) {
  if (fieldsZeroed) *fieldsZeroed = 0;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    setDetail(detail, "could not open the encoded file for timestamp normalisation (error " +
                          std::to_string(GetLastError()) + ")");
    return false;
  }
  LARGE_INTEGER fileSize{};
  bool ok = false;
  if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0) {
    setDetail(detail, "could not measure the container");
  } else {
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (!mapping) {
      setDetail(detail, "could not map the container (error " + std::to_string(GetLastError()) + ")");
    } else {
      void* view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0);
      if (!view) {
        setDetail(detail, "could not map the container view (error " +
                              std::to_string(GetLastError()) + ")");
      } else {
        auto* data = static_cast<std::uint8_t*>(view);
        ok = normalizeMappedMp4(data, static_cast<std::size_t>(fileSize.QuadPart), fieldsZeroed, detail);
        UnmapViewOfFile(view);
      }
      CloseHandle(mapping);
    }
  }
  CloseHandle(file);
  return ok;
}

struct Mp4ExportSink::Impl {
  std::wstring target;
  std::wstring partial;
  ExportVideoConfig video;
  ExportAudioConfig audio;
  bool hasAudio = false;
  bool opened = false;
  bool mfAcquired = false;
  ExportStats stats;
  Microsoft::WRL::ComPtr<IMFSinkWriter> writer;
  DWORD videoStream = 0;
  DWORD audioStream = 0;
  std::int64_t frameDuration = 0;
  std::uint32_t blockAlign = 0;
  std::vector<std::uint8_t> rgb32;
  std::vector<std::uint8_t> audioPending;
  std::uint64_t audioFlushedFrames = 0;

  void releaseWriter() {
    writer.Reset();
    videoStream = 0;
    audioStream = 0;
    opened = false;
    if (mfAcquired) {
      releaseMediaFoundation();
      mfAcquired = false;
    }
  }

  ExportResult writeAudioChunk(const std::uint8_t* pcm, std::uint32_t frames,
                               std::string* detail) {
    const std::uint32_t bytes = frames * blockAlign;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(bytes, buffer.GetAddressOf());
    if (FAILED(hr)) {
      setDetail(detail, fail("MFCreateMemoryBuffer(audio)", hr));
      return ExportResult::WriteFailed;
    }
    BYTE* mapped = nullptr;
    hr = buffer->Lock(&mapped, nullptr, nullptr);
    if (FAILED(hr)) {
      setDetail(detail, fail("IMFMediaBuffer::Lock(audio)", hr));
      return ExportResult::WriteFailed;
    }
    std::memcpy(mapped, pcm, bytes);
    buffer->Unlock();
    hr = buffer->SetCurrentLength(bytes);
    if (FAILED(hr)) {
      setDetail(detail, fail("IMFMediaBuffer::SetCurrentLength(audio)", hr));
      return ExportResult::WriteFailed;
    }
    Microsoft::WRL::ComPtr<IMFSample> sample;
    hr = MFCreateSample(sample.GetAddressOf());
    if (FAILED(hr)) {
      setDetail(detail, fail("MFCreateSample(audio)", hr));
      return ExportResult::WriteFailed;
    }
    hr = sample->AddBuffer(buffer.Get());
    if (FAILED(hr)) {
      setDetail(detail, fail("IMFSample::AddBuffer(audio)", hr));
      return ExportResult::WriteFailed;
    }
    // Sample positions are computed from the running sample count rather than
    // accumulated, so a non-integral block duration cannot drift: 720 frames at
    // 48 kHz is exactly 150000 here, the same length as one demo tick.
    const LONGLONG timestamp =
        static_cast<LONGLONG>((audioFlushedFrames * static_cast<std::uint64_t>(kTicksPerSecond)) /
                              audio.sampleRate);
    const LONGLONG duration = static_cast<LONGLONG>(
        (static_cast<std::uint64_t>(frames) * static_cast<std::uint64_t>(kTicksPerSecond)) /
        audio.sampleRate);
    hr = sample->SetSampleTime(timestamp);
    if (FAILED(hr)) {
      setDetail(detail, fail("IMFSample::SetSampleTime(audio)", hr));
      return ExportResult::WriteFailed;
    }
    hr = sample->SetSampleDuration(duration);
    if (FAILED(hr)) {
      setDetail(detail, fail("IMFSample::SetSampleDuration(audio)", hr));
      return ExportResult::WriteFailed;
    }
    hr = writer->WriteSample(audioStream, sample.Get());
    if (FAILED(hr)) {
      setDetail(detail, fail("IMFSinkWriter::WriteSample(audio)", hr));
      return ExportResult::WriteFailed;
    }
    audioFlushedFrames += frames;
    return ExportResult::Ok;
  }

  void discardPartial() {
    if (!partial.empty()) DeleteFileW(partial.c_str());
  }
};

Mp4ExportSink::Mp4ExportSink() : impl_(new Impl()) {}

Mp4ExportSink::~Mp4ExportSink() {
  if (impl_ && impl_->opened) abort();
  delete impl_;
  impl_ = nullptr;
}

bool Mp4ExportSink::opened() const {
  return impl_ && impl_->opened;
}

const ExportStats& Mp4ExportSink::stats() const {
  return impl_->stats;
}

const std::wstring& Mp4ExportSink::partialPath() const {
  return impl_->partial;
}

const std::wstring& Mp4ExportSink::targetPath() const {
  return impl_->target;
}

ExportResult Mp4ExportSink::open(const std::wstring& target, const ExportVideoConfig& video,
                                 const ExportAudioConfig* audio, bool overwrite,
                                 std::string* detail) {
  if (impl_->opened) {
    setDetail(detail, "sink is already open");
    return ExportResult::InvalidArgument;
  }
  if (target.empty()) {
    setDetail(detail, "target path is empty");
    return ExportResult::InvalidArgument;
  }
  if (video.width == 0 || video.height == 0) {
    setDetail(detail, "video size is empty");
    return ExportResult::InvalidArgument;
  }
  if ((video.width % 2) != 0 || (video.height % 2) != 0) {
    setDetail(detail, "video size must be even (got " + std::to_string(video.width) + "x" +
                          std::to_string(video.height) + ")");
    return ExportResult::InvalidArgument;
  }
  if (video.fpsNumerator == 0 || video.fpsDenominator == 0) {
    setDetail(detail, "frame rate ratio must be positive");
    return ExportResult::InvalidArgument;
  }
  const std::uint64_t frameUnits =
      static_cast<std::uint64_t>(kTicksPerSecond) * video.fpsDenominator;
  if (frameUnits % video.fpsNumerator != 0) {
    setDetail(detail, "frame rate " + std::to_string(video.fpsNumerator) + "/" +
                          std::to_string(video.fpsDenominator) +
                          " does not divide 100 ns units exactly");
    return ExportResult::InvalidArgument;
  }
  if (video.bitRate == 0) {
    setDetail(detail, "video bit rate is zero");
    return ExportResult::InvalidArgument;
  }
  if (audio) {
    if (audio->bitsPerSample != 16 || audio->channels == 0 || audio->channels > 2 ||
        (audio->sampleRate != 44100 && audio->sampleRate != 48000)) {
      setDetail(detail, "unsupported PCM layout (want 16-bit mono/stereo at 44.1 or 48 kHz)");
      return ExportResult::InvalidArgument;
    }
  }

  impl_->target = target;
  impl_->partial = target + L".partial";
  impl_->video = video;
  impl_->frameDuration = static_cast<std::int64_t>(frameUnits / video.fpsNumerator);
  impl_->hasAudio = audio != nullptr;
  if (audio) {
    impl_->audio = *audio;
    impl_->blockAlign = audio->blockAlign();
  }
  impl_->stats = ExportStats();
  impl_->audioFlushedFrames = 0;
  impl_->audioPending.clear();

  if (fileExists(target) && !overwrite) {
    setDetail(detail, "target already exists and overwrite was not requested");
    return ExportResult::AlreadyExists;
  }
  // A .partial left by an interrupted earlier run is not evidence of anything;
  // the new export owns the name now.
  if (fileExists(impl_->partial)) DeleteFileW(impl_->partial.c_str());

  HRESULT hr = acquireMediaFoundation();
  if (FAILED(hr)) {
    setDetail(detail, fail("MFStartup", hr));
    return ExportResult::MediaFoundationUnavailable;
  }
  impl_->mfAcquired = true;

  hr = createSinkWriter(impl_->partial, impl_->writer.GetAddressOf());
  if (FAILED(hr)) {
    setDetail(detail, fail("MFCreateSinkWriterFromURL", hr));
    impl_->discardPartial();
    impl_->releaseWriter();
    return ExportResult::WriterCreateFailed;
  }

  Microsoft::WRL::ComPtr<IMFMediaType> outputType;
  hr = makeH264OutputType(video, outputType.GetAddressOf());
  if (FAILED(hr)) {
    setDetail(detail, fail("makeH264OutputType", hr));
    impl_->discardPartial();
    impl_->releaseWriter();
    return ExportResult::VideoStreamFailed;
  }
  hr = impl_->writer->AddStream(outputType.Get(), &impl_->videoStream);
  if (FAILED(hr)) {
    setDetail(detail, fail("AddStream(video)", hr));
    impl_->discardPartial();
    impl_->releaseWriter();
    return ExportResult::VideoStreamFailed;
  }
  Microsoft::WRL::ComPtr<IMFMediaType> inputType;
  hr = makeVideoInputType(video, inputType.GetAddressOf());
  if (FAILED(hr)) {
    setDetail(detail, fail("makeVideoInputType", hr));
    impl_->discardPartial();
    impl_->releaseWriter();
    return ExportResult::VideoStreamFailed;
  }
  hr = impl_->writer->SetInputMediaType(impl_->videoStream, inputType.Get(), nullptr);
  if (FAILED(hr)) {
    setDetail(detail, fail("SetInputMediaType(video)", hr));
    impl_->discardPartial();
    impl_->releaseWriter();
    return ExportResult::VideoStreamFailed;
  }

  if (impl_->hasAudio) {
    Microsoft::WRL::ComPtr<IMFMediaType> aacType;
    hr = makeAacOutputType(impl_->audio, aacType.GetAddressOf());
    if (FAILED(hr)) {
      setDetail(detail, fail("makeAacOutputType", hr));
      impl_->discardPartial();
      impl_->releaseWriter();
      return ExportResult::AudioStreamFailed;
    }
    hr = impl_->writer->AddStream(aacType.Get(), &impl_->audioStream);
    if (FAILED(hr)) {
      setDetail(detail, fail("AddStream(audio)", hr));
      impl_->discardPartial();
      impl_->releaseWriter();
      return ExportResult::AudioStreamFailed;
    }
    Microsoft::WRL::ComPtr<IMFMediaType> pcmType;
    hr = makePcmInputType(impl_->audio, pcmType.GetAddressOf());
    if (FAILED(hr)) {
      setDetail(detail, fail("makePcmInputType", hr));
      impl_->discardPartial();
      impl_->releaseWriter();
      return ExportResult::AudioStreamFailed;
    }
    hr = impl_->writer->SetInputMediaType(impl_->audioStream, pcmType.Get(), nullptr);
    if (FAILED(hr)) {
      setDetail(detail, fail("SetInputMediaType(audio)", hr));
      impl_->discardPartial();
      impl_->releaseWriter();
      return ExportResult::AudioStreamFailed;
    }
  }

  hr = impl_->writer->BeginWriting();
  if (FAILED(hr)) {
    setDetail(detail, fail("BeginWriting", hr));
    impl_->discardPartial();
    impl_->releaseWriter();
    return ExportResult::BeginWritingFailed;
  }

  impl_->opened = true;
  return ExportResult::Ok;
}

ExportResult Mp4ExportSink::writeVideoFrame(const std::uint8_t* rgb24, std::size_t strideBytes,
                                            std::int64_t timestamp100ns, std::string* detail) {
  if (!impl_->opened) {
    setDetail(detail, "sink is not open");
    return ExportResult::InvalidArgument;
  }
  if (!rgb24) {
    setDetail(detail, "frame pointer is null");
    return ExportResult::InvalidArgument;
  }
  if (strideBytes < static_cast<std::size_t>(impl_->video.width) * 3) {
    setDetail(detail, "row stride is shorter than the frame width");
    return ExportResult::InvalidArgument;
  }
  if (impl_->stats.videoFrames > 0 && timestamp100ns <= impl_->stats.lastVideoTimestamp) {
    setDetail(detail, "video timestamp did not increase (got " + std::to_string(timestamp100ns) +
                          " after " + std::to_string(impl_->stats.lastVideoTimestamp) + ")");
    return ExportResult::InvalidArgument;
  }

  rgb24ToRgb32(rgb24, strideBytes, impl_->video.width, impl_->video.height, &impl_->rgb32);
  const std::uint32_t bytes = static_cast<std::uint32_t>(impl_->rgb32.size());
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = MFCreateMemoryBuffer(bytes, buffer.GetAddressOf());
  if (FAILED(hr)) {
    setDetail(detail, fail("MFCreateMemoryBuffer(video)", hr));
    return ExportResult::WriteFailed;
  }
  BYTE* mapped = nullptr;
  hr = buffer->Lock(&mapped, nullptr, nullptr);
  if (FAILED(hr)) {
    setDetail(detail, fail("IMFMediaBuffer::Lock(video)", hr));
    return ExportResult::WriteFailed;
  }
  std::memcpy(mapped, impl_->rgb32.data(), bytes);
  buffer->Unlock();
  hr = buffer->SetCurrentLength(bytes);
  if (FAILED(hr)) {
    setDetail(detail, fail("IMFMediaBuffer::SetCurrentLength(video)", hr));
    return ExportResult::WriteFailed;
  }
  Microsoft::WRL::ComPtr<IMFSample> sample;
  hr = MFCreateSample(sample.GetAddressOf());
  if (FAILED(hr)) {
    setDetail(detail, fail("MFCreateSample(video)", hr));
    return ExportResult::WriteFailed;
  }
  hr = sample->AddBuffer(buffer.Get());
  if (FAILED(hr)) {
    setDetail(detail, fail("IMFSample::AddBuffer(video)", hr));
    return ExportResult::WriteFailed;
  }
  hr = sample->SetSampleTime(static_cast<LONGLONG>(timestamp100ns));
  if (FAILED(hr)) {
    setDetail(detail, fail("IMFSample::SetSampleTime(video)", hr));
    return ExportResult::WriteFailed;
  }
  hr = sample->SetSampleDuration(static_cast<LONGLONG>(impl_->frameDuration));
  if (FAILED(hr)) {
    setDetail(detail, fail("IMFSample::SetSampleDuration(video)", hr));
    return ExportResult::WriteFailed;
  }
  hr = impl_->writer->WriteSample(impl_->videoStream, sample.Get());
  if (FAILED(hr)) {
    setDetail(detail, fail("IMFSinkWriter::WriteSample(video)", hr));
    return ExportResult::WriteFailed;
  }

  if (impl_->stats.videoFrames == 0) impl_->stats.firstVideoTimestamp = timestamp100ns;
  impl_->stats.lastVideoTimestamp = timestamp100ns;
  ++impl_->stats.videoFrames;
  return ExportResult::Ok;
}

ExportResult Mp4ExportSink::writeAudio(const std::uint8_t* pcm, std::size_t bytes,
                                       std::string* detail) {
  if (!impl_->opened) {
    setDetail(detail, "sink is not open");
    return ExportResult::InvalidArgument;
  }
  if (!impl_->hasAudio) {
    setDetail(detail, "sink was opened without an audio track");
    return ExportResult::InvalidArgument;
  }
  if (!pcm && bytes > 0) {
    setDetail(detail, "audio pointer is null");
    return ExportResult::InvalidArgument;
  }
  if (bytes % impl_->blockAlign != 0) {
    setDetail(detail, "audio byte count is not a whole number of sample frames");
    return ExportResult::InvalidArgument;
  }

  impl_->audioPending.insert(impl_->audioPending.end(), pcm, pcm + bytes);
  impl_->stats.audioBytes += bytes;
  impl_->stats.audioFrames += bytes / impl_->blockAlign;

  const std::size_t chunkBytes =
      static_cast<std::size_t>(kAudioChunkFrames) * impl_->blockAlign;
  while (impl_->audioPending.size() >= chunkBytes) {
    const ExportResult result =
        impl_->writeAudioChunk(impl_->audioPending.data(), kAudioChunkFrames, detail);
    if (result != ExportResult::Ok) return result;
    impl_->audioPending.erase(impl_->audioPending.begin(),
                              impl_->audioPending.begin() + static_cast<std::ptrdiff_t>(chunkBytes));
  }
  return ExportResult::Ok;
}

ExportResult Mp4ExportSink::finalize(std::string* detail) {
  if (!impl_->opened) {
    setDetail(detail, "sink is not open");
    return ExportResult::InvalidArgument;
  }
  if (impl_->hasAudio && !impl_->audioPending.empty()) {
    const std::uint32_t frames =
        static_cast<std::uint32_t>(impl_->audioPending.size() / impl_->blockAlign);
    const ExportResult result = impl_->writeAudioChunk(impl_->audioPending.data(), frames, detail);
    if (result != ExportResult::Ok) return result;
    impl_->audioPending.clear();
  }
  if (impl_->hasAudio && impl_->audioFlushedFrames == 0) {
    // The design doc is explicit: no fabricated silent track. An audio stream
    // that was added but never fed is a caller bug; report it rather than let a
    // 0-sample AAC track into the file.
    setDetail(detail, "audio track was opened but no samples were written");
    return ExportResult::WriteFailed;
  }

  std::string finishDetail;
  HRESULT hr = impl_->writer->Finalize();
  impl_->writer.Reset();
  if (FAILED(hr)) {
    impl_->opened = false;
    setDetail(detail, fail("IMFSinkWriter::Finalize", hr) + " (partial file kept: " +
                          narrowPath(impl_->partial) + ")");
    impl_->releaseWriter();
    return ExportResult::FinalizeFailed;
  }
  // Finalize() closed the sink but the handle itself may be released slightly
  // later on this machine; only then can the file be mapped and renamed.
  std::uint32_t fields = 0;
  if (!normalizeMp4File(impl_->partial, &fields, detail)) {
    impl_->opened = false;
    const std::string partialPath = narrowPath(impl_->partial);
    if (detail) *detail += " (partial file kept: " + partialPath + ")";
    impl_->releaseWriter();
    return ExportResult::IoFailed;
  }
  impl_->stats.normalizedFields = fields;

  // Windows can hold the just-closed file for a moment, and a scanner can hold
  // it longer; retry briefly instead of failing an export that already encoded.
  constexpr int kRenameAttempts = 5;
  BOOL renamed = FALSE;
  for (int attempt = 0; attempt < kRenameAttempts; ++attempt) {
    ++impl_->stats.renameAttempts;
    renamed = MoveFileExW(impl_->partial.c_str(), impl_->target.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (renamed) break;
    Sleep(50);
  }
  if (!renamed) {
    impl_->opened = false;
    const std::string partialPath = narrowPath(impl_->partial);
    setDetail(detail, "could not rename the encoded file into place (error " +
                          std::to_string(GetLastError()) + "; partial file kept: " +
                          partialPath + ")");
    impl_->releaseWriter();
    return ExportResult::IoFailed;
  }

  impl_->opened = false;
  impl_->releaseWriter();
  return ExportResult::Ok;
}

void Mp4ExportSink::abort() {
  if (!impl_) return;
  impl_->writer.Reset();
  impl_->discardPartial();
  impl_->opened = false;
  impl_->releaseWriter();
  impl_->audioPending.clear();
}

}  // namespace tf2::native
