#include "playback_control.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace tf2::native {
namespace {

constexpr std::int64_t kInt32Min = std::numeric_limits<std::int32_t>::min();
constexpr std::int64_t kInt32Max = std::numeric_limits<std::int32_t>::max();

bool isTickMove(PlaybackRequestKind kind) {
  return kind == PlaybackRequestKind::StepTicks
    || kind == PlaybackRequestKind::JumpTicks
    || kind == PlaybackRequestKind::JumpSeconds
    || kind == PlaybackRequestKind::Scrub;
}

} // namespace

double PlaybackControl::ticksForSeconds(double seconds, double tickRate) {
  if (!std::isfinite(seconds) || !std::isfinite(tickRate) || tickRate <= 0.0) return 0.0;
  return std::round(seconds * tickRate);
}

void PlaybackControl::setDemo(std::int32_t endTick, double tickRate,
    std::int32_t firstPacketTick, bool firstPacketOriginKnown, bool startPaused) {
  enabled_ = true;
  endTick_ = std::max<std::int32_t>(0, endTick);
  cursorTick_ = 0;
  firstPacketTick_ = firstPacketTick;
  firstPacketOriginKnown_ = firstPacketOriginKnown && firstPacketTick >= 0;
  tickRate_ = (std::isfinite(tickRate) && tickRate > 0.0) ? tickRate : kDefaultTickRate;
  paused_ = startPaused;
  reverse_ = false;
  speed_ = 1.0;
  accumulator_ = 0.0;
  ++generation_;
  requestId_ = 0;
  pending_ = false;
  pendingId_ = 0;
  pendingRequestedTick_ = -1;
  lastResult_ = PlaybackResult{};
  lastFrame_ = FrameLanding{};
  lastResolvedCursor_ = -1;
  lastFrameRequest_ = 0;
  readings_ = PlaybackReadings{};
  applyPhase();
}

void PlaybackControl::clear() {
  *this = PlaybackControl{};
}

void PlaybackControl::applyPhase() {
  if (!enabled_) {
    phase_ = PlaybackPhase::Disabled;
  } else if (pending_ && paused_) {
    phase_ = PlaybackPhase::SeekPending;
  } else if (paused_) {
    phase_ = PlaybackPhase::Paused;
  } else {
    phase_ = reverse_ ? PlaybackPhase::ReversePlaying : PlaybackPhase::Playing;
  }
}

void PlaybackControl::recordResult(const PlaybackResult& result) {
  lastResult_ = result;
  readings_.lastRequestedTick = result.requestedTick;
  readings_.lastStatus = result.status;
  if (result.status == PlaybackRequestStatus::Applied
      || result.status == PlaybackRequestStatus::ClampedStart
      || result.status == PlaybackRequestStatus::ClampedEnd
      || result.status == PlaybackRequestStatus::SameTick) {
    readings_.lastLandedTick = result.appliedTick;
  }
}

PlaybackResult PlaybackControl::makeResult(const PlaybackRequest& request,
    PlaybackRequestStatus status, std::int32_t requestedTick, const char* reason) const {
  PlaybackResult result;
  result.id = requestId_;
  result.status = status;
  result.requestedTick = requestedTick;
  result.appliedTick = cursorTick_;
  result.speedBefore = speed_;
  result.speedAfter = speed_;
  result.reason = reason;
  (void)request;
  return result;
}

PlaybackResult PlaybackControl::request(const PlaybackRequest& request) {
  const std::int32_t before = cursorTick_;
  const double speedBefore = speed_;
  ++requestId_;
  PlaybackResult result = makeResult(request, PlaybackRequestStatus::Applied, cursorTick_, "");
  result.speedBefore = speedBefore;
  result.speedAfter = speed_;
  readings_.requests++;

  if (!enabled_) {
    result.status = PlaybackRequestStatus::RefusedDisabled;
    result.reason = "disabled";
    ++readings_.rejected;
    recordResult(result);
    return result;
  }
  if (request.textFieldFocused && request.fromKeyboard) {
    result.status = PlaybackRequestStatus::RefusedTextFocus;
    result.reason = "text-focus";
    ++readings_.rejected;
    ++readings_.focusSuppressed;
    recordResult(result);
    return result;
  }

  if (request.kind == PlaybackRequestKind::SetSpeed) {
    const double wanted = request.speed;
    PlaybackRequestStatus status = PlaybackRequestStatus::Applied;
    double applied = wanted;
    const char* reason = "";
    if (!std::isfinite(wanted)) {
      status = PlaybackRequestStatus::RefusedSpeedRange;
      reason = "speed-range";
    } else if (wanted < kMinSpeed || wanted > kMaxSpeed) {
      if (request.strict) {
        status = PlaybackRequestStatus::RefusedSpeedRange;
        reason = "speed-range";
      } else {
        applied = std::clamp(wanted, kMinSpeed, kMaxSpeed);
        status = PlaybackRequestStatus::ClampedSpeed;
      }
    }
    result.status = status;
    result.reason = reason;
    result.requestedTick = cursorTick_;
    result.appliedTick = cursorTick_;
    if (status == PlaybackRequestStatus::RefusedSpeedRange) {
      ++readings_.rejected;
    } else {
      speed_ = applied;
      ++readings_.speedChanges;
    }
    result.speedAfter = speed_;
    recordResult(result);
    return result;
  }

  if (request.kind == PlaybackRequestKind::SetPaused) {
    paused_ = request.paused;
    if (paused_) accumulator_ = 0.0;
    applyPhase();
    result.requestedTick = cursorTick_;
    result.appliedTick = cursorTick_;
    recordResult(result);
    return result;
  }

  if (request.kind == PlaybackRequestKind::SetReverse) {
    reverse_ = request.reverse;
    accumulator_ = 0.0;
    applyPhase();
    result.requestedTick = cursorTick_;
    result.appliedTick = cursorTick_;
    recordResult(result);
    return result;
  }

  if (request.kind == PlaybackRequestKind::Stop) {
    cursorTick_ = 0;
    paused_ = true;
    reverse_ = false;
    accumulator_ = 0.0;
    pending_ = false;
    applyPhase();
    result.requestedTick = 0;
    result.appliedTick = 0;
    result.deltaTicks = 0 - before;
    recordResult(result);
    return result;
  }

  if (request.kind == PlaybackRequestKind::RefreshFrame) {
    forceFrameRefresh();
    result.requestedTick = cursorTick_;
    result.appliedTick = cursorTick_;
    recordResult(result);
    return result;
  }

  if (!isTickMove(request.kind)) {
    result.status = PlaybackRequestStatus::RefusedInvalidTick;
    result.reason = "unknown-request";
    ++readings_.rejected;
    recordResult(result);
    return result;
  }

  const double rate = tickRate_ > 0.0 ? tickRate_ : kDefaultTickRate;
  std::int64_t target = cursorTick_;
  if (request.kind == PlaybackRequestKind::StepTicks) {
    target = static_cast<std::int64_t>(cursorTick_) + request.ticks;
  } else if (request.kind == PlaybackRequestKind::JumpTicks) {
    target = request.tick;
  } else if (request.kind == PlaybackRequestKind::Scrub) {
    target = request.tick;
  } else if (request.kind == PlaybackRequestKind::JumpSeconds) {
    if (!std::isfinite(request.seconds)) {
      result.status = PlaybackRequestStatus::RefusedInvalidTick;
      result.reason = "invalid-seconds";
      ++readings_.rejected;
      recordResult(result);
      return result;
    }
    target = static_cast<std::int64_t>(cursorTick_)
      + static_cast<std::int64_t>(std::llround(request.seconds * rate));
    result.requestedSeconds = request.seconds;
  }

  if (target < kInt32Min || target > kInt32Max) {
    result.status = PlaybackRequestStatus::RefusedInvalidTick;
    result.reason = "invalid-tick";
    ++readings_.rejected;
    recordResult(result);
    return result;
  }
  const std::int32_t requestedTick = static_cast<std::int32_t>(target);
  result.requestedTick = requestedTick;

  if (requestedTick < 0 || requestedTick > endTick_) {
    if (request.strict) {
      result.status = PlaybackRequestStatus::RefusedOutOfRange;
      result.reason = "tick-out-of-range";
      ++readings_.rejected;
      recordResult(result);
      return result;
    }
    const bool atStart = requestedTick < 0;
    result.status = atStart ? PlaybackRequestStatus::ClampedStart
                            : PlaybackRequestStatus::ClampedEnd;
    if (atStart) ++readings_.clampedAtStart; else ++readings_.clampedAtEnd;
    result.appliedTick = atStart ? 0 : endTick_;
    result.deltaTicks = result.appliedTick - before;
    result.requestedTick = requestedTick;
    result.reason = atStart ? "clamped-start" : "clamped-end";
    cursorTick_ = result.appliedTick;
    accumulator_ = 0.0;
    applyPhase();
    recordResult(result);
    return result;
  }

  if (requestedTick == cursorTick_) {
    result.status = PlaybackRequestStatus::SameTick;
    result.appliedTick = cursorTick_;
    result.reason = "same-tick";
    ++readings_.sameTick;
    recordResult(result);
    return result;
  }

  // A tick move that lands on a new cursor. StepTicks and Scrub are scrubbing
  // actions: DESIGN pauses on them. Jumps preserve the transport state (T70
  // gate B: a 1 s / 5 s jump must not change the playing flag).
  if (request.kind == PlaybackRequestKind::StepTicks
      || request.kind == PlaybackRequestKind::Scrub) {
    paused_ = true;
  }
  if (pending_) {
    ++readings_.superseded;
    ++readings_.interruptedSeeks;
    result.supersededRequestId = pendingId_;
  }
  // T93-C: counted at the one place a seek request moves the cursor, so the
  // reading is a fact about this ledger rather than a client-side inference.
  ++readings_.seeks;
  cursorTick_ = requestedTick;
  accumulator_ = 0.0;
  pending_ = true;
  pendingId_ = requestId_;
  pendingRequestedTick_ = requestedTick;
  result.status = PlaybackRequestStatus::Applied;
  result.appliedTick = cursorTick_;
  result.deltaTicks = cursorTick_ - before;
  if (request.kind == PlaybackRequestKind::JumpSeconds) {
    result.appliedSeconds = static_cast<double>(result.deltaTicks) / rate;
    result.deltaSeconds = result.appliedSeconds - result.requestedSeconds;
    if (std::abs(request.seconds - 1.0) < 1e-9) ++readings_.jumps1s;
    else if (std::abs(request.seconds - 5.0) < 1e-9) ++readings_.jumps5s;
  }
  if (request.kind == PlaybackRequestKind::StepTicks) {
    if (request.ticks > 0) ++readings_.stepsForward;
    else if (request.ticks < 0) ++readings_.stepsBackward;
  }
  applyPhase();
  recordResult(result);
  return result;
}

std::int32_t PlaybackControl::advance(double elapsedSeconds) {
  if (!enabled_ || paused_ || pending_) return 0;
  if (!(elapsedSeconds > 0.0) || !std::isfinite(elapsedSeconds)) return 0;
  readings_.elapsedSeconds += elapsedSeconds;
  accumulator_ += elapsedSeconds * tickRate_ * speed_;
  const std::int32_t direction = reverse_ ? -1 : 1;
  std::int32_t moved = 0;
  while (accumulator_ >= 1.0) {
    if (direction > 0 && cursorTick_ >= endTick_) { accumulator_ = 0.0; break; }
    if (direction < 0 && cursorTick_ <= 0) { accumulator_ = 0.0; break; }
    cursorTick_ += direction;
    ++moved;
    accumulator_ -= 1.0;
  }
  if (moved != 0) {
    readings_.advanceTicks += static_cast<std::uint64_t>(moved < 0 ? -moved : moved);
    invalidateFrameIfStale();
  }
  return moved;
}

void PlaybackControl::invalidateFrameIfStale() {
  if (lastFrame_.requestId != 0 && lastResolvedCursor_ != cursorTick_) {
    lastFrame_.valid = false;
    lastFrame_.frameStatus = FrameStatus::Pending;
    lastFrame_.frameTick = -1;
    lastFrame_.frameDelta = -1;
    lastFrame_.landedCursorTick = cursorTick_;
  }
}

void PlaybackControl::noteExternalCursor(std::int32_t tick) {
  cursorTick_ = std::clamp(tick, 0, endTick_);
  invalidateFrameIfStale();
}

void PlaybackControl::resetAccumulator() {
  accumulator_ = 0.0;
}

void PlaybackControl::syncTransport(bool paused, bool reverse, double speed) {
  if (!enabled_) return;
  if (std::isfinite(speed) && speed >= kMinSpeed && speed <= kMaxSpeed) speed_ = speed;
  paused_ = paused;
  reverse_ = reverse;
  applyPhase();
}

void PlaybackControl::noteSceneResolved(std::int32_t requestId, SceneQueryStatus status,
    std::int32_t sceneTick, std::int32_t resolvedSceneTick,
    std::int32_t viewSampleTick, bool viewValid) {
  if (!pending_ || requestId <= 0 || requestId != pendingId_) {
    ++readings_.staleLandings;
    lastFrame_.superseded = true;
    lastFrame_.requestId = requestId;
    lastFrame_.frameStatus = FrameStatus::Superseded;
    return;
  }
  FrameLanding landing;
  landing.requestId = requestId;
  landing.requestedTick = pendingRequestedTick_;
  landing.landedCursorTick = cursorTick_;
  landing.sceneTick = sceneTick;
  landing.resolvedSceneTick = resolvedSceneTick;

  if (!firstPacketOriginKnown_ || firstPacketTick_ < 0) {
    landing.frameStatus = FrameStatus::OriginUnknown;
    landing.reason = "origin-unknown";
  } else {
    const std::int32_t frameTick = resolvedSceneTick - firstPacketTick_;
    switch (status) {
      case SceneQueryStatus::Available:
        if (resolvedSceneTick == sceneTick && frameTick >= 0) {
          landing.frameStatus = FrameStatus::Exact;
          landing.valid = true;
        } else {
          landing.frameStatus = FrameStatus::Invalid;
          landing.reason = "available-mismatch";
        }
        break;
      case SceneQueryStatus::Checkpoint:
        if (resolvedSceneTick <= sceneTick && frameTick >= 0) {
          landing.frameStatus = resolvedSceneTick == sceneTick
            ? FrameStatus::Exact : FrameStatus::Checkpoint;
          landing.valid = true;
        } else {
          landing.frameStatus = FrameStatus::Invalid;
          landing.reason = "checkpoint-after-scene";
        }
        break;
      case SceneQueryStatus::TickBeforeHistory:
        landing.frameStatus = FrameStatus::BeforeWindow;
        landing.reason = "before-window";
        break;
      case SceneQueryStatus::Gap:
        landing.frameStatus = FrameStatus::Gap;
        landing.reason = "gap";
        break;
      case SceneQueryStatus::NoHistory:
        landing.frameStatus = FrameStatus::NoHistory;
        landing.reason = "no-history";
        break;
      case SceneQueryStatus::DeltaBaseMissing:
        landing.frameStatus = FrameStatus::DeltaBaseMissing;
        landing.reason = "delta-base-missing";
        break;
      case SceneQueryStatus::NoRenderer:
        landing.frameStatus = FrameStatus::NoRenderer;
        landing.reason = "no-renderer";
        break;
      case SceneQueryStatus::OriginUnknown:
        landing.frameStatus = FrameStatus::OriginUnknown;
        landing.reason = "origin-unknown";
        break;
    }
    if (landing.valid) {
      landing.frameTick = frameTick;
      landing.frameDelta = sceneTick - resolvedSceneTick;
    }
  }

  if (viewValid && firstPacketOriginKnown_ && firstPacketTick_ >= 0 && viewSampleTick >= 0) {
    landing.viewTick = viewSampleTick - firstPacketTick_;
    landing.viewDelta = sceneTick - viewSampleTick;
    landing.viewValid = true;
  }

  ++readings_.landings;
  if (landing.frameStatus == FrameStatus::Exact) ++readings_.exact;
  else if (landing.frameStatus == FrameStatus::Checkpoint) ++readings_.checkpoint;
  else ++readings_.unavailable;
  if (landing.valid && landing.frameDelta > readings_.maxFrameDelta) {
    readings_.maxFrameDelta = landing.frameDelta;
  }
  pending_ = false;
  pendingId_ = 0;
  pendingRequestedTick_ = -1;
  lastFrame_ = landing;
  lastResolvedCursor_ = cursorTick_;
  lastFrameRequest_ = requestId;
  readings_.lastLandedTick = cursorTick_;
  readings_.lastFrameStatus = landing.frameStatus;
  applyPhase();
}

void PlaybackControl::noteSceneUnavailable(std::int32_t requestId, FrameStatus status) {
  if (!pending_ || requestId <= 0 || requestId != pendingId_) {
    ++readings_.staleLandings;
    lastFrame_.superseded = true;
    lastFrame_.requestId = requestId;
    lastFrame_.frameStatus = FrameStatus::Superseded;
    return;
  }
  FrameLanding landing;
  landing.requestId = requestId;
  landing.requestedTick = pendingRequestedTick_;
  landing.landedCursorTick = cursorTick_;
  landing.frameStatus = status;
  landing.frameTick = -1;
  landing.frameDelta = -1;
  landing.reason = frameStatusName(status);
  ++readings_.landings;
  ++readings_.unavailable;
  pending_ = false;
  pendingId_ = 0;
  pendingRequestedTick_ = -1;
  lastFrame_ = landing;
  lastResolvedCursor_ = cursorTick_;
  lastFrameRequest_ = requestId;
  readings_.lastLandedTick = cursorTick_;
  readings_.lastFrameStatus = landing.frameStatus;
  applyPhase();
}

void PlaybackControl::noteSceneSuperseded(std::int32_t requestId) {
  if (!pending_ || requestId <= 0 || requestId != pendingId_) {
    ++readings_.staleLandings;
    return;
  }
  lastFrame_.requestId = requestId;
  lastFrame_.superseded = true;
  lastFrame_.frameStatus = FrameStatus::Superseded;
  pending_ = false;
  pendingId_ = 0;
  pendingRequestedTick_ = -1;
  readings_.lastFrameStatus = FrameStatus::Superseded;
  applyPhase();
}

void PlaybackControl::forceFrameRefresh() {
  lastFrame_ = FrameLanding{};
  lastFrame_.frameStatus = FrameStatus::Pending;
  lastFrame_.requestId = lastResult_.id;
  lastFrameRequest_ = lastResult_.id;
  lastResolvedCursor_ = -1;
}

PlaybackView PlaybackControl::view() const {
  PlaybackView out;
  out.phase = phase_;
  out.cursorTick = cursorTick_;
  out.endTick = endTick_;
  out.firstPacketTick = firstPacketTick_;
  out.firstPacketOriginKnown = firstPacketOriginKnown_;
  out.paused = paused_;
  out.reverse = reverse_;
  out.speed = speed_;
  out.tickRate = tickRate_;
  out.pendingRequestId = pending_ ? pendingId_ : 0;
  out.lastRequestId = lastResult_.id;
  out.lastStatus = lastResult_.status;
  out.lastFrameRequest = lastFrameRequest_;
  out.boundary = cursorTick_ <= 0 || cursorTick_ >= endTick_;
  return out;
}

PlaybackPhase PlaybackControl::phase() const { return phase_; }

PlaybackReadings PlaybackControl::readings() const { return readings_; }

const char* PlaybackControl::phaseName(PlaybackPhase phase) {
  switch (phase) {
    case PlaybackPhase::Disabled: return "disabled";
    case PlaybackPhase::Paused: return "paused";
    case PlaybackPhase::Playing: return "playing";
    case PlaybackPhase::ReversePlaying: return "reverse-playing";
    case PlaybackPhase::SeekPending: return "seek-pending";
  }
  return "unknown";
}

const char* PlaybackControl::statusName(PlaybackRequestStatus status) {
  switch (status) {
    case PlaybackRequestStatus::Applied: return "applied";
    case PlaybackRequestStatus::ClampedStart: return "clamped-start";
    case PlaybackRequestStatus::ClampedEnd: return "clamped-end";
    case PlaybackRequestStatus::SameTick: return "same-tick";
    case PlaybackRequestStatus::ClampedSpeed: return "clamped-speed";
    case PlaybackRequestStatus::RefusedDisabled: return "refused-disabled";
    case PlaybackRequestStatus::RefusedInvalidTick: return "refused-invalid-tick";
    case PlaybackRequestStatus::RefusedOutOfRange: return "refused-out-of-range";
    case PlaybackRequestStatus::RefusedTextFocus: return "refused-text-focus";
    case PlaybackRequestStatus::RefusedSpeedRange: return "refused-speed-range";
    case PlaybackRequestStatus::Superseded: return "superseded";
  }
  return "unknown";
}

const char* PlaybackControl::frameStatusName(FrameStatus status) {
  switch (status) {
    case FrameStatus::Pending: return "pending";
    case FrameStatus::Exact: return "exact";
    case FrameStatus::Checkpoint: return "checkpoint";
    case FrameStatus::BeforeWindow: return "before-window";
    case FrameStatus::Gap: return "gap";
    case FrameStatus::NoHistory: return "no-history";
    case FrameStatus::DeltaBaseMissing: return "delta-base-missing";
    case FrameStatus::Invalid: return "invalid";
    case FrameStatus::NoRenderer: return "no-renderer";
    case FrameStatus::OriginUnknown: return "origin-unknown";
    case FrameStatus::Superseded: return "superseded";
  }
  return "unknown";
}

const char* PlaybackControl::requestKindName(PlaybackRequestKind kind) {
  switch (kind) {
    case PlaybackRequestKind::StepTicks: return "step-ticks";
    case PlaybackRequestKind::JumpTicks: return "jump-ticks";
    case PlaybackRequestKind::JumpSeconds: return "jump-seconds";
    case PlaybackRequestKind::Scrub: return "scrub";
    case PlaybackRequestKind::SetSpeed: return "set-speed";
    case PlaybackRequestKind::SetPaused: return "set-paused";
    case PlaybackRequestKind::SetReverse: return "set-reverse";
    case PlaybackRequestKind::Stop: return "stop";
    case PlaybackRequestKind::RefreshFrame: return "refresh-frame";
  }
  return "unknown";
}

} // namespace tf2::native
