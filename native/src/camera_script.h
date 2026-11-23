#pragma once

// T76 stage 4 (2026-11-13): declarations for the T50 camera-script driver that
// used to sit inside main.cpp (main.cpp:742-1339 before the move).
//
// WHAT MOVED: the step struct, the thirteen state objects that describe a run
// (the script, its cursor, the mode/done flags, the counters, the pending
// capture and the exit code) and the five functions -- splitWords,
// loadCameraScript, cameraScriptReport, cameraSnapshot, runCameraScriptStep.
//
// WHAT DID NOT MOVE: the three call sites in wWinMain, which still spell
// loadCameraScript(cameraScriptPath), runCameraScriptStep() and
// cameraSnapshot(g_scriptCaptureLabel); every "camera: ..." line a gate reads;
// and the exit code path, which still returns g_cameraScriptExitCode before
// WM_QUIT's wParam. This is a move, not a rewrite: the driver is still TEST-ONLY
// (--camera-script is the only way in), and the default product run still takes
// none of it.
//
// WHY THE STATE IS EXTERN HERE: wWinMain wires the run up (it sets the mode and
// reads the counters for the summary) and the driver owns them, so the objects
// have to have a name in both translation units. They are DEFINED once, in
// src/camera_script.cpp.
//
// Not a public header: nothing in it is exported, no other target includes it,
// and no gate parses it.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tf2::native::main_internal {

struct CameraScriptStep {
  std::string command;
  std::vector<std::string> args;
  // The raw line, so the trace quotes what was asked for and not a
  // re-serialisation of it.
  std::string text;
};

extern std::vector<CameraScriptStep> g_cameraScript;
extern std::size_t g_cameraScriptIndex;
extern bool g_cameraScriptMode;
extern bool g_cameraScriptDone;
extern std::uint32_t g_cameraScriptStepsRun;
extern std::uint32_t g_cameraScriptStepsFailed;
extern bool g_scriptCapturePending;
extern std::string g_scriptCapturePath;
extern std::string g_scriptCaptureLabel;
extern std::uint32_t g_cameraCaptures;
extern std::uint32_t g_cameraScriptWaitFrames;
extern std::int32_t g_scriptTickMark;
extern int g_cameraScriptExitCode;

bool loadCameraScript(const std::filesystem::path& file);
bool runCameraScriptStep();
void cameraSnapshot(const std::string& label);

}  // namespace tf2::native::main_internal
