#include "asset_root.h"
#include "model_loader.h"
#include "viewmodel.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

const char* resolutionName(tf2::native::ModelAssetResolution value) {
  using tf2::native::ModelAssetResolution;
  switch (value) {
    case ModelAssetResolution::FoundLoose: return "found-loose";
    case ModelAssetResolution::FoundVpk: return "found-vpk";
    case ModelAssetResolution::Missing: return "missing";
    case ModelAssetResolution::Ambiguous: return "ambiguous";
    default: return "unknown";
  }
}

const char* statusName(tf2::native::ViewModelStatus value) {
  return value == tf2::native::ViewModelStatus::Ok ? "ok" : "failed";
}

const char* handName(tf2::native::ViewModelHand value) {
  return value == tf2::native::ViewModelHand::Left ? "left" : "right";
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

void usage() {
  std::cerr << "usage: viewmodel_contract_probe --tf-root <tf> --model <v_*.mdl>"
               " [--hand right|left] [--fov <n>] [--dump-attachments]\n";
}

const char* attachmentStatusName(tf2::native::ViewModelAttachmentStatus value) {
  using tf2::native::ViewModelAttachmentStatus;
  switch (value) {
    case ViewModelAttachmentStatus::Valid: return "valid";
    case ViewModelAttachmentStatus::Partial: return "partial";
    default: return "invalid";
  }
}

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path tfRoot;
  std::string model;
  std::string hand = "right";
  bool fovGiven = false;
  double fovValue = 0.0;
  bool dumpAttachments = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--tf-root" && index + 1 < argc) {
      tfRoot = std::filesystem::u8path(argv[++index]);
    } else if (argument == "--model" && index + 1 < argc) {
      model = argv[++index];
    } else if (argument == "--hand" && index + 1 < argc) {
      hand = argv[++index];
    } else if (argument == "--fov" && index + 1 < argc) {
      fovGiven = true;
      fovValue = std::atof(argv[++index]);
    } else if (argument == "--dump-attachments") {
      dumpAttachments = true;
    } else {
      usage();
      return 2;
    }
  }
  if (tfRoot.empty() || model.empty() || (hand != "right" && hand != "left")) {
    usage();
    return 2;
  }

  const auto root = tf2::native::AssetRoot::fromPath(tfRoot);
  const auto handValue = hand == "left" ? tf2::native::ViewModelHand::Left
                                        : tf2::native::ViewModelHand::Right;
  std::optional<float> fov;
  if (fovGiven) fov = static_cast<float>(fovValue);

  // The probe goes through the same builder the product uses. A probe that
  // re-implemented the request would prove nothing about the product path.
  const auto request = tf2::native::buildViewModelRequest(root, model, handValue, fov);

  const auto right = tf2::native::viewModelHandMatrix(tf2::native::ViewModelHand::Right);
  const auto left = tf2::native::viewModelHandMatrix(tf2::native::ViewModelHand::Left);
  const bool handProbePassed = right[0] == 1.0f && left[0] == -1.0f
    && right[5] == 1.0f && left[5] == 1.0f
    && right[10] == 1.0f && left[10] == 1.0f;
  // The mirror must be the only difference: Y and Z are untouched in both.
  const bool mirrorOnlyX = right[5] == left[5] && right[10] == left[10]
    && right[0] == -left[0];

  const bool resourceComplete = request.resources.mdlReadable && request.resources.vvdPresent
    && request.resources.vtxPresent;
  const bool fovClamped = fovGiven
    && std::fabs(request.fov - static_cast<float>(fovValue)) > 1e-4f;
  const bool attachmentsAllValid = request.attachmentCount != 0
    && request.attachments.size() == request.attachmentCount
    && request.attachmentsInvalid == 0 && request.attachmentsPartial == 0;
  // "Failed or stably fell back": the request is still usable, and the damaged
  // attachment was recorded rather than silently used.
  const bool fallbackStable = request.status == tf2::native::ViewModelStatus::Ok
    && (request.attachmentsInvalid == 0 || !request.attachmentReason.empty());
  // Is the *chosen* placement attachment's local matrix the identity? On
  // `v_scattergun_scout` it is, and that is a reading rather than a curiosity:
  // when A is the identity, no frame can separate "the attachment stage was
  // applied" from "it was skipped", so the chain's only witnesses are H (the
  // left-hand mirror) and T (the FOV and the placement translation). Asserting it
  // makes that limit explicit, and turns red if the asset ever changes enough to
  // give the A stage a witness of its own.
  bool alignmentIsIdentity = false;
  if (request.alignmentIndex >= 0
      && static_cast<std::size_t>(request.alignmentIndex) < request.attachments.size()) {
    const auto& chosen = request.attachments[static_cast<std::size_t>(request.alignmentIndex)];
    const float expected[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const float* basis[3] = {chosen.axisX.data(), chosen.axisY.data(), chosen.axisZ.data()};
    alignmentIsIdentity = true;
    for (int column = 0; column < 3; ++column) {
      for (int row = 0; row < 3; ++row) {
        if (std::fabs(basis[column][row] - expected[column][row]) > 1e-5f) alignmentIsIdentity = false;
      }
      if (std::fabs(chosen.origin[column]) > 1e-5f) alignmentIsIdentity = false;
    }
  }
  const bool contractOk = request.status == tf2::native::ViewModelStatus::Ok
    && resourceComplete && request.resources.meshReady && attachmentsAllValid
    && handProbePassed && request.alignmentIndex >= 0;

  std::cout << "{\"model\":\"" << jsonEscape(request.path) << "\""
    << ",\"resolution\":\"" << resolutionName(request.resolution) << "\""
    << ",\"status\":\"" << statusName(request.status) << "\""
    << ",\"reason\":\"" << jsonEscape(request.reason) << "\""
    << ",\"mdlReadable\":" << (request.resources.mdlReadable ? "true" : "false")
    << ",\"vvdPresent\":" << (request.resources.vvdPresent ? "true" : "false")
    << ",\"vtxPresent\":" << (request.resources.vtxPresent ? "true" : "false")
    << ",\"vtxVariant\":\"" << jsonEscape(request.resources.vtxVariant) << "\""
    << ",\"resourceComplete\":" << (resourceComplete ? "true" : "false")
    << ",\"companionsComplete\":" << (request.resources.companionsComplete ? "true" : "false")
    << ",\"meshReady\":" << (request.resources.meshReady ? "true" : "false")
    << ",\"meshReason\":\"" << jsonEscape(request.resources.meshReason) << "\""
    << ",\"meshVertices\":" << request.mesh.vertices.size()
    << ",\"meshIndices\":" << request.mesh.indices.size()
    << ",\"meshTriangleList\":" << (request.mesh.triangleList ? "true" : "false")
    << ",\"materialCount\":" << request.resources.materialCount
    << ",\"materialResolved\":" << request.resources.materialResolved
    << ",\"flatColorFallback\":" << (request.resources.flatColorFallback ? "true" : "false")
    << ",\"attachments\":" << request.attachments.size()
    << ",\"declaredAttachments\":" << request.attachmentCount
    << ",\"validAttachments\":" << request.attachmentsValid
    << ",\"partialAttachments\":" << request.attachmentsPartial
    << ",\"invalidAttachments\":" << request.attachmentsInvalid
    << ",\"attachmentStatus\":\"" << jsonEscape(request.attachmentReason) << "\""
    << ",\"alignmentName\":\"" << jsonEscape(request.alignmentName) << "\""
    << ",\"alignmentIndex\":" << request.alignmentIndex
    << ",\"alignmentReason\":\"" << jsonEscape(request.alignmentReason) << "\""
    << ",\"sequenceCount\":" << request.sequenceLabels.size()
    << ",\"bones\":" << request.bones.size()
    << ",\"defaultFov\":" << tf2::native::kDefaultViewModelFov
    << ",\"fov\":" << request.fov
    << ",\"fovGiven\":" << (fovGiven ? "true" : "false")
    << ",\"fovClamped\":" << (fovClamped ? "true" : "false")
    << ",\"fovLow\":" << tf2::native::normalizeViewModelFov(0.0f)
    << ",\"fovHigh\":" << tf2::native::normalizeViewModelFov(180.0f)
    << ",\"legacyFovDefault\":" << tf2::native::kLegacyViewModelFovDefaultInt
    << ",\"migratedLegacyFov\":" << tf2::native::migrateLegacyViewModelFov(
        tf2::native::kLegacyViewModelFovDefaultInt)
    << ",\"chosenFovKept\":" << tf2::native::migrateLegacyViewModelFov(101)
    << ",\"hand\":\"" << handName(request.hand) << "\""
    << ",\"rightHandX\":" << right[0]
    << ",\"leftHandX\":" << left[0]
    << ",\"handProbePassed\":" << (handProbePassed ? "true" : "false")
    << ",\"mirrorOnlyX\":" << (mirrorOnlyX ? "true" : "false")
    << ",\"fallbackStable\":" << (fallbackStable ? "true" : "false")
    << ",\"alignmentIsIdentity\":" << (alignmentIsIdentity ? "true" : "false")
    << ",\"drawPass\":\"app\""
    << ",\"contractOk\":" << (contractOk ? "true" : "false") << "}\n";
  if (dumpAttachments) {
    // The attachment *contents* were never validated before: model_loader.cpp's
    // only consumer was a count check (`attachments.size() == attachmentCount`),
    // which a table of zeroes passes. This dump is what turned "every real
    // attachment is invalid" into a reading instead of a guess.
    for (const auto& attachment : request.attachments) {
      std::cout << "attachment name=\"" << jsonEscape(attachment.name) << "\""
        << " bone=" << attachment.bone
        << " status=" << attachmentStatusName(attachment.status)
        << " reason=\"" << jsonEscape(attachment.reason) << "\""
        << " origin=" << attachment.origin[0] << "," << attachment.origin[1] << "," << attachment.origin[2]
        << " axisX=" << attachment.axisX[0] << "," << attachment.axisX[1] << "," << attachment.axisX[2]
        << " axisY=" << attachment.axisY[0] << "," << attachment.axisY[1] << "," << attachment.axisY[2]
        << " axisZ=" << attachment.axisZ[0] << "," << attachment.axisZ[1] << "," << attachment.axisZ[2]
        << "\n";
    }
  }

  return contractOk ? 0 : 1;
}
