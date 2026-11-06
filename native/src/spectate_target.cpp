#include "spectate_target.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tf2::native {

namespace {

double smoothstep(double t) {
  if (!(t > 0.0)) return 0.0;
  if (t >= 1.0) return 1.0;
  return t * t * (3.0 - 2.0 * t);
}

} // namespace

EntityHandle decodeEntityHandle(std::int64_t handle, std::size_t entityCount) {
  EntityHandle out;
  // CBaseHandle: index is the low NUM_ENT_ENTRY_BITS (11) bits, serial above it.
  // A negative or zero handle names nothing; entry 0 is the world entity and can
  // never be a player.
  if (handle <= 0) return out;
  const std::int64_t index = handle & 0x7FF;
  if (index <= 0) return out;
  if (entityCount > 0 && static_cast<std::uint64_t>(index) >= static_cast<std::uint64_t>(entityCount)) {
    return out;
  }
  out.entityIndex = static_cast<std::int32_t>(index);
  out.serial = static_cast<std::uint32_t>(handle >> 11);
  out.valid = true;
  return out;
}

bool slotAlive(const SpectateSlot& slot) {
  if (!slot.isPlayer) return false;
  if (slot.hasLifeState && slot.lifeState != 0) return false;
  if (slot.hasHealth) return slot.health > 0;
  // Neither reading: such a row carries no evidence of life, and the relay must
  // not hand the camera to a slot it cannot vouch for.
  return false;
}

bool slotObservable(const SpectateSlot& slot) {
  if (!slot.isPlayer) return false;
  if (!slot.hasOrigin) return false;
  // team 1 is the spectator side. A player class on the spectator team is a
  // camera, not somebody to watch.
  if (slot.hasTeam && slot.team < 2) return false;
  return true;
}

const char* spectateSourceName(SpectateSource source) {
  switch (source) {
    case SpectateSource::Director: return "director";
    case SpectateSource::StableOrder: return "stable";
    case SpectateSource::Manual: return "manual";
    case SpectateSource::None: break;
  }
  return "none";
}

void SpectateDirector::update(double nowSeconds,
                              const std::vector<SpectateSlot>& slots,
                              std::int64_t directorHandle,
                              std::int32_t serverTick,
                              double playbackSpeed) {
  ++frames_;
  const double frameSeconds = hasLastNow_ && nowSeconds > lastNow_ ? nowSeconds - lastNow_ : 0.0;
  lastNow_ = nowSeconds;
  hasLastNow_ = true;
  now_ = nowSeconds;

  // ---- 1. a new timeline? ------------------------------------------------
  // A tick that goes backwards, or one that jumps further than continuous
  // playback could have jumped in the time that actually elapsed, is a seek. The
  // handover is abandoned: interpolating between a focus from the old moment and
  // a target from the new one would draw a camera standing between two points of
  // the match that never coexisted.
  //
  // The bound is the distance continuous playback at THIS speed could have
  // covered, doubled for host jitter: a frame that took 40 ms at 8x moves the
  // tick by ~21, and a seek moves it by thousands. Reading the speed here, rather
  // than assuming the gate's 8x, is what lets a 16x replay be a replay instead of
  // a seek on every frame. The floor keeps a paused screen (no elapsed time, no
  // travel) from calling a one-tick step a seek.
  if (serverTick >= 0 && lastServerTick_ >= 0) {
    const double speed = playbackSpeed > 0.0 ? playbackSpeed : 1.0;
    const double travel = kTickRate * speed * frameSeconds * 2.0;
    const std::int32_t allowed = travel > static_cast<double>(kSeekFloorTicks)
      ? static_cast<std::int32_t>(travel)
      : kSeekFloorTicks;
    if (serverTick < lastServerTick_ || serverTick - lastServerTick_ > allowed) {
      ++resyncs_;
      transitionActive_ = false;
      targetDeadRecorded_ = false;
    }
  }
  if (serverTick >= 0) lastServerTick_ = serverTick;

  // ---- 2. who is dead ----------------------------------------------------
  refreshDead(slots);

  // ---- 3. what the demo's own director says ------------------------------
  // The handle's entry index is a WORLD slot, while `slots` is only the rows this
  // frame cared about. The bound therefore has to be the highest index present, not
  // the row count: a two-row snapshot holding entities 3 and 7 must still accept a
  // handle to entity 7.
  std::size_t worldCount = 0;
  for (const SpectateSlot& slot : slots) {
    if (slot.entityIndex < 0) continue;
    const std::size_t entry = static_cast<std::size_t>(slot.entityIndex) + 1u;
    if (entry > worldCount) worldCount = entry;
  }
  const EntityHandle handle = decodeEntityHandle(directorHandle, worldCount);
  bool directorUsable = false;
  if (handle.valid) {
    const SpectateSlot* slot = nullptr;
    if (slotFor(handle.entityIndex, slots, &slot)) {
      directorUsable = slotObservable(*slot) && slotAlive(*slot);
    }
  }

  // ---- 4. the target we are already on ------------------------------------
  const SpectateSlot* targetSlot = nullptr;
  bool targetPresent = false;
  bool targetAlive = false;
  if (target_ >= 0 && slotFor(target_, slots, &targetSlot)) {
    targetPresent = true;
    targetAlive = slotObservable(*targetSlot) && slotAlive(*targetSlot);
  }
  if (targetPresent && !targetAlive && !targetDeadRecorded_) {
    // One death, one count. The flag is cleared when the target changes or when a
    // seek rewinds to before the death, which is also what makes a replay of the
    // same moment report the same death count instead of accumulating.
    targetDeadRecorded_ = true;
    lastDeathTick_ = serverTick;
    ++deaths_;
  }
  if (!targetPresent) {
    // A slot that vanished and came back is not the same body: do not keep a
    // death recorded for it.
    targetDeadRecorded_ = false;
  }

  // ---- 5. a manual pick wins, and takes effect in this frame --------------
  if (manualPending_) {
    manualPending_ = false;
    const SpectateSlot* manualSlot = nullptr;
    const bool usable = manualTarget_ >= 0 && slotFor(manualTarget_, slots, &manualSlot)
      && slotObservable(*manualSlot) && slotAlive(*manualSlot);
    if (usable) {
      ++manualPicks_;
      if (manualTarget_ != target_) {
        target_ = manualTarget_;
        source_ = SpectateSource::Manual;
        targetDeadRecorded_ = false;
        transitionActive_ = false;
        focus_[0] = manualSlot->origin[0];
        focus_[1] = manualSlot->origin[1];
        focus_[2] = manualSlot->origin[2];
        focusValid_ = true;
      } else {
        source_ = SpectateSource::Manual;
      }
      // Refresh the snapshot-derived readings this update is about to use. The
      // manual pick is this frame's answer, but targetAlive was computed in step 4
      // from the OLD target, so leaving it stale made step 6 believe the player we
      // had just picked was not alive, hand the camera to somebody else in the
      // same frame, and animate the move -- a manual pick turning into a
      // handover, which is the one thing the task says must not happen.
      targetPresent = true;
      targetAlive = true;
      targetSlot = manualSlot;
    } else {
      // Clicking a corpse or a slot that is no longer there changes nothing; the
      // camera stays where it is rather than cutting to a dead body.
      ++manualRejected_;
    }
  }

  // ---- 6. decide who should be on screen ---------------------------------
  std::int32_t desired = -1;
  SpectateSource desiredSource = SpectateSource::None;
  if (directorUsable) {
    desired = handle.entityIndex;
    desiredSource = SpectateSource::Director;
  } else if (targetAlive) {
    desired = target_;
    desiredSource = source_;
  } else {
    desired = pickStable(slots);
    desiredSource = desired >= 0 ? SpectateSource::StableOrder : SpectateSource::None;
  }

  // ---- 7. nobody left: keep the last valid camera ------------------------
  if (desired < 0) {
    ++noTargetFrames_;
    // A handover that was already in flight finishes on its own clock. Standing
    // the camera still the instant the last player dies would be a cut, and the
    // task asks for the opposite: no jump, no default orbit, no world origin.
    if (transitionActive_) {
      const double elapsed = nowSeconds - transitionStart_;
      const double t = kHandoverSeconds > 0.0 ? elapsed / kHandoverSeconds : 1.0;
      if (!(t < 1.0)) transitionActive_ = false;
    }
    return;
  }

  // ---- 8. a different player: adopt, and animate unless it was manual -----
  if (desired != target_) {
    const SpectateSlot* desiredSlot = nullptr;
    slotFor(desired, slots, &desiredSlot);
    const bool hadTarget = target_ >= 0;
    target_ = desired;
    source_ = desiredSource;
    targetDeadRecorded_ = false;
    if (desiredSource == SpectateSource::Director) {
      ++directorPicks_;
    } else if (desiredSource == SpectateSource::StableOrder) {
      ++stablePicks_;
    }
    if (desiredSlot != nullptr && desiredSlot->hasOrigin) {
      if (desiredSource == SpectateSource::Manual || !hadTarget || !focusValid_) {
        // First target of the watch, or a manual pick: no window to animate.
        transitionActive_ = false;
        focus_[0] = desiredSlot->origin[0];
        focus_[1] = desiredSlot->origin[1];
        focus_[2] = desiredSlot->origin[2];
        focusValid_ = true;
      } else {
        ++handovers_;
        transitionStart_ = nowSeconds;
        transitionFrom_[0] = focus_[0];
        transitionFrom_[1] = focus_[1];
        transitionFrom_[2] = focus_[2];
        transitionFromValid_ = true;
        transitionActive_ = true;
      }
    }
  }

  // ---- 9. move the focus --------------------------------------------------
  const SpectateSlot* live = nullptr;
  const bool haveLive = target_ >= 0 && slotFor(target_, slots, &live) && live->hasOrigin;
  if (transitionActive_) {
    const double elapsed = nowSeconds - transitionStart_;
    double t = kHandoverSeconds > 0.0 ? elapsed / kHandoverSeconds : 1.0;
    if (!(t > 0.0)) t = 0.0; // a clock reading older than the start is a new epoch
    if (t >= 1.0) {
      transitionActive_ = false;
      t = 1.0;
    }
    if (haveLive) {
      // The end point follows the player every frame, so a handover to somebody
      // who is running still lands on them; only the start point is frozen.
      const double u = smoothstep(t);
      const float from[3] = {
        transitionFromValid_ ? transitionFrom_[0] : live->origin[0],
        transitionFromValid_ ? transitionFrom_[1] : live->origin[1],
        transitionFromValid_ ? transitionFrom_[2] : live->origin[2],
      };
      for (int axis = 0; axis < 3; ++axis) {
        focus_[axis] = static_cast<float>(from[axis] + (live->origin[axis] - from[axis]) * u);
      }
      focusValid_ = true;
    }
  } else if (haveLive) {
    focus_[0] = live->origin[0];
    focus_[1] = live->origin[1];
    focus_[2] = live->origin[2];
    focusValid_ = true;
  }
}

void SpectateDirector::refreshDead(const std::vector<SpectateSlot>& slots) {
  // Exactly the corpse rule update() step 2 applies, exposed so a draw path can
  // ask for THIS snapshot's dead set instead of inheriting the previous frame's.
  // The rule is repeated nowhere else: update() calls this method.
  deadIndices_.clear();
  for (const SpectateSlot& slot : slots) {
    if (slot.entityIndex < 0 || !slot.isPlayer) continue;
    // Hiding a body is a statement about the picture, so it needs POSITIVE
    // evidence: m_lifeState not 0, or a health reading at or below zero. A row
    // with neither is not a corpse, it is a row this snapshot never resolved --
    // such a row is still refused as a camera target (slotAlive is false), but
    // what gets drawn stays the draw path's decision, not this one.
    const bool certainDeath = (slot.hasLifeState && slot.lifeState != 0)
      || (slot.hasHealth && slot.health <= 0);
    if (!certainDeath) continue;
    deadIndices_.push_back(slot.entityIndex);
  }
  std::sort(deadIndices_.begin(), deadIndices_.end());
  hiddenDead_ = static_cast<std::uint32_t>(deadIndices_.size());
}
void SpectateDirector::pickManual(std::int32_t entityIndex) {
  manualPending_ = true;
  manualTarget_ = entityIndex;
}

bool SpectateDirector::hidesModel(std::int32_t entityIndex) const {
  if (entityIndex < 0) return false;
  return std::binary_search(deadIndices_.begin(), deadIndices_.end(), entityIndex);
}

double SpectateDirector::transitionProgress() const {
  if (!transitionActive_) return 0.0;
  if (kHandoverSeconds <= 0.0) return 1.0;
  double t = (now_ - transitionStart_) / kHandoverSeconds;
  if (!(t > 0.0)) return 0.0;
  if (t > 1.0) return 1.0;
  return t;
}

void SpectateDirector::focus(float out[3]) const {
  out[0] = focus_[0];
  out[1] = focus_[1];
  out[2] = focus_[2];
}

bool SpectateDirector::slotFor(std::int32_t entityIndex,
                               const std::vector<SpectateSlot>& slots,
                               SpectateSlot const** out) const {
  if (entityIndex < 0) return false;
  for (const SpectateSlot& slot : slots) {
    if (slot.entityIndex != entityIndex) continue;
    if (out != nullptr) *out = &slot;
    return true;
  }
  return false;
}

std::int32_t SpectateDirector::pickStable(const std::vector<SpectateSlot>& slots) const {
  // A stable order, not "whatever the map happened to hold": sort by entity index
  // and start after the body that just died, wrapping to the smallest index. Two
  // runs of the same demo therefore choose the same player, which is the only
  // form of "reproducible" that can be asserted frame by frame.
  std::vector<std::int32_t> candidates;
  for (const SpectateSlot& slot : slots) {
    if (slot.entityIndex < 0) continue;
    if (!slotObservable(slot) || !slotAlive(slot)) continue;
    candidates.push_back(slot.entityIndex);
  }
  if (candidates.empty()) return -1;
  std::sort(candidates.begin(), candidates.end());
  if (target_ >= 0) {
    for (const std::int32_t index : candidates) {
      if (index > target_) return index;
    }
  }
  return candidates.front();
}

std::string SpectateDirector::traceFields() const {
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer),
    "target=%d src=%s tr=%d prog=%.3f focus=%.1f,%.1f,%.1f deaths=%u handovers=%u"
    " notarget=%u resync=%u manual=%u rejected=%u director=%u stable=%u hidden=%u lastdeath=%d",
    static_cast<int>(target_), spectateSourceName(source_), transitionActive_ ? 1 : 0,
    transitionProgress(), static_cast<double>(focus_[0]), static_cast<double>(focus_[1]),
    static_cast<double>(focus_[2]), deaths_, handovers_, noTargetFrames_, resyncs_,
    manualPicks_, manualRejected_, directorPicks_, stablePicks_, hiddenDead_,
    static_cast<int>(lastDeathTick_));
  return std::string(buffer);
}

} // namespace tf2::native
