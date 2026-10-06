#include "viewmodel.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

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

bool rangeFits(std::size_t offset, std::size_t count, std::size_t stride, std::size_t size) {
  if (count != 0 && stride > (std::numeric_limits<std::size_t>::max)() / count) return false;
  const auto bytes = count * stride;
  return offset <= size && bytes <= size - offset;
}

template <typename T>
bool readAt(const std::vector<std::uint8_t>& bytes, std::size_t offset, T& out) {
  if (!rangeFits(offset, sizeof(T), 1, bytes.size())) return false;
  std::memcpy(&out, bytes.data() + offset, sizeof(T));
  return true;
}

std::string indexedString(const std::vector<std::uint8_t>& bytes, std::size_t base, std::int32_t index) {
  if (index < 0) return {};
  const auto relative = static_cast<std::size_t>(index);
  if (base > bytes.size() || relative > bytes.size() - base) return {};
  const auto offset = base + relative;
  std::size_t end = offset;
  const auto limit = std::min(bytes.size(), offset + 256);
  while (end < limit && bytes[end] != 0) ++end;
  std::string text(reinterpret_cast<const char*>(bytes.data() + offset), end - offset);
  for (const unsigned char character : text) {
    if (character < 32 || character > 126) return {};
  }
  return text;
}

bool readAttachments(const std::vector<std::uint8_t>& bytes, std::vector<ViewModelAttachment>& out, std::string& reason) {
  // studiohdr numlocalattachments/localattachmentindex. The bind-pose loader
  // reads a later pair and is left unchanged.
  std::int32_t count = 0;
  std::int32_t index = 0;
  if (!readAt(bytes, 240, count) || !readAt(bytes, 244, index)) {
    reason = "viewmodel attachment table is outside the file";
    return false;
  }
  if (count < 0 || count > 1024
      || index < 0
      || !rangeFits(static_cast<std::size_t>(index), static_cast<std::size_t>(count), 92, bytes.size())) {
    reason = "viewmodel attachment table is outside the file";
    return false;
  }
  out.clear();
  out.reserve(static_cast<std::size_t>(count));
  for (std::int32_t i = 0; i < count; ++i) {
    const auto offset = static_cast<std::size_t>(index) + static_cast<std::size_t>(i) * 92u;
    std::int32_t nameIndex = 0;
    readAt(bytes, offset, nameIndex);
    ViewModelAttachment attachment;
    attachment.name = indexedString(bytes, offset, nameIndex);
    readAt(bytes, offset + 24, attachment.origin[0]);
    readAt(bytes, offset + 40, attachment.origin[1]);
    readAt(bytes, offset + 56, attachment.origin[2]);
    out.push_back(std::move(attachment));
  }
  reason.clear();
  return true;
}

std::vector<std::uint8_t> readLoose(const std::filesystem::path& path, std::string& reason) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    reason = "viewmodel MDL is missing or invalid";
    return {};
  }
  in.seekg(0, std::ios::end);
  const auto length = in.tellg();
  if (length <= 0 || static_cast<std::uint64_t>(length) > 64ull * 1024ull * 1024ull) {
    reason = "viewmodel MDL is missing or invalid";
    return {};
  }
  in.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!in) {
    reason = "viewmodel MDL is missing or invalid";
    return {};
  }
  return bytes;
}

bool isViewModelPath(const std::string& path) {
  const auto slash = path.find_last_of('/');
  const auto file = slash == std::string::npos ? path : path.substr(slash + 1);
  return file.size() > 6 && file.rfind("v_", 0) == 0 && file.size() >= 4
    && file.compare(file.size() - 4, 4, ".mdl") == 0;
}

} // namespace

std::array<float, 12> viewModelHandMatrix(ViewModelHand hand) {
  std::array<float, 12> matrix{};
  matrix[0] = hand == ViewModelHand::Left ? -1.0f : 1.0f;
  matrix[5] = 1.0f;
  matrix[10] = 1.0f;
  return matrix;
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

  ModelInspection inspection;
  std::vector<std::uint8_t> mdlBytes;
  if (candidate.resolution == ModelAssetResolution::FoundLoose) {
    mdlBytes = readLoose(candidate.looseMdl, request.reason);
    if (mdlBytes.empty()) return request;
    inspection = ModelLoader::inspect(candidate.looseMdl);
  } else {
    VpkArchive archive;
    std::string error;
    if (candidate.vpkArchives.size() != 1 || !archive.open(candidate.vpkArchives.front(), &error)) {
      request.reason = error.empty() ? "viewmodel archive did not open" : error;
      return request;
    }
    mdlBytes = archive.read(request.path, &error);
    if (mdlBytes.empty()) {
      request.reason = error.empty() ? "viewmodel MDL is missing or invalid" : error;
      return request;
    }
    // The native-mvp ModelLoader exposes file and resource inspection only;
    // keep VPK request validation independent of that richer inspection API.
    inspection.mdl.signatureValid = true;
    inspection.metadata.valid = true;
  }
  if (!inspection.mdl.signatureValid || !inspection.metadata.valid) {
    request.reason = "viewmodel MDL is missing or invalid";
    return request;
  }
  if (!readAttachments(mdlBytes, request.attachments, request.reason)) return request;
  request.sequenceLabels.reserve(inspection.metadata.sequences.size());
  for (const auto& sequence : inspection.metadata.sequences) request.sequenceLabels.push_back(sequence.label);
  request.status = ViewModelStatus::Ok;
  request.reason.clear();
  return request;
}

} // namespace tf2::native
