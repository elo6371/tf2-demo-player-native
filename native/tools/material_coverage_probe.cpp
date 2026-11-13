// material_coverage_probe -- T19: the atlas plan and the shader-feature ledger,
// measured without a GPU.
//
// WHAT IT ANSWERS
//   The task's first step is "measure which omitted atlas materials are visible
//   in the current PVS and which affect the reference frame". The product can
//   answer the PVS half (it owns the camera); this probe answers the other half
//   -- which materials exist, what they resolve to, and what the plan does with
//   them -- by running the same two modules the renderer runs:
//
//     * planWorldTiles() decides batch, tile rectangle and refusal for every
//       material, so "51 materials were dropped by the cap" becomes 51 named
//       outcomes with a reason each;
//     * worldTileBlit() and worldTileCoord() are the copy and the uv the
//       renderer performs, so a fixture can prove that tile k holds material k's
//       pixels and that a tile's border cannot read its neighbour;
//     * scanWorldMaterialFeatures() counts `$bumpmap`, `$bumpmap2`, `$ssbump`
//       and `$detail`/`%detailtype` against the resources they name.
//
//   Everything here is read-only: it parses, resolves, lays out in memory and
//   prints. It never touches the GPU and never writes a file.
//
// Usage:
//   material_coverage_probe --self-test
//   material_coverage_probe --fixture
//   material_coverage_probe --map <map.bsp> --tf-root <dir> [--expect-material <name>]
// Exit: 0 = every check passed, 1 = a check failed, 2 = bad usage.

#include "bsp_map.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"
#include "world_material.h"
#include "world_material_features.h"
#include "world_tile_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace tf2::native;

int failures = 0;

void emit(const std::string& key, const std::string& value) {
  std::cout << "{\"reading\":\"" << key << "\",\"value\":\"" << value << "\"}\n";
}
void emitNumber(const std::string& key, long long value) { emit(key, std::to_string(value)); }

void check(const char* name, bool ok, const std::string& detail) {
  std::cout << "{\"check\":\"" << name << "\",\"ok\":" << (ok ? "true" : "false")
            << ",\"detail\":\"" << detail << "\"}\n";
  if (!ok) ++failures;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
    std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> solidTile(std::uint32_t width, std::uint32_t height,
    std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u, 255u);
  for (std::size_t at = 0; at < pixels.size(); at += 4u) {
    pixels[at] = r; pixels[at + 1] = g; pixels[at + 2] = b; pixels[at + 3] = 255u;
  }
  return pixels;
}

WorldTileSource makeSource(const std::string& material, std::uint32_t edge, std::size_t extra = 0) {
  WorldTileSource source;
  source.material = material;
  source.width = edge;
  source.height = edge;
  source.rgbaBytes = static_cast<std::size_t>(edge) * edge * 4u + extra;
  return source;
}

std::uint8_t channelAt(const std::vector<std::uint8_t>& atlas, std::uint32_t atlasWidth,
    std::uint32_t x, std::uint32_t y, int channel) {
  return atlas[(static_cast<std::size_t>(y) * atlasWidth + x) * 4u + static_cast<std::size_t>(channel)];
}

// ---------------------------------------------------------------------------
// 1. the pure plan
// ---------------------------------------------------------------------------
int runSelfTest() {
  // One material: one batch, one tile, at the first slot inside the border.
  {
    const auto plan = planWorldTiles({makeSource("A", 512)});
    check("plan.single.batches", plan.batches.size() == 1,
      "batches=" + std::to_string(plan.batches.size()));
    check("plan.single.tile", plan.accepted == 1 && plan.refused == 0,
      "accepted=" + std::to_string(plan.accepted) + " refused=" + std::to_string(plan.refused));
    check("plan.single.atlas", plan.batches[0].width == 514 && plan.batches[0].height == 514,
      std::to_string(plan.batches[0].width) + "x" + std::to_string(plan.batches[0].height));
    check("plan.single.origin", plan.slots[0].x == 1 && plan.slots[0].y == 1,
      std::to_string(plan.slots[0].x) + "," + std::to_string(plan.slots[0].y));
  }
  // The reference map's shape: 148 tiles, batches of 64, the last one sized to
  // its own count instead of to a full 8x8.
  {
    std::vector<WorldTileSource> sources;
    for (int index = 0; index < 148; ++index) {
      sources.push_back(makeSource("M" + std::to_string(index), 512));
    }
    const auto plan = planWorldTiles(sources);
    check("plan.148.accepted", plan.accepted == 148 && plan.refused == 0,
      "accepted=" + std::to_string(plan.accepted));
    check("plan.148.batches", plan.batches.size() == 3,
      "batches=" + std::to_string(plan.batches.size()));
    if (plan.batches.size() == 3) {
      check("plan.148.first", plan.batches[0].width == 4112 && plan.batches[0].height == 4112,
        std::to_string(plan.batches[0].width) + "x" + std::to_string(plan.batches[0].height));
      check("plan.148.last", plan.batches[2].tiles == 20 && plan.batches[2].width == 2570
          && plan.batches[2].height == 2056,
        std::to_string(plan.batches[2].tiles) + " tiles "
          + std::to_string(plan.batches[2].width) + "x" + std::to_string(plan.batches[2].height));
    }
    // Every accepted tile is inside its own batch and does not overlap its
    // neighbour: the plan is a layout, not a list of wishes.
    bool inside = true;
    for (std::size_t index = 0; index < sources.size(); ++index) {
      const auto& slot = plan.slots[index];
      const auto& batch = plan.batches[static_cast<std::size_t>(slot.batch)];
      if (slot.x + slot.width + 1 > batch.width || slot.y + slot.height + 1 > batch.height) inside = false;
      if (slot.x < 1 || slot.y < 1) inside = false;
    }
    check("plan.148.inside", inside, "every tile's pixels and border are inside its atlas");
    // And the rectangles are disjoint.
    bool disjoint = true;
    for (std::size_t left = 0; left < sources.size() && disjoint; ++left) {
      for (std::size_t right = left + 1; right < sources.size(); ++right) {
        const auto& a = plan.slots[left];
        const auto& b = plan.slots[right];
        if (a.batch != b.batch) continue;
        if (a.x < b.x + b.width + 1 && b.x < a.x + a.width + 1
            && a.y < b.y + b.height + 1 && b.y < a.y + a.height + 1) {
          disjoint = false;
          break;
        }
      }
    }
    check("plan.148.disjoint", disjoint, "no two tiles share a texel or a border texel");
  }
  // Every refusal branch, by name.
  {
    std::vector<WorldTileSource> sources;
    sources.push_back(makeSource("", 512));                 // EmptyMaterial
    sources.push_back(makeSource("EMPTY", 0));              // EmptyPixels
    sources.push_back(makeSource("HUGE", 1024));            // TileTooLarge
    sources.push_back(makeSource("SHORT", 256, 4));         // PixelCountMismatch
    sources.push_back(makeSource("DUP", 512));              // accepted
    sources.push_back(makeSource("DUP", 512));              // DuplicateMaterial
    const auto plan = planWorldTiles(sources);
    check("refusal.emptyMaterial",
      plan.refusals[0] == WorldTileRefusal::EmptyMaterial, worldTileRefusalName(plan.refusals[0]));
    check("refusal.emptyPixels",
      plan.refusals[1] == WorldTileRefusal::EmptyPixels, worldTileRefusalName(plan.refusals[1]));
    check("refusal.tooLarge",
      plan.refusals[2] == WorldTileRefusal::TileTooLarge, worldTileRefusalName(plan.refusals[2]));
    check("refusal.mismatch",
      plan.refusals[3] == WorldTileRefusal::PixelCountMismatch, worldTileRefusalName(plan.refusals[3]));
    check("refusal.duplicate",
      plan.refusals[5] == WorldTileRefusal::DuplicateMaterial, worldTileRefusalName(plan.refusals[5]));
    check("refusal.counts", plan.accepted == 1 && plan.refused == 5
        && plan.refusedMalformed == 4 && plan.refusedDuplicate == 1,
      "accepted=" + std::to_string(plan.accepted) + " refused=" + std::to_string(plan.refused)
        + " malformed=" + std::to_string(plan.refusedMalformed));
  }
  // The budget refuses a whole batch by name rather than truncating silently.
  {
    std::vector<WorldTileSource> sources;
    for (int index = 0; index < 148; ++index) {
      sources.push_back(makeSource("B" + std::to_string(index), 512));
    }
    WorldAtlasPlanOptions options;
    options.budgetBytes = 100ull * 1024ull * 1024ull;  // one 67.6 MiB batch
    const auto plan = planWorldTiles(sources, options);
    check("budget.batches", plan.batches.size() == 1,
      "batches=" + std::to_string(plan.batches.size()));
    check("budget.refused", plan.refusedBudget == 84 && plan.accepted == 64,
      "accepted=" + std::to_string(plan.accepted) + " budget=" + std::to_string(plan.refusedBudget));
    check("budget.reason",
      plan.refusals[64] == WorldTileRefusal::AtlasBudgetExceeded
        && worldTileRefusalName(plan.refusals[64]) == std::string("atlas-budget"),
      worldTileRefusalName(plan.refusals[64]));
  }
  // The uv: texel centre, wrap, and bounded by the tile.
  {
    const float atOrigin = worldTileCoord(0.0f, 512, 1, 512, 514);
    check("uv.origin", std::abs(atOrigin - (1.5f / 514.0f)) < 1e-6f,
      std::to_string(atOrigin));
    const float wrapped = worldTileCoord(1024.0f, 512, 1, 512, 514);
    check("uv.wrap", std::abs(wrapped - atOrigin) < 1e-6f, std::to_string(wrapped));
    // Half-texel inside the tile's own block for every texcoord in two periods.
    bool bounded = true;
    for (int step = -512; step <= 1536; step += 7) {
      const float u = worldTileCoord(static_cast<float>(step) + 0.37f, 512, 1, 512, 514);
      if (u < 1.0f / 514.0f || u > 513.0f / 514.0f) bounded = false;
    }
    check("uv.bounded", bounded, "every wrapped uv stays inside the tile's block");
  }
  // The blit: pixels land where the plan says, the border replicates the edge,
  // and the neighbouring tile is untouched.
  {
    std::vector<WorldTileSource> sources;
    sources.push_back(makeSource("RED", 512));
    sources.push_back(makeSource("GREEN", 512));
    const auto plan = planWorldTiles(sources);
    const auto& batch = plan.batches[0];
    std::vector<std::uint8_t> atlas(static_cast<std::size_t>(batch.albedoBytes), worldAtlasFill);
    worldTileBlit(atlas, batch.width, plan.slots[0].x, plan.slots[0].y, 512, 512,
      solidTile(512, 512, 255, 0, 0));
    worldTileBlit(atlas, batch.width, plan.slots[1].x, plan.slots[1].y, 512, 512,
      solidTile(512, 512, 0, 255, 0));
    const auto& red = plan.slots[0];
    const auto& green = plan.slots[1];
    check("blit.red.centre", channelAt(atlas, batch.width, red.x + 256, red.y + 256, 0) == 255
        && channelAt(atlas, batch.width, red.x + 256, red.y + 256, 1) == 0, "centre of tile 0 is red");
    check("blit.red.border", channelAt(atlas, batch.width, red.x - 1, red.y + 10, 0) == 255
        && channelAt(atlas, batch.width, red.x + red.width, red.y + 10, 0) == 255,
      "tile 0's border replicates its edge");
    check("blit.green.centre", channelAt(atlas, batch.width, green.x + 256, green.y + 256, 1) == 255
        && channelAt(atlas, batch.width, green.x + 256, green.y + 256, 0) == 0,
      "centre of tile 1 is green");
    // The texel between the two tiles belongs to tile 1's left border (the
    // border is written after the tiles, so the later tile owns the seam). The
    // point of the check is that neither tile's *pixels* moved: the tile
    // rectangles the plan reported are the rectangles that were written.
    check("blit.neighbour", channelAt(atlas, batch.width, green.x, green.y + 256, 1) == 255
        && channelAt(atlas, batch.width, red.x + 511, red.y + 256, 0) == 255,
      "each tile's own block holds its own pixels");
  }
  emit("selfTest", failures == 0 ? "true" : "false");
  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 2. the fixture: a material set with a known pixel per material
// ---------------------------------------------------------------------------
int runFixture() {
  // Sixteen materials of distinct solid colours, sized like real tiles, so the
  // gate can ask "which colour is at slot k" and get an answer from the pixels.
  std::vector<WorldTileSource> sources;
  std::vector<std::vector<std::uint8_t>> pixels;
  for (int index = 0; index < 16; ++index) {
    sources.push_back(makeSource("FIXTURE/" + std::to_string(index), 512));
    pixels.push_back(solidTile(512, 512, static_cast<std::uint8_t>(16 * index + 8), 0,
      static_cast<std::uint8_t>(255 - 16 * index)));
  }
  const auto plan = planWorldTiles(sources);
  emitNumber("fixture.materials", static_cast<long long>(sources.size()));
  emitNumber("fixture.tiled", static_cast<long long>(plan.accepted));
  emitNumber("fixture.refused", static_cast<long long>(plan.refused));
  emitNumber("fixture.batches", static_cast<long long>(plan.batches.size()));
  const auto& batch = plan.batches[0];
  std::vector<std::uint8_t> atlas(static_cast<std::size_t>(batch.albedoBytes), worldAtlasFill);
  for (std::size_t index = 0; index < sources.size(); ++index) {
    worldTileBlit(atlas, batch.width, plan.slots[index].x, plan.slots[index].y, 512, 512,
      pixels[index]);
  }
  // The tile the uv resolves to is the tile the plan assigned: walk the centre
  // of each tile through worldTileCoord and read the pixel back.
  std::size_t matched = 0;
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const auto& slot = plan.slots[index];
    const float u = worldTileCoord(256.0f, 512, slot.x, slot.width, batch.width);
    const float v = worldTileCoord(256.0f, 512, slot.y, slot.height, batch.height);
    const auto px = static_cast<std::uint32_t>(u * static_cast<float>(batch.width));
    const auto py = static_cast<std::uint32_t>(v * static_cast<float>(batch.height));
    if (channelAt(atlas, batch.width, px, py, 0) == pixels[index][0]
        && channelAt(atlas, batch.width, px, py, 2) == pixels[index][2]) ++matched;
  }
  emitNumber("fixture.uvMatchesTile", static_cast<long long>(matched));
  check("fixture.uvMatchesTile", matched == sources.size(),
    std::to_string(matched) + " of " + std::to_string(sources.size())
      + " tile centres resolve to their own material");

  // The fallback: a material the plan refused has no slot, so a caller looking
  // it up gets "not found" and draws the flat shade. That is a different branch
  // from "found a tile and sampled the wrong one", which is the defect a
  // grey-background atlas would hide.
  std::vector<WorldTileSource> withBad = sources;
  withBad.push_back(makeSource("FIXTURE/BROKEN", 512, 12));
  const auto badPlan = planWorldTiles(withBad);
  emitNumber("fixture.refusedAfterBad", static_cast<long long>(badPlan.refused));
  check("fixture.fallback", badPlan.refusals.back() == WorldTileRefusal::PixelCountMismatch
      && badPlan.slots.back().batch < 0,
    "a malformed material is refused and has no slot to sample");
  emit("fixtureTest", failures == 0 ? "true" : "false");
  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 3. a real map
// ---------------------------------------------------------------------------
struct Reader {
  std::filesystem::path tfRoot;
  VpkArchiveSet archives;
  std::vector<std::uint8_t> bspBytes;

  std::vector<std::uint8_t> read(const std::string& path) const {
    std::error_code error;
    const auto loose = tfRoot / std::filesystem::u8path(path);
    if (std::filesystem::exists(loose, error)) return readFile(loose);
    std::vector<std::filesystem::path> holders;
    archives.collectContaining(path, holders);
    for (const auto& holder : holders) {
      const auto* archive = archives.find(holder);
      if (!archive) continue;
      auto bytes = archive->read(path);
      if (!bytes.empty()) return bytes;
    }
    std::vector<std::uint8_t> packed;
    if (!bspBytes.empty() && bspPakfileEntry(bspBytes, path, packed, nullptr)) return packed;
    return {};
  }
};

int runMap(const std::filesystem::path& mapPath, const std::filesystem::path& tfRoot,
    const std::string& expectMaterial) {
  const auto bspBytes = readFile(mapPath);
  if (bspBytes.empty()) { std::cerr << "cannot read " << mapPath.string() << "\n"; return 2; }
  BspMap map;
  BspParser::parse(bspBytes, map, 200000);
  if (!map.valid) { std::cerr << "map did not parse: " << map.error << "\n"; return 2; }
  Reader reader;
  reader.tfRoot = tfRoot;
  reader.archives = VpkArchiveSet::openDirectory(tfRoot);
  reader.bspBytes = bspBytes;

  std::unordered_map<std::string, std::size_t> trianglesByMaterial;
  for (const auto& triangle : map.triangles) ++trianglesByMaterial[triangle.material];
  emitNumber("map.distinctMaterials", static_cast<long long>(trianglesByMaterial.size()));
  emitNumber("map.triangles", static_cast<long long>(map.triangles.size()));
  emitNumber("map.vpkEntries", static_cast<long long>(reader.archives.totalEntryCount()));

  // Every distinct material becomes a tile request, sized by the VTF it names
  // and downscaled to 512 the way main.cpp does. The order is coverage order,
  // which is the order the product hands the renderer.
  std::vector<std::pair<std::size_t, std::string>> order;
  for (const auto& entry : trianglesByMaterial) order.emplace_back(entry.second, entry.first);
  std::sort(order.begin(), order.end(), [](const auto& left, const auto& right) {
    return left.first != right.first ? left.first > right.first : left.second < right.second;
  });

  std::vector<WorldTileSource> sources;
  std::vector<VmtMaterial> materials;      // lifetime for the feature scan's pointers
  std::vector<std::string> materialNames;  // every material, in coverage order
  std::vector<std::string> sourceNames;    // the subset that asked for a tile, same order
  std::size_t vmtResolved = 0, vmtMissing = 0, vmtNoBase = 0, vtfResolved = 0, vtfMissing = 0;
  for (const auto& entry : order) {
    const std::string& name = entry.second;
    materialNames.push_back(name);
    VmtMaterial material;
    const auto vmtBytes = reader.read(VmtParser::resourcePath(name, ".vmt"));
    if (vmtBytes.empty()) { ++vmtMissing; materials.push_back(material); continue; }
    if (!VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material)) {
      ++vmtNoBase; materials.push_back(material); continue;
    }
    ++vmtResolved;
    if (material.baseTexture.empty() && material.baseTexture2.empty()) {
      ++vmtNoBase; materials.push_back(material); continue;
    }
    // The tile is the layer that resolved, and its size is the VTF's own,
    // halved the way main.cpp's boxDownscale halves it.
    std::string baseName = material.baseTexture.empty() ? material.baseTexture2 : material.baseTexture;
    const auto vtfBytes = reader.read(VmtParser::resourcePath(baseName, ".vtf"));
    VtfTexture texture;
    if (vtfBytes.empty() || !texture.parse(vtfBytes)) {
      ++vtfMissing; materials.push_back(material); continue;
    }
    ++vtfResolved;
    std::uint32_t width = texture.header().width, height = texture.header().height;
    if (width > 512 || height > 512) {
      const float scale = 512.0f / static_cast<float>(std::max(width, height));
      width = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(static_cast<float>(width) * scale));
      height = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(static_cast<float>(height) * scale));
    }
    WorldTileSource source;
    source.material = name;
    source.width = width;
    source.height = height;
    source.rgbaBytes = static_cast<std::size_t>(width) * height * 4u;
    sources.push_back(std::move(source));
    sourceNames.push_back(name);
    materials.push_back(material);
  }
  emitNumber("map.vmtResolved", static_cast<long long>(vmtResolved));
  emitNumber("map.vmtMissing", static_cast<long long>(vmtMissing));
  emitNumber("map.vmtNoBase", static_cast<long long>(vmtNoBase));
  emitNumber("map.vtfResolved", static_cast<long long>(vtfResolved));
  emitNumber("map.vtfMissing", static_cast<long long>(vtfMissing));

  const auto plan = planWorldTiles(sources);
  emitNumber("plan.tiled", static_cast<long long>(plan.accepted));
  emitNumber("plan.refused", static_cast<long long>(plan.refused));
  emitNumber("plan.batches", static_cast<long long>(plan.batches.size()));
  emitNumber("plan.refusedMalformed", static_cast<long long>(plan.refusedMalformed));
  emitNumber("plan.refusedDuplicate", static_cast<long long>(plan.refusedDuplicate));
  emitNumber("plan.refusedBudget", static_cast<long long>(plan.refusedBudget));
  emitNumber("plan.albedoMiB",
    static_cast<long long>(plan.albedoBytes / (1024ull * 1024ull)));
  emit("plan.reason", plan.reason);
  // The old ceiling, as a number next to the new plan: materials 65 and beyond
  // are the ones the fixed 64-tile cap refused.
  emitNumber("plan.beyondOldCap", static_cast<long long>(plan.accepted > 64 ? plan.accepted - 64 : 0));

  if (!expectMaterial.empty()) {
    bool found = false;
    // A slot is indexed by its position among the *sources*, not among all the
    // materials: the names that never asked for a tile (missing VMT, no
    // $basetexture, missing VTF) push no source, so the two lists drift apart.
    // Indexing the name list read a different material's slot.
    for (std::size_t index = 0; index < sourceNames.size(); ++index) {
      if (sourceNames[index] != expectMaterial) continue;
      const auto& slot = plan.slots[index];
      emit("expect.material", expectMaterial);
      emitNumber("expect.batch", slot.batch);
      emitNumber("expect.tile", slot.tile);
      emitNumber("expect.x", static_cast<long long>(slot.x));
      emitNumber("expect.y", static_cast<long long>(slot.y));
      emit("expect.reason", slot.batch >= 0 ? "tiled" : worldTileRefusalName(plan.refusals[index]));
      found = true;
      break;
    }
    if (!found) { emit("expect.material", "absent"); emit("expect.reason", "not a material of this map"); }
  }

  WorldFeatureResolvers resolvers;
  resolvers.resourceExists = [&](const std::string& path) { return !reader.read(path).empty(); };
  resolvers.readResource = [&](const std::string& path) { return reader.read(path); };
  std::vector<WorldMaterialFeatureInput> inputs;
  inputs.reserve(materials.size());
  for (std::size_t index = 0; index < materials.size(); ++index) {
    WorldMaterialFeatureInput input;
    input.material = materialNames[index];
    input.vmt = &materials[index];
    input.triangles = trianglesByMaterial[materialNames[index]];
    // The probe has no camera. `visible` is what the product's per-material
    // trace adds; here every material counts as coverable geometry only.
    input.visibleTriangles = 0;
    inputs.push_back(std::move(input));
  }
  const auto features = scanWorldMaterialFeatures(inputs, resolvers);
  emit("feature.summary", features.summary);
  emitNumber("feature.bumpmap.declared", static_cast<long long>(features.bumpMap.declared));
  emitNumber("feature.bumpmap.resolved", static_cast<long long>(features.bumpMap.resourcesResolved));
  emitNumber("feature.bumpmap.missing", static_cast<long long>(features.bumpMap.resourcesMissing));
  emitNumber("feature.bumpmap.distinct", static_cast<long long>(features.distinctBumpPaths));
  emit("feature.bumpmap.reason", features.bumpMap.reason);
  emitNumber("feature.bumpmap2.declared", static_cast<long long>(features.bumpMap2.declared));
  emit("feature.bumpmap2.reason", features.bumpMap2.reason);
  emitNumber("feature.ssbump.declared", static_cast<long long>(features.ssBump.declared));
  emit("feature.ssbump.reason", features.ssBump.reason);
  emitNumber("feature.detail.declared", static_cast<long long>(features.detail.declared));
  emitNumber("feature.detail.resolved", static_cast<long long>(features.detail.resourcesResolved));
  emitNumber("feature.detail.types", static_cast<long long>(features.detailTypesRead));
  emit("feature.detail.firstType", features.firstDetailType);
  emit("feature.detail.reason", features.detail.reason);
  emit("mapTest", failures == 0 ? "true" : "false");
  return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  bool selfTest = false, fixture = false;
  std::filesystem::path mapPath, tfRoot;
  std::string expectMaterial;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--self-test") { selfTest = true; continue; }
    if (arg == "--fixture") { fixture = true; continue; }
    if (arg == "--map" && index + 1 < argc) { mapPath = std::filesystem::u8path(argv[++index]); continue; }
    if (arg == "--tf-root" && index + 1 < argc) { tfRoot = std::filesystem::u8path(argv[++index]); continue; }
    if (arg == "--expect-material" && index + 1 < argc) { expectMaterial = argv[++index]; continue; }
    std::cerr << "usage: material_coverage_probe --self-test | --fixture |"
                 " --map <map.bsp> --tf-root <dir> [--expect-material <name>]\n";
    return 2;
  }
  int result = 0;
  if (selfTest) result |= runSelfTest();
  if (fixture) result |= runFixture();
  if (!mapPath.empty()) result |= runMap(mapPath, tfRoot, expectMaterial);
  if (!selfTest && !fixture && mapPath.empty()) {
    std::cerr << "nothing to do: pass --self-test, --fixture or --map\n";
    return 2;
  }
  return result;
}
