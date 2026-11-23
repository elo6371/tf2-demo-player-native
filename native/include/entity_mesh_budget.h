#pragma once
// T85 P0: the entity-mesh budget.
//
// The product has FOUR independent budgets between "the demo names a model" and
// "the frame draws it", and until now every one of them was an anonymous
// constant that a model could fall past without a name attached to the loss:
//
//   A. prepare   main.cpp: at most 64 world-pass models are prepared (reserve(64)
//                 and "if (preparedEntityMeshes.size() >= 64) break").
//   B. cache     native_renderer_entity.cpp: the GPU mesh table holds at most 96
//                 keys and REFUSES a new key with E_OUTOFMEMORY once full. This
//                 table is shared by the world pass's prepared models, the
//                 overlay's nine class models and the third-person attach's
//                 on-demand supply, so it is the layer that was full when the
//                 flamethrower/sapper/warhorn world models were refused.
//   C. instances native_renderer_entity.cpp: at most 96 instances per update are
//                 accepted, and anything past it was silently dropped.
//   D. draw      main.cpp: at most 96 entity instances reach the draw list; the
//                 budget is split players first, then held-weapon attaches, then
//                 props.
//
// A and B are CAPACITY (how much can be resident at once); C and D are DRAW
// CAPACITY (how much one frame shows). They are different questions and this
// header is the one place that names both, so a refusal can say which budget it
// hit and a reading can say which one moved.
//
// The priority order is the delivery order the task asks for: a visible player
// body outranks the world model of the weapon a player is holding, which
// outranks the overlay's nine class models, which outranks every other world
// prop. First-person-only models (viewmodel rigs and their _arms parts) can
// never appear in a third-person frame, so they are the first thing a full
// budget should give up -- they are classified Deferred, not silently dropped.
#include <cstddef>
#include <cstdint>
#include <string>

namespace tf2::native {

enum class EntityMeshPriority : std::uint8_t {
  PlayerBody = 0,   // a player body the third-person camera can see
  HeldWeapon = 1,   // the world model of the weapon a player is holding
  Overlay = 2,      // the outline's nine class models (required overlay)
  WorldProp = 3,    // everything else the world pass asked for
  Deferred = 4,     // first-person-only geometry: never drawn in third person
};

inline constexpr std::size_t kEntityMeshPriorityCount = 5;

inline const char* entityMeshPriorityName(EntityMeshPriority priority) {
  switch (priority) {
    case EntityMeshPriority::PlayerBody: return "player";
    case EntityMeshPriority::HeldWeapon: return "weapon";
    case EntityMeshPriority::Overlay: return "overlay";
    case EntityMeshPriority::WorldProp: return "prop";
    case EntityMeshPriority::Deferred: return "firstPersonOnly";
  }
  return "?";
}

inline std::size_t entityMeshPriorityIndex(EntityMeshPriority priority) {
  const auto index = static_cast<std::size_t>(priority);
  return index < kEntityMeshPriorityCount ? index : kEntityMeshPriorityCount - 1;
}

// Can a model of this priority appear in a third-person frame at all? A
// first-person-only model is DEFERRED rather than refused: if every other
// candidate fit, the budget may still spend a slot on it, and a gate can see
// that it was not dropped for lack of room.
inline bool entityMeshPriorityDrawable(EntityMeshPriority priority) {
  return priority != EntityMeshPriority::Deferred;
}

// Classify a model path by what it is, using the path spelling the demo's
// precache table uses. This is a classification, not a whitelist: an unknown
// path is a world prop, which is the cheapest honest default.
//
// Why the name and not the asset list: the same path arrives from three
// different places (the demo's asset references, the class fallbacks, and the
// attach's m_iWorldModelIndex lookup), and the budget has to rank a model the
// same way no matter which of the three asked for it.
inline EntityMeshPriority classifyEntityMeshPath(const std::string& path) {
  std::string lowered;
  lowered.reserve(path.size());
  for (char c : path) {
    if (c == '\\') lowered.push_back('/');
    else if (c >= 'A' && c <= 'Z') lowered.push_back(static_cast<char>(c - 'A' + 'a'));
    else lowered.push_back(c);
  }
  const auto has = [&lowered](const char* needle) {
    return lowered.find(needle) != std::string::npos;
  };
  if (has("/v_models/") || has("_arms.mdl") || has("_arms.vmdl")) {
    return EntityMeshPriority::Deferred;
  }
  constexpr const char* kPlayerPrefix = "models/player/";
  // Review minor 4: sizeof() includes the NUL (15); the prefix a path is measured
  // against is 14 characters, and substr(15) on a 14-character path throws
  // std::out_of_range. Off-by-one here would classify nothing on the narrowest
  // legal path and abort the process instead.
  constexpr std::size_t kPlayerPrefixLength = sizeof("models/player/") - 1;
  if (lowered.rfind(kPlayerPrefix, 0) == 0) {
    const std::string rest = lowered.substr(kPlayerPrefixLength);
    // models/player/<class>.mdl is a body; models/player/items/... is a hat or
    // a cosmetic and is NOT a body -- ranking a hat above a held weapon would
    // spend a body's slot on something the third-person path does not draw.
    return rest.find('/') == std::string::npos
      ? EntityMeshPriority::PlayerBody : EntityMeshPriority::WorldProp;
  }
  if (has("/weapons/")) return EntityMeshPriority::HeldWeapon;
  return EntityMeshPriority::WorldProp;
}

}  // namespace tf2::native
