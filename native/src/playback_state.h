#pragma once

// T76 (2026-10-30): the playback transport's state and the one function that
// applies a request, split out of main.cpp. Code motion only: the definitions
// and the trace text are unchanged, and every call site in main.cpp keeps the
// spelling g_playback / applyPlaybackRequest through its using-directive.
//
// T70: g_playback stays the loop's view of the transport because the renderer,
// the audio scheduler and the camera director read it; g_playbackControl owns
// request ids, the pending seek and the frame landing. The only transport
// writes that do not pass through request() are the frame loop's own advance
// and the --capture-tick pin, which report back through noteExternalCursor().

#include <cstdint>

#include "demo_header.h"
#include "playback_control.h"

namespace tf2::native::main_internal {

struct PlaybackState {
  bool enabled = false;
  bool paused = false;
  bool reverse = false;
  double speed = 1.0;
  double tickRate = 66.6666667;
  std::int32_t tick = 0;
  std::int32_t endTick = 0;
  std::int32_t anchorLookupTick = -1;
  bool anchorValid = false;
  tf2::native::DemoIndexEntry anchor{};
};

extern PlaybackState g_playback;
extern tf2::native::PlaybackControl g_playbackControl;

// Applies one request and mirrors the resulting view into the legacy state.
// Every keyboard, native-UI and --camera-script transport command goes through
// here, so "requested" and "landed" cannot diverge without a trace line.
tf2::native::PlaybackResult applyPlaybackRequest(const tf2::native::PlaybackRequest& request);

}  // namespace tf2::native::main_internal
