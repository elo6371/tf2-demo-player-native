// T70 playback_control_probe -- pure-logic driver for PlaybackControl.
//
// Two modes:
//   --playback-test                     the scripted A-F suite; one step= line per
//                                       request, plus advance/interrupt/readings lines
//   --demo PATH --command NAME [...]    one command against a synthetic demo
//                                       (--ticks N) or a named missing demo
//
// The step= line is the gate's input:
//   step=<n> cmd=<name> requested=<t> landed=<t> status=<ok|clamped|unreachable|rejected>
//     playing=<0|1> speed=<f>
// status maps the request status onto the four gate words; the precise status
// and reason are printed on a following "detail=" line when they differ.
#include "playback_control.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace tf2::native;

namespace {

int g_step = 0;
int g_checks = 0;
int g_failures = 0;

const char* gateStatus(PlaybackRequestStatus status) {
  switch (status) {
    case PlaybackRequestStatus::Applied:
    case PlaybackRequestStatus::SameTick:
      return "ok";
    case PlaybackRequestStatus::ClampedStart:
    case PlaybackRequestStatus::ClampedEnd:
    case PlaybackRequestStatus::ClampedSpeed:
      return "clamped";
    case PlaybackRequestStatus::RefusedOutOfRange:
      return "unreachable";
    default:
      return "rejected";
  }
}

void check(bool condition, const char* label, const std::string& detail) {
  ++g_checks;
  if (!condition) ++g_failures;
  std::printf("check=%s ok=%d %s\n", label, condition ? 1 : 0, detail.c_str());
}

void printStep(const PlaybackControl& control, const PlaybackResult& result, const char* command) {
  ++g_step;
  const PlaybackView view = control.view();
  std::printf("step=%d cmd=%s requested=%d landed=%d status=%s playing=%d speed=%.3f\n",
    g_step, command, result.requestedTick, result.appliedTick, gateStatus(result.status),
    view.paused ? 0 : 1, view.speed);
  if (result.status != PlaybackRequestStatus::Applied
      && result.status != PlaybackRequestStatus::SameTick) {
    std::printf("detail=step=%d status=%s reason=%s id=%d\n", g_step,
      PlaybackControl::statusName(result.status), result.reason, result.id);
  }
}

// Resolve the pending request against an exact entity snapshot. The synthetic
// demo has firstPacketTick=100, so playback cursor C is scene tick 100+C.
void settle(PlaybackControl& control, SceneQueryStatus status = SceneQueryStatus::Available) {
  const std::int32_t id = control.pendingRequestId();
  if (id == 0) return;
  const PlaybackView view = control.view();
  const std::int32_t sceneTick = view.firstPacketTick + view.cursorTick;
  const std::int32_t resolved = status == SceneQueryStatus::Available
    ? sceneTick : sceneTick - 3;
  control.noteSceneResolved(id, status, sceneTick, resolved, sceneTick, true);
}

PlaybackRequest moveRequest(PlaybackRequestKind kind, std::int32_t tick, std::int32_t ticks,
    double seconds, bool strict) {
  PlaybackRequest request;
  request.kind = kind;
  request.tick = tick;
  request.ticks = ticks;
  request.seconds = seconds;
  request.strict = strict;
  return request;
}

void runSuite() {
  constexpr std::int32_t kEndTick = 1000;
  constexpr double kRate = 66.6666667;

  // ---- A: frame stepping, ten forward and ten back, plus both boundaries.
  {
    PlaybackControl control;
    control.setDemo(kEndTick, kRate, 100, true, true);
    PlaybackResult result = control.request(moveRequest(PlaybackRequestKind::JumpTicks, 100, 0, 0.0, false));
    printStep(control, result, "seek");
    settle(control);
    for (int i = 0; i < 10; ++i) {
      result = control.request(moveRequest(PlaybackRequestKind::StepTicks, 0, 1, 0.0, false));
      printStep(control, result, "step-forward");
      settle(control);
    }
    for (int i = 0; i < 10; ++i) {
      result = control.request(moveRequest(PlaybackRequestKind::StepTicks, 0, -1, 0.0, false));
      printStep(control, result, "step-backward");
      settle(control);
    }
    result = control.request(moveRequest(PlaybackRequestKind::Scrub, 0, 0, 0.0, false));
    printStep(control, result, "seek");
    settle(control);
    result = control.request(moveRequest(PlaybackRequestKind::StepTicks, 0, -1, 0.0, false));
    printStep(control, result, "step-backward");
    result = control.request(moveRequest(PlaybackRequestKind::Scrub, kEndTick, 0, 0.0, false));
    printStep(control, result, "seek");
    settle(control);
    result = control.request(moveRequest(PlaybackRequestKind::StepTicks, 0, 1, 0.0, false));
    printStep(control, result, "step-forward");
    check(control.view().cursorTick == kEndTick, "a-boundary-end-cursor",
      "cursor=" + std::to_string(control.view().cursorTick));
  }

  // ---- B: 1 s and 5 s jumps. The 5 s jump is made while playing, and the
  // transport state must be unchanged by it.
  {
    PlaybackControl control;
    control.setDemo(kEndTick, kRate, 100, true, true);
    PlaybackResult result = control.request(moveRequest(PlaybackRequestKind::Scrub, 100, 0, 0.0, false));
    printStep(control, result, "seek");
    settle(control);
    result = control.request(moveRequest(PlaybackRequestKind::JumpSeconds, 0, 0, 1.0, false));
    printStep(control, result, "jump-1s");
    settle(control);
    // A SetPaused request carries its own paused flag; moveRequest leaves it at
    // its default (true), which would turn this "resume" into a second pause
    // and make the following playing checks fail for the wrong reason.
    PlaybackRequest resume;
    resume.kind = PlaybackRequestKind::SetPaused;
    resume.paused = false;
    result = control.request(resume);
    printStep(control, result, "resume");
    check(!control.view().paused, "b-playing-before-jump5", "paused=0");
    result = control.request(moveRequest(PlaybackRequestKind::JumpSeconds, 0, 0, 5.0, false));
    printStep(control, result, "jump-5s");
    check(!control.view().paused, "b-playing-after-jump5", "paused=0");
    settle(control);
  }

  // ---- C: speed. Each speed gets a fresh control, one simulated second of
  // advance. The 1.0 vs 2.0 pair is what gate C compares; the rest is the
  // DESIGN.md A15 table.
  {
    const double speeds[] = {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    for (double speed : speeds) {
      PlaybackControl control;
      control.setDemo(100000, kRate, 100, true, true);
      const std::int32_t before = control.view().cursorTick;
      PlaybackRequest speedRequest;
      speedRequest.kind = PlaybackRequestKind::SetSpeed;
      speedRequest.speed = speed;
      const PlaybackResult speedResult = control.request(speedRequest);
      check(control.view().cursorTick == before, "c-speed-does-not-move-tick",
        "speed=" + std::to_string(speed) + " cursor=" + std::to_string(control.view().cursorTick));
      PlaybackRequest play;
      play.kind = PlaybackRequestKind::SetPaused;
      play.paused = false;
      control.request(play);
      const std::int32_t moved = control.advance(1.0);
      std::printf("advance label=speed%.3f ticks=%d cursor=%d speedAfter=%.3f\n",
        speed, moved, control.view().cursorTick, speedResult.speedAfter);
    }
  }

  // ---- D: an interrupt. Two seeks without a landing in between; the first is
  // superseded, interruptedSeeks counts one, the final landing is the second.
  {
    PlaybackControl control;
    control.setDemo(10000, kRate, 100, true, true);
    PlaybackResult first = control.request(moveRequest(PlaybackRequestKind::Scrub, 3000, 0, 0.0, false));
    printStep(control, first, "seek-first");
    PlaybackResult second = control.request(moveRequest(PlaybackRequestKind::Scrub, 4000, 0, 0.0, false));
    printStep(control, second, "seek-second");
    std::printf("interrupt superseded=%d pending=%d interruptedSeeks=%llu\n",
      second.supersededRequestId, control.pendingRequestId(),
      static_cast<unsigned long long>(control.readings().interruptedSeeks));
    check(second.supersededRequestId == first.id, "d-superseded-id",
      "superseded=" + std::to_string(second.supersededRequestId));
    check(control.readings().interruptedSeeks == 1, "d-interrupted-seeks",
      "count=" + std::to_string(control.readings().interruptedSeeks));
    settle(control);
    const FrameLanding landing = control.lastFrame();
    std::printf("landing id=%d landedCursor=%d frameStatus=%s frameTick=%d frameDelta=%d\n",
      landing.requestId, landing.landedCursorTick,
      PlaybackControl::frameStatusName(landing.frameStatus), landing.frameTick, landing.frameDelta);
    check(landing.requestId == second.id, "d-final-landing-id",
      "id=" + std::to_string(landing.requestId));
    check(landing.landedCursorTick == 4000, "d-final-landing-cursor",
      "cursor=" + std::to_string(landing.landedCursorTick));
  }

  // ---- E: unreachable ticks and clamping. Strict requests refuse without
  // moving; non-strict requests clamp to the extent.
  {
    PlaybackControl control;
    control.setDemo(kEndTick, kRate, 100, true, true);
    PlaybackResult result = control.request(moveRequest(PlaybackRequestKind::JumpTicks, 100, 0, 0.0, false));
    printStep(control, result, "seek");
    settle(control);
    const std::int32_t before = control.view().cursorTick;
    result = control.request(moveRequest(PlaybackRequestKind::JumpTicks, kEndTick + 1, 0, 0.0, true));
    printStep(control, result, "seek-strict");
    check(control.view().cursorTick == before, "e-unreachable-end-unchanged",
      "cursor=" + std::to_string(control.view().cursorTick));
    result = control.request(moveRequest(PlaybackRequestKind::JumpTicks, -1, 0, 0.0, true));
    printStep(control, result, "seek-strict");
    check(control.view().cursorTick == before, "e-unreachable-start-unchanged",
      "cursor=" + std::to_string(control.view().cursorTick));
    result = control.request(moveRequest(PlaybackRequestKind::Scrub, kEndTick + 50, 0, 0.0, false));
    printStep(control, result, "seek-clamp");
    check(control.view().cursorTick == kEndTick, "e-clamped-end",
      "cursor=" + std::to_string(control.view().cursorTick));
    result = control.request(moveRequest(PlaybackRequestKind::Scrub, -50, 0, 0.0, false));
    printStep(control, result, "seek-clamp");
    check(control.view().cursorTick == 0, "e-clamped-start",
      "cursor=" + std::to_string(control.view().cursorTick));
  }

  // ---- Focus: a keyboard transport request is refused while a text field has
  // focus. There is no real focus environment in the probe; this is the static
  // rule the product calls with isTextFieldFocused().
  {
    PlaybackControl control;
    control.setDemo(kEndTick, kRate, 100, true, true);
    PlaybackRequest request = moveRequest(PlaybackRequestKind::StepTicks, 0, 1, 0.0, false);
    request.fromKeyboard = true;
    request.textFieldFocused = true;
    const PlaybackResult result = control.request(request);
    printStep(control, result, "step-forward-focus");
    check(control.readings().focusSuppressed == 1, "focus-suppressed",
      "count=" + std::to_string(control.readings().focusSuppressed));
  }

  // ---- Landing map: one checkpoint landing and one unavailable landing.
  {
    PlaybackControl control;
    control.setDemo(kEndTick, kRate, 100, true, true);
    PlaybackResult result = control.request(moveRequest(PlaybackRequestKind::Scrub, 200, 0, 0.0, false));
    printStep(control, result, "seek");
    settle(control, SceneQueryStatus::Checkpoint);
    FrameLanding landing = control.lastFrame();
    std::printf("landing id=%d landedCursor=%d frameStatus=%s frameTick=%d frameDelta=%d\n",
      landing.requestId, landing.landedCursorTick,
      PlaybackControl::frameStatusName(landing.frameStatus), landing.frameTick, landing.frameDelta);
    check(landing.frameStatus == FrameStatus::Checkpoint, "landing-checkpoint",
      "status=" + std::string(PlaybackControl::frameStatusName(landing.frameStatus)));
    check(landing.frameDelta == 3, "landing-checkpoint-delta",
      "delta=" + std::to_string(landing.frameDelta));
    result = control.request(moveRequest(PlaybackRequestKind::Scrub, 300, 0, 0.0, false));
    printStep(control, result, "seek");
    settle(control, SceneQueryStatus::TickBeforeHistory);
    landing = control.lastFrame();
    std::printf("landing id=%d landedCursor=%d frameStatus=%s frameTick=%d frameDelta=%d\n",
      landing.requestId, landing.landedCursorTick,
      PlaybackControl::frameStatusName(landing.frameStatus), landing.frameTick, landing.frameDelta);
    check(landing.frameStatus == FrameStatus::BeforeWindow, "landing-before-window",
      "status=" + std::string(PlaybackControl::frameStatusName(landing.frameStatus)));
  }

  const PlaybackReadings readings = PlaybackControl().readings();
  (void)readings;
  std::printf("checks=%d failures=%d steps=%d\n", g_checks, g_failures, g_step);
}

int runCommand(const std::string& demoPath, const std::string& command,
    std::int32_t ticks, std::int32_t startTick, double speed) {
  const bool hasSynthetic = ticks > 0;
  bool demoExists = false;
  if (!demoPath.empty()) {
    std::FILE* file = std::fopen(demoPath.c_str(), "rb");
    if (file) { demoExists = true; std::fclose(file); }
  }
  PlaybackControl control;
  if (hasSynthetic) {
    control.setDemo(ticks, 66.6666667, 100, true, true);
  } else if (!demoExists) {
    std::printf("demo=missing path=%s\n", demoPath.empty() ? "-" : demoPath.c_str());
  } else {
    std::printf("demo=unparsed path=%s note=pass---ticks-to-synthesize\n", demoPath.c_str());
  }
  PlaybackResult result = control.request(moveRequest(PlaybackRequestKind::JumpTicks, startTick, 0, 0.0, false));
  (void)result;
  if (hasSynthetic) settle(control);
  PlaybackRequest request;
  request.kind = PlaybackRequestKind::StepTicks;
  request.ticks = 1;
  request.strict = false;
  if (command == "step-backward") request.ticks = -1;
  else if (command == "jump-1s") { request.kind = PlaybackRequestKind::JumpSeconds; request.seconds = 1.0; }
  else if (command == "jump-5s") { request.kind = PlaybackRequestKind::JumpSeconds; request.seconds = 5.0; }
  else if (command == "seek") { request.kind = PlaybackRequestKind::Scrub; request.tick = startTick + 10; }
  else if (command == "set-speed") { request.kind = PlaybackRequestKind::SetSpeed; request.speed = speed; }
  else if (command == "pause") { request.kind = PlaybackRequestKind::SetPaused; request.paused = true; }
  else if (command == "resume") { request.kind = PlaybackRequestKind::SetPaused; request.paused = false; }
  else if (command == "stop") { request.kind = PlaybackRequestKind::Stop; }
  else if (command != "step-forward") {
    std::printf("error=unknown-command command=%s\n", command.c_str());
    return 2;
  }
  result = control.request(request);
  if (hasSynthetic) settle(control);
  printStep(control, result, command.c_str());
  if (!demoExists && !hasSynthetic) {
    std::printf("error=demo-not-found path=%s\n", demoPath.empty() ? "-" : demoPath.c_str());
    return 1;
  }
  const bool ok = result.status == PlaybackRequestStatus::Applied
    || result.status == PlaybackRequestStatus::ClampedStart
    || result.status == PlaybackRequestStatus::ClampedEnd
    || result.status == PlaybackRequestStatus::SameTick;
  return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  std::string demoPath;
  std::string command;
  std::int32_t ticks = 0;
  std::int32_t startTick = 0;
  double speed = 1.0;
  bool suite = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--playback-test") suite = true;
    else if (arg == "--demo" && i + 1 < argc) demoPath = argv[++i];
    else if (arg == "--command" && i + 1 < argc) command = argv[++i];
    else if (arg == "--ticks" && i + 1 < argc) ticks = std::atoi(argv[++i]);
    else if (arg == "--start-tick" && i + 1 < argc) startTick = std::atoi(argv[++i]);
    else if (arg == "--speed" && i + 1 < argc) speed = std::atof(argv[++i]);
    else { std::printf("error=unknown-argument arg=%s\n", arg.c_str()); return 2; }
  }
  if (suite) {
    runSuite();
    return g_failures == 0 ? 0 : 1;
  }
  if (command.empty()) {
    std::printf("error=no-mode\n");
    return 2;
  }
  return runCommand(demoPath, command, ticks, startTick, speed);
}
