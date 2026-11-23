#include <Windows.h>
#include <mmsystem.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::string mmError(MMRESULT result, const char* operation) {
  char buffer[256]{};
  if (waveOutGetErrorTextA(result, buffer, static_cast<UINT>(sizeof(buffer))) == MMSYSERR_NOERROR)
    return std::string(operation) + ": " + buffer;
  return std::string(operation) + " failed=" + std::to_string(result);
}
}

int main(int argc, char** argv) {
  bool hasDevice = false;
  bool listDevices = false;
  UINT device = 0;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--list-devices") { listDevices = true; continue; }
    if (std::string(argv[i]) != "--device" || i + 1 >= argc) {
      std::cerr << "usage: winmm_sink_probe [--list-devices] [--device N]\n";
      return 2;
    }
    char* end = nullptr;
    const auto parsed = std::strtoul(argv[++i], &end, 10);
    if (!end || *end != '\0' || parsed > UINT_MAX) {
      std::cerr << "invalid_device_argument\n";
      return 2;
    }
    hasDevice = true;
    device = static_cast<UINT>(parsed);
  }
  if (listDevices && hasDevice) { std::cerr << "device_selection_arguments_conflict\n"; return 2; }
  if (listDevices) {
    const UINT deviceCount = waveOutGetNumDevs();
    std::cout << "mode=list_devices device_count=" << deviceCount << "\n";
    for (UINT index = 0; index < deviceCount; ++index) {
      WAVEOUTCAPSA caps{};
      const MMRESULT result = waveOutGetDevCapsA(index, &caps, sizeof(caps));
      std::cout << "device=" << index << " query=" << static_cast<unsigned>(result);
      if (result == MMSYSERR_NOERROR) std::cout << " name=" << caps.szPname;
      std::cout << "\n";
    }
    return 0;
  }
  if (!hasDevice) {
    std::cout << "mode=dry_run device_opened=0 playback_written=0 reset=skipped close=skipped"
              << " reset_result=-1 unprepare_result=-1 close_result=-1 status=pass\n";
    return 0;
  }
  const UINT deviceCount = waveOutGetNumDevs();
  if (device >= deviceCount) {
    std::cout << "mode=explicit device=" << device << " device_count=" << deviceCount
              << " open_attempt=0 device_opened=0 playback_written=0"
              << " reset_result=-1 unprepare_result=-1 close_result=-1 status=invalid_device\n";
    return 3;
  }

  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 1;
  format.nSamplesPerSec = 8000;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 2;
  format.nAvgBytesPerSec = 16000;
  std::vector<std::uint8_t> silence(1600, 0);
  HWAVEOUT output = nullptr;
  WAVEHDR header{};
  bool prepared = false;
  bool written = false;
  bool opened = false;
  bool resetCalled = false;
  bool resetOk = false;
  bool unprepareCalled = false;
  bool unprepareOk = false;
  bool closeCalled = false;
  bool closeOk = false;
  int openResult = -1;
  int prepareResult = -1;
  int writeResult = -1;
  int resetResult = -1;
  int unprepareResult = -1;
  int closeResult = -1;
  std::string failure;
  const auto fail = [&](MMRESULT result, const char* operation) {
    if (failure.empty()) failure = mmError(result, operation);
  };
  MMRESULT result = waveOutOpen(&output, device, &format, 0, 0, CALLBACK_NULL);
  openResult = static_cast<int>(result);
  if (result != MMSYSERR_NOERROR) fail(result, "waveOutOpen");
  else opened = true;
  if (failure.empty()) {
    header.lpData = reinterpret_cast<LPSTR>(silence.data());
    header.dwBufferLength = static_cast<DWORD>(silence.size());
    result = waveOutPrepareHeader(output, &header, sizeof(header));
    prepareResult = static_cast<int>(result);
    if (result != MMSYSERR_NOERROR) fail(result, "waveOutPrepareHeader");
    else prepared = true;
  }
  if (failure.empty()) {
    result = waveOutWrite(output, &header, sizeof(header));
    writeResult = static_cast<int>(result);
    if (result != MMSYSERR_NOERROR) fail(result, "waveOutWrite");
    else written = true;
  }
  if (output) {
    resetCalled = true;
    result = waveOutReset(output);
    resetResult = static_cast<int>(result);
    resetOk = result == MMSYSERR_NOERROR;
    if (result != MMSYSERR_NOERROR) fail(result, "waveOutReset");
    if (prepared) {
      unprepareCalled = true;
      result = waveOutUnprepareHeader(output, &header, sizeof(header));
      unprepareResult = static_cast<int>(result);
      unprepareOk = result == MMSYSERR_NOERROR;
      if (result != MMSYSERR_NOERROR) fail(result, "waveOutUnprepareHeader");
    }
    closeCalled = true;
    result = waveOutClose(output);
    closeResult = static_cast<int>(result);
    closeOk = result == MMSYSERR_NOERROR;
    if (result != MMSYSERR_NOERROR) fail(result, "waveOutClose");
  }
  std::cout << "mode=explicit device=" << device << " device_count=" << deviceCount
            << " device_opened=" << (opened ? 1 : 0) << " playback_written=" << (written ? 1 : 0)
            << " prepared=" << (prepared ? 1 : 0)
            << " reset_called=" << (resetCalled ? 1 : 0) << " reset_ok=" << (resetOk ? 1 : 0)
            << " unprepare_called=" << (unprepareCalled ? 1 : 0) << " unprepare_ok=" << (unprepareOk ? 1 : 0)
            << " close_called=" << (closeCalled ? 1 : 0) << " close_ok=" << (closeOk ? 1 : 0)
            << " open_result=" << openResult << " prepare_result=" << prepareResult
            << " write_result=" << writeResult << " reset_result=" << resetResult
            << " unprepare_result=" << unprepareResult << " close_result=" << closeResult
            << " status=" << (failure.empty() ? "pass" : "failed") << "\n";
  if (!failure.empty()) std::cout << "error=" << failure << "\n";
  return failure.empty() ? 0 : 4;
}
