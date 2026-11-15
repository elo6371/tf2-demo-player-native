#pragma once

// N13 step 1 (2026-11-15): the identity a `follow` command pins, and the
// vocabulary its trace line prints.
//
// WHY THIS IS NOT IN THE DIRECTOR: spectate_target.h owns a DECISION -- which
// player the camera watches -- and re-takes it on every frame on purpose: a dead
// target hands the camera over so the picture never rests on a corpse. A follow
// command is the opposite kind of object, a user's decision that has to survive
// the director deciding again, or "I pinned player 5" and "the camera happens to
// be on player 5" read the same in a trace. Keeping the pin here leaves the
// director untouched -- step 1 changes no camera behaviour at all -- and makes
// the pin a thing a gate can assert on its own.
//
// The struct carries a serial number even though no serial source exists yet
// (followSlotSerial in main.cpp says why): a CBaseHandle's serial is what tells
// "the same player" from "another player that reused that entity index", and
// writing the field now keeps the reconciliation gates honest about what they
// can and cannot verify in this step instead of pretending the case cannot
// happen.

#include <cstdint>

#include "spectate_target.h"

namespace tf2::native {

// One pinned follow target, field for field what the follow: line prints
// (design 2026-11-15 section 4).
struct FollowTarget {
  std::int32_t entityIndex = -1;
  // 0 = unknown. No per-player handle exists in the snapshot yet; see
  // followSlotSerial. Never written from a computed guess.
  std::uint32_t serial = 0;
  SpectateSource source = SpectateSource::None;
  // The user's pin. Dropped by the reconciliation gates, never by the director,
  // and never by a frame that simply had no snapshot.
  bool pinned = false;
  // An identity is recorded (entityIndex >= 0). Outlives the pin on purpose: the
  // line that reports a lost target still has to name it.
  bool valid = false;
};

// The state tokens of design section 2.2, printed verbatim in the follow:
// line's state= field. The left column of that table, not the "state=gone
// reason=..." form the design sketched: one token fits one field, and the
// reason= field already carries the cause.
enum class FollowState {
  Unset,
  Ok,
  GoneIndexOutOfRange,
  GoneNoSlot,
  GoneIndexReused,
  GoneNotObservable,
  Dead,
  Observer,
  NoSnapshot,
};

inline const char* followStateName(FollowState state) {
  switch (state) {
    case FollowState::Unset: return "unset";
    case FollowState::Ok: return "ok";
    case FollowState::GoneIndexOutOfRange: return "gone-index-out-of-range";
    case FollowState::GoneNoSlot: return "gone-no-slot";
    case FollowState::GoneIndexReused: return "gone-index-reused";
    case FollowState::GoneNotObservable: return "gone-not-observable";
    case FollowState::Dead: return "dead";
    case FollowState::Observer: return "observer";
    case FollowState::NoSnapshot: return "no-snapshot";
  }
  return "unset";
}

// What a REFUSING print puts in state=: the state was not re-decided, so it is
// deliberately not one of the tokens above. A refusal prints the entity the
// command named and this literal, which is how "nothing was applied" and "the
// pin really is unset" stay different readings.
inline constexpr const char* kFollowStateUnchanged = "unchanged";

// The reason tokens of design section 3, plus the three this step adds for
// causes that are not commands: pinned-dead (the pin died), director-refused
// (step 1 cannot yet outrank the relay -- see followFallbacks) and reconcile (a
// state the pin's own reconciliation produced).
enum class FollowReason {
  None,
  Ok,
  IndexOutOfRange,
  NoSlot,
  NotObservable,
  IsObserver,
  Dead,
  NoCandidates,
  NoPovEye,
  AlreadyFollowing,
  NotFollowing,
  PinnedDead,
  DirectorRefused,
  Reconcile,
};

inline const char* followReasonName(FollowReason reason) {
  switch (reason) {
    case FollowReason::None: return "none";
    case FollowReason::Ok: return "ok";
    case FollowReason::IndexOutOfRange: return "index-out-of-range";
    case FollowReason::NoSlot: return "no-slot";
    case FollowReason::NotObservable: return "not-observable";
    case FollowReason::IsObserver: return "is-observer";
    case FollowReason::Dead: return "dead";
    case FollowReason::NoCandidates: return "no-candidates";
    case FollowReason::NoPovEye: return "no-pov-eye";
    case FollowReason::AlreadyFollowing: return "already-following";
    case FollowReason::NotFollowing: return "not-following";
    case FollowReason::PinnedDead: return "pinned-dead";
    case FollowReason::DirectorRefused: return "director-refused";
    case FollowReason::Reconcile: return "reconcile";
  }
  return "none";
}

// The five commands of design section 3. The argument scan latches one of these
// and the frame that has a snapshot applies it; "--follow-target N" with no
// "--follow-action" means Select, because naming a target is asking to watch it.
enum class FollowAction { None, Select, Next, Prev, Unfollow, BackToPov };

} // namespace tf2::native
