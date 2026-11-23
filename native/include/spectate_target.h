#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// SourceTV spectate director (T66).
//
// A SourceTV demo is watched from one entity: whatever svc_SetView named. In the
// S1 sample that is entity 1 -- the SourceTV player itself -- and the demo never
// says which *player* that camera stands behind. svc_SetView fires once with
// viewent=1 and m_hObserverTarget on that entity is a constant handle to a
// permanently dead spectator slot (301073 -> entity 17, serial 147, team 1,
// m_lifeState 2 at every sampled tick from 15000 to 70000). The observed player
// is therefore a decision this product has to make itself, and this file is that
// decision, kept pure so a test can state it as numbers instead of watching the
// title.
//
// The rules, in the order they are applied every frame:
//   1. A dead player is not drawn. The body leaves at the tick its m_lifeState
//      stops being 0; there is no ragdoll and no frozen corpse left behind.
//   2. A target that dies hands the camera over: the demo's own director handle
//      when it names a living player, otherwise the living players in a stable
//      order (entity index ascending, starting after the dead one) so two runs
//      of the same demo pick the same player, invalid/dead/reused slots are
//      skipped, and a manual pick wins over both.
//   3. The handover takes kHandoverSeconds of WALL time. The caller passes a
//      monotonic clock reading, never a playback position, so a paused demo
//      still completes the handover -- "pause must not freeze the camera"
//      restated for the relay, and the only way to assert it without a human.
//   4. A manual pick applies in the same frame, with no transition window.
//   5. With no living player the last valid focus is kept. There is deliberately
//      no fallback to the world origin, to the shipped orbit or to a random
//      player: a camera that jumps on total wipe is the defect this rule names.
// ---------------------------------------------------------------------------

// A m_hObserverTarget value is a CBaseHandle: the entry index sits in the low 11
// bits and the serial number above it (NUM_ENT_ENTRY_BITS). A handle of 0, or one
// whose entry index exceeds the world's entity count, names no entity.
struct EntityHandle {
  std::int32_t entityIndex = -1;
  std::uint32_t serial = 0;
  bool valid = false;
};
EntityHandle decodeEntityHandle(std::int64_t handle, std::size_t entityCount);

// One player-shaped row of an entity snapshot, reduced to the fields the spectate
// decision needs. The defaults are chosen so that a row nobody filled in is never
// "alive": an unreadable player must not win the relay.
struct SpectateSlot {
  std::int32_t entityIndex = -1;
  bool isPlayer = false;
  bool hasHealth = false;
  std::int64_t health = 0;
  bool hasLifeState = false;
  std::int64_t lifeState = 1;
  bool hasTeam = false;
  std::int64_t team = 0;
  bool hasOrigin = false;
  float origin[3] = {0.0f, 0.0f, 0.0f};
};

// Alive means "m_lifeState is 0 and m_iHealth is positive". A snapshot that
// carries no m_lifeState falls back to the health reading rather than assuming
// death, because a field the demo does not send is not evidence of a corpse. A
// row with neither reading is not alive.
bool slotAlive(const SpectateSlot& slot);
// A spectator (team 1) is a player class but never an observation target.
bool slotObservable(const SpectateSlot& slot);

enum class SpectateSource { None, Director, StableOrder, Manual };
const char* spectateSourceName(SpectateSource source);

class SpectateDirector {
public:
  // 0.3 s: the task's number, spelled once.
  static constexpr double kHandoverSeconds = 0.3;
  // A jump is a seek when the tick moves further than continuous playback could
  // have moved it since the previous update. That distance depends on the speed,
  // so it is computed, not spelled: kSeekFloorTicks is the floor (at 66 tick/s a
  // frame moves the tick by a handful) and update() raises it to twice the
  // distance the live speed could have travelled in the elapsed wall time. A
  // single fixed 200 was right at 8x and reported every other frame of a 16x
  // replay as a seek -- the defect T66 note 1 records.
  static constexpr std::int32_t kSeekFloorTicks = 32;
  // The demo's tick rate, spelled once, used only to turn a wall-clock interval
  // into the number of ticks that interval could have contained.
  static constexpr double kTickRate = 66.0;

  // nowSeconds is a monotonic wall clock. It is never compared with, derived
  // from or stored as a demo tick: that separation is what makes "the handover
  // keeps running while the demo is paused" testable by passing one tick twice
  // and advancing only this number.
  //
  // playbackSpeed has no default on purpose. A default would let a caller that
  // knows the live speed silently inherit the gate's speed and misjudge every
  // frame of a fast replay as a seek -- the shape of the bug this parameter
  // exists to fix. Every caller states the speed it is running at.
  void update(double nowSeconds,
              const std::vector<SpectateSlot>& slots,
              std::int64_t directorHandle,
              std::int32_t serverTick,
              double playbackSpeed);

  // Recompute the dead set from a snapshot WITHOUT taking the camera decision.
  // The draw path runs before the frame's own update(): it must be able to ask
  // which bodies THIS snapshot calls corpses, or a death row is drawn on the
  // frame that sees it and hidden on the frame after. Readings the gates assert
  // (target, source, counters) are untouched.
  void refreshDead(const std::vector<SpectateSlot>& slots);

  // The player the user clicked. It takes effect on the next update, immediately
  // and without a transition; the source reads Manual so a gate can tell the two
  // paths apart.
  void pickManual(std::int32_t entityIndex);
  bool manualPending() const { return manualPending_; }

  // True for every player the relay knows to be dead, which is what the draw loop
  // asks before it submits an instance.
  bool hidesModel(std::int32_t entityIndex) const;

  // ---- readings a gate can assert -----------------------------------------
  std::int32_t targetIndex() const { return target_; }
  SpectateSource source() const { return source_; }
  bool transitioning() const { return transitionActive_; }
  double transitionProgress() const;
  bool focusValid() const { return focusValid_; }
  void focus(float out[3]) const;
  std::uint32_t frames() const { return frames_; }
  std::uint32_t deaths() const { return deaths_; }
  std::uint32_t handovers() const { return handovers_; }
  std::uint32_t noTargetFrames() const { return noTargetFrames_; }
  std::uint32_t resyncs() const { return resyncs_; }
  std::uint32_t manualPicks() const { return manualPicks_; }
  std::uint32_t manualRejected() const { return manualRejected_; }
  std::uint32_t directorPicks() const { return directorPicks_; }
  std::uint32_t stablePicks() const { return stablePicks_; }
  std::uint32_t hiddenDead() const { return hiddenDead_; }
  // The entities the last update decided are corpses. Published so a trace can
  // name the body instead of only counting it, and so a gate can ask whether the
  // dead player it is about is the one that left the picture.
  const std::vector<std::int32_t>& deadIndices() const { return deadIndices_; }
  std::int32_t lastDeathTick() const { return lastDeathTick_; }
  std::int32_t lastServerTick() const { return lastServerTick_; }
  // One line, fixed field order, the same form the product traces and the probe
  // prints so a gate cuts fields out of either.
  std::string traceFields() const;

private:
  void adopt(std::int32_t entityIndex, SpectateSource source);
  void startHandover(double nowSeconds, std::int32_t entityIndex);
  bool slotFor(std::int32_t entityIndex, const std::vector<SpectateSlot>& slots,
               SpectateSlot const** out) const;
  std::int32_t pickStable(const std::vector<SpectateSlot>& slots) const;

  double now_ = 0.0;
  std::int32_t target_ = -1;
  SpectateSource source_ = SpectateSource::None;

  bool manualPending_ = false;
  std::int32_t manualTarget_ = -1;

  bool transitionActive_ = false;
  double transitionStart_ = 0.0;
  float transitionFrom_[3] = {0.0f, 0.0f, 0.0f};
  bool transitionFromValid_ = false;

  float focus_[3] = {0.0f, 0.0f, 0.0f};
  bool focusValid_ = false;

  bool targetDeadRecorded_ = false;
  std::vector<std::int32_t> deadIndices_;

  std::int32_t lastServerTick_ = -1;
  std::int32_t lastDeathTick_ = -1;
  // Wall clock of the previous update, so a seek bound can be derived from the
  // time the frames actually took. hasLastNow_ keeps the first update honest:
  // there is no previous interval to measure, so only the floor applies.
  double lastNow_ = 0.0;
  bool hasLastNow_ = false;

  std::uint32_t frames_ = 0;
  std::uint32_t deaths_ = 0;
  std::uint32_t handovers_ = 0;
  std::uint32_t noTargetFrames_ = 0;
  std::uint32_t resyncs_ = 0;
  std::uint32_t manualPicks_ = 0;
  std::uint32_t manualRejected_ = 0;
  std::uint32_t directorPicks_ = 0;
  std::uint32_t stablePicks_ = 0;
  std::uint32_t hiddenDead_ = 0;
};

} // namespace tf2::native
