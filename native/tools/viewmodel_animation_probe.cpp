// ViewModel animation input: does the demo carry one, and does it map onto the
// model the ViewModel pass actually draws?
//
// WHY THIS PROBE EXISTS
//   T1 asked the same question about players and got "no": CTFPlayer networks
//   none of m_nSequence / m_flCycle / m_flPlaybackRate, so a player's skeleton
//   cannot be driven from a demo. The ViewModel is a different class, so the
//   answer had to be measured rather than inherited. It is different: TF2's
//   server creates a networked CTFViewModel per player and drives its sequence.
//
// THE FIELDS ARE COUNTED SEPARATELY, ON PURPOSE
//   Their supply differs. DT_BaseViewModel carries m_nSequence and
//   m_flPlaybackRate and does NOT carry m_flCycle -- so of the three fields the
//   task named, one is not on the wire at all. A single "animation properties"
//   count would have hidden exactly that, which is why `vmfield` prints one line
//   per class with one column per field, and why the three target classes are
//   printed whether or not they have any of them.
//
// THE CENSUS READS THE SNAPSHOT, NOT A PROPERTY DUMP
//   The obvious instrument -- dump every entity's m_vecOrigin at a list of ticks
//   and count the class names -- reports ZERO ViewModel entities on a demo that
//   has hundreds, because DT_BaseViewModel has no m_vecOrigin and the dump only
//   prints entities that carry one. That is not a hypothesis; it happened while
//   this probe was being written, and it nearly became a "the input does not
//   exist" conclusion. The census here reads `EntityState::classId`, which every
//   entity in a snapshot has.
//
// THE TWO QUESTIONS ARE KEPT APART
//   1. Does the input exist?                -> vmfield / vmcensus / vmanim
//   2. Does it map onto the drawn model?    -> vmmapping
//   The entity names a c_<class>_arms model. The pass draws a v_*.mdl named on
//   its own command line. Those are two different index spaces, so the observed
//   sequence is reported AGAINST the drawn model's own sequence table and is
//   never applied to it: an in-range-looking value from the wrong table is a
//   coincidence, not a mapping. `vmmapping` exists to make that a reading.
//
// stdout is one JSON object. Every reading goes to stderr, matching the other
// probes in this tree.

#include "asset_root.h"
#include "demo_header.h"
#include "entity_model.h"
#include "model_loader.h"
#include "viewmodel.h"
#include "vpk_archive.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// The sequence-descriptor stride the animation decoder assumes. Repeated here
// (rather than shared) because the raw dump below has to walk the file without
// the decoder, which is the only thing that makes it a second opinion.
constexpr std::size_t kSeqDescStride = 212;

// A NUL-terminated run of printable ASCII at an absolute file offset, or empty.
// Used only by the raw-header dump below, whose whole point is to be readable
// without trusting this decoder's own decoded fields.
std::string rawAscii(const std::vector<std::uint8_t>& bytes, std::size_t offset) {  if (offset >= bytes.size()) return {};
  std::string text;
  for (std::size_t index = offset; index < bytes.size() && bytes[index] != 0; ++index) {
    const std::uint8_t character = bytes[index];
    if (character < 32 || character > 126) return {};
    if (text.size() >= 40) return text;
    text.push_back(static_cast<char>(character));
  }
  return text;
}

std::int32_t rawWord(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) return -1;
  std::int32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char character : value) {
    switch (character) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 32) out += '?';
        else out += character;
    }
  }
  return out;
}

const char* snapshotStatusName(tf2::native::EntitySnapshotQueryStatus status) {
  using tf2::native::EntitySnapshotQueryStatus;
  switch (status) {
    case EntitySnapshotQueryStatus::Available: return "available";
    case EntitySnapshotQueryStatus::Checkpoint: return "checkpoint";
    case EntitySnapshotQueryStatus::NoHistory: return "no-history";
    case EntitySnapshotQueryStatus::TickBeforeHistory: return "before-window";
    case EntitySnapshotQueryStatus::Gap: return "gap";
    case EntitySnapshotQueryStatus::DeltaBaseMissing: return "delta-base-missing";
  }
  return "unknown";
}

void usage() {
  std::cerr <<
    "usage: viewmodel_animation_probe --tf-root <tf> --demo <demo.dem>\n"
    "         [--sample-start <tick>] [--sample-step <ticks>] [--sample-count <n>]\n"
    "         [--drawn-model <v_*.mdl>] [--mutation <name>] [--self-test]\n"
    "       viewmodel_animation_probe --tf-root <tf> --model-sequences <path.mdl>\n";
}

// A model path is a ViewModel path only when its FILE NAME starts with `v_`.
// The product's own copy of this rule is file-local in native/src/viewmodel.cpp,
// so a second copy here would be a drift hazard: the two would disagree exactly
// when the rule is the thing under test. The self-test therefore does not trust
// this function -- it drives the product's entry point and requires the same
// verdict, and the gate runs the self-test.
bool isViewModelPath(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
  return file.size() > 6 && file.rfind("v_", 0) == 0 && file.size() >= 4
    && file.compare(file.size() - 4, 4, ".mdl") == 0;
}

// ---------------------------------------------------------------------------
// The field-supply reading
// ---------------------------------------------------------------------------

struct FieldSupply {
  std::size_t sequence = 0;
  std::size_t cycle = 0;
  std::size_t rate = 0;
  std::size_t pose = 0;
};

// One line per class, one column per field, matched on the LEAF name only: the
// owner-table prefix varies with the include chain, and the question is whether
// the slot exists in this class's wire format at all.
FieldSupply countFields(const tf2::native::SendTableSchema& table) {
  FieldSupply supply;
  for (const auto& prop : table.flattenedProps) {
    const std::size_t dot = prop.name.rfind('.');
    const std::string leaf = dot == std::string::npos ? prop.name : prop.name.substr(dot + 1);
    if (leaf == "m_nSequence") ++supply.sequence;
    else if (leaf == "m_flCycle") ++supply.cycle;
    else if (leaf == "m_flPlaybackRate") ++supply.rate;
    else if (leaf == "m_flPoseParameter") ++supply.pose;
  }
  return supply;
}

// ---------------------------------------------------------------------------
// Property reads
// ---------------------------------------------------------------------------

struct IntRead {
  bool found = false;
  std::int64_t value = 0;
  std::int32_t lastWriteTick = -1;
};

// The freshness-aware read the product uses for entity properties. Kept as a
// call into the same ranker rather than a re-implementation, so a probe that
// disagrees with the product is impossible by construction.
IntRead readInt(const tf2::native::EntityState& state, const char* leaf) {
  IntRead read;
  const auto ranked = tf2::native::rankPropertyCandidates(state, leaf);
  if (ranked.empty()) return read;
  const auto& name = ranked.front().name;
  const auto found = state.properties.find(name);
  if (found == state.properties.end()) return read;
  read.found = true;
  read.value = found->second.intValue;
  read.lastWriteTick = ranked.front().lastWriteTick;
  return read;
}

struct FloatRead {
  bool found = false;
  double value = 0.0;
};

// m_flPlaybackRate is a Float sendprop; reading it through the int accessor
// would report the raw bit pattern. The value is taken from x, which is where
// the decoder stores a scalar float.
FloatRead readFloat(const tf2::native::EntityState& state, const char* leaf) {
  FloatRead read;
  const auto ranked = tf2::native::rankPropertyCandidates(state, leaf);
  if (ranked.empty()) return read;
  const auto found = state.properties.find(ranked.front().name);
  if (found == state.properties.end()) return read;
  read.found = true;
  read.value = static_cast<double>(found->second.x);
  return read;
}

// ---------------------------------------------------------------------------
// The observation series
// ---------------------------------------------------------------------------

struct Observation {
  std::int32_t tick = 0;
  int entity = -1;
  std::int64_t sequence = -1;
  double rate = 0.0;
  std::int64_t modelIndex = -1;
  std::string modelPath;
  std::int64_t owner = -1;
  std::int64_t weapon = -1;
};

// One series per entity, recording only the samples where something changed.
// "The cursor moves" and "the picture moves" are two different readings; this is
// the first one, and it is deliberately the weaker of the two.
class SequenceSeries {
public:
  // Returns true when this observation was a change and was recorded.
  bool observe(const Observation& observation) {
    const auto found = lastSequenceSeen_.find(observation.entity);
    const bool changed = found == lastSequenceSeen_.end()
      || found->second != observation.sequence;
    if (changed) {
      ++changes_;
      lastSequenceSeen_[observation.entity] = observation.sequence;
      samples_.push_back(observation);
      distinctSequences_.insert(observation.sequence);
      if (observation.sequence < minSequence_) minSequence_ = observation.sequence;
      if (observation.sequence > maxSequence_) maxSequence_ = observation.sequence;
    }
    ++observations_;
    entities_.insert(observation.entity);
    rates_.insert(static_cast<std::int64_t>(std::llround(observation.rate * 100.0)));
    if (!observation.modelPath.empty()) modelPaths_.insert(observation.modelPath);
    return changed;
  }

  std::size_t observations() const { return observations_; }
  std::size_t changes() const { return changes_; }
  const std::vector<Observation>& samples() const { return samples_; }
  std::size_t entities() const { return entities_.size(); }
  std::size_t distinctSequences() const { return distinctSequences_.size(); }
  std::size_t distinctRates() const { return rates_.size(); }
  std::size_t distinctModelPaths() const { return modelPaths_.size(); }
  std::int64_t minSequence() const { return minSequence_; }
  std::int64_t maxSequence() const { return maxSequence_; }
  std::size_t modelsThatAreViewModels() const {
    std::size_t count = 0;
    for (const auto& path : modelPaths_) if (isViewModelPath(path)) ++count;
    return count;
  }

private:
  std::size_t observations_ = 0;
  std::size_t changes_ = 0;
  std::map<int, std::int64_t> lastSequenceSeen_;
  std::vector<Observation> samples_;
  std::set<int> entities_;
  std::set<std::int64_t> distinctSequences_;
  std::set<std::int64_t> rates_;
  std::set<std::string> modelPaths_;
  std::int64_t minSequence_ = 0;
  std::int64_t maxSequence_ = 0;
};

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------
//
// Each one breaks a different reading, so a gate can name the assertion that has
// to go red for it. A mutation that only changed a counter nothing asserts on
// would prove nothing.
enum class Mutation {
  None,
  CensusSkipViewModel,   // the census never counts the class under test
  SequenceFrozen,        // the cursor is reported as never moving
  FieldSupplyCollapse,   // no class is reported as carrying the fields
  CycleInvented,         // m_flCycle is reported present when it is not
  ModelClassFlip,        // the entity's model is reported as a v_* viewmodel
};

Mutation parseMutation(const std::string& name, bool& ok) {
  ok = true;
  if (name == "none") return Mutation::None;
  if (name == "census-skip-viewmodel") return Mutation::CensusSkipViewModel;
  if (name == "sequence-frozen") return Mutation::SequenceFrozen;
  if (name == "field-supply-collapse") return Mutation::FieldSupplyCollapse;
  if (name == "cycle-invented") return Mutation::CycleInvented;
  if (name == "model-class-flip") return Mutation::ModelClassFlip;
  ok = false;
  return Mutation::None;
}

const char* mutationName(Mutation mutation) {
  switch (mutation) {
    case Mutation::None: return "none";
    case Mutation::CensusSkipViewModel: return "census-skip-viewmodel";
    case Mutation::SequenceFrozen: return "sequence-frozen";
    case Mutation::FieldSupplyCollapse: return "field-supply-collapse";
    case Mutation::CycleInvented: return "cycle-invented";
    case Mutation::ModelClassFlip: return "model-class-flip";
  }
  return "none";
}

// ---------------------------------------------------------------------------
// Range classification
// ---------------------------------------------------------------------------

// -1 when the drawn model's table is not known, 1 when the sequence indexes it,
// 0 when it does not. Three outcomes, not two: "not measured" must never be
// readable as "in range".
int sequenceInRange(std::int64_t sequence, std::int64_t sequenceCount) {
  if (sequenceCount < 0 || sequence < 0) return -1;
  return sequence < sequenceCount ? 1 : 0;
}

// ---------------------------------------------------------------------------
// The fixture battery's machine-readable account
// ---------------------------------------------------------------------------
//
// One line per fixture, printed whether the fixture behaved or not, because a gate
// that asserts on a line which is only printed when things go right is a gate
// that cannot see the thing it exists to see. The line carries the verdict, the
// proof's own reading, and the two counts a gate needs to tell "the pose covers
// the skeleton" from "the skeleton was never measured".
struct ProofReport {
  std::size_t fixtures = 0;
  std::size_t accepted = 0;
  std::size_t missing = 0;
  std::size_t modelMismatch = 0;
  std::size_t outOfRange = 0;
  std::size_t boneMismatch = 0;

  void record(const char* name, const tf2::native::ViewModelAnimationInput& input,
              const tf2::native::ViewModelRequest& model,
              const tf2::native::ViewModelAnimationOutcome& outcome) {
    ++fixtures;
    switch (outcome.status) {
      case tf2::native::ViewModelAnimationStatus::Ok: ++accepted; break;
      case tf2::native::ViewModelAnimationStatus::ModelMismatch: ++modelMismatch; break;
      case tf2::native::ViewModelAnimationStatus::SequenceOutOfRange: ++outOfRange; break;
      case tf2::native::ViewModelAnimationStatus::BoneMismatch: ++boneMismatch; break;
      default: ++missing; break;
    }
    // `reason` is printed last because it is several words long: every field
    // before it can be read positionally, and the reason is the tail.
    std::fprintf(stderr,
      "vmproof fixture=%s status=%s modelMatch=%d inRange=%d bindPose=%d matrices=%zu bones=%zu "
      "owner=%d viewEntity=%d modelIndex=%d drawn=%s reason=%s\n",
      name, tf2::native::viewModelAnimationStatusName(outcome.status),
      outcome.entityModelMatchesDrawn ? 1 : 0, outcome.sequenceInRange ? 1 : 0,
      outcome.bindPose ? 1 : 0,
      outcome.skinMatrices.size(), model.bones.size(),
      input.ownerEntity, input.viewEntity, input.modelIndex,
      model.path.empty() ? "-" : model.path.c_str(),
      outcome.reason.empty() ? "-" : outcome.reason.c_str());
  }

  void summary() const {
    std::fprintf(stderr,
      "vmproof-summary fixtures=%zu accepted=%zu missing=%zu modelMismatch=%zu "
      "outOfRange=%zu boneMismatch=%zu\n",
      fixtures, accepted, missing, modelMismatch, outOfRange, boneMismatch);
  }
};

// ---------------------------------------------------------------------------
// Self-test -- pure logic, no demo needed
// ---------------------------------------------------------------------------

int selfTest(const tf2::native::AssetRoot* root) {
  int failures = 0;
  auto check = [&failures](bool condition, const char* what) {
    if (condition) {
      std::fprintf(stderr, "  OK   %s\n", what);
    } else {
      std::fprintf(stderr, "  FAIL %s\n", what);
      ++failures;
    }
  };

  // 1. The v_ rule must agree with the product's, including the file-name-only
  //    part: a c_*_arms model sitting in a v_* directory is still not a v_ model.
  check(isViewModelPath("models/weapons/v_models/v_scattergun_scout.mdl"), "v_ file name is a viewmodel");
  check(!isViewModelPath("models/weapons/c_models/c_soldier_arms.mdl"), "c_ file name is not a viewmodel");
  check(!isViewModelPath("models/v_models/c_soldier_arms.mdl"), "the directory does not decide it");
  check(!isViewModelPath("models/weapons/v_models/v_scattergun_scout.vvd"), "extension is part of the rule");

  // 2. Range classification: three outcomes, and "not measured" is its own.
  check(sequenceInRange(0, 7) == 1, "0 is inside a 7-sequence table");
  check(sequenceInRange(6, 7) == 1, "the last index is inside");
  check(sequenceInRange(7, 7) == 0, "one past the end is outside");
  check(sequenceInRange(16, 7) == 0, "16 is outside a 7-sequence table");
  check(sequenceInRange(16, -1) == -1, "an unknown table is not 'in range'");

  // 3. The series counts changes, not samples. A cursor that repeats must not
  //    read as motion, and a cursor that moves must not read as still.
  {
    SequenceSeries series;
    const std::int64_t values[] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
      Observation observation;
      observation.tick = i;
      observation.entity = 1;
      observation.sequence = values[i];
      series.observe(observation);
    }
    check(series.observations() == 4, "four samples were seen");
    check(series.changes() == 1, "a constant sequence is one change");
    check(series.distinctSequences() == 1, "a constant sequence has one distinct value");
  }
  {
    SequenceSeries series;
    const std::int64_t values[] = {0, 16, 7, 0, 15, 15, 2, 9};
    for (int i = 0; i < 8; ++i) {
      Observation observation;
      observation.tick = i;
      observation.entity = 1;
      observation.sequence = values[i];
      series.observe(observation);
    }
    check(series.changes() == 7, "the observed series has seven changes");
    check(series.distinctSequences() == 6, "the observed series has six distinct values");
    check(series.minSequence() == 0 && series.maxSequence() == 16, "the observed range is 0..16");
  }
  // Per-entity, not global: two entities alternating the same value are still
  // each constant, and a global "last value" would report four changes.
  {
    SequenceSeries series;
    for (int i = 0; i < 4; ++i) {
      Observation observation;
      observation.tick = i;
      observation.entity = i % 2;
      observation.sequence = 5;
      series.observe(observation);
    }
    check(series.changes() == 2, "each entity contributes its own first sample");
    check(series.entities() == 2, "two entities were tracked");
  }

  // 4. The field-supply counter matches on the leaf, so an owner-table prefix
  //    change cannot make a field appear or disappear.
  {
    tf2::native::SendTableSchema table;
    table.flattenedProps.push_back({tf2::native::SendPropType::Int, "DT_BaseAnimating.m_nSequence"});
    table.flattenedProps.push_back({tf2::native::SendPropType::Float, "DT_BaseViewModel.m_flPlaybackRate"});
    table.flattenedProps.push_back({tf2::native::SendPropType::Int, "DT_BaseEntity.m_nModelIndex"});
    const auto supply = countFields(table);
    check(supply.sequence == 1, "the sequence slot is counted through its prefix");
    check(supply.rate == 1, "the rate slot is counted through its prefix");
    check(supply.cycle == 0, "an absent slot counts zero, not one");
  }

  // The fixture battery's account, declared outside the `--tf-root` guard so the
  // summary can be printed (and asserted) whether or not the battery ran: a
  // summary that only appears when the battery ran cannot report a battery that
  // did not.
  //
  // Every fixture's verdict goes into a machine-readable line, so a gate can
  // assert on the battery rather than on one chosen case. The names are the
  // battery's own order and the count is asserted below: a fixture inserted
  // without a name would shift the labels visibly instead of silently keeping a
  // stale one, and the assertion is what turns that from a convention into a
  // check.
  const char* const fixtureNames[] = {
    "no-entity", "no-owner", "wrong-owner", "no-model-index", "model-index-zero",
    "model-unresolved", "model-mismatch", "drawn-matched", "path-separator",
    "no-sequence", "negative-sequence", "sequence-past-own-table", "sequence-in-range-unbound",
    "accepted",
    "bone-mismatch", "no-binding", "unresolved-drawn", "cycle-witness", "cycle-applied",
  };
  constexpr std::size_t fixtureNameCount = sizeof(fixtureNames) / sizeof(fixtureNames[0]);
  ProofReport report;
  std::size_t fixtureOrdinal = 0;

  // 5. Agreement with the product, through the product's own entry point. The
  //    product's rule is file-local, so the only honest way to test agreement is
  //    to ask the product: a path the rule rejects must come back naming exactly
  //    that reason, and a path it accepts must not. Without a TF root the
  //    positive half cannot run (the product would go on to read a VPK), and the
  //    self-test says so out loud instead of quietly testing half the rule.
  if (root != nullptr) {
    const auto rejected = tf2::native::buildViewModelRequest(
      *root, "models/weapons/c_models/c_soldier_arms.mdl", tf2::native::ViewModelHand::Right);
    check(rejected.reason == "path is not a viewmodel",
          "the product rejects a c_*_arms path with exactly that reason");
    const auto accepted = tf2::native::buildViewModelRequest(
      *root, "models/weapons/v_models/v_scattergun_scout.mdl", tf2::native::ViewModelHand::Right);
    check(accepted.reason != "path is not a viewmodel",
          "the product does not reject a v_* path");
    check(isViewModelPath("models/weapons/v_models/v_scattergun_scout.mdl")
            && !isViewModelPath("models/weapons/c_models/c_soldier_arms.mdl"),
          "this probe's copy of the rule agrees with the product on both");

    // 6. The companion proof, driven through the product's own entry point.
    //
    //    Six conditions, four refusals, one fixture per condition. Each fixture
    //    differs from the accepting input by exactly one field, so an assertion
    //    that moves says which field moved it -- a fixture that changed two things
    //    at once could not say which one the contract reads.
    //
    //    The pairing is the REAL one: the model on the left is the composite the
    //    entity's own `m_nModelIndex` names, and the sequence is measured against
    //    ITS table. The first round measured the same sequence against
    //    `v_scattergun_scout`'s seven-entry table, which is why its refusals named
    //    a table nobody was drawing from.
    const auto drawn = tf2::native::buildViewModelCompanionRequest(
      *root, "models/weapons/c_models/c_soldier_arms.mdl", tf2::native::ViewModelHand::Right);
    check(drawn.status == tf2::native::ViewModelStatus::Ok,
          "the product resolves the entity's own model");
    check(!drawn.sequenceLabels.empty(), "the drawn model has a sequence table to be in or out of");
    check(!drawn.bones.empty(), "the drawn model has a skeleton");

    // The companion entry point's own test, and it is two claims. It must accept
    // a `c_*` path -- the `--viewmodel` rule refuses exactly that path, so a
    // companion that resolved here through the other entry point would be
    // impossible. And it must be the SAME parser: the same file read twice must
    // yield the same table and the same skeleton, or the two entry points would
    // disagree about one file and only one of them would be drawing it.
    const auto companionSameFile = tf2::native::buildViewModelCompanionRequest(
      *root, "models/weapons/c_models/c_soldier_arms.mdl", tf2::native::ViewModelHand::Right);
    check(companionSameFile.sequenceLabels.size() == drawn.sequenceLabels.size()
            && companionSameFile.bones.size() == drawn.bones.size(),
          "the companion entry point parses the file the same way twice");
    const auto stillRefusedByTheVPath = tf2::native::buildViewModelRequest(
      *root, "models/weapons/c_models/c_soldier_arms.mdl", tf2::native::ViewModelHand::Right);
    check(stillRefusedByTheVPath.reason == "path is not a viewmodel",
          "the --viewmodel entry point still refuses the same c_*_arms path");

    // A companion path that does not resolve is the "model file is missing" case,
    // and it has to fail in the request rather than in the contract: a contract
    // handed an unresolved model has nothing to measure a sequence against.
    const auto missingCompanion = tf2::native::buildViewModelCompanionRequest(
      *root, "models/weapons/c_models/c_probe_absent_arms.mdl", tf2::native::ViewModelHand::Right);
    check(missingCompanion.status == tf2::native::ViewModelStatus::Failed,
          "a companion whose file is absent does not resolve");
    check(!missingCompanion.reason.empty(),
          "the missing companion says why, rather than resolving to an empty model");

    // The `v_*` model the first round drew, kept as the OTHER table: it has its
    // own sequence count, and the mismatch fixture needs a drawn model that is
    // not the entity's own but still legal to draw.
    const auto other = tf2::native::buildViewModelRequest(
      *root, "models/weapons/v_models/v_scattergun_scout.mdl", tf2::native::ViewModelHand::Right);
    check(other.status == tf2::native::ViewModelStatus::Ok,
          "the product resolves the v_* model as well");
    const auto otherDrawn = tf2::native::buildViewModelCompanionRequest(
      *root, other.path, tf2::native::ViewModelHand::Right);

    tf2::native::AnimationBindingCache bindings;
    bindings.reset(root);
    tf2::native::AnimationInputStatus bindingStatus = tf2::native::AnimationInputStatus::ModelMissing;
    std::string bindingReason;
    // The cache hands out pointers INTO its own vector, and a miss pushes an entry
    // -- so a second resolve for a different model can reallocate and leave the
    // first pointer dangling. Measured here on 2026-10-12: with the two resolves
    // in the other order the companion's binding read `anims.animations.empty()`
    // and every accepting fixture came back `animation source carries no
    // animations`, which is exactly the shape of a wrong answer that looks like a
    // finding. Both entries are therefore inserted FIRST and the pointers taken
    // afterwards, where every call is a cache hit and nothing is pushed.
    bindings.resolve(other.path, bindingStatus, bindingReason);
    bindings.resolve(drawn.path, bindingStatus, bindingReason);
    const auto* otherBinding = bindings.resolve(other.path, bindingStatus, bindingReason);
    const auto* binding = bindings.resolve(drawn.path, bindingStatus, bindingReason);
    check(binding != nullptr && otherBinding != nullptr,
          "both the companion and the v_* model have an animation binding");
    check(binding != otherBinding,
          "the two models are two cache entries, not one");
    if (binding != nullptr) {
      check(binding->renderBones == drawn.bones.size(),
            "the binding's render skeleton is the drawn model's skeleton");
    }

    tf2::native::AnimationSampleRequest timing;
    timing.tick = 0;
    timing.tickRate = 30.0f;

    // Every fixture's verdict goes into a machine-readable line, so a gate can
    // assert on the battery rather than on one chosen case.
    const auto resolve = [&](const tf2::native::ViewModelAnimationInput& input,
                             const tf2::native::ViewModelRequest& model,
                             const tf2::native::AnimationBinding* bound) {
      const auto outcome = tf2::native::resolveViewModelAnimationInput(input, model, bound, timing);
      const char* name = fixtureOrdinal < fixtureNameCount ? fixtureNames[fixtureOrdinal] : "unnamed";
      ++fixtureOrdinal;
      report.record(name, input, model, outcome);
      return outcome;
    };

    // The accepting input. Every fixture below is this, one field changed.
    tf2::native::ViewModelAnimationInput input;
    input.entityPresent = true;
    input.entityIndex = 7;
    input.hasOwner = true;
    input.ownerEntity = 42;          // the handle's low 11 bits
    input.viewEntity = 42;           // what svc_SetView named
    input.hasModelIndex = true;
    input.modelIndex = 100;          // a demo precache slot
    input.entityModelPath = drawn.path;
    input.hasSequence = true;
    input.sequence = 0;

    // (a) the two absences, kept apart: no entity at all, and an entity whose
    //     owner is somebody else.
    {
      auto fixture = input;
      fixture.entityPresent = false;
      fixture.entityIndex = -1;
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "no owning entity resolves to missing");
      check(outcome.bindPose, "missing leaves the bind pose");
      check(outcome.reason.find("no viewmodel entity") != std::string::npos,
            "missing names which of the two absences it was");
    }
    {
      auto fixture = input;
      fixture.hasOwner = false;
      fixture.ownerEntity = -1;
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "an entity with no m_hOwner resolves to missing");
      check(outcome.reason.find("m_hOwner") != std::string::npos,
            "the absent owner is named");
    }
    {
      // The wrong-owner fixture: the entity names a DIFFERENT owner. This is the
      // case the lookup above already filters, which is why the contract's own
      // check has to be provable from a fixture instead.
      auto fixture = input;
      fixture.ownerEntity = 43;
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "an entity owned by another entity resolves to missing");
      check(outcome.bindPose, "a wrong owner leaves the bind pose");
      check(outcome.reason.find("owned by entity 43") != std::string::npos
              && outcome.reason.find("view entity 42") != std::string::npos,
            "the wrong owner names both entities");
    }
    // (b) the model index: absent, zero, and present-but-unresolved. Zero is not
    //     the same refusal as unresolved -- the first is an entity that named no
    //     model, the second an index the demo's precache does not carry.
    {
      auto fixture = input;
      fixture.hasModelIndex = false;
      fixture.modelIndex = -1;
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "an entity with no m_nModelIndex resolves to missing");
      check(outcome.reason.find("m_nModelIndex") != std::string::npos,
            "the absent model index is named");
    }
    {
      auto fixture = input;
      fixture.modelIndex = 0;
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "m_nModelIndex 0 resolves to missing, not to a model");
      check(outcome.reason.find("names no model") != std::string::npos,
            "model index 0 is refused as naming no model");
    }
    {
      auto fixture = input;
      fixture.modelIndex = 4095;
      fixture.entityModelPath.clear();
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "an index the precache does not carry resolves to missing");
      check(outcome.reason.find("model_precache") != std::string::npos,
            "the unresolved index names the precache");
    }
    // (c) the entity's own model is not the model the pass draws -- the refusal
    //     this round exists for. Sequence 0 is legal for the drawn `v_*` table, so
    //     only the path equality is wrong: this is exactly the frame the first
    //     round accepted as a coincidence.
    {
      auto fixture = input;
      fixture.entityModelPath = drawn.path;
      const auto outcome = resolve(fixture, otherDrawn, otherBinding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::ModelMismatch,
            "a drawn model that is not the entity's own resolves to modelMismatch");
      check(outcome.bindPose, "a model mismatch leaves the bind pose");
      check(!outcome.entityModelMatchesDrawn, "the mismatch reading is reported as false");
      check(!outcome.sequenceInRange,
            "a model mismatch never reaches the range question, so it is not in range");
      check(outcome.reason.find("c_soldier_arms.mdl") != std::string::npos
              && outcome.reason.find("v_scattergun_scout.mdl") != std::string::npos,
            "the mismatch names both models");
      // The same input, one field changed: the entity's own model becomes the one
      // being drawn, and the refusal turns into an acceptance. Without this pair
      // the mismatch could be a constant.
      auto matched = fixture;
      matched.entityModelPath = other.path;
      const auto accepted = resolve(matched, otherDrawn, otherBinding);
      check(accepted.status == tf2::native::ViewModelAnimationStatus::Ok,
            "the same input against a matching drawn model resolves to ok");
    }
    // A path the demo wrote with the other separator and mixed case is the same
    // model. Both sides are normalised, and this is the fixture that says so: a
    // raw comparison would refuse a correct pairing on a cosmetic detail.
    {
      auto fixture = input;
      fixture.entityModelPath = "models\\weapons\\c_models\\c_soldier_arms.mdl";
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Ok,
            "a backslash-separated entity path resolves to ok");
      check(outcome.entityModelMatchesDrawn, "the normalised comparison reports a match");
    }
    // (d) the sequence, against the entity's OWN table.
    {
      auto fixture = input;
      fixture.hasSequence = false;
      fixture.sequence = -1;
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "an entity with no m_nSequence resolves to missing, not to sequence 0");
      check(outcome.reason.find("m_nSequence") != std::string::npos,
            "the absent sequence is named");
    }
    {
      auto fixture = input;
      fixture.sequence = -1;
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "a negative sequence resolves to missing");
      check(outcome.reason.find("negative") != std::string::npos,
            "the negative sequence is named as negative");
    }
    {
      auto fixture = input;
      fixture.sequence = static_cast<int>(drawn.sequenceLabels.size());
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::SequenceOutOfRange,
            "one past the entity's own table resolves to sequenceOutOfRange");
      check(!outcome.sequenceInRange, "an out-of-range sequence is not reported as in range");
      check(outcome.bindPose, "an out-of-range sequence leaves the bind pose");
      check(outcome.reason.find(std::to_string(drawn.sequenceLabels.size())) != std::string::npos,
            "the refusal names the entity's own table's size");
      // The two tables really are different sizes, so "out of range of the v_*
      // table but inside the entity's own" is a value that exists. This is the
      // measured fact the round rests on, asserted rather than assumed.
      check(drawn.sequenceLabels.size() != other.sequenceLabels.size(),
            "the entity's own table and the v_* table are different sizes");
    }
    // (e) inside the entity's own table and still naming no pose -- the shape the
    //     pinned recordings actually show. Sequence 1 sits inside the companion
    //     model's own table and reaches no animation descriptor at all
    //     (`vmproof-rawblend` counts 53 of the 54 entries with no blend block), so
    //     the range question and the pose question must be able to answer
    //     differently. Folded into one reading, the recording's own state would be
    //     indistinguishable from an index defect, and a defect is what this
    //     round is supposed to catch.
    {
      auto fixture = input;
      fixture.sequence = 1;
      const auto outcome = resolve(fixture, drawn, binding);
      check(outcome.sequenceInRange,
            "a sequence inside the entity's own table reads as in range");
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "and still resolves to missing, because that entry names no pose");
      check(outcome.bindPose, "an entry with no pose leaves the bind pose");
      check(outcome.entityModelMatchesDrawn,
            "the model proof is unaffected by the pose being absent");
    }
    // (f) in range -- the acceptance branch, which has to be reachable or (d) is
    //     a constant with extra steps
    input.sequence = 0;
    const auto outcomeInRange = resolve(input, drawn, binding);
    check(outcomeInRange.status == tf2::native::ViewModelAnimationStatus::Ok,
          "sequence 0 resolves to ok");
    check(!outcomeInRange.bindPose, "ok does not leave the bind pose");
    check(outcomeInRange.entityModelMatchesDrawn, "ok reports the model match");
    check(outcomeInRange.sequenceInRange, "ok reports the sequence as inside the entity's own table");
    check(outcomeInRange.skinMatrices.size() == drawn.bones.size(),
          "the accepted pose covers the drawn skeleton exactly");
    const auto acceptedOutcome = outcomeInRange;

    // (f2) bone mismatch: the same accepted input against a skeleton one bone
    //     longer. This is the guard the standalone --model path applies, and the
    //     only way to reach it with real assets is to ask for the wrong skeleton.
    {
      tf2::native::ViewModelRequest longerSkeleton = drawn;
      longerSkeleton.bones.resize(drawn.bones.size() + 1);
      const auto outcome = resolve(input, longerSkeleton, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::BoneMismatch,
            "a skeleton one bone longer resolves to boneMismatch");
      check(outcome.bindPose, "a bone mismatch leaves the bind pose");
    }
    // (g) a drawn model with no animation data at all
    {
      const auto outcome = resolve(input, drawn, nullptr);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "a drawn model with no animation data resolves to missing");
    }
    // (h) a drawn model that was never resolved. Without the guard in the
    //     resolver an empty sequence table makes every sequence "past the end",
    //     and a resource failure would be reported as a sequence problem.
    {
      tf2::native::ViewModelRequest unresolved;
      unresolved.status = tf2::native::ViewModelStatus::Failed;
      unresolved.reason = "viewmodel was not found";
      const auto outcome = resolve(input, unresolved, binding);
      check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
            "an unresolved drawn model resolves to missing, not sequenceOutOfRange");
      check(outcome.reason.find("viewmodel was not found") != std::string::npos,
            "the unresolved reason is carried through");
    }
    // (i) m_flCycle: DT_BaseViewModel has none, so the field exists to report the
    //     absence. A cycle that IS supplied has to reach the decoder rather than
    //     being dropped, or "absent" and "ignored" would look the same.
    //
    //     The witness sequence is SEARCHED for, not hardcoded. The index that
    //     worked for the `v_*` model is 1, and on the companion model sequence 1
    //     carries no animation binding at all -- hardcoding either one would turn
    //     the witness into a fact about a file instead of a fact about the
    //     decoder, and the failure would surface as "the cycle is ignored".
    // What either model's own table offers, COUNTED rather than sampled. This is
    // the premise the cycle witness needs, and it is reported instead of assumed
    // because the two models do not answer it the same way: the `v_*` file is a
    // multi-frame animation set, and the composite arms file is a table of
    // one-frame poses -- one per weapon configuration. A witness hardcoded to the
    // `v_*` answer would read as "the cycle is ignored" on the model this round
    // actually draws, and a witness hardcoded to the companion's answer would
    // stop covering the branch that samples a frame at all.
    struct TableShape {
      std::size_t sequences = 0;
      std::size_t animated = 0;    // sequences that reach an animation descriptor
      std::size_t multiFrame = 0;  // ... of which more than one frame
      std::int32_t maxFrames = 0;
      int firstMultiFrame = -1;
      int firstMultiFrameFrames = 0;
    };
    const auto shapeOf = [](const tf2::native::AnimationBinding* candidate) {
      TableShape shape;
      if (candidate == nullptr) return shape;
      shape.sequences = candidate->anims.sequences.size();
      for (std::size_t index = 0; index < candidate->anims.sequences.size(); ++index) {
        const std::int32_t animDesc = candidate->anims.sequences[index].animDesc;
        if (animDesc < 0
            || static_cast<std::size_t>(animDesc) >= candidate->anims.animations.size()) continue;
        ++shape.animated;
        const std::int32_t frames =
          candidate->anims.animations[static_cast<std::size_t>(animDesc)].frameCount;
        if (frames > shape.maxFrames) shape.maxFrames = frames;
        if (frames > 1) {
          ++shape.multiFrame;
          if (shape.firstMultiFrame < 0) {
            shape.firstMultiFrame = static_cast<int>(index);
            shape.firstMultiFrameFrames = frames;
          }
        }
      }
      return shape;
    };
    const TableShape companionShape = shapeOf(binding);
    const TableShape controlShape = shapeOf(otherBinding);
    const std::size_t companionAnimated = companionShape.animated;
    const std::size_t companionMultiFrame = companionShape.multiFrame;
    const std::int32_t companionMaxFrames = companionShape.maxFrames;
    const int witnessSequence = controlShape.firstMultiFrame;
    const int witnessFrames = controlShape.firstMultiFrameFrames;
    std::fprintf(stderr,
      "vmproof-companion path=%s animPath=%s separate=%d includes=%zu sequences=%zu animations=%zu "
      "animated=%zu multiFrame=%zu maxFrames=%d\n",
      drawn.path.c_str(),
      binding != nullptr ? binding->animPath.c_str() : "-",
      binding != nullptr && binding->separateAnimSource ? 1 : 0,
      binding != nullptr ? binding->anims.includes.size() : 0,
      binding != nullptr ? binding->anims.sequences.size() : 0,
      binding != nullptr ? binding->anims.animations.size() : 0,
      companionAnimated, companionMultiFrame, companionMaxFrames);
    if (binding != nullptr) {
      for (const auto& include : binding->anims.includes) {
        std::fprintf(stderr, "vmproof-include path=%s include=%s\n", drawn.path.c_str(), include.c_str());
      }
    }
    // The same measurement on the `v_*` control, because the two files disagree
    // about it and the disagreement is the whole reason the cycle witness had to
    // move: the control is a multi-frame animation set, and the companion is not.
    std::fprintf(stderr,
      "vmproof-control path=%s sequences=%zu animations=%zu animated=%zu multiFrame=%zu "
      "maxFrames=%d witness=%d witnessFrames=%d\n",
      other.path.c_str(), controlShape.sequences,
      otherBinding != nullptr ? otherBinding->anims.animations.size() : 0,
      controlShape.animated, controlShape.multiFrame, controlShape.maxFrames,
      witnessSequence, witnessFrames);
    // The raw file, word for word, read from the bytes this decoder just parsed
    // rather than from the fields it decoded out of them. "One animation, and
    // every sequence but one carrying no binding" is a large claim about a model
    // the task is about to draw, and it is the kind of claim a reader cannot
    // check by rerunning the same decoder -- so the header's own counters, the
    // sequence table's own words and the strings they point at are printed
    // beside it. The offsets named here are the v44+ layout this decoder
    // assumes; if that assumption were wrong, these words would not spell a
    // sequence label.
    if (binding != nullptr && binding->anims.bytes.size() > 400) {
      const auto& raw = binding->anims.bytes;
      const std::int32_t rawAnimCount = rawWord(raw, 180);
      const std::int32_t rawAnimIndex = rawWord(raw, 184);
      const std::int32_t rawSeqCount = rawWord(raw, 188);
      const std::int32_t rawSeqIndex = rawWord(raw, 192);
      std::fprintf(stderr,
        "vmproof-rawhdr file=%zu declaredLength=%d animCount=%d animIndex=%d seqCount=%d seqIndex=%d\n",
        raw.size(), rawWord(raw, 76), rawAnimCount, rawAnimIndex, rawSeqCount, rawSeqIndex);
      const std::int32_t shown = std::min<std::int32_t>(rawSeqCount, 3);
      for (std::int32_t index = 0; index < shown; ++index) {
        const std::size_t base = static_cast<std::size_t>(rawSeqIndex)
          + static_cast<std::size_t>(index) * kSeqDescStride;
        const std::int32_t at4 = rawWord(raw, base + 4);
        const std::int32_t at8 = rawWord(raw, base + 8);
        std::fprintf(stderr,
          "vmproof-rawseq index=%d w0=%d w4=%d w8=%d w12=%d w16=%d w56=%d w60=%d w72=%d w76=%d "
          "strAt4=%s strAt8=%s\n",
          index, rawWord(raw, base), at4, at8, rawWord(raw, base + 12), rawWord(raw, base + 16),
          rawWord(raw, base + 56), rawWord(raw, base + 60), rawWord(raw, base + 72), rawWord(raw, base + 76),
          rawAscii(raw, base + (at4 > 0 ? static_cast<std::size_t>(at4) : 0)).c_str(),
          rawAscii(raw, base + (at8 > 0 ? static_cast<std::size_t>(at8) : 0)).c_str());
      }
      std::size_t blendZero = 0;
      std::size_t pointerZero = 0;
      for (std::size_t index = 0; index < binding->anims.sequences.size(); ++index) {
        const std::size_t base = static_cast<std::size_t>(rawSeqIndex) + index * kSeqDescStride;
        if (rawWord(raw, base + 56) <= 0) ++blendZero;
        if (rawWord(raw, base + 60) <= 0) ++pointerZero;
      }
      std::fprintf(stderr, "vmproof-rawblend sequences=%zu word56NonPositive=%zu word60NonPositive=%zu\n",
                   binding->anims.sequences.size(), blendZero, pointerZero);
    }
    // The companion's own table, asserted as DATA instead of papered over with a
    // witness it cannot supply. Measured on this tree, and printed raw above: 54
    // sequences, one animation descriptor, every sequence but one carrying no
    // blend block at all, and the one that does carrying a single frame. So there
    // is no motion inside a companion sequence to witness, and the honest thing to
    // pin is that fact -- a witness that cannot exist must not be replaced by a
    // witness that reports nothing.
    check(companionShape.animated == 1,
          "exactly one of the companion model's sequences reaches an animation descriptor");
    check(companionShape.multiFrame == 0,
          "no companion sequence carries more than one frame");
    check(companionShape.maxFrames == 1,
          "the companion's single binding is a one-frame pose");
    // The cycle witness therefore runs on the `v_*` control, which IS a
    // multi-frame set: the branch that samples a frame from a supplied cycle
    // stays covered by a live witness even though the model this round draws
    // cannot exercise it. Which model the witness ran on is printed, so the two
    // models cannot be confused for one another in the log.
    check(witnessSequence >= 0,
          "the v_* control model has a sequence with more than one frame");
    if (witnessSequence >= 0) {
      tf2::native::ViewModelAnimationInput cycled = input;
      cycled.entityModelPath = other.path;
      cycled.sequence = witnessSequence;
      const auto plain = resolve(cycled, otherDrawn, otherBinding);
      check(plain.status == tf2::native::ViewModelAnimationStatus::Ok,
            "the witness sequence is accepted, so the cycle reading is on a live branch");
      check(plain.entityModelMatchesDrawn,
            "the witness runs on a proven pair, not on a mismatched one");
      check(plain.frameCount == witnessFrames,
            "the witness sequence reports the frame count its animation carries");
      check(plain.frameCount > 1, "the witness sequence has more than one frame");
      cycled.hasCycle = true;
      cycled.cycle = 0.5f;
      const auto halfway = resolve(cycled, otherDrawn, otherBinding);
      check(halfway.status == tf2::native::ViewModelAnimationStatus::Ok,
            "a supplied cycle does not change the status");
      check(halfway.frame != plain.frame,
            "a supplied cycle moves the sampled frame");
      check(halfway.skinMatrices.size() == otherDrawn.bones.size(),
            "the cycle witness covers the control's skeleton exactly");
    }
    // The companion's accepted pose, read once more for the reading it carries:
    // the sampling path ran on the model this round draws, and what it can report
    // is the one frame that model has. Without this the companion's accepted path
    // would only be read with the frame count unasserted.
    check(acceptedOutcome.frameCount == companionMaxFrames,
          "the companion's accepted sequence reports the single frame it carries");
  } else {
    std::fprintf(stderr,
                 "  NOTE the product cross-check did not run: no --tf-root was given, so\n"
                 "       the accepting half of the rule was not tested against the product\n");
  }

  if (root != nullptr) {
    check(fixtureOrdinal == fixtureNameCount,
          "every fixture in the companion battery has a name in the report");
    report.summary();
  }

  std::fprintf(stderr, "self-test failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Model sequence count -- for ANY model, not only v_*
// ---------------------------------------------------------------------------
//
// The product refuses a non-v_ path, and that refusal is deliberate. It is also
// exactly what makes the c_<class>_arms model -- the one the ViewModel entity
// actually names -- unmeasurable through the product path. This mode reads it,
// because "how many sequences does the model the entity names have?" is the
// question that decides whether the two index spaces really are different.
int reportModelSequences(const tf2::native::AssetRoot& root, const std::string& path) {
  const auto candidate = tf2::native::ModelLoader::resolveAsset(root, path);
  tf2::native::ModelInspection inspection;
  std::string reason;
  if (candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose) {
    inspection = tf2::native::ModelLoader::inspect(candidate.looseMdl);
  } else if (candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk
             && candidate.vpkArchives.size() == 1) {
    tf2::native::VpkArchive archive;
    std::string error;
    if (!archive.open(candidate.vpkArchives.front(), &error)) reason = error;
    else inspection = tf2::native::ModelLoader::inspectVpk(archive, path);
  } else {
    reason = "model was not found";
  }
  const bool readable = inspection.mdl.signatureValid && inspection.metadata.valid;
  // The animation shape of the same file, taken from the binding the product
  // itself would use. Sequence count alone cannot answer the question this round
  // turns on -- an index can be inside a table and still name no pose -- so the
  // count of sequences that reach a descriptor, the count with more than one
  // frame, and the LOWEST sequence index that is animated at all are reported
  // beside it. `firstAnimated` is what a reviewer needs to check a claim like
  // "the recording never selects a sequence this model can pose": it is the one
  // index that would produce a pose, and it is compared against the sequences the
  // demo actually carries rather than assumed away.
  tf2::native::AnimationBindingCache cache;
  cache.reset(&root);
  tf2::native::AnimationInputStatus bindingStatus = tf2::native::AnimationInputStatus::ModelMissing;
  std::string bindingReason;
  const auto* binding = cache.resolve(path, bindingStatus, bindingReason);
  std::size_t animated = 0;
  std::size_t multiFrame = 0;
  std::int32_t maxFrames = 0;
  std::int32_t firstAnimated = -1;
  if (binding != nullptr) {
    for (std::size_t index = 0; index < binding->anims.sequences.size(); ++index) {
      const std::int32_t animDesc = binding->anims.sequences[index].animDesc;
      if (animDesc < 0
          || static_cast<std::size_t>(animDesc) >= binding->anims.animations.size()) continue;
      ++animated;
      if (firstAnimated < 0) firstAnimated = static_cast<std::int32_t>(index);
      const std::int32_t frames =
        binding->anims.animations[static_cast<std::size_t>(animDesc)].frameCount;
      if (frames > maxFrames) maxFrames = frames;
      if (frames > 1) ++multiFrame;
    }
  }
  std::fprintf(stderr,
    "modelseq path=%s resolution=%d readable=%d sequences=%zu bones=%zu reason=%s "
    "animated=%zu multiFrame=%zu maxFrames=%d firstAnimated=%d binding=%s\n",
    path.c_str(), static_cast<int>(candidate.resolution), readable ? 1 : 0,
    inspection.metadata.sequences.size(), inspection.metadata.bones.size(),
    reason.empty() ? "-" : reason.c_str(),
    animated, multiFrame, maxFrames, firstAnimated,
    binding != nullptr ? "ok" : "unavailable");
  std::fflush(stderr);
  std::cout << "{\"ok\":" << (readable ? "true" : "false")
            << ",\"path\":\"" << jsonEscape(path) << "\""
            << ",\"resolution\":" << static_cast<int>(candidate.resolution)
            << ",\"readable\":" << (readable ? "true" : "false")
            << ",\"sequences\":" << inspection.metadata.sequences.size()
            << ",\"bones\":" << inspection.metadata.bones.size()
            << ",\"animated\":" << animated
            << ",\"multiFrame\":" << multiFrame
            << ",\"maxFrames\":" << maxFrames
            << ",\"firstAnimated\":" << firstAnimated
            << ",\"binding\":\"" << (binding != nullptr ? "ok" : "unavailable") << "\""
            << ",\"reason\":\"" << jsonEscape(reason) << "\"}\n";
  return readable ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path tfRoot;
  std::string demoPath;
  std::string drawnModel;
  std::string modelSequences;
  std::string mutationArgument = "none";
  std::int32_t sampleStart = 1000;
  std::int32_t sampleStep = 400;
  std::size_t sampleCount = 140;
  bool selfTestOnly = false;

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--self-test") {
      selfTestOnly = true;
    } else if (argument == "--tf-root" && index + 1 < argc) {
      tfRoot = argv[++index];
    } else if (argument == "--demo" && index + 1 < argc) {
      demoPath = argv[++index];
    } else if (argument == "--drawn-model" && index + 1 < argc) {
      drawnModel = argv[++index];
    } else if (argument == "--model-sequences" && index + 1 < argc) {
      modelSequences = argv[++index];
    } else if (argument == "--mutation" && index + 1 < argc) {
      mutationArgument = argv[++index];
    } else if (argument == "--sample-start" && index + 1 < argc) {
      sampleStart = static_cast<std::int32_t>(std::strtol(argv[++index], nullptr, 10));
    } else if (argument == "--sample-step" && index + 1 < argc) {
      sampleStep = static_cast<std::int32_t>(std::strtol(argv[++index], nullptr, 10));
      if (sampleStep <= 0) {
        std::cerr << "viewmodel_animation_probe: --sample-step must be positive\n";
        return 13;
      }
    } else if (argument == "--sample-count" && index + 1 < argc) {
      sampleCount = static_cast<std::size_t>(std::strtoul(argv[++index], nullptr, 10));
      if (sampleCount < 2) {
        std::cerr << "viewmodel_animation_probe: --sample-count must be at least 2; "
                     "a window of one tick cannot show a cursor moving\n";
        return 13;
      }
    } else {
      std::cerr << "viewmodel_animation_probe: unknown argument " << argument << "\n";
      usage();
      return 13;
    }
  }

  if (tfRoot.empty()) {
    // The self-test runs without a TF root, but then it cannot drive the
    // product's entry point and says so rather than silently testing half a rule.
    if (selfTestOnly) return selfTest(nullptr);
    usage();
    return 13;
  }
  const auto assets = tf2::native::AssetRoot::fromPath(tfRoot.string());
  if (selfTestOnly) return selfTest(&assets);

  bool mutationOk = false;
  const Mutation mutation = parseMutation(mutationArgument, mutationOk);
  if (!mutationOk) {
    std::cerr << "viewmodel_animation_probe: unknown mutation " << mutationArgument << "\n";
    return 13;
  }

  if (!modelSequences.empty()) return reportModelSequences(assets, modelSequences);

  if (demoPath.empty()) {
    usage();
    return 13;
  }

  tf2::native::DemoHeader header;
  tf2::native::DemoIndex index;
  tf2::native::DemoNetworkSummary summary;
  if (!tf2::native::parseDemoHeaderFile(demoPath, header)) {
    std::cerr << "viewmodel_animation_probe: demo header parse failed\n";
    return 13;
  }
  tf2::native::indexDemoFile(demoPath, header, index);
  // The protocol has to be in place before the scan: svc_CreateStringTable reads
  // its payload length differently above and below protocol 23, and the wrong
  // branch produces a plausible summary of the wrong contents.
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
    std::cerr << "viewmodel_animation_probe: demo scan failed\n";
    return 13;
  }

  // ---------------------------------------------------------------------
  // 1. Field supply, per class, per field.
  // ---------------------------------------------------------------------
  const char* kTargetClasses[] = {"CTFViewModel", "CBaseViewModel", "CTFPlayer"};
  std::map<std::string, tf2::native::ServerClassSchema> targetById;
  std::map<std::string, std::int32_t> targetClassId;
  std::map<std::string, FieldSupply> targetSupply;
  for (const auto& name : kTargetClasses) {
    for (const auto& serverClass : summary.serverClassSchemas) {
      if (serverClass.name != name) continue;
      targetClassId[name] = static_cast<std::int32_t>(serverClass.id);
      const tf2::native::SendTableSchema* table = nullptr;
      for (const auto& candidate : summary.sendTableSchemas) {
        if (candidate.name == serverClass.dataTable) { table = &candidate; break; }
      }
      targetSupply[name] = table != nullptr ? countFields(*table) : FieldSupply{};
      break;
    }
  }
  std::size_t classesWithSequence = 0, classesWithCycle = 0, classesWithRate = 0;
  for (const auto& serverClass : summary.serverClassSchemas) {
    const tf2::native::SendTableSchema* table = nullptr;
    for (const auto& candidate : summary.sendTableSchemas) {
      if (candidate.name == serverClass.dataTable) { table = &candidate; break; }
    }
    if (table == nullptr) continue;
    const auto supply = countFields(*table);
    if (supply.sequence > 0) ++classesWithSequence;
    if (supply.cycle > 0) ++classesWithCycle;
    if (supply.rate > 0) ++classesWithRate;
  }
  if (mutation == Mutation::FieldSupplyCollapse) {
    classesWithSequence = 0;
    classesWithCycle = 0;
    classesWithRate = 0;
    for (auto& entry : targetSupply) entry.second = FieldSupply{};
  }
  if (mutation == Mutation::CycleInvented) {
    for (auto& entry : targetSupply) entry.second.cycle = 1;
  }
  std::size_t targetClassesFound = 0;
  for (const auto& name : kTargetClasses) {
    const auto supply = targetSupply[name];
    const auto id = targetClassId.find(name);
    if (id != targetClassId.end()) ++targetClassesFound;
    std::fprintf(stderr, "vmfield class=%s id=%d sequence=%zu cycle=%zu rate=%zu pose=%zu\n",
                 name, id == targetClassId.end() ? -1 : id->second,
                 supply.sequence, supply.cycle, supply.rate, supply.pose);
  }
  std::fprintf(stderr,
               "vmfield-summary classesWithSequence=%zu classesWithCycle=%zu classesWithRate=%zu "
               "targetClasses=%zu targetClassesFound=%zu\n",
               classesWithSequence, classesWithCycle, classesWithRate,
               sizeof(kTargetClasses) / sizeof(kTargetClasses[0]), targetClassesFound);

  // ---------------------------------------------------------------------
  // 2. Census by snapshot class id, plus the observation series.
  // ---------------------------------------------------------------------
  const std::int32_t viewModelClassId = targetClassId.count("CTFViewModel")
    ? targetClassId["CTFViewModel"] : -1;
  const std::int32_t baseViewModelClassId = targetClassId.count("CBaseViewModel")
    ? targetClassId["CBaseViewModel"] : -1;

  std::map<std::int32_t, std::set<int>> census;
  std::set<std::int32_t> classesSeen;
  std::size_t ticksQueried = 0;
  std::size_t ticksWithSnapshot = 0;
  // Which ViewModel entity belongs to the player whose eyes the demo records.
  // svc_SetView names that player, and the ViewModel entity carries m_hOwner --
  // so the pairing is read, not guessed from "the first ViewModel entity in the
  // packet". Without a view entity the product has no principled way to choose,
  // which is itself a status it has to report rather than paper over.
  const std::int64_t viewEntity = static_cast<std::int64_t>(summary.lastViewEntity);
  std::size_t observationsOwnedByViewEntity = 0;
  std::set<int> entitiesOwnedByViewEntity;
  SequenceSeries series;
  // Mutation support: the first sequence seen per entity, so `sequence-frozen`
  // can report a cursor that never moves while every other reading stays real.
  std::map<int, std::int64_t> frozenSequence;
  std::size_t firstSequenceSeenByEntity = 0;

  for (std::size_t step = 0; step < sampleCount; ++step) {
    const std::int32_t tick = sampleStart + static_cast<std::int32_t>(step) * sampleStep;
    std::vector<tf2::native::EntityState> states;
    std::int32_t resolvedTick = 0;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolvedTick);
    ++ticksQueried;
    if (status == tf2::native::EntitySnapshotQueryStatus::NoHistory
        || status == tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory) {
      continue;
    }
    ++ticksWithSnapshot;
    for (std::size_t entity = 0; entity < states.size(); ++entity) {
      const std::int32_t classId = states[entity].classId;
      if (classId < 0) continue;
      classesSeen.insert(classId);
      census[classId].insert(static_cast<int>(entity));
      if (classId != viewModelClassId && classId != baseViewModelClassId) continue;
      if (mutation == Mutation::CensusSkipViewModel && classId == viewModelClassId) {
        census[classId].erase(static_cast<int>(entity));
        continue;
      }
      Observation observation;
      observation.tick = resolvedTick;
      observation.entity = static_cast<int>(entity);
      const auto sequence = readInt(states[entity], "m_nSequence");
      observation.sequence = sequence.found ? sequence.value : -1;
      const auto rate = readFloat(states[entity], "m_flPlaybackRate");
      observation.rate = rate.found ? rate.value : 0.0;
      const auto modelIndex = readInt(states[entity], "m_nModelIndex");
      observation.modelIndex = modelIndex.found ? modelIndex.value : -1;
      if (observation.modelIndex >= 0) {
        const auto found = summary.modelPrecache.find(
          static_cast<std::uint16_t>(observation.modelIndex));
        if (found != summary.modelPrecache.end()) observation.modelPath = found->second;
      }
      const auto owner = readInt(states[entity], "m_hOwner");
      observation.owner = owner.found ? owner.value : -1;
      // Source packs the entity index into the low 11 bits of a handle. The
      // serial above it is not validated: EntityState carries no serial, so a
      // reused slot still resolves to that slot. That limit is stated, not
      // worked around.
      if (observation.owner >= 0) {
        const std::int64_t ownerIndex = observation.owner % 2048;
        if (viewEntity > 0 && ownerIndex == viewEntity) {
          ++observationsOwnedByViewEntity;
          entitiesOwnedByViewEntity.insert(observation.entity);
        }
      }
      const auto weapon = readInt(states[entity], "m_hWeapon");
      observation.weapon = weapon.found ? weapon.value : -1;
      if (mutation == Mutation::ModelClassFlip) {
        const std::size_t slash = observation.modelPath.find_last_of("/\\");
        const std::size_t base = slash == std::string::npos ? 0 : slash + 1;
        const std::string file = observation.modelPath.substr(base);
        if (!file.empty() && file.rfind("v_", 0) != 0) {
          observation.modelPath = observation.modelPath.substr(0, base) + "v_" + file;
        }
      }
      if (mutation == Mutation::SequenceFrozen) {
        // Report every entity's sequence as the first one ever seen for it.
        auto found = frozenSequence.find(observation.entity);
        if (found == frozenSequence.end()) {
          frozenSequence[observation.entity] = observation.sequence;
          ++firstSequenceSeenByEntity;
        } else {
          observation.sequence = found->second;
        }
      }
      series.observe(observation);
    }
  }

  for (const auto& name : kTargetClasses) {
    const auto id = targetClassId.find(name);
    const std::int32_t classId = id == targetClassId.end() ? -1 : id->second;
    const std::size_t entities = classId < 0 ? 0 : census[classId].size();
    std::fprintf(stderr, "vmcensus class=%s id=%d entities=%zu ticks=%zu\n",
                 name, classId, entities, ticksWithSnapshot);
  }
  std::size_t entitiesSeen = 0;
  for (const auto& entry : census) entitiesSeen += entry.second.size();
  std::fprintf(stderr, "vmcensus-summary ticksQueried=%zu ticksWithSnapshot=%zu classesSeen=%zu entitiesSeen=%zu\n",
               ticksQueried, ticksWithSnapshot, classesSeen.size(), entitiesSeen);
  std::fprintf(stderr,
               "vmlocal setViewCount=%zu viewEntity=%lld viewModelObservations=%zu "
               "ownedByViewEntity=%zu entitiesOwnedByViewEntity=%zu\n",
               summary.setViewCount, static_cast<long long>(viewEntity),
               series.observations(), observationsOwnedByViewEntity,
               entitiesOwnedByViewEntity.size());

  for (const auto& sample : series.samples()) {
    std::fprintf(stderr,
                 "vmanim tick=%d entity=%d sequence=%lld rate=%.2f modelIndex=%lld model=%s owner=%lld weapon=%lld\n",
                 sample.tick, sample.entity, static_cast<long long>(sample.sequence), sample.rate,
                 static_cast<long long>(sample.modelIndex),
                 sample.modelPath.empty() ? "-" : sample.modelPath.c_str(),
                 static_cast<long long>(sample.owner), static_cast<long long>(sample.weapon));
  }
  std::fprintf(stderr,
               "vmanim-summary observations=%zu changes=%zu series=%zu entities=%zu "
               "distinctSequences=%zu minSequence=%lld maxSequence=%lld distinctRates=%zu "
               "modelPaths=%zu vStarModels=%zu\n",
               series.observations(), series.changes(), series.samples().size(), series.entities(),
               series.distinctSequences(),
               static_cast<long long>(series.minSequence()), static_cast<long long>(series.maxSequence()),
               series.distinctRates(), series.distinctModelPaths(), series.modelsThatAreViewModels());

  // ---------------------------------------------------------------------
  // 3. Does the observed sequence index the drawn model's table?
  // ---------------------------------------------------------------------
  std::int64_t drawnSequenceCount = -1;
  std::string drawnModelNormalized = drawnModel;
  if (!drawnModel.empty()) {
    const auto candidate = tf2::native::ModelLoader::resolveAsset(assets, drawnModel);
    tf2::native::ModelInspection inspection;
    if (candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose) {
      inspection = tf2::native::ModelLoader::inspect(candidate.looseMdl);
    } else if (candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk
               && candidate.vpkArchives.size() == 1) {
      tf2::native::VpkArchive archive;
      std::string error;
      if (archive.open(candidate.vpkArchives.front(), &error)) {
        inspection = tf2::native::ModelLoader::inspectVpk(archive, drawnModel);
      }
    }
    if (inspection.mdl.signatureValid && inspection.metadata.valid) {
      drawnSequenceCount = static_cast<std::int64_t>(inspection.metadata.sequences.size());
    }
  }
  std::size_t inRange = 0, outOfRange = 0, modelMatchesDrawn = 0;
  for (const auto& sample : series.samples()) {
    const int verdict = sequenceInRange(sample.sequence, drawnSequenceCount);
    if (verdict == 1) ++inRange;
    else if (verdict == 0) ++outOfRange;
    if (!drawnModel.empty() && sample.modelPath == drawnModelNormalized) ++modelMatchesDrawn;
  }
  const char* indexSpace = "unmeasured";
  if (drawnSequenceCount >= 0) {
    indexSpace = modelMatchesDrawn > 0 ? "match" : "mismatch";
  }
  std::fprintf(stderr,
               "vmmapping-summary drawnModel=%s drawnSequenceCount=%lld observations=%zu "
               "inRange=%zu outOfRange=%zu entityModelMatchesDrawn=%zu indexSpace=%s\n",
               drawnModel.empty() ? "none" : drawnModel.c_str(),
               static_cast<long long>(drawnSequenceCount), series.samples().size(),
               inRange, outOfRange, modelMatchesDrawn, indexSpace);

  // ---------------------------------------------------------------------
  // 4. The per-entity question: the sequence against ITS OWN model's table
  // ---------------------------------------------------------------------
  //
  // Block 3 compares every observation against ONE drawn model, and that is only
  // the right question when the drawn model belongs to the entity being
  // observed. A recording with several players of several classes answers it
  // across a mixed population: on the SourceTV recording this tree pins,
  // `vmmapping-summary` reads outOfRange=121 against a single arms model purely
  // because thirty viewmodel entities of seven different classes were folded
  // into one denominator.
  //
  // This block asks the question the entity can actually answer: is the observed
  // sequence inside the table of the model that entity itself reports? It is a
  // separate counter from block 3 on purpose -- if the two ever disagree, that
  // disagreement is the reading, not a bug in one of them.
  std::map<std::string, std::int64_t> modelSequenceCounts;
  const auto sequenceCountForModel = [&](const std::string& path) -> std::int64_t {
    const auto cached = modelSequenceCounts.find(path);
    if (cached != modelSequenceCounts.end()) return cached->second;
    std::int64_t count = -1;
    const auto candidate = tf2::native::ModelLoader::resolveAsset(assets, path);
    tf2::native::ModelInspection inspection;
    if (candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose) {
      inspection = tf2::native::ModelLoader::inspect(candidate.looseMdl);
    } else if (candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk
               && candidate.vpkArchives.size() == 1) {
      tf2::native::VpkArchive archive;
      std::string error;
      if (archive.open(candidate.vpkArchives.front(), &error)) {
        inspection = tf2::native::ModelLoader::inspectVpk(archive, path);
      }
    }
    if (inspection.mdl.signatureValid && inspection.metadata.valid) {
      count = static_cast<std::int64_t>(inspection.metadata.sequences.size());
    }
    modelSequenceCounts.emplace(path, count);
    return count;
  };

  std::size_t selfMeasured = 0, selfInRange = 0, selfOutOfRange = 0, selfUnmeasured = 0;
  for (const auto& sample : series.samples()) {
    if (sample.modelPath.empty()) { ++selfUnmeasured; continue; }
    const std::int64_t count = sequenceCountForModel(sample.modelPath);
    if (count < 0) { ++selfUnmeasured; continue; }
    ++selfMeasured;
    const int verdict = sequenceInRange(sample.sequence, count);
    if (verdict == 1) ++selfInRange;
    else if (verdict == 0) {
      ++selfOutOfRange;
      // Named, not just counted: a counter that reads 0 because it never looked is
      // indistinguishable from a counter that looked and found nothing, and a
      // counter that reads 1 needs to say WHICH sample so the one can be explained
      // instead of averaged away.
      std::fprintf(stderr,
                   "vmself-outlier tick=%d entity=%d sequence=%lld modelIndex=%lld "
                   "model=%s sequences=%lld\n",
                   sample.tick, sample.entity, static_cast<long long>(sample.sequence),
                   static_cast<long long>(sample.modelIndex), sample.modelPath.c_str(),
                   static_cast<long long>(count));
    }
  }
  std::fprintf(stderr,
               "vmselfsum observations=%zu measured=%zu unmeasured=%zu inRange=%zu "
               "outOfRange=%zu modelsQueried=%zu\n",
               series.samples().size(), selfMeasured, selfUnmeasured, selfInRange,
               selfOutOfRange, modelSequenceCounts.size());
  for (const auto& row : modelSequenceCounts) {
    if (row.second < 0) {
      std::fprintf(stderr, "vmself model=%s sequences=-1 (the entity's own model did not resolve)\n",
                   row.first.c_str());
    }
  }

  // ---------------------------------------------------------------------
  // One JSON object on stdout.
  // ---------------------------------------------------------------------
  std::cout << "{"
            << "\"ok\":true"
            << ",\"selfTest\":false"
            << ",\"demo\":\"" << jsonEscape(demoPath) << "\""
            << ",\"mutation\":\"" << mutationName(mutation) << "\""
            << ",\"sampleStart\":" << sampleStart
            << ",\"sampleStep\":" << sampleStep
            << ",\"sampleCount\":" << sampleCount
            << ",\"ticksQueried\":" << ticksQueried
            << ",\"ticksWithSnapshot\":" << ticksWithSnapshot
            << ",\"classesWithSequence\":" << classesWithSequence
            << ",\"classesWithCycle\":" << classesWithCycle
            << ",\"classesWithRate\":" << classesWithRate
            << ",\"targetClassesFound\":" << targetClassesFound
            << ",\"viewModelClassId\":" << viewModelClassId
            << ",\"viewModelEntities\":"
            << (viewModelClassId < 0 ? 0 : census[viewModelClassId].size())
            << ",\"baseViewModelEntities\":"
            << (baseViewModelClassId < 0 ? 0 : census[baseViewModelClassId].size())
            << ",\"setViewCount\":" << summary.setViewCount
            << ",\"viewEntity\":" << viewEntity
            << ",\"observationsOwnedByViewEntity\":" << observationsOwnedByViewEntity
            << ",\"entitiesOwnedByViewEntity\":" << entitiesOwnedByViewEntity.size()
            << ",\"observations\":" << series.observations()
            << ",\"sequenceChanges\":" << series.changes()
            << ",\"entities\":" << series.entities()
            << ",\"distinctSequences\":" << series.distinctSequences()
            << ",\"minSequence\":" << series.minSequence()
            << ",\"maxSequence\":" << series.maxSequence()
            << ",\"distinctRates\":" << series.distinctRates()
            << ",\"modelPaths\":" << series.distinctModelPaths()
            << ",\"vStarModels\":" << series.modelsThatAreViewModels()
            << ",\"drawnModel\":\"" << jsonEscape(drawnModel) << "\""
            << ",\"drawnSequenceCount\":" << drawnSequenceCount
            << ",\"inRange\":" << (drawnSequenceCount < 0 ? -1 : static_cast<long long>(inRange))
            << ",\"outOfRange\":" << (drawnSequenceCount < 0 ? -1 : static_cast<long long>(outOfRange))
            << ",\"entityModelMatchesDrawn\":" << modelMatchesDrawn
            << ",\"indexSpace\":\"" << indexSpace << "\""
            << "}\n";
  std::fflush(stderr);
  std::fflush(stdout);
  return 0;
}
