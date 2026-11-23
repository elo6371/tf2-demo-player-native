// mf_export_probe.cpp -- Media Foundation export capability probe (N27/T105 E5).
//
// WHY THIS FILE EXISTS
//   The product decision (docs/N27-SCOPE-DECISIONS-2026-11-15.md sections 7 and 8)
//   is "encode in-product with IMFSinkWriter: H.264 video + AAC audio in an MP4,
//   audio mixed offline to PCM". Before any product code is written, this probe
//   must prove that THIS machine's Media Foundation can do each part, and must
//   print raw HRESULTs rather than claim anything. Every section below is labelled
//   with the question it answers (Q1..Q7) so the report can cite a command line.
//
//   Nothing here is product code. It links only against system DLLs
//   (mfplat, mfreadwrite, mfuuid, ole32) -- that is itself the answer to Q7.
//
// MODES
//   mf_export_probe --out <dir>              run Q1..Q6 + throughput + determinism
//   mf_export_probe --emit-inputs <dir>      write 60 BMP frames + audio.wav
//   mf_export_probe --encode-from-disk <dir> --from-disk-out <file>
//                                            separate process: BMP+WAV -> mp4
//
// BUILD
//   native/tools/mf_export_probe.sh (cl.exe directly; native/CMakeLists.txt is
//   deliberately not touched by this work).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mftransform.h>
#include <codecapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

FILE* g_log = nullptr;

void out(const char* format, ...) {
  char buffer[8192];
  va_list args;
  va_start(args, format);
  const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  size_t length = 0;
  if (written > 0) length = std::min(static_cast<size_t>(written), sizeof(buffer) - 1);
  if (length == 0) return;
  std::fwrite(buffer, 1, length, stdout);
  std::fflush(stdout);
  if (g_log != nullptr) {
    std::fwrite(buffer, 1, length, g_log);
    std::fflush(g_log);
  }
}

const char* hrName(HRESULT hr) {
  switch (hr) {
    case S_OK: return "S_OK";
    case S_FALSE: return "S_FALSE";
    case E_FAIL: return "E_FAIL";
    case E_INVALIDARG: return "E_INVALIDARG";
    case E_NOTIMPL: return "E_NOTIMPL";
    case E_NOINTERFACE: return "E_NOINTERFACE";
    case E_POINTER: return "E_POINTER";
    case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
    case E_ACCESSDENIED: return "E_ACCESSDENIED";
    case E_UNEXPECTED: return "E_UNEXPECTED";
    case E_ABORT: return "E_ABORT";
    case E_HANDLE: return "E_HANDLE";
    case MF_E_INVALIDMEDIATYPE: return "MF_E_INVALIDMEDIATYPE";
    case MF_E_INVALIDTYPE: return "MF_E_INVALIDTYPE";
    case MF_E_ATTRIBUTENOTFOUND: return "MF_E_ATTRIBUTENOTFOUND";
    case MF_E_UNSUPPORTED_BYTESTREAM_TYPE: return "MF_E_UNSUPPORTED_BYTESTREAM_TYPE";
    case MF_E_TRANSFORM_ASYNC_LOCKED: return "MF_E_TRANSFORM_ASYNC_LOCKED";
    case MF_E_TRANSFORM_NEED_MORE_INPUT: return "MF_E_TRANSFORM_NEED_MORE_INPUT";
    case MF_E_TRANSFORM_STREAM_CHANGE: return "MF_E_TRANSFORM_STREAM_CHANGE";
    case MF_E_NO_MORE_TYPES: return "MF_E_NO_MORE_TYPES";
    case MF_E_INVALIDSTREAMNUMBER: return "MF_E_INVALIDSTREAMNUMBER";
    case MF_E_TOPO_CODEC_NOT_FOUND: return "MF_E_TOPO_CODEC_NOT_FOUND";
    case MF_E_UNSUPPORTED_D3D_TYPE: return "MF_E_UNSUPPORTED_D3D_TYPE";
    case MF_E_INVALID_FORMAT: return "MF_E_INVALID_FORMAT";
    case MF_E_END_OF_STREAM: return "MF_E_END_OF_STREAM";
    case MF_E_SHUTDOWN: return "MF_E_SHUTDOWN";
    case MF_E_NOTACCEPTING: return "MF_E_NOTACCEPTING";
    case MF_E_UNEXPECTED: return "MF_E_UNEXPECTED";
    case MF_E_HW_MFT_FAILED_START_STREAMING: return "MF_E_HW_MFT_FAILED_START_STREAMING";
    case MF_E_GRL_VERSION_TOO_LOW: return "MF_E_GRL_VERSION_TOO_LOW";
    case MF_E_GRL_RENEWAL_NOT_FOUND: return "MF_E_GRL_RENEWAL_NOT_FOUND";
    case MF_E_HARDWARE_DRM_UNSUPPORTED: return "MF_E_HARDWARE_DRM_UNSUPPORTED";
    default: return "unknown-hresult";
  }
}

std::string hrStr(HRESULT hr) {
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), "0x%08lX (%s)", static_cast<unsigned long>(static_cast<uint32_t>(hr)), hrName(hr));
  return std::string(buffer);
}

std::string guidStr(const GUID& guid) {
  wchar_t wide[64]{};
  if (StringFromGUID2(guid, wide, 64) <= 0) return "<guid-format-failed>";
  char utf8[128]{};
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
  return std::string(utf8);
}

std::string wideToUtf8(const wchar_t* text) {
  if (text == nullptr) return "<null>";
  char utf8[512]{};
  WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
  return std::string(utf8);
}

std::string subtypeName(const GUID& subtype) {
  struct Entry { const GUID* guid; const char* name; };
  const Entry table[] = {
      {&MFVideoFormat_H264, "MFVideoFormat_H264"},
      {&MFVideoFormat_H264_ES, "MFVideoFormat_H264_ES"},
      {&MFVideoFormat_HEVC, "MFVideoFormat_HEVC"},
      {&MFVideoFormat_MPEG2, "MFVideoFormat_MPEG2"},
      {&MFVideoFormat_VP80, "MFVideoFormat_VP80"},
      {&MFVideoFormat_VP90, "MFVideoFormat_VP90"},
      {&MFVideoFormat_MJPG, "MFVideoFormat_MJPG"},
      {&MFVideoFormat_NV12, "MFVideoFormat_NV12"},
      {&MFVideoFormat_YUY2, "MFVideoFormat_YUY2"},
      {&MFVideoFormat_UYVY, "MFVideoFormat_UYVY"},
      {&MFVideoFormat_YV12, "MFVideoFormat_YV12"},
      {&MFVideoFormat_IYUV, "MFVideoFormat_IYUV"},
      {&MFVideoFormat_I420, "MFVideoFormat_I420"},
      {&MFVideoFormat_RGB32, "MFVideoFormat_RGB32"},
      {&MFVideoFormat_ARGB32, "MFVideoFormat_ARGB32"},
      {&MFVideoFormat_RGB24, "MFVideoFormat_RGB24"},
      {&MFVideoFormat_RGB565, "MFVideoFormat_RGB565"},
      {&MFVideoFormat_P010, "MFVideoFormat_P010"},
      {&MFAudioFormat_AAC, "MFAudioFormat_AAC"},
      {&MFAudioFormat_PCM, "MFAudioFormat_PCM"},
      {&MFAudioFormat_Float, "MFAudioFormat_Float"},
      {&MFAudioFormat_MP3, "MFAudioFormat_MP3"},
      {&MFAudioFormat_Dolby_AC3, "MFAudioFormat_Dolby_AC3"},
  };
  for (const Entry& entry : table) {
    if (*entry.guid == subtype) return std::string(entry.name);
  }
  return std::string("other:") + guidStr(subtype);
}

std::string majorTypeName(const GUID& major) {
  if (major == MFMediaType_Video) return "video";
  if (major == MFMediaType_Audio) return "audio";
  if (major == MFMediaType_Stream) return "stream";
  return guidStr(major);
}

std::wstring utf8ToWide(const std::string& text) {
  if (text.empty()) return std::wstring();
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wide(static_cast<size_t>(length > 1 ? length - 1 : 0), L'\0');
  if (length > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], length);
  return wide;
}

template <typename T>
void rel(T*& pointer) {
  if (pointer != nullptr) {
    pointer->Release();
    pointer = nullptr;
  }
}

double fileSizeBytes(const std::string& path) {
  const std::wstring wide = utf8ToWide(path);
  HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return -1.0;
  LARGE_INTEGER size{};
  const BOOL ok = GetFileSizeEx(handle, &size);
  CloseHandle(handle);
  return ok ? static_cast<double>(size.QuadPart) : -1.0;
}

long long nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

double nowUs() {
  using namespace std::chrono;
  return static_cast<double>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------- OS inventory

// winternl.h is deliberately not included (it redefines several Nt* prototypes);
// RtlGetVersion only needs the OSVERSIONINFO layout, so declare it here.
struct OsVersionInfo {
  ULONG dwOSVersionInfoSize;
  ULONG dwMajorVersion;
  ULONG dwMinorVersion;
  ULONG dwBuildNumber;
  ULONG dwPlatformId;
  WCHAR szCSDVersion[128];
};

std::string osDescription() {
  using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);
  std::string result = "unavailable";
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll != nullptr) {
    auto getVersion = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (getVersion != nullptr) {
      OsVersionInfo info{};
      info.dwOSVersionInfoSize = sizeof(info);
      if (getVersion(&info) == 0L) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "%lu.%lu build %lu", info.dwMajorVersion,
                      info.dwMinorVersion, info.dwBuildNumber);
        result = buffer;
      }
    }
  }
  SYSTEM_INFO systemInfo{};
  GetNativeSystemInfo(&systemInfo);
  const char* arch = "unknown";
  switch (systemInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: arch = "arm64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86"; break;
    default: break;
  }
  return result + " " + arch;
}

// ---------------------------------------------------------------- MF helpers

HRESULT createSinkWriterMp4(const std::wstring& path, IMFSinkWriter** writer) {
  IMFAttributes* attributes = nullptr;
  HRESULT hr = MFCreateAttributes(&attributes, 2);
  if (FAILED(hr)) return hr;
  hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
  if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
  HRESULT createResult = hr;
  if (SUCCEEDED(hr)) createResult = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes, writer);
  rel(attributes);
  return createResult;
}

struct VideoShape {
  UINT32 width = 1280;
  UINT32 height = 720;
  UINT32 fpsNumerator = 30;
  UINT32 fpsDenominator = 1;
};

HRESULT makeH264OutputType(const VideoShape& shape, IMFMediaType** type, bool withProfile) {
  IMFMediaType* mediaType = nullptr;
  HRESULT hr = MFCreateMediaType(&mediaType);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AVG_BITRATE, 4000000);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (SUCCEEDED(hr)) hr = MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, shape.width, shape.height);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, shape.fpsNumerator, shape.fpsDenominator);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  if (withProfile) {
    // Main profile means B-frames, and B-frames mean a one-frame reorder delay at
    // the head of the stream. MFPROBE_H264_PROFILE=base checks whether that delay
    // is what pushes the first video PTS off zero.
    char profileEnv[16] = {0};
    const DWORD envLen = GetEnvironmentVariableA("MFPROBE_H264_PROFILE", profileEnv, sizeof(profileEnv));
    UINT32 profile = eAVEncH264VProfile_Main;
    if (envLen > 0 && _stricmp(profileEnv, "base") == 0) profile = eAVEncH264VProfile_Base;
    if (envLen > 0 && _stricmp(profileEnv, "high") == 0) profile = eAVEncH264VProfile_High;
    hr = mediaType->SetUINT32(MF_MT_MPEG2_PROFILE, profile);
  }
  if (FAILED(hr)) {
    rel(mediaType);
    return hr;
  }
  *type = mediaType;
  return S_OK;
}

HRESULT makeVideoInputType(const VideoShape& shape, const GUID& subtype, IMFMediaType** type) {
  IMFMediaType* mediaType = nullptr;
  HRESULT hr = MFCreateMediaType(&mediaType);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = mediaType->SetGUID(MF_MT_SUBTYPE, subtype);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (SUCCEEDED(hr)) hr = MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, shape.width, shape.height);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, shape.fpsNumerator, shape.fpsDenominator);
  if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  if (FAILED(hr)) {
    rel(mediaType);
    return hr;
  }
  *type = mediaType;
  return S_OK;
}

HRESULT makeAacOutputType(UINT32 sampleRate, UINT32 channels, UINT32 bitsPerSample, IMFMediaType** type) {
  IMFMediaType* mediaType = nullptr;
  HRESULT hr = MFCreateMediaType(&mediaType);
  if (FAILED(hr)) return hr;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  if (SUCCEEDED(hr)) hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
  if (SUCCEEDED(hr)) {
    hr = mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (sampleRate / 4) * channels * bitsPerSample / 8 / 4);
  }
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
  if (FAILED(hr)) {
    rel(mediaType);
    return hr;
  }
  *type = mediaType;
  return S_OK;
}

HRESULT makePcmInputType(UINT32 sampleRate, UINT32 channels, UINT32 bitsPerSample, IMFMediaType** type) {
  IMFMediaType* mediaType = nullptr;
  HRESULT hr = MFCreateMediaType(&mediaType);
  if (FAILED(hr)) return hr;
  const UINT32 blockAlign = channels * bitsPerSample / 8;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  if (SUCCEEDED(hr)) hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * blockAlign);
  if (FAILED(hr)) {
    rel(mediaType);
    return hr;
  }
  *type = mediaType;
  return S_OK;
}

// ------------------------------------------------------------------ frame data

const UINT32 kWidth = 1280;
const UINT32 kHeight = 720;
const UINT32 kFps = 30;
const UINT32 kSampleBytesPerPixel = 4;

struct Rgb24Frame {
  std::vector<uint8_t> pixels;  // top-down, 3 bytes per pixel, R G B
};

void synthFrame(int index, Rgb24Frame* frame) {
  if (frame->pixels.size() != static_cast<size_t>(kWidth) * kHeight * 3)
    frame->pixels.assign(static_cast<size_t>(kWidth) * kHeight * 3, 0);
  uint8_t* row = frame->pixels.data();
  for (UINT32 y = 0; y < kHeight; ++y) {
    const UINT32 g = y * 255 / (kHeight - 1);
    for (UINT32 x = 0; x < kWidth; ++x) {
      const UINT32 r = x * 255 / (kWidth - 1);
      row[x * 3 + 0] = static_cast<uint8_t>(r);
      row[x * 3 + 1] = static_cast<uint8_t>(g);
      row[x * 3 + 2] = static_cast<uint8_t>(255 - r);
    }
    row += static_cast<size_t>(kWidth) * 3;
  }
  const int barWidth = 96;
  const int barX = static_cast<int>((static_cast<long long>(index) * 20) % (kWidth - barWidth));
  for (UINT32 y = 120; y < 600; ++y) {
    uint8_t* line = frame->pixels.data() + static_cast<size_t>(y) * kWidth * 3;
    for (int x = barX; x < barX + barWidth; ++x) {
      line[x * 3 + 0] = 250;
      line[x * 3 + 1] = 250;
      line[x * 3 + 2] = 250;
    }
  }
}

void rgb24ToRgb32(const Rgb24Frame& source, std::vector<uint8_t>* destination) {
  // MFVideoFormat_RGB32 is BGRA in memory and bottom-up by default.
  destination->assign(static_cast<size_t>(kWidth) * kHeight * 4, 0xFF);
  uint8_t* target = destination->data();
  for (UINT32 y = 0; y < kHeight; ++y) {
    const uint8_t* sourceRow = source.pixels.data() + static_cast<size_t>(kHeight - 1 - y) * kWidth * 3;
    for (UINT32 x = 0; x < kWidth; ++x) {
      target[x * 4 + 0] = sourceRow[x * 3 + 2];
      target[x * 4 + 1] = sourceRow[x * 3 + 1];
      target[x * 4 + 2] = sourceRow[x * 3 + 0];
      target[x * 4 + 3] = 0xFF;
    }
    target += static_cast<size_t>(kWidth) * 4;
  }
}

uint8_t clampByte(int value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<uint8_t>(value);
}

void rgb24ToNv12(const Rgb24Frame& source, std::vector<uint8_t>* destination) {
  const size_t lumaSize = static_cast<size_t>(kWidth) * kHeight;
  destination->assign(lumaSize * 3 / 2, 128);
  uint8_t* luma = destination->data();
  uint8_t* chroma = destination->data() + lumaSize;
  for (UINT32 y = 0; y < kHeight; ++y) {
    const uint8_t* sourceRow = source.pixels.data() + static_cast<size_t>(y) * kWidth * 3;
    for (UINT32 x = 0; x < kWidth; ++x) {
      const int r = sourceRow[x * 3 + 0];
      const int g = sourceRow[x * 3 + 1];
      const int b = sourceRow[x * 3 + 2];
      const double yValue = 0.2126 * r + 0.7152 * g + 0.0722 * b;
      luma[static_cast<size_t>(y) * kWidth + x] = clampByte(static_cast<int>(std::lround(16.0 + yValue * 219.0 / 255.0)));
      if ((y % 2) == 0 && (x % 2) == 0) {
        const double cb = (b - yValue) / 1.8556;
        const double cr = (r - yValue) / 1.5748;
        const size_t offset = (static_cast<size_t>(y / 2) * (kWidth / 2) + (x / 2)) * 2;
        chroma[offset + 0] = clampByte(static_cast<int>(std::lround(128.0 + cb * 224.0 / 255.0)));
        chroma[offset + 1] = clampByte(static_cast<int>(std::lround(128.0 + cr * 224.0 / 255.0)));
      }
    }
  }
}

void rgb24ToYuy2(const Rgb24Frame& source, std::vector<uint8_t>* destination) {
  destination->assign(static_cast<size_t>(kWidth) * kHeight * 2, 0);
  uint8_t* target = destination->data();
  for (UINT32 y = 0; y < kHeight; ++y) {
    const uint8_t* sourceRow = source.pixels.data() + static_cast<size_t>(y) * kWidth * 3;
    for (UINT32 pair = 0; pair < kWidth / 2; ++pair) {
      double yValues[2]{};
      double sumB = 0.0;
      double sumR = 0.0;
      for (int lane = 0; lane < 2; ++lane) {
        const UINT32 x = pair * 2 + static_cast<UINT32>(lane);
        const int r = sourceRow[x * 3 + 0];
        const int g = sourceRow[x * 3 + 1];
        const int b = sourceRow[x * 3 + 2];
        yValues[lane] = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        sumB += b;
        sumR += r;
      }
      const double avgY = (yValues[0] + yValues[1]) / 2.0;
      const double cb = (sumB / 2.0 - avgY) / 1.8556;
      const double cr = (sumR / 2.0 - avgY) / 1.5748;
      size_t offset = (static_cast<size_t>(y) * kWidth + pair * 2) * 2;
      target[offset + 0] = clampByte(static_cast<int>(std::lround(16.0 + yValues[0] * 219.0 / 255.0)));
      target[offset + 1] = clampByte(static_cast<int>(std::lround(128.0 + cb * 224.0 / 255.0)));
      target[offset + 2] = clampByte(static_cast<int>(std::lround(16.0 + yValues[1] * 219.0 / 255.0)));
      target[offset + 3] = clampByte(static_cast<int>(std::lround(128.0 + cr * 224.0 / 255.0)));
    }
  }
}

// --------------------------------------------------------------- PCM + WAV I/O

std::vector<uint8_t> makeSinePcm(UINT32 sampleRate, UINT32 channels, UINT32 bitsPerSample, double seconds) {
  const size_t totalFrames = static_cast<size_t>(std::llround(seconds * sampleRate));
  std::vector<uint8_t> pcm(totalFrames * channels * (bitsPerSample / 8), 0);
  const double twoPi = 6.28318530717958647692;
  for (size_t frame = 0; frame < totalFrames; ++frame) {
    const double t = static_cast<double>(frame) / sampleRate;
    for (UINT32 channel = 0; channel < channels; ++channel) {
      const double frequency = channel == 0 ? 440.0 : 660.0;
      const double value = std::sin(twoPi * frequency * t) * 0.25 *
                           std::min(1.0, static_cast<double>(totalFrames - frame) / (sampleRate / 50.0));
      const int16_t sample = static_cast<int16_t>(std::lround(value * 32767.0));
      const size_t offset = (frame * channels + channel) * 2;
      pcm[offset + 0] = static_cast<uint8_t>(sample & 0xFF);
      pcm[offset + 1] = static_cast<uint8_t>((static_cast<uint16_t>(sample) >> 8) & 0xFF);
    }
  }
  return pcm;
}

bool writeWav(const std::string& path, const std::vector<uint8_t>& pcm, UINT32 sampleRate, UINT32 channels, UINT32 bitsPerSample) {
  FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) return false;
  const UINT32 blockAlign = channels * bitsPerSample / 8;
  const UINT32 byteRate = sampleRate * blockAlign;
  const UINT32 dataSize = static_cast<UINT32>(pcm.size());
  const UINT32 riffSize = 36 + dataSize;
  auto put32 = [&](UINT32 value) { std::fwrite(&value, 4, 1, file); };
  auto put16 = [&](UINT16 value) { std::fwrite(&value, 2, 1, file); };
  std::fwrite("RIFF", 1, 4, file);
  put32(riffSize);
  std::fwrite("WAVE", 1, 4, file);
  std::fwrite("fmt ", 1, 4, file);
  put32(16);
  put16(1);
  put16(static_cast<UINT16>(channels));
  put32(sampleRate);
  put32(byteRate);
  put16(static_cast<UINT16>(blockAlign));
  put16(static_cast<UINT16>(bitsPerSample));
  std::fwrite("data", 1, 4, file);
  put32(dataSize);
  const bool ok = std::fwrite(pcm.data(), 1, pcm.size(), file) == pcm.size();
  std::fclose(file);
  return ok;
}

bool readWav(const std::string& path, std::vector<uint8_t>* pcm, UINT32* sampleRate, UINT32* channels, UINT32* bitsPerSample) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return false;
  std::vector<uint8_t> bytes;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size <= 44) {
    std::fclose(file);
    return false;
  }
  bytes.resize(static_cast<size_t>(size));
  const bool readOk = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
  std::fclose(file);
  if (!readOk) return false;
  if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return false;
  size_t offset = 12;
  bool haveFormat = false;
  while (offset + 8 <= bytes.size()) {
    char chunk[5]{};
    std::memcpy(chunk, bytes.data() + offset, 4);
    UINT32 chunkSize = 0;
    std::memcpy(&chunkSize, bytes.data() + offset + 4, 4);
    const size_t payload = offset + 8;
    if (std::memcmp(chunk, "fmt ", 4) == 0 && payload + 16 <= bytes.size()) {
      UINT16 format = 0;
      UINT16 channelCount = 0;
      UINT32 rate = 0;
      UINT16 align = 0;
      UINT16 bits = 0;
      std::memcpy(&format, bytes.data() + payload + 0, 2);
      std::memcpy(&channelCount, bytes.data() + payload + 2, 2);
      std::memcpy(&rate, bytes.data() + payload + 4, 4);
      std::memcpy(&align, bytes.data() + payload + 12, 2);
      std::memcpy(&bits, bytes.data() + payload + 14, 2);
      if (format != 1) return false;
      *sampleRate = rate;
      *channels = channelCount;
      *bitsPerSample = bits;
      (void)align;
      haveFormat = true;
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      if (!haveFormat) return false;
      const size_t available = bytes.size() - payload;
      const size_t copySize = std::min(static_cast<size_t>(chunkSize), available);
      pcm->assign(bytes.begin() + static_cast<std::ptrdiff_t>(payload),
                  bytes.begin() + static_cast<std::ptrdiff_t>(payload + copySize));
      return true;
    }
    offset = payload + chunkSize + (chunkSize & 1u);
  }
  return false;
}

// ---------------------------------------------------------------- BMP 24-bit IO

bool writeBmp24(const std::string& path, const Rgb24Frame& frame) {
  FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) return false;
  const UINT32 rowStride = (kWidth * 3 + 3) & ~3u;
  const UINT32 imageSize = rowStride * kHeight;
  BITMAPFILEHEADER fileHeader{};
  fileHeader.bfType = 0x4D42;
  fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  fileHeader.bfSize = fileHeader.bfOffBits + imageSize;
  BITMAPINFOHEADER infoHeader{};
  infoHeader.biSize = sizeof(BITMAPINFOHEADER);
  infoHeader.biWidth = static_cast<LONG>(kWidth);
  infoHeader.biHeight = static_cast<LONG>(kHeight);
  infoHeader.biPlanes = 1;
  infoHeader.biBitCount = 24;
  infoHeader.biCompression = BI_RGB;
  infoHeader.biSizeImage = imageSize;
  std::fwrite(&fileHeader, sizeof(fileHeader), 1, file);
  std::fwrite(&infoHeader, sizeof(infoHeader), 1, file);
  std::vector<uint8_t> row(rowStride, 0);
  for (int y = static_cast<int>(kHeight) - 1; y >= 0; --y) {  // bottom-up
    const uint8_t* sourceRow = frame.pixels.data() + static_cast<size_t>(y) * kWidth * 3;
    for (UINT32 x = 0; x < kWidth; ++x) {
      row[x * 3 + 0] = sourceRow[x * 3 + 2];
      row[x * 3 + 1] = sourceRow[x * 3 + 1];
      row[x * 3 + 2] = sourceRow[x * 3 + 0];
    }
    std::fwrite(row.data(), 1, rowStride, file);
  }
  std::fclose(file);
  return true;
}

bool readBmp24(const std::string& path, Rgb24Frame* frame) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return false;
  BITMAPFILEHEADER fileHeader{};
  BITMAPINFOHEADER infoHeader{};
  bool ok = std::fread(&fileHeader, sizeof(fileHeader), 1, file) == 1 &&
            std::fread(&infoHeader, sizeof(infoHeader), 1, file) == 1;
  if (!ok || fileHeader.bfType != 0x4D42) {
    std::fclose(file);
    return false;
  }
  const bool bottomUp = infoHeader.biHeight > 0;
  const UINT32 height = static_cast<UINT32>(bottomUp ? infoHeader.biHeight : -infoHeader.biHeight);
  const UINT32 width = static_cast<UINT32>(infoHeader.biWidth);
  const UINT32 bits = infoHeader.biBitCount;
  if (width != kWidth || height != kHeight || (bits != 24 && bits != 32) ||
      (infoHeader.biCompression != BI_RGB && infoHeader.biCompression != BI_BITFIELDS)) {
    std::fclose(file);
    return false;
  }
  const UINT32 pixelStride = bits / 8;
  const UINT32 rowStride = ((width * pixelStride) + 3) & ~3u;
  std::vector<uint8_t> row(rowStride, 0);
  std::fseek(file, static_cast<long>(fileHeader.bfOffBits), SEEK_SET);
  frame->pixels.assign(static_cast<size_t>(width) * height * 3, 0);
  for (UINT32 line = 0; line < height; ++line) {
    if (std::fread(row.data(), 1, rowStride, file) != rowStride) {
      std::fclose(file);
      return false;
    }
    const UINT32 y = bottomUp ? (height - 1 - line) : line;
    uint8_t* target = frame->pixels.data() + static_cast<size_t>(y) * width * 3;
    for (UINT32 x = 0; x < width; ++x) {
      target[x * 3 + 0] = row[x * pixelStride + 2];
      target[x * 3 + 1] = row[x * pixelStride + 1];
      target[x * 3 + 2] = row[x * pixelStride + 0];
    }
  }
  std::fclose(file);
  return true;
}

// ------------------------------------------------------------------- encoding

struct EncodeStats {
  bool writerCreated = false;
  bool beginWriting = false;
  bool videoStreamAdded = false;
  bool audioStreamAdded = false;
  bool videoInputSet = false;
  bool audioInputSet = false;
  HRESULT writerHr = E_FAIL;
  HRESULT beginHr = E_FAIL;
  HRESULT videoAddHr = E_FAIL;
  HRESULT audioAddHr = E_FAIL;
  HRESULT videoInputHr = E_FAIL;
  HRESULT audioInputHr = E_FAIL;
  HRESULT finalizeHr = E_FAIL;
  HRESULT firstWriteHr = E_FAIL;
  long long totalMs = 0;
  double writeMs = 0.0;
  double convertMs = 0.0;
  unsigned long long bytes = 0;
  int videoSamplesWritten = 0;
  int audioSamplesWritten = 0;
  std::string failure;
};

using FrameProvider = std::function<bool(int index, Rgb24Frame* frame)>;

void toInputBuffer(const Rgb24Frame& frame, const GUID& subtype, std::vector<uint8_t>* buffer) {
  if (subtype == MFVideoFormat_NV12)
    rgb24ToNv12(frame, buffer);
  else if (subtype == MFVideoFormat_YUY2)
    rgb24ToYuy2(frame, buffer);
  else
    rgb24ToRgb32(frame, buffer);
}

EncodeStats encodeToFile(const std::string& pathUtf8,
                         const VideoShape& shape,
                         const GUID& videoInputSubtype,
                         int frameCount,
                         const std::vector<uint8_t>* audioPcm,
                         UINT32 audioSampleRate,
                         UINT32 audioChannels,
                         UINT32 audioBitsPerSample,
                         const FrameProvider& provider,
                         bool withProfile) {
  EncodeStats stats;
  const long long start = nowMs();
  const std::wstring path = utf8ToWide(pathUtf8);

  IMFSinkWriter* writer = nullptr;
  stats.writerHr = createSinkWriterMp4(path, &writer);
  stats.writerCreated = SUCCEEDED(stats.writerHr);
  if (!stats.writerCreated) {
    stats.failure = "MFCreateSinkWriterFromURL failed";
    stats.totalMs = nowMs() - start;
    return stats;
  }

  DWORD videoStream = 0;
  IMFMediaType* videoOutput = nullptr;
  stats.videoAddHr = makeH264OutputType(shape, &videoOutput, withProfile);
  if (SUCCEEDED(stats.videoAddHr)) stats.videoAddHr = writer->AddStream(videoOutput, &videoStream);
  rel(videoOutput);
  stats.videoStreamAdded = SUCCEEDED(stats.videoAddHr);

  if (stats.videoStreamAdded) {
    IMFMediaType* videoInput = nullptr;
    stats.videoInputHr = makeVideoInputType(shape, videoInputSubtype, &videoInput);
    if (SUCCEEDED(stats.videoInputHr)) stats.videoInputHr = writer->SetInputMediaType(videoStream, videoInput, nullptr);
    rel(videoInput);
    stats.videoInputSet = SUCCEEDED(stats.videoInputHr);
  }

  DWORD audioStream = 0;
  bool wantAudio = audioPcm != nullptr && !audioPcm->empty();
  if (wantAudio) {
    IMFMediaType* audioOutput = nullptr;
    stats.audioAddHr = makeAacOutputType(audioSampleRate, audioChannels, audioBitsPerSample, &audioOutput);
    if (SUCCEEDED(stats.audioAddHr)) stats.audioAddHr = writer->AddStream(audioOutput, &audioStream);
    rel(audioOutput);
    stats.audioStreamAdded = SUCCEEDED(stats.audioAddHr);
    if (stats.audioStreamAdded) {
      IMFMediaType* audioInput = nullptr;
      stats.audioInputHr = makePcmInputType(audioSampleRate, audioChannels, audioBitsPerSample, &audioInput);
      if (SUCCEEDED(stats.audioInputHr)) stats.audioInputHr = writer->SetInputMediaType(audioStream, audioInput, nullptr);
      rel(audioInput);
      stats.audioInputSet = SUCCEEDED(stats.audioInputHr);
    }
  }

  if (!stats.videoInputSet) {
    stats.failure = "video SetInputMediaType failed or stream missing";
    rel(writer);
    stats.totalMs = nowMs() - start;
    return stats;
  }

  stats.beginHr = writer->BeginWriting();
  stats.beginWriting = SUCCEEDED(stats.beginHr);

  Rgb24Frame frame;
  std::vector<uint8_t> inputBuffer;
  const LONGLONG frameDuration = 10000000LL / shape.fpsNumerator * shape.fpsDenominator;
  for (int index = 0; index < frameCount; ++index) {
    if (!provider(index, &frame)) {
      stats.failure = "frame provider failed";
      break;
    }
    const double convertStart = nowUs();
    toInputBuffer(frame, videoInputSubtype, &inputBuffer);
    stats.convertMs += (nowUs() - convertStart) / 1000.0;

    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(inputBuffer.size()), &buffer);
    if (SUCCEEDED(hr)) {
      BYTE* destination = nullptr;
      hr = buffer->Lock(&destination, nullptr, nullptr);
      if (SUCCEEDED(hr)) {
        std::memcpy(destination, inputBuffer.data(), inputBuffer.size());
        buffer->Unlock();
        buffer->SetCurrentLength(static_cast<DWORD>(inputBuffer.size()));
      }
    }
    IMFSample* sample = nullptr;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(static_cast<LONGLONG>(index) * frameDuration);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(frameDuration);
    if (SUCCEEDED(hr)) {
      const double writeStart = nowUs();
      hr = writer->WriteSample(videoStream, sample);
      stats.writeMs += (nowUs() - writeStart) / 1000.0;
      if (SUCCEEDED(hr) && index == 0) stats.firstWriteHr = hr;
      if (SUCCEEDED(hr)) ++stats.videoSamplesWritten;
    }
    rel(sample);
    rel(buffer);
    if (FAILED(hr)) {
      char message[160];
      std::snprintf(message, sizeof(message), "video WriteSample frame %d hr=%s", index, hrStr(hr).c_str());
      stats.failure = message;
      break;
    }
  }

  if (wantAudio && stats.audioInputSet && stats.failure.empty()) {
    const UINT32 blockAlign = audioChannels * audioBitsPerSample / 8;
    const size_t bytesPerSample = 1024ull * blockAlign;  // 1024 frames == one AAC frame
    const LONGLONG audioFrameDuration = 1024LL * 10000000LL / audioSampleRate;
    size_t offset = 0;
    LONGLONG timestamp = 0;
    while (offset < audioPcm->size()) {
      const size_t take = std::min(bytesPerSample, audioPcm->size() - offset);
      IMFMediaBuffer* buffer = nullptr;
      HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(take), &buffer);
      if (SUCCEEDED(hr)) {
        BYTE* destination = nullptr;
        hr = buffer->Lock(&destination, nullptr, nullptr);
        if (SUCCEEDED(hr)) {
          std::memcpy(destination, audioPcm->data() + offset, take);
          buffer->Unlock();
          buffer->SetCurrentLength(static_cast<DWORD>(take));
        }
      }
      IMFSample* sample = nullptr;
      if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
      if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
      if (SUCCEEDED(hr)) hr = sample->SetSampleTime(timestamp);
      if (SUCCEEDED(hr)) {
        const LONGLONG duration = static_cast<LONGLONG>((take / blockAlign) * 10000000LL / audioSampleRate);
        hr = sample->SetSampleDuration(duration);
      }
      if (SUCCEEDED(hr)) {
        hr = writer->WriteSample(audioStream, sample);
        if (SUCCEEDED(hr)) ++stats.audioSamplesWritten;
      }
      rel(sample);
      rel(buffer);
      if (FAILED(hr)) {
        char message[160];
        std::snprintf(message, sizeof(message), "audio WriteSample offset=%zu hr=%s", offset, hrStr(hr).c_str());
        stats.failure = message;
        break;
      }
      offset += take;
      timestamp += audioFrameDuration;
    }
  }

  stats.finalizeHr = writer->Finalize();
  rel(writer);
  stats.totalMs = nowMs() - start;
  const double bytes = fileSizeBytes(pathUtf8);
  stats.bytes = bytes > 0.0 ? static_cast<unsigned long long>(bytes) : 0ull;
  return stats;
}

// ------------------------------------------------------------------- Q1 and Q2

struct EncoderEntry {
  std::wstring name;
  GUID clsid{};
  bool clsidValid = false;
  HRESULT activateHr = E_FAIL;
  bool hasTargetOutput = false;
  std::string typeNotes;
};

std::string dumpTransformTypes(IMFTransform* transform,
                               const GUID& targetSubtype,
                               bool* hasTarget,
                               bool trySetOutput) {
  std::string notes;
  char line[512];
  bool foundTargetType = false;
  IMFMediaType* targetType = nullptr;
  DWORD outputCount = 0;
  for (DWORD index = 0; index < 64; ++index) {
    IMFMediaType* type = nullptr;
    const HRESULT hr = transform->GetOutputAvailableType(0, index, &type);
    if (hr == MF_E_NO_MORE_TYPES) {
      std::snprintf(line, sizeof(line), "outputTypes=%lu", outputCount);
      notes += line;
      notes += " (terminated by MF_E_NO_MORE_TYPES)";
      break;
    }
    if (FAILED(hr)) {
      std::snprintf(line, sizeof(line), "GetOutputAvailableType(%lu) hr=%s", index, hrStr(hr).c_str());
      notes += line;
      break;
    }
    ++outputCount;
    GUID major{};
    GUID subtype{};
    type->GetGUID(MF_MT_MAJOR_TYPE, &major);
    type->GetGUID(MF_MT_SUBTYPE, &subtype);
    UINT32 width = 0;
    UINT32 height = 0;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
    std::snprintf(line, sizeof(line), " out[%lu]=%s|%s", index, majorTypeName(major).c_str(),
                  subtypeName(subtype).c_str());
    notes += line;
    if (width != 0 && height != 0) {
      std::snprintf(line, sizeof(line), " %ux%u", width, height);
      notes += line;
    }
    if (subtype == targetSubtype) {
      foundTargetType = true;
      if (targetType == nullptr) {
        targetType = type;
        type = nullptr;
      }
    }
    rel(type);
  }
  if (!foundTargetType && trySetOutput) {
    // Some MFTs (notably hardware ones) refuse GetOutputAvailableType; the only
    // honest test left is to hand them a well-formed type and observe the HRESULT.
    IMFMediaType* handmade = nullptr;
    if (targetSubtype == MFVideoFormat_H264) {
      VideoShape shape;
      makeH264OutputType(shape, &handmade, false);
    } else {
      makeAacOutputType(48000, 2, 16, &handmade);
    }
    if (handmade != nullptr) {
      const HRESULT hr = transform->SetOutputType(0, handmade, 0);
      std::snprintf(line, sizeof(line), " SetOutputType(handmade target) hr=%s", hrStr(hr).c_str());
      notes += line;
      if (SUCCEEDED(hr)) {
        foundTargetType = true;
        targetType = handmade;
        handmade = nullptr;
      }
      rel(handmade);
    }
  } else if (foundTargetType && trySetOutput) {
    const HRESULT hr = transform->SetOutputType(0, targetType, 0);
    std::snprintf(line, sizeof(line), " SetOutputType(first target type) hr=%s", hrStr(hr).c_str());
    notes += line;
    if (FAILED(hr)) rel(targetType);
  }
  if (targetType != nullptr) {
    DWORD inputCount = 0;
    for (DWORD index = 0; index < 64; ++index) {
      IMFMediaType* type = nullptr;
      const HRESULT hr = transform->GetInputAvailableType(0, index, &type);
      if (hr == MF_E_NO_MORE_TYPES) break;
      if (FAILED(hr)) {
        std::snprintf(line, sizeof(line), " inputs:GetInputAvailableType(%lu) hr=%s", index, hrStr(hr).c_str());
        notes += line;
        break;
      }
      ++inputCount;
      GUID major{};
      GUID subtype{};
      type->GetGUID(MF_MT_MAJOR_TYPE, &major);
      type->GetGUID(MF_MT_SUBTYPE, &subtype);
      UINT32 width = 0;
      UINT32 height = 0;
      MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
      std::snprintf(line, sizeof(line), " in[%lu]=%s|%s", index, majorTypeName(major).c_str(),
                    subtypeName(subtype).c_str());
      notes += line;
      if (width != 0 && height != 0) {
        std::snprintf(line, sizeof(line), " %ux%u", width, height);
        notes += line;
      }
      rel(type);
    }
    if (inputCount == 0) notes += " in[]=none-enumerated";
    rel(targetType);
  }
  if (hasTarget != nullptr) *hasTarget = foundTargetType;
  return notes;
}

std::vector<EncoderEntry> enumerateEncoders(const GUID& category,
                                            const GUID& majorType,
                                            const GUID* outputFilterSubtype,
                                            const char* label,
                                            bool withTypeDetails,
                                            const GUID& targetSubtype,
                                            UINT32* activateFailureCount) {
  std::vector<EncoderEntry> entries;
  IMFActivate** activations = nullptr;
  UINT32 count = 0;
  MFT_REGISTER_TYPE_INFO filter{};
  filter.guidMajorType = majorType;
  if (outputFilterSubtype != nullptr) filter.guidSubtype = *outputFilterSubtype;
  const UINT32 flags = MFT_ENUM_FLAG_ALL;
  const HRESULT hr = MFTEnumEx(category, flags, nullptr, outputFilterSubtype != nullptr ? &filter : nullptr,
                               &activations, &count);
  out("[%s] MFTEnumEx(category=%s flags=0x%X %s) hr=%s count=%u\n", label,
      category == MFT_CATEGORY_VIDEO_ENCODER ? "MFT_CATEGORY_VIDEO_ENCODER" : "MFT_CATEGORY_AUDIO_ENCODER",
      flags, outputFilterSubtype != nullptr ? "with-output-subtype-filter" : "no-type-filter",
      hrStr(hr).c_str(), count);
  if (FAILED(hr)) return entries;
  for (UINT32 index = 0; index < count; ++index) {
    EncoderEntry entry;
    LPWSTR name = nullptr;
    if (SUCCEEDED(activations[index]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, nullptr))) {
      entry.name = name != nullptr ? name : L"";
      CoTaskMemFree(name);
    } else {
      entry.name = L"<no-friendly-name>";
    }
    entry.clsidValid = SUCCEEDED(activations[index]->GetGUID(MFT_TRANSFORM_CLSID_Attribute, &entry.clsid));
    if (withTypeDetails) {
      IMFTransform* transform = nullptr;
      entry.activateHr = activations[index]->ActivateObject(__uuidof(IMFTransform), reinterpret_cast<void**>(&transform));
      if (SUCCEEDED(entry.activateHr) && transform != nullptr) {
        bool hasTarget = false;
        entry.typeNotes = dumpTransformTypes(transform, targetSubtype, &hasTarget, true);
        entry.hasTargetOutput = hasTarget;
        rel(transform);
      } else {
        if (activateFailureCount != nullptr) ++(*activateFailureCount);
      }
    }
    entries.push_back(entry);
    out("[%s]   #%u name=\"%s\" clsid=%s activate=%s targetOutput=%s%s\n", label, index,
        wideToUtf8(entry.name.c_str()).c_str(),
        entry.clsidValid ? guidStr(entry.clsid).c_str() : "<no-clsid>",
        withTypeDetails ? hrStr(entry.activateHr).c_str() : "(not-activated)",
        withTypeDetails ? (entry.hasTargetOutput ? "yes" : "no") : "?",
        entry.typeNotes.empty() ? "" : "");
    if (!entry.typeNotes.empty()) out("[%s]       %s\n", label, entry.typeNotes.c_str());
  }
  for (UINT32 index = 0; index < count; ++index) rel(activations[index]);
  CoTaskMemFree(activations);
  return entries;
}

// -------------------------------------------------------------------- Q6 readback

struct ReadbackResult {
  HRESULT createHr = E_FAIL;
  HRESULT durationHr = E_FAIL;
  unsigned long long duration100ns = 0;
  int streamCount = 0;
  std::vector<std::string> streamLines;
  HRESULT decodeHr = E_FAIL;
  HRESULT decodeMediaTypeHr = E_FAIL;
  bool gotFrame = false;
  LONGLONG firstTimestamp = 0;
  LONGLONG lastTimestamp = 0;
  DWORD firstBufferBytes = 0;
  int decodedFrameCount = 0;
  DWORD firstFlags = 0;
  std::string decodedSubtype;
  int videoStreamIndex = -1;
  int audioStreamIndex = -1;
};

ReadbackResult readback(const std::string& pathUtf8) {
  ReadbackResult result;
  const std::wstring path = utf8ToWide(pathUtf8);
  IMFSourceReader* reader = nullptr;
  result.createHr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
  if (FAILED(result.createHr)) return result;

  PROPVARIANT duration{};
  result.durationHr = reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration);
  if (SUCCEEDED(result.durationHr) && duration.vt == VT_UI8) result.duration100ns = duration.uhVal.QuadPart;

  for (DWORD index = 0; index < 16; ++index) {
    IMFMediaType* native = nullptr;
    const HRESULT hr = reader->GetNativeMediaType(index, 0, &native);
    if (FAILED(hr)) {
      char line[160];
      std::snprintf(line, sizeof(line), "stream[%lu] GetNativeMediaType hr=%s (end of streams)", index, hrStr(hr).c_str());
      result.streamLines.push_back(line);
      break;
    }
    ++result.streamCount;
    GUID major{};
    GUID subtype{};
    native->GetGUID(MF_MT_MAJOR_TYPE, &major);
    native->GetGUID(MF_MT_SUBTYPE, &subtype);
    PROPVARIANT streamDuration{};
    unsigned long long streamDurationValue = 0;
    if (SUCCEEDED(reader->GetPresentationAttribute(index, MF_PD_DURATION, &streamDuration)) &&
        streamDuration.vt == VT_UI8) {
      streamDurationValue = streamDuration.uhVal.QuadPart;
    }
    char line[512];
    std::snprintf(line, sizeof(line), "stream[%lu] major=%s subtype=%s duration=%.3fs",
                  index, majorTypeName(major).c_str(), subtypeName(subtype).c_str(),
                  static_cast<double>(streamDurationValue) / 10000000.0);
    if (major == MFMediaType_Video) {
      UINT32 width = 0;
      UINT32 height = 0;
      MFGetAttributeSize(native, MF_MT_FRAME_SIZE, &width, &height);
      UINT32 rateNum = 0;
      UINT32 rateDen = 0;
      MFGetAttributeRatio(native, MF_MT_FRAME_RATE, &rateNum, &rateDen);
      char extra[128];
      std::snprintf(extra, sizeof(extra), " framesize=%ux%u fps=%u/%u", width, height, rateNum, rateDen);
      result.streamLines.push_back(std::string(line) + extra);
      if (result.videoStreamIndex < 0) result.videoStreamIndex = static_cast<int>(index);
    } else if (major == MFMediaType_Audio) {
      UINT32 rate = 0;
      UINT32 channels = 0;
      native->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
      native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
      char extra[128];
      std::snprintf(extra, sizeof(extra), " rate=%u channels=%u", rate, channels);
      result.streamLines.push_back(std::string(line) + extra);
      if (result.audioStreamIndex < 0) result.audioStreamIndex = static_cast<int>(index);
    } else {
      result.streamLines.push_back(line);
    }
    rel(native);
  }

  if (result.videoStreamIndex >= 0) {
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(static_cast<DWORD>(result.videoStreamIndex), TRUE);
    // Without an explicit current media type the source reader hands back the
    // compressed H.264 samples from the container and never inserts the decoder.
    // Asking for NV12 is what makes "did a frame decode" an honest question.
    IMFMediaType* requested = nullptr;
    if (SUCCEEDED(MFCreateMediaType(&requested)) && requested != nullptr) {
      requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
      requested->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
      result.decodeMediaTypeHr = reader->SetCurrentMediaType(static_cast<DWORD>(result.videoStreamIndex), nullptr, requested);
      rel(requested);
    }
    for (int attempt = 0; attempt < 80; ++attempt) {
      DWORD actualStream = 0;
      DWORD flags = 0;
      LONGLONG timestamp = 0;
      IMFSample* sample = nullptr;
      const HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &actualStream, &flags, &timestamp, &sample);
      if (FAILED(hr)) {
        if (attempt == 0) result.decodeHr = hr;
        rel(sample);
        break;
      }
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        rel(sample);
        break;
      }
      if (sample != nullptr && (flags & MF_SOURCE_READERF_STREAMTICK) == 0) {
        if (!result.gotFrame) {
          result.gotFrame = true;
          result.decodeHr = hr;
          result.firstTimestamp = timestamp;
          result.firstFlags = flags;
          IMFMediaBuffer* buffer = nullptr;
          if (SUCCEEDED(sample->GetBufferByIndex(0, &buffer)) && buffer != nullptr) {
            DWORD length = 0;
            buffer->GetCurrentLength(&length);
            result.firstBufferBytes = length;
            rel(buffer);
          }
        }
        result.lastTimestamp = timestamp;
        ++result.decodedFrameCount;
      }
      rel(sample);
    }
    IMFMediaType* current = nullptr;
    if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current)) && current != nullptr) {
      GUID subtype{};
      current->GetGUID(MF_MT_SUBTYPE, &subtype);
      result.decodedSubtype = subtypeName(subtype);
      rel(current);
    }
  }
  rel(reader);
  return result;
}

// ------------------------------------------------------------------ sample hex

std::string firstBytesHex(const std::string& pathUtf8, int count) {
  const std::wstring path = utf8ToWide(pathUtf8);
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return "<open-failed>";
  uint8_t bytes[32]{};
  DWORD read = 0;
  const BOOL ok = ReadFile(handle, bytes, static_cast<DWORD>(count), &read, nullptr);
  CloseHandle(handle);
  if (!ok) return "<read-failed>";
  std::string text;
  char pair[8];
  for (DWORD index = 0; index < read; ++index) {
    std::snprintf(pair, sizeof(pair), "%02x", bytes[index]);
    if (!text.empty()) text += ' ';
    text += pair;
  }
  return text;
}

// ------------------------------------------------- task A: re-readable dumps
//
// WHY THIS SECTION EXISTS (N27 export a-priori; m06702 / m00700)
//   "the probe can encode" is not the claim "each input reached the file". The
//   claim is made checkable by giving every link its own on-disk artifact and a
//   DIFFERENT mode that reads it back:
//     --dump-inputs <dir>              frames.rgb24 + frames.txt + pcm.raw + pcm.txt
//     --encode-dump <dir> --mp4 <file> READS those dumps back (verifies every frame
//                                      sha256 and the pcm sha256) and encodes the
//                                      artifact, so the encoder input is the dump
//                                      and not a fresh synthFrame() call
//     --decode <file> --dump-decoded <dir> [--expect-dump <dir>]
//                                      decodes every video frame and every audio
//                                      sample, writes decoded_frames.rgb24 /
//                                      decoded_video.txt / decoded_audio.raw /
//                                      decoded_audio.txt, re-reads what it wrote,
//                                      and asserts counts against the source dump
//     --normalize <in.mp4> <out.mp4>   independent step: zero the 10 container
//                                      creation/modification fields (mvhd 2, each
//                                      trak's tkhd 2 + mdia/mdhd 2) located by real
//                                      box parsing and the box version byte, never
//                                      by a fixed file offset
//
//   sha256 is implemented here so the probe can print per-artifact digests; the
//   verify script cross-checks the whole-file digest against the system sha256sum,
//   which is the independent control on this implementation.

const int kFrameCount = 60;

struct Sha256 {
  uint32_t state[8];
  uint64_t totalBytes;
  uint8_t buffer[64];
  size_t bufferSize;

  Sha256() { reset(); }

  void reset() {
    static const uint32_t initial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    for (int index = 0; index < 8; ++index) state[index] = initial[index];
    totalBytes = 0;
    bufferSize = 0;
  }

  static uint32_t rotate(uint32_t value, int bits) { return (value >> bits) | (value << (32 - bits)); }

  void compress(const uint8_t* block) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    uint32_t w[64];
    for (int index = 0; index < 16; ++index) {
      w[index] = (static_cast<uint32_t>(block[index * 4]) << 24) | (static_cast<uint32_t>(block[index * 4 + 1]) << 16) |
                 (static_cast<uint32_t>(block[index * 4 + 2]) << 8) | static_cast<uint32_t>(block[index * 4 + 3]);
    }
    for (int index = 16; index < 64; ++index) {
      const uint32_t s0 = rotate(w[index - 15], 7) ^ rotate(w[index - 15], 18) ^ (w[index - 15] >> 3);
      const uint32_t s1 = rotate(w[index - 2], 17) ^ rotate(w[index - 2], 19) ^ (w[index - 2] >> 10);
      w[index] = w[index - 16] + s0 + w[index - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int index = 0; index < 64; ++index) {
      const uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
      const uint32_t ch = (e & f) ^ ((~e) & g);
      const uint32_t t1 = h + s1 + ch + k[index] + w[index];
      const uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
  }

  void update(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    totalBytes += size;
    while (size > 0) {
      const size_t take = std::min(size, sizeof(buffer) - bufferSize);
      std::memcpy(buffer + bufferSize, bytes, take);
      bufferSize += take;
      bytes += take;
      size -= take;
      if (bufferSize == sizeof(buffer)) {
        compress(buffer);
        bufferSize = 0;
      }
    }
  }

  std::string finish() {
    const uint64_t bits = totalBytes * 8;
    const uint8_t marker = 0x80;
    update(&marker, 1);
    const uint8_t zero = 0;
    while (bufferSize != 56) update(&zero, 1);
    uint8_t length[8];
    for (int index = 0; index < 8; ++index) length[7 - index] = static_cast<uint8_t>((bits >> (8 * index)) & 0xFF);
    update(length, 8);
    char text[65];
    for (int index = 0; index < 8; ++index) std::snprintf(text + index * 8, 9, "%08x", state[index]);
    return std::string(text, 64);
  }
};

std::string sha256Hex(const void* data, size_t size) {
  Sha256 digest;
  digest.update(data, size);
  return digest.finish();
}

const char* yesNo(bool value) { return value ? "yes" : "no"; }

uint32_t be32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

uint64_t be64(const uint8_t* bytes) {
  return (static_cast<uint64_t>(be32(bytes)) << 32) | static_cast<uint64_t>(be32(bytes + 4));
}

bool readFileAll(const std::string& path, std::vector<uint8_t>* data) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return false;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size < 0) { std::fclose(file); return false; }
  data->assign(static_cast<size_t>(size), 0);
  const size_t got = data->empty() ? 0 : std::fread(data->data(), 1, data->size(), file);
  std::fclose(file);
  return got == data->size();
}

bool writeFileAll(const std::string& path, const void* data, size_t size) {
  FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) return false;
  const size_t put = size == 0 ? 0 : std::fwrite(data, 1, size, file);
  const bool closed = std::fclose(file) == 0;
  return put == size && closed;
}

bool readTextFile(const std::string& path, std::string* text) {
  std::vector<uint8_t> data;
  if (!readFileAll(path, &data)) return false;
  text->assign(data.begin(), data.end());
  return true;
}

bool writeTextFile(const std::string& path, const std::string& text) {
  return writeFileAll(path, text.data(), text.size());
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) lines.push_back(line);
    start = end + 1;
  }
  return lines;
}

bool readKey(const std::string& line, const char* key, std::string* value) {
  const std::string prefix = std::string(key) + "=";
  if (line.compare(0, prefix.size(), prefix) != 0) return false;
  *value = line.substr(prefix.size());
  return true;
}

struct FrameManifest {
  int width = 0;
  int height = 0;
  int fps = 0;
  int count = 0;
  unsigned long long frameBytes = 0;
  std::vector<std::string> hashes;
};

struct PcmManifest {
  int sampleRate = 0;
  int channels = 0;
  int bits = 0;
  unsigned long long frames = 0;
  unsigned long long bytes = 0;
  std::string hash;
};

bool loadFrameManifest(const std::string& path, FrameManifest* manifest) {
  std::string text;
  if (!readTextFile(path, &text)) return false;
  int parsed = 0;
  for (const std::string& line : splitLines(text)) {
    std::string value;
    if (readKey(line, "width", &value)) manifest->width = std::atoi(value.c_str());
    else if (readKey(line, "height", &value)) manifest->height = std::atoi(value.c_str());
    else if (readKey(line, "fps", &value)) manifest->fps = std::atoi(value.c_str());
    else if (readKey(line, "count", &value)) manifest->count = std::atoi(value.c_str());
    else if (readKey(line, "frame_bytes", &value)) manifest->frameBytes = static_cast<unsigned long long>(std::stoull(value));
    else {
      int index = -1;
      char hex[128] = {0};
      if (std::sscanf(line.c_str(), "frame %d sha256 %127s", &index, hex) == 2) {
        if (index != parsed) return false;
        manifest->hashes.push_back(std::string(hex));
        ++parsed;
      }
    }
  }
  return manifest->count > 0 && static_cast<int>(manifest->hashes.size()) == manifest->count && manifest->frameBytes > 0;
}

bool loadPcmManifest(const std::string& path, PcmManifest* manifest) {
  std::string text;
  if (!readTextFile(path, &text)) return false;
  for (const std::string& line : splitLines(text)) {
    std::string value;
    if (readKey(line, "sample_rate", &value)) manifest->sampleRate = std::atoi(value.c_str());
    else if (readKey(line, "channels", &value)) manifest->channels = std::atoi(value.c_str());
    else if (readKey(line, "bits", &value)) manifest->bits = std::atoi(value.c_str());
    else if (readKey(line, "frames", &value)) manifest->frames = static_cast<unsigned long long>(std::stoull(value));
    else if (readKey(line, "bytes", &value)) manifest->bytes = static_cast<unsigned long long>(std::stoull(value));
    else if (readKey(line, "sha256", &value)) manifest->hash = value;
  }
  return manifest->sampleRate > 0 && manifest->channels > 0 && manifest->bits > 0 && manifest->frames > 0 &&
         manifest->bytes > 0 && !manifest->hash.empty();
}

int dumpInputs(const std::string& dir) {
  out("=== mf_export_probe: input dump (task A) ===\n");
  out("mode=dump-inputs dir=%s\n", dir.c_str());
  const std::string framesPath = dir + "/frames.rgb24";
  const std::string pcmPath = dir + "/pcm.raw";
  const unsigned long long frameBytes = static_cast<unsigned long long>(kWidth) * kHeight * 3;

  FILE* frames = std::fopen(framesPath.c_str(), "wb");
  if (frames == nullptr) {
    out("[dump-inputs] cannot open %s for writing\n", framesPath.c_str());
    return 6;
  }
  Sha256 framesSha;
  std::string frameManifest = "format=rgb24-frames-v1\n";
  frameManifest += "width=" + std::to_string(kWidth) + "\n";
  frameManifest += "height=" + std::to_string(kHeight) + "\n";
  frameManifest += "fps=" + std::to_string(kFps) + "\n";
  frameManifest += "count=" + std::to_string(kFrameCount) + "\n";
  frameManifest += "frame_bytes=" + std::to_string(frameBytes) + "\n";

  Rgb24Frame frame;
  bool framesWritten = true;
  for (int index = 0; index < kFrameCount; ++index) {
    synthFrame(index, &frame);
    const size_t size = frame.pixels.size();
    if (std::fwrite(frame.pixels.data(), 1, size, frames) != size) { framesWritten = false; break; }
    framesSha.update(frame.pixels.data(), size);
    frameManifest += "frame " + std::to_string(index) + " sha256 " + sha256Hex(frame.pixels.data(), size) + "\n";
  }
  const bool framesClosed = std::fclose(frames) == 0;

  const std::vector<uint8_t> pcm = makeSinePcm(48000, 2, 16, 2.0);
  const unsigned long long sampleFrames = pcm.size() / (2 * 2);
  const std::string pcmDigest = sha256Hex(pcm.data(), pcm.size());
  const bool pcmWritten = writeFileAll(pcmPath, pcm.data(), pcm.size());
  const bool wavWritten = writeWav(dir + "/audio.wav", pcm, 48000, 2, 16);
  std::string pcmManifest = "format=pcm-s16-v1\n";
  pcmManifest += "sample_rate=48000\nchannels=2\nbits=16\n";
  pcmManifest += "frames=" + std::to_string(sampleFrames) + "\n";
  pcmManifest += "bytes=" + std::to_string(static_cast<unsigned long long>(pcm.size())) + "\n";
  pcmManifest += "sha256=" + pcmDigest + "\n";

  const bool frameManifestWritten = writeTextFile(dir + "/frames.txt", frameManifest);
  const bool pcmManifestWritten = writeTextFile(dir + "/pcm.txt", pcmManifest);

  out("[dump-inputs] frames=%d frameBytes=%llu framesFile=%s framesSha256=%s\n", kFrameCount, frameBytes,
      framesPath.c_str(), framesSha.finish().c_str());
  out("[dump-inputs] pcmSampleFrames=%llu pcmBytes=%zu pcmFile=%s pcmSha256=%s wavOk=%s\n", sampleFrames,
      pcm.size(), pcmPath.c_str(), pcmDigest.c_str(), yesNo(wavWritten));
  out("[dump-inputs] manifests frame_manifest=%s pcm_manifest=%s\n", yesNo(frameManifestWritten), yesNo(pcmManifestWritten));
  out("[dump-inputs] ANSWER framesWritten=%s pcmWritten=%s (both are re-read by --encode-dump)\n",
      yesNo(framesWritten && framesClosed && frameManifestWritten), yesNo(pcmWritten && pcmManifestWritten));
  return (framesWritten && framesClosed && pcmWritten && wavWritten && frameManifestWritten && pcmManifestWritten) ? 0 : 6;
}

int encodeDump(const std::string& dir, const std::string& mp4Path) {
  out("=== mf_export_probe: encode from dump (task A) ===\n");
  out("mode=encode-dump dir=%s mp4=%s\n", dir.c_str(), mp4Path.c_str());

  FrameManifest manifest;
  PcmManifest pcmManifest;
  if (!loadFrameManifest(dir + "/frames.txt", &manifest)) {
    out("[encode-dump] cannot read or malformed %s/frames.txt\n", dir.c_str());
    return 7;
  }
  if (!loadPcmManifest(dir + "/pcm.txt", &pcmManifest)) {
    out("[encode-dump] cannot read or malformed %s/pcm.txt\n", dir.c_str());
    return 7;
  }

  FILE* file = std::fopen((dir + "/frames.rgb24").c_str(), "rb");
  if (file == nullptr) {
    out("[encode-dump] cannot open %s/frames.rgb24\n", dir.c_str());
    return 7;
  }
  std::vector<Rgb24Frame> frames(static_cast<size_t>(manifest.count));
  std::vector<uint8_t> buffer(static_cast<size_t>(manifest.frameBytes));
  int shaMatches = 0;
  bool readOk = true;
  for (int index = 0; index < manifest.count; ++index) {
    if (std::fread(buffer.data(), 1, buffer.size(), file) != buffer.size()) { readOk = false; break; }
    if (sha256Hex(buffer.data(), buffer.size()) == manifest.hashes[index]) ++shaMatches;
    frames[index].pixels.assign(buffer.begin(), buffer.end());
  }
  std::fclose(file);

  std::vector<uint8_t> pcm;
  if (!readFileAll(dir + "/pcm.raw", &pcm)) {
    out("[encode-dump] cannot read %s/pcm.raw\n", dir.c_str());
    return 7;
  }
  const std::string pcmDigest = sha256Hex(pcm.data(), pcm.size());
  const unsigned long long sampleFrames = pcm.size() / static_cast<size_t>(pcmManifest.channels * (pcmManifest.bits / 8));
  const bool pcmShaMatch = pcmDigest == pcmManifest.hash && pcm.size() == pcmManifest.bytes;

  out("[encode-dump] framesRead=%d frameShaMatches=%d frameBytes=%llu readOk=%s\n", static_cast<int>(frames.size()),
      shaMatches, static_cast<unsigned long long>(manifest.frameBytes), yesNo(readOk));
  out("[encode-dump] pcmBytes=%zu sampleFrames=%llu pcmShaMatch=%s manifestFrames=%llu\n", pcm.size(), sampleFrames,
      yesNo(pcmShaMatch), pcmManifest.frames);

  const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const HRESULT mfHr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  out("[encode-dump] CoInitializeEx hr=%s MFStartup hr=%s\n", hrStr(coHr).c_str(), hrStr(mfHr).c_str());
  if (FAILED(mfHr)) return 3;

  VideoShape shape;
  const GUID inputSubtype = MFVideoFormat_RGB32;
  const std::vector<Rgb24Frame>* table = &frames;
  FrameProvider provider = [table](int index, Rgb24Frame* frame) -> bool {
    if (index < 0 || index >= static_cast<int>(table->size())) return false;
    *frame = (*table)[static_cast<size_t>(index)];
    return true;
  };
  const EncodeStats stats = encodeToFile(mp4Path, shape, inputSubtype, manifest.count, &pcm, pcmManifest.sampleRate,
                                         pcmManifest.channels, pcmManifest.bits, provider, true);
  out("[encode-dump] writerCreate hr=%s videoAdd hr=%s videoInput hr=%s audioAdd hr=%s audioInput hr=%s finalize hr=%s\n",
      hrStr(stats.writerHr).c_str(), hrStr(stats.videoAddHr).c_str(), hrStr(stats.videoInputHr).c_str(),
      hrStr(stats.audioAddHr).c_str(), hrStr(stats.audioInputHr).c_str(), hrStr(stats.finalizeHr).c_str());
  out("[encode-dump] videoSamplesWritten=%d audioSamplesWritten=%d bytes=%llu elapsed=%lldms failure=%s\n",
      stats.videoSamplesWritten, stats.audioSamplesWritten, stats.bytes, stats.totalMs,
      stats.failure.empty() ? "<none>" : stats.failure.c_str());

  MFShutdown();
  CoUninitialize();

  const bool ok = readOk && shaMatches == manifest.count && pcmShaMatch && stats.failure.empty() &&
                  stats.videoSamplesWritten == manifest.count;
  out("[encode-dump] ASSERT framesReadMatch=%s (%d==%d) frameShaAllMatch=%s (%d/%d) pcmShaMatch=%s videoSamplesWrittenMatch=%s\n",
      yesNo(readOk && static_cast<int>(frames.size()) == manifest.count), static_cast<int>(frames.size()), manifest.count,
      yesNo(shaMatches == manifest.count), shaMatches, manifest.count, yesNo(pcmShaMatch),
      yesNo(stats.videoSamplesWritten == manifest.count));
  out("[encode-dump] ANSWER encodedFromArtifact=%s file=%s\n", yesNo(ok), mp4Path.c_str());
  return ok ? 0 : 7;
}

void nv12ToRgb24(const uint8_t* data, unsigned long length, int stride, UINT32 width, UINT32 height,
                 std::vector<uint8_t>* rgb) {
  rgb->assign(static_cast<size_t>(width) * height * 3, 0);
  const size_t rowStride = stride > 0 ? static_cast<size_t>(stride) : static_cast<size_t>(width);
  const size_t lumaSize = rowStride * height;
  if (length < lumaSize + rowStride * (height / 2)) return;
  const uint8_t* luma = data;
  const uint8_t* chroma = data + lumaSize;
  for (UINT32 y = 0; y < height; ++y) {
    const uint8_t* lumaRow = luma + static_cast<size_t>(y) * rowStride;
    const uint8_t* chromaRow = chroma + static_cast<size_t>(y / 2) * rowStride;
    uint8_t* target = rgb->data() + static_cast<size_t>(y) * width * 3;
    for (UINT32 x = 0; x < width; ++x) {
      const int lumaValue = lumaRow[x];
      const int u = static_cast<int>(chromaRow[(x / 2) * 2 + 0]) - 128;
      const int v = static_cast<int>(chromaRow[(x / 2) * 2 + 1]) - 128;
      const double yv = (lumaValue - 16) * 255.0 / 219.0;
      const double cb = u * 255.0 / 224.0;
      const double cr = v * 255.0 / 224.0;
      const double r = yv + 1.5748 * cr;
      const double b = yv + 1.8556 * cb;
      const double g = (yv - 0.2126 * r - 0.0722 * b) / 0.7152;
      target[x * 3 + 0] = clampByte(static_cast<int>(std::lround(r)));
      target[x * 3 + 1] = clampByte(static_cast<int>(std::lround(g)));
      target[x * 3 + 2] = clampByte(static_cast<int>(std::lround(b)));
    }
  }
}

int decodeDump(const std::string& mp4Path, const std::string& outDir, const std::string& expectDir) {
  out("=== mf_export_probe: decode read-back (task A) ===\n");
  out("mode=decode mp4=%s outDir=%s expectDump=%s\n", mp4Path.c_str(), outDir.c_str(),
      expectDir.empty() ? "<none>" : expectDir.c_str());

  FrameManifest source;
  PcmManifest sourcePcm;
  const bool haveSource = !expectDir.empty();
  if (haveSource) {
    if (!loadFrameManifest(expectDir + "/frames.txt", &source)) {
      out("[decode] cannot read %s/frames.txt\n", expectDir.c_str());
      return 8;
    }
    if (!loadPcmManifest(expectDir + "/pcm.txt", &sourcePcm)) {
      out("[decode] cannot read %s/pcm.txt\n", expectDir.c_str());
      return 8;
    }
    out("[decode] source frames=%d frameBytes=%llu sampleFrames=%llu\n", source.count,
        static_cast<unsigned long long>(source.frameBytes), sourcePcm.frames);
  }

  const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const HRESULT mfHr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  out("[decode] CoInitializeEx hr=%s MFStartup hr=%s\n", hrStr(coHr).c_str(), hrStr(mfHr).c_str());
  if (FAILED(mfHr)) return 3;

  IMFSourceReader* reader = nullptr;
  const HRESULT createHr = MFCreateSourceReaderFromURL(utf8ToWide(mp4Path).c_str(), nullptr, &reader);
  out("[decode] MFCreateSourceReaderFromURL hr=%s\n", hrStr(createHr).c_str());
  if (FAILED(createHr) || reader == nullptr) { MFShutdown(); CoUninitialize(); return 8; }

  int videoIndex = -1;
  int audioIndex = -1;
  for (DWORD index = 0; index < 16; ++index) {
    IMFMediaType* native = nullptr;
    const HRESULT nativeHr = reader->GetNativeMediaType(index, 0, &native);
    if (FAILED(nativeHr)) break;
    GUID major{};
    native->GetGUID(MF_MT_MAJOR_TYPE, &major);
    if (major == MFMediaType_Video && videoIndex < 0) videoIndex = static_cast<int>(index);
    if (major == MFMediaType_Audio && audioIndex < 0) audioIndex = static_cast<int>(index);
    rel(native);
  }
  out("[decode] streams: videoStream=%d audioStream=%d\n", videoIndex, audioIndex);

  // ------------------------------------------------------------------ video
  int decodedFrames = 0;
  int artifactMismatches = 0;
  bool artifactReadback = false;
  double sumMeanAbsDiff = 0.0;
  double maxMeanAbsDiff = 0.0;
  LONGLONG videoFirstTs = 0;
  LONGLONG videoLastTs = 0;
  HRESULT videoDecodeHr = S_OK;
  std::vector<std::string> frameHashes;
  const std::string decodedFramesPath = outDir + "/decoded_frames.rgb24";
  FILE* decodedFile = std::fopen(decodedFramesPath.c_str(), "wb");
  std::vector<uint8_t> rgb;
  std::vector<uint8_t> sourceFrame;
  FILE* sourceFile = nullptr;
  if (decodedFile == nullptr) out("[decode] cannot open %s for writing\n", decodedFramesPath.c_str());
  if (haveSource) {
    sourceFile = std::fopen((expectDir + "/frames.rgb24").c_str(), "rb");
    if (sourceFile == nullptr) out("[decode] cannot open %s/frames.rgb24\n", expectDir.c_str());
  }

  if (videoIndex >= 0 && decodedFile != nullptr) {
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(static_cast<DWORD>(videoIndex), TRUE);
    IMFMediaType* requested = nullptr;
    HRESULT typeHr = E_FAIL;
    if (SUCCEEDED(MFCreateMediaType(&requested)) && requested != nullptr) {
      requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
      requested->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
      typeHr = reader->SetCurrentMediaType(static_cast<DWORD>(videoIndex), nullptr, requested);
      rel(requested);
    }
    out("[decode] SetCurrentMediaType(video, NV12) hr=%s\n", hrStr(typeHr).c_str());
    int stride = static_cast<int>(kWidth);
    IMFMediaType* current = nullptr;
    if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(videoIndex), &current)) && current != nullptr) {
      UINT32 strideValue = 0;
      if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideValue)) && strideValue != 0)
        stride = static_cast<int>(strideValue);
      rel(current);
    }
    out("[decode] video stride=%d\n", stride);

    while (true) {
      DWORD actualStream = 0;
      DWORD flags = 0;
      LONGLONG timestamp = 0;
      IMFSample* sample = nullptr;
      const HRESULT readHr = reader->ReadSample(static_cast<DWORD>(videoIndex), 0, &actualStream, &flags, &timestamp, &sample);
      if (FAILED(readHr)) {
        videoDecodeHr = readHr;
        out("[decode] video ReadSample hr=%s after %d frames\n", hrStr(readHr).c_str(), decodedFrames);
        rel(sample);
        break;
      }
      const bool end = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
      if (!end && sample != nullptr && (flags & MF_SOURCE_READERF_STREAMTICK) == 0) {
        IMFMediaBuffer* buffer = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer != nullptr) {
          BYTE* bytes = nullptr;
          DWORD length = 0;
          if (SUCCEEDED(buffer->Lock(&bytes, nullptr, &length))) {
            nv12ToRgb24(bytes, length, stride, kWidth, kHeight, &rgb);
            buffer->Unlock();
            frameHashes.push_back(sha256Hex(rgb.data(), rgb.size()));
            if (std::fwrite(rgb.data(), 1, rgb.size(), decodedFile) != rgb.size())
              out("[decode] short write at frame %d\n", decodedFrames);
            if (sourceFile != nullptr) {
              if (sourceFrame.empty()) sourceFrame.assign(static_cast<size_t>(kWidth) * kHeight * 3, 0);
              if (std::fread(sourceFrame.data(), 1, sourceFrame.size(), sourceFile) == sourceFrame.size()) {
                unsigned long long diff = 0;
                for (size_t i = 0; i < sourceFrame.size(); ++i)
                  diff += static_cast<unsigned long long>(std::abs(static_cast<int>(rgb[i]) - static_cast<int>(sourceFrame[i])));
                const double mean = static_cast<double>(diff) / static_cast<double>(sourceFrame.size());
                sumMeanAbsDiff += mean;
                if (mean > maxMeanAbsDiff) maxMeanAbsDiff = mean;
              }
            }
            if (decodedFrames == 0) videoFirstTs = timestamp;
            videoLastTs = timestamp;
            ++decodedFrames;
          }
          rel(buffer);
        }
      }
      rel(sample);
      if (end) break;
    }
    std::fclose(decodedFile);
    if (sourceFile != nullptr) std::fclose(sourceFile);

    // re-read the artifact this mode just wrote
    FILE* reread = std::fopen(decodedFramesPath.c_str(), "rb");
    if (reread != nullptr) {
      std::vector<uint8_t> check(static_cast<size_t>(kWidth) * kHeight * 3);
      int index = 0;
      while (index < static_cast<int>(frameHashes.size()) &&
             std::fread(check.data(), 1, check.size(), reread) == check.size()) {
        if (sha256Hex(check.data(), check.size()) != frameHashes[static_cast<size_t>(index)]) ++artifactMismatches;
        ++index;
      }
      std::fclose(reread);
      artifactReadback = index == static_cast<int>(frameHashes.size()) && artifactMismatches == 0;
    }
    std::string frameManifest = "format=decoded-frames-v1\n";
    frameManifest += "width=" + std::to_string(kWidth) + "\n";
    frameManifest += "height=" + std::to_string(kHeight) + "\n";
    frameManifest += "count=" + std::to_string(decodedFrames) + "\n";
    frameManifest += "frame_bytes=" + std::to_string(static_cast<unsigned long long>(kWidth) * kHeight * 3) + "\n";
    for (size_t index = 0; index < frameHashes.size(); ++index)
      frameManifest += "frame " + std::to_string(index) + " sha256 " + frameHashes[index] + "\n";
    writeTextFile(outDir + "/decoded_video.txt", frameManifest);
  } else {
    if (decodedFile != nullptr) std::fclose(decodedFile);
    if (sourceFile != nullptr) std::fclose(sourceFile);
  }

  // ------------------------------------------------------------------ audio
  std::vector<uint8_t> decodedAudio;
  LONGLONG audioFirstTs = 0;
  LONGLONG audioLastTs = 0;
  int audioReadCount = 0;
  HRESULT audioDecodeHr = S_OK;
  HRESULT audioTypeHr = E_FAIL;
  // The video pass above drained the first reader to EOS, so audio needs its own reader at position 0.
  IMFSourceReader* audioReader = nullptr;
  HRESULT audioReaderHr = E_FAIL;
  if (audioIndex >= 0) {
    audioReaderHr = MFCreateSourceReaderFromURL(utf8ToWide(mp4Path).c_str(), nullptr, &audioReader);
    if (SUCCEEDED(audioReaderHr) && audioReader != nullptr) {
      audioReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
      audioReader->SetStreamSelection(static_cast<DWORD>(audioIndex), TRUE);
    }
    out("[decode] audio reader hr=%s stream=%d\n", hrStr(audioReaderHr).c_str(), audioIndex);
    IMFMediaType* pcmType = nullptr;
    if (SUCCEEDED(MFCreateMediaType(&pcmType)) && pcmType != nullptr) {
      pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
      pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
      audioTypeHr = audioReader != nullptr
                        ? audioReader->SetCurrentMediaType(static_cast<DWORD>(audioIndex), nullptr, pcmType)
                        : E_POINTER;
      rel(pcmType);
    }
    if (FAILED(audioTypeHr) && audioReader != nullptr) {
      IMFMediaType* detailed = nullptr;
      if (SUCCEEDED(MFCreateMediaType(&detailed)) && detailed != nullptr) {
        detailed->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        detailed->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        detailed->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        detailed->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        detailed->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        detailed->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        detailed->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000);
        audioTypeHr = audioReader != nullptr
                          ? audioReader->SetCurrentMediaType(static_cast<DWORD>(audioIndex), nullptr, detailed)
                          : E_POINTER;
        rel(detailed);
      }
    }
    out("[decode] SetCurrentMediaType(audio, PCM 48k stereo 16-bit) hr=%s\n", hrStr(audioTypeHr).c_str());
    if (SUCCEEDED(audioTypeHr)) {
      while (true) {
        DWORD actualStream = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* sample = nullptr;
        const HRESULT readHr = audioReader != nullptr
                                   ? audioReader->ReadSample(static_cast<DWORD>(audioIndex), 0, &actualStream, &flags,
                                                             &timestamp, &sample)
                                   : E_POINTER;
        if (FAILED(readHr)) {
          audioDecodeHr = readHr;
          out("[decode] audio ReadSample hr=%s after %d samples\n", hrStr(readHr).c_str(), audioReadCount);
          rel(sample);
          break;
        }
        const bool end = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
        if (!end && sample != nullptr && (flags & MF_SOURCE_READERF_STREAMTICK) == 0) {
          IMFMediaBuffer* buffer = nullptr;
          if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer != nullptr) {
            BYTE* bytes = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(buffer->Lock(&bytes, nullptr, &length))) {
              decodedAudio.insert(decodedAudio.end(), bytes, bytes + length);
              buffer->Unlock();
              if (audioReadCount == 0) audioFirstTs = timestamp;
              audioLastTs = timestamp;
              ++audioReadCount;
            }
            rel(buffer);
          }
        }
        rel(sample);
        if (end) break;
      }
    }
  }
  rel(audioReader);
  rel(reader);

  const unsigned long long decodedSampleFrames = decodedAudio.size() / 4;
  const std::string audioDigest = sha256Hex(decodedAudio.data(), decodedAudio.size());
  const bool audioWritten = writeFileAll(outDir + "/decoded_audio.raw", decodedAudio.data(), decodedAudio.size());
  std::string audioManifest = "format=decoded-pcm-s16-v1\n";
  audioManifest += "sample_rate=48000\nchannels=2\nbits=16\n";
  audioManifest += "frames=" + std::to_string(decodedSampleFrames) + "\n";
  audioManifest += "bytes=" + std::to_string(static_cast<unsigned long long>(decodedAudio.size())) + "\n";
  audioManifest += "sha256=" + audioDigest + "\n";
  const bool audioManifestWritten = writeTextFile(outDir + "/decoded_audio.txt", audioManifest);
  std::vector<uint8_t> audioReread;
  const bool audioRereadable = readFileAll(outDir + "/decoded_audio.raw", &audioReread) &&
                               sha256Hex(audioReread.data(), audioReread.size()) == audioDigest;

  const bool videoCountMatch = haveSource ? decodedFrames == source.count : decodedFrames > 0;
  const long long audioDelta = static_cast<long long>(decodedSampleFrames) -
                               static_cast<long long>(haveSource ? sourcePcm.frames : decodedSampleFrames);
  const long long audioAbsDelta = audioDelta < 0 ? -audioDelta : audioDelta;
  const bool audioCountMatch =
      haveSource ? (decodedSampleFrames > 0 && audioAbsDelta <= 2048) : decodedSampleFrames > 0;

  // AAC is lossy, so decoded PCM can never be byte-identical to the source; compare sample magnitudes.
  double pcmMeanAbsDiff = 0.0;
  int pcmMaxAbsDiff = 0;
  unsigned long long pcmComparedFrames = 0;
  if (haveSource) {
    std::vector<uint8_t> sourcePcmBytes;
    if (readFileAll(expectDir + "/pcm.raw", &sourcePcmBytes) && sourcePcmBytes.size() >= 4 &&
        decodedAudio.size() >= 4) {
      const size_t samples =
          (sourcePcmBytes.size() < decodedAudio.size() ? sourcePcmBytes.size() : decodedAudio.size()) / 2;
      const int16_t* sourceSamples = reinterpret_cast<const int16_t*>(sourcePcmBytes.data());
      const int16_t* decodedSamples = reinterpret_cast<const int16_t*>(decodedAudio.data());
      unsigned long long diffSum = 0;
      for (size_t index = 0; index < samples; ++index) {
        const int diff = std::abs(static_cast<int>(sourceSamples[index]) - static_cast<int>(decodedSamples[index]));
        diffSum += static_cast<unsigned long long>(diff);
        if (diff > pcmMaxAbsDiff) pcmMaxAbsDiff = diff;
      }
      pcmComparedFrames = static_cast<unsigned long long>(samples / 2);
      pcmMeanAbsDiff = samples == 0 ? 0.0 : static_cast<double>(diffSum) / static_cast<double>(samples);
    }
  }
  const bool pcmSimilar = haveSource ? (pcmComparedFrames > 0 && pcmMeanAbsDiff <= 600.0) : decodedSampleFrames > 0;

  out("[decode] video frames=%d firstTs=%lld lastTs=%lld decodeHr=%s\n", decodedFrames,
      static_cast<long long>(videoFirstTs), static_cast<long long>(videoLastTs), hrStr(videoDecodeHr).c_str());
  out("[decode] audio samples=%d bytes=%zu sampleFrames=%llu firstTs=%lld lastTs=%lld decodeHr=%s\n",
      audioReadCount, decodedAudio.size(), decodedSampleFrames, static_cast<long long>(audioFirstTs),
      static_cast<long long>(audioLastTs), hrStr(audioDecodeHr).c_str());
  out("[decode] pcmCompare frames=%llu meanAbsDiff=%.3f maxAbsDiff=%d decodedSha=%s sourceSha=%s lossy=true\n",
      pcmComparedFrames, pcmMeanAbsDiff, pcmMaxAbsDiff, audioDigest.c_str(),
      haveSource ? sourcePcm.hash.c_str() : "<none>");
  out("[decode] artifacts decoded_frames=%s decoded_audio=%s decoded_audio_manifest=%s video_manifest=%s\n",
      decodedFramesPath.c_str(), yesNo(audioWritten), yesNo(audioManifestWritten), yesNo(artifactReadback));
  out("[decode] ASSERT videoFramesMatch=%s (decoded=%d source=%d)\n", yesNo(videoCountMatch), decodedFrames,
      haveSource ? source.count : -1);
  out("[decode] ASSERT audioSamplesMatch=%s (decoded=%llu source=%llu delta=%lld tolerance=2048)\n",
      yesNo(audioCountMatch), decodedSampleFrames, haveSource ? sourcePcm.frames : 0, audioDelta);
  out("[decode] ASSERT pcmSimilar=%s (meanAbsDiff=%.3f threshold=600)\n", yesNo(pcmSimilar), pcmMeanAbsDiff);
  out("[decode] videoMeanAbsDiff=%.3f maxFrameMeanAbsDiff=%.3f artifactReadback=%s audioRereadable=%s\n",
      decodedFrames > 0 ? sumMeanAbsDiff / decodedFrames : 0.0, maxMeanAbsDiff, yesNo(artifactReadback),
      yesNo(audioRereadable));
  const bool ok = videoIndex >= 0 && audioIndex >= 0 && decodedFrames > 0 && audioWritten && artifactReadback &&
                  audioRereadable && videoCountMatch && audioCountMatch && pcmSimilar;
  out("[decode] ANSWER videoFramesMatch=%s audioSamplesMatch=%s pcmSimilar=%s artifactReadback=%s\n",
      yesNo(videoCountMatch), yesNo(audioCountMatch), yesNo(pcmSimilar), yesNo(artifactReadback));
  MFShutdown();
  CoUninitialize();
  return ok ? 0 : 7;
}

// ------------------------------------------------- container timestamps (task A)

struct Mp4Box {
  size_t offset = 0;
  unsigned long long size = 0;
  std::string type;
};

bool parseMp4Boxes(const std::vector<uint8_t>& data, size_t begin, size_t end, std::vector<Mp4Box>* boxes,
                   std::string* error) {
  size_t position = begin;
  while (position + 8 <= end) {
    Mp4Box box;
    box.offset = position;
    const uint32_t size32 = be32(&data[position]);
    box.type.assign(reinterpret_cast<const char*>(&data[position + 4]), 4);
    unsigned long long header = 8;
    if (size32 == 1) {
      if (position + 16 > end) { *error = "truncated 64-bit box header"; return false; }
      box.size = be64(&data[position + 8]);
      header = 16;
    } else if (size32 == 0) {
      box.size = end - position;
    } else {
      box.size = size32;
    }
    if (box.size < header || position + box.size > end) {
      *error = "box size out of range for type " + box.type;
      return false;
    }
    boxes->push_back(box);
    position += static_cast<size_t>(box.size);
  }
  return true;
}

struct TimestampField {
  std::string path;
  std::string type;
  int version = 0;
  int width = 0;
  size_t creationOffset = 0;
  size_t modificationOffset = 0;
  unsigned long long oldCreation = 0;
  unsigned long long oldModification = 0;
};

int normalizeMp4(const std::string& inPath, const std::string& outPath) {
  out("=== mf_export_probe: container timestamp normalization (task A) ===\n");
  std::vector<uint8_t> data;
  if (!readFileAll(inPath, &data)) {
    out("[normalize] cannot read %s\n", inPath.c_str());
    return 9;
  }
  const std::string inputDigest = sha256Hex(data.data(), data.size());
  const size_t originalSize = data.size();
  std::string error;
  std::vector<Mp4Box> top;
  if (!parseMp4Boxes(data, 0, data.size(), &top, &error)) {
    out("[normalize] top-level parse failed: %s\n", error.c_str());
    return 9;
  }
  size_t moovOffset = 0;
  bool haveMoov = false;
  for (const Mp4Box& box : top) {
    out("[normalize] topLevel type=%s offset=%zu size=%llu\n", box.type.c_str(), box.offset, box.size);
    if (box.type == "moov") { moovOffset = box.offset; haveMoov = true; }
  }
  if (!haveMoov) {
    out("[normalize] no moov box found -- nothing to normalize\n");
    return 9;
  }

  std::vector<TimestampField> fields;
  auto zeroTimes = [&data, &fields](const Mp4Box& box, const std::string& path) -> bool {
    if (box.size < 12) return false;
    const size_t base = box.offset + 8;
    TimestampField field;
    field.path = path;
    field.type = box.type;
    field.version = static_cast<int>(data[base]);
    size_t creationOffset = 0;
    size_t modificationOffset = 0;
    if (field.version == 0) {
      creationOffset = base + 4;
      modificationOffset = base + 8;
      field.width = 4;
    } else if (field.version == 1) {
      creationOffset = base + 4;
      modificationOffset = base + 12;
      field.width = 8;
    } else {
      out("[normalize] UNKNOWN version=%d for %s (not zeroed)\n", field.version, path.c_str());
      return false;
    }
    if (box.offset + box.size < modificationOffset + static_cast<size_t>(field.width)) {
      out("[normalize] box %s declares size=%llu but the timestamp fields fall outside it\n", path.c_str(), box.size);
      return false;
    }
    field.creationOffset = creationOffset;
    field.modificationOffset = modificationOffset;
    field.oldCreation = field.width == 4 ? static_cast<unsigned long long>(be32(&data[creationOffset]))
                                         : be64(&data[creationOffset]);
    field.oldModification = field.width == 4 ? static_cast<unsigned long long>(be32(&data[modificationOffset]))
                                             : be64(&data[modificationOffset]);
    for (int index = 0; index < field.width; ++index) {
      data[creationOffset + index] = 0;
      data[modificationOffset + index] = 0;
    }
    out("[normalize] field path=%s type=%s version=%d width=%d creation_offset=%zu old_creation=%llu modification_offset=%zu old_modification=%llu\n",
        path.c_str(), field.type.c_str(), field.version, field.width, field.creationOffset, field.oldCreation,
        field.modificationOffset, field.oldModification);
    fields.push_back(field);
    return true;
  };

  std::vector<Mp4Box> moovChildren;
  if (!parseMp4Boxes(data, moovOffset + 8, moovOffset + static_cast<size_t>(0) + [&]() {
        for (const Mp4Box& box : top) if (box.offset == moovOffset) return static_cast<size_t>(box.size);
        return static_cast<size_t>(0);
      }(), &moovChildren, &error)) {
    out("[normalize] moov parse failed: %s\n", error.c_str());
    return 9;
  }
  int trakIndex = 0;
  for (const Mp4Box& child : moovChildren) {
    if (child.type == "mvhd") {
      zeroTimes(child, "moov/mvhd");
    } else if (child.type == "trak") {
      std::vector<Mp4Box> trakChildren;
      if (!parseMp4Boxes(data, child.offset + 8, child.offset + static_cast<size_t>(child.size), &trakChildren, &error)) {
        out("[normalize] trak[%d] parse failed: %s\n", trakIndex, error.c_str());
        return 9;
      }
      for (const Mp4Box& trakChild : trakChildren) {
        if (trakChild.type == "tkhd")
          zeroTimes(trakChild, "moov/trak[" + std::to_string(trakIndex) + "]/tkhd");
        else if (trakChild.type == "mdia") {
          std::vector<Mp4Box> mdiaChildren;
          if (!parseMp4Boxes(data, trakChild.offset + 8, trakChild.offset + static_cast<size_t>(trakChild.size),
                             &mdiaChildren, &error)) {
            out("[normalize] trak[%d]/mdia parse failed: %s\n", trakIndex, error.c_str());
            return 9;
          }
          for (const Mp4Box& mdiaChild : mdiaChildren)
            if (mdiaChild.type == "mdhd")
              zeroTimes(mdiaChild, "moov/trak[" + std::to_string(trakIndex) + "]/mdia/mdhd");
        }
      }
      ++trakIndex;
    }
  }

  if (fields.empty()) {
    out("[normalize] no timestamp fields located -- refusing to write an identical file as if it were normalized\n");
    return 9;
  }
  const bool written = writeFileAll(outPath, data.data(), data.size());
  out("[normalize] input=%s sha256=%s size=%zu\n", inPath.c_str(), inputDigest.c_str(), originalSize);
  out("[normalize] output=%s written=%s size=%zu\n", outPath.c_str(), yesNo(written), data.size());

  std::vector<uint8_t> verify;
  int nonzero = 0;
  const bool reread = readFileAll(outPath, &verify);
  if (reread) {
    for (const TimestampField& field : fields) {
      bool dirty = false;
      for (int index = 0; index < field.width; ++index)
        if (verify[field.creationOffset + index] != 0 || verify[field.modificationOffset + index] != 0) dirty = true;
      if (dirty) ++nonzero;
    }
  }
  const std::string outputDigest = reread ? sha256Hex(verify.data(), verify.size()) : std::string("<reread-failed>");
  unsigned long long bytesZeroed = 0;
  for (const TimestampField& field : fields) bytesZeroed += static_cast<unsigned long long>(field.width) * 2;
  out("[normalize] output sha256=%s size_unchanged=%s verify_nonzero_fields=%d\n", outputDigest.c_str(),
      yesNo(reread && verify.size() == originalSize), nonzero);
  out("[normalize] ANSWER fieldsZeroed=%zu bytesZeroed=%llu traks=%d artifactReadback=%s\n", fields.size(),
      bytesZeroed, trakIndex, yesNo(reread && nonzero == 0 && verify.size() == originalSize));
  return written && reread && nonzero == 0 && verify.size() == originalSize ? 0 : 9;
}

// ----------------------------------------------------------------------- modes

int runProbe(const std::string& outDir) {
  const std::string samplePath = outDir + "/sample_720p_2s.mp4";
  const std::string detAPath = outDir + "/det_a_2s.mp4";
  const std::string detBPath = outDir + "/det_b_2s.mp4";
  const std::string throughputPath = outDir + "/throughput_100f.mp4";

  out("=== mf_export_probe: Media Foundation export capability probe ===\n");
  out("os=\"%s\"\n", osDescription().c_str());
  out("mode=probe outDir=%s\n", outDir.c_str());

  const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  out("CoInitializeEx(MULTITHREADED) hr=%s\n", hrStr(coHr).c_str());
  const HRESULT mfHr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  out("MFStartup(MF_VERSION, MFSTARTUP_FULL) hr=%s\n", hrStr(mfHr).c_str());
  if (FAILED(mfHr)) return 3;

  // ---------------------------------------------------------------- Q1
  out("\n--- Q1: MFT_CATEGORY_VIDEO_ENCODER census ---\n");
  UINT32 videoActivateFailures = 0;
  std::vector<EncoderEntry> videoAll = enumerateEncoders(
      MFT_CATEGORY_VIDEO_ENCODER, MFMediaType_Video, nullptr, "Q1-all", true, MFVideoFormat_H264, &videoActivateFailures);
  int h264ByName = 0;
  for (const EncoderEntry& entry : videoAll) {
    if (entry.hasTargetOutput) ++h264ByName;
  }
  out("[Q1-all] total=%zu encoders with MFVideoFormat_H264 output available=%d activateFailures=%u\n",
      videoAll.size(), h264ByName, videoActivateFailures);
  std::vector<EncoderEntry> videoH264 = enumerateEncoders(
      MFT_CATEGORY_VIDEO_ENCODER, MFMediaType_Video, &MFVideoFormat_H264, "Q1-filter", false, MFVideoFormat_H264, nullptr);
  out("[Q1-filter] MFTEnumEx with pOutputType={video,H264} returned %zu MFT(s)\n", videoH264.size());
  out("[Q1] ANSWER: h264EncoderPresent=%s (filtered-count=%zu)\n", videoH264.empty() ? "no" : "yes", videoH264.size());

  // ---------------------------------------------------------------- Q2
  out("\n--- Q2: MFT_CATEGORY_AUDIO_ENCODER census ---\n");
  UINT32 audioActivateFailures = 0;
  std::vector<EncoderEntry> audioAll = enumerateEncoders(
      MFT_CATEGORY_AUDIO_ENCODER, MFMediaType_Audio, nullptr, "Q2-all", true, MFAudioFormat_AAC, &audioActivateFailures);
  int aacByName = 0;
  for (const EncoderEntry& entry : audioAll) {
    if (entry.hasTargetOutput) ++aacByName;
  }
  out("[Q2-all] total=%zu encoders with MFAudioFormat_AAC output available=%d activateFailures=%u\n",
      audioAll.size(), aacByName, audioActivateFailures);
  std::vector<EncoderEntry> audioAac = enumerateEncoders(
      MFT_CATEGORY_AUDIO_ENCODER, MFMediaType_Audio, &MFAudioFormat_AAC, "Q2-filter", false, MFAudioFormat_AAC, nullptr);
  out("[Q2-filter] MFTEnumEx with pOutputType={audio,AAC} returned %zu MFT(s)\n", audioAac.size());
  out("[Q2] ANSWER: aacEncoderPresent=%s (filtered-count=%zu)\n", audioAac.empty() ? "no" : "yes", audioAac.size());

  // ---------------------------------------------------------------- Q3
  out("\n--- Q3: IMFSinkWriter MP4 (MPEG4 container) with H.264 + AAC streams ---\n");
  const std::wstring q3Path = utf8ToWide(outDir + "/q3_container_probe.mp4");
  IMFSinkWriter* q3Writer = nullptr;
  const HRESULT q3Create = createSinkWriterMp4(q3Path, &q3Writer);
  out("[Q3] MFCreateSinkWriterFromURL(MF_TRANSCODE_CONTAINERTYPE=MFTranscodeContainerType_MPEG4) hr=%s\n",
      hrStr(q3Create).c_str());
  VideoShape shape;
  if (SUCCEEDED(q3Create)) {
    DWORD streamIndex = 0;
    IMFMediaType* videoType = nullptr;
    HRESULT hr = makeH264OutputType(shape, &videoType, true);
    out("[Q3] makeH264OutputType(1280x720@30 4Mbps + MF_MT_MPEG2_PROFILE=Main) hr=%s\n", hrStr(hr).c_str());
    if (SUCCEEDED(hr)) hr = q3Writer->AddStream(videoType, &streamIndex);
    out("[Q3] AddStream(H.264 video) hr=%s\n", hrStr(hr).c_str());
    rel(videoType);

    DWORD audioIndex = 0;
    IMFMediaType* audioType = nullptr;
    HRESULT audioHr = makeAacOutputType(48000, 2, 16, &audioType);
    out("[Q3] makeAacOutputType(48000Hz stereo 16bit AAC-LC) hr=%s\n", hrStr(audioHr).c_str());
    if (SUCCEEDED(audioHr)) audioHr = q3Writer->AddStream(audioType, &audioIndex);
    out("[Q3] AddStream(AAC audio) hr=%s\n", hrStr(audioHr).c_str());
    rel(audioType);
    out("[Q3] ANSWER: bothStreamsAdded=%s\n", (SUCCEEDED(hr) && SUCCEEDED(audioHr)) ? "yes" : "no");
    rel(q3Writer);
  } else {
    out("[Q3] ANSWER: bothStreamsAdded=no (writer creation failed)\n");
  }
  DeleteFileW(q3Path.c_str());

  // ---------------------------------------------------------------- Q4
  out("\n--- Q4: which CPU input pixel formats does the H.264 sink accept ---\n");
  const GUID candidates[] = {MFVideoFormat_RGB32, MFVideoFormat_NV12, MFVideoFormat_YUY2};
  GUID acceptedSubtype{};
  bool haveAccepted = false;
  for (const GUID& candidate : candidates) {
    const std::wstring probePath = utf8ToWide(outDir + "/q4_probe.mp4");
    IMFSinkWriter* writer = nullptr;
    const HRESULT createResult = createSinkWriterMp4(probePath, &writer);
    HRESULT addResult = E_FAIL;
    HRESULT inputResult = E_FAIL;
    if (SUCCEEDED(createResult)) {
      DWORD streamIndex = 0;
      IMFMediaType* outputType = nullptr;
      addResult = makeH264OutputType(shape, &outputType, false);
      if (SUCCEEDED(addResult)) addResult = writer->AddStream(outputType, &streamIndex);
      rel(outputType);
      if (SUCCEEDED(addResult)) {
        IMFMediaType* inputType = nullptr;
        inputResult = makeVideoInputType(shape, candidate, &inputType);
        if (SUCCEEDED(inputResult)) inputResult = writer->SetInputMediaType(streamIndex, inputType, nullptr);
        rel(inputType);
      }
      rel(writer);
    } else {
      addResult = createResult;
    }
    out("[Q4] input=%s: createWriter hr=%s AddStream(h264) hr=%s SetInputMediaType hr=%s => %s\n",
        subtypeName(candidate).c_str(), hrStr(createResult).c_str(), hrStr(addResult).c_str(),
        hrStr(inputResult).c_str(),
        (SUCCEEDED(addResult) && SUCCEEDED(inputResult)) ? "ACCEPTED" : "rejected");
    if (SUCCEEDED(addResult) && SUCCEEDED(inputResult) && !haveAccepted) {
      acceptedSubtype = candidate;
      haveAccepted = true;
    }
    DeleteFileW(probePath.c_str());
  }
  out("[Q4] ANSWER: rgb32Accepted=%s chosenInput=%s needsOwnNv12Converter=%s\n",
      (haveAccepted && acceptedSubtype == MFVideoFormat_RGB32) ? "yes" : "no",
      haveAccepted ? subtypeName(acceptedSubtype).c_str() : "none",
      (acceptedSubtype == MFVideoFormat_RGB32) ? "no" : "yes");

  if (!haveAccepted) {
    out("\n[Q5] SKIPPED: no accepted CPU input format, cannot write a sample.\n");
    MFShutdown();
    CoUninitialize();
    return 4;
  }

  // ---------------------------------------------------------------- Q5
  out("\n--- Q5: real sample 1280x720 30fps 60 frames + 2s 48kHz stereo PCM ---\n");
  const UINT32 audioSampleRate = 48000;
  const UINT32 audioChannels = 2;
  const UINT32 audioBits = 16;
  const std::vector<uint8_t> pcm = makeSinePcm(audioSampleRate, audioChannels, audioBits, 2.0);
  out("[Q5] pcm bytes=%zu (%zu frames @ %u Hz stereo 16-bit)\n", pcm.size(), pcm.size() / 4, audioSampleRate);
  FrameProvider synth = [](int index, Rgb24Frame* frame) {
    synthFrame(index, frame);
    return true;
  };
  const EncodeStats sample = encodeToFile(samplePath, shape, acceptedSubtype, 60, &pcm,
                                          audioSampleRate, audioChannels, audioBits, synth, true);
  out("[Q5] writerCreate hr=%s\n", hrStr(sample.writerHr).c_str());
  out("[Q5] AddStream(video) hr=%s\n", hrStr(sample.videoAddHr).c_str());
  out("[Q5] SetInputMediaType(video=%s) hr=%s\n", subtypeName(acceptedSubtype).c_str(),
      hrStr(sample.videoInputHr).c_str());
  out("[Q5] AddStream(audio AAC) hr=%s\n", hrStr(sample.audioAddHr).c_str());
  out("[Q5] SetInputMediaType(audio PCM) hr=%s\n", hrStr(sample.audioInputHr).c_str());
  out("[Q5] BeginWriting hr=%s\n", hrStr(sample.beginHr).c_str());
  out("[Q5] first WriteSample hr=%s\n", hrStr(sample.firstWriteHr).c_str());
  out("[Q5] Finalize hr=%s\n", hrStr(sample.finalizeHr).c_str());
  out("[Q5] videoSamplesWritten=%d audioSamplesWritten=%d\n", sample.videoSamplesWritten,
      sample.audioSamplesWritten);
  out("[Q5] file=%s bytes=%llu elapsed=%lldms writeOnly=%.1fms convert=%.1fms\n", samplePath.c_str(),
      sample.bytes, sample.totalMs, sample.writeMs, sample.convertMs);
  out("[Q5] failure=%s\n", sample.failure.empty() ? "<none>" : sample.failure.c_str());
  out("[Q5] first 16 bytes: %s\n", firstBytesHex(samplePath, 16).c_str());

  // ---------------------------------------------------------------- Q6
  out("\n--- Q6: independent read-back through IMFSourceReader ---\n");
  const ReadbackResult rb = readback(samplePath);
  out("[Q6] MFCreateSourceReaderFromURL hr=%s\n", hrStr(rb.createHr).c_str());
  out("[Q6] container MF_PD_DURATION hr=%s duration=%.3fs\n", hrStr(rb.durationHr).c_str(),
      static_cast<double>(rb.duration100ns) / 10000000.0);
  for (const std::string& line : rb.streamLines) out("[Q6] %s\n", line.c_str());
  out("[Q6] ReadSample(first video) hr=%s gotFrame=%s timestamp=%lld(100ns) bytes=%lu decodedSubtype=%s\n",
      hrStr(rb.decodeHr).c_str(), rb.gotFrame ? "yes" : "no", rb.firstTimestamp, rb.firstBufferBytes,
      rb.decodedSubtype.empty() ? "?" : rb.decodedSubtype.c_str());
  out("[Q6] ANSWER: streamCount=%d videoStream=%d audioStream=%d firstFrameDecoded=%s\n", rb.streamCount,
      rb.videoStreamIndex, rb.audioStreamIndex, rb.gotFrame ? "yes" : "no");

  // ---------------------------------------------------------------- throughput
  out("\n--- throughput: 100 frames 1280x720 H.264 (video only) ---\n");
  const EncodeStats throughput = encodeToFile(throughputPath, shape, acceptedSubtype, 100, nullptr,
                                              0, 0, 0, synth, false);
  out("[throughput] ok=%s frames=%d elapsed=%lldms writeOnly=%.1fms convert=%.1fms bytes=%llu\n",
      throughput.failure.empty() ? "yes" : "no", throughput.videoSamplesWritten, throughput.totalMs,
      throughput.writeMs, throughput.convertMs, throughput.bytes);

  // ---------------------------------------------------------------- determinism
  out("\n--- determinism: same 60 frames + same PCM encoded twice ---\n");
  const EncodeStats detA = encodeToFile(detAPath, shape, acceptedSubtype, 60, &pcm,
                                        audioSampleRate, audioChannels, audioBits, synth, true);
  const EncodeStats detB = encodeToFile(detBPath, shape, acceptedSubtype, 60, &pcm,
                                        audioSampleRate, audioChannels, audioBits, synth, true);
  out("[determinism] a=%s bytes=%llu elapsed=%lldms\n", detAPath.c_str(), detA.bytes, detA.totalMs);
  out("[determinism] b=%s bytes=%llu elapsed=%lldms\n", detBPath.c_str(), detB.bytes, detB.totalMs);
  out("[determinism] sameByteSize=%s (sha256 compared outside this process)\n",
      detA.bytes == detB.bytes ? "yes" : "no");
  out("[determinism] aFailure=%s bFailure=%s\n", detA.failure.empty() ? "<none>" : detA.failure.c_str(),
      detB.failure.empty() ? "<none>" : detB.failure.c_str());

  out("\n=== probe done ===\n");
  MFShutdown();
  CoUninitialize();
  return 0;
}

int emitInputs(const std::string& dir) {
  out("mode=emit-inputs dir=%s\n", dir.c_str());
  Rgb24Frame frame;
  int written = 0;
  for (int index = 0; index < 60; ++index) {
    char name[64];
    std::snprintf(name, sizeof(name), "/frame_%03d.bmp", index);
    synthFrame(index, &frame);
    if (writeBmp24(dir + name, frame)) ++written;
  }
  const std::vector<uint8_t> pcm = makeSinePcm(48000, 2, 16, 2.0);
  const bool wavOk = writeWav(dir + "/audio.wav", pcm, 48000, 2, 16);
  out("mode=emit-inputs bmpWritten=%d bmpSize=%ux%u wavOk=%s wavBytes=%zu\n", written, kWidth, kHeight,
      wavOk ? "yes" : "no", pcm.size());
  return written == 60 && wavOk ? 0 : 5;
}

int encodeFromDisk(const std::string& dir, const std::string& outputPath) {
  out("mode=encode-from-disk dir=%s out=%s\n", dir.c_str(), outputPath.c_str());
  const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const HRESULT mfHr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  out("CoInitializeEx hr=%s MFStartup hr=%s\n", hrStr(coHr).c_str(), hrStr(mfHr).c_str());
  if (FAILED(mfHr)) return 3;

  std::vector<uint8_t> pcm;
  UINT32 sampleRate = 0;
  UINT32 channels = 0;
  UINT32 bits = 0;
  const bool wavOk = readWav(dir + "/audio.wav", &pcm, &sampleRate, &channels, &bits);
  out("readWav(%s/audio.wav) ok=%s bytes=%zu rate=%u channels=%u bits=%u\n", dir.c_str(),
      wavOk ? "yes" : "no", pcm.size(), sampleRate, channels, bits);
  if (!wavOk) {
    MFShutdown();
    CoUninitialize();
    return 6;
  }

  VideoShape shape;
  FrameProvider provider = [&dir](int index, Rgb24Frame* frame) {
    char name[64];
    std::snprintf(name, sizeof(name), "/frame_%03d.bmp", index);
    return readBmp24(dir + name, frame);
  };
  Rgb24Frame firstFrame;
  const bool firstOk = provider(0, &firstFrame);
  out("readBmp(frame_000.bmp) ok=%s bytes=%zu\n", firstOk ? "yes" : "no", firstFrame.pixels.size());
  if (!firstOk) {
    MFShutdown();
    CoUninitialize();
    return 7;
  }

  const EncodeStats stats = encodeToFile(outputPath, shape, MFVideoFormat_RGB32, 60, &pcm, sampleRate,
                                         channels, bits, provider, false);
  out("[from-disk] AddStream(video) hr=%s SetInputMediaType(video=RGB32) hr=%s\n",
      hrStr(stats.videoAddHr).c_str(), hrStr(stats.videoInputHr).c_str());
  out("[from-disk] AddStream(audio) hr=%s SetInputMediaType(audio) hr=%s Finalize hr=%s\n",
      hrStr(stats.audioAddHr).c_str(), hrStr(stats.audioInputHr).c_str(), hrStr(stats.finalizeHr).c_str());
  out("[from-disk] file=%s bytes=%llu elapsed=%lldms videoSamples=%d audioSamples=%d failure=%s\n",
      outputPath.c_str(), stats.bytes, stats.totalMs, stats.videoSamplesWritten, stats.audioSamplesWritten,
      stats.failure.empty() ? "<none>" : stats.failure.c_str());
  out("[from-disk] first 16 bytes: %s\n", firstBytesHex(outputPath, 16).c_str());
  const ReadbackResult rb = readback(outputPath);
  out("[from-disk] readback streamCount=%d firstFrameDecoded=%s duration=%.3fs\n", rb.streamCount,
      rb.gotFrame ? "yes" : "no", static_cast<double>(rb.duration100ns) / 10000000.0);
  MFShutdown();
  CoUninitialize();
  return stats.failure.empty() && rb.gotFrame ? 0 : 8;
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  std::string mode = "probe";
  std::string outDir = ".";
  std::string inputsDir;
  std::string fromDiskOut;
  std::string dumpInputsDir;
  std::string encodeDumpDir;
  std::string mp4Path;
  std::string decodeDir;
  std::string expectDir;
  std::string normalizeIn;
  std::string normalizeOut;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    const bool hasValue = index + 1 < argc;
    if (argument == "--out" && hasValue) {
      outDir = argv[++index];
    } else if (argument == "--emit-inputs" && hasValue) {
      mode = "emit";
      inputsDir = argv[++index];
    } else if (argument == "--encode-from-disk" && hasValue) {
      mode = "disk";
      inputsDir = argv[++index];
    } else if (argument == "--from-disk-out" && hasValue) {
      fromDiskOut = argv[++index];
    } else if (argument == "--dump-inputs" && hasValue) {
      mode = "dumpinputs";
      dumpInputsDir = argv[++index];
    } else if (argument == "--encode-dump" && hasValue) {
      mode = "encodedump";
      encodeDumpDir = argv[++index];
    } else if (argument == "--mp4" && hasValue) {
      mp4Path = argv[++index];
    } else if (argument == "--decode" && hasValue) {
      mode = "decode";
      mp4Path = argv[++index];
    } else if (argument == "--dump-decoded" && hasValue) {
      decodeDir = argv[++index];
    } else if (argument == "--expect-dump" && hasValue) {
      expectDir = argv[++index];
    } else if (argument == "--normalize" && index + 2 < argc) {
      mode = "normalize";
      normalizeIn = argv[++index];
      normalizeOut = argv[++index];
    } else if (argument == "--help") {
      std::printf("usage: mf_export_probe --out <dir> | --emit-inputs <dir> | --encode-from-disk <dir> --from-disk-out <file>\n");
      return 0;
    } else {
      std::printf("unknown argument: %s\n", argument.c_str());
      return 2;
    }
  }
  if (mode == "probe") {
    const std::string logPath = outDir + "/probe.log";
    g_log = std::fopen(logPath.c_str(), "wb");
  }
  int result = 0;
  if (mode == "emit") {
    result = emitInputs(inputsDir);
  } else if (mode == "disk") {
    result = encodeFromDisk(inputsDir, fromDiskOut);
  } else if (mode == "dumpinputs") {
    result = dumpInputs(dumpInputsDir);
  } else if (mode == "encodedump") {
    if (mp4Path.empty()) {
      std::printf("--encode-dump needs --mp4 <file>\n");
      result = 2;
    } else {
      result = encodeDump(encodeDumpDir, mp4Path);
    }
  } else if (mode == "decode") {
    if (mp4Path.empty() || decodeDir.empty()) {
      std::printf("--decode needs --dump-decoded <dir>\n");
      result = 2;
    } else {
      result = decodeDump(mp4Path, decodeDir, expectDir);
    }
  } else if (mode == "normalize") {
    result = normalizeMp4(normalizeIn, normalizeOut);
  } else {
    result = runProbe(outDir);
  }
  if (g_log != nullptr) std::fclose(g_log);
  return result;
}
