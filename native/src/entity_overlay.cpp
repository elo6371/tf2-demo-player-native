#include "entity_overlay.h"

#include "entity_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace tf2::native {
namespace {

// The same suffix matcher the transform path uses (entity_model.cpp). Kept here
// rather than exposed from there because this file must not be able to change
// what extractTransform reads: a second matcher that drifted would make the
// sample's team disagree with the mesh's team, which is exactly the defect the
// sample exists to catch.
bool suffixMatch(const std::string& name, const char* suffix) {
  const std::size_t length = std::strlen(suffix);
  if (name.size() == length) return name == suffix;
  if (name.size() > length && name.compare(name.size() - length, length, suffix) == 0) {
    return name[name.size() - length - 1] == '.';
  }
  return false;
}

const EntityPropertyValue* findProperty(const EntityState& state, const char* suffix) {
  const EntityPropertyValue* best = nullptr;
  std::string bestName;
  for (const auto& [name, value] : state.properties) {
    if (!suffixMatch(name, suffix)) continue;
    // The same preference rule extractTransform applies: freshest write first,
    // then the full-precision Local slot, then a stable name order. Duplicated
    // here only as an ordering of two rows this file already holds; the VALUES
    // all come from extractTransform.
    const bool better = !best
      || value.lastWriteTick > best->lastWriteTick
      || (value.lastWriteTick == best->lastWriteTick && name < bestName);
    if (better) { best = &value; bestName = name; }
  }
  return best;
}

bool readIntProperty(const EntityState& state, const char* suffix, std::int64_t& out) {
  const EntityPropertyValue* value = findProperty(state, suffix);
  if (value == nullptr) return false;
  if (value->type == SendPropType::Float) {
    if (!std::isfinite(value->x)) return false;
    out = static_cast<std::int64_t>(value->x);
    return true;
  }
  if (value->type != SendPropType::Int) return false;
  out = value->intValue;
  return true;
}

// A player is identified by CLASS NAME, never by a pinned class id: the id is a
// property of one recording's server class table and changes between demos.
bool isPlayerClassName(const std::string& className) {
  return className.find("TFPlayer") != std::string::npos
    || className.find("Player") != std::string::npos;
}

// Projectile identity, by name, for the same reason. The list is deliberately
// generous and matched on substrings a Source class name actually carries:
// T53 owns extending it, and this function is the single place to do that.
bool isProjectileClassName(const std::string& className) {
  static const char* const kMarkers[] = {
    "Projectile", "Grenade", "Rocket", "Pipebomb", "Arrow", "Flare", "Jar",
    "Ball", "Sticky", "Dynamite", "Nail", "Bolt",
  };
  for (const char* marker : kMarkers) {
    if (className.find(marker) != std::string::npos) return true;
  }
  return false;
}

// A ViewModel is never a player and never a projectile even though its class
// name contains "Player" on some recordings. Skipping it keeps the sample's
// candidate count honest.
bool isViewModelClassName(const std::string& className) {
  return className.find("ViewModel") != std::string::npos;
}

const char* classNameOf(const std::vector<ServerClassSchema>& schemas, std::int32_t classId) {
  if (classId < 0) return "";
  const auto index = static_cast<std::size_t>(classId);
  if (index >= schemas.size()) return "";
  return schemas[index].name.c_str();
}

} // namespace

const char* overlayStageName(OverlayStage stage) {
  switch (stage) {
    case OverlayStage::World: return "world";
    case OverlayStage::Water: return "water";
    case OverlayStage::EntityMarkers: return "entityMarkers";
    case OverlayStage::EntityModels: return "entityModels";
    case OverlayStage::ProjectileTracks: return "projectileTracks";
    case OverlayStage::Overlay: return "overlay";
    case OverlayStage::ViewModel: return "viewModel";
    case OverlayStage::Hud: return "hud";
  }
  return "unknown";
}

const char* overlayTeamName(OverlayTeam team) {
  switch (team) {
    case OverlayTeam::Unknown: return "unknown";
    case OverlayTeam::Red: return "red";
    case OverlayTeam::Blue: return "blue";
  }
  return "unknown";
}

OverlayTeam classifyOverlayTeam(bool hasTeam, std::int64_t teamRaw) {
  // Absent and out-of-range are the same ANSWER and two different REASONS: the
  // caller still has hasTeam and teamRaw to tell them apart, which is why the
  // sample carries all three fields.
  if (!hasTeam) return OverlayTeam::Unknown;
  if (teamRaw == kOverlayTeamRedRaw) return OverlayTeam::Red;
  if (teamRaw == kOverlayTeamBlueRaw) return OverlayTeam::Blue;
  return OverlayTeam::Unknown;
}

OverlayColor overlayTeamColor(OverlayTeam team) {
  OverlayColor colour;
  switch (team) {
    case OverlayTeam::Red:
      // A saturated red with enough green to stay readable against the warm
      // lightmaps this map ships; the pixel assertion pins the channel ORDER
      // (red > green and red > blue), not an exact triple.
      colour.rgba[0] = 0.92f; colour.rgba[1] = 0.16f; colour.rgba[2] = 0.14f; colour.rgba[3] = 1.0f;
      return colour;
    case OverlayTeam::Blue:
      colour.rgba[0] = 0.16f; colour.rgba[1] = 0.42f; colour.rgba[2] = 0.95f; colour.rgba[3] = 1.0f;
      return colour;
    case OverlayTeam::Unknown:
      // Neutral grey. Deliberately neither red-dominant nor blue-dominant: an
      // unknown team must be unable to pass a red/blue pixel assertion.
      colour.rgba[0] = 0.72f; colour.rgba[1] = 0.72f; colour.rgba[2] = 0.72f; colour.rgba[3] = 1.0f;
      return colour;
  }
  return colour;
}

int overlaySampleStride(OverlaySampleTier tier) {
  switch (tier) {
    case OverlaySampleTier::EveryTick: return 1;
    case OverlaySampleTier::EveryOtherTick: return 2;
    case OverlaySampleTier::EveryFourthTick: return 4;
  }
  return 1;
}

bool overlayTickSampled(OverlaySampleTier tier, std::int32_t tick) {
  const int stride = overlaySampleStride(tier);
  if (stride <= 1) return true;
  // A negative tick is refused rather than folded: the modulo of a negative
  // number is negative, and "-1 % 4 == 3" would silently make every negative
  // tick a sampled one under some strides and not others.
  if (tick < 0) return false;
  return (tick % stride) == 0;
}

const char* overlaySampleTierName(OverlaySampleTier tier) {
  switch (tier) {
    case OverlaySampleTier::EveryTick: return "everyTick";
    case OverlaySampleTier::EveryOtherTick: return "everyOtherTick";
    case OverlaySampleTier::EveryFourthTick: return "everyFourthTick";
  }
  return "everyTick";
}

const char* overlayPrimitiveKindName(OverlayPrimitiveKind kind) {
  switch (kind) {
    case OverlayPrimitiveKind::Silhouette: return "silhouette";
    case OverlayPrimitiveKind::Line: return "line";
    case OverlayPrimitiveKind::Marker: return "marker";
  }
  return "unknown";
}

const char* overlayDepthModeName(OverlayDepthMode mode) {
  switch (mode) {
    case OverlayDepthMode::XRay: return "xray";
    case OverlayDepthMode::DepthTested: return "depthTested";
  }
  return "unknown";
}

const char* overlaySnapshotFidelityName(OverlaySnapshotFidelity fidelity) {
  switch (fidelity) {
    case OverlaySnapshotFidelity::Unavailable: return "unavailable";
    case OverlaySnapshotFidelity::CheckpointFallback: return "checkpointFallback";
    case OverlaySnapshotFidelity::ExactTick: return "exactTick";
  }
  return "unavailable";
}

const char* overlayLifecycleTransitionName(OverlayLifecycleTransition transition) {
  switch (transition) {
    case OverlayLifecycleTransition::Appeared: return "appeared";
    case OverlayLifecycleTransition::Persisted: return "persisted";
    case OverlayLifecycleTransition::Vanished: return "vanished";
  }
  return "appeared";
}

std::vector<OverlayEntitySample> sampleOverlayEntities(
    const std::vector<EntityState>& statesByIndex,
    const std::vector<ServerClassSchema>& classSchemas,
    const std::unordered_map<std::uint16_t, std::string>& modelPrecache,
    OverlayEntityFilter filter,
    const OverlayClassSet* classSet,
    OverlaySampleStats* stats) {
  OverlaySampleStats local;
  OverlaySampleStats& counts = stats ? *stats : local;
  counts = OverlaySampleStats{};
  std::vector<OverlayEntitySample> samples;
  for (std::size_t index = 0; index < statesByIndex.size(); ++index) {
    if (index > 0xffffu) break;
    ++counts.entitiesSeen;
    const auto& state = statesByIndex[index];
    // classId < 0 means "no entity in this slot" -- a freed slot, not an entity
    // that has been sampled and found wanting.
    if (state.classId < 0) continue;
    const char* className = classNameOf(classSchemas, state.classId);
    if (isViewModelClassName(className)) { ++counts.viewModelsSkipped; continue; }
    const bool isPlayer = isPlayerClassName(className);
    bool selected = false;
    if (filter == OverlayEntityFilter::Players) {
      selected = isPlayer;
    } else if (classSet != nullptr) {
      // Exact class-name match against the caller's measured set. Exact, not a
      // substring rule: T53 measured that a keyword rule catches weapons that
      // sit at the origin while held and misses CTFGrenadePipebombProjectile
      // entirely.
      const std::string name(className);
      selected = std::find(classSet->classNames.begin(), classSet->classNames.end(), name)
        != classSet->classNames.end();
    } else {
      selected = isProjectileClassName(className);
    }
    if (!selected) continue;
    ++counts.candidatesSeen;

    OverlayEntitySample sample;
    sample.present = true;
    sample.entityIndex = static_cast<std::uint16_t>(index);
    sample.classId = state.classId;
    sample.className = className;
    sample.isPlayer = isPlayer;
    // Pose comes from the ONE transform rule the entity mesh uses. This is not
    // a convenience: a silhouette drawn at a second, hand-rolled origin would
    // be an outline around nothing, and the two readers would disagree exactly
    // where the freshness rule matters (the POV recordings).
    const ModelInstanceTransform transform = EntityModelResolver::extractTransform(state);
    sample.hasOrigin = transform.hasOrigin;
    sample.hasAngles = transform.hasAngles;
    if (transform.hasOrigin) {
      sample.origin[0] = transform.origin[0];
      sample.origin[1] = transform.origin[1];
      sample.origin[2] = transform.origin[2];
      ++counts.withOrigin;
    }
    if (transform.hasAngles) {
      sample.angles[0] = transform.angles[0];
      sample.angles[1] = transform.angles[1];
      sample.angles[2] = transform.angles[2];
      ++counts.withAngles;
    }
    std::int64_t teamRaw = 0;
    sample.hasTeam = readIntProperty(state, "m_iTeamNum", teamRaw);
    sample.teamRaw = teamRaw;
    sample.team = classifyOverlayTeam(sample.hasTeam, teamRaw);
    if (sample.team == OverlayTeam::Unknown) ++counts.teamUnknown; else ++counts.teamKnown;
    std::int64_t playerClass = 0;
    sample.hasPlayerClass = readIntProperty(state, "m_iClass", playerClass);
    sample.playerClass = playerClass;
    if (sample.hasPlayerClass) ++counts.playerClassKnown;

    // The model the entity's own m_nModelIndex names. Empty is a real answer --
    // an entity that names no model -- and is reported as a reason, not as an
    // error.
    std::int64_t modelIndex = 0;
    if (readIntProperty(state, "m_nModelIndex", modelIndex)
        && modelIndex >= 1 && modelIndex <= 0xffff) {
      const auto found = modelPrecache.find(static_cast<std::uint16_t>(modelIndex));
      if (found != modelPrecache.end() && !found->second.empty()) {
        sample.modelPath = found->second;
        ++counts.modelPathResolved;
      } else {
        sample.reason = "modelIndex is not declared in modelprecache";
      }
    } else {
      sample.reason = "no m_nModelIndex slot";
    }
    if (!sample.hasOrigin) {
      if (!sample.reason.empty()) sample.reason += "; ";
      sample.reason += "origin missing";
    } else if (!sample.hasTeam) {
      if (!sample.reason.empty()) sample.reason += "; ";
      sample.reason += "team slot absent";
    }
    ++counts.sampled;
    samples.push_back(std::move(sample));
  }
  return samples;
}

OverlaySnapshotResolution resolveOverlaySnapshot(
    const DemoNetworkSummary& summary,
    std::int32_t tick,
    std::vector<EntityState>& states) {
  OverlaySnapshotResolution resolution;
  resolution.requestedTick = tick;
  resolution.resolvedTick = tick;
  states.clear();
  // The shared accessor: the ONE query implementation, plus the honesty fields.
  // Nothing here re-implements the checkpoint/archive walk -- a second copy
  // would be free to pick a different checkpoint than the frame was drawn from.
  const auto status = queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolution.resolvedTick);
  resolution.stalenessTicks = tick - resolution.resolvedTick;
  switch (status) {
    case EntitySnapshotQueryStatus::Available:
      resolution.fidelity = OverlaySnapshotFidelity::ExactTick;
      resolution.reason = "replayed through the retained live window";
      break;
    case EntitySnapshotQueryStatus::Checkpoint:
      resolution.fidelity = OverlaySnapshotFidelity::CheckpointFallback;
      resolution.reason = "newest retained checkpoint at or before the tick";
      break;
    case EntitySnapshotQueryStatus::NoHistory:
      resolution.fidelity = OverlaySnapshotFidelity::Unavailable;
      resolution.reason = "the recording carries no entity history";
      states.clear();
      break;
    case EntitySnapshotQueryStatus::TickBeforeHistory:
      resolution.fidelity = OverlaySnapshotFidelity::Unavailable;
      resolution.reason = "the tick precedes every retained snapshot";
      states.clear();
      break;
    case EntitySnapshotQueryStatus::Gap:
      resolution.fidelity = OverlaySnapshotFidelity::Unavailable;
      resolution.reason = "the retained history has a gap at this tick";
      states.clear();
      break;
    case EntitySnapshotQueryStatus::DeltaBaseMissing:
      resolution.fidelity = OverlaySnapshotFidelity::Unavailable;
      resolution.reason = "a delta's base is no longer retained";
      states.clear();
      break;
  }
  if (resolution.fidelity == OverlaySnapshotFidelity::Unavailable) {
    resolution.resolvedTick = tick;
    resolution.stalenessTicks = 0;
  }
  return resolution;
}

std::vector<OverlayLifecycleEvent> OverlayEntityTracker::observe(
    std::int32_t tick,
    const std::vector<OverlayEntitySample>& samples,
    bool includePersisted) {
  std::vector<OverlayLifecycleEvent> events;
  // A tick fed twice is refused rather than re-applied: the tracker's answer
  // must be a function of the ticks, not of how many frames asked about one.
  if (haveTick_ && tick <= lastTick_) { ++duplicates_; return events; }
  const std::int32_t sinceLast = haveTick_ ? (tick - lastTick_) : 0;
  ++stats_.ticksFed;

  std::unordered_map<std::uint16_t, const OverlayEntitySample*> current;
  current.reserve(samples.size() * 2u + 1u);
  for (const auto& sample : samples) {
    if (!sample.present) continue;
    current[sample.entityIndex] = &sample;
  }

  for (const auto& [index, sample] : current) {
    const auto found = previous_.find(index);
    if (found == previous_.end()) {
      OverlayLifecycleEvent event;
      event.transition = OverlayLifecycleTransition::Appeared;
      event.entityIndex = index;
      event.classId = sample->classId;
      event.className = sample->className;
      event.isPlayer = sample->isPlayer;
      event.team = sample->team;
      event.tick = tick;
      event.ticksSinceLastSample = sinceLast;
      events.push_back(std::move(event));
      ++stats_.appeared;
    } else if (includePersisted) {
      OverlayLifecycleEvent event;
      event.transition = OverlayLifecycleTransition::Persisted;
      event.entityIndex = index;
      event.classId = sample->classId;
      event.className = sample->className;
      event.isPlayer = sample->isPlayer;
      event.team = sample->team;
      event.tick = tick;
      event.ticksSinceLastSample = sinceLast;
      events.push_back(std::move(event));
      ++stats_.persisted;
    }
  }
  for (const auto& [index, sample] : previous_) {
    if (current.find(index) != current.end()) continue;
    // The row the entity HAD, not a default one: "entity 12 (CTFPlayer, blue)
    // vanished" is a reading; "entity 12 vanished" is a number.
    OverlayLifecycleEvent event;
    event.transition = OverlayLifecycleTransition::Vanished;
    event.entityIndex = index;
    event.classId = sample.classId;
    event.className = sample.className;
    event.isPlayer = sample.isPlayer;
    event.team = sample.team;
    event.tick = tick;
    event.ticksSinceLastSample = sinceLast;
    events.push_back(std::move(event));
    ++stats_.vanished;
  }
  std::sort(events.begin(), events.end(),
            [](const OverlayLifecycleEvent& left, const OverlayLifecycleEvent& right) {
              if (left.tick != right.tick) return left.tick < right.tick;
              if (left.transition != right.transition) return left.transition < right.transition;
              return left.entityIndex < right.entityIndex;
            });

  previous_.clear();
  previous_.reserve(current.size() * 2u + 1u);
  for (const auto& [index, sample] : current) previous_[index] = *sample;
  haveTick_ = true;
  lastTick_ = tick;
  return events;
}

void OverlayEntityTracker::reset() {
  haveTick_ = false;
  lastTick_ = 0;
  previous_.clear();
  stats_ = OverlayLifecycleStats{};
  duplicates_ = 0;
}

OverlayDrawList buildOutlineDrawList(
    const std::vector<OverlayEntitySample>& samples,
    const std::vector<std::string>& knownMeshes,
    bool enabled,
    OverlayDepthMode depth) {
  OverlayDrawList list;
  list.enabled = enabled;
  list.depth = depth;
  std::unordered_set<std::string> known(knownMeshes.begin(), knownMeshes.end());
  for (const auto& sample : samples) {
    if (!sample.present) { ++list.rejectedNotPlayer; continue; }
    // A row that is not a player is counted rather than silently dropped: a
    // projectile filter reused by mistake then reads as a number instead of as
    // an empty frame.
    if (!sample.isPlayer) { ++list.rejectedNotPlayer; continue; }
    if (!enabled) { ++list.rejectedDisabled; continue; }
    if (!sample.hasOrigin) { ++list.rejectedNoOrigin; continue; }
    if (sample.modelPath.empty() || known.find(sample.modelPath) == known.end()) {
      // The cache key the entity pass would have used. A path that no uploaded
      // mesh answers is rejected here; drawing a fallback quad in team colour
      // would put a coloured square where a player is not.
      ++list.rejectedNoMesh;
      continue;
    }
    OverlayDrawItem item;
    item.kind = OverlayPrimitiveKind::Silhouette;
    item.entityIndex = sample.entityIndex;
    item.isPlayer = true;
    item.team = sample.team;
    item.color = overlayTeamColor(sample.team);
    item.cacheKey = sample.modelPath;
    item.origin[0] = sample.origin[0];
    item.origin[1] = sample.origin[1];
    item.origin[2] = sample.origin[2];
    item.hasAngles = sample.hasAngles;
    if (sample.hasAngles) {
      item.angles[0] = sample.angles[0];
      item.angles[1] = sample.angles[1];
      item.angles[2] = sample.angles[2];
    }
    ++list.submittedByTeam[static_cast<std::size_t>(sample.team)];
    list.items.push_back(std::move(item));
  }
  return list;
}

const char* overlaySlotOwnerName(OverlaySlotOwner owner) {
  switch (owner) {
    case OverlaySlotOwner::None: return "none";
    case OverlaySlotOwner::Outline: return "outline";
    case OverlaySlotOwner::Trajectory: return "trajectory";
  }
  return "none";
}

bool OverlayToggleState::acquire(OverlaySlotOwner owner) {
  if (owner == OverlaySlotOwner::None) { ++refusedForeign_; return false; }
  if (owner_ != OverlaySlotOwner::None && owner_ != owner) { ++refusedForeign_; return false; }
  if (owner_ == OverlaySlotOwner::None) ++acquisitions_;
  owner_ = owner;
  return true;
}

bool OverlayToggleState::release(OverlaySlotOwner owner) {
  if (owner == OverlaySlotOwner::None || owner_ != owner) { ++refusedForeign_; return false; }
  owner_ = OverlaySlotOwner::None;
  ++releases_;
  return true;
}

bool OverlayToggleState::set(OverlaySlotOwner who, bool value) {
  // Ownership is the whole point: a press from a feature that does not own the
  // slot is counted and ignored, so the frame another feature is measuring
  // cannot be changed out from under it.
  if (owner_ == OverlaySlotOwner::None || owner_ != who) { ++refusedForeign_; return enabled_; }
  ++presses_;
  if (value != enabled_) { enabled_ = value; ++stateChanges_; }
  return enabled_;
}

bool OverlayToggleState::toggle(OverlaySlotOwner who) {
  if (owner_ == OverlaySlotOwner::None || owner_ != who) { ++refusedForeign_; return enabled_; }
  return set(who, !enabled_);
}

bool overlayPassOrderValid(std::size_t overlayOrdinal, std::size_t viewModelOrdinal) {
  // Both ordinals must be set. A pass that never ran reports 0, and 0 < 0 is
  // false only because of this line -- without it a build in which the overlay
  // was never drawn would satisfy "the overlay drew before the weapon".
  if (overlayOrdinal == 0 || viewModelOrdinal == 0) return false;
  return overlayOrdinal < viewModelOrdinal;
}

bool overlayPixelMatchesTeam(std::uint8_t r, std::uint8_t g, std::uint8_t b, OverlayTeam team) {
  const int red = r, green = g, blue = b;
  const bool redDominant = red >= green + kOverlayTeamChannelMargin
    && red >= blue + kOverlayTeamChannelMargin && red >= kOverlayTeamChannelFloor;
  const bool blueDominant = blue >= red + kOverlayTeamChannelMargin
    && blue >= green + kOverlayTeamChannelMargin && blue >= kOverlayTeamChannelFloor;
  switch (team) {
    case OverlayTeam::Red: return redDominant;
    case OverlayTeam::Blue: return blueDominant;
    case OverlayTeam::Unknown: return overlayPixelIsNeutral(r, g, b);
  }
  return false;
}

bool overlayPixelIsNeutral(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  const int red = r, green = g, blue = b;
  // Neither channel dominance, stated as the negation of BOTH margins rather
  // than as "close to grey": the map's own warm albedo is neutral by this
  // predicate, which is correct -- the predicate only ever asks whether a
  // pixel could be mistaken for a team colour.
  const bool redDominant = red >= green + kOverlayTeamChannelMargin
    && red >= blue + kOverlayTeamChannelMargin && red >= kOverlayTeamChannelFloor;
  const bool blueDominant = blue >= red + kOverlayTeamChannelMargin
    && blue >= green + kOverlayTeamChannelMargin && blue >= kOverlayTeamChannelFloor;
  return !redDominant && !blueDominant;
}

} // namespace tf2::native
