#include "viewmodel.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace tf2::native {
namespace {

std::string normalizePath(std::string value) {
  for (char& character : value) {
    if (character == '\\') character = '/';
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  while (!value.empty() && value.front() == '/') value.erase(value.begin());
  return value;
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

ViewModelRequest buildViewModelRequest(const AssetRoot& root, const std::string& path,
    ViewModelHand hand, std::optional<float> fov) {
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
  if (!isViewModelPath(request.path)) {
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

  request.status = ViewModelStatus::Ok;
  request.reason.clear();
  return request;
}

} // namespace tf2::native
