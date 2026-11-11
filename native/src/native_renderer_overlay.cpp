// Overlay and projectile timelines: overlay draw lists, overlay buffers, projectile lines and tracks.
//
// One translation unit of the renderer split: the bodies were moved verbatim out
// of native_renderer.cpp, so pass order, thresholds and trace text are unchanged.

#include "native_renderer.h"
#include "renderer_internal.h"

#include "entity_model.h"
#include "hud_font.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tf2::native {


bool Renderer::uploadProjectileLines(const std::vector<ProjectileLine>& lines) {
  projectileVertexBuffer_.Reset();
  projectileVertexCapacity_ = 0;
  projectileVertexCount_ = 0;
  if (!device_ || !worldBoundsValid_ || lines.empty()) return true;
  const std::size_t limited = std::min<std::size_t>(lines.size(), 512u);
  std::vector<WorldVertex> vertices;
  vertices.reserve(limited * 2u);
  const auto convert = [&](const float point[3], const float color[4]) {
    return WorldVertex{(point[0] - worldCenterX_) * worldHorizontalScale_,
      (point[1] - worldCenterY_) * worldHorizontalScale_,
      (point[2] - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f,
      color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  };
  for (std::size_t i = 0; i < limited; ++i) {
    if (!std::isfinite(lines[i].start[0]) || !std::isfinite(lines[i].start[1]) || !std::isfinite(lines[i].start[2])
        || !std::isfinite(lines[i].end[0]) || !std::isfinite(lines[i].end[1]) || !std::isfinite(lines[i].end[2])) continue;
    vertices.push_back(convert(lines[i].start, lines[i].color));
    vertices.push_back(convert(lines[i].end, lines[i].color));
  }
  if (vertices.empty()) return true;
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = vertices.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) return false;
  projectileVertexBuffer_ = std::move(buffer);
  projectileVertexCapacity_ = vertices.size();
  projectileVertexCount_ = static_cast<UINT>(vertices.size());
  return true;
}

void Renderer::setProjectileTimeline(const std::vector<ProjectileTimelineEvent>& events, std::int32_t tick) {
  projectileVertexCount_ = 0;
  if (!device_ || !worldBoundsValid_) return;
  std::vector<WorldVertex> vertices;
  vertices.reserve(std::min<std::size_t>(events.size() * 2u, 32768u));
  const auto normalize = [this](float x, float y, float z) {
    return DirectX::XMFLOAT3(
      (x - worldCenterX_) * worldHorizontalScale_,
      (y - worldCenterY_) * worldHorizontalScale_,
      (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f);
  };
  for (const auto& event : events) {
    if (event.tick != tick || !event.hasOrigin) continue;
    const auto start = normalize(event.origin[0], event.origin[1], event.origin[2]);
    if (event.className == "CTETFExplosion" || event.className == "CTEExplosion") {
      const float radius = event.hasRadius
        ? std::clamp(static_cast<float>(event.radius), 8.0f, 512.0f) : 16.0f;
      const auto x = normalize(event.origin[0] + radius, event.origin[1], event.origin[2]);
      const auto y = normalize(event.origin[0], event.origin[1] + radius, event.origin[2]);
      const auto z = normalize(event.origin[0], event.origin[1], event.origin[2] + radius);
      constexpr float color[4] = {1.0f, 0.55f, 0.05f, 1.0f};
      vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({x.x, x.y, x.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({y.x, y.y, y.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      vertices.push_back({z.x, z.y, z.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
      if (vertices.size() >= 32768u) break;
      continue;
    }
    if (!event.hasDirection) continue;
    DirectX::XMFLOAT3 direction(event.direction[0], event.direction[1], event.direction[2]);
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!std::isfinite(length) || length < 1e-4f) continue;
    direction.x /= length; direction.y /= length; direction.z /= length;
    const auto end = normalize(event.origin[0] + direction.x * 96.0f,
                               event.origin[1] + direction.y * 96.0f,
                               event.origin[2] + direction.z * 96.0f);
    constexpr float color[4] = {1.0f, 0.25f, 0.05f, 1.0f};
    vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    vertices.push_back({end.x, end.y, end.z, color[0], color[1], color[2], color[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    if (vertices.size() >= 32768u) break;
  }
  if (vertices.empty()) return;
  if (!projectileVertexBuffer_ || projectileVertexCapacity_ < vertices.size()) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    // F0-c: one seam check per create site, so an armed run can fail this exact
    // allocation and the refusal is named instead of just dropping the lines.
    // F0-c: the replacement is built in a temporary and swapped in only once
    // the device accepted it. Releasing the old buffer as an argument to
    // CreateBuffer -- which is what this did -- destroys it before the call is
    // even attempted, so a refusal leaves no buffer at all while the capacity
    // still claims there is one.
    Microsoft::WRL::ComPtr<ID3D11Buffer> projectileBufferResultTemporary;
    const HRESULT projectileBufferResult = resourceFaultHit("overlay.projectile.createbuffer")
      ? static_cast<HRESULT>(E_OUTOFMEMORY)
      : device_->CreateBuffer(&description, nullptr, projectileBufferResultTemporary.GetAddressOf());
    if (FAILED(projectileBufferResult)) {
      ++resourceRefusals_;
      std::fprintf(stderr,
        "[renderer-resource] refuse surface=overlay.projectile.createbuffer kind=create hresult=0x%08lX vertices=%zu\n",
        static_cast<unsigned long>(projectileBufferResult), vertices.size());
      std::fflush(stderr);
      return;
    }
    projectileVertexBuffer_ = std::move(projectileBufferResultTemporary);
    projectileVertexCapacity_ = vertices.size();
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(projectileVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(WorldVertex));
  context_->Unmap(projectileVertexBuffer_.Get(), 0);
  projectileVertexCount_ = static_cast<UINT>(vertices.size());
}

// ===========================================================================
// T53: THE OBSERVED TRAJECTORY PASS
//
// The difference between this and setProjectileTimeline above is the whole point
// of the task. That pass is fed from TempEntity GENERATION events: one position
// and a velocity, so anything drawn from it has to be extrapolated, and
// extrapolation is the constant-velocity assumption this pass deliberately
// avoids.
//
// This pass is fed positions the demo actually recorded, one per tick. Nothing
// here interpolates, smooths, or extends a trail past the last observed point:
// a straight stretch of a trail is straight because the projectile flew straight,
// not because the renderer filled a gap.
//
// The points arrive already in map units and already carrying their AGE, so the
// fade toward the linger limit is a property of the data rather than of a
// clock inside the renderer. That keeps a captured frame reproducible: the same
// tick and the same retained rows produce the same vertices, with no dependence
// on how long the process has been running.
// ===========================================================================
void Renderer::setProjectileTrackPoints(const std::vector<ProjectileTrackPoint>& points, std::int32_t tick) {
  projectileTrackVertexCount_ = 0;
  projectileTrackPointCount_ = points.size();
  if (!device_ || !worldBoundsValid_ || points.size() < 2) return;

  std::vector<WorldVertex> vertices;
  vertices.reserve(std::min<std::size_t>(points.size() * 2u, 65536u));

  // The same map-to-box transform every other world vertex goes through. A trail
  // at a different scale from the world it sits in would still look plausible in
  // isolation, which is why this is the renderer's own normalisation and not a
  // second copy.
  const auto normalize = [this](float x, float y, float z) {
    return DirectX::XMFLOAT3(
      (x - worldCenterX_) * worldHorizontalScale_,
      (y - worldCenterY_) * worldHorizontalScale_,
      (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f);
  };

  // A LINE LIST, two vertices per segment, one segment per adjacent observed
  // pair. Adjacent means adjacent IN THE ARRAY, which the caller has already
  // ordered by tick within one lifetime; a jump between two projectile lifetimes
  // would otherwise be drawn as a segment across the map, which is the slot-reuse
  // defect showing up again as a line.
  const float kLingerTicks = 33.0f;
  for (std::size_t i = 1; i < points.size(); ++i) {
    const auto& previous = points[i - 1];
    const auto& current = points[i];
    // A lifetime change must never be joined.
    if (previous.entityIndex != current.entityIndex) continue;
    if (std::abs(current.ageTicks - previous.ageTicks) > 1.5f) continue;

    const auto start = normalize(previous.x, previous.y, previous.z);
    const auto end = normalize(current.x, current.y, current.z);

    // Colour fades with age: a fresh segment is bright, and a segment at the
    // linger limit is nearly gone. Alpha, not size, because the pass blends.
    const float age = current.ageTicks < 0.0f ? 0.0f : current.ageTicks;
    const float fade = age >= kLingerTicks ? 0.0f : (1.0f - age / kLingerTicks);
    const float alpha = 0.25f + 0.75f * fade;
    // Warm colour, deliberately not team-coloured: T52 owns team colour, and a
    // trail that changed hue with team would be claiming a mapping this pass does
    // not read.
    const float color[4] = {1.0f, 0.72f, 0.25f, alpha};

    vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3],
                        0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    vertices.push_back({end.x, end.y, end.z, color[0], color[1], color[2], color[3],
                        0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
    if (vertices.size() >= 65536u) break;
  }
  if (vertices.empty()) return;

  if (!projectileTrackVertexBuffer_ || projectileTrackVertexCapacity_ < vertices.size()) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    // F0-c: the replacement is built in a temporary and swapped in only once
    // the device accepted it. Releasing the old buffer as an argument to
    // CreateBuffer -- which is what this did -- destroys it before the call is
    // even attempted, so a refusal leaves no buffer at all while the capacity
    // still claims there is one.
    Microsoft::WRL::ComPtr<ID3D11Buffer> trackBufferResultTemporary;
    const HRESULT trackBufferResult = resourceFaultHit("overlay.track.createbuffer")
      ? static_cast<HRESULT>(E_OUTOFMEMORY)
      : device_->CreateBuffer(&description, nullptr, trackBufferResultTemporary.GetAddressOf());
    if (FAILED(trackBufferResult)) {
      ++resourceRefusals_;
      std::fprintf(stderr,
        "[renderer-resource] refuse surface=overlay.track.createbuffer kind=create hresult=0x%08lX vertices=%zu\n",
        static_cast<unsigned long>(trackBufferResult), vertices.size());
      std::fflush(stderr);
      return;
    }
    projectileTrackVertexBuffer_ = std::move(trackBufferResultTemporary);
    projectileTrackVertexCapacity_ = vertices.size();
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(projectileTrackVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(WorldVertex));
  context_->Unmap(projectileTrackVertexBuffer_.Get(), 0);
  projectileTrackVertexCount_ = static_cast<UINT>(vertices.size());
}

void Renderer::setCpuParticleTimeline(const std::vector<ProjectileTimelineEvent>& events, std::int32_t tick) {
  if (!device_ || !worldBoundsValid_) return;
  constexpr std::int32_t lifetimeTicks = 8;
  std::vector<WorldVertex> particles;
  particles.reserve(4096);
  const auto normalize = [this](float x, float y, float z) {
    return DirectX::XMFLOAT3((x - worldCenterX_) * worldHorizontalScale_,
      (y - worldCenterY_) * worldHorizontalScale_,
      (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f);
  };
  for (const auto& event : events) {
    if (event.tick > tick || event.tick < tick - lifetimeTicks || !event.hasOrigin) continue;
    const float age = static_cast<float>(tick - event.tick) / static_cast<float>(lifetimeTicks);
    if (event.className == "CTETFExplosion" || event.className == "CTEExplosion") {
      const float radius = event.hasRadius ? std::clamp(static_cast<float>(event.radius), 8.0f, 256.0f) : 16.0f;
      const auto a = normalize(event.origin[0], event.origin[1], event.origin[2]);
      const auto b = normalize(event.origin[0] + radius * (1.0f - age), event.origin[1], event.origin[2]);
      constexpr float c[4] = {1.0f, 0.75f, 0.1f, 1.0f};
      particles.push_back({a.x,a.y,a.z,c[0],c[1],c[2],c[3],0,0,0,1,0});
      particles.push_back({b.x,b.y,b.z,c[0],c[1],c[2],c[3],0,0,0,1,0});
    } else if (event.className == "CTEFireBullets" && event.hasDirection) {
      DirectX::XMFLOAT3 d(event.direction[0], event.direction[1], event.direction[2]);
      const float length = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
      if (!std::isfinite(length) || length < 1e-4f) continue;
      d.x /= length; d.y /= length; d.z /= length;
      const auto a = normalize(event.origin[0] + d.x * age * 24.0f, event.origin[1] + d.y * age * 24.0f, event.origin[2] + d.z * age * 24.0f);
      const auto b = normalize(event.origin[0] + d.x * (age * 24.0f + 10.0f), event.origin[1] + d.y * (age * 24.0f + 10.0f), event.origin[2] + d.z * (age * 24.0f + 10.0f));
      constexpr float c[4] = {1.0f, 0.3f, 0.05f, 1.0f};
      particles.push_back({a.x,a.y,a.z,c[0],c[1],c[2],c[3],0,0,0,1,0});
      particles.push_back({b.x,b.y,b.z,c[0],c[1],c[2],c[3],0,0,0,1,0});
    }
    if (particles.size() >= 4096) break;
  }
  if (particles.empty()) return;
  if (!projectileVertexBuffer_ || projectileVertexCapacity_ < particles.size()) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(particles.size() * sizeof(WorldVertex));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    // F0-c: the replacement is built in a temporary and swapped in only once
    // the device accepted it. Releasing the old buffer as an argument to
    // CreateBuffer -- which is what this did -- destroys it before the call is
    // even attempted, so a refusal leaves no buffer at all while the capacity
    // still claims there is one.
    Microsoft::WRL::ComPtr<ID3D11Buffer> particleBufferResultTemporary;
    const HRESULT particleBufferResult = resourceFaultHit("overlay.particle.createbuffer")
      ? static_cast<HRESULT>(E_OUTOFMEMORY)
      : device_->CreateBuffer(&description, nullptr, particleBufferResultTemporary.GetAddressOf());
    if (FAILED(particleBufferResult)) {
      ++resourceRefusals_;
      std::fprintf(stderr,
        "[renderer-resource] refuse surface=overlay.particle.createbuffer kind=create hresult=0x%08lX vertices=%zu\n",
        static_cast<unsigned long>(particleBufferResult), particles.size());
      std::fflush(stderr);
      return;
    }
    projectileVertexBuffer_ = std::move(particleBufferResultTemporary);
    projectileVertexCapacity_ = particles.size();
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(projectileVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
  std::memcpy(mapped.pData, particles.data(), particles.size() * sizeof(WorldVertex));
  context_->Unmap(projectileVertexBuffer_.Get(), 0);
  projectileVertexCount_ = static_cast<UINT>(particles.size());
}

void Renderer::setOverlayDrawList(const OverlayDrawList& list) {
  // Stored, not drawn: the pass runs inside draw(), in its own slot, and a
  // caller must be able to submit on a sampled tick and keep drawing between
  // samples. The index is the depth mode, so the two lists cannot overwrite
  // each other.
  const std::size_t slot = list.depth == OverlayDepthMode::DepthTested ? 1u : 0u;
  overlayLists_[slot] = list;
  // Rebuilt from BOTH stored lists. A rebuild that read only the list just
  // handed in would erase the other mode buffers, so the second submission of
  // one frame would silently delete the first -- the failure the contract rule
  // "one list per depth mode, two lists per frame" (v1, T53 requirement (c))
  // exists to make impossible.
  rebuildOverlayBuffers();
}

void Renderer::rebuildOverlayBuffers() {
  overlayInstanceCount_ = 0;
  overlayInstances_.clear();
  // D1: the identity rows describe the buffer built below, so they are cleared with
  // it. A stale row would attribute this frame's silhouettes to last frame's slots.
  overlayInstanceIdentity_.clear();
  overlayLineVertexCount_ = 0;
  overlayLineVertexCountByDepth_[0] = 0;
  overlayLineVertexCountByDepth_[1] = 0;
  overlayLineStartByDepth_[0] = 0;
  overlayLineStartByDepth_[1] = 0;
  overlayDrawRanges_.clear();
  if (!device_ || !context_ || !worldBoundsValid_) return;

  const auto normalize = [this](float x, float y, float z) {
    return DirectX::XMFLOAT3((x - worldCenterX_) * worldHorizontalScale_,
      (y - worldCenterY_) * worldHorizontalScale_,
      (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f);
  };
  EntityModelWorldMap world;
  world.centerX = worldCenterX_;
  world.centerY = worldCenterY_;
  world.minZ = worldMinZ_;
  world.horizontalScale = worldHorizontalScale_;
  world.zScale = 0.8f / worldSpanZ_;
  constexpr std::size_t maxInstances = 96;

  // ---- silhouettes, one bucket per (mode, mesh) --------------------------
  struct Bucket {
    ID3D11Buffer* vertexBuffer = nullptr;
    UINT vertexCount = 0;
    OverlayDepthMode depth = OverlayDepthMode::XRay;
    std::vector<EntityModelInstanceGpu> items;
  };

  std::vector<EntityModelInstanceGpu> packed;
  std::vector<Bucket> buckets;
  std::size_t accepted = 0;
  for (std::size_t slot = 0; slot < 2 && accepted < maxInstances; ++slot) {
    const OverlayDrawList& list = overlayLists_[slot];
    if (!list.enabled) continue;
    // Keyed per mode, so one mesh drawn in both modes stays two draws with two
    // depth modes instead of collapsing into whichever list came first.
    std::unordered_map<std::string, std::size_t> bucketOf;
    for (const auto& item : list.items) {
      if (item.kind != OverlayPrimitiveKind::Silhouette) continue;
      if (accepted >= maxInstances) break;
      if (item.cacheKey.empty()) continue;
      const auto mesh = entityModelMeshes_.find(item.cacheKey);
      if (mesh == entityModelMeshes_.end() || !mesh->second.vertexBuffer
          || mesh->second.vertexCount < 3 || (mesh->second.vertexCount % 3) != 0) continue;
      EntityModelInstanceRows rows;
      if (!buildEntityModelInstanceRows(item.origin, item.angles, item.hasAngles, world, rows)) continue;
      const auto inserted = bucketOf.emplace(item.cacheKey, buckets.size());
      if (inserted.second) {
        Bucket created;
        created.vertexBuffer = mesh->second.vertexBuffer.Get();
        created.vertexCount = mesh->second.vertexCount;
        created.depth = list.depth;
        buckets.push_back(created);
      }
      EntityModelInstanceGpu gpu{};
      for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) gpu.position[row][column] = rows.positionRows[row * 4 + column];
        for (int column = 0; column < 3; ++column) gpu.normal[row][column] = rows.normalRows[row * 3 + column];
      }
      gpu.color[0] = item.color.rgba[0];
      gpu.color[1] = item.color.rgba[1];
      gpu.color[2] = item.color.rgba[2];
      gpu.color[3] = item.color.rgba[3];
      // Skinning is advertised only when the matrices for it were uploaded, the
      // same rule the entity pass follows: an instance that indexes past the
      // buffer is drawn unskinned rather than reading another entity bones.
      const bool skinned = item.skinned && item.boneCount > 0
        && static_cast<std::size_t>(item.boneOffset) + item.boneCount <= entityBoneMatrixCount_;
      if (skinned) {
        gpu.boneOffset = item.boneOffset;
        gpu.skinEnabled = 1u;
        gpu.padding[0] = item.boneCount;
      }
      // T52 D1 attribution (2026-10-23): the entity index travels INTO the GPU
      // record so it can be read back out of the buffer the draw call consumes.
      // padding[1] is a slot the 128-byte layout already has, so the stride and
      // every static_assert above are untouched. The question this answers is the
      // one the pixel readings cannot: whether the instance the GPU draws is the
      // instance the CPU published a box for.
      gpu.padding[1] = item.entityIndex;
      // Keep the identity and the origin alongside the GPU record, in the same
      // order, so range.instanceStart indexes both tables.
      OverlayInstanceRecord cpu;
      cpu.entityIndex = item.entityIndex;
      cpu.team = item.team;
      cpu.origin[0] = item.origin[0];
      cpu.origin[1] = item.origin[1];
      cpu.origin[2] = item.origin[2];
      // What the box will be computed from, recorded next to it.
      {
        std::uint32_t hash = 2166136261u;
        for (const char character : item.cacheKey) {
          hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(character));
          hash *= 16777619u;
        }
        cpu.meshKeyHash = hash;
      }
      cpu.meshVertexCount = mesh->second.vertexCount;
      cpu.skinned = skinned ? 1u : 0u;
      for (int axis = 0; axis < 3; ++axis) {
        cpu.meshBoundsMin[axis] = mesh->second.boundsMin[axis];
        cpu.meshBoundsMax[axis] = mesh->second.boundsMax[axis];
      }
      // The mesh's own box, through the same rows the instance is drawn with.
      for (int axis = 0; axis < 3; ++axis) {
        cpu.worldMin[axis] = 1.0e30f;
        cpu.worldMax[axis] = -1.0e30f;
      }
      for (int corner = 0; corner < 8; ++corner) {
        const float modelPoint[3] = {
          (corner & 1) ? mesh->second.boundsMax[0] : mesh->second.boundsMin[0],
          (corner & 4) ? mesh->second.boundsMax[1] : mesh->second.boundsMin[1],
          (corner & 2) ? mesh->second.boundsMax[2] : mesh->second.boundsMin[2],
        };
        for (int axis = 0; axis < 3; ++axis) {
          const float* row = &rows.positionRows[axis * 4];
          const float world = row[0] * modelPoint[0] + row[1] * modelPoint[1]
            + row[2] * modelPoint[2] + row[3];
          cpu.worldMin[axis] = std::min(cpu.worldMin[axis], world);
          cpu.worldMax[axis] = std::max(cpu.worldMax[axis], world);
        }
      }
      for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
          cpu.rows[row * 4 + column] = rows.positionRows[row * 4 + column];
        }
      }
      cpu.vertexPositions = mesh->second.cpuPositions.data();
      cpu.vertexPositionCount = static_cast<std::uint32_t>(mesh->second.cpuPositions.size() / 3);
      overlayInstances_.push_back(cpu);
      buckets[inserted.first->second].items.push_back(gpu);
      ++accepted;
    }
  }
  if (accepted > 0) {
    packed.reserve(accepted);
    for (auto& bucket : buckets) {
      if (bucket.items.empty()) continue;
      OverlayDrawRange range;
      range.vertexBuffer = bucket.vertexBuffer;
      range.vertexCount = bucket.vertexCount;
      range.instanceStart = static_cast<UINT>(packed.size());
      range.instanceCount = static_cast<UINT>(bucket.items.size());
      range.depth = bucket.depth;
      packed.insert(packed.end(), bucket.items.begin(), bucket.items.end());
      overlayDrawRanges_.push_back(range);
    }
  }
  if (!packed.empty()) {
    if (!overlayInstanceBuffer_ || overlayInstanceCapacity_ < packed.size()) {
      D3D11_BUFFER_DESC description{};
      const auto capacity = std::max(packed.size(), std::size_t{32});
      description.ByteWidth = static_cast<UINT>(capacity * sizeof(EntityModelInstanceGpu));
      description.Usage = D3D11_USAGE_DYNAMIC;
      description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      // F0-c: the replacement is built in a temporary and swapped in only once
      // the device accepted it. Releasing the old buffer as an argument to
      // CreateBuffer -- which is what this did -- destroys it before the call is
      // even attempted, so a refusal leaves no buffer at all while the capacity
      // still claims there is one.
      Microsoft::WRL::ComPtr<ID3D11Buffer> instanceBufferResultTemporary;
      const HRESULT instanceBufferResult = resourceFaultHit("overlay.instance.createbuffer")
        ? static_cast<HRESULT>(E_OUTOFMEMORY)
        : device_->CreateBuffer(&description, nullptr, instanceBufferResultTemporary.GetAddressOf());
      if (FAILED(instanceBufferResult)) {
        ++resourceRefusals_;
        std::fprintf(stderr,
          "[renderer-resource] refuse surface=overlay.instance.createbuffer kind=create hresult=0x%08lX instances=%zu\n",
          static_cast<unsigned long>(instanceBufferResult), packed.size());
        std::fflush(stderr);
        overlayDrawRanges_.clear();
        return;
      }
      overlayInstanceBuffer_ = std::move(instanceBufferResultTemporary);
      overlayInstanceCapacity_ = capacity;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(overlayInstanceBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      overlayDrawRanges_.clear();
      return;
    }
    std::memcpy(mapped.pData, packed.data(), packed.size() * sizeof(EntityModelInstanceGpu));
    context_->Unmap(overlayInstanceBuffer_.Get(), 0);
    overlayInstanceCount_ = packed.size();
    // ---- T52 D1: read the identity back out of the buffer being drawn ------
    // The CPU records the gate publishes boxes from and this GPU buffer are filled
    // from one vector, in one order -- that is the ASSUMPTION D1 puts in doubt
    // (four blobs, fourteen published boxes, 60 of 484 pixels inside any of them).
    // Reading the entity index back out of the vertex buffer proves which entity
    // each drawn instance actually carries, or fails to.
    {
      D3D11_BUFFER_DESC readback{};
      // CopyResource needs the SAME size as the source buffer, which is allocated at
      // capacity (>= packed.size()), not at the packed size. With the packed size the
      // copy fails, the readback is empty, and the section that asserts on it reads
      // zero instances -- a measurement that did not happen.
      readback.ByteWidth = static_cast<UINT>(overlayInstanceCapacity_ * sizeof(EntityModelInstanceGpu));
      readback.Usage = D3D11_USAGE_STAGING;
      readback.BindFlags = 0;
      readback.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
      if (SUCCEEDED(device_->CreateBuffer(&readback, nullptr, staging.GetAddressOf()))) {
        context_->CopyResource(staging.Get(), overlayInstanceBuffer_.Get());
        D3D11_MAPPED_SUBRESOURCE back{};
        if (SUCCEEDED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &back))) {
          const auto* drawn = static_cast<const EntityModelInstanceGpu*>(back.pData);
          overlayInstanceIdentity_.reserve(packed.size());
          for (std::size_t index = 0; index < packed.size(); ++index) {
            overlayInstanceIdentity_.emplace_back(
              drawn[index].padding[1], packed[index].padding[1]);
          }
          // T52 (2026-10-23c): the same readback one level deeper. D1's residue is now
          // a question about the TRANSFORM, not about the identity: the box the pass
          // publishes is rows*vertex projected with the frame mvp, and if the rows in
          // the buffer the draw consumes differ from the rows the box used, the two
          // describe different objects. First instance only -- four floats are enough
          // to show agreement or its absence, and the trace stays readable.
          if (!packed.empty()) {
            for (int column = 0; column < 4; ++column) {
              overlayInstanceRow0Gpu_[column] = drawn[0].position[0][column];
              overlayInstanceRow0Cpu_[column] = packed[0].position[0][column];
            }
            overlayInstanceRow0Valid_ = true;
          }
          context_->Unmap(staging.Get(), 0);
        }
      }
    }
  }

  // ---- lines and markers, one contiguous range per depth mode -------------
  // Built into the SAME pass so they share its depth mode per list and its
  // restore discipline. The vertices are in world space already, converted with
  // the same map-to-box transform every other world vertex uses.
  std::vector<WorldVertex> lineVertices;
  for (std::size_t slot = 0; slot < 2; ++slot) {
    const OverlayDrawList& list = overlayLists_[slot];
    const UINT rangeStart = static_cast<UINT>(lineVertices.size());
    if (list.enabled) {
      for (const auto& item : list.items) {
        if (item.kind == OverlayPrimitiveKind::Silhouette) continue;
        const DirectX::XMFLOAT3 startPoint = normalize(item.start[0], item.start[1], item.start[2]);
        const DirectX::XMFLOAT3 endPoint = normalize(item.end[0], item.end[1], item.end[2]);
        const auto vertex = [&](const DirectX::XMFLOAT3& p) {
          return WorldVertex{p.x, p.y, p.z, item.color.rgba[0], item.color.rgba[1],
            item.color.rgba[2], item.color.rgba[3], 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        };
        lineVertices.push_back(vertex(startPoint));
        lineVertices.push_back(vertex(endPoint));
        if (item.kind == OverlayPrimitiveKind::Marker) {
          // A three-axis cross, the same shape the fallback markers use, so a
          // marker reads as a point in space rather than as a short segment.
          constexpr float extent = 8.0f;
          const DirectX::XMFLOAT3 axes[3] = {
            normalize(item.start[0] + extent, item.start[1], item.start[2]),
            normalize(item.start[0], item.start[1] + extent, item.start[2]),
            normalize(item.start[0], item.start[1], item.start[2] + extent),
          };
          for (const auto& axis : axes) { lineVertices.push_back(vertex(startPoint)); lineVertices.push_back(vertex(axis)); }
        }
        if (lineVertices.size() >= 32768u) break;
      }
    }
    overlayLineStartByDepth_[slot] = rangeStart;
    overlayLineVertexCountByDepth_[slot] = static_cast<UINT>(lineVertices.size()) - rangeStart;
  }
  if (!lineVertices.empty()) {
    if (!overlayLineBuffer_ || overlayLineCapacity_ < lineVertices.size()) {
      D3D11_BUFFER_DESC description{};
      description.ByteWidth = static_cast<UINT>(lineVertices.size() * sizeof(WorldVertex));
      description.Usage = D3D11_USAGE_DYNAMIC;
      description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      // F0-c: the replacement is built in a temporary and swapped in only once
      // the device accepted it. Releasing the old buffer as an argument to
      // CreateBuffer -- which is what this did -- destroys it before the call is
      // even attempted, so a refusal leaves no buffer at all while the capacity
      // still claims there is one.
      Microsoft::WRL::ComPtr<ID3D11Buffer> lineBufferResultTemporary;
      const HRESULT lineBufferResult = resourceFaultHit("overlay.line.createbuffer")
        ? static_cast<HRESULT>(E_OUTOFMEMORY)
        : device_->CreateBuffer(&description, nullptr, lineBufferResultTemporary.GetAddressOf());
      if (FAILED(lineBufferResult)) {
        ++resourceRefusals_;
        std::fprintf(stderr,
          "[renderer-resource] refuse surface=overlay.line.createbuffer kind=create hresult=0x%08lX vertices=%zu\n",
          static_cast<unsigned long>(lineBufferResult), lineVertices.size());
        std::fflush(stderr);
        overlayLineVertexCount_ = 0;
        overlayLineStartByDepth_[0] = 0;
        overlayLineStartByDepth_[1] = 0;
        return;
      }
      overlayLineBuffer_ = std::move(lineBufferResultTemporary);
      overlayLineCapacity_ = lineVertices.size();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(overlayLineBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      overlayLineVertexCount_ = 0;
      overlayLineStartByDepth_[0] = 0;
      overlayLineStartByDepth_[1] = 0;
      return;
    }
    std::memcpy(mapped.pData, lineVertices.data(), lineVertices.size() * sizeof(WorldVertex));
    context_->Unmap(overlayLineBuffer_.Get(), 0);
    overlayLineVertexCount_ = static_cast<UINT>(lineVertices.size());
  }
}

} // namespace tf2::native
