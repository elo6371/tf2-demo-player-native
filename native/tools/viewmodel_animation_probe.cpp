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

    // 6. The resolver contract, driven through the product's own entry point. A
    //    gate that asserts "the status is sequenceOutOfRange" is asserting a
    //    string unless every branch is reachable and each one is shown to need
    //    its own input. So: one real model, one real animation binding, and one
    //    case per branch -- with the negative cases differing from the positive
    //    one by exactly one field.
    const auto drawn = tf2::native::buildViewModelRequest(
      *root, "models/weapons/v_models/v_scattergun_scout.mdl", tf2::native::ViewModelHand::Right);
    check(drawn.status == tf2::native::ViewModelStatus::Ok,
          "the product resolves the drawn model");
    check(!drawn.sequenceLabels.empty(), "the drawn model has a sequence table to be in or out of");
    check(!drawn.bones.empty(), "the drawn model has a skeleton");

    tf2::native::AnimationBindingCache bindings;
    bindings.reset(root);
    tf2::native::AnimationInputStatus bindingStatus = tf2::native::AnimationInputStatus::ModelMissing;
    std::string bindingReason;
    const auto* binding = bindings.resolve(drawn.path, bindingStatus, bindingReason);
    check(binding != nullptr, "the drawn model has an animation binding");
    if (binding != nullptr) {
      check(binding->renderBones == drawn.bones.size(),
            "the binding's render skeleton is the drawn model's skeleton");
    }
    tf2::native::AnimationSampleRequest timing;
    timing.tick = 0;
    timing.tickRate = 30.0f;

    const auto resolve = [&](const tf2::native::ViewModelAnimationInput& input,
                             const tf2::native::ViewModelRequest& model,
                             const tf2::native::AnimationBinding* bound) {
      return tf2::native::resolveViewModelAnimationInput(input, model, bound, timing);
    };

    // (a) no entity at all
    tf2::native::ViewModelAnimationInput input;
    auto outcome = resolve(input, drawn, binding);
    check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
          "no owning entity resolves to missing");
    check(outcome.bindPose, "missing leaves the bind pose");
    check(outcome.reason.find("no viewmodel entity") != std::string::npos,
          "missing names which of the two absences it was");

    // (b) an entity whose sequence slot is absent -- one field away from (c)
    input.entityPresent = true;
    input.entityIndex = 7;
    outcome = resolve(input, drawn, binding);
    check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
          "an entity with no m_nSequence resolves to missing, not to sequence 0");
    check(outcome.reason.find("m_nSequence") != std::string::npos,
          "the absent sequence is named");

    // (c) out of range -- the refusal the round exists for
    input.hasSequence = true;
    input.sequence = static_cast<int>(drawn.sequenceLabels.size());
    outcome = resolve(input, drawn, binding);
    check(outcome.status == tf2::native::ViewModelAnimationStatus::SequenceOutOfRange,
          "one past the drawn table resolves to sequenceOutOfRange");
    check(outcome.bindPose, "an out-of-range sequence leaves the bind pose");
    check(outcome.reason.find(std::to_string(drawn.sequenceLabels.size())) != std::string::npos,
          "the refusal names the drawn table's size");

    // (d) in range -- the acceptance branch, which has to be reachable or (c) is
    //     a constant with extra steps
    input.sequence = 0;
    outcome = resolve(input, drawn, binding);
    check(outcome.status == tf2::native::ViewModelAnimationStatus::Ok,
          "sequence 0 resolves to ok");
    check(!outcome.bindPose, "ok does not leave the bind pose");
    check(outcome.skinMatrices.size() == drawn.bones.size(),
          "the accepted pose covers the drawn skeleton exactly");
    const auto acceptedOutcome = outcome;

    // (e) bone mismatch: the same accepted input against a skeleton one bone
    //     longer. This is the guard the standalone --model path applies, and the
    //     only way to reach it with real assets is to ask for the wrong skeleton.
    tf2::native::ViewModelRequest longerSkeleton = drawn;
    longerSkeleton.bones.resize(drawn.bones.size() + 1);
    outcome = resolve(input, longerSkeleton, binding);
    check(outcome.status == tf2::native::ViewModelAnimationStatus::BoneMismatch,
          "a skeleton one bone longer resolves to boneMismatch");
    check(outcome.bindPose, "a bone mismatch leaves the bind pose");

    // (f) a drawn model with no animation data at all
    outcome = resolve(input, drawn, nullptr);
    check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
          "a drawn model with no animation data resolves to missing");

    // (g) a drawn model that was never resolved. Without the guard in the
    //     resolver an empty sequence table makes every sequence "past the end",
    //     and a resource failure would be reported as a sequence problem.
    tf2::native::ViewModelRequest unresolved;
    unresolved.status = tf2::native::ViewModelStatus::Failed;
    unresolved.reason = "viewmodel was not found";
    outcome = resolve(input, unresolved, binding);
    check(outcome.status == tf2::native::ViewModelAnimationStatus::Missing,
          "an unresolved drawn model resolves to missing, not sequenceOutOfRange");
    check(outcome.reason.find("viewmodel was not found") != std::string::npos,
          "the unresolved reason is carried through");

    // (h) m_flCycle: DT_BaseViewModel has none, so the field exists to report the
    //     absence. A cycle that IS supplied has to reach the decoder rather than
    //     being dropped, or "absent" and "ignored" would look the same. The
    //     witness is a sequence long enough for a half-way cycle to land on a
    //     different frame -- asserted first, so a one-frame sequence reddens the
    //     premise instead of the conclusion.
    tf2::native::ViewModelAnimationInput cycled = input;
    cycled.sequence = 1;
    const auto plain = resolve(cycled, drawn, binding);
    check(plain.status == tf2::native::ViewModelAnimationStatus::Ok,
          "sequence 1 is accepted, so the cycle witness is on a live branch");
    check(plain.frameCount > 1, "the witness sequence has more than one frame");
    cycled.hasCycle = true;
    cycled.cycle = 0.5f;
    const auto halfway = resolve(cycled, drawn, binding);
    check(halfway.status == tf2::native::ViewModelAnimationStatus::Ok,
          "a supplied cycle does not change the status");
    check(halfway.frame != plain.frame,
          "a supplied cycle moves the sampled frame");
    check(acceptedOutcome.skinMatrices.size() == halfway.skinMatrices.size(),
          "the cycle witness covers the same skeleton");
  } else {
    std::fprintf(stderr,
                 "  NOTE the product cross-check did not run: no --tf-root was given, so\n"
                 "       the accepting half of the rule was not tested against the product\n");
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
  std::fprintf(stderr, "modelseq path=%s resolution=%d readable=%d sequences=%zu bones=%zu reason=%s\n",
               path.c_str(), static_cast<int>(candidate.resolution), readable ? 1 : 0,
               inspection.metadata.sequences.size(), inspection.metadata.bones.size(),
               reason.empty() ? "-" : reason.c_str());
  std::fflush(stderr);
  std::cout << "{\"ok\":" << (readable ? "true" : "false")
            << ",\"path\":\"" << jsonEscape(path) << "\""
            << ",\"resolution\":" << static_cast<int>(candidate.resolution)
            << ",\"readable\":" << (readable ? "true" : "false")
            << ",\"sequences\":" << inspection.metadata.sequences.size()
            << ",\"bones\":" << inspection.metadata.bones.size()
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
