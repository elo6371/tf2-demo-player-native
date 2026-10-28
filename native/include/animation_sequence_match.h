#pragma once
// T61.1: how a sequence needle ("run", "walk", "move", "idle", "stand") is
// allowed to match a sequence label, and the one selector that uses it.
//
// The defect this header exists to prevent: the product matched a needle with
// std::string::find, so "walk" matched inside "airwalk" and every moving player
// was handed a reload-in-air cycle (ReloadAirwalk_PRIMARY and friends). A
// substring test answers "does this word appear anywhere in that string", which
// is not the question being asked.
//
// SCOPE. Both rules below are used by the MOVING side only. The idle side keeps
// the pre-T61.1 case-blind substring match, because the review of the first
// draft of this change drew the task boundary there: this task fixes "walk"
// matching inside "airwalk" and changes no other motion rule. The matching idle
// defect ("stand" inside "AttackStand") was measured on a live demo -- every
// idle sample read AttackStand_PRIMARY -- but it belongs to its own task and
// must not be verified together with this one. sequenceLabelHasSubstring below
// exists so the idle path keeps the old behaviour through THIS header instead
// of through a second copy of the matcher living somewhere else.
//
// The two moving rules, both measured against the nine class tables before they
// were written (evidence/t57-player-model/raw/player_models.json):
//
//   1. TOKEN boundary. A needle matches only where it is a whole token, both
//      sides delimited by a non-alphanumeric character, the end of the label,
//      or a CamelCase transition.
//   2. PRIMARY token. Among the labels a needle still matches, the ones whose
//      FIRST token is the needle are preferred. This is what separates
//      run_PRIMARY from layer_AttackStandMelee_runS_arms: "run" is a whole
//      token in both, but only the first is a run cycle, and the second is an
//      arm overlay that happens to carry "run" in its name.
//
// Rule 2 is not decoration. Measured over all nine classes, rule 1 alone
// removes airwalk but leaves five classes on an arm-layer clip
// (layer_AttackStandMelee_runS_arms, layer_gesture_primary_help_runN_armL,
// armslayer_PDA_run_fire), i.e. it swaps one wrong clip for another. With rule
// 2 every class lands on its own locomotion cycle: run_PRIMARY in eight
// classes and run_SECONDARY for the Spy.
//
// ONE SELECTOR. chooseSequenceByNeedles below is the whole of the choice: the
// needle lists, the two match modes, the one-frame-pose staircase and the
// return order. The product (native/src/main.cpp) and the probe
// (native/tools/model_pose_probe.cpp) both call it, so the probe cannot report
// a sequence the product would not have played. The first draft of this change
// had the candidate order and the needle list written twice, once in each
// translation unit; a review of that draft named it, correctly, as a second
// place for the same rule to drift.
//
// Deliberately NOT here: an activity-number test. Source assigns the same
// activity 0 to AIM matrix poses, airwalk variants and run cycles alike in
// these tables, so the field cannot separate them even though
// tf2::native::AnimationSequence carries it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

namespace sequence_label_detail {

inline bool asciiAlnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
inline bool asciiUpper(char c) { return c >= 'A' && c <= 'Z'; }
inline bool asciiLower(char c) { return c >= 'a' && c <= 'z'; }
inline char asciiLowerCopy(char c) {
  return asciiUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace sequence_label_detail

// True when needle occurs in label as a whole token, case-insensitive.
inline bool sequenceLabelHasToken(const std::string& label, const std::string& needle) {
  using namespace sequence_label_detail;
  if (needle.empty() || needle.size() > label.size()) return false;
  for (std::size_t start = 0; start + needle.size() <= label.size(); ++start) {
    bool same = true;
    for (std::size_t k = 0; k < needle.size(); ++k) {
      if (asciiLowerCopy(label[start + k]) != asciiLowerCopy(needle[k])) {
        same = false;
        break;
      }
    }
    if (!same) continue;
    const std::size_t end = start + needle.size();
    const bool leftOk = start == 0 || !asciiAlnum(label[start - 1])
      || (asciiUpper(label[start]) && asciiLower(label[start - 1]));
    const bool rightOk = end >= label.size() || !asciiAlnum(label[end])
      || (asciiUpper(label[end]) && asciiLower(label[end - 1]));
    if (leftOk && rightOk) return true;
  }
  return false;
}

// True when needle occurs anywhere in label, case-insensitive. This is the
// pre-T61.1 test, kept ONLY for the idle side so that withdrawing the idle rule
// change restores the old pick exactly instead of approximating it. Never use
// it for the moving needles: for those it IS the defect.
inline bool sequenceLabelHasSubstring(const std::string& label, const std::string& needle) {
  using namespace sequence_label_detail;
  if (needle.empty() || needle.size() > label.size()) return false;
  for (std::size_t start = 0; start + needle.size() <= label.size(); ++start) {
    bool same = true;
    for (std::size_t k = 0; k < needle.size(); ++k) {
      if (asciiLowerCopy(label[start + k]) != asciiLowerCopy(needle[k])) {
        same = false;
        break;
      }
    }
    if (same) return true;
  }
  return false;
}

// The label's first token, lowercased: run_PRIMARY -> "run",
// AttackStand_PRIMARY -> "attack", armslayer_PDA_run_fire -> "armslayer".
inline std::string sequenceLabelLeadingToken(const std::string& label) {
  using namespace sequence_label_detail;
  std::string out;
  for (std::size_t i = 0; i < label.size(); ++i) {
    const char c = label[i];
    if (!asciiAlnum(c)) break;
    if (i > 0 && asciiUpper(c) && asciiLower(label[i - 1])) break;
    out.push_back(asciiLowerCopy(c));
  }
  return out;
}

// True when the label's primary action is the needle itself. Callers pass the
// lowercase needle literals, so no case folding is needed on that side.
inline bool sequenceLabelPrimaryTokenMatches(const std::string& label, const std::string& needle) {
  return !needle.empty() && sequenceLabelLeadingToken(label) == needle;
}

// The needle lists are here, not in the callers: a candidate list that lives in
// two translation units is a list that can disagree with itself.
inline constexpr const char* kMovingNeedles[] = {"run", "walk", "move"};
inline constexpr std::size_t kMovingNeedleCount = 3;
inline constexpr const char* kIdleNeedles[] = {"idle", "stand"};
inline constexpr std::size_t kIdleNeedleCount = 2;

enum class SequenceMatchMode {
  // The pre-T61.1 test: a needle matches anywhere inside the label.
  Substring,
  // T61.1: whole tokens only, and a label whose FIRST token is the needle wins
  // over one that merely contains it. Used for the moving needles.
  WholeToken,
};

// The selector. labelAt(i) returns sequence i's label, frameCountAt(i) returns
// sequence i's frame count (0 when the sequence has no animation descriptor).
// Tiers, in the order they are tried:
//   animatedPrimary, animated, posedPrimary, posed, fallbackPrimary, fallback
// where 'animated' means frameCount > 1, 'posed' means frameCount == 1 and
// 'fallback' means the sequence carries no descriptor at all. Within a tier the
// earliest sequence index wins, and the 'primary' tiers only exist in WholeToken
// mode -- which is what keeps the idle pick byte-identical to its pre-T61.1
// behaviour.
template <typename LabelAt, typename FrameCountAt>
std::int32_t chooseSequenceByNeedles(std::size_t sequenceCount,
    const LabelAt& labelAt, const FrameCountAt& frameCountAt,
    const char* const* needles, std::size_t needleCount,
    SequenceMatchMode mode, std::string* label) {
  const bool wholeToken = mode == SequenceMatchMode::WholeToken;
  std::int32_t animated = -1;
  std::int32_t animatedPrimary = -1;
  std::int32_t posed = -1;
  std::int32_t posedPrimary = -1;
  std::int32_t fallback = -1;
  std::int32_t fallbackPrimary = -1;
  std::string animatedLabel, animatedPrimaryLabel;
  std::string posedLabel, posedPrimaryLabel;
  std::string fallbackLabel, fallbackPrimaryLabel;
  for (std::size_t index = 0; index < sequenceCount; ++index) {
    const std::string& candidate = labelAt(index);
    const std::int32_t frameCount = frameCountAt(index);
    const bool hasDescriptor = frameCount > 0;
    for (std::size_t needle = 0; needle < needleCount; ++needle) {
      const char* const needleText = needles[needle];
      const bool matched = wholeToken
        ? sequenceLabelHasToken(candidate, needleText)
        : sequenceLabelHasSubstring(candidate, needleText);
      if (!matched) continue;
      const bool primary = wholeToken
        && sequenceLabelPrimaryTokenMatches(candidate, needleText);
      if (hasDescriptor) {
        // A label match can still be a one-frame pose: PRIMARY_aimmatrix_run is
        // a static aim pose with frameCount 1, and cycling through it would
        // stand still no matter what the clock says. Keep the animated pick
        // when one exists and only fall back to a single-frame pose.
        if (frameCount > 1) {
          if (primary && animatedPrimary < 0) {
            animatedPrimary = static_cast<std::int32_t>(index);
            animatedPrimaryLabel = candidate;
          }
          if (animated < 0) {
            animated = static_cast<std::int32_t>(index);
            animatedLabel = candidate;
          }
        } else {
          if (primary && posedPrimary < 0) {
            posedPrimary = static_cast<std::int32_t>(index);
            posedPrimaryLabel = candidate;
          }
          if (posed < 0) {
            posed = static_cast<std::int32_t>(index);
            posedLabel = candidate;
          }
        }
      } else {
        if (primary && fallbackPrimary < 0) {
          fallbackPrimary = static_cast<std::int32_t>(index);
          fallbackPrimaryLabel = candidate;
        }
        if (fallback < 0) {
          fallback = static_cast<std::int32_t>(index);
          fallbackLabel = candidate;
        }
      }
    }
  }
  if (animatedPrimary >= 0) {
    if (label != nullptr) *label = animatedPrimaryLabel;
    return animatedPrimary;
  }
  if (animated >= 0) {
    if (label != nullptr) *label = animatedLabel;
    return animated;
  }
  if (posedPrimary >= 0) {
    if (label != nullptr) *label = posedPrimaryLabel;
    return posedPrimary;
  }
  if (posed >= 0) {
    if (label != nullptr) *label = posedLabel;
    return posed;
  }
  if (fallbackPrimary >= 0) {
    if (label != nullptr) *label = fallbackPrimaryLabel;
    return fallbackPrimary;
  }
  if (fallback >= 0 && label != nullptr) *label = fallbackLabel;
  return fallback;
}

// Every candidate, in preference order, with duplicates removed. The selector
// above answers 'which one wins'; this answers 'in what order would you try
// them', which is what a caller needs when it applies an extra filter of its
// own. The probe is that caller: it can only use a sequence whose four
// quarter-cycle poses differ, so it walks this order and keeps the first
// candidate that survives that test.
//
// Bands, in order: labels whose LEADING token is the needle, then labels that
// carry the needle as a whole token (or, in Substring mode, anywhere in the
// label). There is no separate exact-match band: on these tables no label
// equals a needle at all (measured over all nine classes), and a caller that
// already knows the selector's answer tries it first anyway.
//
// SEQUENCE-major, the same nesting the selector uses: the sequence index is the
// outer loop and the needle loop only supplies the predicate, so within a band
// the EARLIEST sequence wins. The first version of this function nested the
// needle outside the index, which put "the first sequence that matches needle
// 0" ahead of "an earlier sequence that matches needle 1" -- so a caller
// walking this list could try, and report a pose for, a sequence the selector
// would never have chosen first (T61.1 review 2, P1). model_pose_probe's
// --matcher-selftest asserts the property that pins this: the list's head is
// the selector's own first choice, and masking a prefix of the list leaves the
// selector's answer equal to the next entry.
template <typename LabelAt>
std::vector<std::size_t> rankSequenceCandidates(std::size_t sequenceCount,
    const LabelAt& labelAt, const char* const* needles, std::size_t needleCount,
    SequenceMatchMode mode) {
  const bool wholeToken = mode == SequenceMatchMode::WholeToken;
  std::vector<std::size_t> ranked;
  std::vector<bool> taken(sequenceCount, false);
  const auto push = [&](std::size_t index) {
    if (index >= sequenceCount || taken[index]) return;
    taken[index] = true;
    ranked.push_back(index);
  };
  for (std::size_t index = 0; index < sequenceCount; ++index) {
    const std::string& candidate = labelAt(index);
    for (std::size_t needle = 0; needle < needleCount; ++needle) {
      const char* const needleText = needles[needle];
      const bool leading = wholeToken
        ? sequenceLabelPrimaryTokenMatches(candidate, needleText)
        : sequenceLabelHasSubstring(candidate, needleText);
      if (leading) { push(index); break; }
    }
  }
  for (std::size_t index = 0; index < sequenceCount; ++index) {
    const std::string& candidate = labelAt(index);
    for (std::size_t needle = 0; needle < needleCount; ++needle) {
      const char* const needleText = needles[needle];
      const bool matched = wholeToken
        ? sequenceLabelHasToken(candidate, needleText)
        : sequenceLabelHasSubstring(candidate, needleText);
      if (matched) { push(index); break; }
    }
  }
  return ranked;
}

}  // namespace tf2::native
