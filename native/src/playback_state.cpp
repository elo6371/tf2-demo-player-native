#include "playback_state.h"

#include "main_internal.h"

namespace tf2::native::main_internal {

PlaybackState g_playback{};
tf2::native::PlaybackControl g_playbackControl;

// Applies one request and mirrors the resulting view into the legacy state.
// Every keyboard, native-UI and --camera-script transport command goes through
// here, so "requested" and "landed" cannot diverge without a trace line.
tf2::native::PlaybackResult applyPlaybackRequest(const tf2::native::PlaybackRequest& request) {
  const tf2::native::PlaybackResult result = g_playbackControl.request(request);
  traceFmt("playback: request id=%d kind=%s source=%s requestedTick=%d requestedSeconds=%.6f"
    " appliedTick=%d appliedSeconds=%.6f deltaTicks=%d deltaSeconds=%.6f"
    " speedBefore=%.6f speedAfter=%.6f strict=%d status=%s reason=%s",
    static_cast<int>(result.id),
    tf2::native::PlaybackControl::requestKindName(request.kind), request.source,
    static_cast<int>(result.requestedTick), result.requestedSeconds,
    static_cast<int>(result.appliedTick), result.appliedSeconds,
    static_cast<int>(result.deltaTicks), result.deltaSeconds,
    result.speedBefore, result.speedAfter, request.strict ? 1 : 0,
    tf2::native::PlaybackControl::statusName(result.status), result.reason);
  const tf2::native::PlaybackView view = g_playbackControl.view();
  g_playback.tick = view.cursorTick;
  g_playback.paused = view.paused;
  g_playback.reverse = view.reverse;
  g_playback.speed = view.speed;
  return result;
}

}  // namespace tf2::native::main_internal
