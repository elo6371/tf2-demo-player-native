// T70: fine playback control.
//
// One request/landing ledger for every way the playback cursor can move:
// keyboard, the native UI, a --camera-script step, or the wall-clock advance in
// the frame loop. The design is D:\TF2_Native_Test\evidence\t70-t72\DESIGN.md;
// this header is the product-facing subset of it.
//
// The class is pure: no Win32, no D3D, no demo parser. The product feeds it a
// demo extent (setDemo), asks it to move (request), lets it run the wall clock
// (advance), and tells it what the scene resolve found (noteSceneResolved).
// Every reading a gate needs is a counter or a struct field, never a log scrape
// of the product.
//
// Deliberate deviation from DESIGN.md: DESIGN pauses on every seek including
// JumpTicks/JumpSeconds. The T70 acceptance gate requires a 1 s / 5 s jump to
// leave the playing flag unchanged, so jumps preserve the transport state here
// and only StepTicks/Scrub pause. See evidence/t70-controls/REPORT.md.
#ifndef TF2_NATIVE_PLAYBACK_CONTROL_H
#define TF2_NATIVE_PLAYBACK_CONTROL_H

#include <cstdint>

namespace tf2::native {

enum class PlaybackPhase {
  Disabled,
  Paused,
  Playing,
  ReversePlaying,
  SeekPending,
};

enum class PlaybackRequestKind {
  StepTicks,
  JumpTicks,
  JumpSeconds,
  Scrub,
  SetSpeed,
  SetPaused,
  SetReverse,
  Stop,
  RefreshFrame,
};

enum class PlaybackRequestStatus {
  Applied,
  ClampedStart,
  ClampedEnd,
  SameTick,
  ClampedSpeed,
  RefusedDisabled,
  RefusedInvalidTick,
  RefusedOutOfRange,
  RefusedTextFocus,
  RefusedSpeedRange,
  Superseded,
};

enum class FrameStatus {
  Pending,
  Exact,
  Checkpoint,
  BeforeWindow,
  Gap,
  NoHistory,
  DeltaBaseMissing,
  Invalid,
  NoRenderer,
  OriginUnknown,
  Superseded,
};

// The scene query outcome, mapped from EntitySnapshotQueryStatus by the
// product. Kept separate from that enum so this class stays parser-free.
enum class SceneQueryStatus {
  Available,
  Checkpoint,
  TickBeforeHistory,
  Gap,
  NoHistory,
  DeltaBaseMissing,
  NoRenderer,
  OriginUnknown,
};

struct PlaybackRequest {
  PlaybackRequestKind kind = PlaybackRequestKind::RefreshFrame;
  std::int32_t ticks = 0;
  double seconds = 0.0;
  std::int32_t tick = 0;
  double speed = 1.0;
  bool paused = true;
  bool reverse = false;
  bool strict = false;
  bool fromKeyboard = false;
  bool textFieldFocused = false;
  const char* source = "";
};

struct PlaybackResult {
  std::int32_t id = 0;
  PlaybackRequestStatus status = PlaybackRequestStatus::RefusedDisabled;
  std::int32_t requestedTick = -1;
  std::int32_t appliedTick = -1;
  std::int32_t deltaTicks = 0;
  double requestedSeconds = 0.0;
  double appliedSeconds = 0.0;
  double deltaSeconds = 0.0;
  double speedBefore = 1.0;
  double speedAfter = 1.0;
  // >0 when this request displaced a pending seek; the displaced request's id.
  std::int32_t supersededRequestId = 0;
  const char* reason = "";
};

struct FrameLanding {
  std::int32_t requestId = 0;
  bool valid = false;
  bool superseded = false;
  std::int32_t requestedTick = -1;
  std::int32_t landedCursorTick = -1;
  std::int32_t sceneTick = -1;
  std::int32_t resolvedSceneTick = -1;
  std::int32_t frameTick = -1;
  std::int32_t frameDelta = -1;
  FrameStatus frameStatus = FrameStatus::Pending;
  std::int32_t viewTick = -1;
  std::int32_t viewDelta = -1;
  bool viewValid = false;
  const char* reason = "";
};

struct PlaybackView {
  PlaybackPhase phase = PlaybackPhase::Disabled;
  std::int32_t cursorTick = 0;
  std::int32_t endTick = 0;
  std::int32_t firstPacketTick = -1;
  bool firstPacketOriginKnown = false;
  bool paused = true;
  bool reverse = false;
  double speed = 1.0;
  double tickRate = 66.6666667;
  std::int32_t pendingRequestId = 0;
  std::int32_t lastRequestId = 0;
  PlaybackRequestStatus lastStatus = PlaybackRequestStatus::Applied;
  std::int32_t lastFrameRequest = 0;
  bool boundary = false;
};

struct PlaybackReadings {
  std::uint64_t requests = 0;
  std::uint64_t landings = 0;
  std::uint64_t rejected = 0;
  std::uint64_t clampedAtStart = 0;
  std::uint64_t clampedAtEnd = 0;
  std::uint64_t interruptedSeeks = 0;
  // Cursor moves made by a seek REQUEST (StepTicks/JumpTicks/JumpSeconds/Scrub),
  // as opposed to the wall clock. T93-C: the audio scheduler treats a seek as a
  // discontinuity in either direction, and a counter here is the only way to say
  // "this tick jump was a seek" instead of guessing from its size -- a fast
  // playback advance is also a multi-tick frame.
  std::uint64_t seeks = 0;
  std::uint64_t superseded = 0;
  std::uint64_t staleLandings = 0;
  std::uint64_t sameTick = 0;
  std::uint64_t speedChanges = 0;
  std::uint64_t advanceTicks = 0;
  std::uint64_t stepsForward = 0;
  std::uint64_t stepsBackward = 0;
  std::uint64_t jumps1s = 0;
  std::uint64_t jumps5s = 0;
  std::uint64_t exact = 0;
  std::uint64_t checkpoint = 0;
  std::uint64_t unavailable = 0;
  std::uint64_t focusSuppressed = 0;
  double elapsedSeconds = 0.0;
  std::int32_t maxFrameDelta = 0;
  std::int32_t lastRequestedTick = -1;
  std::int32_t lastLandedTick = -1;
  PlaybackRequestStatus lastStatus = PlaybackRequestStatus::Applied;
  FrameStatus lastFrameStatus = FrameStatus::Pending;
};

class PlaybackControl {
public:
  static constexpr double kMinSpeed = 0.125;
  static constexpr double kMaxSpeed = 64.0;
  static constexpr double kDefaultTickRate = 66.6666667;

  void setDemo(std::int32_t endTick, double tickRate, std::int32_t firstPacketTick,
    bool firstPacketOriginKnown, bool startPaused);
  void clear();

  PlaybackResult request(const PlaybackRequest& request);
  std::int32_t advance(double elapsedSeconds);

  bool needsSceneResolve() const { return pending_; }
  std::int32_t pendingRequestId() const { return pending_ ? pendingId_ : 0; }

  void noteSceneResolved(std::int32_t requestId, SceneQueryStatus status,
    std::int32_t sceneTick, std::int32_t resolvedSceneTick,
    std::int32_t viewSampleTick, bool viewValid);
  void noteSceneUnavailable(std::int32_t requestId, FrameStatus status);
  void noteSceneSuperseded(std::int32_t requestId);
  void forceFrameRefresh();

  // Product integration hooks. The frame loop still owns g_playback and can
  // pin the cursor for --capture-tick; this adopts that move without counting
  // it as a request, and invalidates a landing that no longer describes the
  // cursor.
  void noteExternalCursor(std::int32_t tick);
  void resetAccumulator();
  // Product integration hook: adopt the legacy g_playback transport fields when
  // a path outside request() changed them (defensive; the keyboard/UI/script
  // paths all route through request()). Never counts as a request.
  void syncTransport(bool paused, bool reverse, double speed);

  PlaybackView view() const;
  PlaybackPhase phase() const;
  FrameLanding lastFrame() const { return lastFrame_; }
  PlaybackReadings readings() const;

  static double ticksForSeconds(double seconds, double tickRate);
  static const char* phaseName(PlaybackPhase phase);
  static const char* statusName(PlaybackRequestStatus status);
  static const char* frameStatusName(FrameStatus status);
  static const char* requestKindName(PlaybackRequestKind kind);

private:
  void applyPhase();
  void invalidateFrameIfStale();
  void recordResult(const PlaybackResult& result);
  bool tickInRange(std::int32_t tick) const { return tick >= 0 && tick <= endTick_; }
  PlaybackResult makeResult(const PlaybackRequest& request, PlaybackRequestStatus status,
    std::int32_t requestedTick, const char* reason) const;

  bool enabled_ = false;
  std::int32_t endTick_ = 0;
  std::int32_t cursorTick_ = 0;
  std::int32_t firstPacketTick_ = -1;
  bool firstPacketOriginKnown_ = false;
  double tickRate_ = kDefaultTickRate;
  bool paused_ = true;
  bool reverse_ = false;
  double speed_ = 1.0;
  double accumulator_ = 0.0;
  PlaybackPhase phase_ = PlaybackPhase::Disabled;
  std::uint64_t generation_ = 0;

  std::int32_t requestId_ = 0;
  std::int32_t pendingId_ = 0;
  std::int32_t pendingRequestedTick_ = -1;
  bool pending_ = false;

  PlaybackResult lastResult_{};
  FrameLanding lastFrame_{};
  std::int32_t lastResolvedCursor_ = -1;
  std::int32_t lastFrameRequest_ = 0;

  PlaybackReadings readings_{};
};

} // namespace tf2::native

#endif // TF2_NATIVE_PLAYBACK_CONTROL_H
