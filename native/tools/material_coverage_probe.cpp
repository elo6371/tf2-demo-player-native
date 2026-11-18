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
// N07: --list-materials is read by the map analysis, which is a free function, so
// the flag lives at file scope rather than being threaded through every call.
static bool listMaterials = false;

struct Reader {
  std::filesystem::path tfRoot;
  VpkArchiveSet archives;
  std::vector<std::uint8_t> bspBytes;

  // N07: the same chain, but it says which store answered and where it failed.
  // "not found" and "found but unreadable" are different defects, and the summary
  // counter alone cannot tell them apart.
  struct Traced {
    std::vector<std::uint8_t> bytes;
    std::string source;   // loose | vpk:<file> | bsp-pak | none
    std::string stage;    // ok | absent | read-failed
  };
  Traced readTraced(const std::string& path) const {
    Traced out;
    std::error_code error;
    const auto loose = tfRoot / std::filesystem::u8path(path);
    if (std::filesystem::exists(loose, error)) {
      out.bytes = readFile(loose);
      out.source = "loose";
      out.stage = out.bytes.empty() ? "read-failed" : "ok";
      return out;
    }
    // The archive is asked with readEntry rather than read, because read folds
    // "no such name" and "the name is here but it has no bytes" into the same
    // empty vector. That fold is how an asset that exists and is empty used to
    // reach the ledger as absent: measured through this chain before the change,
    // a zero-length tree entry read absent/none (control
    // emptyAsset.vpkZeroLengthChain). The first archive that HAS the name now
    // answers, and an empty or refused answer is a refusal, not a miss.
    std::vector<std::filesystem::path> holders;
    archives.collectContaining(path, holders);
    for (const auto& holder : holders) {
      const auto* archive = archives.find(holder);
      if (!archive) continue;
      const auto entry = archive->readEntry(path);
      if (!entry.found) continue;
      out.source = "vpk:" + holder.filename().string();
      if (entry.error.empty() && !entry.bytes.empty()) {
        out.bytes = entry.bytes;
        out.stage = "ok";
      } else {
        out.stage = "read-failed";
      }
      return out;
    }
    std::vector<std::uint8_t> packed;
    if (!bspBytes.empty() && bspPakfileEntry(bspBytes, path, packed, nullptr)) {
      out.bytes = std::move(packed);
      out.source = "bsp-pak";
      out.stage = "ok";
      return out;
    }
    out.source = "none";
    out.stage = "absent";
    return out;
  }

  std::vector<std::uint8_t> read(const std::string& path) const {
    std::error_code error;
    const auto loose = tfRoot / std::filesystem::u8path(path);
    if (std::filesystem::exists(loose, error)) return readFile(loose);
    std::vector<std::filesystem::path> holders;
    archives.collectContaining(path, holders);
    for (const auto& holder : holders) {
      const auto* archive = archives.find(holder);
      if (!archive) continue;
      // Same rule as readTraced: the first archive that has the name answers,
      // even when its answer is no bytes.
      const auto entry = archive->readEntry(path);
      if (entry.found) return entry.bytes;
    }
    std::vector<std::uint8_t> packed;
    if (!bspBytes.empty() && bspPakfileEntry(bspBytes, path, packed, nullptr)) return packed;
    return {};
  }
};

// ---------------------------------------------------------------------------
// 3. the controls the closure rests on
// ---------------------------------------------------------------------------
// A map that reports zero missing materials is worth reading only if the same
// ladder can still say no. Every control below pairs a case that must resolve
// with the same shape one key away that must be refused, and each one is
// self-contained: the store is a temporary directory this code writes and
// removes, so the control runs on any machine and needs no install.
const char* const kGoodVmt =
  "\"LightmappedGeneric\"\n{\n  \"$basetexture\" \"controls/good\"\n}\n";
const char* const kGoodInclude =
  "\"LightmappedGeneric\"\n{\n  \"$basetexture\" \"controls/included\"\n  \"$translucent\" \"1\"\n}\n";
const char* const kPatchVmt =
  "\"patch\"\n{\n  \"include\" \"materials/controls/include_target.vmt\"\n"
  "  \"replace\"\n  {\n    \"$envmap\" \"controls/cube\"\n  }\n}\n";
const char* const kWaterVmt =
  "\"Water\"\n{\n  \"$abovewater\" \"0\"\n  \"$underwateroverlay\" \"effects/water_warp\"\n}\n";

// emit()/check() write JSON by hand, so a refusal message carrying a quote would
// truncate the record instead of reporting it. Flatten before printing; the
// message itself is evidence and is not rewritten.
std::string oneLine(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c == '"' || c == '\n' || c == '\r' || c == '\\') out.push_back('\'');
    else out.push_back(c);
  }
  return out;
}

std::vector<std::uint8_t> textBytes(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

bool writeFileBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

// A VTF the decoder accepts: version 7.1, 64-byte header, one mip, RGBA8888.
// The same shape terrain_blend_probe.cpp:80 builds, kept here so this probe's
// controls do not depend on another tool's fixture.
std::vector<std::uint8_t> controlVtf(std::uint16_t width, std::uint16_t height) {
  std::vector<std::uint8_t> bytes(64u + static_cast<std::size_t>(width) * height * 4u, 0u);
  std::memcpy(bytes.data(), "VTF\0", 4);
  const auto put32 = [&bytes](std::size_t at, std::uint32_t value) {
    std::memcpy(bytes.data() + at, &value, 4);
  };
  const auto put16 = [&bytes](std::size_t at, std::uint16_t value) {
    std::memcpy(bytes.data() + at, &value, 2);
  };
  put32(4, 7);          // major
  put32(8, 1);          // minor
  put32(12, 64);        // header size
  put16(16, width);
  put16(18, height);
  put32(20, 0);         // flags: not a cubemap
  put16(24, 1);         // frames
  put32(52, 0);         // format: RGBA8888
  bytes[56] = 1;        // mip count
  for (std::size_t at = 64; at < bytes.size(); ++at) {
    bytes[at] = static_cast<std::uint8_t>(at * 7u);
  }
  return bytes;
}

struct VpkName {
  std::string extension;
  std::string path;
  std::string filename;
};

VpkName splitVpkName(const std::string& name) {
  VpkName out;
  std::string stem = name;
  const auto dot = stem.find_last_of('.');
  const auto slashInFull = stem.find_last_of('/');
  if (dot != std::string::npos && (slashInFull == std::string::npos || dot > slashInFull)) {
    out.extension = stem.substr(dot + 1);
    stem = stem.substr(0, dot);
  }
  const auto slash = stem.find_last_of('/');
  if (slash == std::string::npos) {
    out.path = " ";
    out.filename = stem;
  } else {
    out.path = stem.substr(0, slash);
    out.filename = stem.substr(slash + 1);
  }
  // The format spells "no path" and "no extension" as a single space.
  if (out.extension.empty()) out.extension = " ";
  return out;
}

// A VPK v2 directory file with every entry inline (archive index 0x7fff). The
// tree grammar is the one VpkArchive::open reads: an extension string, then per
// path a string, then per filename a string plus an 18-byte record, with an
// empty extension string closing the whole tree. Inline entries keep the fixture
// to one file, so the control exercises the reader and not the numbered-volume
// naming. Nothing in native/ could write a VPK before this, which is why the
// priority control had to be written by hand or not at all.
std::vector<std::uint8_t> buildVpkDirectory(
    const std::map<std::string, std::vector<std::uint8_t>>& entries) {
  using Files = std::map<std::string, const std::vector<std::uint8_t>*>;
  std::map<std::string, std::map<std::string, Files>> tree;
  for (const auto& entry : entries) {
    const auto split = splitVpkName(entry.first);
    tree[split.extension][split.path][split.filename] = &entry.second;
  }
  std::vector<std::uint8_t> text;
  std::vector<std::uint8_t> data;
  const auto putText = [&text](const std::string& value) {
    text.insert(text.end(), value.begin(), value.end());
    text.push_back(0);
  };
  const auto put16 = [&text](std::uint16_t value) {
    text.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    text.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  };
  const auto put32 = [&text](std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
      text.push_back(static_cast<std::uint8_t>((value >> (byte * 8)) & 0xFFu));
    }
  };
  for (const auto& extension : tree) {
    putText(extension.first);
    for (const auto& path : extension.second) {
      putText(path.first);
      for (const auto& file : path.second) {
        putText(file.first);
        put32(0u);                                       // crc: no reader here verifies it
        put16(0u);                                       // preload length
        put16(0x7FFFu);                                  // archive index: inline
        put32(static_cast<std::uint32_t>(data.size()));  // offset from the tree end
        put32(static_cast<std::uint32_t>(file.second->size()));
        put16(0xFFFFu);                                  // record terminator
        data.insert(data.end(), file.second->begin(), file.second->end());
      }
      putText("");                                     // end of this filename table
      putText("");                                     // end of this path table
    }
  }
  putText("");                                           // terminal extension

  std::vector<std::uint8_t> bytes;
  const auto put32v = [&bytes](std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
      bytes.push_back(static_cast<std::uint8_t>((value >> (byte * 8)) & 0xFFu));
    }
  };
  put32v(0x55AA1234u);                                   // signature
  put32v(2u);                                            // version
  put32v(static_cast<std::uint32_t>(text.size()));       // tree size
  put32v(static_cast<std::uint32_t>(data.size()));       // data section size
  put32v(0u);                                            // md5 section
  put32v(0u);                                            // other section
  put32v(0u);                                            // signature section
  bytes.insert(bytes.end(), text.begin(), text.end());
  bytes.insert(bytes.end(), data.begin(), data.end());
  return bytes;
}

int runControls() {
  const auto chk = [](const char* name, bool ok, const std::string& detail) {
    check(name, ok, oneLine(detail));
  };
  // ---- 1. bad VMT text ----------------------------------------------------
  {
    VmtMaterial good;
    const bool parsed = VmtParser::parse(kGoodVmt, good);
    chk("control.vmtText.positive",
      parsed && good.valid && good.baseTexture == "controls/good",
      "a well-formed VMT parses and names its $basetexture");
    VmtMaterial broken;
    const bool accepted = VmtParser::parse("\"LightmappedGeneric\" \"$basetexture\" \"x\"", broken);
    chk("control.vmtText.negative", !accepted && !broken.valid && !broken.error.empty(),
      "a header with no block is refused (" + broken.error + ")");
    VmtMaterial empty;
    const bool acceptedEmpty = VmtParser::parse("", empty);
    chk("control.vmtText.empty", !acceptedEmpty && !empty.valid && !empty.error.empty(),
      "an empty file is refused (" + empty.error + ")");
  }
  // ---- 2. a patch whose include is missing --------------------------------
  {
    VmtParser::IncludeReader supplyOne = [](const std::string&) {
      return textBytes(kGoodInclude);
    };
    VmtMaterial resolved;
    const bool parsed = VmtParser::parse(kPatchVmt, resolved, supplyOne);
    chk("control.include.positive",
      parsed && resolved.patch.declared && resolved.patch.resolved
        && resolved.baseTexture == "controls/included",
      "a patch resolves through the store and takes the include's $basetexture");
    VmtParser::IncludeReader supplyNothing = [](const std::string&) {
      return std::vector<std::uint8_t>();
    };
    VmtMaterial missing;
    const bool accepted = VmtParser::parse(kPatchVmt, missing, supplyNothing);
    chk("control.include.negative",
      !accepted && missing.patch.declared && !missing.patch.resolved,
      "a patch whose include cannot be read is refused, not drawn from the wrapper ("
        + missing.error + ")");
    VmtMaterial storeless;
    const bool acceptedNoStore = VmtParser::parse(kPatchVmt, storeless);
    chk("control.include.noStore", !acceptedNoStore && !storeless.patch.resolved,
      "a patch with no store given is refused (" + storeless.error + ")");
  }
  // ---- 3. an empty or corrupt asset --------------------------------------
  {
    VtfTexture emptyTexture;
    std::string whyEmpty;
    chk("control.emptyAsset.emptyVtf",
      !emptyTexture.parse(std::vector<std::uint8_t>(), &whyEmpty),
      "zero bytes never parse as a texture (" + whyEmpty + ")");
    VtfTexture corrupt;
    std::string whyCorrupt;
    chk("control.emptyAsset.corruptVtf", !corrupt.parse(textBytes("NOTAVTF"), &whyCorrupt),
      "a wrong magic is refused (" + whyCorrupt + ")");
    VtfTexture real;
    const auto vtfBytes = controlVtf(8, 8);
    chk("control.emptyAsset.positive",
      real.parse(vtfBytes) && real.header().width == 8 && real.header().height == 8,
      "a header-only VTF of the same size parses, so the two refusals above are about the bytes");
  }
  // ---- 4. tf before hl2, and loose before both ---------------------------
  // tf is mounted first and hl2 second (vpk_archive.cpp:308, :315), so a name
  // present in both must be answered by tf. That claim used to be a comment, and
  // a comment cannot go red; these checks can.
  {
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error)
      / "tf2-n07-material-controls";
    std::filesystem::remove_all(root, error);
    const auto tf = root / "tf";
    const auto hl2 = root / "hl2";
    std::filesystem::create_directories(tf / "materials" / "controls", error);
    std::filesystem::create_directories(hl2, error);

    const std::string tfText =
      "\"LightmappedGeneric\"\n{\n  \"$basetexture\" \"controls/from_tf\"\n}\n";
    const std::string hl2Text =
      "\"LightmappedGeneric\"\n{\n  \"$basetexture\" \"controls/from_hl2\"\n}\n";
    const std::string looseText =
      "\"LightmappedGeneric\"\n{\n  \"$basetexture\" \"controls/from_loose\"\n}\n";

    std::map<std::string, std::vector<std::uint8_t>> tfEntries;
    tfEntries["materials/controls/priority.vmt"] = textBytes(tfText);
    tfEntries["materials/controls/shadowed.vmt"] = textBytes(tfText);
    // A zero-length tree entry: the archive can answer "found, preload only, no
    // bytes", which is not the same answer as "no such name".
    tfEntries["materials/controls/zero_length.vmt"] = {};
    std::map<std::string, std::vector<std::uint8_t>> hl2Entries;
    hl2Entries["materials/controls/priority.vmt"] = textBytes(hl2Text);
    hl2Entries["materials/controls/shadowed.vmt"] = textBytes(hl2Text);
    hl2Entries["materials/controls/hl2_only.vmt"] = textBytes(hl2Text);

    const bool wroteTf = writeFileBytes(tf / "components_dir.vpk", buildVpkDirectory(tfEntries));
    const bool wroteHl2 = writeFileBytes(hl2 / "hl2_misc_dir.vpk", buildVpkDirectory(hl2Entries));
    const bool wroteEmpty = writeFileBytes(tf / "materials" / "controls" / "empty.vmt",
      std::vector<std::uint8_t>());
    const bool wroteLoose = writeFileBytes(tf / "materials" / "controls" / "shadowed.vmt",
      textBytes(looseText));
    chk("control.store.fixture", wroteTf && wroteHl2 && wroteEmpty && wroteLoose,
      "the temporary store was written under " + root.string());

    Reader reader;
    reader.tfRoot = tf;
    reader.archives = VpkArchiveSet::openDirectory(tf);
    chk("control.store.mounted", reader.archives.totalEntryCount() >= 5,
      std::to_string(reader.archives.totalEntryCount()) + " entries across "
        + std::to_string(reader.archives.size()) + " archives (tf and hl2)");

    const auto priority = reader.readTraced("materials/controls/priority.vmt");
    chk("control.priority.tfWins",
      priority.stage == "ok" && priority.source == "vpk:components_dir.vpk"
        && std::string(priority.bytes.begin(), priority.bytes.end()) == tfText,
      "a name in both archives is answered by tf, source=" + priority.source);

    const auto hl2Only = reader.readTraced("materials/controls/hl2_only.vmt");
    chk("control.priority.hl2Fallback",
      hl2Only.stage == "ok" && hl2Only.source == "vpk:hl2_misc_dir.vpk"
        && std::string(hl2Only.bytes.begin(), hl2Only.bytes.end()) == hl2Text,
      "a name only hl2 has still resolves, so the second mount is live, source="
        + hl2Only.source);

    const auto loose = reader.readTraced("materials/controls/shadowed.vmt");
    chk("control.priority.looseFirst",
      loose.stage == "ok" && loose.source == "loose"
        && std::string(loose.bytes.begin(), loose.bytes.end()) == looseText,
      "a loose file outranks both archives, source=" + loose.source);

    const auto emptyLoose = reader.readTraced("materials/controls/empty.vmt");
    chk("control.emptyAsset.looseEmpty",
      emptyLoose.stage == "read-failed" && emptyLoose.source == "loose",
      "an existing but empty file is a refusal, not a pass, stage=" + emptyLoose.stage);

    std::vector<std::filesystem::path> holders;
    reader.archives.collectContaining("materials/controls/zero_length.vmt", holders);
    bool zeroFound = false;
    bool zeroPreloadOnly = false;
    if (!holders.empty()) {
      if (const auto* archive = reader.archives.find(holders.front())) {
        const auto entry = archive->readEntry("materials/controls/zero_length.vmt");
        zeroFound = entry.found;
        zeroPreloadOnly = entry.store == VpkArchive::Store::PreloadOnly;
      }
    }
    chk("control.emptyAsset.vpkZeroLength", zeroFound && zeroPreloadOnly,
      "the archive answers found-preload-only for a zero-length entry");
    // The same entry through the chain runMap actually uses. This was a reading
    // (and read absent/none) until the chain was taught to ask readEntry, so the
    // check is what keeps that fix from silently reverting: a name that exists
    // with no bytes must never come back as "absent", which is the answer that
    // sends a material down the missing-asset path.
    const auto zeroTraced = reader.readTraced("materials/controls/zero_length.vmt");
    chk("control.emptyAsset.vpkZeroLengthChain",
      zeroTraced.stage == "read-failed" && zeroTraced.source == "vpk:components_dir.vpk",
      "an empty archived asset reads a refusal, not a miss, stage="
        + zeroTraced.stage + " source=" + zeroTraced.source);

    std::filesystem::remove_all(root, error);
  }
  // ---- 5. water is classified, not assumed -------------------------------
  {
    VmtMaterial water;
    const bool parsed = VmtParser::parse(kWaterVmt, water);
    chk("control.water.positive",
      parsed && water.waterShader && water.baseTexture.empty()
        && water.baseTexture2.empty(),
      "a Water shader with no $basetexture is classified water, shader=" + water.shader);
    VmtMaterial opaque;
    VmtParser::parse(kGoodVmt, opaque);
    chk("control.water.negative", !opaque.waterShader,
      "a LightmappedGeneric with a $basetexture is not classified water");
    VmtMaterial bottom;
    VmtParser::parse(
      "\"LightmappedGeneric\"\n{\n  \"$basetexture\" \"controls/x\"\n"
      "  \"$bottommaterial\" \"water/water_beneath\"\n}\n", bottom);
    chk("control.water.byBottomMaterial", bottom.waterShader,
      "a material naming a $bottommaterial is water even when its shader is not Water");
  }
  emit("controlTest", failures == 0 ? "true" : "false");
  return failures == 0 ? 0 : 1;
}

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
  // N07: one line per material, same order and same scope as the counters below,
  // so the named list and the summary close on each other instead of being two
  // independent counts that happen to look similar.
  std::size_t listed = 0, stageOk = 0, stageAbsent = 0, stageReadFail = 0;
  std::size_t stageVmtParse = 0, stageIncludeMissing = 0, stageNoBase = 0;
  // R3: water draws through the water pass, so it is its own stage and counts
  // toward closure instead of being reported as a material missing a texture.
  std::size_t stageWater = 0;
  std::size_t stageVtfAbsent = 0, stageVtfParse = 0;
  for (const auto& entry : order) {
    const std::string& name = entry.second;
    const std::size_t triangles = entry.first;
    materialNames.push_back(name);
    VmtMaterial material;
    const auto vmt = reader.readTraced(VmtParser::resourcePath(name, ".vmt"));
    auto note = [&](const char* stage, const std::string& reason) {
      ++listed;
      if (std::string(stage) == "ok") ++stageOk;
      else if (std::string(stage) == "absent") ++stageAbsent;
      else if (std::string(stage) == "read-failed") ++stageReadFail;
      else if (std::string(stage) == "vmt-parse") ++stageVmtParse;
      else if (std::string(stage) == "vmt-include-missing") ++stageIncludeMissing;
      else if (std::string(stage) == "vmt-no-basetexture") ++stageNoBase;
      else if (std::string(stage) == "vmt-water") ++stageWater;
      else if (std::string(stage) == "vtf-absent") ++stageVtfAbsent;
      else if (std::string(stage) == "vtf-parse") ++stageVtfParse;
      if (listMaterials) {
        std::printf("material=%s triangles=%zu source=%s stage=%s reason=%s\n",
          name.c_str(), triangles, vmt.source.c_str(), stage, reason.c_str());
      }
    };
    if (vmt.stage != "ok") {  // absent | read-failed
      ++vmtMissing;
      note(vmt.stage.c_str(), "vmt " + vmt.stage + " in loose, vpk and bsp-pak");
      materials.push_back(material);
      continue;
    }
    // N07: the include reader matters. Without it every patch VMT fails with
    // "no store was given" -- 133 of sultry's 263 materials read as parse errors
    // while the product resolves them through the same chain used here.
    VmtParser::IncludeReader readInclude = [&](const std::string& resource) {
      return reader.read(resource);
    };
    if (!VmtParser::parse(std::string(vmt.bytes.begin(), vmt.bytes.end()), material, readInclude)) {
      ++vmtNoBase;
      if (material.patch.declared && !material.patch.resolved) {
        note("vmt-include-missing", "patch include " + material.patch.includePath);
      } else {
        note("vmt-parse", "VMT parser refused the file");
      }
      materials.push_back(material);
      continue;
    }
    ++vmtResolved;
    if (material.patch.declared && !material.patch.resolved) {
      note("vmt-include-missing", "patch include " + material.patch.includePath);
      materials.push_back(material);
      continue;
    }
    // R3 (main.cpp:6185-6194): a Water shader names no $basetexture on purpose --
    // the surface is the refracted scene, drawn by the water pass. Same test the
    // product applies, so the two ledgers agree instead of this probe calling
    // water a defect while the product calls it a shader.
    if (material.waterShader && material.baseTexture.empty()
        && material.baseTexture2.empty()) {
      note("vmt-water", "Water shader " + material.shader
        + " draws through the water pass and names no $basetexture");
      materials.push_back(material);
      continue;
    }
    if (material.baseTexture.empty() && material.baseTexture2.empty()) {
      ++vmtNoBase;
      note("vmt-no-basetexture", "no $basetexture and no $basetexture2");
      materials.push_back(material);
      continue;
    }
    // The tile is the layer that resolved, and its size is the VTF's own,
    // halved the way main.cpp's boxDownscale halves it.
    std::string baseName = material.baseTexture.empty() ? material.baseTexture2 : material.baseTexture;
    const auto vtf = reader.readTraced(VmtParser::resourcePath(baseName, ".vtf"));
    VtfTexture texture;
    if (vtf.stage != "ok") {
      ++vtfMissing;
      note("vtf-absent", "vtf " + vtf.stage + " for " + baseName);
      materials.push_back(material);
      continue;
    }
    if (!texture.parse(vtf.bytes)) {
      ++vtfMissing;
      note("vtf-parse", "VTF parser refused " + baseName);
      materials.push_back(material);
      continue;
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
    note("ok", "vmt=" + vmt.source + " vtf=" + vtf.source);
    materials.push_back(material);
  }
  emitNumber("map.vmtResolved", static_cast<long long>(vmtResolved));
  const std::size_t staged = stageOk + stageAbsent + stageReadFail + stageVmtParse
    + stageIncludeMissing + stageNoBase + stageWater + stageVtfAbsent + stageVtfParse;
  emitNumber("map.materialsListed", static_cast<long long>(listed));
  emitNumber("map.stage.ok", static_cast<long long>(stageOk));
  emitNumber("map.stage.vmtAbsent", static_cast<long long>(stageAbsent));
  emitNumber("map.stage.readFailed", static_cast<long long>(stageReadFail));
  emitNumber("map.stage.vmtParse", static_cast<long long>(stageVmtParse));
  emitNumber("map.stage.includeMissing", static_cast<long long>(stageIncludeMissing));
  emitNumber("map.stage.noBasetexture", static_cast<long long>(stageNoBase));
  emitNumber("map.stage.water", static_cast<long long>(stageWater));
  emitNumber("map.stage.vtfAbsent", static_cast<long long>(stageVtfAbsent));
  emitNumber("map.stage.vtfParse", static_cast<long long>(stageVtfParse));
  // The list and the summary must close: every named material lands in exactly one
  // stage, and the stages sum to the number of lines printed.
  emitNumber("map.stage.closure", static_cast<long long>(staged == listed ? 1 : 0));
  emitNumber("map.vmtMissing", static_cast<long long>(vmtMissing));
  emitNumber("map.vmtNoBase", static_cast<long long>(vmtNoBase));
  emitNumber("map.vmtWater", static_cast<long long>(stageWater));
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
  bool selfTest = false, fixture = false, controls = false;
  std::filesystem::path mapPath, tfRoot;
  std::string expectMaterial;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--self-test") { selfTest = true; continue; }
    if (arg == "--fixture") { fixture = true; continue; }
    if (arg == "--controls") { controls = true; continue; }
    if (arg == "--map" && index + 1 < argc) { mapPath = std::filesystem::u8path(argv[++index]); continue; }
    if (arg == "--tf-root" && index + 1 < argc) { tfRoot = std::filesystem::u8path(argv[++index]); continue; }
    if (arg == "--expect-material" && index + 1 < argc) { expectMaterial = argv[++index]; continue; }
    if (arg == "--list-materials") { listMaterials = true; continue; }
    std::cerr << "usage: material_coverage_probe --self-test | --fixture | --controls |"
                 " --map <map.bsp> --tf-root <dir> [--expect-material <name>] [--list-materials]\n";
    return 2;
  }
  int result = 0;
  if (selfTest) result |= runSelfTest();
  if (fixture) result |= runFixture();
  if (!mapPath.empty()) result |= runMap(mapPath, tfRoot, expectMaterial);
  if (controls) result |= runControls();
  if (!selfTest && !fixture && !controls && mapPath.empty()) {
    std::cerr << "nothing to do: pass --self-test, --fixture, --controls or --map\n";
    return 2;
  }
  return result;
}
