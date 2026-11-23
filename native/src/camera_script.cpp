// T76 stage 4 (2026-11-13): the T50 camera-script driver, moved out of main.cpp
// as a pure move. main.cpp:742-1339 became this file; the only edits inside the
// moved text are the ones a new translation unit needs: the step struct's
// definition lives in camera_script.h (it was the same text), and the bodies
// below are byte-for-byte the ones that were in main.cpp.
//
// WHY IT MOVED: 598 lines of a test-only driver were the largest contiguous
// block in main.cpp with no renderer, input or demo-decoding responsibility. The
// seam is state-heavy -- the driver reads the playback view, the camera director,
// the bookmark store, the renderer pointer, the key table, the pause controller,
// the review session and the hotkey dispatchers -- which is why those seven
// objects are declared in main_camera.h / playback_state.h / main_internal.h
// rather than reached through globals that only one file can name.
//
// WHAT A GATE CAN STILL SEE: every reading is emitted by the same traceFmt text
// as before, the three call sites in main.cpp are unchanged, and the exit code
// still comes from g_cameraScriptExitCode. No gate patches text inside this
// file; the mutations that patch main.cpp (build-identity's dispatchHotkey
// anchor, hud-check's registerKeybinds line, camera-ui's main-loop condition)
// stay in main.cpp and are untouched by the move.

#include "camera_script.h"

#include "main_internal.h"
#include "main_camera.h"
#include "playback_state.h"
#include "native_renderer.h"
#include "keybinds.h"
#include "native_ui.h"
#include "review_session.h"
#include "settings_store.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace tf2::native::main_internal {

std::vector<CameraScriptStep> g_cameraScript;
std::size_t g_cameraScriptIndex = 0;
bool g_cameraScriptMode = false;
bool g_cameraScriptDone = false;
std::uint32_t g_cameraScriptStepsRun = 0;
std::uint32_t g_cameraScriptStepsFailed = 0;
// A capture step waits for the frame it asked for; the loop must not quit on it
// the way the one-shot --capture-frame path does.
bool g_scriptCapturePending = false;
std::string g_scriptCapturePath;
std::string g_scriptCaptureLabel;
std::uint32_t g_cameraCaptures = 0;
// Frames to wait before the next step, for steps whose effect lands on a later
// frame (a capture, a scene-tick camera application).
std::uint32_t g_cameraScriptWaitFrames = 0;
// The tick a `mark-tick` step recorded, for `expect-same-tick` to compare against.
std::int32_t g_scriptTickMark = -1;
// The exit code a `quit` step asked for; -1 means "no script ran".
int g_cameraScriptExitCode = -1;

std::vector<std::string> splitWords(const std::string& line) {
  std::vector<std::string> words;
  std::size_t index = 0;
  while (index < line.size()) {
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) ++index;
    if (index >= line.size()) break;
    const std::size_t start = index;
    while (index < line.size() && line[index] != ' ' && line[index] != '\t') ++index;
    words.push_back(line.substr(start, index - start));
  }
  return words;
}

bool loadCameraScript(const std::filesystem::path& file) {
  g_cameraScript.clear();
  std::ifstream input(file);
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    auto words = splitWords(line);
    if (words.empty()) continue;
    CameraScriptStep step;
    step.text = line;
    step.command = words.front();
    step.args.assign(words.begin() + 1, words.end());
    g_cameraScript.push_back(std::move(step));
  }
  return !g_cameraScript.empty();
}

// Every step reports through this pair so a failed step is a reading, not a
// silent skip: `camera: accept` carries the step index, the command and a
// one-word result.
void cameraScriptReport(std::size_t index, const std::string& command,
    const std::string& result, const std::string& detail) {
  traceFmt("camera: accept step=%zu cmd=%s result=%s detail=%s", index,
    command.c_str(), result.c_str(), detail.empty() ? "-" : detail.c_str());
}

// Parses a pose out of the renderer and reports it. `label` names the moment;
// the gate compares two labels' numbers, which is what makes "the camera moved
// while the tick did not" a measurement rather than a claim.
void cameraSnapshot(const std::string& label) {
  if (!g_renderer) {
    traceFmt("camera: snapshot label=%s valid=0", label.c_str());
    return;
  }
  const auto reading = g_renderer->orbitPose();
  if (!reading.valid) {
    traceFmt("camera: snapshot label=%s valid=0 tick=%d", label.c_str(),
      static_cast<int>(g_playback.tick));
    return;
  }
  tf2::native::CameraPose pose;
  pose.focusX = reading.focusX; pose.focusY = reading.focusY; pose.focusZ = reading.focusZ;
  pose.yaw = reading.yaw; pose.pitch = reading.pitch; pose.distance = reading.distance;
  pose.valid = true;
  traceFmt("camera: snapshot label=%s valid=1 tick=%d paused=%d mode=%s demoView=%d fov=%.6f %s",
    label.c_str(), static_cast<int>(g_playback.tick), g_playback.paused ? 1 : 0,
    cameraModeToken(g_cameraMode.mode()), reading.demoView ? 1 : 0,
    static_cast<double>(reading.fovDegrees), tf2::native::poseTraceFields(pose).c_str());
}

// Runs the pending script step. Returns true when a step ran (the caller then
// continues the loop without doing anything else this iteration), so the
// sequence of steps is deterministic even when the machine is loaded.
bool runCameraScriptStep() {
  using namespace tf2::native;
  if (!g_cameraScriptMode || g_cameraScriptIndex >= g_cameraScript.size()) return false;
  if (g_cameraScriptWaitFrames > 0) { --g_cameraScriptWaitFrames; return true; }
  if (g_scriptCapturePending) return true;   // the capture owns the loop
  const std::size_t index = g_cameraScriptIndex;
  const CameraScriptStep& step = g_cameraScript[index];
  ++g_cameraScriptIndex;
  ++g_cameraScriptStepsRun;
  const auto fail = [&](const std::string& detail) {
    ++g_cameraScriptStepsFailed;
    cameraScriptReport(index, step.command, "fail", detail);
    return true;
  };
  const auto ok = [&](const std::string& detail) {
    cameraScriptReport(index, step.command, "ok", detail);
    return true;
  };
  const auto number = [&](std::size_t position, double& value) {
    if (position >= step.args.size()) return false;
    char* end = nullptr;
    value = std::strtod(step.args[position].c_str(), &end);
    return end && *end == '\0';
  };
  if (step.command == "tick") {
    double wanted = 0.0;
    if (!number(0, wanted)) return fail("bad-argument");
    // Waiting is not failing: the step stays unexecuted until the tick arrives.
    // The execution counter is rolled back with the index, or a long wait would
    // report dozens of executed steps that never ran and the gate's
    // "steps executed" assertion would count iterations instead of steps.
    if (g_playback.tick < static_cast<std::int32_t>(wanted)) {
      --g_cameraScriptIndex;
      --g_cameraScriptStepsRun;
      return true;
    }
    return ok("tick=" + std::to_string(g_playback.tick));
  }
  if (step.command == "frames") {
    double count = 0.0;
    if (!number(0, count)) return fail("bad-argument");
    g_cameraScriptWaitFrames = static_cast<std::uint32_t>(count);
    return ok("wait=" + std::to_string(g_cameraScriptWaitFrames));
  }
  if (step.command == "pause" || step.command == "resume") {
    const bool paused = step.command == "pause";
    tf2::native::PlaybackRequest request;
    request.kind = tf2::native::PlaybackRequestKind::SetPaused;
    request.paused = paused;
    request.source = "camera-script";
    applyPlaybackRequest(request);
    g_pauseKey.paused = paused;
    g_cameraDirector.setPaused(paused);
    return ok(paused ? "paused" : "playing");
  }
  if (step.command == "orbit") {
    double dx = 0.0, dy = 0.0;
    if (!number(0, dx) || !number(1, dy)) return fail("bad-argument");
    // Through the renderer, which is what the product does for a drag: the
    // director then reads the pose back, so both halves agree.
    if (!g_renderer) return fail("no-renderer");
    g_renderer->orbitCamera(static_cast<float>(dx), static_cast<float>(dy));
    syncRendererPoseToDirector();
    return ok("moved");
  }
  if (step.command == "zoom") {
    double delta = 0.0;
    if (!number(0, delta)) return fail("bad-argument");
    if (!g_renderer) return fail("no-renderer");
    g_renderer->zoomCamera(static_cast<float>(delta));
    syncRendererPoseToDirector();
    return ok("zoomed");
  }
  if (step.command == "save") {
    double slot = 0.0;
    if (!number(0, slot)) return fail("bad-argument");
    std::string name;
    for (std::size_t position = 1; position < step.args.size(); ++position) {
      if (!name.empty()) name += " ";
      name += step.args[position];
    }
    const bool saved = saveCameraBookmark(static_cast<std::size_t>(slot), name);
    traceFmt("camera: bookmark action=save slot=%d name=%s ok=%d",
      static_cast<int>(slot), name.empty() ? "-" : name.c_str(), saved ? 1 : 0);
    return saved ? ok("slot=" + std::to_string(static_cast<int>(slot))) : fail("refused");
  }
  if (step.command == "jump") {
    double slot = 0.0;
    if (!number(0, slot)) return fail("bad-argument");
    std::string reason;
    const bool jumped = jumpToCameraBookmark(static_cast<std::size_t>(slot), reason);
    const auto pose = g_cameraDirector.pose();
    traceFmt("camera: bookmark action=jump slot=%d ok=%d reason=%s %s",
      static_cast<int>(slot), jumped ? 1 : 0, reason.c_str(),
      pose.valid ? tf2::native::poseTraceFields(pose).c_str() : "focus=-");
    return jumped ? ok(reason) : fail(reason);
  }
  if (step.command == "delete") {
    double slot = 0.0;
    if (!number(0, slot)) return fail("bad-argument");
    const bool removed = g_cameraBookmarks.remove(static_cast<std::size_t>(slot));
    if (removed && g_renderer) g_renderer->clearCameraPreset(static_cast<std::size_t>(slot) - 1);
    traceFmt("camera: bookmark action=delete slot=%d ok=%d",
      static_cast<int>(slot), removed ? 1 : 0);
    return removed ? ok("deleted") : fail("empty");
  }
  if (step.command == "reset-view") {
    g_cameraDirector.resetView();
    syncDirectorPoseToRenderer(CameraMoveSource::Reset);
    return ok("reset-view");
  }
  if (step.command == "reset-to-player") {
    std::string reason;
    handleCameraAction(camera_actions::kResetToPlayer, reason);
    return ok(reason);
  }
  if (step.command == "freefly") {
    const bool on = !step.args.empty() && step.args[0] == "on";
    const std::string action = camera_actions::kFreeFly;
    std::string reason;
    handleCameraAction(action, reason);
    if (on != g_cameraDirector.freeFly()) {
      // The action toggles; a script that asks for a state gets that state.
      handleCameraAction(action, reason);
    }
    // T93-B: the step reports the state it LEFT the product in, not the state it
    // asked for. T85 read `result=ok detail=on` on a run whose freeFlySteps was
    // 0 and whose every drawn frame said mode=first-person: "on" was a promise
    // about a toggle and nothing checked it. The detail now carries the mode the
    // product is actually in, and a state the step cannot reach fails the step.
    if (on != g_cameraDirector.freeFly()) return fail("unreachable");
    return ok(std::string(g_cameraDirector.freeFly() ? "on" : "off")
      + " mode=" + tf2::native::cameraModeName(g_cameraMode.mode()));
  }
  // I1 (2026-11-23): the mouse click's player switch, scriptable so a gate can
  // drive it without synthesising input. Same function the click calls.
  if (step.command == "switch-player") {
    if (step.args.empty() || (step.args[0] != "next" && step.args[0] != "prev")) return fail("bad-argument");
    std::string reason;
    const bool accepted = requestPlayerSwitch(step.args[0] == "next", "script", &reason);
    return accepted ? ok(reason) : fail(reason);
  }
  if (step.command == "snapshot") {
    cameraSnapshot(step.args.empty() ? "unnamed" : step.args[0]);
    return ok(step.args.empty() ? "unnamed" : step.args[0]);
  }
  if (step.command == "verify-bookmark") {
    // The exactness assertion, inside the product. Compares the renderer's own
    // pose with the bookmarked one through the same poseEqual a save/jump round
    // trip uses, so "the pose came back" is exact rather than to 6 decimals on a
    // trace line. A jump that lands 1e-3 away reports match=0.
    double slot = 0.0;
    if (!number(0, slot)) return fail("bad-argument");
    const std::size_t wanted = static_cast<std::size_t>(slot);
    if (!g_cameraBookmarks.saved(wanted) || !g_renderer) return fail("no-bookmark");
    const auto reading = g_renderer->orbitPose();
    if (!reading.valid) return fail("no-pose");
    tf2::native::CameraPose current;
    current.focusX = reading.focusX; current.focusY = reading.focusY;
    current.focusZ = reading.focusZ; current.yaw = reading.yaw;
    current.pitch = reading.pitch; current.distance = reading.distance;
    current.valid = true;
    const auto& saved = g_cameraBookmarks.at(wanted);
    // Two separate readings, because the mutation harness proves each one can go
    // red on its own: the six orbit fields, and the projection the frame is
    // drawn through. A jump that restores the orbit into the wrong fov is a
    // different picture even though the pose numbers match.
    const bool match = tf2::native::poseEqual(current, saved.pose);
    const bool fovMatch = reading.fovDegrees == saved.fov;
    traceFmt("camera: verify kind=bookmark slot=%d match=%d fovMatch=%d fovWant=%s fovGot=%s want=%s got=%s",
      static_cast<int>(wanted), match ? 1 : 0, fovMatch ? 1 : 0,
      tf2::native::formatPoseField(saved.fov).c_str(),
      tf2::native::formatPoseField(reading.fovDegrees).c_str(),
      tf2::native::formatPose(saved.pose).c_str(), tf2::native::formatPose(current).c_str());
    return match && fovMatch ? ok("exact") : fail(match ? "fov-mismatch" : "pose-mismatch");
  }
  if (step.command == "verify-name") {
    double slot = 0.0;
    if (!number(0, slot)) return fail("bad-argument");
    std::string wanted;
    for (std::size_t position = 1; position < step.args.size(); ++position) {
      if (!wanted.empty()) wanted += " ";
      wanted += step.args[position];
    }
    const auto& bookmark = g_cameraBookmarks.at(static_cast<std::size_t>(slot));
    const bool match = bookmark.saved && bookmark.name == wanted;
    traceFmt("camera: verify kind=name slot=%d match=%d name=%s",
      static_cast<int>(slot), match ? 1 : 0, bookmark.name.c_str());
    return match ? ok("named") : fail("name-mismatch");
  }
  if (step.command == "capture") {
    if (!g_renderer || step.args.empty()) return fail("bad-argument");
    g_scriptCapturePath = step.args[0];
    g_scriptCaptureLabel = step.args.size() > 1 ? step.args[1] : step.args[0];
    g_scriptCapturePending = true;
    // The capture is armed here and requested on the next drawn frame; the
    // `camera: capture` line then names the tick it landed on. The step still
    // reports, so "the step was executed" and "the frame was written" stay two
    // separate readings.
    return ok("armed=" + g_scriptCaptureLabel);
  }
  if (step.command == "key") {
    double vk = 0.0;
    if (!number(0, vk)) return fail("bad-argument");
    bool ctrl = false, alt = false, shift = false;
    for (std::size_t position = 1; position < step.args.size(); ++position) {
      if (step.args[position] == "ctrl") ctrl = true;
      else if (step.args[position] == "alt") alt = true;
      else if (step.args[position] == "shift") shift = true;
    }
    std::string action;
    std::string result;
    dispatchHotkey(static_cast<unsigned>(vk), ctrl, alt, shift, false, action, result);
    traceFmt("camera: keys vk=%d ctrl=%d alt=%d shift=%d action=%s result=%s",
      static_cast<int>(vk), ctrl ? 1 : 0, alt ? 1 : 0, shift ? 1 : 0,
      action.empty() ? "-" : action.c_str(), result.c_str());
    return ok(result);
  }
  if (step.command == "conflict") {
    // Binds `action` to `chord` and reports whether the table accepted it.
    // This is the step the gate uses to prove conflict detection is real: the
    // answer must be "refused" and the action must keep its own key.
    if (step.args.size() < 2) return fail("bad-argument");
    const auto chord = tf2::native::parseChord(step.args[1]);
    const bool accepted = g_keyBinds.set(step.args[0], chord);
    traceFmt("camera: keys action=%s chord=%s result=%s current=%s",
      step.args[0].c_str(), tf2::native::formatChord(chord).c_str(),
      accepted ? "accepted" : "conflict",
      tf2::native::formatChord(g_keyBinds.actionKey(step.args[0])).c_str());
    return ok(accepted ? "accepted" : "conflict");
  }
  if (step.command == "bind") {
    if (step.args.size() < 2) return fail("bad-argument");
    const auto chord = tf2::native::parseChord(step.args[1]);
    const bool accepted = g_keyBinds.set(step.args[0], chord);
    traceFmt("camera: keys action=%s chord=%s result=%s current=%s",
      step.args[0].c_str(), tf2::native::formatChord(chord).c_str(),
      accepted ? "accepted" : "conflict",
      tf2::native::formatChord(g_keyBinds.actionKey(step.args[0])).c_str());
    return ok(accepted ? "accepted" : "conflict");
  }
  if (step.command == "unbind") {
    if (step.args.empty()) return fail("bad-argument");
    g_keyBinds.unbind(step.args[0]);
    traceFmt("camera: keys action=%s result=unbound", step.args[0].c_str());
    return ok("unbound");
  }
  if (step.command == "reset-keys") {
    g_keyBinds.resetAll();
    traceFmt("camera: keys action=* result=reset-all resets=%u",
      g_keyBinds.resetCount());
    return ok("reset-all");
  }
  if (step.command == "focus-text") {
    g_textFieldFocused = !step.args.empty() && step.args[0] == "on";
    return ok(g_textFieldFocused ? "on" : "off");
  }
  if (step.command == "director") {
    traceFmt("camera: director frozen=%u advanced=%u violations=%u tick=%d"
      " moves=%u movesWhilePaused=%u freeFlySteps=%u bookmarkApplies=%u"
      " resetToPlayer=%u/%u resetView=%u focusSuppressed=%u",
      g_cameraDirector.frozenDemoFrames(), g_cameraDirector.advancedDemoFrames(),
      g_cameraDirector.pauseViolations(), static_cast<int>(g_cameraDirector.lastTick()),
      g_cameraDirector.cameraMoves(), g_cameraDirector.cameraMovesWhilePaused(),
      g_cameraDirector.freeFlySteps(), g_cameraDirector.bookmarkApplies(),
      g_cameraDirector.resetToPlayerRequests(), g_cameraDirector.resetToPlayerApplies(),
      g_cameraDirector.resetViewCount(), g_hotkeySuppressedByFocus);
    return ok("reported");
  }
  if (step.command == "expect-tick") {
    double wanted = -1.0;
    if (!number(0, wanted)) return fail("bad-argument");
    traceFmt("camera: expect what=tick expected=%d actual=%d match=%d",
      static_cast<int>(wanted), static_cast<int>(g_playback.tick),
      static_cast<int>(wanted) == g_playback.tick ? 1 : 0);
    return static_cast<std::int32_t>(wanted) == g_playback.tick
      ? ok("match") : fail("mismatch");
  }
  if (step.command == "mark-tick") {
    g_scriptTickMark = g_playback.tick;
    traceFmt("camera: mark tick=%d", static_cast<int>(g_scriptTickMark));
    return ok("marked");
  }
  if (step.command == "expect-same-tick") {
    // "the tick is exactly where it was when the mark was taken". Exact rather
    // than "did not move much": the pause contract is that the cursor stands
    // still, and a gate that accepted a wobble would accept a broken freeze.
    const bool same = g_playback.tick == g_scriptTickMark;
    traceFmt("camera: expect what=same-tick marked=%d actual=%d match=%d",
      static_cast<int>(g_scriptTickMark), static_cast<int>(g_playback.tick),
      same ? 1 : 0);
    return same ? ok("frozen") : fail("tick-moved");
  }
  if (step.command == "expect-tick-gt") {
    // "the tick advanced past N". Separate from expect-tick because the
    // resume half of the contract is a claim about direction, not about a
    // particular value: the loop is free to draw several ticks between two
    // steps and pinning an exact number would make the gate a timing test.
    double wanted = -1.0;
    if (!number(0, wanted)) return fail("bad-argument");
    const bool advanced = g_cameraDirector.lastTick() > static_cast<std::int32_t>(wanted);
    traceFmt("camera: expect what=tick-gt expected=%d actual=%d match=%d",
      static_cast<int>(wanted), static_cast<int>(g_cameraDirector.lastTick()),
      advanced ? 1 : 0);
    return advanced ? ok("advanced") : fail("not-advanced");
  }
  if (step.command == "save-settings") {
    const bool bookmarks = g_cameraBookmarks.saveToSettings(tf2::native::settingsPath());
    const bool keys = g_keyBinds.saveToSettings(tf2::native::settingsPath());
    traceFmt("camera: settings bookmarks=%d keys=%d path=%s", bookmarks ? 1 : 0,
      keys ? 1 : 0, tf2::native::settingsPath().u8string().c_str());
    return bookmarks && keys ? ok("saved") : fail("refused");
  }
  if (step.command == "load-settings") {
    const bool bookmarks = g_cameraBookmarks.loadFromSettings(tf2::native::settingsPath());
    const bool keys = g_keyBinds.loadFromSettings(tf2::native::settingsPath());
    traceFmt("camera: settings bookmarks=%d keys=%d loaded=1 slots=%zu",
      bookmarks ? 1 : 0, keys ? 1 : 0, g_cameraBookmarks.savedCount());
    return ok("loaded");
  }
  // T70: the scripted transport driver. Every command goes through the same
  // request() the keyboard uses, so a scripted acceptance run exercises the
  // production path rather than a test-only copy.
  if (step.command == "step") {
    double delta = 0.0;
    if (!number(0, delta)) return fail("bad-argument");
    tf2::native::PlaybackRequest request;
    request.kind = tf2::native::PlaybackRequestKind::StepTicks;
    request.ticks = static_cast<std::int32_t>(delta);
    request.source = "camera-script";
    applyPlaybackRequest(request);
    return ok("tick=" + std::to_string(g_playback.tick));
  }
  if (step.command == "jump-seconds") {
    double seconds = 0.0;
    if (!number(0, seconds)) return fail("bad-argument");
    tf2::native::PlaybackRequest request;
    request.kind = tf2::native::PlaybackRequestKind::JumpSeconds;
    request.seconds = seconds;
    request.source = "camera-script";
    applyPlaybackRequest(request);
    return ok("tick=" + std::to_string(g_playback.tick));
  }
  if (step.command == "seek") {
    double tick = 0.0;
    if (!number(0, tick)) return fail("bad-argument");
    tf2::native::PlaybackRequest request;
    request.kind = tf2::native::PlaybackRequestKind::JumpTicks;
    request.tick = static_cast<std::int32_t>(tick);
    request.source = "camera-script";
    applyPlaybackRequest(request);
    return ok("tick=" + std::to_string(g_playback.tick));
  }
  if (step.command == "speed") {
    double speed = 0.0;
    if (!number(0, speed)) return fail("bad-argument");
    tf2::native::PlaybackRequest request;
    request.kind = tf2::native::PlaybackRequestKind::SetSpeed;
    request.speed = speed;
    request.source = "camera-script";
    applyPlaybackRequest(request);
    return ok("speed=" + std::to_string(g_playback.speed));
  }
  if (step.command == "playback-state") {
    const tf2::native::PlaybackView view = g_playbackControl.view();
    const tf2::native::PlaybackReadings readings = g_playbackControl.readings();
    traceFmt("playback: state phase=%s cursor=%d end=%d paused=%d reverse=%d speed=%.6f"
      " rate=%.6f firstPacketTick=%d origin=%d pending=%d lastRequest=%d lastStatus=%s"
      " lastFrameRequest=%d boundary=%d requests=%llu landings=%llu rejected=%llu",
      tf2::native::PlaybackControl::phaseName(view.phase),
      static_cast<int>(view.cursorTick), static_cast<int>(view.endTick),
      view.paused ? 1 : 0, view.reverse ? 1 : 0, view.speed, view.tickRate,
      static_cast<int>(view.firstPacketTick), view.firstPacketOriginKnown ? 1 : 0,
      static_cast<int>(view.pendingRequestId), static_cast<int>(view.lastRequestId),
      tf2::native::PlaybackControl::statusName(view.lastStatus),
      static_cast<int>(view.lastFrameRequest), view.boundary ? 1 : 0,
      static_cast<unsigned long long>(readings.requests),
      static_cast<unsigned long long>(readings.landings),
      static_cast<unsigned long long>(readings.rejected));
    return ok("cursor=" + std::to_string(view.cursorTick));
  }
  if (step.command == "save-bookmark") {
    // A T50 numbered slot, saved the way the key handler saves it, so the
    // workspace carries more than the camera the user is standing in.
    if (step.args.empty()) return fail("save-bookmark needs a slot");
    char* slotEnd = nullptr;
    const unsigned long long slotValue = std::strtoull(step.args[0].c_str(), &slotEnd, 10);
    if (!slotEnd || *slotEnd != 0 || slotValue < 1) return fail("bad-slot");
    std::string slotName;
    for (std::size_t k = 1; k < step.args.size(); ++k) {
      if (!slotName.empty()) slotName.push_back(32);
      slotName += step.args[k];
    }
    if (!saveCameraBookmark(static_cast<std::size_t>(slotValue), slotName)) {
      return fail("bookmark-refused");
    }
    return ok("slot=" + std::to_string(slotValue));
  }
  // T71/T72: the review workspace and its bookmarks, drivable from the same
  // script the camera gates use. Nothing here has a shortcut into the stores: a
  // step calls the session, the session calls the store, and the store writes
  // the file, so "the product saved a workspace" is a file on disk after the run.
  if (step.command == "workspace-report") {
    if (!g_reviewSession) return fail("no-workspace");
    g_reviewSession->traceState();
    return ok("reported");
  }
  if (step.command == "workspace-apply") {
    if (!g_reviewSession) return fail("no-workspace");
    std::string detail;
    return g_reviewSession->applyPlayback(detail) ? ok(detail) : fail(detail);
  }
  if (step.command == "workspace-save") {
    if (!g_reviewSession) return fail("no-workspace");
    // force writes even when nothing changed; the default is an autosave, the
    // same call the process makes on the way out.
    const bool force = !step.args.empty() && step.args.front() == "force";
    std::string detail;
    return g_reviewSession->save(force, detail) ? ok(detail) : fail(detail);
  }
  const auto bookmarkId = [&](std::size_t position, std::uint64_t& value) {
    if (position >= step.args.size()) return false;
    char* end = nullptr;
    value = std::strtoull(step.args[position].c_str(), &end, 10);
    return end && *end == '\0' && value > 0;
  };
  const auto restFrom = [&](std::size_t position) {
    std::string joined;
    for (std::size_t k = position; k < step.args.size(); ++k) {
      if (!joined.empty()) joined.push_back(' ');
      joined += step.args[k];
    }
    return joined;
  };
  if (step.command == "bookmark-add") {
    if (!g_reviewSession) return fail("no-workspace");
    if (step.args.empty()) return fail("bad-argument");
    std::string detail;
    const bool added = g_reviewSession->addManual(g_playback.tick, step.args.front(),
      restFrom(1), detail);
    return added ? ok(detail) : fail(detail);
  }
  if (step.command == "bookmark-add-event") {
    if (!g_reviewSession) return fail("no-workspace");
    double wanted = 0.0;
    if (!number(0, wanted)) return fail("bad-argument");
    const std::string title = step.args.size() > 1 ? step.args[1] : std::string("event");
    std::string detail;
    const bool added = g_reviewSession->addEvent(static_cast<std::int32_t>(wanted), title,
      restFrom(2), detail);
    return added ? ok(detail) : fail(detail);
  }
  if (step.command == "bookmark-list") {
    if (!g_reviewSession) return fail("no-workspace");
    tf2::native::ReviewSort sort = tf2::native::ReviewSort::Tick;
    if (!step.args.empty()) {
      const std::string& name = step.args.front();
      if (name == "created") sort = tf2::native::ReviewSort::Created;
      else if (name == "title") sort = tf2::native::ReviewSort::Title;
      else if (name != "tick") return fail("bad-argument");
    }
    g_reviewSession->list(sort, nullptr, step.args.empty() ? "all" : step.args.front());
    return ok("listed");
  }
  if (step.command == "bookmark-query") {
    if (!g_reviewSession) return fail("no-workspace");
    tf2::native::ReviewQuery query;
    query.text = restFrom(0);
    g_reviewSession->list(tf2::native::ReviewSort::Tick, &query, "query");
    return ok("queried");
  }
  if (step.command == "bookmark-update") {
    if (!g_reviewSession) return fail("no-workspace");
    std::uint64_t id = 0;
    if (!bookmarkId(0, id)) return fail("bad-argument");
    std::string detail;
    const bool updated = g_reviewSession->update(id,
      step.args.size() > 1 ? step.args[1] : std::string(), restFrom(2), detail);
    return updated ? ok(detail) : fail(detail);
  }
  if (step.command == "bookmark-remove") {
    if (!g_reviewSession) return fail("no-workspace");
    std::uint64_t id = 0;
    if (!bookmarkId(0, id)) return fail("bad-argument");
    std::string detail;
    return g_reviewSession->erase(id, detail) ? ok(detail) : fail(detail);
  }
  if (step.command == "bookmark-jump") {
    if (!g_reviewSession) return fail("no-workspace");
    std::uint64_t id = 0;
    if (!bookmarkId(0, id)) return fail("bad-argument");
    std::string detail;
    return g_reviewSession->jump(id, detail) ? ok(detail) : fail(detail);
  }

  if (step.command == "quit") {
    traceFmt("camera: accept steps=%u failures=%u captures=%u",
      g_cameraScriptStepsRun, g_cameraScriptStepsFailed, g_cameraCaptures);
    // The exit code is recorded here rather than read back from WM_QUIT: the
    // main loop leaves through `break` on g_cameraScriptDone, so the queued quit
    // message is never the thing that ends the process and message.wParam would
    // still hold whatever was last dispatched. A gate reading a wrong exit code
    // as "the run passed" is the failure this avoids.
    g_cameraScriptExitCode = g_cameraScriptStepsFailed == 0 ? 0 : 1;
    g_cameraScriptDone = true;
    return true;
  }
  return fail("unknown-command");
}
}  // namespace tf2::native::main_internal
