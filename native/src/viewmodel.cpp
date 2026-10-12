#include "viewmodel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>

#include "vmt_material.h"

namespace tf2::native {
namespace {

// One resource, loose file first and then every archive that holds the key --
// the same order the product's own material reads use (and the same order
// viewmodel_material_probe measured). `root.resolve` only covers loose files;
// a resource that lives in a VPK comes back through `archives()`, which is the
// one place the archive set is opened, so this helper cannot multiply the
// expensive directory parse.
std::vector<std::uint8_t> readAssetResource(const AssetRoot& root, const std::string& resource) {
  std::vector<std::uint8_t> bytes;
  const auto resolved = root.resolve(std::filesystem::u8path(resource));
  if (!resolved.empty()) {
    std::ifstream file(resolved, std::ios::binary);
    if (file) {
      bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
  }
  if (!bytes.empty()) return bytes;
  std::vector<std::filesystem::path> holders;
  root.archives().collectContaining(resource, holders);
  for (const auto& holder : holders) {
    const auto* archive = root.archives().find(holder);
    if (!archive) continue;
    bytes = archive->read(resource);
    if (!bytes.empty()) return bytes;
  }
  return {};
}
} // namespace

// The material chain, slot by slot, stopping at the VTF existing. See
// ViewModelMaterialReading for why it stops there and what the count is for.
//
// Defined outside the anonymous namespace because it is exported: the probe
// drives this function with a hand-built inspection to reach the two chain
// states a shipped model cannot show (a texture slot naming a material that is
// not installed, and a material naming a texture that is not installed), which
// is the same code path the product runs on its own real inspection.
ViewModelMaterialReading resolveViewModelMaterialChain(const AssetRoot& root,
    const ModelInspection& inspection) {
  ViewModelMaterialReading reading;
  reading.slots = inspection.metadata.textureCandidates.size();
  for (std::size_t slot = 0; slot < inspection.metadata.textureCandidates.size(); ++slot) {
    const auto& candidates = inspection.metadata.textureCandidates[slot];
    if (candidates.empty()) {
      if (reading.firstFailure.empty()) {
        reading.firstFailure = "slot " + std::to_string(slot) + " has no candidate path";
      }
      continue;
    }
    ++reading.named;
    bool vmtRead = false;
    bool vtfRead = false;
    std::string failure;
    for (const auto& candidate : candidates) {
      const std::string vmtResource = VmtParser::resourcePath(candidate, ".vmt");
      const auto vmtBytes = readAssetResource(root, vmtResource);
      if (vmtBytes.empty()) {
        failure = "material " + vmtResource + " was not found";
        continue;
      }
      VmtMaterial material;
      if (!VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material)) {
        failure = "material " + vmtResource + " did not parse";
        continue;
      }
      vmtRead = true;
      if (material.baseTexture.empty()) {
        failure = "material " + vmtResource + " declares no $basetexture";
        break;
      }
      const std::string vtfResource = VmtParser::resourcePath(material.baseTexture, ".vtf");
      if (readAssetResource(root, vtfResource).empty()) {
        failure = "texture " + vtfResource + " was not found";
        break;
      }
      vtfRead = true;
      failure.clear();
      break;
    }
    if (vmtRead) ++reading.vmtResolved;
    if (vtfRead) ++reading.vtfResolved;
    if (!vtfRead && reading.firstFailure.empty()) {
      reading.firstFailure = "slot " + std::to_string(slot) + ": "
        + (failure.empty() ? std::string("no candidate produced a readable texture") : failure);
    }
  }
  return reading;
}

namespace {

// Kept as a local name, but there is only one rule: model_loader.h exports it
// because entity_model.cpp needs the same answer when it checks that a weapon's
// world model is not the arms composite. A second implementation here would be
// free to drift, and the drift would be invisible -- a comparison that misses
// draws the wrong model with every count still green.
std::string normalizePath(const std::string& value) {
  return normalizeModelPath(value);
}

bool isViewModelPath(const std::string& path) {
  const auto slash = path.find_last_of('/');
  const auto file = slash == std::string::npos ? path : path.substr(slash + 1);
  return file.size() > 6 && file.rfind("v_", 0) == 0 && file.size() >= 4
    && file.compare(file.size() - 4, 4, ".mdl") == 0;
}

bool finite3(const std::array<float, 3>& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

float length3(const std::array<float, 3>& value) {
  return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

// A rotation basis is unit length. 0.5..2.0 is loose enough that a real model
// never trips it and tight enough that a truncated read (an all-zero basis) or a
// misread stride does. Failing it is `Partial`, not `Invalid`: the transform is
// still finite and the bone still exists.
bool axisLengthReasonable(const std::array<float, 3>& value) {
  const float length = length3(value);
  return length >= 0.5f && length <= 2.0f;
}

// The classification the plan asks for, in the order the reasons matter: a
// nameless attachment cannot be selected by name at all, a bone index outside
// the skeleton cannot be resolved, and a non-finite transform cannot be applied.
// Only after all three pass does a suspicious basis get to be `Partial`.
ViewModelAttachment classifyAttachment(const ModelAttachment& source, std::size_t boneCount) {
  ViewModelAttachment attachment;
  attachment.name = source.name;
  attachment.bone = source.bone;
  attachment.origin = source.origin;
  attachment.axisX = source.axisX;
  attachment.axisY = source.axisY;
  attachment.axisZ = source.axisZ;
  if (attachment.name.empty()) {
    attachment.reason = "attachment name is unreadable";
    return attachment;
  }
  if (source.bone < 0 || static_cast<std::size_t>(source.bone) >= boneCount) {
    attachment.reason = "attachment bone index is outside the skeleton";
    return attachment;
  }
  if (!finite3(attachment.origin) || !finite3(attachment.axisX)
      || !finite3(attachment.axisY) || !finite3(attachment.axisZ)) {
    attachment.reason = "attachment transform is not finite";
    return attachment;
  }
  if (!axisLengthReasonable(attachment.axisX) || !axisLengthReasonable(attachment.axisY)
      || !axisLengthReasonable(attachment.axisZ)) {
    attachment.status = ViewModelAttachmentStatus::Partial;
    attachment.reason = "attachment basis is not unit length";
    return attachment;
  }
  attachment.status = ViewModelAttachmentStatus::Valid;
  return attachment;
}

const char* attachmentStatusName(ViewModelAttachmentStatus status) {
  switch (status) {
    case ViewModelAttachmentStatus::Valid: return "valid";
    case ViewModelAttachmentStatus::Partial: return "partial";
    default: return "invalid";
  }
}

void fillResources(const ModelInspection& inspection, ViewModelResourceStatus& resources) {
  resources.mdlReadable = inspection.mdl.signatureValid && inspection.metadata.valid;
  resources.vvdPresent = inspection.vvd.signatureValid;
  if (inspection.vtx.signatureValid) {
    resources.vtxPresent = true;
    resources.vtxVariant = "dx90";
  } else if (inspection.vtxDx80.signatureValid) {
    resources.vtxPresent = true;
    resources.vtxVariant = "dx80";
  } else if (inspection.vtxSw.signatureValid) {
    resources.vtxPresent = true;
    resources.vtxVariant = "sw";
  }
  resources.materialCount = inspection.metadata.textureNames.empty()
    ? static_cast<std::size_t>(inspection.metadata.textureCount)
    : inspection.metadata.textureNames.size();
  // Flat-colour phase: the texture table is read and counted, but nothing binds
  // a VMT yet, so this stays 0 rather than claiming a material that is not used.
  resources.materialResolved = 0;
  resources.companionsComplete = resources.mdlReadable && resources.vvdPresent && resources.vtxPresent;
}

} // namespace

std::array<float, 12> viewModelHandMatrix(ViewModelHand hand) {
  std::array<float, 12> matrix{};
  matrix[0] = hand == ViewModelHand::Left ? -1.0f : 1.0f;
  matrix[5] = 1.0f;
  matrix[10] = 1.0f;
  return matrix;
}

std::vector<std::string> defaultViewModelAlignmentPreferences(ViewModelHand hand) {
  // `weapon_bone` is the placement anchor on every shipped weapon viewmodel
  // checked on 2026-10-11 (`v_scattergun_scout`, `v_bat_scout`, `v_pda_spy`,
  // `v_grenadelauncher_demo`); `v_watch_spy` carries the left-hand variant
  // `weapon_bone_L`. The other hand's bone stays as a fallback so a model
  // without a variant is still placeable, and the reason string says so.
  if (hand == ViewModelHand::Left) return { "weapon_bone_L", "weapon_bone" };
  return { "weapon_bone", "weapon_bone_R" };
}

std::int32_t selectViewModelAlignment(ViewModelRequest& request,
    const std::vector<std::string>& preferences) {
  request.alignmentName.clear();
  request.alignmentIndex = -1;
  if (request.attachments.empty()) {
    request.alignmentReason = "the model carries no attachment; the pass uses the model transform";
    return -1;
  }
  std::string present;
  for (const auto& attachment : request.attachments) {
    if (attachment.name.empty()) continue;
    if (!present.empty()) present += ",";
    present += attachment.name;
  }
  std::string rejected;
  for (const auto& wanted : preferences) {
    for (std::size_t index = 0; index < request.attachments.size(); ++index) {
      const auto& attachment = request.attachments[index];
      if (attachment.name != wanted) continue;
      if (attachment.status == ViewModelAttachmentStatus::Invalid) {
        if (!rejected.empty()) rejected += ",";
        rejected += wanted + "(invalid)";
        break;
      }
      request.alignmentName = attachment.name;
      request.alignmentIndex = static_cast<std::int32_t>(index);
      request.alignmentReason = "placement attachment '" + wanted + "' is "
        + attachmentStatusName(attachment.status);
      if (!rejected.empty()) request.alignmentReason += "; rejected " + rejected;
      return request.alignmentIndex;
    }
  }
  // No placement anchor. This is a documented fallback, not "take the first
  // one": the request keeps the model's own transform and the reason names what
  // the model does have, so a reader can tell an effect-only attachment set from
  // a model that was never going to be placeable.
  request.alignmentReason = "no placement attachment present (found: " + present
    + "); the pass uses the model transform";
  return -1;
}

namespace {

// The one implementation both entry points share. `requireViewModelName` is the
// only difference between them and it is a parameter rather than a second
// function body on purpose: the companion has to be inspected, counted,
// alignment-selected and mesh-built by the same parser, or the two paths would
// disagree about the same file and the disagreement would only show up as a
// ViewModel that draws under one entry point and not the other.
ViewModelRequest buildRequest(const AssetRoot& root, const std::string& path,
    ViewModelHand hand, std::optional<float> fov, bool requireViewModelName) {
  ViewModelRequest request;
  request.hand = hand;
  request.handTransform = viewModelHandMatrix(hand);
  request.fov = normalizeViewModelFov(fov.value_or(kDefaultViewModelFov));
  request.path = normalizePath(path);
  if (request.path.empty()) {
    request.reason = "empty viewmodel path";
    return request;
  }
  if (request.path.size() < 4 || request.path.compare(request.path.size() - 4, 4, ".mdl") != 0) {
    request.path += ".mdl";
  }
  if (requireViewModelName && !isViewModelPath(request.path)) {
    request.reason = "path is not a viewmodel";
    return request;
  }
  const auto candidate = ModelLoader::resolveAsset(root, request.path);
  request.path = candidate.requestedPath.empty() ? request.path : candidate.requestedPath;
  request.resolution = candidate.resolution;
  request.companionsComplete = candidate.companionSetComplete;
  if (candidate.resolution == ModelAssetResolution::Ambiguous) {
    request.reason = "viewmodel path is ambiguous";
    return request;
  }
  if (candidate.resolution != ModelAssetResolution::FoundLoose
      && candidate.resolution != ModelAssetResolution::FoundVpk) {
    request.reason = "viewmodel was not found";
    return request;
  }
  if (!candidate.companionSetComplete) {
    request.reason = "viewmodel companions are missing";
    return request;
  }

  // One inspection, from whichever source holds the model. This used to be a
  // second, hand-rolled MDL reader inside this file (hard-coded offsets 240/244,
  // and only the three origin scalars at +24/+40/+56), which disagreed with
  // model_loader.cpp about the same structure. The attachment table, the bones
  // and the sequences now come from the same parser the world models use.
  ModelInspection inspection;
  if (candidate.resolution == ModelAssetResolution::FoundLoose) {
    inspection = ModelLoader::inspect(candidate.looseMdl);
  } else {
    VpkArchive archive;
    std::string error;
    if (candidate.vpkArchives.size() != 1 || !archive.open(candidate.vpkArchives.front(), &error)) {
      request.reason = error.empty() ? "viewmodel archive did not open" : error;
      return request;
    }
    inspection = ModelLoader::inspectVpk(archive, request.path);
  }
  if (!inspection.mdl.signatureValid || !inspection.metadata.valid) {
    request.reason = "viewmodel MDL is missing or invalid";
    return request;
  }

  fillResources(inspection, request.resources);
  request.bones = inspection.metadata.bones;
  request.sequenceLabels.reserve(inspection.metadata.sequences.size());
  for (const auto& sequence : inspection.metadata.sequences) request.sequenceLabels.push_back(sequence.label);

  request.attachmentCount = inspection.metadata.attachmentCount;
  request.attachments.reserve(inspection.metadata.attachments.size());
  for (const auto& source : inspection.metadata.attachments) {
    auto attachment = classifyAttachment(source, request.bones.size());
    if (attachment.status == ViewModelAttachmentStatus::Valid) ++request.attachmentsValid;
    else if (attachment.status == ViewModelAttachmentStatus::Partial) ++request.attachmentsPartial;
    else ++request.attachmentsInvalid;
    request.attachments.push_back(std::move(attachment));
  }
  if (request.attachments.size() != request.attachmentCount) {
    request.attachmentReason = "the attachment table did not parse completely";
  } else if (request.attachmentsInvalid != 0) {
    request.attachmentReason = std::to_string(request.attachmentsInvalid)
      + " attachment(s) unusable; alignment falls back to the model transform";
  } else if (request.attachmentsPartial != 0) {
    request.attachmentReason = std::to_string(request.attachmentsPartial)
      + " attachment(s) usable but suspicious";
  } else {
    request.attachmentReason = "all attachments valid";
  }

  // A damaged attachment is recorded and skipped; it never fails the request.
  selectViewModelAlignment(request, defaultViewModelAlignmentPreferences(hand));

  if (!request.resources.companionsComplete) {
    request.reason = "viewmodel companions are missing";
    return request;
  }
  // Build the mesh once here so the draw path never decodes a VTX per frame.
  std::size_t descriptor = inspection.metadata.meshes.size();
  for (std::size_t index = 0; index < inspection.metadata.meshes.size(); ++index) {
    if (inspection.metadata.meshes[index].indexStatus == ModelMeshIndexStatus::Available) {
      descriptor = index;
      break;
    }
  }
  if (descriptor < inspection.metadata.meshes.size()) {
    std::string error;
    if (ModelLoader::buildModelMeshData(inspection, descriptor, request.mesh, error)) {
      request.resources.meshReady = true;
    } else {
      request.resources.meshReason = error.empty() ? "viewmodel mesh did not build" : error;
    }
  } else {
    request.resources.meshReason = "the model has no decodable mesh";
  }

  // Measured here, where the MDL's own texture table is still in hand. It does
  // not gate the draw: a model whose materials are missing is still drawable in
  // flat colour, and the reading is what names that state rather than hiding it
  // behind an identical picture.
  request.materialReading = resolveViewModelMaterialChain(root, inspection);
  // The material chain, resolved against the same inspection. `descriptorIndex`
  // is the descriptor the mesh was built from, and it is passed rather than
  // assumed: `request.mesh.descriptorIndex` is set by the builder, so the slot
  // the picture uses is read from the mesh table for *that* descriptor instead of
  // from a default that happens to agree with it on the shipped models.
  request.materials = resolveViewModelMaterialTable(root, inspection.metadata,
    request.resources.meshReady ? static_cast<std::int32_t>(request.mesh.descriptorIndex) : -1);
  // `materialCount` is a property of the MDL and is final here. `materialResolved`
  // and `flatColorFallback` are not: they depend on whether the decoder produced
  // pixels, which happens in the caller, on the device side, one slot at a time.
  // `applyViewModelMaterialResult` is that half.
  request.resources.materialCount = request.materials.materialSlots();
  request.resources.materialResolved = 0;
  request.resources.flatColorFallback = true;

  request.status = ViewModelStatus::Ok;
  request.reason.clear();
  return request;
}

} // namespace

ViewModelRequest buildViewModelRequest(const AssetRoot& root, const std::string& path,
    ViewModelHand hand, std::optional<float> fov) {
  return buildRequest(root, path, hand, fov, true);
}

ViewModelRequest buildViewModelCompanionRequest(const AssetRoot& root, const std::string& path,
    ViewModelHand hand, std::optional<float> fov) {
  return buildRequest(root, path, hand, fov, false);
}

void applyViewModelMaterialResult(ViewModelRequest& request) {
  request.resources.materialResolved = request.materials.materialResolved();
  // The fallback reading is about the *drawn* range, not about the model: a
  // ViewModel with three resolved slots and a fourth that failed still draws a
  // texture for the mesh that was built, and calling that "flat colour fallback"
  // would name the wrong thing. The drawn slot is the one the picture uses.
  const auto drawn = request.materials.drawnSlot;
  const bool drawnHasMaterial = drawn >= 0
    && static_cast<std::size_t>(drawn) < request.materials.slots.size()
    && request.materials.slots[static_cast<std::size_t>(drawn)].pixelsDecoded;
  request.resources.flatColorFallback = !drawnHasMaterial;
}

const char* viewModelAnimationStatusName(ViewModelAnimationStatus status) {
  switch (status) {
    case ViewModelAnimationStatus::Ok: return "ok";
    case ViewModelAnimationStatus::ModelMismatch: return "modelMismatch";
    case ViewModelAnimationStatus::SequenceOutOfRange: return "sequenceOutOfRange";
    case ViewModelAnimationStatus::BoneMismatch: return "boneMismatch";
    default: return "missing";
  }
}

ViewModelAnimationOutcome resolveViewModelAnimationInput(
    const ViewModelAnimationInput& input, const ViewModelRequest& drawn,
    const AnimationBinding* drawnBinding, const AnimationSampleRequest& timing) {
  ViewModelAnimationOutcome outcome;
  if (drawn.status != ViewModelStatus::Ok) {
    // Nothing was drawn, so there is no sequence table to be inside or outside
    // of. Without this branch an empty drawn table would make every sequence
    // `>= 0` read as out of range, and a resource failure would be reported as
    // `sequenceOutOfRange` -- a mislabelled reading, which is the failure mode
    // this project keeps finding in its own gates.
    outcome.reason = "the drawn model was not resolved: "
      + (drawn.reason.empty() ? std::string("no reason recorded") : drawn.reason);
    return outcome;
  }
  // The order of these branches is the order of the reasons, and each is
  // reachable on its own. See the header for why the model proof sits before the
  // range check: which table is being indexed is settled before how far into it
  // the index reaches, or the range verdict is a statement about the wrong table.
  if (!input.entityPresent) {
    outcome.reason = "no viewmodel entity owned by the view entity";
    return outcome;
  }
  // The owner proof. The lookup above already selects on this equality, so this
  // branch is unreachable through the product today -- and that is exactly why it
  // is here. The negative fixture for a wrong owner and the mutation that clears
  // it both enter through this field, and a contract that could not reject them
  // would be a rule stated in a comment rather than in code.
  if (!input.hasOwner) {
    outcome.reason = "the viewmodel entity at index " + std::to_string(input.entityIndex)
      + " carries no m_hOwner";
    return outcome;
  }
  if (input.ownerEntity != input.viewEntity) {
    outcome.reason = "the viewmodel entity at index " + std::to_string(input.entityIndex)
      + " is owned by entity " + std::to_string(input.ownerEntity)
      + ", not by the view entity " + std::to_string(input.viewEntity);
    return outcome;
  }
  // The model proof's first half. Zero is the value an entity carries when no
  // model was set, so writing it to the precache would be a lookup for a model
  // that was never named -- and on the POV recording the EMPTY ViewModel slot of
  // a player with no weapon holds exactly that.
  if (!input.hasModelIndex) {
    outcome.reason = "the viewmodel entity at index " + std::to_string(input.entityIndex)
      + " carries no m_nModelIndex";
    return outcome;
  }
  if (input.modelIndex == 0) {
    outcome.reason = "m_nModelIndex is 0, which names no model";
    return outcome;
  }
  if (input.entityModelPath.empty()) {
    outcome.reason = "m_nModelIndex " + std::to_string(input.modelIndex)
      + " is not in the demo's model_precache";
    return outcome;
  }
  if (!input.hasSequence) {
    outcome.reason = "the viewmodel entity at index " + std::to_string(input.entityIndex)
      + " carries no m_nSequence";
    return outcome;
  }
  if (input.sequence < 0) {
    outcome.reason = "m_nSequence is negative";
    return outcome;
  }
  // The model proof's second half, and the one this round is about: the pass has
  // to be drawing the model the entity named. Both sides are normalised because
  // one comes from the demo's precache (verbatim, as the server wrote it) and the
  // other from the asset resolver (lowercased, forward slashes) -- a difference
  // of case or separator is not a difference of model, and treating it as one
  // would refuse a correct pairing on a cosmetic detail.
  const std::string entityModel = normalizePath(input.entityModelPath);
  const bool entityModelMatches = !entityModel.empty() && entityModel == drawn.path;
  if (!entityModelMatches) {
    outcome.reason = "the pass draws " + (drawn.path.empty() ? std::string("-") : drawn.path)
      + " but the entity's own m_nModelIndex names " + input.entityModelPath;
    outcome.status = ViewModelAnimationStatus::ModelMismatch;
    return outcome;
  }
  outcome.entityModelMatchesDrawn = true;
  // Measured against the model the pass draws, and that model has just been
  // proven to be the entity's own, so this is the entity's own table.
  const std::size_t drawnSequences = drawn.sequenceLabels.size();
  if (static_cast<std::size_t>(input.sequence) >= drawnSequences) {
    outcome.status = ViewModelAnimationStatus::SequenceOutOfRange;
    outcome.reason = "m_nSequence " + std::to_string(input.sequence)
      + " is past the entity's own model's " + std::to_string(drawnSequences) + " sequences";
    return outcome;
  }
  outcome.sequenceInRange = true;
  if (drawnBinding == nullptr) {
    outcome.reason = "the drawn model carries no animation data";
    return outcome;
  }
  AnimationSampleRequest request = timing;
  request.sequence = input.sequence;
  request.hasCycle = input.hasCycle;
  request.cycle = input.cycle;
  request.hasPlaybackRate = input.hasPlaybackRate;
  request.playbackRate = input.playbackRate;
  const AnimationSampleResult sampled = buildAnimationSkinMatrices(drawnBinding, request);
  outcome.sequenceLabel = sampled.sequenceLabel;
  outcome.frame = sampled.frame;
  outcome.frameCount = sampled.frameCount;
  if (sampled.bindPose || sampled.skinMatrices.empty()) {
    // T1's refusal, translated. A bone that did not line up and a skeleton too
    // large for the skinning buffer are both "the tables do not match", which is
    // one status here; everything else it can refuse is an absent or unusable
    // input, which is Missing. The decoder's own words are kept in `reason` so
    // the translation does not lose the detail.
    outcome.status = sampled.status == AnimationInputStatus::BoneMismatch
        || sampled.status == AnimationInputStatus::TooManyBones
      ? ViewModelAnimationStatus::BoneMismatch
      : ViewModelAnimationStatus::Missing;
    outcome.reason = sampled.reason;
    return outcome;
  }
  // The pose is indexed by the render skeleton's bone order, which is the same
  // order the mesh's bone indices use. A table that does not cover the drawn
  // skeleton would silently mis-index every vertex, so it is refused rather than
  // uploaded -- the same guard the standalone `--model` path applies.
  if (sampled.skinMatrices.size() != drawn.bones.size()) {
    outcome.status = ViewModelAnimationStatus::BoneMismatch;
    outcome.reason = "the pose covers " + std::to_string(sampled.skinMatrices.size())
      + " bones but the drawn model has " + std::to_string(drawn.bones.size());
    return outcome;
  }
  outcome.status = ViewModelAnimationStatus::Ok;
  outcome.bindPose = false;
  outcome.skinMatrices = sampled.skinMatrices;
  outcome.reason.clear();
  return outcome;
}

} // namespace tf2::native
