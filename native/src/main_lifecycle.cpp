// T76 (2026-10-29): moved verbatim out of main.cpp's first anonymous
// namespace (main.cpp:72-151 before the move). The two types (LifecycleMode,
// LifecycleRequest) are declared in main_internal.h; everything else here is
// the shipped text, comments included.

#include <Windows.h>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string>

#include "install_lifecycle.h"
#include "main_internal.h"

namespace tf2::native::main_internal {

// Four modes, not "a playback run with a flag": each answers a question and
// exits. They are recognised before the settings file is read and before the
// window is created, which is what makes --version usable from a script and
// keeps an uninstall from depending on a working GPU, a demo or a saved root.
bool parseLifecycleRequest(int argumentCount, wchar_t** arguments, LifecycleRequest* request) {
  for (int i = 1; i < argumentCount; ++i) {
    const wchar_t* argument = arguments[i];
    if (wcscmp(argument, L"--version") == 0) request->mode = LifecycleMode::Version;
    else if (wcscmp(argument, L"--install") == 0) request->mode = LifecycleMode::Install;
    else if (wcscmp(argument, L"--uninstall") == 0) request->mode = LifecycleMode::Uninstall;
    else if (wcscmp(argument, L"--install-status") == 0) request->mode = LifecycleMode::Status;
    else if (wcscmp(argument, L"--install-dry-run") == 0) request->options.dryRun = true;
    else if (wcscmp(argument, L"--install-dir") == 0 && i + 1 < argumentCount) {
      request->options.installDir = std::filesystem::absolute(std::filesystem::path(arguments[++i]));
      request->directoryGiven = true;
    }
  }
  // A DRY RUN IS A MODE, NOT AN OPTION THAT FLOATS ON ITS OWN.
  //   `--install-dry-run` used to set options.dryRun and nothing else. A
  //   command line that named only that flag therefore left mode == None, this
  //   function returned false, and the process fell through to the normal GUI
  //   start. Measured 2026-11-07: a real window opened, no install=DRYRUN
  //   line was ever printed, and the run ended by itself ~68 s later with code 1.
  //   The decision is made AFTER the whole argument list is read, so the flag's
  //   position cannot decide whether it is honoured.
  if (request->options.dryRun && request->mode == LifecycleMode::None) {
    request->mode = LifecycleMode::Install;
  }
  // `--uninstall --install-dry-run` (or --version, or --install-status) asks for
  // two different answers. Guessing one of them would make a script that passes
  // the wrong pair look like it worked; it is refused with exit 2 instead.
  if (request->options.dryRun && request->mode != LifecycleMode::None
      && request->mode != LifecycleMode::Install) {
    request->dryRunConflict = true;
    return true;
  }
  return request->mode != LifecycleMode::None;
}

// The default install directory is the executable's own, not the working
// directory: a user who double clicks the exe from an arbitrary folder must not
// install into that folder by accident.
std::filesystem::path defaultInstallDirectory() {
  wchar_t self[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, self, MAX_PATH) == 0) return {};
  return std::filesystem::path(self).parent_path();
}

// A REDIRECTED STDOUT IS THE CALLER'S AND IS NEVER REPLACED.
//   The first version of this attached to the parent console unconditionally
//   and then reopened CONOUT$ "to be safe". Under a shell redirect such as
//   "tf2_demo_native --version > out.txt" AttachConsole succeeds against the
//   shell's console, the reopen points stdout at the terminal, and the file the
//   caller redirected to stays 0 bytes while the reading appears on a terminal
//   nobody captured. Measured on 2026-10-20: 0 bytes in the file and exit 0 --
//   a silent, green-shaped lie, and the exact failure a gate that parses the
//   file would call "no output" or, worse, "passed". Only a process with no
//   usable stdout at all needs the console (a double click, where Windows hands
//   a GUI subsystem process an invalid handle), and only then is it attached,
//   used and freed.
int runLifecycle(LifecycleRequest& request) {
  tf2::native::installer::Result result;
  if (request.dryRunConflict) {
    const char* mode = request.mode == LifecycleMode::Uninstall ? "uninstall"
      : request.mode == LifecycleMode::Status ? "install-status"
      : request.mode == LifecycleMode::Version ? "version" : "unknown";
    result.lines.push_back(std::string("install=FAIL reason=dry-run-conflict mode=") + mode
      + " detail=--install-dry-run only modifies --install");
    result.exitCode = 2;
  } else {
    if (!request.directoryGiven) request.options.installDir = defaultInstallDirectory();
    switch (request.mode) {
      case LifecycleMode::Version: result = tf2::native::installer::version(request.options); break;
      case LifecycleMode::Install: result = tf2::native::installer::install(request.options); break;
      case LifecycleMode::Uninstall: result = tf2::native::installer::uninstall(request.options); break;
      case LifecycleMode::Status:
      default: result = tf2::native::installer::status(request.options); break;
    }
  }
  const HANDLE given = GetStdHandle(STD_OUTPUT_HANDLE);
  const bool noStdout = given == nullptr || given == INVALID_HANDLE_VALUE;
  bool attached = false;
  std::FILE* sink = stdout;
  if (noStdout) {
    attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    if (attached) {
      std::FILE* reopened = nullptr;
      if (_wfreopen_s(&reopened, L"CONOUT$", L"w", stdout) == 0 && reopened != nullptr) sink = reopened;
    }
  }
  for (const auto& line : result.lines) {
    std::fprintf(sink, "%s\n", line.c_str());
    std::fflush(sink);
  }
  const int code = result.exitCode;
  if (attached) FreeConsole();
  return code;
}

}  // namespace tf2::native::main_internal
