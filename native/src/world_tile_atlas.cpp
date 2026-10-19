#include "world_tile_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace tf2::native {
namespace {

// The smallest square grid that holds n tiles: ceil(sqrt(n)) columns. A batch
// of 20 therefore costs 5x4 and not 8x8, which is the difference between 21 MiB
// and 67.6 MiB for the last batch on the reference map.
int gridColumns(int tiles) {
  if (tiles <= 0) return 0;
  int columns = 1;
  while (columns * columns < tiles) ++columns;
  return columns;
}

int gridRows(int tiles, int columns) {
  return columns <= 0 ? 0 : (tiles + columns - 1) / columns;
}

std::uint64_t bytesFor(int columns, int rows, std::uint32_t tileSize) {
  return static_cast<std::uint64_t>(columns) * tileSize
    * static_cast<std::uint64_t>(rows) * tileSize * 4ull;
}

} // namespace

const char* worldTileRefusalName(WorldTileRefusal reason) {
  switch (reason) {
    case WorldTileRefusal::None: return "none";
    case WorldTileRefusal::EmptyMaterial: return "empty-material";
    case WorldTileRefusal::EmptyPixels: return "empty-pixels";
    case WorldTileRefusal::TileTooLarge: return "tile-too-large";
    case WorldTileRefusal::PixelCountMismatch: return "pixel-count-mismatch";
    case WorldTileRefusal::DuplicateMaterial: return "duplicate-material";
    case WorldTileRefusal::AtlasBudgetExceeded: return "atlas-budget";
  }
  return "unknown";
}

WorldAtlasPlan planWorldTiles(const std::vector<WorldTileSource>& sources,
    const WorldAtlasPlanOptions& options) {
  WorldAtlasPlan plan;
  plan.slots.resize(sources.size());
  plan.refusals.assign(sources.size(), WorldTileRefusal::None);
  const std::uint32_t largestTile = options.tileSize > 2u * options.tileBorder
    ? options.tileSize - 2u * options.tileBorder : 0u;
  const int maxPerBatch = static_cast<int>(std::max<std::uint32_t>(1u, options.maxTilesPerBatch));

  // ---- 1. validate, dedupe, and group into batches by count ----------------
  // The grouping is by the caller's order, which since T12 is coverage order
  // (largest surface first). A batch boundary therefore falls on the smallest
  // surfaces, and the tiles that a budget refusal would drop are the ones the
  // picture depends on least -- a fact the reading reports rather than the
  // policy assuming.
  std::vector<std::vector<std::size_t>> batchSources;
  std::unordered_set<std::string> seen;
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const WorldTileSource& source = sources[index];
    WorldTileRefusal refusal = WorldTileRefusal::None;
    if (source.material.empty()) refusal = WorldTileRefusal::EmptyMaterial;
    else if (source.width == 0 || source.height == 0) refusal = WorldTileRefusal::EmptyPixels;
    else if (source.width > largestTile || source.height > largestTile) {
      refusal = WorldTileRefusal::TileTooLarge;
    } else if (source.rgbaBytes
        != static_cast<std::size_t>(source.width) * source.height * 4u) {
      refusal = WorldTileRefusal::PixelCountMismatch;
    } else if (!seen.insert(source.material).second) {
      refusal = WorldTileRefusal::DuplicateMaterial;
    }
    if (refusal != WorldTileRefusal::None) {
      plan.refusals[index] = refusal;
      if (refusal == WorldTileRefusal::DuplicateMaterial) ++plan.refusedDuplicate;
      else ++plan.refusedMalformed;
      ++plan.refused;
      continue;
    }
    if (batchSources.empty()
        || static_cast<int>(batchSources.back().size()) >= maxPerBatch) {
      batchSources.emplace_back();
    }
    batchSources.back().push_back(index);
  }

  // ---- 2. lay each batch out and price it ----------------------------------
  struct PricedBatch {
    WorldTileBatch batch;
    std::vector<std::size_t> sources;
    bool hasBlend = false;
    bool hasModulate = false;
    std::uint64_t totalBytes = 0;
  };
  std::vector<PricedBatch> priced;
  for (const auto& members : batchSources) {
    PricedBatch batch;
    batch.sources = members;
    batch.batch.tiles = static_cast<int>(members.size());
    batch.batch.columns = gridColumns(batch.batch.tiles);
    batch.batch.rows = gridRows(batch.batch.tiles, batch.batch.columns);
    batch.batch.width = static_cast<std::uint32_t>(batch.batch.columns) * options.tileSize;
    batch.batch.height = static_cast<std::uint32_t>(batch.batch.rows) * options.tileSize;
    batch.batch.albedoBytes = bytesFor(batch.batch.columns, batch.batch.rows, options.tileSize);
    for (const std::size_t index : members) {
      if (sources[index].blendBytes > 0) batch.hasBlend = true;
      if (sources[index].modulateBytes > 0) batch.hasModulate = true;
    }
    batch.totalBytes = batch.batch.albedoBytes
      + (batch.hasBlend ? batch.batch.albedoBytes : 0)
      + (batch.hasModulate ? batch.batch.albedoBytes : 0);
    priced.push_back(std::move(batch));
  }

  // ---- 3. the budget, charged per batch -------------------------------------
  // A batch is the unit of allocation (one albedo texture, plus the blend and
  // modulate textures if any tile needs them), so it is also the unit of
  // refusal: half a batch has no texture to live in. Batches are walked in
  // order and the first one that does not fit ends the plan -- later batches
  // hold smaller surfaces than it does, so skipping one to take another would
  // be choosing a worse picture for the same money.
  std::size_t admitted = 0;
  std::uint64_t spentAlbedo = 0, spentBlend = 0, spentModulate = 0;
  for (const auto& batch : priced) {
    const std::uint64_t nextAlbedo = spentAlbedo + batch.batch.albedoBytes;
    const std::uint64_t nextBlend = spentBlend + (batch.hasBlend ? batch.batch.albedoBytes : 0);
    const std::uint64_t nextModulate = spentModulate
      + (batch.hasModulate ? batch.batch.albedoBytes : 0);
    if (nextAlbedo + nextBlend + nextModulate > options.budgetBytes) break;
    spentAlbedo = nextAlbedo;
    spentBlend = nextBlend;
    spentModulate = nextModulate;
    ++admitted;
  }

  // ---- 4. place the admitted tiles, name the rest ---------------------------
  for (std::size_t batchIndex = 0; batchIndex < priced.size(); ++batchIndex) {
    const auto& batch = priced[batchIndex];
    if (batchIndex >= admitted) {
      for (const std::size_t index : batch.sources) {
        plan.refusals[index] = WorldTileRefusal::AtlasBudgetExceeded;
        ++plan.refusedBudget;
        ++plan.refused;
      }
      continue;
    }
    const int placedBatch = static_cast<int>(plan.batches.size());
    for (std::size_t order = 0; order < batch.sources.size(); ++order) {
      const std::size_t index = batch.sources[order];
      WorldTileSlot& slot = plan.slots[index];
      slot.batch = placedBatch;
      slot.tile = static_cast<int>(order);
      slot.x = static_cast<std::uint32_t>(order % batch.batch.columns) * options.tileSize
        + options.tileBorder;
      slot.y = static_cast<std::uint32_t>(order / batch.batch.columns) * options.tileSize
        + options.tileBorder;
      slot.width = sources[index].width;
      slot.height = sources[index].height;
      // T21: the uv normaliser is the source size, not the tile size. A source
      // that was downscaled to the tile keeps its own texcoord range, and
      // handing worldTileCoord the tile size made every oversized texture wrap
      // early -- measured on pl_odyssey: 70 of 85 tiled materials ship a
      // texture larger than 512, and with the tile size in this field each of
      // them repeated 2x2 (1024 source) or 4x4 (2048 source) across the face
      // it belongs to.
      slot.sourceWidth = sources[index].sourceWidth > 0
        ? sources[index].sourceWidth : sources[index].width;
      slot.sourceHeight = sources[index].sourceHeight > 0
        ? sources[index].sourceHeight : sources[index].height;
      slot.blend = sources[index].blendBytes > 0;
      slot.modulate = sources[index].modulateBytes > 0;
      ++plan.accepted;
    }
    WorldTileBatch placed = batch.batch;
    for (const std::size_t index : batch.sources) {
      if (plan.slots[index].blend) ++placed.blendTiles;
      if (plan.slots[index].modulate) ++placed.modulateTiles;
    }
    plan.batches.push_back(placed);
  }
  plan.albedoBytes = spentAlbedo;
  plan.blendBytes = spentBlend;
  plan.modulateBytes = spentModulate;

  char summary[512] = {0};
  std::snprintf(summary, sizeof(summary),
    "%zu of %zu materials tiled in %zu batch(es); refused=%zu (malformed=%zu, duplicate=%zu,"
    " budget=%zu); layers=%llu+%llu+%llu MiB",
    plan.accepted, sources.size(), plan.batches.size(), plan.refused, plan.refusedMalformed,
    plan.refusedDuplicate, plan.refusedBudget,
    static_cast<unsigned long long>(plan.albedoBytes / (1024ull * 1024ull)),
    static_cast<unsigned long long>(plan.blendBytes / (1024ull * 1024ull)),
    static_cast<unsigned long long>(plan.modulateBytes / (1024ull * 1024ull)));
  plan.reason = summary;
  return plan;
}

void worldTileBlit(std::vector<std::uint8_t>& atlas, std::uint32_t atlasWidth,
    std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
    const std::vector<std::uint8_t>& pixels) {
  if (atlasWidth == 0 || width == 0 || height == 0 || x == 0 || y == 0) return;
  if (pixels.size() < static_cast<std::size_t>(width) * height * 4u) return;
  const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
  for (std::uint32_t row = 0; row < height; ++row) {
    const std::size_t rowStart = (static_cast<std::size_t>(y + row) * atlasWidth + x) * 4u;
    if (rowStart + rowBytes + 4u > atlas.size()) return;
    std::memcpy(atlas.data() + rowStart, pixels.data() + static_cast<std::size_t>(row) * rowBytes,
      rowBytes);
    // The border: the tile's own first and last texel, duplicated outside it.
    std::memcpy(atlas.data() + rowStart - 4u, pixels.data() + static_cast<std::size_t>(row) * rowBytes, 4u);
    std::memcpy(atlas.data() + rowStart + rowBytes,
      pixels.data() + static_cast<std::size_t>(row) * rowBytes + rowBytes - 4u, 4u);
  }
  const std::size_t topRow = (static_cast<std::size_t>(y - 1) * atlasWidth + x - 1) * 4u;
  const std::size_t bottomRow = (static_cast<std::size_t>(y + height) * atlasWidth + x - 1) * 4u;
  if (bottomRow + (rowBytes + 8u) > atlas.size()) return;
  std::memcpy(atlas.data() + topRow, atlas.data() + (static_cast<std::size_t>(y) * atlasWidth + x - 1) * 4u,
    rowBytes + 8u);
  std::memcpy(atlas.data() + bottomRow,
    atlas.data() + (static_cast<std::size_t>(y + height - 1) * atlasWidth + x - 1) * 4u,
    rowBytes + 8u);
}

float worldTileCoord(float texcoord, std::uint32_t sourceSize, std::uint32_t offset,
    std::uint32_t extent, std::uint32_t atlasSize) {
  if (sourceSize == 0 || atlasSize == 0) return 0.0f;
  const float size = static_cast<float>(sourceSize);
  const float wrapped = texcoord - std::floor(texcoord / size) * size;
  return (static_cast<float>(offset) + wrapped * static_cast<float>(extent) / size + 0.5f)
    / static_cast<float>(atlasSize);
}

} // namespace tf2::native
