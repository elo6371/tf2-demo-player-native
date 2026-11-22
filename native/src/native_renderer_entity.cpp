// Entity models: bind-pose upload, bone matrices, meshes, textures and per-frame instances.
//
// One translation unit of the renderer split: the bodies were moved verbatim out
// of native_renderer.cpp, so pass order, thresholds and trace text are unchanged.

#include "native_renderer.h"
#include "renderer_internal.h"

#include "entity_model.h"
#include "hud_font.h"
#include "leaf_ambient_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tf2::native {

namespace {
// LAYER B: the resident mesh table's bound, and the one number this task did
// NOT raise. It is a cache bound, not a draw bound: capacity 96 means "at most
// 96 models resident at once", which is why the policy at the bound is
// eviction-and-refusal rather than "grow the constant". The ledger that
// publishes it is "budget: meshTable" in main.cpp.
constexpr std::size_t kEntityModelMeshCapacity = 96;
}  // namespace

std::size_t Renderer::entityModelMeshCapacity() const { return kEntityModelMeshCapacity; }

bool Renderer::uploadBindPoseModel(const std::vector<ModelDrawVertex>& vertices) {
  modelVertexBuffer_.Reset(); modelVertexCount_ = 0; modelGpuStatus_ = ModelGpuStatus::NotLoaded;
  if (vertices.empty() || vertices.size() > (std::numeric_limits<UINT>::max)()) { lastError_ = E_INVALIDARG; return false; }
  std::vector<ModelGpuVertex> gpu;
  gpu.reserve(vertices.size());
  std::array<float, 3> minimum = vertices.front().position, maximum = vertices.front().position;
  for (const auto& v : vertices) for (std::size_t axis = 0; axis < 3; ++axis) {
    minimum[axis] = (std::min)(minimum[axis], v.position[axis]);
    maximum[axis] = (std::max)(maximum[axis], v.position[axis]);
  }
  const float extent = (std::max)({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2], 1e-4f});
  const float scale = 1.2f / extent;
  const std::array<float, 3> center = {(minimum[0] + maximum[0]) * 0.5f, (minimum[1] + maximum[1]) * 0.5f, (minimum[2] + maximum[2]) * 0.5f};
  for (const auto& v : vertices) {
    ModelGpuVertex output{};
    output.x = (v.position[0]-center[0])*scale; output.y = (v.position[1]-center[1])*scale;
    output.z = (v.position[2]-center[2])*scale; output.r = 0.72f; output.g = 0.72f;
    output.b = 0.76f; output.a = 1.0f; output.u = v.texcoord[0]; output.v = v.texcoord[1];
    output.nx = v.normal[0]; output.ny = v.normal[1]; output.nz = v.normal[2];
    output.weights[0] = v.weights[0]; output.weights[1] = v.weights[1]; output.weights[2] = v.weights[2];
    output.boneIndices[0] = v.boneIndices[0]; output.boneIndices[1] = v.boneIndices[1];
    output.boneIndices[2] = v.boneIndices[2]; output.boneCount = v.boneCount;
    gpu.push_back(output);
  }
  D3D11_BUFFER_DESC desc{}; desc.ByteWidth = static_cast<UINT>(gpu.size() * sizeof(ModelGpuVertex)); desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = gpu.data();
  if (FAILED(device_->CreateBuffer(&desc, &initial, modelVertexBuffer_.GetAddressOf()))) { lastError_ = E_FAIL; return false; }
  modelVertexCount_ = static_cast<UINT>(gpu.size());
  modelGpuStatus_ = ModelGpuStatus::BindPoseOnly;
  return true;
}

bool Renderer::uploadBoneMatrices(const std::vector<std::array<float, 16>>& boneMatrices, bool enableSkinning) {
  if (!device_ || !context_ || !modelSkinningConstants_) { lastError_ = E_FAIL; return false; }
  constexpr std::size_t maxBones = 128;
  if (boneMatrices.empty() || boneMatrices.size() > maxBones) { lastError_ = E_INVALIDARG; return false; }
  for (const auto& matrix : boneMatrices) {
    for (const float element : matrix) {
      if (!std::isfinite(element)) { lastError_ = E_INVALIDARG; return false; }
    }
  }
  const std::size_t boneCount = boneMatrices.size();
  struct SkinningConstants {
    float bones[128][16];
    std::uint32_t boneCount;
    std::uint32_t skinEnabled;
    std::uint32_t padding[2];
  };
  SkinningConstants constants{};
  for (std::size_t i = 0; i < boneCount; ++i) {
    for (std::size_t j = 0; j < 16; ++j) {
      constants.bones[i][j] = boneMatrices[i][j];
    }
  }
  constants.boneCount = static_cast<std::uint32_t>(boneCount);
  constants.skinEnabled = enableSkinning ? 1u : 0u;
  constants.padding[0] = 0u;
  constants.padding[1] = 0u;
  context_->UpdateSubresource(modelSkinningConstants_.Get(), 0, nullptr, &constants, 0, 0);
  modelGpuStatus_ = enableSkinning ? ModelGpuStatus::SkinningReady : ModelGpuStatus::BindPoseOnly;
  return true;
}

bool Renderer::createTarget(UINT width, UINT height) {
  if (!swapChain_) return false;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) return false;
  if (FAILED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, target_.GetAddressOf()))) return false;
  D3D11_TEXTURE2D_DESC depthDescription{};
  depthDescription.Width = width;
  depthDescription.Height = height;
  depthDescription.MipLevels = 1;
  depthDescription.ArraySize = 1;
  depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depthDescription.SampleDesc.Count = 1;
  depthDescription.Usage = D3D11_USAGE_DEFAULT;
  depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(device_->CreateTexture2D(&depthDescription, nullptr, depthTexture_.GetAddressOf()))) return false;
  if (FAILED(device_->CreateDepthStencilView(depthTexture_.Get(), nullptr, depthStencilView_.GetAddressOf()))) return false;
  width_ = width;
  height_ = height;
  lastError_ = S_OK;
  return true;
}

void Renderer::releaseTarget() { depthStencilView_.Reset(); depthTexture_.Reset(); target_.Reset(); }

void Renderer::setEntityMarkers(const std::vector<EntityMarker>& markers) {
  entityMarkerVertexCount_ = 0;
  if (!device_ || !worldBoundsValid_ || markers.empty()) return;
  constexpr std::size_t maxMarkers = 256;
  std::vector<WorldVertex> vertices;
  vertices.reserve(std::min(markers.size(), maxMarkers) * 6u);
  const auto normalize = [this](float x, float y, float z) {
    return DirectX::XMFLOAT3((x - worldCenterX_) * worldHorizontalScale_,
      (y - worldCenterY_) * worldHorizontalScale_,
      (z - worldMinZ_) * worldHorizontalScale_ + 0.1f);
  };
  for (std::size_t i = 0; i < markers.size() && i < maxMarkers; ++i) {
    const auto& marker = markers[i];
    if (!std::isfinite(marker.position[0]) || !std::isfinite(marker.position[1]) || !std::isfinite(marker.position[2])) continue;
    const auto center = normalize(marker.position[0], marker.position[1], marker.position[2]);
    constexpr float extent = 12.0f;
    const auto x = normalize(marker.position[0] + extent, marker.position[1], marker.position[2]);
    const auto y = normalize(marker.position[0], marker.position[1] + extent, marker.position[2]);
    const auto z = normalize(marker.position[0], marker.position[1], marker.position[2] + extent);
    const auto vertex = [&](const DirectX::XMFLOAT3& p) {
      return WorldVertex{p.x, p.y, p.z, marker.color[0], marker.color[1], marker.color[2], marker.color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    };
    vertices.push_back(vertex(center)); vertices.push_back(vertex(x));
    vertices.push_back(vertex(center)); vertices.push_back(vertex(y));
    vertices.push_back(vertex(center)); vertices.push_back(vertex(z));
  }
  if (vertices.empty()) return;
  if (!entityMarkerVertexBuffer_ || entityMarkerVertexCapacity_ < vertices.size()) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> replacement;
    if (FAILED(device_->CreateBuffer(&description, nullptr, replacement.GetAddressOf()))) return;
    entityMarkerVertexBuffer_ = std::move(replacement);
    entityMarkerVertexCapacity_ = vertices.size();
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(entityMarkerVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(WorldVertex));
  context_->Unmap(entityMarkerVertexBuffer_.Get(), 0);
  entityMarkerVertexCount_ = static_cast<UINT>(vertices.size());
}

std::string Renderer::evictableEntityModelMeshKey() const {
  // Which resident to give up, or end() when every one of them outranks the
  // newcomer. The order is a policy, not a preference:
  //   1. never a mesh that outranks the newcomer (a body is never traded for a
  //      prop, a held weapon never for a world prop);
  //   2. never a mesh the most recent instance update actually drew. That mesh
  //      is behind a live draw range, and dropping it is mutation m8's defect --
  //      a submitted key with an empty frame -- arriving as a cache policy;
  //   3. among the rest: lowest priority first, then least recently drawn, then
  //      the key itself, so the choice is deterministic across two processes.
  //
  // T85 P0 review B1/M1: the candidate set is stated by the ladder, not by
  // comparing ranks with the newcomer (that comparison let the overlay -- rank 2,
  // and never touched by an instance update -- become the only victim, i.e. the
  // policy spent the nine MANDATORY silhouettes to admit a gun). Only
  // WorldProp/Deferred meshes may be spent; bodies, held weapons and the pinned
  // overlay are protected outright.
  std::string bestKey;
  int bestRank = -1;
  std::uint64_t bestTouch = 0;
  for (const auto& entry : entityModelMeshes_) {
    const auto& mesh = entry.second;
    if (mesh.pinned || mesh.mapStaticProp) continue;
    const int rank = static_cast<int>(mesh.priority);
    if (rank < static_cast<int>(EntityMeshPriority::WorldProp)) continue;
    // entityModelMeshFrame_ == 0 means no instance update has run yet (the
    // one-time upload pass), so nothing is live and everything is evictable.
    if (entityModelMeshFrame_ != 0 && mesh.lastTouchedFrame >= entityModelMeshFrame_) continue;
    const bool better = bestKey.empty()
      || rank > bestRank
      || (rank == bestRank && mesh.lastTouchedFrame < bestTouch)
      || (rank == bestRank && mesh.lastTouchedFrame == bestTouch && entry.first < bestKey);
    if (better) { bestKey = entry.first; bestRank = rank; bestTouch = mesh.lastTouchedFrame; }
  }
  return bestKey;
}

bool Renderer::uploadEntityModelMesh(const std::string& cacheKey, const std::vector<ModelDrawVertex>& vertices,
    const std::vector<ModelMeshMaterialRun>& runs, EntityMeshPriority priority, bool mapStaticProp) {
  if (!device_ || cacheKey.empty() || vertices.size() < 3) { lastError_ = E_INVALIDARG; return false; }
  // T85 P0: THE TABLE IS A BUDGETED CACHE, NOT A WALL.
  //
  // 96 is kept (2026-10-22 added it for the world pass's 64 models plus the
  // outline's nine class models, which share this table). What changed is what
  // happens at the bound: until now a full table refused an unknown key with
  // E_OUTOFMEMORY and never tried again, and the three held-weapon world models
  // this task exists for (c_flamethrower, c_sapper_xmas, c_shogun_warhorn) were
  // refused exactly there while 96 slots were held by models that outrank
  // nobody: first-person rigs, gibs and world props. A refusal with no policy is
  // the worst failure shape available here -- the draw list submits the key, the
  // counter counts it, and the frame is empty.
  //
  // So a full table now makes room: the least useful resident is evicted for a
  // more important newcomer (evictableEntityModelMeshKey), and only a newcomer that
  // outranks nothing resident is refused -- with a counter and a reason. Both
  // outcomes are published in "budget: meshTable" (main.cpp).
  const auto existing = entityModelMeshes_.find(cacheKey);
  const bool wasResident = existing != entityModelMeshes_.end();
  if (wasResident && existing->second.mapStaticProp != mapStaticProp) {
    lastError_ = E_INVALIDARG;
    return false;
  }
  if (!wasResident && mapStaticProp && mapStaticPropMeshCount() >= mapStaticPropMeshCapacityLimit) {
    lastError_ = E_OUTOFMEMORY;
    return false;
  }
  // T57 measured this line cutting every one of the nine player models at 12000
  // vertices (603..20724 LOD0 indices lost per class). The cap was a budget for
  // some model that was never named, applied to a list whose length the model
  // itself already determined. There is no cap now: the buffer is exactly the
  // de-indexed LOD0 list. If a cap is ever re-introduced the vertices it drops
  // are COUNTED here, so the loss is a published reading instead of a silent cut.
  const auto count = vertices.size() - (vertices.size() % 3);
  if (count < 3) { lastError_ = E_INVALIDARG; return false; }
  if (count != vertices.size()) ++entityModelMeshTruncatedCount_;
  std::vector<EntityModelGpuVertex> local(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& source = vertices[i];
    local[i] = EntityModelGpuVertex{
      source.position[0], source.position[1], source.position[2],
      source.normal[0], source.normal[1], source.normal[2],
      source.texcoord[0], source.texcoord[1],
      source.weights[0], source.weights[1], source.weights[2],
      source.boneIndices[0], source.boneIndices[1], source.boneIndices[2], source.boneCount};
  }
  // EntityModelGpuVertex carries the position as three scalars (x, y, z), not an
  // array: the first attempt indexed a member called position and did not compile.
  float boundsMin[3] = {local[0].x, local[0].y, local[0].z};
  float boundsMax[3] = {boundsMin[0], boundsMin[1], boundsMin[2]};
  for (const auto& vertex : local) {
    const float position[3] = {vertex.x, vertex.y, vertex.z};
    for (int axis = 0; axis < 3; ++axis) {
      boundsMin[axis] = std::min(boundsMin[axis], position[axis]);
      boundsMax[axis] = std::max(boundsMax[axis], position[axis]);
    }
  }
  // Validate the material runs against the buffer they describe BEFORE anything
  // is created. A run that runs past the end would draw another model's vertices,
  // and a gap would leave a hole in the mesh; both are silent on screen.
  std::vector<ModelMeshMaterialRun> validatedRuns;
  if (runs.empty()) {
    validatedRuns.push_back(ModelMeshMaterialRun{0u, static_cast<std::uint32_t>(count), 0u});
  } else {
    std::uint32_t expected = 0;
    for (const auto& run : runs) {
      if (run.vertexCount == 0 || run.vertexStart != expected
          || static_cast<std::size_t>(run.vertexStart) + run.vertexCount > count) {
        lastError_ = E_INVALIDARG;
        return false;
      }
      expected += run.vertexCount;
      validatedRuns.push_back(run);
    }
    if (expected != count) { lastError_ = E_INVALIDARG; return false; }
  }
  // N17 item 1: the slab's own numbers. Skipping range 97 removes it, which names
  // the run that draws it (models/player/scout.mdl, material slot 0, window
  // [0, 8490)) but says nothing about why those triangles look like a plank. Two
  // candidates can be told apart here, from the CPU copy the buffer was built from:
  // long triangles (a vertex dragged by its pose -- bind pose showed the shape
  // changes with the pose) and texture coordinates spanning the atlas (one triangle
  // smearing a whole sheet across the picture). Measured per material run;
  // TF2_NATIVE_UV_DUMP holds a substring of the cache key to report, so a normal
  // run prints nothing and cannot change a frame.
  static const char* const slabInspectEnv = std::getenv("TF2_NATIVE_UV_DUMP");
  if (slabInspectEnv != nullptr && !cacheKey.empty() && cacheKey.find(slabInspectEnv) != std::string::npos) {
    for (const auto& run : validatedRuns) {
      const auto squaredDistance = [](const EntityModelGpuVertex& p, const EntityModelGpuVertex& q) {
        const double dx = static_cast<double>(p.x) - q.x;
        const double dy = static_cast<double>(p.y) - q.y;
        const double dz = static_cast<double>(p.z) - q.z;
        return dx * dx + dy * dy + dz * dz;
      };
      double maxEdge = 0.0;
      double maxUvArea = 0.0;
      double farU = 0.0;
      double farV = 0.0;
      double worstU[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
      std::size_t triangles = 0;
      std::size_t longEdges = 0;
      std::size_t wideUv = 0;
      std::size_t nonFinite = 0;
      for (std::uint32_t t = 0; t + 2 < run.vertexCount; t += 3) {
        const auto& a = local[run.vertexStart + t];
        const auto& b = local[run.vertexStart + t + 1];
        const auto& c = local[run.vertexStart + t + 2];
        ++triangles;
        if (!std::isfinite(a.u) || !std::isfinite(a.v) || !std::isfinite(b.u) || !std::isfinite(b.v)
            || !std::isfinite(c.u) || !std::isfinite(c.v)) ++nonFinite;
        for (const auto* p : {&a, &b, &c}) {
          farU = std::max(farU, std::fabs(static_cast<double>(p->u)));
          farV = std::max(farV, std::fabs(static_cast<double>(p->v)));
        }
        const double edge = std::sqrt(std::max({squaredDistance(a, b), squaredDistance(b, c), squaredDistance(c, a)}));
        if (std::isfinite(edge)) {
          maxEdge = std::max(maxEdge, edge);
          if (edge > 8.0) ++longEdges;
        }
        const double uvArea = 0.5 * std::fabs((static_cast<double>(b.u) - a.u) * (static_cast<double>(c.v) - a.v)
          - (static_cast<double>(c.u) - a.u) * (static_cast<double>(b.v) - a.v));
        if (std::isfinite(uvArea)) {
          if (uvArea > maxUvArea) {
            maxUvArea = uvArea;
            worstU[0] = a.u; worstU[1] = a.v; worstU[2] = b.u; worstU[3] = b.v; worstU[4] = c.u; worstU[5] = c.v;
          }
          if (uvArea > 0.02) ++wideUv;
        }
      }
      std::printf("uv: key=%s slot=%u verts=%u tris=%zu maxEdge=%.2f longEdges=%zu maxUvArea=%.4f wideUv=%zu nanUv=%zu maxU=%.2f maxV=%.2f worstUv=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)\n",
        cacheKey.c_str(), run.materialSlot, run.vertexCount, triangles, maxEdge, longEdges,
        maxUvArea, wideUv, nonFinite, farU, farV,
        worstU[0], worstU[1], worstU[2], worstU[3], worstU[4], worstU[5]);
    }
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(count * sizeof(EntityModelGpuVertex));
  description.Usage = D3D11_USAGE_IMMUTABLE;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = local.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  // Review minor 2: the real HRESULT, not a generic E_FAIL -- the trace's
  // lastError is the only evidence a refusal is D3D memory pressure rather than
  // a policy decision, and it must stay the code D3D actually returned.
  const HRESULT createResult = device_->CreateBuffer(&description, &data, buffer.GetAddressOf());
  if (FAILED(createResult)) {
    lastError_ = createResult;
    return false;
  }
  EntityModelMesh mesh;
  mesh.vertexBuffer = std::move(buffer);
  mesh.cpuPositions.reserve(static_cast<std::size_t>(count) * 3);
  for (const auto& vertex : local) {
    mesh.cpuPositions.push_back(vertex.x);
    mesh.cpuPositions.push_back(vertex.y);
    mesh.cpuPositions.push_back(vertex.z);
  }
  mesh.vertexCount = static_cast<UINT>(count);
  mesh.runs = std::move(validatedRuns);
  for (int axis = 0; axis < 3; ++axis) {
    mesh.boundsMin[axis] = boundsMin[axis];
    mesh.boundsMax[axis] = boundsMax[axis];
  }
  // T85 P0 review B5: the newcomer's buffer exists before any victim is chosen.
  // Choosing first would let a build failure above (invalid runs, CreateBuffer)
  // spend a resident and still fail, turning a D3D error into a hole in the
  // frame. Here a failed build leaves the table exactly as it was.
  EntityMeshPriority residentPriority = priority;
  if (wasResident) {
    // Re-uploading a key drops the textures already attached to it, so this is a
    // reading and not a no-op: a bounded cache that rotates the attach's keys
    // would unpaint guns. The attach's own rule is "once per key"; the counter
    // proves it is still true after eviction exists.
    ++entityModelMeshReuploads_;
    // Never demote an existing key: the world pass and the attach can both ask
    // for the same model with different labels, and the higher label must win.
    if (static_cast<int>(existing->second.priority) < static_cast<int>(priority)) {
      residentPriority = existing->second.priority;
    }
  } else if (!mapStaticProp && entityModelMeshCount() >= kEntityModelMeshCapacity) {
    const std::string victimKey = evictableEntityModelMeshKey();
    if (victimKey.empty()) {
      // Every resident is protected (bodies, held weapons, pinned overlay) or was
      // drawn by the last instance update. Refuse, with the reason on the record:
      // ++capacityRefusals is the published "the table was full of things it may
      // not spend" counter, and the caller names the key.
      lastError_ = E_OUTOFMEMORY;
      ++entityModelMeshCapacityRefusals_;
      return false;
    }
    entityModelMeshes_.erase(victimKey);
    // The evicted mesh owned the vertex buffer the draw ranges point at, so BOTH
    // range sets are cleared in the SAME step: a range holding a released buffer
    // is a use-after-free, not a short frame. Entity ranges are rebuilt by the
    // next setEntityModelInstances; overlay ranges by the next overlay submit.
    // (A pinned overlay mesh can never be the victim; this clear is the belt to
    // that brace, not the braces.)
    entityModelDrawRanges_.clear();
    overlayDrawRanges_.clear();
    entityModelTexturedRangeCount_ = 0;
    entityModelTexturedInstanceCount_ = 0;
    // B4: the caller owns the path->key tables; hand it the victim so it can
    // unpublish and re-supply instead of leaving a dead key behind.
    entityModelMeshEvictedKeys_.push_back(victimKey);
    ++entityModelMeshEvictions_;
  }
  EntityModelMesh& resident = entityModelMeshes_[cacheKey];
  resident = std::move(mesh);
  resident.priority = residentPriority;
  resident.pinned = residentPriority == EntityMeshPriority::Overlay;
  resident.mapStaticProp = mapStaticProp;
  resident.lastTouchedFrame = entityModelMeshFrame_;
  entityModelDrawRanges_.clear();
  entityModelVertexCount_ = 0;
  entityModelInstanceCount_ = 0;
  entityModelInstanceSlotCount_ = 0;
  entityModelTexturedRangeCount_ = 0;
  entityModelTexturedInstanceCount_ = 0;
  return true;
}

bool Renderer::uploadEntityModelTexture(const std::string& cacheKey, std::uint32_t materialSlot,
    std::uint32_t skinFamily, const std::vector<std::uint8_t>& rgba, UINT width, UINT height,
    std::uint32_t materialFlags, const std::vector<std::uint8_t>& secondRgba,
    UINT secondWidth, UINT secondHeight, const VmtMaterialProxy& proxy) {
  if (!device_ || cacheKey.empty() || rgba.empty() || width == 0 || height == 0) { lastError_ = E_INVALIDARG; return false; }
  const auto mesh = entityModelMeshes_.find(cacheKey);
  if (mesh == entityModelMeshes_.end()) { lastError_ = E_INVALIDARG; return false; }
  Texture2D texture;
  std::string error;
  if (!texture.create(device_.Get(), rgba, width, height, &error)) {
    if (!error.empty()) OutputDebugStringA(("Entity model texture upload failed: " + error + "\n").c_str());
    lastError_ = E_INVALIDARG;
    return false;
  }
  const auto key = EntityModelMesh::textureKey(materialSlot, skinFamily);
  Texture2D secondTexture;
  if (!secondRgba.empty()) {
    if (!secondTexture.create(device_.Get(), secondRgba, secondWidth, secondHeight, &error)) {
      lastError_ = E_INVALIDARG;
      return false;
    }
    materialFlags |= 128u;
  } else {
    materialFlags &= ~128u;
  }
  const bool existed = mesh->second.textures.find(key) != mesh->second.textures.end();
  mesh->second.textures[key] = std::move(texture);
  mesh->second.materialFlags[key] = materialFlags;
  mesh->second.materialProxies[key] = proxy;
  if (!secondRgba.empty()) mesh->second.secondTextures[key] = std::move(secondTexture);
  else mesh->second.secondTextures.erase(key);
  if (!existed) ++entityModelTextureCount_;
  return true;
}

// T105/task-37 Half B: arm the per-instance trace for exactly one frame.
//
// The caller arms this on the same iteration that requests the capture, so the
// lines describe the frame that gets written rather than whichever frame
// happened to be drawn first. Two counters travel with the frame so "the gate
// admitted nothing" cannot be mistaken for "the gate admitted twelve and the
// cap swallowed them": printed counts the instances that reached the report,
// skipped counts the ones the entity filter or the 16-line cap turned away.
void Renderer::armModelLightingTrace(std::int64_t tick,
  const std::unordered_set<std::uint16_t>& entities) {
  modelLightingTraceTick_ = tick;
  modelLightingTraceEntities_ = entities;
  modelLightingTraceLines_.clear();
  leafAmbientTracePrinted_ = 0;
  leafAmbientTraceSkipped_ = 0;
  leafAmbientTraceTick_ = tick;
}

void Renderer::setEntityModelInstances(const std::vector<EntityModelDrawInstance>& instances) {
  entityModelVertexCount_ = 0;
  entityModelInstanceCount_ = 0;
  mapStaticPropInstanceCount_ = 0;
  mapStaticPropInstanceRejected_ = 0;
  entityModelInstanceSlotCount_ = 0;
  entityModelSkinnedInstanceCount_ = 0;
  entityModelTexturedRangeCount_ = 0;
  entityModelTexturedInstanceCount_ = 0;
  entityModelUntexturedRangeCount_ = 0;
  entityModelFirstUntexturedRange_.clear();
  entityModelDrawRanges_.clear();
  // T105/task-37 Half B: the leaf account is per instance update, like every
  // other counter above. Not zeroing them here would make the capture line
  // report every frame's instances added together, which is not a frame reading.
  leafAmbientWithCube_ = 0;
  leafAmbientNoCubeInLeaf_ = 0;
  leafAmbientOutsideLeaf_ = 0;
  leafAmbientNoLump_ = 0;
  leafAmbientNotAsked_ = 0;
  leafAmbientWithCubeEmpty_ = 0;
  leafAmbientSlotCount_ = 0;
  // The identity is also reset here, and `checked` is decided by the ARM, not
  // by whether the walk below got far enough to sum: an early return (no
  // device, no instances) must read as a true 0 == 0 identity for an armed
  // update, and as "n/a" -- not as a verified zero -- for an unarmed one.
  leafAmbientRequests_ = 0;
  leafAmbientIdentityChecked_ = leafAmbientEffective();
  leafAmbientIdentityOk_ = true;
  // The trace account is per update for the same reason: "the gate admitted no
  // instance" and "the gate admitted 12" must not be the same printed number.
  leafAmbientTracePrinted_ = 0;
  leafAmbientTraceSkipped_ = 0;
  if (!device_ || !context_ || !worldBoundsValid_ || instances.empty() || entityModelMeshes_.empty()) return;
  if (!std::isfinite(worldCenterX_) || !std::isfinite(worldCenterY_) || !std::isfinite(worldMinZ_)
      || !std::isfinite(worldSpanZ_) || !std::isfinite(worldHorizontalScale_)
      || worldSpanZ_ <= 0.0f || worldHorizontalScale_ <= 0.0f) return;
  EntityModelWorldMap world;
  world.centerX = worldCenterX_;
  world.centerY = worldCenterY_;
  world.minZ = worldMinZ_;
  world.horizontalScale = worldHorizontalScale_;
  world.zScale = worldHorizontalScale_;
  constexpr std::size_t maxInstances = entityModelInstanceCapacityLimit;
  // A bucket is one (model, skin family) pair: the same model worn by a red and a
  // blue player shares the vertex buffer but not the paint, so it needs one range
  // per material run per family. Keying only by cacheKey -- as this did before T60
  // -- made the two teams one draw and one texture.
  struct Bucket {
    const EntityModelMesh* mesh = nullptr;
    std::uint32_t skinFamily = 0;
    std::uint32_t body = 0;
    // The bucket's own identity string, kept so a range can be named by the same
    // model#family#body key the instance trace uses.
    std::string key;
    std::vector<EntityModelInstanceGpu> items;
  };
  std::vector<Bucket> buckets;
  std::unordered_map<std::string, std::size_t> bucketOf;
  buckets.reserve(8);
  bucketOf.reserve(8);
  // T105/task-37 Half B: the frame's de-duplicated leaf cube table. Distinct
  // cubes only -- a row of props in one room resolves to one leaf and must cost
  // one slot, not one slot per prop. Filled while the instances are walked and
  // uploaded once, below, so the shader indexes cubes rather than instances.
  std::vector<LeafAmbientCubeFace> leafCubes;
  leafCubes.reserve(8);
  std::size_t accepted = 0;
  std::size_t staticAccepted = 0;
  // T85 P0: one frame counter and four refusal counters. Until this change every
  // rejection in the loop below was a bare `continue`: a key with no mesh, a
  // buffer that could not be drawn, a non-finite matrix and an instance past the
  // 96-instance draw capacity all produced the same invisible short frame. A
  // bounded budget is allowed to refuse -- it is not allowed to be silent.
  ++entityModelMeshFrame_;
  entityModelInstanceRejectedMissingMesh_ = 0;
  entityModelInstanceRejectedDeferredNotResident_ = 0;
  entityModelInstanceRejectedBadMesh_ = 0;
  entityModelInstanceRejectedNonFinite_ = 0;
  entityModelInstanceRejectedBudget_ = 0;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    const auto& instance = instances[i];
    const auto reject = [&](std::size_t& entityCounter) {
      if (instance.mapStaticProp) ++mapStaticPropInstanceRejected_;
      else ++entityCounter;
    };
    if (instance.cacheKey.empty()) { reject(entityModelInstanceRejectedBadMesh_); continue; }
    const auto mesh = entityModelMeshes_.find(instance.cacheKey);
    if (mesh == entityModelMeshes_.end()) {
      // Two different facts, counted apart (lead review, point 2): a model the
      // 64-slot prepare budget deferred is a bounded, published decision, while a
      // key the caller believes in with nothing behind it is the m8 defect. One
      // counter for both made the defect indistinguishable from the budget.
      if (instance.budgetDeferred) { reject(entityModelInstanceRejectedDeferredNotResident_); }
      else { reject(entityModelInstanceRejectedMissingMesh_); }
      continue;
    }
    if (mesh->second.mapStaticProp != instance.mapStaticProp
        || !mesh->second.vertexBuffer || mesh->second.vertexCount < 3
        || (mesh->second.vertexCount % 3) != 0) { reject(entityModelInstanceRejectedBadMesh_); continue; }
    EntityModelInstanceRows rows;
    if (!buildEntityModelInstanceRows(instance.origin, instance.angles, instance.hasAngles,
          world, rows, instance.sourceModelSpace)) {
      reject(entityModelInstanceRejectedNonFinite_);
      continue;
    }
    // An instance placed by a local matrix rides on another model's bone: the
    // rows just built carry the PARENT's frame to world, so the local goes
    // inside the composition -- C = P * L in this file's row-major "v' = M v"
    // convention. On the outside it would swing the held model around the
    // parent's origin instead of around the hand.
    if (instance.hasLocalMatrix) {
      float local[12];
      bool finite = true;
      for (int k = 0; k < 12; ++k) {
        local[k] = instance.localPositionRows[k];
        if (!std::isfinite(local[k])) finite = false;
      }
      // One non-finite row would poison every vertex of the instance, and a
      // silently vanished weapon is the failure mode this whole path exists to
      // remove; refuse the instance instead and let the caller count it.
      if (!finite) { reject(entityModelInstanceRejectedNonFinite_); continue; }
      EntityModelInstanceRows composed;
      for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
          float sum = 0.0f;
          for (int k = 0; k < 3; ++k) sum += rows.positionRows[row * 4 + k] * local[k * 4 + column];
          composed.positionRows[row * 4 + column] = sum;
          float normal = 0.0f;
          for (int k = 0; k < 3; ++k) normal += rows.normalRows[row * 3 + k] * local[k * 4 + column];
          composed.normalRows[row * 3 + column] = normal;
        }
        float translation = rows.positionRows[row * 4 + 3];
        for (int k = 0; k < 3; ++k) translation += rows.positionRows[row * 4 + k] * local[k * 4 + 3];
        composed.positionRows[row * 4 + 3] = translation;
      }
      rows = composed;
    }
    // The draw capacity is layer D's twin in this file: at most maxInstances
    // entity instances reach one update. Past it the instance is REFUSED and
    // counted; it is not silently skipped.
    if ((instance.mapStaticProp && staticAccepted >= mapStaticPropInstanceCapacityLimit)
        || (!instance.mapStaticProp && accepted - staticAccepted >= maxInstances)) {
      reject(entityModelInstanceRejectedBudget_);
      continue;
    }
    // A mesh this update actually asked for is not evictable this frame. The
    // eviction policy reads this stamp (evictForEntityModelMesh).
    mesh->second.lastTouchedFrame = entityModelMeshFrame_;
    // N17 item 3: the bodygroup selector applies to any instance that carries
    // one, not only to a BSP static prop -- a player body has m_nBody too. Zero
    // is the neutral value: a model with no bodygroup table has bodyCount == 0,
    // so every run still draws.
    const std::uint32_t selectedBody = instance.body;
    const std::string bucketKey = instance.cacheKey + "#" + std::to_string(instance.skinFamily)
      + "#body=" + std::to_string(selectedBody);
    const auto inserted = bucketOf.emplace(bucketKey, buckets.size());
    if (inserted.second) {
      Bucket created;
      created.mesh = &mesh->second;
      created.skinFamily = instance.skinFamily;
      created.body = selectedBody;
      created.key = bucketKey;
      buckets.push_back(std::move(created));
    }
    EntityModelInstanceGpu gpu{};
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 4; ++column) gpu.position[row][column] = rows.positionRows[row * 4 + column];
      for (int column = 0; column < 3; ++column) gpu.normal[row][column] = rows.normalRows[row * 3 + column];
    }
    // T105/task-37 Half B: this instance's baked ambient cube -> one slot in the
    // frame table. The slot rides in gpu.normal[0][3], the .w of the first normal
    // row, because that lane is dead: the entity vertex shader reads the row as
    // input.nrow0.xyz only (native_renderer_shaders.cpp:1059) and nothing else
    // samples it. It is the only free lane in the instance record -- padding[0]
    // and padding[1] are both live (boneCount here, material flags below) and the
    // struct is 128 bytes under static_assert, so the feature cannot grow it.
    //
    // A caller that had no cube gets the sentinel, NOT slot 0 and NOT a zero
    // cube: slot 0 is a real cube somebody else's leaf owns, and a zero cube
    // reads as "in the dark" when the truth is "no bake here". Which of the
    // absences it was is counted separately below.
    float ambientSlotValue = kLeafAmbientNoSlot;
    int ambientSlotIndex = -1;
    if (leafAmbientEffective()) {
      if (instance.hasLeafCube) {
        LeafAmbientCubeFace face;
        for (int direction = 0; direction < kLeafAmbientFaces; ++direction) {
          for (int channel = 0; channel < 3; ++channel) {
            face.rgb[direction][channel] = instance.leafCubeColor[direction][channel] * kLeafAmbientScale;
          }
        }
        ambientSlotIndex = static_cast<int>(leafAmbientSlotFor(leafCubes, face));
        ambientSlotValue = static_cast<float>(ambientSlotIndex);
        ++leafAmbientWithCube_;
        // The empty-but-present cube: counted as a HIT above (it is one)
        // and additionally here, so a frame can report how many of its
        // cube-bearing instances are the black-bake case instead of
        // leaving that to a map census. Half A's 42.65% is the OTHER case
        // -- a leaf that stores no sample at all; conflating the two would
        // make the feature look like a no-op on the leaves it does cover.
        bool cubeIsEmpty = true;
        for (int direction = 0; direction < kLeafAmbientFaces && cubeIsEmpty; ++direction) {
          for (int channel = 0; channel < 3; ++channel) {
            if (face.rgb[direction][channel] != 0.0f) { cubeIsEmpty = false; break; }
          }
        }
        if (cubeIsEmpty) ++leafAmbientWithCubeEmpty_;
      } else {
        switch (instance.leafCubeAbsence) {
          case EntityLeafCubeAbsence::OutsideLeaf: ++leafAmbientOutsideLeaf_; break;
          case EntityLeafCubeAbsence::LeafWithoutCube: ++leafAmbientNoCubeInLeaf_; break;
          case EntityLeafCubeAbsence::MapWithoutLump: ++leafAmbientNoLump_; break;
          case EntityLeafCubeAbsence::NotAsked: default: ++leafAmbientNotAsked_; break;
        }
      }
    }
    gpu.normal[0][3] = ambientSlotValue;
    // T105: the read-only half of the world-space normal claim. These are the
    // exact rows that reach the GPU above, printed for the instances that are
    // actually drawn, so "the entity vertex shader rotates a model normal by
    // this matrix, therefore its pixel shader sees a world normal" is checkable
    // against numbers instead of being taken from the shader source. The
    // falsifier is the second instance of the same model at a different angle:
    // model-space normals would be identical for both, and these rows are not.
    // Deduped on the printed text and capped, because this runs on every
    // playback frame long before the capture frame, and a trace that repeats
    // thousands of identical lines documents nothing. Off by default, so an
    // ordinary run prints nothing.
    // T105/task-37 Half B: the gate. Before this, the block below ran on EVERY
    // playback frame and deduped on the printed text; the 16-line cap then filled
    // during the first frames of the demo, which is why every earlier leaf trace
    // is world props and never a capture-frame player. The gate is the capture
    // frame itself (armed by the caller) narrowed to the player bodies that frame
    // actually draws, so the lines describe the picture the operator is looking
    // at instead of the first props the engine happened to submit.
    const bool traceArmed = settings_.modelLightingTrace && modelLightingTraceTick_ >= 0;
    const bool traceWanted = traceArmed
      && (modelLightingTraceEntities_.empty()
          || (instance.entityIndex >= 0
              && modelLightingTraceEntities_.count(
                   static_cast<std::uint16_t>(instance.entityIndex)) != 0));
    if (traceWanted) {
      char line[512];
      std::snprintf(line, sizeof(line),
        "lighting-normal: index=%d key=%s origin=(%.3f,%.3f,%.3f) rows=(%.4f,%.4f,%.4f)(%.4f,%.4f,%.4f)(%.4f,%.4f,%.4f)",
        instance.entityIndex,
        instance.cacheKey.empty() ? "(unknown)" : instance.cacheKey.c_str(),
        instance.origin[0], instance.origin[1], instance.origin[2],
        rows.normalRows[0], rows.normalRows[1], rows.normalRows[2],
        rows.normalRows[3], rows.normalRows[4], rows.normalRows[5],
        rows.normalRows[6], rows.normalRows[7], rows.normalRows[8]);
      if (modelLightingTraceLines_.size() < 16
          && modelLightingTraceLines_.insert(std::string(line)).second) {
        std::printf("%s", line);
        std::printf("\n");
        // T105/task-37 Half B: the same instance's baked ambient cube, printed
        // from the SAME trace site as the rows above so the cube and the rows
        // that carry it are never in doubt about which instance they describe.
        //   leaf   the BSP leaf the caller resolved, -1 when it resolved none
        //   slot   the table row the pixel shader will index; -1 is the
        //          sentinel that falls back to the global cube
        //   absent why there is no cube, when there is none (four answers,
        //          counted apart -- see EntityLeafCubeAbsence)
        //   cube   the six faces AS THE MAP SHIPPED THEM, linear and unscaled,
        //          in (+X,-X,+Y,-Y,+Z,-Z), so the scale constant can be
        //          checked against real data rather than taken on trust
        char leafLine[1024];
        std::snprintf(leafLine, sizeof(leafLine),
          "lighting-leaf: index=%d key=%s origin=(%.3f,%.3f,%.3f) point=(%.3f,%.3f,%.3f)"
          " leaf=%d slot=%d absent=%s"
          " cube=(%.6f,%.6f,%.6f)(%.6f,%.6f,%.6f)(%.6f,%.6f,%.6f)"
          "(%.6f,%.6f,%.6f)(%.6f,%.6f,%.6f)(%.6f,%.6f,%.6f)",
          instance.entityIndex,
          instance.cacheKey.empty() ? "(unknown)" : instance.cacheKey.c_str(),
          instance.origin[0], instance.origin[1], instance.origin[2],
          instance.leafCubePoint[0], instance.leafCubePoint[1], instance.leafCubePoint[2],
          instance.leafCubeIndex, ambientSlotIndex,
          leafCubeAbsenceName(instance.leafCubeAbsence),
          instance.leafCubeColor[0][0], instance.leafCubeColor[0][1], instance.leafCubeColor[0][2],
          instance.leafCubeColor[1][0], instance.leafCubeColor[1][1], instance.leafCubeColor[1][2],
          instance.leafCubeColor[2][0], instance.leafCubeColor[2][1], instance.leafCubeColor[2][2],
          instance.leafCubeColor[3][0], instance.leafCubeColor[3][1], instance.leafCubeColor[3][2],
          instance.leafCubeColor[4][0], instance.leafCubeColor[4][1], instance.leafCubeColor[4][2],
          instance.leafCubeColor[5][0], instance.leafCubeColor[5][1], instance.leafCubeColor[5][2]);
        std::printf("%s", leafLine);
        std::printf("\n");
        ++leafAmbientTracePrinted_;
      } else {
        ++leafAmbientTraceSkipped_;
      }
    } else if (traceArmed) {
      ++leafAmbientTraceSkipped_;
    }
    gpu.color[0] = instance.color[0];
    gpu.color[1] = instance.color[1];
    gpu.color[2] = instance.color[2];
    gpu.color[3] = instance.color[3];
    // Skinning is only advertised when the caller actually supplied matrices for
    // this instance. An instance whose boneOffset would run past the uploaded
    // buffer is drawn unskinned rather than reading another entity's bones --
    // a wrong pose is worse than a static one, and it is silent.
    const bool skinned = instance.skinningEnabled
      && instance.boneCount > 0
      && static_cast<std::size_t>(instance.boneOffset) + instance.boneCount <= entityBoneMatrixCount_;
    if (skinned) {
      gpu.boneOffset = instance.boneOffset;
      gpu.skinEnabled = 1u;
      gpu.padding[0] = instance.boneCount;
      ++entityModelSkinnedInstanceCount_;
    }
    buckets[inserted.first->second].items.push_back(gpu);
    ++accepted;
    if (instance.mapStaticProp) ++staticAccepted;
  }
  if (accepted == 0 || buckets.empty()) return;
  std::vector<EntityModelInstanceGpu> packed;
  packed.reserve(accepted);
  entityModelDrawRanges_.reserve(buckets.size());
  UINT uniqueVertices = 0;
  for (const auto& bucket : buckets) {
    if (bucket.items.empty() || bucket.mesh == nullptr || bucket.mesh->vertexCount < 3) continue;
    bool instanceCountedAsTextured = false;
    for (const auto& run : bucket.mesh->runs) {
      if (run.vertexCount < 3) continue;
      // N17 item 3: every bucket that has a bodygroup selection filters its runs,
      // not just a static prop. Runs built without body metadata (bodyCount == 0)
      // are unaffected, so this cannot remove geometry from a model that has no
      // selector.
      if (!run.selectedForBody(bucket.body)) continue;
      EntityModelDrawRange range;
      range.vertexBuffer = bucket.mesh->vertexBuffer.Get();
      range.vertexStart = run.vertexStart;
      range.vertexCount = run.vertexCount;
      range.cacheKey = bucket.key;
      range.body = bucket.body;
      range.instanceStart = static_cast<UINT>(packed.size());
      range.instanceCount = static_cast<UINT>(bucket.items.size());
      const auto texture = bucket.mesh->textures.find(
        EntityModelMesh::textureKey(run.materialSlot, bucket.skinFamily));
      range.textureView = texture == bucket.mesh->textures.end() ? nullptr : texture->second.view();
      if (!range.textureView) {
        // N17 item 4: the draw loop skips a range without a material. Count it and
        // keep the first example so the reason is on the record; a silent skip is
        // what made 313 ranges read as 312 with no owner.
        ++entityModelUntexturedRangeCount_;
        if (entityModelFirstUntexturedRange_.empty()) {
          entityModelFirstUntexturedRange_ = bucket.key + "#slot=" + std::to_string(run.materialSlot)
            + "#verts=" + std::to_string(run.vertexCount)
            + (bucket.mesh->mapStaticProp ? "#prop" : "#entity");
        }
      }
      const auto flags = bucket.mesh->materialFlags.find(
        EntityModelMesh::textureKey(run.materialSlot, bucket.skinFamily));
      const auto materialFlags = flags == bucket.mesh->materialFlags.end() ? 0u : flags->second;
      range.noCull = (materialFlags & 256u) != 0u;
      range.materialFlags = materialFlags;
      range.materialSlot = run.materialSlot;
      range.translucent = (materialFlags & (8u | 64u | 1024u)) != 0u;
      const auto second = bucket.mesh->secondTextures.find(
        EntityModelMesh::textureKey(run.materialSlot, bucket.skinFamily));
      range.secondTextureView = second == bucket.mesh->secondTextures.end() ? nullptr : second->second.view();
      const auto proxy = bucket.mesh->materialProxies.find(
        EntityModelMesh::textureKey(run.materialSlot, bucket.skinFamily));
      if (proxy != bucket.mesh->materialProxies.end()) range.proxy = proxy->second;
      if (range.proxy.textureScroll && std::isfinite(entityModelTimeSeconds_)) {
        constexpr double kPi = 3.14159265358979323846;
        const double angle = static_cast<double>(range.proxy.textureScrollAngle) * kPi / 180.0;
        const double distance = entityModelTimeSeconds_ * range.proxy.textureScrollRate;
        const auto wrap = [](double value) {
          value -= std::floor(value);
          return static_cast<float>(value);
        };
        range.proxyScrollU = wrap(distance * std::cos(angle));
        range.proxyScrollV = wrap(distance * std::sin(angle));
      }
      if (range.proxy.sineColor && std::isfinite(entityModelTimeSeconds_)
          && range.proxy.sinePeriod > 0.0f) {
        constexpr double kTau = 6.28318530717958647692;
        const double phase = entityModelTimeSeconds_ * kTau / range.proxy.sinePeriod;
        range.proxyColor = range.proxy.sineMin + (range.proxy.sineMax - range.proxy.sineMin)
          * static_cast<float>((std::sin(phase) + 1.0) * 0.5);
      }
      packed.insert(packed.end(), bucket.items.begin(), bucket.items.end());
      for (std::size_t item = range.instanceStart; item < packed.size(); ++item) {
        packed[item].padding[1] = materialFlags;
      }
      entityModelDrawRanges_.push_back(range);
      if (range.textureView) {
        ++entityModelTexturedRangeCount_;
        // An instance covered by any of its model's textured runs is a textured
        // instance; counting it per run would report more instances than exist.
        if (!instanceCountedAsTextured) {
          if (!bucket.mesh->mapStaticProp) entityModelTexturedInstanceCount_ += range.instanceCount;
          instanceCountedAsTextured = true;
        }
      }
    }
    uniqueVertices += bucket.mesh->vertexCount;
  }
  if (packed.empty()) {
    entityModelDrawRanges_.clear();
    entityModelTexturedRangeCount_ = 0;
    entityModelTexturedInstanceCount_ = 0;
    return;
  }
  if (!entityModelInstanceBuffer_ || entityModelInstanceCapacity_ < packed.size()) {
    D3D11_BUFFER_DESC description{};
    const auto capacity = std::max(packed.size(), std::size_t{96});
    description.ByteWidth = static_cast<UINT>(capacity * sizeof(EntityModelInstanceGpu));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> replacement;
    if (FAILED(device_->CreateBuffer(&description, nullptr, replacement.GetAddressOf()))) {
      entityModelDrawRanges_.clear();
      entityModelTexturedRangeCount_ = 0;
      entityModelTexturedInstanceCount_ = 0;
      entityModelUntexturedRangeCount_ = 0;
      entityModelFirstUntexturedRange_.clear();
      return;
    }
    entityModelInstanceBuffer_ = std::move(replacement);
    entityModelInstanceCapacity_ = capacity;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(entityModelInstanceBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    entityModelDrawRanges_.clear();
    entityModelTexturedRangeCount_ = 0;
    entityModelTexturedInstanceCount_ = 0;
    return;
  }
  std::memcpy(mapped.pData, packed.data(), packed.size() * sizeof(EntityModelInstanceGpu));
  context_->Unmap(entityModelInstanceBuffer_.Get(), 0);
  entityModelVertexCount_ = uniqueVertices;
  // Two readings, not one. "accepted" is how many entities were drawn;
  // packed.size() is how many instance rows the ranges carry, i.e. the same
  // entities counted once per material run. The textured-instance numerator is
  // per instance (a bucket is counted once), so its denominator has to be
  // accepted; using the slot sum here made N/N unreachable for every model with
  // more than one run -- 91/201 on 2026-10-28 -- and the gate read that as a
  // paint defect when the paint was mostly there.
  entityModelInstanceCount_ = accepted - staticAccepted;
  mapStaticPropInstanceCount_ = staticAccepted;
  // T105/task-37 Half B: the counter identity, asserted where both sides are
  // final. Every instance `accepted` above passed through the leaf block of
  // this same loop exactly once, so the five buckets must sum to it. This is
  // not a tolerance: a bucket that lost an instance would draw one model with
  // the global cube instead of its leaf, which reads as a slightly darker
  // patch and names no leaf. On a mismatch every term is printed on one line
  // and lastError_ is set, which the device-error policy turns into a
  // non-zero exit -- the assertion is not a comment.
  if (leafAmbientIdentityChecked_) {
    leafAmbientRequests_ = leafAmbientWithCube_ + leafAmbientNoCubeInLeaf_
      + leafAmbientOutsideLeaf_ + leafAmbientNoLump_ + leafAmbientNotAsked_;
    leafAmbientIdentityOk_ = leafAmbientRequests_ == accepted;
    if (!leafAmbientIdentityOk_) {
      std::fprintf(stderr,
        "[renderer-invariant] leaf-ambient counter-identity requests=%zu instances=%zu "
        "withCube=%zu noCubeInLeaf=%zu outsideLeaf=%zu noLump=%zu notAsked=%zu\n",
        leafAmbientRequests_, accepted, leafAmbientWithCube_, leafAmbientNoCubeInLeaf_,
        leafAmbientOutsideLeaf_, leafAmbientNoLump_, leafAmbientNotAsked_);
      lastError_ = E_UNEXPECTED;
    }
    // The black-cube count is a subset of withCube by construction; assert
    // it instead of leaving the reader to assume it. An all-zero cube that
    // ended in a miss bucket would be a silent change on exactly the leaves
    // whose bake is black.
    if (leafAmbientWithCubeEmpty_ > leafAmbientWithCube_) {
      std::fprintf(stderr,
        "[renderer-invariant] leaf-ambient empty-cube-subset withCubeEmpty=%zu withCube=%zu\n",
        leafAmbientWithCubeEmpty_, leafAmbientWithCube_);
      leafAmbientIdentityOk_ = false;
      lastError_ = E_UNEXPECTED;
    }
  }
  entityModelInstanceSlotCount_ = packed.size();
  // The cube table last, from the same walk that handed the instances their
  // slots: uploading it anywhere else would let the table and the slot indices
  // come from different updates, which is a wrong-colour frame with no symptom
  // until somebody compares two leaves.
  uploadLeafAmbientCubes(leafCubes);
}

void Renderer::setEntityBoneMatrices(const std::vector<std::array<float, 16>>& boneMatrices) {
  entityBoneMatrixCount_ = 0;
  if (boneMatrices.empty()) return;
  if (!device_ || !context_) return;
  // Every matrix has to be finite before it reaches the GPU: one NaN in the
  // buffer turns a whole instance into a black smear and gives no clue which
  // entity it came from. Rejecting the upload is the loud failure.
  for (const auto& matrix : boneMatrices) {
    for (const float element : matrix) {
      if (!std::isfinite(element)) { lastError_ = E_INVALIDARG; return; }
    }
  }
  if (!entityBoneBuffer_ || entityBoneCapacity_ < boneMatrices.size()) {
    D3D11_BUFFER_DESC description{};
    const auto capacity = std::max(boneMatrices.size(), std::size_t{256});
    description.ByteWidth = static_cast<UINT>(capacity * sizeof(std::array<float, 16>));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    // One float4 per element, four elements per bone matrix -- strides the
    // `StructuredBuffer<float4>` declaration in the entity and overlay shaders.
    description.StructureByteStride = sizeof(float) * 4;
    Microsoft::WRL::ComPtr<ID3D11Buffer> replacementBuffer;
    const HRESULT boneBufferResult = resourceFaultHit("entity.bone.createbuffer")
      ? static_cast<HRESULT>(E_OUTOFMEMORY)
      : device_->CreateBuffer(&description, nullptr, replacementBuffer.GetAddressOf());
    if (FAILED(boneBufferResult)) {
      ++resourceRefusals_;
      std::fprintf(stderr,
        "[renderer-resource] refuse surface=entity.bone.createbuffer kind=create hresult=0x%08lX bones=%zu\n",
        static_cast<unsigned long>(boneBufferResult), boneMatrices.size());
      std::fflush(stderr);
      return;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view.Buffer.FirstElement = 0;
    // T62: the entity shader reads this table as `StructuredBuffer<float4>`,
    // four elements per bone, so the structure stride has to be one float4 and
    // the view has to span four elements per matrix. The buffer was created with
    // a 64-byte stride and a matrix-count view while the shader indexed it as
    // float4: every bone read 64 bytes away from the one it asked for, which
    // sampled other bones' matrices and turned every skinned player into a
    // spiked tent. The viewmodel upload (native_renderer_viewmodel.cpp, T18)
    // already had this right; the entity upload did not.
    view.Buffer.NumElements = static_cast<UINT>(capacity * 4);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> replacementView;
    const HRESULT boneViewResult = resourceFaultHit("entity.bone.createsrv")
      ? static_cast<HRESULT>(E_OUTOFMEMORY)
      : device_->CreateShaderResourceView(replacementBuffer.Get(), &view,
        replacementView.GetAddressOf());
    if (FAILED(boneViewResult)) {
      ++resourceRefusals_;
      std::fprintf(stderr,
        "[renderer-resource] refuse surface=entity.bone.createsrv kind=create hresult=0x%08lX bones=%zu\n",
        static_cast<unsigned long>(boneViewResult), boneMatrices.size());
      std::fflush(stderr);
      return;
    }
    entityBoneBuffer_ = std::move(replacementBuffer);
    entityBoneView_ = std::move(replacementView);
    entityBoneCapacity_ = capacity;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT boneMapResult = resourceFaultHit("entity.bone.map")
    ? static_cast<HRESULT>(E_OUTOFMEMORY)
    : context_->Map(entityBoneBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(boneMapResult)) {
    ++resourceMapRefusals_;
    std::fprintf(stderr,
      "[renderer-resource] refuse surface=entity.bone.map kind=map hresult=0x%08lX\n",
      static_cast<unsigned long>(boneMapResult));
    std::fflush(stderr);
    return;
  }
  std::memcpy(mapped.pData, boneMatrices.data(), boneMatrices.size() * sizeof(std::array<float, 16>));
  context_->Unmap(entityBoneBuffer_.Get(), 0);
  entityBoneMatrixCount_ = boneMatrices.size();
}

void Renderer::uploadLeafAmbientCubes(const std::vector<LeafAmbientCubeFace>& distinct) {
  leafAmbientSlotCount_ = 0;
  if (distinct.empty()) return;
  if (!device_ || !context_) return;
  // Same two guarantees the bone table above makes, for the same reasons: a NaN
  // face would turn a whole model black and name no culprit, and the buffer is
  // re-created only when the population outgrows it rather than every frame.
  for (const auto& cube : distinct) {
    for (const auto& face : cube.rgb) {
      for (const float channel : face) {
        if (!std::isfinite(channel)) { lastError_ = E_INVALIDARG; return; }
      }
    }
  }
  const std::size_t elements = distinct.size() * static_cast<std::size_t>(kLeafAmbientFaces);
  if (!leafAmbientCubeBuffer_ || leafAmbientCubeCapacity_ < distinct.size()) {
    D3D11_BUFFER_DESC description{};
    const auto capacity = std::max(distinct.size(), std::size_t{16});
    description.ByteWidth = static_cast<UINT>(capacity * kLeafAmbientFaces * sizeof(float) * 4);
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    // One float4 per element, six elements per cube -- the shape the world pixel
    // shader's `StructuredBuffer<float4> leafAmbientCubeFaces` indexes. The T62
    // bug was exactly this pair disagreeing (stride one float4, view sized per
    // matrix), so both numbers are written from the same expression here.
    description.StructureByteStride = sizeof(float) * 4;
    Microsoft::WRL::ComPtr<ID3D11Buffer> replacementBuffer;
    const HRESULT cubeBufferResult = resourceFaultHit("entity.leafcube.createbuffer")
      ? static_cast<HRESULT>(E_OUTOFMEMORY)
      : device_->CreateBuffer(&description, nullptr, replacementBuffer.GetAddressOf());
    if (FAILED(cubeBufferResult)) {
      ++resourceRefusals_;
      std::fprintf(stderr,
        "[renderer-resource] refuse surface=entity.leafcube.createbuffer kind=create hresult=0x%08lX slots=%zu\n",
        static_cast<unsigned long>(cubeBufferResult), distinct.size());
      std::fflush(stderr);
      return;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view.Buffer.FirstElement = 0;
    view.Buffer.NumElements = static_cast<UINT>(capacity * kLeafAmbientFaces);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> replacementView;
    const HRESULT cubeViewResult = resourceFaultHit("entity.leafcube.createsrv")
      ? static_cast<HRESULT>(E_OUTOFMEMORY)
      : device_->CreateShaderResourceView(replacementBuffer.Get(), &view,
        replacementView.GetAddressOf());
    if (FAILED(cubeViewResult)) {
      ++resourceRefusals_;
      std::fprintf(stderr,
        "[renderer-resource] refuse surface=entity.leafcube.createsrv kind=create hresult=0x%08lX slots=%zu\n",
        static_cast<unsigned long>(cubeViewResult), distinct.size());
      std::fflush(stderr);
      return;
    }
    leafAmbientCubeBuffer_ = std::move(replacementBuffer);
    leafAmbientCubeView_ = std::move(replacementView);
    leafAmbientCubeCapacity_ = capacity;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const HRESULT cubeMapResult = resourceFaultHit("entity.leafcube.map")
    ? static_cast<HRESULT>(E_OUTOFMEMORY)
    : context_->Map(leafAmbientCubeBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(cubeMapResult)) {
    ++resourceMapRefusals_;
    std::fprintf(stderr,
      "[renderer-resource] refuse surface=entity.leafcube.map kind=map hresult=0x%08lX\n",
      static_cast<unsigned long>(cubeMapResult));
    std::fflush(stderr);
    return;
  }
  std::memcpy(mapped.pData, distinct.data(), elements * sizeof(float) * 4);
  context_->Unmap(leafAmbientCubeBuffer_.Get(), 0);
  leafAmbientSlotCount_ = distinct.size();
}

} // namespace tf2::native
