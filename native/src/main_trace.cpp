// T76 (2026-10-29): moved verbatim out of main.cpp's anonymous namespace
// (main.cpp:236-318 before the move). Only the surrounding namespace changed:
// the bodies, the comments and the trace text are the shipped bytes.

// TEMP DIAGNOSTIC -- entity-material wiring regression, 2026-10-09.
// The process leaves with 0xC0000409 a fraction of a second after the main loop
// starts advancing ticks. The metrics file's last line cannot say where, and the
// exception is a fast-fail, so no filter that runs after the fact can. These
// checkpoints can: every line is flushed as it is written, so the last line in
// the file names the phase the process died in. Nothing opens the file unless
// TF2_NATIVE_TRACE names one, so every other run is byte-for-byte unaffected.

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "main_internal.h"

namespace tf2::native::main_internal {
namespace {

std::FILE* g_traceFile = nullptr;
LARGE_INTEGER g_traceFrequency{};
LARGE_INTEGER g_traceOrigin{};

}  // namespace

double traceNowMs() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return 1000.0 * static_cast<double>(now.QuadPart - g_traceOrigin.QuadPart)
    / static_cast<double>(g_traceFrequency.QuadPart);
}

void traceCheckpoint(const char* what) {
  if (!g_traceFile) return;
  std::fprintf(g_traceFile, "%.1f %s\n", traceNowMs(), what);
  std::fflush(g_traceFile);
}

// The format string is NOT checked by the compiler. `_Printf_format_string_` was
// tried here and is inert for a user-defined variadic function: under /W4 a probe
// with `%d` fed a `const char*` produced no C4477, while the CRT's own printf did.
// Only /analyze acts on the annotation, and that is a project-wide flag rather than
// a local one. So a mismatch between this format and its arguments compiles
// silently -- one did: `%zu` fed a `uint32_t` printed uninitialised register bits as
// a bone count. Cast the argument to the type the conversion asks for, and do not
// trust the compiler to catch it.
void traceFmt(const char* format, ...) {
  if (!g_traceFile) return;
  // 192 was enough while every line was a handful of counters. A witness line
  // names a model path, and TF2 model paths run past 60 characters, so the
  // reading was being silently truncated mid-field -- the worst shape for a
  // diagnostic: it looks like it answered.
  //
  // T21: 512 then did the same thing to the per-face trace. That line carries
  // two VTF paths, a tile rectangle, an atlas uv, three sampled texels and four
  // depths; measured on pl_odyssey it needs 540-odd bytes and lost its tail
  // (`depthMatch`, `frameRaw`) without saying so, so a point was being compared
  // with no depth verdict and read as "this face". The size is now generous and
  // the truncation that remains is written down instead of hidden: a line that
  // cannot be trusted says it cannot be trusted.
  char buffer[2048];
  va_list args;
  va_start(args, format);
  const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (written < 0) return;
  if (static_cast<std::size_t>(written) >= sizeof(buffer)) {
    static const char kTail[] = "...[TRUNCATED]";
    std::memcpy(buffer + sizeof(buffer) - sizeof(kTail), kTail, sizeof(kTail));
  }
  traceCheckpoint(buffer);
}

void openTrace() {
  // Read through the Win32 call rather than getenv/fopen so the steady build
  // keeps its one pre-existing warning and no more: a diagnostic channel that
  // adds noise to every build is a diagnostic channel people turn off.
  wchar_t path[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"TF2_NATIVE_TRACE", path, MAX_PATH) == 0) return;
  if (_wfopen_s(&g_traceFile, path, L"w") != 0) return;
  if (!g_traceFile) return;
  QueryPerformanceFrequency(&g_traceFrequency);
  QueryPerformanceCounter(&g_traceOrigin);
  std::set_terminate([]() {
    traceCheckpoint("TERMINATE (a C++ exception left main)");
    std::abort();
  });
  // A fast-fail is not catchable, but a stack-cookie failure or a plain access
  // violation reaches this filter before the process dies, and the code is what
  // separates "our buffer was overrun" from "the heap was corrupted".
  SetUnhandledExceptionFilter([](LPEXCEPTION_POINTERS info) -> LONG {
    traceFmt("UNHANDLED 0x%08lX",
      static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode));
    return EXCEPTION_EXECUTE_HANDLER;
  });
  traceCheckpoint("trace open");
}

}  // namespace tf2::native::main_internal
