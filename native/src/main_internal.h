#pragma once

// T76 (2026-10-29): declarations for the code split out of main.cpp.
//
// WHY THIS HEADER EXISTS
//   main.cpp was 11,291 lines and every one of its helpers lived in one
//   anonymous namespace: internal linkage, no declaration anywhere, no way to
//   move a function out without re-typing its signature in the caller. T76 moves
//   the low-risk, self-contained pieces into their own translation units; the
//   declarations they need in order to stay callable from main.cpp are here.
//
// WHAT THIS IS NOT
//   Not a public header, not a contract, not included by any other target.
//   Nothing in it is exported and no gate parses it. The product's external
//   contract -- command line switches, trace text, exit codes -- is unchanged by
//   the move and is asserted by the gates, not by this file.
//
// WHY THE CALL SITES KEEP THEIR OLD SPELLING
//   main.cpp includes this header and adds one `using namespace
//   tf2::native::main_internal;` after its includes. That is the whole change on
//   the calling side: a call written `traceFmt("mode: summary ...")` compiles
//   exactly as before. Renaming every call site would have made the move
//   unprovable as pure motion and produced a larger diff than the code moved.

#include <filesystem>

#include "install_lifecycle.h"

namespace tf2::native::main_internal {

// ------------------------------------------------------------------- trace
// TEMP DIAGNOSTIC -- entity-material wiring regression, 2026-10-09 (moved out
// of main.cpp:236-318). Nothing opens the trace file unless TF2_NATIVE_TRACE
// names one, so every other run is byte-for-byte unaffected.
double traceNowMs();
void traceCheckpoint(const char* what);
void traceFmt(const char* format, ...);
void openTrace();

// ----------------------------------------------------- installer lifecycle
// T37 (moved out of main.cpp:72-151): four modes, not "a playback run with a
// flag": each answers a question and exits. They are recognised before the
// settings file is read and before the window is created, which is what makes
// --version usable from a script and keeps an uninstall from depending on a
// working GPU, a demo or a saved root.
enum class LifecycleMode { None, Version, Install, Uninstall, Status };

struct LifecycleRequest {
  LifecycleMode mode = LifecycleMode::None;
  tf2::native::installer::Options options;
  bool directoryGiven = false;
};

bool parseLifecycleRequest(int argumentCount, wchar_t** arguments, LifecycleRequest* request);
std::filesystem::path defaultInstallDirectory();
int runLifecycle(LifecycleRequest& request);

}  // namespace tf2::native::main_internal
