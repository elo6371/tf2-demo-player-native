// video_export.cpp -- BMP frame sequence (+ optional PCM) -> MP4, and MP4 verify.
//
// WHY THIS FILE EXISTS
//   The product writes one BMP per captured tick (--capture-sequence, N27 step 1).
//   The in-product --export-video switch cannot land in this branch because the
//   owner's constraint keeps native/src/main.cpp out of this work, so the hook
//   ships as a patch (native/tools/export-hook-main.patch). This tool is the
//   offline half of the same chain and exercises the same product encoder module:
//
//     tf2_demo_native --capture-sequence <dir> 1200 8
//     video_export --frames <dir> --frame-count 8 --size 1264x680 \
//         --audio take.pcm --out take.mp4
//     video_export --verify take.mp4
//
//   It links native/src/demo_export.cpp (in tf2_core), so the bytes it produces
//   come from the product's encoder wrapper, not a second copy of it. Only file
//   I/O and the BMP/WAV readers live here.
//
//   --verify is the independent reader: it reopens the MP4 through
//   IMFSourceReader, decodes every video sample to NV12 and every audio sample to
//   PCM, and reports frame counts, durations and whether presentation times are
//   strictly increasing. It shares no writer code, which is why the gate treats
//   its numbers as evidence instead of the writer grading itself.
//
// WHY THE SIZE IS EXPLICIT
//   The product's client area is 1264x681 on this machine (a 1280x720 window
//   minus the frame), an odd height. H.264 is 4:2:0, so the sink refuses odd
//   dimensions rather than cropping silently. Without --size the tool takes the
//   size from the first BMP and reduces an odd dimension by one row/column,
//   saying so in the trace. Nothing is scaled: the top-left region is copied and
//   the remainder padded black, so pixel comparisons against the BMPs still line
//   up.
//
// TRACE
//   Every line starts with "video-export:" and is ASCII, so the gate can grep it.

#include "demo_export.h"

// The build sets WIN32_LEAN_AND_MEAN and NOMINMAX for every target; repeating the
// defines here is C4005.
#include <windows.h>

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::int64_t kTicksPerSecond = 10000000;

std::string narrow(const std::wstring& text) {
  if (text.empty()) return std::string();
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) return std::string();
  std::string out(static_cast<std::size_t>(bytes - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), bytes, nullptr, nullptr);
  return out;
}

std::uint16_t readU16(const std::uint8_t* data) {
  return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
}

std::uint32_t readU32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::int32_t readI32(const std::uint8_t* data) {
  return static_cast<std::int32_t>(readU32(data));
}

bool readWholeFile(const std::wstring& path, std::vector<std::uint8_t>* bytes, std::string* error) {
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
    *error = "could not open " + narrow(path);
    return false;
  }
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(file);
    *error = "file is empty: " + narrow(path);
    return false;
  }
  bytes->assign(static_cast<std::size_t>(size), 0);
  const std::size_t read = std::fread(bytes->data(), 1, bytes->size(), file);
  std::fclose(file);
  if (read != bytes->size()) {
    *error = "short read: " + narrow(path);
    return false;
  }
  return true;
}

struct BitmapFrame {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgb24;  // top-down, tightly packed
};

// The product's writer (native_renderer_capture.cpp writeBackBufferToFile) emits
// 24-bit BI_RGB BMPs with bottom-up rows and 4-byte row padding. This reader also
// accepts 32-bit and top-down files so a frame dumped by another tool still
// loads, but rejects anything compressed.
// collectFramePaths: every "frame_<digits>.bmp" in the directory, ordered by the
// number. The writer (capture-sequence-check.sh) has used both frame_000.bmp and
// frame_000000.bmp spellings over the project history, and a fixed printf width
// would silently read the wrong files -- or none -- after a rename. Sorting by
// value also keeps the order independent of how the filesystem returns entries.
bool collectFramePaths(const std::wstring& dir, std::vector<std::wstring>* paths,
                       std::string* error) {
  std::error_code ec;
  std::filesystem::directory_iterator iterator(dir, ec);
  if (ec) {
    *error = "could not open the frames directory " + narrow(dir);
    return false;
  }
  std::vector<std::pair<unsigned long long, std::wstring>> numbered;
  for (const auto& entry : iterator) {
    if (!entry.is_regular_file(ec) || ec) continue;
    const std::wstring name = entry.path().filename().wstring();
    constexpr wchar_t kPrefix[] = L"frame_";
    constexpr wchar_t kSuffix[] = L".bmp";
    const std::size_t prefixLength = 6;
    const std::size_t suffixLength = 4;
    if (name.size() <= prefixLength + suffixLength) continue;
    if (name.compare(0, prefixLength, kPrefix) != 0) continue;
    if (name.compare(name.size() - suffixLength, suffixLength, kSuffix) != 0) continue;
    const std::wstring digits = name.substr(prefixLength, name.size() - prefixLength - suffixLength);
    if (digits.empty()) continue;
    bool numeric = true;
    for (const wchar_t c : digits) {
      if (c < L'0' || c > L'9') { numeric = false; break; }
    }
    if (!numeric) continue;
    numbered.emplace_back(std::wcstoull(digits.c_str(), nullptr, 10),
                          (std::filesystem::path(dir) / name).wstring());
  }
  if (numbered.empty()) {
    *error = "no frame_<digits>.bmp files in " + narrow(dir);
    return false;
  }
  std::sort(numbered.begin(), numbered.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
  paths->clear();
  paths->reserve(numbered.size());
  for (const auto& entry : numbered) paths->push_back(entry.second);
  return true;
}
bool loadBitmap(const std::wstring& path, BitmapFrame* frame, std::string* error) {
  std::vector<std::uint8_t> bytes;
  if (!readWholeFile(path, &bytes, error)) return false;
  if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') {
    *error = "not a BMP: " + narrow(path);
    return false;
  }
  const std::uint32_t dataOffset = readU32(bytes.data() + 10);
  const std::uint32_t dibSize = readU32(bytes.data() + 14);
  if (dibSize < 40) {
    *error = "BMP DIB header is too old: " + narrow(path);
    return false;
  }
  const std::int32_t width = readI32(bytes.data() + 18);
  const std::int32_t heightRaw = readI32(bytes.data() + 22);
  const std::uint16_t bits = readU16(bytes.data() + 28);
  const std::uint32_t compression = readU32(bytes.data() + 30);
  if (width <= 0 || heightRaw == 0) {
    *error = "BMP has an empty size: " + narrow(path);
    return false;
  }
  if (bits != 24 && bits != 32) {
    *error = "BMP is not 24 or 32 bits: " + narrow(path);
    return false;
  }
  if (compression != 0 && compression != 3) {
    *error = "BMP is compressed: " + narrow(path);
    return false;
  }
  const bool topDown = heightRaw < 0;
  const int height = topDown ? -heightRaw : heightRaw;
  const std::size_t sourceStride = bits == 24
      ? ((static_cast<std::size_t>(width) * 3 + 3) / 4) * 4
      : static_cast<std::size_t>(width) * 4;
  if (dataOffset + sourceStride * static_cast<std::size_t>(height) > bytes.size()) {
    *error = "BMP pixel data is truncated: " + narrow(path);
    return false;
  }
  frame->width = width;
  frame->height = height;
  frame->rgb24.assign(static_cast<std::size_t>(width) * height * 3, 0);
  for (int y = 0; y < height; ++y) {
    const int sourceRow = topDown ? y : (height - 1 - y);
    const std::uint8_t* source =
        bytes.data() + dataOffset + static_cast<std::size_t>(sourceRow) * sourceStride;
    std::uint8_t* target = frame->rgb24.data() + static_cast<std::size_t>(y) * width * 3;
    for (int x = 0; x < width; ++x) {
      // BMP stores BGR(A); the sink's converter expects RGB24.
      target[x * 3 + 0] = bits == 24 ? source[x * 3 + 2] : source[x * 4 + 2];
      target[x * 3 + 1] = bits == 24 ? source[x * 3 + 1] : source[x * 4 + 1];
      target[x * 3 + 2] = bits == 24 ? source[x * 3 + 0] : source[x * 4 + 0];
    }
  }
  return true;
}

// Copies the top-left min(source, target) region and pads the rest black. No
// scaling: a scaled frame could not be compared against the BMP sequence.
void fitFrame(const BitmapFrame& source, int targetWidth, int targetHeight,
              std::vector<std::uint8_t>* out) {
  out->assign(static_cast<std::size_t>(targetWidth) * targetHeight * 3, 0);
  const int copyWidth = std::min<int>(source.width, targetWidth);
  const int copyHeight = std::min<int>(source.height, targetHeight);
  for (int y = 0; y < copyHeight; ++y) {
    std::memcpy(out->data() + static_cast<std::size_t>(y) * targetWidth * 3,
                source.rgb24.data() + static_cast<std::size_t>(y) * source.width * 3,
                static_cast<std::size_t>(copyWidth) * 3);
  }
}

struct PcmSource {
  std::vector<std::uint8_t> bytes;
  std::uint32_t sampleRate = 48000;
  std::uint32_t channels = 2;
};

bool loadWav(const std::vector<std::uint8_t>& file, PcmSource* pcm, std::string* error) {
  if (file.size() < 44 || std::memcmp(file.data(), "RIFF", 4) != 0 ||
      std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
    *error = "not a RIFF/WAVE file";
    return false;
  }
  std::uint64_t cursor = 12;
  std::uint16_t format = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bits = 0;
  bool haveFormat = false;
  while (cursor + 8 <= file.size()) {
    const std::uint8_t* chunk = file.data() + cursor;
    const std::uint32_t chunkSize = readU32(chunk + 4);
    const std::uint64_t body = cursor + 8;
    if (body + chunkSize > file.size()) break;
    if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
      format = readU16(file.data() + body + 0);
      channels = readU16(file.data() + body + 2);
      sampleRate = readU32(file.data() + body + 4);
      bits = readU16(file.data() + body + 14);
      haveFormat = true;
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      pcm->bytes.assign(file.data() + body, file.data() + body + chunkSize);
    }
    cursor = body + chunkSize + (chunkSize & 1u);
  }
  if (!haveFormat || pcm->bytes.empty()) {
    *error = "WAV has no fmt/data chunk";
    return false;
  }
  if (format != 1 || bits != 16) {
    *error = "WAV is not 16-bit PCM (format " + std::to_string(format) + ", bits " +
             std::to_string(bits) + ")";
    return false;
  }
  pcm->sampleRate = sampleRate;
  pcm->channels = channels;
  if (channels == 0 || channels > 2) {
    *error = "WAV channel count is unsupported: " + std::to_string(channels);
    return false;
  }
  if (sampleRate != 44100 && sampleRate != 48000) {
    *error = "WAV sample rate is unsupported: " + std::to_string(sampleRate);
    return false;
  }
  return true;
}

bool loadPcm(const std::wstring& path, std::uint32_t expectedRate, std::uint32_t expectedChannels,
             PcmSource* pcm, std::string* error) {
  std::vector<std::uint8_t> raw;
  if (!readWholeFile(path, &raw, error)) return false;
  if (raw.size() >= 12 && std::memcmp(raw.data(), "RIFF", 4) == 0) {
    if (!loadWav(raw, pcm, error)) return false;
  } else {
    // Raw little-endian PCM: s16 interleaved. The layout has to be stated on the
    // command line because a raw file carries no header.
    pcm->bytes = std::move(raw);
    pcm->sampleRate = expectedRate;
    pcm->channels = expectedChannels;
  }
  const std::uint32_t blockAlign = pcm->channels * 2;
  if (pcm->bytes.size() % blockAlign != 0) {
    *error = "PCM byte count is not a whole number of sample frames";
    return false;
  }
  return true;
}

void printUsage() {
  // fputs, not printf: the text contains '%06d' and 'WxH' placeholders that are
  // documentation, and printf would read them as format specifiers (C4473).
  std::fputs(
      "usage:\n"
      "  video_export --frames <dir> --frame-count <n> --out <file.mp4> [options]\n"
      "  video_export --verify <file.mp4>\n"
      "options:\n"
      "  --start-index <n>           first frame_<digits>.bmp in sorted order (default 0)\n"
      "  --size <WxH>                output size; default = first BMP, odd dimension -1\n"
      "  --fps-num <n> --fps-den <n> frame rate ratio (default 200/3 = demo tick rate)\n"
      "  --bitrate <bits/s>          H.264 average bit rate (default 8000000)\n"
      "  --audio <file.wav|.pcm>     optional PCM track\n"
      "  --audio-rate <hz>           raw PCM rate (default 48000)\n"
      "  --audio-channels <n>        raw PCM channels (default 2)\n"
      "  --overwrite                 replace an existing output file\n"
      "  --allow-duration-mismatch   do not fail when audio and video lengths differ\n",
      stdout);
}

bool takeValue(const std::vector<std::wstring>& args, std::size_t* index, std::wstring* value) {
  if (*index + 1 >= args.size()) return false;
  *value = args[*index + 1];
  *index += 1;
  return true;
}

bool parseUnsigned(const std::wstring& text, std::uint32_t* value) {
  if (text.empty()) return false;
  wchar_t* end = nullptr;
  const unsigned long parsed = std::wcstoul(text.c_str(), &end, 10);
  if (end == nullptr || *end != L'\0') return false;
  *value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool parseSize(const std::wstring& text, std::uint32_t* width, std::uint32_t* height) {
  if (text.empty()) return false;
  wchar_t* end = nullptr;
  const unsigned long w = std::wcstoul(text.c_str(), &end, 10);
  if (end == nullptr || (*end != L'x' && *end != L'X')) return false;
  const wchar_t* rest = end + 1;
  const unsigned long h = std::wcstoul(rest, &end, 10);
  if (end == nullptr || *end != L'\0') return false;
  *width = static_cast<std::uint32_t>(w);
  *height = static_cast<std::uint32_t>(h);
  return true;
}

int runEncode(const std::vector<std::wstring>& args) {
  std::wstring framesDir;
  std::wstring audioPath;
  std::wstring outPath;
  bool haveFrames = false;
  bool haveCount = false;
  bool haveOut = false;
  std::uint32_t frameCount = 0;
  std::uint32_t startIndex = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t fpsNum = 200;
  std::uint32_t fpsDen = 3;
  std::uint32_t bitRate = 8000000;
  std::uint32_t audioRate = 48000;
  std::uint32_t audioChannels = 2;
  bool overwrite = false;
  bool allowDurationMismatch = false;

  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::wstring& arg = args[i];
    std::wstring value;
    if (arg == L"--frames") {
      if (!takeValue(args, &i, &framesDir)) {
        std::printf("video-export: result=fail detail=--frames needs a value\n");
        return 2;
      }
      haveFrames = true;
    } else if (arg == L"--frame-count") {
      if (!takeValue(args, &i, &value) || !parseUnsigned(value, &frameCount)) {
        std::printf("video-export: result=fail detail=--frame-count needs a number\n");
        return 2;
      }
      haveCount = true;
    } else if (arg == L"--start-index") {
      if (!takeValue(args, &i, &value) || !parseUnsigned(value, &startIndex)) {
        std::printf("video-export: result=fail detail=--start-index needs a number\n");
        return 2;
      }
    } else if (arg == L"--out") {
      if (!takeValue(args, &i, &outPath)) {
        std::printf("video-export: result=fail detail=--out needs a value\n");
        return 2;
      }
      haveOut = true;
    } else if (arg == L"--size") {
      if (!takeValue(args, &i, &value) || !parseSize(value, &width, &height)) {
        std::printf("video-export: result=fail detail=--size needs WxH\n");
        return 2;
      }
    } else if (arg == L"--fps-num") {
      if (!takeValue(args, &i, &value) || !parseUnsigned(value, &fpsNum)) return 2;
    } else if (arg == L"--fps-den") {
      if (!takeValue(args, &i, &value) || !parseUnsigned(value, &fpsDen)) return 2;
    } else if (arg == L"--bitrate") {
      if (!takeValue(args, &i, &value) || !parseUnsigned(value, &bitRate)) return 2;
    } else if (arg == L"--audio") {
      if (!takeValue(args, &i, &audioPath)) return 2;
    } else if (arg == L"--audio-rate") {
      if (!takeValue(args, &i, &value) || !parseUnsigned(value, &audioRate)) return 2;
    } else if (arg == L"--audio-channels") {
      if (!takeValue(args, &i, &value) || !parseUnsigned(value, &audioChannels)) return 2;
    } else if (arg == L"--overwrite") {
      overwrite = true;
    } else if (arg == L"--allow-duration-mismatch") {
      allowDurationMismatch = true;
    } else {
      std::printf("video-export: result=fail detail=unknown argument %s\n", narrow(arg).c_str());
      return 2;
    }
  }
  if (!haveFrames || !haveCount || !haveOut || frameCount == 0) {
    std::printf("video-export: result=fail detail=--frames, --frame-count and --out are required\n");
    printUsage();
    return 2;
  }
  if (fpsNum == 0 || fpsDen == 0 ||
      (kTicksPerSecond * static_cast<std::int64_t>(fpsDen)) % fpsNum != 0) {
    std::printf("video-export: result=fail detail=frame rate does not divide into 100 ns units\n");
    return 2;
  }
  const std::int64_t frameDuration =
      (kTicksPerSecond * static_cast<std::int64_t>(fpsDen)) / fpsNum;

  std::vector<std::wstring> frames;
  std::string error;
  if (!collectFramePaths(framesDir, &frames, &error)) {
    std::printf("video-export: result=fail detail=%s\n", error.c_str());
    return 1;
  }
  if (static_cast<std::uint64_t>(startIndex) + frameCount > frames.size()) {
    std::printf(
        "video-export: result=fail detail=frame range %u..%llu is past the %zu frame_<digits>.bmp "
        "files in %s\n",
        startIndex,
        static_cast<unsigned long long>(startIndex + static_cast<std::uint64_t>(frameCount) - 1),
        frames.size(), narrow(framesDir).c_str());
    return 1;
  }
  BitmapFrame first;
  if (!loadBitmap(frames[startIndex], &first, &error)) {
    std::printf("video-export: result=fail detail=%s\n", error.c_str());
    return 1;
  }
  if (width == 0 || height == 0) {
    width = static_cast<std::uint32_t>(first.width);
    height = static_cast<std::uint32_t>(first.height);
    if ((width % 2) != 0) {
      std::printf("video-export: note=source width %u is odd; cropping one column\n", width);
      --width;
    }
    if ((height % 2) != 0) {
      std::printf("video-export: note=source height %u is odd; cropping one row\n", height);
      --height;
    }
  }
  if ((width % 2) != 0 || (height % 2) != 0 || width == 0 || height == 0) {
    std::printf("video-export: result=fail detail=output size must be even and non-zero\n");
    return 2;
  }

  PcmSource pcm;
  bool haveAudio = false;
  if (!audioPath.empty()) {
    if (!loadPcm(audioPath, audioRate, audioChannels, &pcm, &error)) {
      std::printf("video-export: result=fail detail=%s\n", error.c_str());
      return 1;
    }
    haveAudio = true;
    const std::uint64_t audioFrames = pcm.bytes.size() / (pcm.channels * 2);
    const std::int64_t audioEnd = static_cast<std::int64_t>(
        (audioFrames * static_cast<std::uint64_t>(kTicksPerSecond)) / pcm.sampleRate);
    const std::int64_t videoEnd = static_cast<std::int64_t>(frameCount) * frameDuration;
    const std::int64_t difference = audioEnd > videoEnd ? audioEnd - videoEnd : videoEnd - audioEnd;
    std::printf("video-export: audio frames=%llu rate=%u channels=%u ms=%lld\n",
                static_cast<unsigned long long>(audioFrames), pcm.sampleRate, pcm.channels,
                static_cast<long long>(audioEnd / 10000));
    if (difference > 1000 && !allowDurationMismatch) {
      std::printf(
          "video-export: result=fail detail=audio ends at tick %lld but the video ends at tick %lld; "
          "pass --allow-duration-mismatch to encode anyway\n",
          static_cast<long long>(audioEnd / frameDuration),
          static_cast<long long>(videoEnd / frameDuration));
      return 2;
    }
  }

  std::printf("video-export: frames-dir=%s frames=%u start=%u size=%ux%u fps=%u/%u bitrate=%u\n",
              narrow(framesDir).c_str(), frameCount, startIndex, width, height, fpsNum, fpsDen,
              bitRate);
  if (haveAudio) std::printf("video-export: audio=%s\n", narrow(audioPath).c_str());

  tf2::native::ExportVideoConfig video;
  video.width = width;
  video.height = height;
  video.fpsNumerator = fpsNum;
  video.fpsDenominator = fpsDen;
  video.bitRate = bitRate;
  tf2::native::ExportAudioConfig audio;
  audio.sampleRate = pcm.sampleRate;
  audio.channels = pcm.channels;
  audio.bitsPerSample = 16;

  tf2::native::Mp4ExportSink sink;
  std::string detail;
  tf2::native::ExportResult result =
      sink.open(outPath, video, haveAudio ? &audio : nullptr, overwrite, &detail);
  if (result != tf2::native::ExportResult::Ok) {
    std::printf("video-export: result=fail stage=open code=%s detail=%s\n",
                tf2::native::exportResultName(result), detail.c_str());
    return 1;
  }

  std::vector<std::uint8_t> target;
  for (std::uint32_t index = 0; index < frameCount; ++index) {
    const std::wstring& framePath = frames[startIndex + index];
    BitmapFrame frame;
    if (!loadBitmap(framePath, &frame, &error)) {
      std::printf("video-export: result=fail detail=%s\n", error.c_str());
      sink.abort();
      return 1;
    }
    if (frame.width != first.width || frame.height != first.height) {
      std::printf("video-export: result=fail detail=frame %u has size %dx%d, expected %dx%d\n",
                  index, frame.width, frame.height, first.width, first.height);
      sink.abort();
      return 1;
    }
    fitFrame(frame, static_cast<int>(width), static_cast<int>(height), &target);
    const std::int64_t timestamp = static_cast<std::int64_t>(index) * frameDuration;
    result = sink.writeVideoFrame(target.data(), static_cast<std::size_t>(width) * 3, timestamp,
                                  &detail);
    if (result != tf2::native::ExportResult::Ok) {
      std::printf("video-export: result=fail stage=video frame=%u code=%s detail=%s\n", index,
                  tf2::native::exportResultName(result), detail.c_str());
      sink.abort();
      return 1;
    }
    if (index % 30 == 0 || index + 1 == frameCount) {
      std::printf("video-export: frame=%u ts=%lld\n", index, static_cast<long long>(timestamp));
    }
  }

  if (haveAudio) {
    result = sink.writeAudio(pcm.bytes.data(), pcm.bytes.size(), &detail);
    if (result != tf2::native::ExportResult::Ok) {
      std::printf("video-export: result=fail stage=audio code=%s detail=%s\n",
                  tf2::native::exportResultName(result), detail.c_str());
      sink.abort();
      return 1;
    }
  }

  result = sink.finalize(&detail);
  if (result != tf2::native::ExportResult::Ok) {
    std::printf("video-export: result=fail stage=finalize code=%s detail=%s\n",
                tf2::native::exportResultName(result), detail.c_str());
    return 1;
  }
  const tf2::native::ExportStats& stats = sink.stats();
  std::printf(
      "video-export: wrote=%s video-frames=%u audio-frames=%llu video-first-ts=%lld "
      "video-last-ts=%lld normalized-fields=%u rename-attempts=%u\n",
      narrow(outPath).c_str(), stats.videoFrames,
      static_cast<unsigned long long>(stats.audioFrames),
      static_cast<long long>(stats.firstVideoTimestamp),
      static_cast<long long>(stats.lastVideoTimestamp), stats.normalizedFields,
      stats.renameAttempts);
  std::printf("video-export: result=ok\n");
  return 0;
}

// Opens the finished MP4 with IMFSourceReader and reports what is actually in it.
int runVerify(const std::wstring& path) {
  const HRESULT startup = MFStartup(MF_VERSION);
  if (FAILED(startup)) {
    std::printf("video-export: verify result=fail detail=MFStartup failed\n");
    return 1;
  }
  ComPtr<IMFSourceReader> reader;
  HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, reader.GetAddressOf());
  if (FAILED(hr)) {
    std::printf("video-export: verify result=fail detail=could not open the file\n");
    MFShutdown();
    return 1;
  }
  // A source reader has no presentation descriptor accessor; the container
  // attributes come through GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE)
  // and the stream list through GetNativeMediaType until the reader runs out of
  // stream numbers. Both are the documented reader-side equivalents.
  UINT64 duration = 0;
  bool haveDuration = false;
  {
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(reader->GetPresentationAttribute(
            static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value)) &&
        value.vt == VT_UI8) {
      duration = value.uhVal.QuadPart;
      haveDuration = true;
    }
    PropVariantClear(&value);
  }

  DWORD videoStream = MAXDWORD;
  DWORD audioStream = MAXDWORD;
  DWORD videoCount = 0;
  DWORD audioCount = 0;
  DWORD streamCount = 0;
  for (DWORD index = 0;; ++index) {
    ComPtr<IMFMediaType> nativeType;
    hr = reader->GetNativeMediaType(index, 0, nativeType.GetAddressOf());
    if (FAILED(hr)) break;  // MF_E_INVALIDSTREAMNUMBER: past the last stream
    GUID major = {};
    if (FAILED(nativeType->GetGUID(MF_MT_MAJOR_TYPE, &major))) continue;
    ++streamCount;
    if (major == MFMediaType_Video) {
      ++videoCount;
      if (videoStream == MAXDWORD) videoStream = index;
    } else if (major == MFMediaType_Audio) {
      ++audioCount;
      if (audioStream == MAXDWORD) audioStream = index;
    }
  }
  std::printf("video-export: verify streams=%lu video-streams=%lu audio-streams=%lu\n",
              static_cast<unsigned long>(streamCount), static_cast<unsigned long>(videoCount),
              static_cast<unsigned long>(audioCount));
  if (haveDuration) {
    std::printf("video-export: verify duration-100ns=%llu duration-ms=%llu\n",
                static_cast<unsigned long long>(duration),
                static_cast<unsigned long long>(duration / 10000));
  }

  UINT32 videoFrames = 0;
  UINT32 videoWidth = 0;
  UINT32 videoHeight = 0;
  LONGLONG firstTimestamp = 0;
  LONGLONG lastTimestamp = 0;
  bool monotonic = true;
  if (videoStream != MAXDWORD) {
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(videoStream, TRUE);
    ComPtr<IMFMediaType> nativeType;
    if (SUCCEEDED(reader->GetNativeMediaType(videoStream, 0, nativeType.GetAddressOf()))) {
      MFGetAttributeSize(nativeType.Get(), MF_MT_FRAME_SIZE, &videoWidth, &videoHeight);
    }
    if (videoWidth > 0 && videoHeight > 0) {
      // NV12 is what the H.264 decoder emits natively, so no colour-converter
      // MFT has to be inserted and every decoded sample really is one frame.
      ComPtr<IMFMediaType> outputType;
      if (SUCCEEDED(MFCreateMediaType(outputType.GetAddressOf()))) {
        outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, videoWidth, videoHeight);
        reader->SetCurrentMediaType(videoStream, nullptr, outputType.Get());
      }
    }
    for (;;) {
      DWORD flags = 0;
      LONGLONG timestamp = 0;
      ComPtr<IMFSample> sample;
      hr = reader->ReadSample(videoStream, 0, nullptr, &flags, &timestamp, sample.GetAddressOf());
      if (FAILED(hr)) break;
      if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) break;
      if (!sample) continue;
      if (videoFrames > 0 && timestamp <= lastTimestamp) monotonic = false;
      if (videoFrames == 0) firstTimestamp = timestamp;
      lastTimestamp = timestamp;
      ++videoFrames;
    }
  }

  std::uint64_t audioFrames = 0;
  std::uint64_t audioBytes = 0;
  // Peak and RMS of the DECODED samples, not of the input file: a track that
  // survives the muxer but decodes to zeros (wrong format negotiation, silent
  // encoder input, half-written buffer) reads as peak=0 here.
  std::int64_t audioPeak = 0;
  double audioSquareSum = 0.0;
  UINT32 audioRate = 0;
  UINT32 audioChannels = 0;
  if (audioStream != MAXDWORD) {
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(audioStream, TRUE);
    // Rewind before reading the second stream. Without this the reader answers the
    // first audio ReadSample with ENDOFSTREAM (flags=0x2, sample=null) because the
    // video pass already walked the presentation to its end; measured on the
    // 8-frame smoke file, where the seek turned 0 decoded samples into 6144.
    {
      PROPVARIANT start;
      PropVariantInit(&start);
      start.vt = VT_I8;
      start.hVal.QuadPart = 0;
      reader->SetCurrentPosition(GUID_NULL, start);
      PropVariantClear(&start);
    }
    ComPtr<IMFMediaType> nativeType;
    if (SUCCEEDED(reader->GetNativeMediaType(audioStream, 0, nativeType.GetAddressOf()))) {
      nativeType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &audioRate);
      nativeType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &audioChannels);
    }
    if (audioRate > 0 && audioChannels > 0) {
      ComPtr<IMFMediaType> pcmType;
      if (SUCCEEDED(MFCreateMediaType(pcmType.GetAddressOf()))) {
        pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        pcmType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audioRate);
        pcmType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audioChannels);
        pcmType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, audioChannels * 2);
        pcmType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audioRate * audioChannels * 2);
        reader->SetCurrentMediaType(audioStream, nullptr, pcmType.Get());
      }
    }
    const std::uint32_t blockAlign = (audioChannels > 0 ? audioChannels : 2) * 2;
    for (;;) {
      DWORD flags = 0;
      ComPtr<IMFSample> sample;
      hr = reader->ReadSample(audioStream, 0, nullptr, &flags, nullptr, sample.GetAddressOf());
      if (FAILED(hr)) break;
      if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) break;
      if (!sample) continue;
      ComPtr<IMFMediaBuffer> buffer;
      if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) continue;
      DWORD length = 0;
      if (FAILED(buffer->GetCurrentLength(&length))) continue;
      audioBytes += length;
      audioFrames += length / blockAlign;
      BYTE* samples = nullptr;
      if (SUCCEEDED(buffer->Lock(&samples, nullptr, nullptr))) {
        const auto* pcm16 = reinterpret_cast<const std::int16_t*>(samples);
        const std::size_t count = length / 2;
        for (std::size_t sampleIndex = 0; sampleIndex < count; ++sampleIndex) {
          const std::int64_t value = pcm16[sampleIndex];
          const std::int64_t magnitude = value < 0 ? -value : value;
          if (magnitude > audioPeak) audioPeak = magnitude;
          const double asDouble = static_cast<double>(value);
          audioSquareSum += asDouble * asDouble;
        }
        buffer->Unlock();
      }
    }
  }

  std::printf(
      "video-export: verify video-frames=%lu width=%lu height=%lu first-ts=%lld last-ts=%lld "
      "monotonic=%d\n",
      static_cast<unsigned long>(videoFrames), static_cast<unsigned long>(videoWidth),
      static_cast<unsigned long>(videoHeight), static_cast<long long>(firstTimestamp),
      static_cast<long long>(lastTimestamp), monotonic ? 1 : 0);
  const std::uint64_t audioSamples = audioBytes / 2;
  const std::int64_t audioRms = audioSamples > 0
      ? static_cast<std::int64_t>(std::sqrt(audioSquareSum / static_cast<double>(audioSamples)) + 0.5)
      : 0;
  std::printf("video-export: verify audio-frames=%llu audio-bytes=%llu audio-rate=%lu "
              "audio-channels=%lu audio-peak=%lld audio-rms=%lld\n",
              static_cast<unsigned long long>(audioFrames),
              static_cast<unsigned long long>(audioBytes),
              static_cast<unsigned long>(audioRate), static_cast<unsigned long>(audioChannels),
              static_cast<long long>(audioPeak), static_cast<long long>(audioRms));
  // An audio stream that decodes to nothing is a failure of the reader path, not a
  // silent-file property: the encoder refuses to open an audio track with no
  // samples, so a container that HAS an audio track must yield decoded samples.
  const bool audioOk = audioCount == 0 || audioFrames > 0;
  const bool ok = videoCount > 0 && videoFrames > 0 && monotonic && audioOk;
  std::printf("video-export: verify result=%s\n", ok ? "ok" : "fail");
  MFShutdown();
  return ok ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::vector<std::wstring> args(argv + 1, argv + argc);
  if (args.empty()) {
    printUsage();
    return 2;
  }
  if (args[0] == L"--help" || args[0] == L"-h") {
    printUsage();
    return 0;
  }
  if (args[0] == L"--verify") {
    if (args.size() < 2) {
      std::printf("video-export: result=fail detail=--verify needs a path\n");
      return 2;
    }
    return runVerify(args[1]);
  }
  return runEncode(args);
}
