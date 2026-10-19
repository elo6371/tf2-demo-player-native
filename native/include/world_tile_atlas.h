#pragma once

// T19 (2026-10-19): the world pass's tile atlas, as a plan rather than a loop.
//
// WHY THIS FILE EXISTS
// --------------------
// The world pass used to own two numbers that could not be wrong in a way
// anybody could see:
//
//   * `worldTextures.size() >= 64` in main.cpp stopped *fetching* VMTs and VTFs
//     after 64 materials, and
//   * `kMaxTiles = 64` in the renderer stopped *placing* tiles after 64,
//     with a `continue` and no counter.
//
// Measured on cp_snakewater_final1 from the demo's own reference frame: 148
// distinct materials, 64 tiled, **51 refused by the cap**, and the trace said
// `capped=51` while nothing said which 51, how much of the frame they cover, or
// why the atlas could not hold them. A drop that only exists as a number is the
// same defect this repo keeps finding: it cannot be attributed, so it cannot be
// fixed or accepted.
//
// So the placement policy is now one pure function with a name for every
// outcome. The renderer builds its textures and its draw batches from the plan;
// the probe runs the same function over a synthetic material set, so a gate
// testing "the atlas holds what it says" is not testing a second implementation
// of the layout.
//
// WHAT IS BOUNDED
// ---------------
// A batch is at most `maxTilesPerBatch` tiles in a grid whose width and height
// are chosen from the count, so a batch of 20 costs 20 tiles and not 64. A plan
// may have several batches (the reference map needs three); each becomes one
// texture and one draw call, and a vertex belongs to exactly one batch, which
// is what keeps one uv valid for one tile. The total is bounded by
// `budgetBytes` per layer -- measured, not guessed: on the reference map the
// three albedo atlases are 156 MiB together, and a budget refusal is named
// `atlas-budget` instead of silently ending the walk.

#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// Why a material did not get a tile. Every branch that used to be a bare
// `continue` has one of these, and the name is what gets printed.
enum class WorldTileRefusal {
  None,
  EmptyMaterial,
  EmptyPixels,
  TileTooLarge,
  PixelCountMismatch,
  DuplicateMaterial,
  AtlasBudgetExceeded,
};

const char* worldTileRefusalName(WorldTileRefusal reason);

// One material the world pass wants a tile for. `blendBytes` / `modulateBytes`
// are the second layer and the blend mask already resampled onto this tile's
// own `width` x `height`; zero means the material did not ship one.
//
// `sourceWidth` / `sourceHeight` are the VTF's dimensions BEFORE the downscale
// to `width` x `height`, and they are what the uv has to be normalised by: a
// texcoord is in source texels, so a 1024-wide texture reduced to a 256-texel
// tile covers a face once only if the tile coordinate is computed from 1024.
// Zero means "the tile is the source", which is the truth for every texture
// that already fit.
struct WorldTileSource {
  std::string material;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t sourceWidth = 0;
  std::uint32_t sourceHeight = 0;
  std::size_t rgbaBytes = 0;
  std::size_t blendBytes = 0;
  bool blendDeclared = false;
  std::size_t modulateBytes = 0;
};

// Where one material's pixels live. `x` and `y` address the tile's first pixel,
// inside the one-texel border the blit replicates, so a tile's block in the
// atlas is [x-1, x+width] x [y-1, y+height].
struct WorldTileSlot {
  int batch = -1;
  int tile = -1;
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // T21: the pre-downscale dimensions the uv must be normalised by. Equal to
  // `width` / `height` for a texture that fit the tile.
  std::uint32_t sourceWidth = 0;
  std::uint32_t sourceHeight = 0;
  bool blend = false;
  bool modulate = false;
};

// One atlas. `columns` x `rows` tiles, each `tileSize` square including its
// border, so `width = columns * tileSize`.
struct WorldTileBatch {
  int tiles = 0;
  int columns = 0;
  int rows = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t albedoBytes = 0;
  int blendTiles = 0;
  int modulateTiles = 0;
};

struct WorldAtlasPlanOptions {
  // 512 texels plus a one-texel replicated border on each side. The caller
  // downscales anything larger to 512, so `tileSize - 2 * border` is the
  // largest tile this plan can hold; a source larger than that is refused by
  // name rather than clipped.
  std::uint32_t tileSize = 514;
  std::uint32_t tileBorder = 1;
  std::uint32_t maxTilesPerBatch = 64;
  // Per layer, over every batch. 384 MiB admits the reference map's three
  // albedo atlases (156 MiB) with room for the two blend layers; a map that
  // needs more is refused by name and the reading says how much it would have
  // cost.
  std::uint64_t budgetBytes = 384ull * 1024ull * 1024ull;
};

struct WorldAtlasPlan {
  // Parallel to the sources handed in, so a caller can look up a material's
  // outcome by the index it passed. `slots[i].batch < 0` means refused and
  // `refusals[i]` names why.
  std::vector<WorldTileSlot> slots;
  std::vector<WorldTileRefusal> refusals;
  std::vector<WorldTileBatch> batches;
  std::size_t accepted = 0;
  std::size_t refused = 0;
  std::size_t refusedMalformed = 0;
  std::size_t refusedDuplicate = 0;
  std::size_t refusedBudget = 0;
  std::uint64_t albedoBytes = 0;
  std::uint64_t blendBytes = 0;
  std::uint64_t modulateBytes = 0;
  // One measured sentence naming the counts, in the style the capability layer
  // uses: a reader can re-derive it from the numbers next to it.
  std::string reason;
};

WorldAtlasPlan planWorldTiles(const std::vector<WorldTileSource>& sources,
  const WorldAtlasPlanOptions& options = {});

// The uv a vertex carries for a tile inside a batch atlas:
//
//   u = (offset + wrap(texcoord, sourceSize) * extent / sourceSize + 0.5) / atlasSize
//
// The `+ 0.5` is the texel centre, and the wrap is what makes a material whose
// texture repeats over a large face keep repeating inside its tile instead of
// sampling the neighbouring tile. Pure, and shared with the probe: a gate that
// re-derives this arithmetic would be checking a copy.
float worldTileCoord(float texcoord, std::uint32_t sourceSize, std::uint32_t offset,
  std::uint32_t extent, std::uint32_t atlasSize);

// Copies a tile into an atlas and replicates its one-texel border. Shared with
// the probe for the same reason as the uv: the gate's "the tile at slot k holds
// material k's pixels" is only about the plan if the copy is the one the
// renderer performs. Requires width + 2 <= tileSize and the tile to sit inside
// the atlas, which is what the plan guarantees.
void worldTileBlit(std::vector<std::uint8_t>& atlas, std::uint32_t atlasWidth,
  std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
  const std::vector<std::uint8_t>& pixels);

// Fills an atlas' background. 88 is the value the world pass has always used:
// a mid-grey that is visibly not a texture, so an unassigned texel is a
// finding rather than a plausible dark patch.
constexpr std::uint8_t worldAtlasFill = 88u;

} // namespace tf2::native
