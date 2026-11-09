// World geometry: unit-box normalisation, sky/water geometry and visibility draw lists.
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


// T26: the face-id frame's encoding, in one place. $colour is unused by the id
// mode, so it carries the BSP face index: 8 bits per channel, and every vertex
// of a triangle carries the same value, so the interpolated result is exact and
// a decode can never be a blend of two faces.
static void writeWorldFaceId(float& r, float& g, float& b, float& a, std::int32_t faceIndex) {
  const std::uint32_t id = static_cast<std::uint32_t>(faceIndex);
  r = static_cast<float>(id & 0xFFu) / 255.0f;
  g = static_cast<float>((id >> 8) & 0xFFu) / 255.0f;
  b = static_cast<float>((id >> 16) & 0xFFu) / 255.0f;
  a = 1.0f;
}

bool Renderer::uploadWorldGeometry(const BspMap& map, const std::string& texturedMaterial, UINT textureWidth, UINT textureHeight) {
  if (!device_ || !map.valid || map.triangles.empty()) {
    lastError_ = E_INVALIDARG;
    return false;
  }
  // T26: `--world-geometry sky` reuses this overload -- one flat colour, no
  // atlas -- over `map.skyTriangles` instead of `map.triangles`. The bounds and
  // the transform still come from the world triangles, so the mask is projected
  // exactly where the world pass would have drawn the same map coordinates; a
  // mask built on its own bounds would be a picture of a different map.
  const std::vector<BspTriangle>& source = worldSkyFaceOnly_ ? map.skyTriangles : map.triangles;
  if (source.empty()) {
    lastError_ = E_INVALIDARG;
    return false;
  }
  // This overload draws the world with ONE material's texture. It is the
  // fallback, not the product path, so it says so where a reader will see it.
  worldAtlasReason_ = "the atlas overload was not used; the world was drawn with one material texture";
  worldAtlasSize_ = 0;
  worldAtlasBytes_ = 0;
  worldAtlasCount_ = 0;
  worldAtlasTotalBytes_ = 0;
  // T19: this path has no batches and no index buffer. Leaving the previous
  // upload's in place would make the draw take the batched branch against a
  // vertex buffer that was never laid out for it.
  worldAtlasTextures_.clear();
  worldBlendAtlasTextures_.clear();
  worldModulateAtlasTextures_.clear();
  worldDrawBatches_.clear();
  worldTriangleBatch_.clear();
  worldIndexBuffer_.Reset();
  worldIndexCount_ = 0;
  worldIndexCapacity_ = 0;
  worldTileTable_.clear();
  worldTilesTiled_ = 0;
  worldTilesRefused_ = 0;
  worldTilesRefusedMalformed_ = 0;
  worldTilesRefusedDuplicate_ = 0;
  worldTilesRefusedBudget_ = 0;
  worldAtlasBatches_ = 0;
  worldAtlasBlendBatches_ = 0;
  worldAtlasLayerBytes_ = 0;
  worldAtlasPlanReason_ = "one material texture, so no atlas plan was made";
  float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
  float minY = std::numeric_limits<float>::max(), maxY = std::numeric_limits<float>::lowest();
  float minZ = std::numeric_limits<float>::max(), maxZ = std::numeric_limits<float>::lowest();
  for (const auto& triangle : map.triangles) {
    for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
      minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
      minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
      minZ = std::min(minZ, point.z); maxZ = std::max(maxZ, point.z);
    }
  }
  const float spanX = std::max(maxX - minX, 1.0f), spanY = std::max(maxY - minY, 1.0f), spanZ = std::max(maxZ - minZ, 1.0f);
  const float horizontalSpan = std::max(spanX, spanY), horizontalScale = 1.8f / horizontalSpan;
  const float centerX = (minX + maxX) * 0.5f, centerY = (minY + maxY) * 0.5f;
  std::vector<WorldVertex> vertices;
  vertices.reserve(source.size() * 3);
  std::size_t lightmappedVertices = 0;
  std::size_t displacedTriangles = 0;
  std::size_t waterTriangles = 0;
  // U-A1 (2026-11-09): these vertices are NOT map units. X/Y are multiplied by
  // horizontalScale (1 box unit = 1/horizontalScale map units) and Z by 0.8/spanZ
  // (1 box unit = spanZ/0.8 map units), so the box is anisotropic: only where
  // horizontalScale == 0.8/spanZ is a box-space distance a map-space distance.
  // The shaders inherit this -- native_renderer_shaders.cpp:36 sets
  // worldPosition = input.position and the camera is written in the same box
  // (native_renderer_frame.cpp:98-105) -- so a fog range quoted in map units has
  // to be converted per axis, never by a single scalar. The atlas overload below
  // (~:590) normalizes identically.
  const auto convert = [&](const BspVertex& point, const BspVertex& normal) {
    return WorldVertex{(point.x - centerX) * horizontalScale, (point.y - centerY) * horizontalScale, (point.z - minZ) / spanZ * 0.8f + 0.1f, 0.62f, 0.65f, 0.68f, 1.0f, 0.0f, 0.0f, normal.x, normal.y, normal.z};
  };
  for (const auto& triangle : source) {
    const std::uint32_t hash = static_cast<std::uint32_t>(std::hash<std::string>{}(triangle.material));
    const float shade = 0.78f + static_cast<float>(hash % 23) / 100.0f;
    const bool textured = !texturedMaterial.empty() && triangle.material == texturedMaterial && textureWidth > 0 && textureHeight > 0;
    const auto ab = BspVertex{triangle.b.x - triangle.a.x, triangle.b.y - triangle.a.y, triangle.b.z - triangle.a.z};
    const auto ac = BspVertex{triangle.c.x - triangle.a.x, triangle.c.y - triangle.a.y, triangle.c.z - triangle.a.z};
    const auto nx = ab.y * ac.z - ab.z * ac.y;
    const auto ny = ab.z * ac.x - ab.x * ac.z;
    const auto nz = ab.x * ac.y - ab.y * ac.x;
    const auto normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    const auto normal = normalLength > 1e-6f ? BspVertex{nx / normalLength, ny / normalLength, nz / normalLength} : BspVertex{0.0f, 0.0f, 1.0f};
    auto a = convert(triangle.a, normal), b = convert(triangle.b, normal), c = convert(triangle.c, normal);
    a.r = shade; a.g = shade; a.b = shade; a.a = textured ? 1.0f : 0.0f;
    b.r = shade; b.g = shade; b.b = shade; b.a = textured ? 1.0f : 0.0f;
    c.r = shade; c.g = shade; c.b = shade; c.a = textured ? 1.0f : 0.0f;
    // T59: the same mask the atlas overload applies, so --world-refused-mask means
    // one thing on both paths. Without this the flag painted 0 px whenever the
    // single-material overload was the one that uploaded (the sky pass and the
    // atlas-failed fallback), and a reading of 0 could not be told apart from
    // "nothing refused is on screen".
    if (worldRefusedMask_ && !textured) {
      a.r = b.r = c.r = 1.0f; a.g = b.g = c.g = 0.0f; a.b = b.b = c.b = 1.0f;
    }
    // T26: the face-id frame. `$colour` is unused by the id mode, so it carries
    // the face index instead. A helper because both upload overloads must agree:
    // the first version of this wrote the id only on the single-material path,
    // and the world capture -- which goes through the atlas overload -- came
    // back pure white, which decodes to face 16,777,215 and does not exist.
    if (worldFaceIdUpload_ && triangle.faceIndex >= 0) {
      writeWorldFaceId(a.r, a.g, a.b, a.a, triangle.faceIndex);
      writeWorldFaceId(b.r, b.g, b.b, b.a, triangle.faceIndex);
      writeWorldFaceId(c.r, c.g, c.b, c.a, triangle.faceIndex);
    }
    const float invWidth = textured ? 1.0f / static_cast<float>(textureWidth) : 0.0f;
    const float invHeight = textured ? 1.0f / static_cast<float>(textureHeight) : 0.0f;
    a.u = triangle.au * invWidth; a.v = triangle.av * invHeight; b.u = triangle.bu * invWidth; b.v = triangle.bv * invHeight; c.u = triangle.cu * invWidth; c.v = triangle.cv * invHeight;
    if (triangle.lightmapped) { a.lu = triangle.alu; a.lv = triangle.alv; b.lu = triangle.blu; b.lv = triangle.blv; c.lu = triangle.clu; c.lv = triangle.clv; }
    // Same outcome-based count as the atlas overload below: keyed on the vertex
    // the pixel shader will actually see, not on the triangle's claim.
    if (a.lu >= 0.0f) lightmappedVertices += 3;
    // Counted here rather than trusted from the parse: this loop is what decides
    // how many triangles exist, and both tags are the parser's own on the
    // triangle it emitted -- a displacement face arrives already subdivided into
    // its grid, so a displaced triangle here is a tessellated one and the count
    // is the geometry the renderer will draw.
    if (triangle.displacement) ++displacedTriangles;
    if (triangle.water) ++waterTriangles;
    vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = vertices.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  worldVertexBuffer_ = std::move(buffer);
  worldVertexCount_ = static_cast<UINT>(vertices.size());
  worldLightmappedVertexCount_ = static_cast<UINT>(lightmappedVertices);
  {
    float cx0 = 1e30f, cy0 = 1e30f, cz0 = 1e30f, cx1 = -1e30f, cy1 = -1e30f, cz1 = -1e30f;
    for (const auto& v : vertices) {
      cx0 = std::min(cx0, v.x); cy0 = std::min(cy0, v.y); cz0 = std::min(cz0, v.z);
      cx1 = std::max(cx1, v.x); cy1 = std::max(cy1, v.y); cz1 = std::max(cz1, v.z);
    }
    worldConvertedMin_[0] = cx0; worldConvertedMin_[1] = cy0; worldConvertedMin_[2] = cz0;
    worldConvertedMax_[0] = cx1; worldConvertedMax_[1] = cy1; worldConvertedMax_[2] = cz1;
  }
  worldDisplacedTriangleCount_ = static_cast<UINT>(displacedTriangles);
  worldWaterTriangleCount_ = static_cast<UINT>(waterTriangles);
  worldMinX_ = minX; worldMinY_ = minY; worldMinZ_ = minZ;
  worldMaxX_ = maxX; worldMaxY_ = maxY; worldMaxZ_ = maxZ;
  worldCenterX_ = centerX; worldCenterY_ = centerY;
  worldSpanZ_ = spanZ; worldHorizontalScale_ = horizontalScale;
  worldBoundsValid_ = true;
  worldLightmapStatus_ = WorldLightmapStatus::FallbackUnlit;
  worldLightmapIntensity_ = std::clamp(map.lightmapIntensity, 0.15f, 2.0f);
  if (map.lightmapTriangleCount > 0) worldLightmapStatus_ = WorldLightmapStatus::Active;
  return true;
}

bool Renderer::uploadWorldGeometry(const BspMap& map, const std::vector<WorldTexture>& textures) {
  if (!device_ || !map.valid || map.triangles.empty()) {
    lastError_ = E_INVALIDARG;
    return false;
  }
  // The tile size is the plan's (`WorldAtlasPlanOptions::tileSize`), not a
  // second constant kept here: two copies of 514 is one copy that can drift.
  worldBlendTexturesSeen_ = textures.size();
  worldBlendLayersSeen_ = 0;
  for (const auto& source : textures) if (!source.blendRgba.empty()) ++worldBlendLayersSeen_;
  // T27: every feature reading is written by this upload, never inherited: a
  // frame that did not build the atlas must not read the previous frame's
  // tile count as its own.
  worldBumpFeature_ = WorldFeatureState{};
  worldBump2Feature_ = WorldFeatureState{};
  worldDetailFeature_ = WorldFeatureState{};
  // T96: the alpha readings are per upload too, for the same reason.
  worldBlendedTriangleCount_ = 0;
  worldCutoutTriangleCount_ = 0;
  worldBlendedBatchCount_ = 0;
  worldBumpFeature_.enabled = worldMaterialBumpEnabled_;
  worldBump2Feature_.enabled = worldMaterialBumpEnabled_ && worldBump2Enabled_;
  worldDetailFeature_.enabled = worldDetailEnabled_;
  // ---- T19: the plan, not a capped walk -----------------------------------
  // Before this, `accepted.size() >= kMaxTiles` skipped the material with a
  // `continue` and no counter: measured on cp_snakewater_final1 from the
  // reference frame, 51 of 148 materials were skipped that way while the trace
  // read `capped=51` and named none of them. planWorldTiles gives every source
  // an outcome -- a batch and a tile, or a refusal with a name -- and sizes
  // each atlas to the tiles it actually holds.
  std::vector<WorldTileSource> requests;
  requests.reserve(textures.size());
  for (const auto& source : textures) {
    WorldTileSource request;
    request.material = source.material;
    request.width = source.width;
    request.height = source.height;
    request.sourceWidth = source.sourceWidth > 0 ? source.sourceWidth : source.width;
    request.sourceHeight = source.sourceHeight > 0 ? source.sourceHeight : source.height;
    request.rgbaBytes = source.rgba.size();
    request.blendDeclared = source.blendDeclared;
    // Only a layer that was fetched enters the plan. `blendRgba` is empty both
    // when the switch withheld it and when it did not resolve, so the atlas a
    // `--world-blend off` run builds has the same rectangles as the on run --
    // which is what keeps the two frames differing by the shader branch alone.
    request.blendBytes = source.blendRgba.size();
    request.modulateBytes = source.blendModulateRgba.size();
    // T27: the two normal layers enter the plan only when their switch is on
    // and main.cpp actually fetched pixels. Both halves matter: with the switch
    // off the atlas must not exist (the default frame is byte-identical), and a
    // declared-but-unresolved `$bumpmap` must not price a texture it cannot
    // fill.
    request.bumpBytes = worldMaterialBumpEnabled_ ? source.bumpRgba.size() : 0;
    request.bump2Bytes = (worldMaterialBumpEnabled_ && worldBump2Enabled_)
      ? source.bump2Rgba.size() : 0;
    requests.push_back(std::move(request));
  }
  const WorldAtlasPlan plan = planWorldTiles(requests);
  worldAtlasPlanReason_ = plan.reason;
  worldTilesTiled_ = plan.accepted;
  worldTilesRefused_ = plan.refused;
  worldTilesRefusedMalformed_ = plan.refusedMalformed;
  worldTilesRefusedDuplicate_ = plan.refusedDuplicate;
  worldTilesRefusedBudget_ = plan.refusedBudget;
  worldAtlasBatches_ = plan.batches.size();
  worldAtlasLayerBytes_ = plan.albedoBytes + plan.blendBytes + plan.modulateBytes
    + plan.bumpBytes + plan.bump2Bytes;
  worldTileTable_.clear();
  worldTileTable_.reserve(textures.size());
  for (std::size_t index = 0; index < textures.size(); ++index) {
    WorldTileReading reading;
    reading.material = textures[index].material;
    reading.batch = plan.slots[index].batch;
    reading.tile = plan.slots[index].tile;
    reading.width = static_cast<int>(plan.slots[index].width);
    reading.height = static_cast<int>(plan.slots[index].height);
    reading.x = static_cast<int>(plan.slots[index].x);
    reading.y = static_cast<int>(plan.slots[index].y);
    reading.sourceWidth = static_cast<int>(plan.slots[index].sourceWidth);
    reading.sourceHeight = static_cast<int>(plan.slots[index].sourceHeight);
    if (plan.slots[index].batch >= 0) {
      const auto& placed = plan.batches[static_cast<std::size_t>(plan.slots[index].batch)];
      reading.atlasWidth = static_cast<int>(placed.width);
      reading.atlasHeight = static_cast<int>(placed.height);
    }
    reading.blend = plan.slots[index].blend;
    reading.modulate = plan.slots[index].modulate;
    // T96: the consumption side, decided here because this is the only place
    // that knows both the material's keys and the plan's verdict.
    reading.translucent = textures[index].translucent;
    reading.alphaTest = textures[index].alphaTest;
    if (plan.slots[index].batch < 0) {
      reading.alphaMode = (reading.translucent || reading.alphaTest) ? "refused" : "opaque";
      reading.alphaReason = reading.translucent || reading.alphaTest
        ? "the material declares an alpha key but got no atlas tile" : "no alpha key";
    } else if (reading.translucent && !worldAlphaBlendState_) {
      reading.alphaMode = "refused";
      reading.alphaReason = "the world pass has no alpha blend state";
    } else if (reading.translucent) {
      reading.alphaMode = "blended";
      reading.alphaReason = "$translucent";
    } else if (reading.alphaTest) {
      reading.alphaMode = "cutout";
      reading.alphaReason = "$alphatest";
    } else {
      reading.alphaMode = "opaque";
      reading.alphaReason = "no alpha key";
    }
    reading.reason = plan.slots[index].batch >= 0
      ? "tiled" : worldTileRefusalName(plan.refusals[index]);
    worldTileTable_.push_back(std::move(reading));
  }
  worldBlendAccepted_ = plan.accepted;
  worldBlendAcceptedLayers_ = 0;
  for (const auto& slot : plan.slots) if (slot.batch >= 0 && slot.blend) ++worldBlendAcceptedLayers_;
  if (plan.accepted == 0) {
    // Nothing to bind. The caller falls back to the single material texture,
    // and the reason says which refusal emptied the plan rather than "upload
    // failed".
    worldAtlasReason_ = plan.reason;
    worldAtlasSize_ = 0;
    worldAtlasBytes_ = 0;
    worldAtlasCount_ = 0;
    worldAtlasTotalBytes_ = 0;
    lastError_ = HRESULT_FROM_WIN32(ERROR_NO_DATA);
    return false;
  }

  // One CPU-side buffer per batch and layer, painted tile by tile. The batch's
  // own width is the row stride: a batch of 20 tiles is 5x4, and using batch
  // 0's 8x8 stride for it would write past the end.
  std::vector<std::vector<std::uint8_t>> atlasPixels(plan.batches.size());
  std::vector<std::vector<std::uint8_t>> blendPixels(plan.batches.size());
  std::vector<std::vector<std::uint8_t>> modulatePixels(plan.batches.size());
  // T27: the two normal-map layers. Their fill is the neutral normal
  // (128,128,255) rather than the albedo fill (88): a texel a material did not
  // write must decode to "straight up", so a stray sample is a no-op instead of
  // a plausible-looking tilt that a frame would show as shading.
  std::vector<std::vector<std::uint8_t>> bumpPixels(plan.batches.size());
  std::vector<std::vector<std::uint8_t>> bump2Pixels(plan.batches.size());
  // The detail layer's fill is opaque black. No vertex samples it -- bit 2 of
  // the mask is set from the same set of materials that was blitted here -- and
  // black is chosen so that a regression which sampled the fill anyway would
  // show as a crushed surface rather than as a plausible one: the modulate,
  // additive and multiply branches all collapse toward 0 on it.
  std::vector<std::vector<std::uint8_t>> detailPixels(plan.batches.size());
  const auto makeNeutral = [](std::size_t bytes, std::uint8_t r, std::uint8_t g,
                              std::uint8_t b) {
    std::vector<std::uint8_t> pixels(bytes, 0);
    for (std::size_t at = 0; at + 3 < pixels.size(); at += 4u) {
      pixels[at + 0] = r; pixels[at + 1] = g; pixels[at + 2] = b; pixels[at + 3] = 255u;
    }
    return pixels;
  };
  for (std::size_t batch = 0; batch < plan.batches.size(); ++batch) {
    atlasPixels[batch].assign(static_cast<std::size_t>(plan.batches[batch].albedoBytes), worldAtlasFill);
  }
  std::unordered_set<std::string> blendedMaterials;
  std::unordered_set<std::string> modulatedMaterials;
  std::unordered_set<std::string> terrainMaterials;
  std::unordered_set<std::string> bumpMaterials;
  std::unordered_set<std::string> bump2Materials;
  std::unordered_set<std::string> detailBlitMaterials;
  std::size_t blendTiles = 0, blendModulateTiles = 0, malformedLayers = 0;
  for (std::size_t index = 0; index < textures.size(); ++index) {
    const WorldTileSlot& slot = plan.slots[index];
    if (slot.batch < 0) continue;
    const auto& source = textures[index];
    const auto& batch = plan.batches[static_cast<std::size_t>(slot.batch)];
    const std::size_t expected = static_cast<std::size_t>(slot.width) * slot.height * 4u;
    worldTileBlit(atlasPixels[slot.batch], batch.width, slot.x, slot.y, slot.width, slot.height,
      source.rgba);
    // A layer whose byte count does not match its tile is refused rather than
    // sampled: reading past the end of it would paint whatever the allocator
    // left there, and that picture would look like a finding.
    if (source.blendDeclared) terrainMaterials.insert(source.material);
    const bool hasBlend = worldBlendEnabled_ && !source.blendRgba.empty();
    if (hasBlend && source.blendRgba.size() != expected) {
      ++malformedLayers;
    } else if (hasBlend) {
      if (blendPixels[slot.batch].empty()) {
        blendPixels[slot.batch].assign(atlasPixels[slot.batch].size(), worldAtlasFill);
      }
      worldTileBlit(blendPixels[slot.batch], batch.width, slot.x, slot.y, slot.width, slot.height,
        source.blendRgba);
      blendedMaterials.insert(source.material);
      ++blendTiles;
    } else {
      worldTileTable_[index].blend = false;
    }
    const bool hasModulate = !source.blendModulateRgba.empty();
    if (hasModulate && source.blendModulateRgba.size() != expected) {
      ++malformedLayers;
    } else if (hasModulate && blendedMaterials.count(source.material)) {
      if (modulatePixels[slot.batch].empty()) {
        modulatePixels[slot.batch].assign(atlasPixels[slot.batch].size(), worldAtlasFill);
      }
      worldTileBlit(modulatePixels[slot.batch], batch.width, slot.x, slot.y, slot.width, slot.height,
        source.blendModulateRgba);
      modulatedMaterials.insert(source.material);
      ++blendModulateTiles;
    } else {
      worldTileTable_[index].modulate = false;
    }
    if (!blendedMaterials.count(source.material)) worldTileTable_[index].blend = false;
    // ---- T27: the two normal layers ---------------------------------------
    // Each is resampled from the material's tile grid (where main.cpp put it)
    // into the feature atlas' own rectangle. The feature atlas is the same
    // rectangles in normalised space at 1/featureScale the resolution, so the
    // albedo uv addresses it without a second texcoord. A rectangle that cannot
    // be placed leaves the material's bit clear (the atlas has no texel under
    // its uv, and "sampled the filler" would be reported as applied).
    const auto blitFeature = [&](const std::vector<std::uint8_t>& rgba,
                                 std::vector<std::uint8_t>& target, WorldFeatureState& state,
                                 std::uint8_t fill) {
      if (rgba.empty()) return false;
      if (rgba.size() != expected) return false;
      WorldFeatureTileRect rect;
      if (!worldFeatureTileRect(slot, batch.width, batch.height, batch.featureWidth,
                                batch.featureHeight, rect)) {
        return false;
      }
      std::vector<std::uint8_t> resampled;
      if (!resampleRgba(rgba, slot.width, slot.height, rect.width, rect.height, resampled)) {
        return false;
      }
      if (target.empty()) target = makeNeutral(static_cast<std::size_t>(batch.featureBytes),
                                               fill, fill, fill);
      worldTileBlit(target, batch.featureWidth, rect.x, rect.y, rect.width, rect.height, resampled);
      ++state.tiles;
      return true;
    };
    if (worldMaterialBumpEnabled_ && blitFeature(source.bumpRgba, bumpPixels[slot.batch],
                                                 worldBumpFeature_, 128u)) {
      worldTileTable_[index].bump = true;
      bumpMaterials.insert(source.material);
    }
    if (worldMaterialBumpEnabled_ && worldBump2Enabled_
        && blitFeature(source.bump2Rgba, bump2Pixels[slot.batch], worldBump2Feature_, 128u)) {
      worldTileTable_[index].bump2 = true;
      bump2Materials.insert(source.material);
    }
    if (worldDetailEnabled_ && blitFeature(source.detailRgba, detailPixels[slot.batch],
                                           worldDetailFeature_, 0u)) {
      worldTileTable_[index].detail = true;
      detailBlitMaterials.insert(source.material);
    }
  }
  worldBlendTiles_ = blendTiles;
  worldBlendModulateTiles_ = blendModulateTiles;
  worldBlendMalformedLayers_ = malformedLayers;
  worldBlendVertices_ = 0;
  worldBlendModulateVertices_ = 0;
  worldBumpFeature_.materials = bumpMaterials.size();
  worldBump2Feature_.materials = bump2Materials.size();
  worldDetailFeature_.materials = detailBlitMaterials.size();
  // T12's 67.6 MiB note still applies per batch: a batch of 64 tiles is over the
  // 64 MiB guard the ordinary textures use, and that guard exists to catch a
  // runaway allocation rather than to refuse the size this pass is built around.
  // The whole plan's cost is bounded by the planner's budget and reported.
  worldAtlasTextures_.clear();
  worldBlendAtlasTextures_.clear();
  worldModulateAtlasTextures_.clear();
  worldBumpAtlasTextures_.clear();
  worldBump2AtlasTextures_.clear();
  // The single-atlas readings are the largest atlas, not batch 0's: a gate that
  // asked "is there a 4112-wide, 64-MiB atlas" still gets the atlas it meant,
  // and the total is reported separately.
  worldAtlasSize_ = 0;
  worldAtlasBytes_ = 0;
  worldAtlasCount_ = plan.batches.size();
  worldAtlasTotalBytes_ = plan.albedoBytes;
  for (const auto& layout : plan.batches) {
    worldAtlasSize_ = std::max(worldAtlasSize_, static_cast<std::size_t>(layout.width));
    worldAtlasBytes_ = std::max(worldAtlasBytes_, layout.albedoBytes);
  }
  worldAtlasBlendBatches_ = 0;
  std::string atlasError;
  for (std::size_t batch = 0; batch < plan.batches.size(); ++batch) {
    const auto& layout = plan.batches[batch];
    const std::uint64_t atlasBudget = std::max<std::uint64_t>(64ull * 1024ull * 1024ull,
      layout.albedoBytes);
    Texture2D albedo;
    if (!albedo.create(device_.Get(), atlasPixels[batch], layout.width, layout.height,
                       &atlasError, atlasBudget)) {
      worldAtlasReason_ = atlasError.empty() ? "the world atlas texture was refused" : atlasError;
      lastError_ = E_FAIL;
      return false;
    }
    worldAtlasTextures_.push_back(std::move(albedo));
    if (!blendPixels[batch].empty()) {
      Texture2D blend;
      if (!blend.create(device_.Get(), blendPixels[batch], layout.width, layout.height,
                        &atlasError, atlasBudget)) {
        worldAtlasReason_ = atlasError.empty() ? "the blend atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldBlendAtlasTextures_.push_back(std::move(blend));
      ++worldAtlasBlendBatches_;
    } else {
      worldBlendAtlasTextures_.emplace_back();
    }
    if (!modulatePixels[batch].empty()) {
      Texture2D modulate;
      if (!modulate.create(device_.Get(), modulatePixels[batch], layout.width, layout.height,
                           &atlasError, atlasBudget)) {
        worldAtlasReason_ = atlasError.empty()
          ? "the blend modulate atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldModulateAtlasTextures_.push_back(std::move(modulate));
    } else {
      worldModulateAtlasTextures_.emplace_back();
    }
    // T27: the normal-map layers, one texture per batch at the feature
    // resolution. Empty when the switch was off or no tile in this batch asked
    // for the layer, which is what keeps an off run's allocation identical to
    // the pre-T27 build.
    const std::uint64_t featureBudget = std::max<std::uint64_t>(64ull * 1024ull * 1024ull,
      layout.featureBytes);
    if (!bumpPixels[batch].empty()) {
      Texture2D bump;
      if (!bump.create(device_.Get(), bumpPixels[batch], layout.featureWidth,
                       layout.featureHeight, &atlasError, featureBudget)) {
        worldAtlasReason_ = atlasError.empty()
          ? "the bump atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldBumpAtlasTextures_.push_back(std::move(bump));
    } else {
      worldBumpAtlasTextures_.emplace_back();
    }
    if (!bump2Pixels[batch].empty()) {
      Texture2D bump2;
      if (!bump2.create(device_.Get(), bump2Pixels[batch], layout.featureWidth,
                        layout.featureHeight, &atlasError, featureBudget)) {
        worldAtlasReason_ = atlasError.empty()
          ? "the bump2 atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldBump2AtlasTextures_.push_back(std::move(bump2));
    } else {
      worldBump2AtlasTextures_.emplace_back();
    }
    // The detail layer, same rectangle rule and same budget as the normals.
    if (!detailPixels[batch].empty()) {
      Texture2D detail;
      if (!detail.create(device_.Get(), detailPixels[batch], layout.featureWidth,
                         layout.featureHeight, &atlasError, featureBudget)) {
        worldAtlasReason_ = atlasError.empty()
          ? "the detail atlas texture was refused" : atlasError;
        lastError_ = E_FAIL;
        return false;
      }
      worldDetailAtlasTextures_.push_back(std::move(detail));
    } else {
      worldDetailAtlasTextures_.emplace_back();
    }
  }
  worldAtlasReason_ = "bound";
  // The old single-atlas members keep their meaning for the two other passes
  // that still sample one texture (the entity and projectile draws use
  // `texture_`, not these). Batch 0 is what they would need if they ever did.
  worldBlendTexture_.reset();
  worldBlendModulateTexture_.reset();

  float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
  float minY = std::numeric_limits<float>::max(), maxY = std::numeric_limits<float>::lowest();
  float minZ = std::numeric_limits<float>::max(), maxZ = std::numeric_limits<float>::lowest();
  for (const auto& triangle : map.triangles) for (const auto& point : {triangle.a, triangle.b, triangle.c}) {
    minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
    minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
    minZ = std::min(minZ, point.z); maxZ = std::max(maxZ, point.z);
  }
  const float spanX = std::max(maxX - minX, 1.0f), spanY = std::max(maxY - minY, 1.0f), spanZ = std::max(maxZ - minZ, 1.0f);
  const float horizontalSpan = std::max(spanX, spanY), horizontalScale = 1.8f / horizontalSpan;
  const float centerX = (minX + maxX) * 0.5f, centerY = (minY + maxY) * 0.5f;
  std::vector<WorldVertex> vertices;
  vertices.reserve(map.triangles.size() * 3);
  // T27: the feature material counts are counted off the BITS THE VERTICES
  // CARRY, not off the blit set. A material can be blitted into an atlas and
  // still have no vertex that samples it (the product path refuses layer 2 when
  // layer 1 is absent); reporting the blit set would say five materials apply
  // while the shader sees one.
  std::unordered_set<std::string> bumpVertexMaterials;
  std::unordered_set<std::string> bump2VertexMaterials;
  std::unordered_set<std::string> detailVertexMaterials;
  // T12: 256 buckets over [0,1]. The count of distinct buckets is what
  // separates "the painted weights arrived" from "a constant was written".
  std::vector<unsigned char> weightBuckets(256, 0);
  // T27: the same instrument for `$bumpmap2`'s lerp weight, because the SDK
  // blends layer 2's normal by the vertex's blend factor -- a constant 1 and a
  // painted gradient both draw a picture, and only the distribution tells them
  // apart.
  std::vector<unsigned char> bump2WeightBuckets(256, 0);
  std::unordered_set<std::string> detailMaterials;
  std::size_t lightmappedVertices = 0;
  std::size_t displacedTriangles = 0;
  std::size_t waterTriangles = 0;
  const auto convert = [&](const BspVertex& point, const BspVertex& normal) {
    return WorldVertex{(point.x - centerX) * horizontalScale, (point.y - centerY) * horizontalScale, (point.z - minZ) / spanZ * 0.8f + 0.1f, 0.62f, 0.65f, 0.68f, 0.0f, 0.0f, 0.0f, normal.x, normal.y, normal.z};
  };
  // T19: material -> its slot in the plan. A material the plan refused is
  // absent here, and the vertex loop's else branch is then the flat fallback the
  // shader already implements -- the same branch a material with no texture has
  // always taken, which is why an omission is a shade and not a wrong tile.
  std::unordered_map<std::string, std::size_t> tileByMaterial;
  tileByMaterial.reserve(plan.accepted);
  for (std::size_t index = 0; index < textures.size(); ++index) {
    if (plan.slots[index].batch >= 0) tileByMaterial.emplace(textures[index].material, index);
  }
  // A triangle whose material got no tile is still drawn: its vertex alpha is
  // 0, the pixel shader's `lerp(colour.rgb, sampled * colour.rgb, alpha)` then
  // uses the flat shade, and untiled geometry is a picture of the map rather
  // than a hole. It rides in batch 0 so it has an atlas to be drawn with, and
  // the flat branch is what keeps that atlas from painting it.
  const std::int16_t untiledBatch = plan.batches.empty() ? -1 : 0;
  worldTriangleBatch_.assign(map.triangles.size(), untiledBatch);
  // The ordinal of the triangle inside the buffer being uploaded, which is what
  // an index has to be built from. It equals the map's own ordinal unless the
  // terrain filter drops a triangle, and in that mode no batch is built at all
  // (see below), so the two never disagree where it matters.
  std::size_t uploadedTriangle = 0;
  for (const auto& triangle : map.triangles) {
    // T12: the terrain-only frame. The bounds above were taken over every
    // triangle on purpose, so filtering here leaves the terrain at exactly the
    // position and scale it has in the full frame -- a mask taken from a
    // re-centred world would not overlay the picture it is meant to bound.
    if (worldTerrainOnly_ && terrainMaterials.count(triangle.material) == 0) continue;
    const auto ab = BspVertex{triangle.b.x - triangle.a.x, triangle.b.y - triangle.a.y, triangle.b.z - triangle.a.z};
    const auto ac = BspVertex{triangle.c.x - triangle.a.x, triangle.c.y - triangle.a.y, triangle.c.z - triangle.a.z};
    const float nx = ab.y * ac.z - ab.z * ac.y, ny = ab.z * ac.x - ab.x * ac.z, nz = ab.x * ac.y - ab.y * ac.x;
    const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    const auto normal = normalLength > 1e-6f ? BspVertex{nx / normalLength, ny / normalLength, nz / normalLength} : BspVertex{0.0f, 0.0f, 1.0f};
    auto a = convert(triangle.a, normal), b = convert(triangle.b, normal), c = convert(triangle.c, normal);
    const auto tile = tileByMaterial.find(triangle.material);
    if (tile != tileByMaterial.end()) {
      const WorldTileSlot& t = plan.slots[tile->second];
      const WorldTileBatch& batch = plan.batches[static_cast<std::size_t>(t.batch)];
      a.r = a.g = a.b = b.r = b.g = b.b = c.r = c.g = c.b = 1.0f;
      a.a = b.a = c.a = 1.0f;
      // The uv is normalised by THIS batch's atlas: a batch of 20 tiles is
      // 5x4, so its v is divided by four tile heights and not by the width of
      // the first atlas. T21: and it is normalised by the SOURCE size, not the
      // tile size -- `worldTileCoord` scales the wrapped texcoord by
      // extent/sourceSize, which is the downscale. Passing the tile size in
      // both places collapsed that ratio to 1 and made every downscaled
      // texture repeat: 70 of the 85 tiled materials on pl_odyssey.
      // ---- R2: `$basetexturetransform` -----------------------------------
      // Source re-maps the material's texcoord before the sampler ever sees it:
      // the shader computes p = (uv - center) * scale, rotates p, and adds
      // center + translate back (lightmappedgeneric_vs20.fxc:149-150). This is
      // applied to the texcoord handed to the atlas walk, so the wrap/tile
      // arithmetic below is unchanged. It is NOT applied to the lightmap uv,
      // which is a separate attribute assigned further down: the VMT defines the
      // transform as not affecting lightmaps.
      float au = triangle.au, av = triangle.av;
      float bu = triangle.bu, bv = triangle.bv;
      float cu = triangle.cu, cv = triangle.cv;
      {
        const WorldTexture& tiledMaterial = textures[tile->second];
        // The transform is normalised by the SOURCE size, the same denominator
        // `worldTileCoord` uses. Zero means this tile never recorded one (the tile
        // is its own source); without the number there is no normalised uv to
        // rotate, and inventing one would move the texture without saying so, so
        // the walk falls back to the face's own axes.
        if (tiledMaterial.baseTextureTransform && t.sourceWidth != 0 && t.sourceHeight != 0) {
          constexpr float kPi = 3.14159265358979323846f;
          const float angle = tiledMaterial.baseTransformRotate * kPi / 180.0f;
          const float cosAngle = std::cos(angle), sinAngle = std::sin(angle);
          const float sourceW = static_cast<float>(t.sourceWidth);
          const float sourceH = static_cast<float>(t.sourceHeight);
          const float centerU = tiledMaterial.baseTransformCenter[0];
          const float centerV = tiledMaterial.baseTransformCenter[1];
          const auto applyTransform = [&](float u, float v, float* outU, float* outV) {
            const float x = (u / sourceW - centerU) * tiledMaterial.baseTransformScale[0];
            const float y = (v / sourceH - centerV) * tiledMaterial.baseTransformScale[1];
            // Positive rotate turns the texture counter-clockwise as the player
            // sees it, and v runs down the image, so the rotation below is the
            // visual one (see the note in vmt_material.cpp).
            const float rx = x * cosAngle - y * sinAngle;
            const float ry = x * sinAngle + y * cosAngle;
            *outU = (rx + centerU + tiledMaterial.baseTransformTranslate[0]) * sourceW;
            *outV = (ry + centerV + tiledMaterial.baseTransformTranslate[1]) * sourceH;
          };
          applyTransform(triangle.au, triangle.av, &au, &av);
          applyTransform(triangle.bu, triangle.bv, &bu, &bv);
          applyTransform(triangle.cu, triangle.cv, &cu, &cv);
        }
      }
      a.u = worldTileCoord(au, t.sourceWidth, t.x, t.width, batch.width);
      a.v = worldTileCoord(av, t.sourceHeight, t.y, t.height, batch.height);
      b.u = worldTileCoord(bu, t.sourceWidth, t.x, t.width, batch.width);
      b.v = worldTileCoord(bv, t.sourceHeight, t.y, t.height, batch.height);
      c.u = worldTileCoord(cu, t.sourceWidth, t.x, t.width, batch.width);
      c.v = worldTileCoord(cv, t.sourceHeight, t.y, t.height, batch.height);
      // T12: a weight is handed to the shader only for a material that shipped
      // a second layer. A displacement whose VMT names one layer keeps the
      // sentinel, because the blend atlas has no tile under its uv and the
      // branch would sample the filler.
      if (blendedMaterials.count(triangle.material)) {
        a.blend = triangle.aAlpha; b.blend = triangle.bAlpha; c.blend = triangle.cAlpha;
        const float modulate = modulatedMaterials.count(triangle.material) ? 1.0f : 0.0f;
        a.blendModulate = b.blendModulate = c.blendModulate = modulate;
      }
      // T12: counted off the *vertex*, not off the triangle. The first version of
      // this loop read `triangle.aAlpha` here, so a mutation that forced the
      // vertex weight to a constant moved no reading at all -- measured, m3 read
      // distinctWeights=9 with every uploaded weight at 1. What reaches the
      // shader is the vertex, so the vertex is what is counted; the same rule the
      // lightmap counter above states.
      for (const float weight : {a.blend, b.blend, c.blend}) {
        if (!(weight >= 0.0f)) continue;
        ++worldBlendVertices_;
        if (a.blendModulate > 0.5f) ++worldBlendModulateVertices_;
        const float clamped = std::min(std::max(weight, 0.0f), 1.0f);
        weightBuckets[static_cast<std::size_t>(clamped * 255.0f + 0.5f)] = 1;
      }
      if (triangle.lightmapped) { a.lu = triangle.alu; a.lv = triangle.alv; b.lu = triangle.blu; b.lv = triangle.blv; c.lu = triangle.clu; c.lv = triangle.clv; }
      // ---- T27: the per-material feature inputs ---------------------------
      // The mask is built from the blits this upload performed, not from the
      // VMTs: `bumpMaterials` is filled in the same loop that wrote the atlas,
      // so "the bit is set" and "there is a texel under this uv" are the same
      // statement by construction.
      {
        const WorldTexture& material = textures[tile->second];
        int mask = 0;
        if (bumpMaterials.count(triangle.material)) mask |= 1;
        // T27: layer 2 has no base normal unless layer 1 was blitted, so the
        // product path only sets bit 2 when bit 0 is set. The diagnostic does
        // NOT set bit 2 on a material without layer 1: measured on this machine,
        // that state moved 38,538 px even with the layer-2 arm deleted, and it
        // still did after the layer-1 writes were made selects -- the state
        // itself, not one write, and the mechanism was not isolated. So the
        // diagnostic instead sets `featureDiag` (the shader's `feature.w`) and
        // lerps onto the base the frame already computed.
        const bool layer2Declared = bump2Materials.count(triangle.material) != 0;
        if (layer2Declared && (mask & 1) != 0) mask |= 2;
        const bool layer2Diagnostic = layer2Declared && worldBump2Diagnostic_;
        const bool detailDrawn = worldDetailEnabled_
          && detailBlitMaterials.count(triangle.material) != 0;
        if (detailDrawn) mask |= 4;
        // T96: the alpha keys, same per-vertex channel as the feature bits
        // above. Bit 8 = `$translucent`, bit 16 = `$alphatest`, set from the
        // VMT's own keys rather than from a global switch: the keys are per
        // material and the shader is told per vertex. A translucent material
        // whose blend state is missing keeps its bit and reads mode=refused on
        // the consumption line, rather than silently drawing opaque.
        const bool alphaBlended = material.translucent;
        const bool alphaCutout = material.alphaTest;
        if (alphaBlended) mask |= 8;
        if (alphaCutout) mask |= 16;
        a.featureMask = b.featureMask = c.featureMask = static_cast<float>(mask);
        // Counted off what reached the vertex buffer, the same rule the feature
        // counters use: a declared material whose triangle never got here is
        // not a drawn triangle.
        if (alphaBlended) ++worldBlendedTriangleCount_;
        else if (alphaCutout) ++worldCutoutTriangleCount_;
        a.detailMode = b.detailMode = c.detailMode = static_cast<float>(material.detailMode);
        a.detailFactor = b.detailFactor = c.detailFactor = material.detailFactor;
        a.featureDiag = b.featureDiag = c.featureDiag = layer2Diagnostic ? 1.0f : 0.0f;
        // The face's own texture basis. Normalised here rather than in the
        // shader because it is constant across the face and already in map
        // space, which is the space the uv was computed in.
        float tx = triangle.uAxis[0], ty = triangle.uAxis[1], tz = triangle.uAxis[2];
        const float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (tlen > 1.0e-6f) { tx /= tlen; ty /= tlen; tz /= tlen; }
        else { tx = 0.0f; ty = 0.0f; tz = 1.0f; }
        // The bitangent's sign: does cross(N, T) point along the map's +v axis?
        const float bx = normal.y * tz - normal.z * ty;
        const float by = normal.z * tx - normal.x * tz;
        const float bz = normal.x * ty - normal.y * tx;
        const float vSign = (bx * triangle.vAxis[0] + by * triangle.vAxis[1]
          + bz * triangle.vAxis[2]) < 0.0f ? -1.0f : 1.0f;
        a.tangentX = b.tangentX = c.tangentX = tx;
        a.tangentY = b.tangentY = c.tangentY = ty;
        a.tangentZ = b.tangentZ = c.tangentZ = tz;
        a.tangentSign = b.tangentSign = c.tangentSign = vSign;
        // Counted off the vertices, the same rule the blend weights above use:
        // a material in the set whose triangle never reached the buffer must not
        // read as applied.
        if ((mask & 1) != 0) {
          worldBumpFeature_.vertices += 3;
          bumpVertexMaterials.insert(triangle.material);
        }
        if ((mask & 2) != 0 || layer2Diagnostic) {
          worldBump2Feature_.vertices += 3;
          bump2VertexMaterials.insert(triangle.material);
          // The weight the shader will lerp layer 2's normal by: `max(blend,0)`,
          // saturate, exactly what the pixel shader computes.
          const float clamped = std::min(std::max(a.blend, 0.0f), 1.0f);
          bump2WeightBuckets[static_cast<std::size_t>(clamped * 255.0f + 0.5f)] = 1;
        }
        if ((mask & 4) != 0) {
          worldDetailFeature_.vertices += 3;
          detailMaterials.insert(triangle.material);
          detailVertexMaterials.insert(triangle.material);
        }
      }
      // T96: the value is now the SUB-batch index, not the atlas batch: the
      // index buffer and the pvs regrouping both key on this, and the blend
      // state is a property of the draw call.
      worldTriangleBatch_[uploadedTriangle] = static_cast<std::int16_t>(
        static_cast<int>(t.batch) * 2 + (textures[tile->second].translucent ? 1 : 0));
    } else {
      const std::uint32_t hash = static_cast<std::uint32_t>(std::hash<std::string>{}(triangle.material));
      const float shade = 0.78f + static_cast<float>(hash % 23) / 100.0f;
      a.r = a.g = a.b = b.r = b.g = b.b = c.r = c.g = c.b = shade;
      // T59 diagnostic: the same shade, replaced by magenta on request so the untiled
      // set is countable in pixels. The alpha stays 0 -- that is what makes the pixel
      // shader take the flat branch at all.
      if (worldRefusedMask_) {
        a.r = b.r = c.r = 1.0f;
        a.g = b.g = c.g = 0.0f;
        a.b = b.b = c.b = 1.0f;
      }
      // The lightmap is per *face*, not per *texture*: a face whose material
      // was refused a tile still has its own RGBExp32 slice, and dropping its
      // light uv here would draw it flat. On snakewater the atlas covered 64 of
      // 148 materials before T19, so this branch was where most of the map's
      // lightmapped triangles lost their lighting -- the atlas was bound, the
      // status read Active, and most of the map sampled nothing.
      if (triangle.lightmapped) { a.lu = triangle.alu; a.lv = triangle.alv; b.lu = triangle.blu; b.lv = triangle.blv; c.lu = triangle.clu; c.lv = triangle.clv; }
    }
    // Counted off the vertex, not off the triangle: the pixel shader samples
    // the lightmap when lightUv.x >= 0.0, so a counter keyed on
    // triangle.lightmapped would keep reporting the intent after the uv had
    // been dropped -- which is how a regression injection went unnoticed the
    // first time this instrument was written.
    if (a.lu >= 0.0f) lightmappedVertices += 3;
    // Counted here rather than trusted from the parse: this loop is what decides
    // how many triangles exist, and both tags are the parser's own on the
    // triangle it emitted -- a displacement face arrives already subdivided into
    // its grid, so a displaced triangle here is a tessellated one and the count
    // is the geometry the renderer will draw.
    if (triangle.displacement) ++displacedTriangles;
    if (triangle.water) ++waterTriangles;
    // T26: the same id write as the single-material overload above. Without it
    // `--world-face-id` is white on the product path, which is the path the
    // seam trace has to read.
    if (worldFaceIdUpload_ && triangle.faceIndex >= 0) {
      writeWorldFaceId(a.r, a.g, a.b, a.a, triangle.faceIndex);
      writeWorldFaceId(b.r, b.g, b.b, b.a, triangle.faceIndex);
      writeWorldFaceId(c.r, c.g, c.b, c.a, triangle.faceIndex);
    }
    vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
    ++uploadedTriangle;
  }
  // T12: the weight distribution, summarised from the buckets the loop filled.
  // Computed here rather than in the pixel shader because it is a fact about
  // what was uploaded, and a shader-side counter would be a reading about the
  // branch rather than about the geometry.
  worldBlendWeightMin_ = -1.0f;
  worldBlendWeightMax_ = -1.0f;
  worldBlendDistinctWeights_ = 0;
  for (std::size_t bucket = 0; bucket < weightBuckets.size(); ++bucket) {
    if (!weightBuckets[bucket]) continue;
    const float value = static_cast<float>(bucket) / 255.0f;
    if (worldBlendWeightMin_ < 0.0f || value < worldBlendWeightMin_) worldBlendWeightMin_ = value;
    if (value > worldBlendWeightMax_) worldBlendWeightMax_ = value;
    ++worldBlendDistinctWeights_;
  }
  // T27: the detail layer's material count, and `$bumpmap2`'s weight
  // distribution -- the reading that separates the painted blend from a
  // constant.
  worldBumpFeature_.materials = bumpVertexMaterials.size();
  worldBump2Feature_.materials = bump2VertexMaterials.size();
  worldDetailFeature_.materials = detailVertexMaterials.size();
  worldDetailFeature_.tiles = detailMaterials.size();
  worldBump2Feature_.bump2WeightMin = -1.0f;
  worldBump2Feature_.bump2WeightMax = -1.0f;
  worldBump2Feature_.bump2DistinctWeights = 0;
  for (std::size_t bucket = 0; bucket < bump2WeightBuckets.size(); ++bucket) {
    if (!bump2WeightBuckets[bucket]) continue;
    const float value = static_cast<float>(bucket) / 255.0f;
    if (worldBump2Feature_.bump2WeightMin < 0.0f
        || value < worldBump2Feature_.bump2WeightMin) {
      worldBump2Feature_.bump2WeightMin = value;
    }
    if (value > worldBump2Feature_.bump2WeightMax) worldBump2Feature_.bump2WeightMax = value;
    ++worldBump2Feature_.bump2DistinctWeights;
  }
  // T12: an empty filter result is a refusal, not a zero-triangle buffer.
  // CreateBuffer with ByteWidth 0 fails on its own, but it fails with E_FAIL
  // and no reason; the caller would then see `uploadedWorld == false` and,
  // before the fallback was guarded, draw the whole world in `terrain` mode.
  if (vertices.empty()) {
    worldAtlasReason_ = worldTerrainOnly_
      ? "the terrain filter kept no triangle, so nothing was uploaded"
      : "the map produced no vertex, so nothing was uploaded";
    lastError_ = HRESULT_FROM_WIN32(ERROR_NO_DATA);
    return false;
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = vertices.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) {
    lastError_ = E_FAIL;
    return false;
  }
  worldVertexBuffer_ = std::move(buffer);
  worldVertexCount_ = static_cast<UINT>(vertices.size());
  worldLightmappedVertexCount_ = static_cast<UINT>(lightmappedVertices);
  // ---- T19: the index buffer, grouped by batch -----------------------------
  if (worldTerrainOnly_)
  {
    // `--world-geometry terrain` uploads a *filtered* vertex buffer while the
    // pvs draw list and the batch table are indexed by the map's own triangle
    // numbers, so an index buffer built from that table would point past the
    // end of what was uploaded -- measured: the terrain-only frame lost a third
    // of its terrain region and the blend gate's on/off pair fell from 1930
    // changed pixels to 470. This mode draws the whole filtered buffer, which is
    // what it did before batching existed.
    worldDrawBatches_.clear();
    worldIndexCount_ = 0;
  }
  else
  // The vertex buffer keeps map.triangles order (the pvs draw list indexes it as
  // triangle*3, and reordering would make every index main.cpp hands over
  // wrong), so batching is expressed in the index buffer instead: batch 0's
  // triangles come first, then batch 1's, and each batch is one contiguous
  // DrawIndexed range with its own atlas. This buffer is also what the pass
  // draws with no clip at all, which is why it is built here rather than in
  // setWorldVisDrawList.
  {
    // T96: two sub-batches per atlas batch -- opaque (mode 0) and blended
    // (mode 1). The texture is the same atlas; only the device state differs.
    std::vector<std::vector<std::uint32_t>> perBatch(plan.batches.size() * 2u);
    for (std::size_t triangle = 0; triangle < worldTriangleBatch_.size(); ++triangle) {
      const int batch = worldTriangleBatch_[triangle];
      if (batch < 0) continue;
      auto& target = perBatch[static_cast<std::size_t>(batch)];
      const auto base = static_cast<std::uint32_t>(triangle * 3u);
      target.push_back(base);
      target.push_back(base + 1u);
      target.push_back(base + 2u);
    }
    std::vector<std::uint32_t> grouped;
    grouped.reserve(map.triangles.size() * 3u);
    worldDrawBatches_.clear();
    // T96: opaque sub-batches first, blended last. `perBatch` is indexed by
    // (batch, mode); the atlas views are the batch's, the mode is the only thing
    // the draw call adds. This ordering is the minimum the blend equation needs:
    // every blended range is drawn after every opaque one. Per-triangle
    // back-to-front sorting is NOT done here (the pass has no per-triangle sort)
    // and that limit is stated in docs/T96-ALPHA-PLAN.md.
    for (int alphaMode = 0; alphaMode < 2; ++alphaMode) {
      for (std::size_t batch = 0; batch < plan.batches.size(); ++batch) {
        const std::size_t sub = batch * 2u + static_cast<std::size_t>(alphaMode);
        if (sub >= perBatch.size()) continue;
        WorldDrawBatch draw;
        draw.alphaMode = alphaMode;
        draw.tiles = plan.batches[batch].tiles;
        draw.firstIndex = static_cast<UINT>(grouped.size());
        draw.indexCount = static_cast<UINT>(perBatch[sub].size());
        draw.albedoView = worldAtlasTextures_[batch].view();
        draw.blendView = batch < worldBlendAtlasTextures_.size()
          ? worldBlendAtlasTextures_[batch].view() : nullptr;
        draw.modulateView = batch < worldModulateAtlasTextures_.size()
          ? worldModulateAtlasTextures_[batch].view() : nullptr;
        // T27: the feature views are bound per batch for the same reason the
        // blend atlas is: a batch that holds no bump tile must not sample the
        // previous batch's normal map.
        draw.bumpView = batch < worldBumpAtlasTextures_.size()
          ? worldBumpAtlasTextures_[batch].view() : nullptr;
        draw.bump2View = batch < worldBump2AtlasTextures_.size()
          ? worldBump2AtlasTextures_[batch].view() : nullptr;
        draw.detailView = batch < worldDetailAtlasTextures_.size()
          ? worldDetailAtlasTextures_[batch].view() : nullptr;
        // Dense on purpose: this table gets one entry per (mode, batch) pair
        // even when the sub-batch holds no triangle, so its size is exactly
        // 2 * plan.batches.size() and the entry for a sub index is a closed
        // form instead of a search (setWorldVisDrawList maps it back).
        // Emitting only the non-empty pairs made the table a different
        // length AND a different order from the sub index, and the pvs
        // regroup then read entry N for sub-batch N -- a permutation of the
        // atlas views, so a batch drew against another batch's atlas and
        // its uv landed on the filler (measured: the include-pose
        // --world-albedo frame collapsed to a single colour, 88,88,88).
        // The draw loop skips a zero-length range anyway.
        worldDrawBatches_.push_back(draw);
        if (alphaMode == 1 && draw.indexCount > 0) ++worldBlendedBatchCount_;
        if (!perBatch[sub].empty()) {
          grouped.insert(grouped.end(), perBatch[sub].begin(), perBatch[sub].end());
        }
      }
    }
    if (!grouped.empty()) {
      D3D11_BUFFER_DESC indexDescription{};
      indexDescription.ByteWidth = static_cast<UINT>(grouped.size() * sizeof(std::uint32_t));
      indexDescription.Usage = D3D11_USAGE_DYNAMIC;
      indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
      indexDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
      if (FAILED(device_->CreateBuffer(&indexDescription, nullptr, indexBuffer.GetAddressOf()))) {
        lastError_ = E_FAIL;
        return false;
      }
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (FAILED(context_->Map(indexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        lastError_ = E_FAIL;
        return false;
      }
      std::memcpy(mapped.pData, grouped.data(), grouped.size() * sizeof(std::uint32_t));
      context_->Unmap(indexBuffer.Get(), 0);
      worldIndexBuffer_ = std::move(indexBuffer);
      worldIndexCapacity_ = static_cast<UINT>(grouped.size());
      worldIndexCount_ = static_cast<UINT>(grouped.size());
    }
  }
  {
    float cx0 = 1e30f, cy0 = 1e30f, cz0 = 1e30f, cx1 = -1e30f, cy1 = -1e30f, cz1 = -1e30f;
    for (const auto& v : vertices) {
      cx0 = std::min(cx0, v.x); cy0 = std::min(cy0, v.y); cz0 = std::min(cz0, v.z);
      cx1 = std::max(cx1, v.x); cy1 = std::max(cy1, v.y); cz1 = std::max(cz1, v.z);
    }
    worldConvertedMin_[0] = cx0; worldConvertedMin_[1] = cy0; worldConvertedMin_[2] = cz0;
    worldConvertedMax_[0] = cx1; worldConvertedMax_[1] = cy1; worldConvertedMax_[2] = cz1;
  }
  worldDisplacedTriangleCount_ = static_cast<UINT>(displacedTriangles);
  worldWaterTriangleCount_ = static_cast<UINT>(waterTriangles);
  worldMinX_ = minX; worldMinY_ = minY; worldMinZ_ = minZ;
  worldMaxX_ = maxX; worldMaxY_ = maxY; worldMaxZ_ = maxZ;
  worldCenterX_ = centerX; worldCenterY_ = centerY;
  worldSpanZ_ = spanZ; worldHorizontalScale_ = horizontalScale;
  worldBoundsValid_ = true;
  worldLightmapStatus_ = WorldLightmapStatus::FallbackUnlit;
  worldLightmapIntensity_ = std::clamp(map.lightmapIntensity, 0.15f, 2.0f);
  if (map.lightmapTriangleCount > 0) worldLightmapStatus_ = WorldLightmapStatus::Active;
  return true;
}

bool Renderer::uploadWorldSkyGeometry(const std::vector<WorldSkyVertex>& vertices) {
  worldSkyVertexBuffer_.Reset();
  worldSkyVertexCount_ = 0;
  if (vertices.empty()) {
    worldSkyReason_ = "no sky vertices were built";
    return true;
  }
  if (vertices.size() > (std::numeric_limits<UINT>::max)() / sizeof(WorldVertex)) {
    worldSkyReason_ = "the sky box is too large to upload";
    return false;
  }
  // Uploaded as WorldVertex, not as a sky-specific layout: the pass reuses the
  // world shader and its input layout, and a second vertex layout for six quads
  // would be one more thing to keep in step with the shader. The fields the sky
  // does not use are left at the values the shader ignores in sky mode.
  std::vector<WorldVertex> gpu;
  gpu.reserve(vertices.size());
  for (const auto& vertex : vertices) {
    WorldVertex converted{};
    converted.x = vertex.x; converted.y = vertex.y; converted.z = vertex.z;
    converted.r = 1.0f; converted.g = 1.0f; converted.b = 1.0f; converted.a = 0.0f;
    converted.u = vertex.u; converted.v = vertex.v;
    converted.nx = 0.0f; converted.ny = 0.0f; converted.nz = 0.0f;
    converted.lu = -1.0f; converted.lv = -1.0f;
    gpu.push_back(converted);
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(gpu.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = gpu.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) {
    worldSkyReason_ = "CreateBuffer refused the sky box";
    return false;
  }
  worldSkyVertexBuffer_ = std::move(buffer);
  worldSkyVertexCount_ = static_cast<UINT>(gpu.size());
  return true;
}

void Renderer::setWorldVisClipEnabled(bool enabled) {
  worldVisClipEnabled_ = enabled;
  if (!enabled) {
    worldVisClipApplied_ = false;
    worldVisClipState_ = WorldVisClipState::Disabled;
    worldVisClipReason_ = "vis clip disabled by the caller";
  }
}

// ---------------------------------------------------------------------------
// T2 (2026-10-14): the water surface
// ---------------------------------------------------------------------------
// A second world buffer holding only the water triangles, in the same converted
// space and the same vertex layout as the world buffer. The layout is shared on
// purpose (see the static_assert at the top of this file) and the buffer is
// separate on purpose: the water pass draws its own subset, so switching it off
// cannot change a single triangle the world pass draws and cannot move the pvs
// clip's `drawn + hidden == input` identity.

bool Renderer::uploadWorldWaterGeometry(const std::vector<WorldWaterVertex>& vertices) {
  worldWaterVertexBuffer_.Reset();
  worldWaterVertexCount_ = 0;
  worldWaterDrewThisFrame_ = false;
  if (vertices.empty()) {
    worldWaterState_ = WorldWaterSurfaceState::Unavailable;
    worldWaterReason_ = "no water geometry was built";
    return true;
  }
  // Not a whole number of triangles is a corrupted buffer, not water. Refusing
  // by name is what stops a truncated upload from drawing half a surface and
  // still counting as a face.
  if (vertices.size() % 3u != 0u || vertices.size() % 2u != 0u) {
    worldWaterState_ = WorldWaterSurfaceState::Unavailable;
    worldWaterReason_ = "the water buffer holds " + std::to_string(vertices.size())
      + " vertices, which is not a whole number of faces";
    return false;
  }
  if (vertices.size() > (std::numeric_limits<UINT>::max)() / sizeof(WorldVertex)) {
    worldWaterState_ = WorldWaterSurfaceState::Unavailable;
    worldWaterReason_ = "the water surface is too large to upload";
    return false;
  }
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(WorldVertex));
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = vertices.data();
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device_->CreateBuffer(&description, &initial, buffer.GetAddressOf()))) {
    worldWaterState_ = WorldWaterSurfaceState::Unavailable;
    worldWaterReason_ = "CreateBuffer refused the water surface";
    return false;
  }
  worldWaterVertexBuffer_ = std::move(buffer);
  worldWaterVertexCount_ = static_cast<UINT>(vertices.size());
  return true;
}

bool Renderer::setWorldVisDrawList(const WorldVisDrawList& drawList) {
  worldVisClipState_ = drawList.state;
  worldVisHiddenTriangles_ = drawList.hiddenTriangles;
  worldVisTrianglesWithoutCluster_ = drawList.trianglesWithoutCluster;
  worldVisInputTriangles_ = drawList.inputTriangles;
  worldVisCameraCluster_ = drawList.cameraCluster;
  worldVisClipReason_ = drawList.reason;
  if (!worldVisClipEnabled_ || !drawList.applied || drawList.indices.empty()) {
    // Everything is drawn, which is what the pass did before the clip existed.
    // The reason word still says why, so "off" and "the pvs could not answer"
    // are not the same line.
    worldVisClipApplied_ = false;
    worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
    return true;
  }
  // T19: the draw list arrives in map.triangles order (triangle*3 + j) and the
  // index buffer is grouped by batch, so the surviving triangles are regrouped
  // here. `worldTriangleBatch_` is the same mapping the upload built, which is
  // what keeps the draw list and the atlas from disagreeing about which batch a
  // triangle belongs to.
  if (worldDrawBatches_.empty()) {
    // No batch table: `--world-geometry terrain` filtered the vertex buffer and
    // the list was computed against the full triangle set, so applying it here
    // would index past the buffer. The whole filtered buffer is drawn, which is
    // this mode's behaviour from before batching.
    worldVisClipApplied_ = false;
    worldVisClipReason_ = "this geometry carries no batch table, so the whole buffer is drawn";
    worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
    return true;
  }
  // Entry index for a sub-batch index. The table is dense and ordered by
  // DRAW order -- every opaque sub-batch first, then every blended one --
  // while `worldTriangleBatch_` stores `batch * 2 + mode`. Reading the table
  // with the sub index directly is therefore a permutation once there is
  // more than one atlas batch: the entry carries another batch's atlas view
  // and the triangle's uv, which addresses its OWN batch's rectangles,
  // lands on the filler. Measured before this mapping existed: the
  // include-pose --world-albedo frame was one colour (88,88,88).
  const std::size_t entryCount = worldDrawBatches_.size();
  const std::size_t atlasBatches = entryCount / 2u;
  std::vector<std::vector<std::uint32_t>> perBatch(entryCount);
  for (const std::uint32_t index : drawList.indices) {
    const std::size_t triangle = static_cast<std::size_t>(index) / 3u;
    const int sub = triangle < worldTriangleBatch_.size()
      ? worldTriangleBatch_[triangle] : (worldDrawBatches_.empty() ? -1 : 0);
    if (sub < 0) continue;
    const std::size_t at = static_cast<std::size_t>(sub);
    const std::size_t entry = (at % 2u) * atlasBatches + at / 2u;
    if (entry >= entryCount) continue;
    perBatch[entry].push_back(index);
  }
  std::vector<std::uint32_t> regrouped;
  regrouped.reserve(drawList.indices.size());
  for (std::size_t batch = 0; batch < perBatch.size(); ++batch) {
    worldDrawBatches_[batch].firstIndex = static_cast<UINT>(regrouped.size());
    worldDrawBatches_[batch].indexCount = static_cast<UINT>(perBatch[batch].size());
    regrouped.insert(regrouped.end(), perBatch[batch].begin(), perBatch[batch].end());
  }
  const UINT needed = static_cast<UINT>(regrouped.size());
  if (needed == 0) {
    worldVisClipApplied_ = false;
    worldVisClipReason_ = "the pvs list was empty after regrouping, so the whole buffer is drawn";
    worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
    return true;
  }
  if (needed > worldIndexCapacity_ || !worldIndexBuffer_) {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(drawList.indices.size() * sizeof(std::uint32_t));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_INDEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    if (FAILED(device_->CreateBuffer(&description, nullptr, buffer.GetAddressOf()))) {
      worldVisClipApplied_ = false;
      worldVisClipReason_ = "CreateBuffer refused the pvs index buffer";
      worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
      return false;
    }
    worldIndexBuffer_ = std::move(buffer);
    worldIndexCapacity_ = needed;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(worldIndexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    worldVisClipApplied_ = false;
    worldVisClipReason_ = "Map refused the pvs index buffer";
    worldVisDrawnTriangles_ = worldVertexCount_ / 3u;
    return false;
  }
  std::memcpy(mapped.pData, regrouped.data(),
    static_cast<std::size_t>(needed) * sizeof(std::uint32_t));
  context_->Unmap(worldIndexBuffer_.Get(), 0);
  worldIndexCount_ = needed;
  worldVisClipApplied_ = true;
  worldVisDrawnTriangles_ = drawList.outputTriangles;
  return true;
}

} // namespace tf2::native
